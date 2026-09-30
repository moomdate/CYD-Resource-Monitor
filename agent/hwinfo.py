"""Hardware probes for the monitor agent: GPUs from every vendor, and temperatures.

Each probe is optional and fails soft - it returns nothing when its API / tool / driver
is not there, and the display shows "--". Probes are cheap to call but some shell out or
walk device trees, so the agent runs them on a background thread at their own interval
(see HardwareHub) and the 2 Hz serial loop never waits on them.

GPU sources, best first (a later source only fills fields an earlier one left empty):
  NVIDIA  all OS    NVML (pip install nvidia-ml-py), else `nvidia-smi`
  AMD     Linux     amdgpu sysfs: busy %, VRAM, temp, power, fan, clock
          Windows   LibreHardwareMonitor web server (temps), else pyadl (pip install pyadl)
  Intel   Linux     i915/xe sysfs: busy % from RC6 residency, clock, temp (Arc)
  any     Windows   PDH "GPU Engine" counters: load + dedicated VRAM for every WDDM GPU,
                    names/VRAM size from the registry (no admin, no extra installs)
  Apple   macOS     IOAccelerator PerformanceStatistics via `ioreg` (load, also AMD/Intel Macs)

Temperatures:
  macOS   Apple Silicon IOHID thermal sensors via ctypes (CPU, GPU, SSD - no sudo);
          Intel Macs: `smctemp` if installed
  Linux   psutil sensors (coretemp / k10temp / zenpower / nvme), amdgpu hwmon
  Windows LibreHardwareMonitor (CPU / GPU / drive temps need a kernel driver on Windows)
"""

import json
import os
import platform
import re
import shutil
import subprocess
import threading
import time

IS_WIN = platform.system() == "Windows"
IS_MAC = platform.system() == "Darwin"
IS_LINUX = platform.system() == "Linux"

GPU_FIELDS = ("load", "temp", "vram_used", "vram_total", "power", "clock", "fan")


# ── helpers ────────────────────────────────────────────────────
def short_gpu_name(raw):
    """'NVIDIA GeForce RTX 4070 Ti SUPER' -> 'GeForce RTX 4070 Ti SUPER' (fits 24 chars)."""
    s = re.sub(r"\((?:R|TM)\)", "", raw or "", flags=re.I)
    s = re.sub(r"^(NVIDIA|Advanced Micro Devices, Inc\.|AMD/ATI|AMD|Intel Corporation|Intel)\s+", "", s.strip(),
               flags=re.I)
    s = re.sub(r"^.*\[(.+)\]\s*$", r"\1", s)            # lspci: "Navi 32 [Radeon RX 7800 XT]" -> marketing name
    s = re.sub(r"\bGraphics Controller\b", "Graphics", s, flags=re.I)
    s = re.sub(r"\s+", " ", s).strip()
    if len(s) > 23:                                      # too long for the display: drop brand words
        s = re.sub(r"^(GeForce|Radeon)\s+(?=RTX|GTX|RX|Pro)", "", s)
    return s[:23].strip() or "GPU"


def _identity(name):
    """Key to recognise one GPU reported by two sources under different spellings:
    'NVIDIA GeForce RTX 4070 Ti' and 'RTX 4070 Ti' -> '4070 ti'. Uses the model number and
    what follows it (Ti / SUPER / XT matter); names without a number use the whole name."""
    low = re.sub(r"\b(graphics|gpu)\b", "", (name or "").lower())
    m = re.search(r"\b(?:m\d|\d{3,5})\b.*", low)
    return re.sub(r"\s+", " ", (m.group(0) if m else low)).strip()


def merge_gpus(*sources):
    """Merge GPU lists from several probes: the first source to report a GPU wins its name;
    later sources only fill fields that are still missing. Discrete GPUs sort first."""
    merged = []
    for src in sources:
        for g in src or []:
            key = _identity(g.get("name"))
            hit = next((m for m in merged if _identity(m.get("name")) == key), None)
            if hit is None and len(merged) == 1 and len(src) == 1 and not merged[0].get("_strong"):
                hit = merged[0]                     # one GPU on each side: almost surely the same card
            if hit is None:
                merged.append(dict(g))
                continue
            for k, v in g.items():
                if k not in hit or hit[k] is None:
                    hit[k] = v
    merged.sort(key=lambda g: (not g.get("discrete", False), -(g.get("load") or 0)))
    for g in merged:
        g.pop("_strong", None)
    return merged


