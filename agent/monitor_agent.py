#!/usr/bin/env python3
"""CYD Resource Monitor agent.

Reads system stats and streams them as JSON lines over USB serial
to the ESP32 display. Works on Windows and macOS.

  python monitor_agent.py               # auto-detect port, 2 Hz
  python monitor_agent.py --list        # list serial ports
  python monitor_agent.py --port COM5   # explicit port
  python monitor_agent.py --print       # dry run: print JSON, no serial

Optional richer data:
  - NVIDIA GPUs:            pip install pynvml
  - Windows temps/AMD/Intel: run LibreHardwareMonitor with
    Options > Remote Web Server enabled (default http://localhost:8085)
  - macOS CPU temp:          install `smctemp` (brew install smctemp)
"""

import argparse
import json
import platform
import re
import shutil
import socket
import subprocess
import sys
import functools
print = functools.partial(print, flush=True)
import time

import psutil

IS_WIN = platform.system() == "Windows"
IS_MAC = platform.system() == "Darwin"

# ── NVIDIA via pynvml (optional) ───────────────────────────────
try:
    import pynvml
    pynvml.nvmlInit()
    NVML = True
except Exception:
    NVML = False


def nvidia_gpus():
    gpus = []
    if not NVML:
        return gpus
    try:
        for i in range(pynvml.nvmlDeviceGetCount()):
            h = pynvml.nvmlDeviceGetHandleByIndex(i)
            name = pynvml.nvmlDeviceGetName(h)
            if isinstance(name, bytes):
                name = name.decode()
            util = pynvml.nvmlDeviceGetUtilizationRates(h)
            mem = pynvml.nvmlDeviceGetMemoryInfo(h)
            temp = pynvml.nvmlDeviceGetTemperature(h, pynvml.NVML_TEMPERATURE_GPU)
            gpus.append({
                "name": name[:24],
                "load": float(util.gpu),
                "temp": float(temp),
                "vram_used": round(mem.used / 2**30, 1),
                "vram_total": round(mem.total / 2**30, 1),
                "discrete": True,
            })
    except Exception:
        pass
    return gpus


# ── LibreHardwareMonitor web server (optional, Windows) ────────
def lhm_fetch(url):
    """Return flat list of (path, value, unit) from LHM's data.json tree."""
    import urllib.request
    try:
        with urllib.request.urlopen(url, timeout=1) as r:
            tree = json.load(r)
    except Exception:
        return None
    flat = []

    def walk(node, path):
        text = node.get("Text", "")
        val = node.get("Value", "")
        p = path + [text]
        if val:
            m = re.match(r"([\d.,]+)\s*(.*)", str(val))
            if m:
                try:
                    flat.append(("/".join(p), float(m.group(1).replace(",", ".")), m.group(2)))
                except ValueError:
                    pass
        for ch in node.get("Children", []):
            walk(ch, p)

    walk(tree, [])
    return flat


def lhm_extract(flat):
    """Pull CPU temp and non-NVIDIA GPU stats out of the LHM sensor list."""
    out = {"cpu_temp": None, "gpus": []}
    if not flat:
        return out
    # CPU package/core temperature
    for path, v, unit in flat:
        low = path.lower()
        if unit == "°C" and "cpu" in low and ("package" in low or "core (tctl" in low or "core average" in low):
            out["cpu_temp"] = v
            break
    if out["cpu_temp"] is None:
        cands = [v for p, v, u in flat if u == "°C" and "cpu" in p.lower()]
        if cands:
            out["cpu_temp"] = max(cands)
    # AMD / Intel GPU nodes (NVIDIA already handled by pynvml)
    gpu_names = set()
    for path, v, unit in flat:
        parts = path.split("/")
        for part in parts:
            low = part.lower()
            if ("gpu" in low or "radeon" in low or "graphics" in low) and len(part) > 6 and "nvidia" not in low:
                gpu_names.add(part)
    for name in gpu_names:
        g = {"name": name[:24], "discrete": not ("intel" in name.lower() and "uhd" in name.lower())}
        for path, v, unit in flat:
            if name not in path:
                continue
            low = path.lower()
            if unit == "%" and ("gpu core" in low or "d3d 3d" in low) and "load" not in g:
                g["load"] = v
            if unit == "°C" and "temp" not in g:
                g["temp"] = v
        out["gpus"].append(g)
    return out


# ── macOS helpers ──────────────────────────────────────────────
def mac_gpus():
    gpus = []
    try:
        r = subprocess.run(
            ["system_profiler", "-json", "SPDisplaysDataType"],
            capture_output=True, text=True, timeout=10)
        data = json.loads(r.stdout).get("SPDisplaysDataType", [])
        for d in data:
            name = d.get("sppci_model", "GPU")
            bus = str(d.get("sppci_bus", ""))
            gpus.append({
                "name": name[:24],
                "discrete": "pcie" in bus.lower() or "spdisplays_pcie" in bus.lower(),
            })
    except Exception:
        pass
    return gpus


def mac_cpu_temp():
    if not shutil.which("smctemp"):
        return None
    try:
        r = subprocess.run(["smctemp", "-c"], capture_output=True, text=True, timeout=2)
        return float(r.stdout.strip())
    except Exception:
        return None


def host_name():
    name = socket.gethostname().split(".")[0]
    if name and not name.isdigit():
        return name[:20]
    if IS_MAC:  # hostname can be an IP on some networks; ask macOS directly
        try:
            r = subprocess.run(["scutil", "--get", "ComputerName"],
                               capture_output=True, text=True, timeout=2)
            if r.stdout.strip():
                return r.stdout.strip()[:20]
        except Exception:
            pass
    return "Mac" if IS_MAC else "PC"


# ── payload assembly ───────────────────────────────────────────
class Sampler:
    def __init__(self, lhm_url):
        self.lhm_url = lhm_url
        self.prev_disk = psutil.disk_io_counters()
        self.prev_net = psutil.net_io_counters()
        self.prev_t = time.time()
        self.static_mac_gpus = mac_gpus() if IS_MAC else []
        psutil.cpu_percent()  # prime

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
        self.prev_disk, self.prev_net = dio, nio

        cpu_temp = None
        gpus = nvidia_gpus()

        if IS_WIN and self.lhm_url:
            lhm = lhm_extract(lhm_fetch(self.lhm_url))
            cpu_temp = lhm["cpu_temp"]
            gpus += lhm["gpus"]
        if IS_MAC:
            cpu_temp = mac_cpu_temp()
            if not gpus:
                gpus = list(self.static_mac_gpus)

        gpus.sort(key=lambda g: not g.get("discrete", False))  # discrete first

        payload = {
            "cpu": {
                "load": round(psutil.cpu_percent(), 1),
                "freq": round(freq.current, 0) if freq else 0,
                "cores": psutil.cpu_count(logical=True),
            },
            "ram": {
                "pct": round(vm.percent, 1),
                "used": round(vm.used / 2**30, 1),
                "total": round(vm.total / 2**30, 1),
            },
            "gpus": gpus[:2],
            "disk": {"pct": round(du.percent, 1), "r": round(disk_r, 1), "w": round(disk_w, 1)},
            "net": {"dl": round(net_dl, 2), "ul": round(net_ul, 2)},
            "host": {"name": host_name(),
                     "os": "win" if IS_WIN else ("mac" if IS_MAC else "linux")},
        }
        if cpu_temp is not None:
            payload["cpu"]["temp"] = round(cpu_temp, 1)
        return payload


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
