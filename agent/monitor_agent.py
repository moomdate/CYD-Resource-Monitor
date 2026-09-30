#!/usr/bin/env python3
"""CYD Resource Monitor agent.

Reads system stats and streams them as JSON lines over USB serial
to the ESP32 display. Works on Windows, macOS and Linux.

  python monitor_agent.py               # auto-detect port, 2 Hz
  python monitor_agent.py --list        # list serial ports
  python monitor_agent.py --port COM5   # explicit port
  python monitor_agent.py --print       # dry run: print JSON, no serial

GPU and temperature sources are in hwinfo.py (every field is optional - the display shows
"--" for what is missing). Out of the box, with no extras:
  - GPU load, any vendor:   Windows (PDH counters), Linux (amdgpu / i915 / xe sysfs), macOS (ioreg)
  - temperatures:           macOS Apple Silicon (CPU, GPU, SSD), Linux (psutil + amdgpu hwmon)
Optional extras for more:
  - NVIDIA (temp, power, clock, fan, VRAM): pip install nvidia-ml-py  (or nvidia-smi on PATH)
  - Windows temps (CPU / GPU / drive), CPU power / voltage / fan, drive activity:
                            run LibreHardwareMonitor with Options > Remote Web Server
                            enabled (default http://localhost:8085)
  - Windows AMD GPU temp without LibreHardwareMonitor: pip install pyadl
  - Intel Mac CPU temp:     brew install smctemp

Protocol (one JSON object per line, all keys but "cpu"/"ram"/"net"/"disk" optional):
  cpu   {load, freq, cores, temp, name, power, volt, fan, per_core[<=16]}
  ram   {pct, used, total}
  gpus  [{name, load, temp, vram_used, vram_total, discrete, power, clock, fan}]   (<= 2, discrete first)
  disk  {pct, act, r, w, temp, model}      pct = capacity used, act = busy %, r/w = MiB/s
  net   {dl, ul}                           MiB/s
  host  {name, os, clock}                  clock = local seconds since midnight
Keys added after the first firmware (name, power, volt, fan, per_core, act, temp, model,
clock) are simply ignored by older firmware, and older agents work with the new firmware.
"""

import argparse
import functools
import json
import math
import os
import platform
import re
import shutil
import socket
import subprocess
import sys
print = functools.partial(print, flush=True)
import time

import psutil

import hwinfo

IS_WIN = platform.system() == "Windows"
IS_MAC = platform.system() == "Darwin"
IS_LINUX = platform.system() == "Linux"

MAX_CORES = 16   # the display draws at most this many per-core bars

# ── LibreHardwareMonitor web server (optional, Windows) ────────
# LHM's data.json is a tree: computer > hardware (CPU, GPU, drive...) > sensor group > sensor.
# Hardware nodes carry an icon (cpu.png, hdd.png, mainboard.png, ...) that tells us what they are.
HW_ICONS = ("cpu", "hdd", "mainboard", "chip", "ram", "nvidia", "ati", "intel", "nic")


def lhm_fetch(url):
    """Return a flat list of sensors from LHM's data.json tree, or None if it is not reachable.
    Each sensor is (path, value, unit, hardware_kind, hardware_name)."""
    import urllib.request
    try:
        with urllib.request.urlopen(url, timeout=1) as r:
            tree = json.load(r)
    except Exception:
        return None
    return lhm_flatten(tree)


def lhm_flatten(tree):
    flat = []

    def walk(node, path, hw_kind, hw_name):
        text = node.get("Text", "")
        icon = str(node.get("ImageURL", "")).lower()
        for k in HW_ICONS:                          # a node whose icon is a hardware icon is a device
            if icon.endswith(k + ".png"):
                hw_kind, hw_name = k, text
                break
        val = node.get("Value", "")
        p = path + [text]
        if val:
            m = re.match(r"([\d.,]+)\s*(.*)", str(val))
            if m:
                try:
                    flat.append(("/".join(p), float(m.group(1).replace(",", ".")), m.group(2).strip(),
                                 hw_kind, hw_name))
                except ValueError:
                    pass
        for ch in node.get("Children", []):
            walk(ch, p, hw_kind, hw_name)

    walk(tree, [], "", "")
    return flat