def _read(path, cast=str):
    try:
        with open(path) as f:
            return cast(f.read().strip())
    except (OSError, ValueError):
        return None


def _run(cmd, timeout=3):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout).stdout
    except (OSError, subprocess.SubprocessError):
        return ""


# ── NVIDIA ─────────────────────────────────────────────────────
class NvidiaProbe:
    """NVML if the python binding is installed, otherwise nvidia-smi if it is on PATH."""
    interval = 1.0

    def __init__(self):
        self.nvml = None
        try:
            import pynvml                           # provided by the nvidia-ml-py package
            pynvml.nvmlInit()
            self.nvml = pynvml
        except Exception:
            pass
        self.smi = None if self.nvml else shutil.which("nvidia-smi")

    def available(self):
        return bool(self.nvml or self.smi)

    def read(self):
        return self._read_nvml() if self.nvml else parse_nvidia_smi(_run([
            self.smi, "--query-gpu=name,utilization.gpu,temperature.gpu,memory.used,memory.total,"
                      "power.draw,clocks.gr,fan.speed", "--format=csv,noheader,nounits"]))

    def _read_nvml(self):
        n, out = self.nvml, []
        for i in range(n.nvmlDeviceGetCount()):
            h = n.nvmlDeviceGetHandleByIndex(i)
            name = n.nvmlDeviceGetName(h)
            g = {"name": short_gpu_name(name.decode() if isinstance(name, bytes) else name),
                 "vendor": "nvidia", "discrete": True, "_strong": True}
            for key, fn in (("load", lambda: n.nvmlDeviceGetUtilizationRates(h).gpu),
                            ("temp", lambda: n.nvmlDeviceGetTemperature(h, n.NVML_TEMPERATURE_GPU)),
                            ("power", lambda: n.nvmlDeviceGetPowerUsage(h) / 1000.0),
                            ("clock", lambda: n.nvmlDeviceGetClockInfo(h, n.NVML_CLOCK_GRAPHICS)),
                            ("fan", lambda: n.nvmlDeviceGetFanSpeed(h))):
                try:
                    g[key] = float(fn())
                except Exception:                    # laptops: no fan / power sensor
                    pass
            try:
                mem = n.nvmlDeviceGetMemoryInfo(h)
                g["vram_used"], g["vram_total"] = round(mem.used / 2**30, 1), round(mem.total / 2**30, 1)
            except Exception:
                pass
            out.append(g)
        return out


def parse_nvidia_smi(text):
    out = []
    for line in (text or "").strip().splitlines():
        f = [x.strip() for x in line.split(",")]
        if len(f) < 8:
            continue

        def num(s, scale=1.0):
            try:
                return float(s) * scale
            except ValueError:                        # "[N/A]", "[Not Supported]"
                return None
        out.append({"name": short_gpu_name(f[0]), "vendor": "nvidia", "discrete": True, "_strong": True,
                    "load": num(f[1]), "temp": num(f[2]),
                    "vram_used": round(num(f[3]) / 1024, 1) if num(f[3]) is not None else None,
                    "vram_total": round(num(f[4]) / 1024, 1) if num(f[4]) is not None else None,
                    "power": num(f[5]), "clock": num(f[6]), "fan": num(f[7])})
    return out


# ── Linux: amdgpu / i915 / xe sysfs ────────────────────────────
class LinuxDrmProbe:
    """AMD and Intel GPUs straight from sysfs - no root, no tools. NVIDIA is left to NvidiaProbe."""
    interval = 1.0

    def __init__(self, root="/sys/class/drm"):
        self.root = root
        self.cards = []
        self.rc6_prev = {}                          # card -> (rc6_ms, wall_ms) for Intel busy %
        try:
            names = sorted(os.listdir(root))
        except OSError:
            names = []
        for c in names:
            if not re.fullmatch(r"card\d+", c):
                continue
            dev = os.path.join(root, c, "device")
            vendor = (_read(os.path.join(dev, "vendor")) or "").lower()
            if vendor in ("0x1002", "0x8086"):
                self.cards.append((c, dev, "amd" if vendor == "0x1002" else "intel",
                                   self._name(dev, vendor)))

    @staticmethod
    def _name(dev, vendor):
        slot = os.path.basename(os.path.realpath(dev))            # PCI address, e.g. 0000:03:00.0
        out = _run(["lspci", "-mm", "-s", slot]) if shutil.which("lspci") else ""
        m = re.findall(r'"([^"]*)"', out)
        if len(m) >= 3 and m[2]:
            return short_gpu_name(m[2])
        return "Radeon GPU" if vendor == "0x1002" else "Intel Graphics"

    def available(self):
        return bool(self.cards)

    def read(self):
        out = []
        for card, dev, vendor, name in self.cards:
            out.append(self._amd(dev, name) if vendor == "amd" else self._intel(card, dev, name))
        return out

    @staticmethod
    def _hwmon(dev):
        base = os.path.join(dev, "hwmon")
        try:
            return [os.path.join(base, h) for h in sorted(os.listdir(base))]
        except OSError:
            return []

    def _amd(self, dev, name):
        g = {"name": name, "vendor": "amd"}
        g["load"] = _read(os.path.join(dev, "gpu_busy_percent"), float)
        used = _read(os.path.join(dev, "mem_info_vram_used"), int)
        total = _read(os.path.join(dev, "mem_info_vram_total"), int)
        if used is not None and total:
            g["vram_used"], g["vram_total"] = round(used / 2**30, 1), round(total / 2**30, 1)
        g["discrete"] = bool(total and total > 2 * 2**30)       # APUs carve out <= 2 GB
        for h in self._hwmon(dev):
            temp = None
            for i in range(1, 4):                                   # prefer the "edge" sensor
                label = (_read(os.path.join(h, f"temp{i}_label")) or "").lower()
                val = _read(os.path.join(h, f"temp{i}_input"), int)
                if val is not None and (temp is None or label == "edge"):
                    temp = val / 1000.0
            g["temp"] = temp
            p = _read(os.path.join(h, "power1_average"), int) or _read(os.path.join(h, "power1_input"), int)
            g["power"] = p / 1e6 if p else None
            g["fan"] = _read(os.path.join(h, "fan1_input"), float)
            f = _read(os.path.join(h, "freq1_input"), int)
            g["clock"] = f / 1e6 if f else None
            break
        return g

    def _intel(self, card, dev, name):
        g = {"name": name, "vendor": "intel", "discrete": "arc" in name.lower()}
        base = os.path.join(self.root, card)
        rc6 = None
        for p in (os.path.join(base, "gt", "gt0", "rc6_residency_ms"),
                  os.path.join(base, "power", "rc6_residency_ms")):
            rc6 = _read(p, int)
            if rc6 is not None:
                break
        now = time.monotonic() * 1000.0
        if rc6 is not None:
            prev = self.rc6_prev.get(card)
            self.rc6_prev[card] = (rc6, now)
            if prev and now > prev[1]:
                idle = (rc6 - prev[0]) / (now - prev[1])          # fraction of time power-gated
                g["load"] = max(0.0, min(100.0, 100.0 * (1.0 - idle)))
        clk = _read(os.path.join(base, "gt", "gt0", "rps_act_freq_mhz"), float) or \
            _read(os.path.join(base, "gt_act_freq_mhz"), float)
        g["clock"] = clk
        for h in self._hwmon(dev):                                  # Arc (DG2) exposes a hwmon
            t = _read(os.path.join(h, "temp1_input"), int)
            if t:
                g["temp"] = t / 1000.0
                break
        return g