def lhm_extract(flat):
    """Pull CPU temp / power / voltage / fan, drive info and non-NVIDIA GPU stats out of the LHM sensor list."""
    out = {"cpu_temp": None, "cpu_power": None, "cpu_volt": None, "cpu_fan": None, "disk": {}, "gpus": []}
    if not flat:
        return out

    # CPU package/core temperature
    for path, v, unit, kind, hw in flat:
        low = path.lower()
        if unit == "°C" and (kind == "cpu" or "cpu" in low) and \
           ("package" in low or "core (tctl" in low or "core average" in low):
            out["cpu_temp"] = v
            break
    if out["cpu_temp"] is None:
        cands = [v for p, v, u, k, h in flat if u == "°C" and (k == "cpu" or "cpu" in p.lower())]
        if cands:
            out["cpu_temp"] = max(cands)

    # CPU package power, core voltage
    for path, v, unit, kind, hw in flat:
        low = path.lower()
        if kind == "cpu" and unit == "W" and "package" in low and out["cpu_power"] is None:
            out["cpu_power"] = v
        if kind == "cpu" and unit == "V" and out["cpu_volt"] is None and ("cpu core" in low or "core (svi2" in low):
            out["cpu_volt"] = v
    if out["cpu_volt"] is None:                     # Intel: "Core #1 VID" ... - take the highest
        vids = [v for p, v, u, k, h in flat if k == "cpu" and u == "V"]
        if vids:
            out["cpu_volt"] = max(vids)

    # CPU fan: a fan whose name mentions the CPU, on the board / Super I/O chip
    for path, v, unit, kind, hw in flat:
        if unit == "RPM" and kind in ("mainboard", "chip") and "cpu" in path.lower().split("/")[-1]:
            out["cpu_fan"] = v
            break

    # First drive: name, temperature, total activity
    for path, v, unit, kind, hw in flat:
        if kind != "hdd":
            continue
        d = out["disk"]
        if not d:
            d["name"] = hw
        if d.get("name") != hw:
            continue                                # only the first drive
        last = path.lower().split("/")[-1]
        if unit == "°C" and "temp" not in d:
            d["temp"] = v
        if unit == "%" and "total activity" in last:
            d["act"] = v

    # GPUs of every vendor: hardware nodes with a GPU icon or GPU-ish name. NVIDIA cards also come
    # from NVML with more detail; hwinfo.merge_gpus() folds the two readings of one card together.
    gpu_names = []
    for path, v, unit, kind, hw in flat:
        low = hw.lower()
        if not hw or hw in gpu_names or kind == "cpu":
            continue
        if kind in ("ati", "nvidia") or any(k in low for k in ("radeon", "geforce", "graphics", "gpu", " arc")):
            gpu_names.append(hw)
    for name in gpu_names:
        low = name.lower()
        integrated = ("intel" in low and "arc" not in low) or ("radeon" in low and "graphics" in low
                                                                and " rx " not in f" {low} ")
        g = {"name": hwinfo.short_gpu_name(name), "discrete": not integrated}
        for path, v, unit, kind, hw in flat:
            if hw != name:
                continue
            low = path.lower()
            if unit == "%" and ("gpu core" in low or "d3d 3d" in low) and "load" not in g:
                g["load"] = v
            if unit == "°C" and "temp" not in g:
                g["temp"] = v
        out["gpus"].append(g)
    return out


# ── macOS helpers ──────────────────────────────────────────────
def mac_disk_model():
    try:
        r = subprocess.run(["system_profiler", "-json", "SPNVMeDataType"],
                           capture_output=True, text=True, timeout=10)
        for ctrl in json.loads(r.stdout).get("SPNVMeDataType", []):
            for item in ctrl.get("_items", []):
                if item.get("device_model"):
                    return item["device_model"]
    except Exception:
        pass
    try:
        r = subprocess.run(["diskutil", "info", "/"], capture_output=True, text=True, timeout=5)
        m = re.search(r"Media Name:\s*(.+)", r.stdout)
        if m:
            return m.group(1).strip()
    except Exception:
        pass
    return None