# ── macOS: IOAccelerator statistics (Apple Silicon, and AMD / Intel GPUs in Intel Macs) ──
class MacGpuProbe:
    """Reads IOAccelerator 'PerformanceStatistics' straight from IOKit via ctypes (~1 ms). Spawning
    `ioreg` for the same data cost ~34 ms of CPU per read - that alone was 3 % of a core at 1 Hz -
    so the subprocess is only a fallback."""
    interval = 1.0
    KEYS = ("Device Utilization %", "GPU Activity(%)", "vramUsedBytes", "vramFreeBytes")

    def __init__(self):
        self.names = mac_gpu_names() if IS_MAC else []
        self.iokit = None
        if IS_MAC:
            try:
                self._init_iokit()
            except Exception:
                self.iokit = None

    def available(self):
        return IS_MAC

    def _init_iokit(self):
        import ctypes
        import ctypes.util
        vp, u32 = ctypes.c_void_p, ctypes.c_uint32
        cf = ctypes.CDLL(ctypes.util.find_library("CoreFoundation"))
        io = ctypes.CDLL(ctypes.util.find_library("IOKit"))
        io.IOServiceMatching.restype = vp
        io.IOServiceMatching.argtypes = [ctypes.c_char_p]
        io.IOServiceGetMatchingServices.argtypes = [u32, vp, ctypes.POINTER(u32)]
        io.IOIteratorNext.restype = u32
        io.IOIteratorNext.argtypes = [u32]
        io.IOObjectRelease.argtypes = [u32]
        io.IORegistryEntryCreateCFProperty.restype = vp
        io.IORegistryEntryCreateCFProperty.argtypes = [u32, vp, vp, u32]
        cf.CFStringCreateWithCString.restype = vp
        cf.CFStringCreateWithCString.argtypes = [vp, ctypes.c_char_p, u32]
        cf.CFDictionaryGetValue.restype = vp
        cf.CFDictionaryGetValue.argtypes = [vp, vp]
        cf.CFNumberGetValue.restype = ctypes.c_bool
        cf.CFNumberGetValue.argtypes = [vp, ctypes.c_int, vp]
        cf.CFRelease.argtypes = [vp]
        utf8 = 0x08000100
        self.k_stats = cf.CFStringCreateWithCString(None, b"PerformanceStatistics", utf8)
        self.k_items = {k: cf.CFStringCreateWithCString(None, k.encode(), utf8) for k in self.KEYS}
        self.cf, self.iokit, self.ctypes = cf, io, ctypes

    def _read_iokit(self):
        """PerformanceStatistics of every accelerator as the same text ioreg prints, so one parser
        handles both paths."""
        ct, cf, io = self.ctypes, self.cf, self.iokit
        it = ct.c_uint32(0)
        if io.IOServiceGetMatchingServices(0, io.IOServiceMatching(b"IOAccelerator"), ct.byref(it)) != 0:
            return None
        blocks = []
        try:
            while True:
                entry = io.IOIteratorNext(it)
                if not entry:
                    break
                stats = io.IORegistryEntryCreateCFProperty(entry, self.k_stats, None, 0)
                io.IOObjectRelease(entry)
                if not stats:
                    continue
                vals = []
                for k, key in self.k_items.items():
                    num = cf.CFDictionaryGetValue(stats, key)
                    out = ct.c_int64(0)
                    if num and cf.CFNumberGetValue(num, 4, ct.byref(out)):     # kCFNumberSInt64Type
                        vals.append(f'"{k}"={out.value}')
                cf.CFRelease(stats)
                blocks.append('"PerformanceStatistics" = {' + ",".join(vals) + "}")
        finally:
            io.IOObjectRelease(it.value)
        return "\n".join(blocks)

    def read(self):
        text = self._read_iokit() if self.iokit else None
        if text is None:
            text = _run(["ioreg", "-r", "-d", "1", "-w", "0", "-c", "IOAccelerator"])
        return parse_ioreg_accelerators(text, self.names)


def mac_gpu_names():
    """[(name, discrete)] from system_profiler, in the same order ioreg lists accelerators."""
    try:
        data = json.loads(_run(["system_profiler", "-json", "SPDisplaysDataType"], timeout=10) or "{}")
    except ValueError:
        return []
    out = []
    for d in data.get("SPDisplaysDataType", []):
        bus = str(d.get("sppci_bus", "")).lower()
        out.append((short_gpu_name(d.get("sppci_model", "GPU")), "pcie" in bus))
    return out