# ── Linux helpers ──────────────────────────────────────────────
def linux_sensors():
    """(cpu_temp, cpu_fan, nvme_temp) from psutil's sensor API; any may be None."""
    cpu_temp = fan = nvme = None
    try:
        temps = psutil.sensors_temperatures()
    except Exception:
        temps = {}
    for chip in ("coretemp", "k10temp", "zenpower", "cpu_thermal", "cpu-thermal", "acpitz"):
        entries = temps.get(chip) or []
        if not entries:
            continue
        pick = next((e for e in entries if any(k in (e.label or "").lower() for k in ("package", "tctl", "tdie"))),
                    entries[0])
        cpu_temp = pick.current
        break
    ent = temps.get("nvme") or []
    if ent:
        pick = next((e for e in ent if "composite" in (e.label or "").lower()), ent[0])
        nvme = pick.current
    try:
        for entries in psutil.sensors_fans().values():
            if entries:
                fan = entries[0].current
                break
    except Exception:
        pass
    return cpu_temp, fan, nvme


def linux_disk_model():
    try:
        devs = sorted(os.listdir("/sys/block"))
    except OSError:
        return None
    devs.sort(key=lambda d: not d.startswith("nvme"))          # NVMe first
    for d in devs:
        if d.startswith(("loop", "ram", "zram", "dm-", "md", "sr")):
            continue
        try:
            with open(f"/sys/block/{d}/device/model") as f:
                name = f.read().strip()
            if name:
                return name
        except OSError:
            continue
    return None


# ── identity helpers ───────────────────────────────────────────
_ASCII_MAP = {"\u2018": "'", "\u2019": "'", "\u201c": '"', "\u201d": '"', "\u2013": "-", "\u2014": "-",
              "\u00a0": " ", "\u2122": "", "\u00ae": ""}


def to_ascii(s):
    """The display's fonts are ASCII: 'Surasak\u2019s MacBook' -> "Surasak's MacBook"."""
    s = "".join(_ASCII_MAP.get(c, c) for c in (s or ""))
    return "".join(c for c in s if 32 <= ord(c) < 127).strip()

def clean_cpu_name(raw):
    """'AMD Ryzen 7 7800X3D 8-Core Processor' -> 'AMD Ryzen 7 7800X3D' (fits the display)."""
    s = re.sub(r"\((?:R|TM)\)", "", raw or "", flags=re.I)
    s = re.sub(r"\b(?:CPU|Processor)\b", "", s, flags=re.I)
    s = re.sub(r"@\s*[\d.]+\s*[GM]Hz", "", s, flags=re.I)
    s = re.sub(r"\b\d+-Core\b", "", s, flags=re.I)
    return re.sub(r"\s+", " ", s).strip()[:27]


def cpu_name():
    raw = ""
    try:
        if IS_WIN:
            import winreg
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                                r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as k:
                raw = winreg.QueryValueEx(k, "ProcessorNameString")[0]
        elif IS_MAC:
            raw = subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"],
                                 capture_output=True, text=True, timeout=2).stdout
        else:
            with open("/proc/cpuinfo") as f:
                for line in f:
                    if line.lower().startswith("model name"):
                        raw = line.split(":", 1)[1]
                        break
    except Exception:
        pass
    return clean_cpu_name(to_ascii(raw or platform.processor()))


def host_name():
    name = to_ascii(socket.gethostname().split(".")[0])
    if name and not name.isdigit():
        return name[:20]
    if IS_MAC:  # hostname can be an IP on some networks; ask macOS directly
        try:
            r = subprocess.run(["scutil", "--get", "ComputerName"],
                               capture_output=True, text=True, timeout=2)
            if to_ascii(r.stdout):
                return to_ascii(r.stdout)[:20]
        except Exception:
            pass
    return "Mac" if IS_MAC else "PC"


# ── payload helpers ────────────────────────────────────────────
def group_cores(values, n=MAX_CORES):
    """Per-core loads as ints; more than n logical CPUs are averaged into n groups."""
    vals = [max(0.0, min(100.0, float(v))) for v in values]
    if len(vals) > n:
        size = len(vals) / n
        vals = [sum(vals[int(i * size):int((i + 1) * size)]) / max(1, len(vals[int(i * size):int((i + 1) * size)]))
                for i in range(n)]
    return [int(round(v)) for v in vals]


class ActivityEstimator:
    """Drive activity % when the OS gives no busy-time counter: throughput relative to the
    highest throughput seen this session (never below `floor` MiB/s). An estimate, not a measurement."""

    def __init__(self, floor=150.0):
        self.peak = floor

    def update(self, mib_per_s):
        self.peak = max(self.peak, mib_per_s)
        return min(100.0, 100.0 * mib_per_s / self.peak)