def parse_ioreg_accelerators(text, names=()):
    out = []
    for i, block in enumerate(re.findall(r'"PerformanceStatistics" = \{([^}]*)\}', text or "")):
        stats = dict((k, float(v)) for k, v in re.findall(r'"([^"]+)"=(\d+(?:\.\d+)?)', block))
        name, discrete = names[i] if i < len(names) else ("GPU", False)
        g = {"name": name, "vendor": "apple" if "apple" in name.lower() else "", "discrete": discrete}
        if "Device Utilization %" in stats:
            g["load"] = stats["Device Utilization %"]
        elif "GPU Activity(%)" in stats:                        # older AMD drivers on Intel Macs
            g["load"] = stats["GPU Activity(%)"]
        used, free = stats.get("vramUsedBytes"), stats.get("vramFreeBytes")
        if used is not None and free is not None:               # discrete GPUs: real VRAM
            g["vram_used"], g["vram_total"] = round(used / 2**30, 1), round((used + free) / 2**30, 1)
        out.append(g)
    return out


# ── Windows: PDH GPU counters (vendor neutral) ─────────────────
class WinPdhProbe:
    """GPU load and dedicated VRAM for every WDDM GPU (NVIDIA / AMD / Intel), the same counters
    Task Manager uses. Adapter names and VRAM sizes come from the registry; counters are keyed
    by adapter LUID, which is matched to a name by dedicated-memory use (largest to largest)."""
    interval = 1.0

    def __init__(self):
        self.ok = False
        self.adapters = win_gpu_adapters() if IS_WIN else []
        if not IS_WIN:
            return
        try:
            import ctypes
            from ctypes import wintypes
            self.ct, self.wt = ctypes, wintypes
            self.pdh = ctypes.WinDLL("pdh")
            self.q = wintypes.HANDLE()
            if self.pdh.PdhOpenQueryW(None, None, ctypes.byref(self.q)) != 0:
                return
            self.c_eng, self.c_mem = wintypes.HANDLE(), wintypes.HANDLE()
            add = self.pdh.PdhAddEnglishCounterW
            if add(self.q, r"\GPU Engine(*)\Utilization Percentage", None, ctypes.byref(self.c_eng)) != 0:
                return
            add(self.q, r"\GPU Adapter Memory(*)\Dedicated Usage", None, ctypes.byref(self.c_mem))
            self.pdh.PdhCollectQueryData(self.q)                  # rate counters need a first sample
            self.ok = True
        except Exception:
            self.ok = False

    def available(self):
        return self.ok

    def _array(self, counter):
        ct, wt = self.ct, self.wt

        class Value(ct.Structure):
            _fields_ = [("CStatus", wt.DWORD), ("doubleValue", ct.c_double)]

        class Item(ct.Structure):
            _fields_ = [("szName", wt.LPWSTR), ("FmtValue", Value)]

        PDH_FMT_DOUBLE, PDH_FMT_NOCAP100, PDH_MORE_DATA = 0x200, 0x8000, 0x800007D2
        size, count = wt.DWORD(0), wt.DWORD(0)
        fmt = PDH_FMT_DOUBLE | PDH_FMT_NOCAP100
        if self.pdh.PdhGetFormattedCounterArrayW(counter, fmt, ct.byref(size), ct.byref(count), None) \
                & 0xFFFFFFFF != PDH_MORE_DATA:
            return []
        buf = (ct.c_byte * size.value)()
        if self.pdh.PdhGetFormattedCounterArrayW(counter, fmt, ct.byref(size), ct.byref(count), buf) != 0:
            return []
        items = ct.cast(buf, ct.POINTER(Item))
        return [(items[i].szName, items[i].FmtValue.doubleValue) for i in range(count.value)
                if items[i].FmtValue.CStatus in (0, 1)]

    def read(self):
        if self.pdh.PdhCollectQueryData(self.q) != 0:
            return []
        return pdh_gpus(self._array(self.c_eng), self._array(self.c_mem), self.adapters)


def pdh_gpus(engine_items, memory_items, adapters):
    """Pure aggregation (unit-tested): per adapter LUID, sum each engine type over processes and
    take the busiest type (Task Manager's 'GPU' figure); attach dedicated memory; name adapters."""
    per_luid = {}
    for inst, val in engine_items:
        m = re.search(r"luid_(0x[0-9a-fA-F]+_0x[0-9a-fA-F]+)_phys_\d+_eng_\d+_engtype_(\w+)", inst)
        if m:
            eng = per_luid.setdefault(m.group(1).lower(), {})
            eng[m.group(2)] = eng.get(m.group(2), 0.0) + val
    mem = {}
    for inst, val in memory_items:
        m = re.search(r"luid_(0x[0-9a-fA-F]+_0x[0-9a-fA-F]+)", inst)
        if m:
            mem[m.group(1).lower()] = mem.get(m.group(1).lower(), 0.0) + val
    luids = sorted(set(per_luid) | set(mem), key=lambda l: -mem.get(l, 0.0))
    named = sorted(adapters, key=lambda a: -a.get("vram_total", 0))
    out = []
    for i, luid in enumerate(luids):
        engines = per_luid.get(luid, {})
        if not engines and mem.get(luid, 0.0) <= 0:
            continue                                            # ghost / basic-render adapters
        ad = named[i] if i < len(named) else {"name": "GPU", "vram_total": 0}
        g = {"name": ad["name"], "vendor": ad.get("vendor", ""), "discrete": ad.get("discrete", False),
             "load": min(100.0, max(engines.values())) if engines else 0.0}
        if luid in mem and ad.get("vram_total"):
            g["vram_used"], g["vram_total"] = round(mem[luid] / 2**30, 1), round(ad["vram_total"], 1)
        out.append(g)
    return out


def win_gpu_adapters():
    """Display adapters from the registry: name, vendor, VRAM (qwMemorySize is 64-bit, unlike
    WMI's AdapterRAM which caps at 4 GB)."""
    try:
        import winreg
    except ImportError:
        return []
    out = []
    cls = r"SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}"
    try:
        root = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, cls)
    except OSError:
        return []
    for i in range(64):
        try:
            sub = winreg.EnumKey(root, i)
        except OSError:
            break
        if not sub.isdigit():
            continue
        try:
            with winreg.OpenKey(root, sub) as k:
                desc = winreg.QueryValueEx(k, "DriverDesc")[0]
                try:
                    vram = int(winreg.QueryValueEx(k, "HardwareInformation.qwMemorySize")[0])
                except OSError:
                    raw = winreg.QueryValueEx(k, "HardwareInformation.MemorySize")[0]
                    vram = int.from_bytes(raw, "little") if isinstance(raw, bytes) else int(raw)
        except (OSError, ValueError):
            continue
        low = desc.lower()
        if "basic render" in low or "remote" in low or "virtual" in low:
            continue
        vendor = "nvidia" if "nvidia" in low else "amd" if ("amd" in low or "radeon" in low) else \
            "intel" if "intel" in low else ""
        integrated = vendor == "intel" and "arc" not in low or ("radeon" in low and "graphics" in low
                                                                 and " rx " not in f" {low} ")
        out.append({"name": short_gpu_name(desc), "vendor": vendor, "discrete": not integrated,
                    "vram_total": vram / 2**30})
    return out


# ── Windows: AMD via ADL (optional) ────────────────────────────
class AdlProbe:
    interval = 1.0

    def __init__(self):
        self.devs = []
        try:
            from pyadl import ADLManager
            self.devs = ADLManager.getInstance().getDevices()
        except Exception:
            pass

    def available(self):
        return bool(self.devs)

    def read(self):
        out = []
        for d in self.devs:
            name = d.adapterName.decode() if isinstance(d.adapterName, bytes) else str(d.adapterName)
            g = {"name": short_gpu_name(name), "vendor": "amd", "discrete": True}
            for key, fn in (("load", d.getCurrentUsage), ("temp", d.getCurrentTemperature),
                            ("clock", d.getCurrentEngineClock)):
                try:
                    g[key] = float(fn())
                except Exception:
                    pass
            out.append(g)
        return out