def clean(obj):
    """Drop None and non-finite numbers, recursively, so the firmware sees 'absent' instead of NaN."""
    if isinstance(obj, dict):
        out = {}
        for k, v in obj.items():
            v = clean(v)
            if v is None or v == {}:
                continue
            out[k] = v
        return out
    if isinstance(obj, list):
        return [x for x in (clean(v) for v in obj) if x is not None]
    if isinstance(obj, float) and not math.isfinite(obj):
        return None
    return obj


# ── payload assembly ───────────────────────────────────────────
class StaticHub:
    """Stand-in for hwinfo.HardwareHub with fixed readings (tests, --no-hw)."""

    def __init__(self, snapshot=None):
        self.data = snapshot or {}

    def snapshot(self):
        return dict(self.data)


def gpu_payload(g):
    """Round a merged GPU dict for the wire; unknown fields are dropped later by clean()."""
    r = lambda v, n=0: None if v is None else round(float(v), n)   # noqa: E731
    return {"name": to_ascii(g.get("name"))[:23] or "GPU", "load": r(g.get("load")), "temp": r(g.get("temp")),
            "vram_used": r(g.get("vram_used"), 1), "vram_total": r(g.get("vram_total"), 1),
            "discrete": bool(g.get("discrete")), "power": r(g.get("power"), 1),
            "clock": r(g.get("clock")), "fan": r(g.get("fan"))}


class Sampler:
    def __init__(self, lhm_url, hub=None):
        self.lhm_url = lhm_url
        if hub is None:
            extra = {}
            if IS_WIN and lhm_url:
                extra["lhm"] = (lambda: lhm_extract(lhm_fetch(lhm_url)), 2.0)
            hub = hwinfo.HardwareHub(hwinfo.default_probes(), extra)
            hub.start()                         # slow probes run on their own thread
        self.hub = hub
        self.prev_disk = psutil.disk_io_counters()
        self.prev_net = psutil.net_io_counters()
        self.prev_t = time.time()
        self.cpu_name = cpu_name()
        self.host = host_name()
        self.estimator = ActivityEstimator()
        self.disk_model = None
        if IS_MAC:
            self.disk_model = mac_disk_model()
        elif IS_LINUX:
            self.disk_model = linux_disk_model()
        psutil.cpu_percent()                    # prime both counters; the first reading is meaningless
        psutil.cpu_percent(percpu=True)

    def sample(self):
        now = time.time()
        dt = max(0.1, now - self.prev_t)
        self.prev_t = now

        vm = psutil.virtual_memory()
        freq = psutil.cpu_freq()
        du = psutil.disk_usage("C:\\" if IS_WIN else "/")

        dio = psutil.disk_io_counters()
        nio = psutil.net_io_counters()
        disk_r = (dio.read_bytes - self.prev_disk.read_bytes) / dt / 2**20
        disk_w = (dio.write_bytes - self.prev_disk.write_bytes) / dt / 2**20
        net_dl = (nio.bytes_recv - self.prev_net.bytes_recv) / dt / 2**20
        net_ul = (nio.bytes_sent - self.prev_net.bytes_sent) / dt / 2**20
        busy = None
        if hasattr(dio, "busy_time") and hasattr(self.prev_disk, "busy_time"):   # Linux / BSD only
            busy = min(100.0, (dio.busy_time - self.prev_disk.busy_time) / (dt * 10.0))
        self.prev_disk, self.prev_net = dio, nio

        cpu_temp = cpu_power = cpu_volt = cpu_fan = None
        disk_temp = disk_act = None
        disk_model = self.disk_model
        hw = self.hub.snapshot()                # latest background readings, never blocks
        lhm = hw.get("lhm") or {}

        if lhm:
            cpu_temp, cpu_power = lhm.get("cpu_temp"), lhm.get("cpu_power")
            cpu_volt, cpu_fan = lhm.get("cpu_volt"), lhm.get("cpu_fan")
            disk_temp = (lhm.get("disk") or {}).get("temp")
            disk_act = (lhm.get("disk") or {}).get("act")
            disk_model = (lhm.get("disk") or {}).get("name") or disk_model
        if IS_LINUX:
            cpu_temp, cpu_fan, disk_temp = linux_sensors()
        mac_t = hw.get("MacThermalProbe") or {}
        if mac_t:
            cpu_temp = mac_t.get("cpu", cpu_temp)
            disk_temp = mac_t.get("ssd", disk_temp)

        # best source first; later ones only fill what is missing (see hwinfo.merge_gpus)
        gpus = hwinfo.merge_gpus(hw.get("NvidiaProbe"), lhm.get("gpus"), hw.get("AdlProbe"),
                                 hw.get("LinuxDrmProbe"), hw.get("WinPdhProbe"), hw.get("MacGpuProbe"))
        if gpus and mac_t.get("gpu") is not None and gpus[0].get("temp") is None:
            gpus[0]["temp"] = mac_t["gpu"]          # Apple GPU temp comes from the SoC sensors

        if disk_act is None:
            disk_act = busy if busy is not None else self.estimator.update(disk_r + disk_w)

        per = psutil.cpu_percent(percpu=True)
        lt = time.localtime()
        payload = {
            "cpu": {
                "load": round(psutil.cpu_percent(), 1),
                "freq": round(freq.current, 0) if freq and freq.current else None,   # None: unknown -> omitted
                "cores": psutil.cpu_count(logical=True),
                "name": self.cpu_name or None,
                "temp": round(cpu_temp, 1) if cpu_temp is not None else None,
                "power": round(cpu_power, 1) if cpu_power is not None else None,
                "volt": round(cpu_volt, 3) if cpu_volt is not None else None,
                "fan": round(cpu_fan) if cpu_fan is not None else None,
                "per_core": group_cores(per) if per else None,
            },
            "ram": {
                "pct": round(vm.percent, 1),
                "used": round(vm.used / 2**30, 1),
                "total": round(vm.total / 2**30, 1),
            },
            "gpus": [gpu_payload(g) for g in gpus[:2]],
            "disk": {
                "pct": round(du.percent, 1),
                "act": round(disk_act, 0),
                "r": round(disk_r, 1),
                "w": round(disk_w, 1),
                "temp": round(disk_temp, 0) if disk_temp is not None else None,
                "model": to_ascii(disk_model)[:23] or None,
            },
            "net": {"dl": round(net_dl, 2), "ul": round(net_ul, 2)},
            "host": {"name": self.host,
                     "os": "win" if IS_WIN else ("mac" if IS_MAC else "linux"),
                     "clock": lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec},
        }
        return clean(payload)