# ── macOS: Apple Silicon thermal sensors via IOHID ─────────────
class MacThermalProbe:
    """CPU / GPU / SSD temperatures on Apple Silicon without sudo: the same IOHID event system
    `powermetrics`-free tools (Stats, macmon) read. Intel Macs fall back to `smctemp`."""
    interval = 2.0

    def __init__(self):
        self.client = None
        self.smctemp = shutil.which("smctemp") if IS_MAC else None
        if IS_MAC and platform.machine() == "arm64":
            try:
                self._init_iohid()
            except Exception:
                self.client = None

    def available(self):
        return bool(self.client or self.smctemp)

    def _init_iohid(self):
        import ctypes
        import ctypes.util
        vp = ctypes.c_void_p
        cf = ctypes.CDLL(ctypes.util.find_library("CoreFoundation"))
        io = ctypes.CDLL(ctypes.util.find_library("IOKit"))
        cf.CFStringCreateWithCString.restype = vp
        cf.CFStringCreateWithCString.argtypes = [vp, ctypes.c_char_p, ctypes.c_uint32]
        cf.CFNumberCreate.restype = vp
        cf.CFNumberCreate.argtypes = [vp, ctypes.c_int, vp]
        cf.CFDictionaryCreate.restype = vp
        cf.CFDictionaryCreate.argtypes = [vp, ctypes.POINTER(vp), ctypes.POINTER(vp), ctypes.c_long, vp, vp]
        cf.CFArrayGetCount.restype = ctypes.c_long
        cf.CFArrayGetCount.argtypes = [vp]
        cf.CFArrayGetValueAtIndex.restype = vp
        cf.CFArrayGetValueAtIndex.argtypes = [vp, ctypes.c_long]
        cf.CFStringGetCString.restype = ctypes.c_bool
        cf.CFStringGetCString.argtypes = [vp, ctypes.c_char_p, ctypes.c_long, ctypes.c_uint32]
        cf.CFRelease.argtypes = [vp]
        io.IOHIDEventSystemClientCreate.restype = vp
        io.IOHIDEventSystemClientCreate.argtypes = [vp]
        io.IOHIDEventSystemClientSetMatching.argtypes = [vp, vp]
        io.IOHIDEventSystemClientCopyServices.restype = vp
        io.IOHIDEventSystemClientCopyServices.argtypes = [vp]
        io.IOHIDServiceClientCopyProperty.restype = vp
        io.IOHIDServiceClientCopyProperty.argtypes = [vp, vp]
        io.IOHIDServiceClientCopyEvent.restype = vp
        io.IOHIDServiceClientCopyEvent.argtypes = [vp, ctypes.c_int64, ctypes.c_int32, ctypes.c_int64]
        io.IOHIDEventGetFloatValue.restype = ctypes.c_double
        io.IOHIDEventGetFloatValue.argtypes = [vp, ctypes.c_int32]
        utf8 = 0x08000100

        def cfstr(s):
            return cf.CFStringCreateWithCString(None, s.encode(), utf8)

        def cfint(i):
            v = ctypes.c_int32(i)
            return cf.CFNumberCreate(None, 3, ctypes.byref(v))       # kCFNumberSInt32Type

        keys = (vp * 2)(cfstr("PrimaryUsagePage"), cfstr("PrimaryUsage"))
        vals = (vp * 2)(cfint(0xFF00), cfint(5))                     # Apple vendor page, temperature
        kcb = vp.in_dll(cf, "kCFTypeDictionaryKeyCallBacks")
        vcb = vp.in_dll(cf, "kCFTypeDictionaryValueCallBacks")
        match = cf.CFDictionaryCreate(None, keys, vals, 2, ctypes.addressof(kcb), ctypes.addressof(vcb))
        self.client = io.IOHIDEventSystemClientCreate(None)
        io.IOHIDEventSystemClientSetMatching(self.client, match)
        self.cf, self.io, self.ctypes, self.utf8 = cf, io, ctypes, utf8
        self.k_product = cfstr("Product")

    def _iohid_readings(self):
        cf, io = self.cf, self.io
        services = io.IOHIDEventSystemClientCopyServices(self.client)
        if not services:
            return []
        buf = self.ctypes.create_string_buffer(128)
        out = []
        try:
            for i in range(cf.CFArrayGetCount(services)):
                s = cf.CFArrayGetValueAtIndex(services, i)
                name = io.IOHIDServiceClientCopyProperty(s, self.k_product)
                label = buf.value.decode(errors="replace") if name and cf.CFStringGetCString(
                    name, buf, 128, self.utf8) else ""
                if name:
                    cf.CFRelease(name)
                ev = io.IOHIDServiceClientCopyEvent(s, 15, 0, 0)          # kIOHIDEventTypeTemperature
                if ev:
                    out.append((label, io.IOHIDEventGetFloatValue(ev, 15 << 16)))
                    cf.CFRelease(ev)
        finally:
            cf.CFRelease(services)
        return out

    def read(self):
        if self.client:
            return classify_apple_temps(self._iohid_readings())
        try:
            return {"cpu": float(_run(["smctemp", "-c"], timeout=2).strip())}
        except ValueError:
            return {}


def classify_apple_temps(readings):
    """Pure (unit-tested): IOHID sensor labels -> {'cpu', 'gpu', 'ssd'} in °C.
    CPU = hottest performance/efficiency-cluster sensor (what throttling reacts to);
    GPU = hottest 'GPU MTR' sensor - on some M1s those are stuck at a placeholder ~30 °C,
          then the SoC die average is the better estimate (the GPU is on the same die);
    SSD = NAND temperature."""
    def pick(pred, agg=max):
        vals = [v for l, v in readings if pred(l) and 5.0 < v < 130.0]
        return agg(vals) if vals else None

    cpu = pick(lambda l: re.match(r"[pe]ACC MTR Temp", l) or l.startswith("PMU tdie"))
    gpu = pick(lambda l: l.startswith("GPU MTR"))
    soc = pick(lambda l: "SOC" in l and "Die" in l, lambda v: sum(v) / len(v)) or \
        pick(lambda l: l.startswith("SOC MTR"), lambda v: sum(v) / len(v))
    if gpu is not None and soc is not None and gpu <= 31.0 and soc > 40.0:
        gpu = soc
    ssd = pick(lambda l: l.startswith("NAND"))
    return {k: round(v, 1) for k, v in (("cpu", cpu), ("gpu", gpu if gpu is not None else soc),
                                         ("ssd", ssd)) if v is not None}


# ── background hub ─────────────────────────────────────────────
class HardwareHub(threading.Thread):
    """Runs the slow probes on their own cadence on a daemon thread. snapshot() is lock-free
    enough for our purposes (dict swap) and never blocks the serial loop."""

    def __init__(self, probes, extra=None):
        super().__init__(daemon=True, name="hwinfo")
        self.probes = [p for p in probes if safe_available(p)]
        self.extra = extra or {}                   # name -> (callable, interval): e.g. LHM
        self.results = {}
        self.stop_evt = threading.Event()

    def run(self):
        due = {id(p): 0.0 for p in self.probes}
        due.update({k: 0.0 for k in self.extra})
        while not self.stop_evt.is_set():
            now = time.monotonic()
            for p in self.probes:
                if now >= due[id(p)]:
                    self.results[type(p).__name__] = safe_read(p)
                    due[id(p)] = now + p.interval
            for k, (fn, interval) in self.extra.items():
                if now >= due[k]:
                    try:
                        self.results[k] = fn()
                    except Exception:
                        self.results[k] = None
                    due[k] = now + interval
            self.stop_evt.wait(0.1)

    def snapshot(self):
        return dict(self.results)


def safe_available(p):
    try:
        return p.available()
    except Exception:
        return False


def safe_read(p):
    try:
        return p.read()
    except Exception:
        return None


def default_probes():
    probes = [NvidiaProbe()]
    if IS_LINUX:
        probes.append(LinuxDrmProbe())
    if IS_WIN:
        probes += [WinPdhProbe(), AdlProbe()]
    if IS_MAC:
        probes += [MacGpuProbe(), MacThermalProbe()]
    return probes