# ── serial ─────────────────────────────────────────────────────
def find_port():
    from serial.tools import list_ports
    ports = list(list_ports.comports())
    for p in ports:  # CH340 first (the CYD's USB-serial chip)
        if (p.vid == 0x1A86) or "CH340" in (p.description or "") \
           or "usbserial" in p.device or "wchusbserial" in p.device:
            return p.device
    return ports[0].device if ports else None


def main():
    ap = argparse.ArgumentParser(description="CYD Resource Monitor agent")
    ap.add_argument("--port", help="serial port (default: auto-detect CH340)")
    ap.add_argument("--interval", type=float, default=0.5, help="seconds between updates")
    ap.add_argument("--lhm", default="http://localhost:8085/data.json",
                    help="LibreHardwareMonitor web-server URL ('' to disable)")
    ap.add_argument("--list", action="store_true", help="list serial ports and exit")
    ap.add_argument("--print", dest="dry", action="store_true",
                    help="print JSON to stdout instead of serial")
    args = ap.parse_args()

    if args.list:
        from serial.tools import list_ports
        for p in list_ports.comports():
            print(f"{p.device:24} {p.description}")
        return

    sampler = Sampler(args.lhm if IS_WIN else None)

    if args.dry:
        while True:
            print(json.dumps(sampler.sample()))
            time.sleep(args.interval)

    import serial
    while True:
        port = args.port or find_port()
        if not port:
            print("no serial port found, retrying in 3 s...  (--list to inspect)")
            time.sleep(3)
            continue
        try:
            with serial.Serial(port, 115200, timeout=1) as ser:
                print(f"connected to {port}")
                while True:
                    line = json.dumps(sampler.sample(), separators=(",", ":")) + "\n"
                    ser.write(line.encode())
                    time.sleep(args.interval)
        except (serial.SerialException, OSError) as e:
            print(f"serial error ({e}), reconnecting in 3 s...")
            time.sleep(3)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
