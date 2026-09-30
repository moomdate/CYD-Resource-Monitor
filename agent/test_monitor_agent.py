"""Tests for monitor_agent.py:   python -m unittest discover -s agent -v

Runs anywhere: psutil is replaced by a small fake, so no hardware or OS sensors are needed.
Checks the JSON the firmware receives - especially that the payload stays backward compatible
(every key the first firmware reads is still there) and that missing data is omitted, never null/NaN.
"""
import json
import os
import sys
import types
import unittest
from collections import namedtuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

try:                                    # the module imports psutil at load time
    import psutil  # noqa: F401
except ImportError:
    sys.modules["psutil"] = types.ModuleType("psutil")

import monitor_agent as agent  # noqa: E402

IO = namedtuple("IO", "read_bytes write_bytes")
NET = namedtuple("NET", "bytes_recv bytes_sent")
VM = namedtuple("VM", "percent used total")
DU = namedtuple("DU", "percent")
FREQ = namedtuple("FREQ", "current")


class FakePsutil:
    """Just enough psutil for Sampler. Counters advance a fixed step per call."""

    def __init__(self, per_core=(10.0, 20.0, 30.0, 40.0), freq=3200.0):
        self.per_core, self.freq_value = list(per_core), freq
        self.n = 0

    def cpu_percent(self, percpu=False):
        return list(self.per_core) if percpu else sum(self.per_core) / max(1, len(self.per_core))

    def cpu_freq(self):
        return FREQ(self.freq_value) if self.freq_value is not None else None

    def cpu_count(self, logical=True):
        return len(self.per_core) or None

    def virtual_memory(self):
        return VM(38.8, 12.4 * 2**30, 32 * 2**30)

    def disk_usage(self, path):
        return DU(54.0)

    def disk_io_counters(self):
        self.n += 1
        return IO(self.n * 50 * 2**20, self.n * 10 * 2**20)

    def net_io_counters(self):
        self.n += 1
        return NET(self.n * 5 * 2**20, self.n * 1 * 2**20)


def make_sampler(fake=None):
    """Sampler with everything that would shell out / touch the OS stubbed."""
    agent.psutil = fake or FakePsutil()
    agent.mac_gpus = lambda: []
    agent.mac_disk_model = lambda: "TEST NVME 1TB"
    agent.mac_cpu_temp = lambda: None
    agent.linux_disk_model = lambda: "TEST NVME 1TB"
    agent.linux_sensors = lambda: (None, None, None)
    agent.cpu_name = lambda: "Test CPU"
    agent.host_name = lambda: "testhost"
    agent.nvidia_gpus = lambda: []
    return agent.Sampler(None)


class TestHelpers(unittest.TestCase):
    def test_clean_cpu_name(self):
        c = agent.clean_cpu_name
        self.assertEqual(c("AMD Ryzen 7 7800X3D 8-Core Processor"), "AMD Ryzen 7 7800X3D")
        self.assertEqual(c("Intel(R) Core(TM) i7-12700K CPU @ 3.60GHz"), "Intel Core i7-12700K")
        self.assertEqual(c("Apple M2 Pro"), "Apple M2 Pro")
        self.assertEqual(c(""), "")
        self.assertEqual(c(None), "")
        self.assertLessEqual(len(c("X" * 80)), 27)

    def test_group_cores(self):
        self.assertEqual(agent.group_cores([10.4, 99.6, 0]), [10, 100, 0])
        self.assertEqual(len(agent.group_cores([50] * 64)), 16)               # 64 threads -> 16 bars
        g = agent.group_cores([10, 20, 30, 40] * 8)
        self.assertEqual(len(g), 16)
        self.assertEqual(g[:2], [15, 35])                                     # neighbours are averaged
        self.assertEqual(agent.group_cores([150, -5]), [100, 0])              # clamped
        self.assertEqual(agent.group_cores([]), [])

    def test_activity_estimator(self):
        e = agent.ActivityEstimator(floor=100.0)
        self.assertEqual(e.update(0), 0)
        self.assertEqual(e.update(50), 50)
        self.assertEqual(e.update(400), 100)          # new peak
        self.assertEqual(e.update(200), 50)           # relative to that peak

    def test_clean_drops_none_nan_and_empty(self):
        out = agent.clean({"a": 1.5, "b": None, "c": float("nan"), "d": {"x": None}, "e": [1, None, float("inf")],
                           "f": {"y": 2}, "g": [], "h": True})
        self.assertEqual(out, {"a": 1.5, "e": [1], "f": {"y": 2}, "g": [], "h": True})
        json.dumps(out, allow_nan=False)


LHM_TREE = {
    "Text": "Sensor", "Children": [{
        "Text": "TESTPC", "ImageURL": "images_icon/computer.png", "Children": [
            {"Text": "AMD Ryzen 7 7800X3D", "ImageURL": "images_icon/cpu.png", "Children": [
                {"Text": "Voltages", "Children": [
                    {"Text": "Core (SVI2 TFN)", "Value": "1.081 V"}]},
                {"Text": "Powers", "Children": [
                    {"Text": "Package", "Value": "82.4 W"}, {"Text": "Cores", "Value": "70.1 W"}]},
                {"Text": "Temperatures", "Children": [
                    {"Text": "Core (Tctl/Tdie)", "Value": "64.0 °C"}]}]},
            {"Text": "ASUS ROG STRIX", "ImageURL": "images_icon/mainboard.png", "Children": [
                {"Text": "Nuvoton NCT6798D", "ImageURL": "images_icon/chip.png", "Children": [
                    {"Text": "Fans", "Children": [
                        {"Text": "Fan #1", "Value": "900 RPM"}, {"Text": "CPU Fan", "Value": "1236 RPM"}]}]}]},
            {"Text": "Samsung SSD 990 PRO 2TB", "ImageURL": "images_icon/hdd.png", "Children": [
                {"Text": "Temperatures", "Children": [{"Text": "Temperature", "Value": "49.0 °C"}]},
                {"Text": "Load", "Children": [
                    {"Text": "Used Space", "Value": "54.0 %"}, {"Text": "Total Activity", "Value": "72.0 %"}]}]},
            {"Text": "WD Blue", "ImageURL": "images_icon/hdd.png", "Children": [
                {"Text": "Temperatures", "Children": [{"Text": "Temperature", "Value": "33.0 °C"}]}]},
            {"Text": "AMD Radeon RX 7900", "ImageURL": "images_icon/ati.png", "Children": [
                {"Text": "Load", "Children": [{"Text": "GPU Core", "Value": "35.0 %"}]},
                {"Text": "Temperatures", "Children": [{"Text": "GPU Core", "Value": "58.0 °C"}]}]},
        ]}]}


class TestLibreHardwareMonitor(unittest.TestCase):
    def test_extract_full(self):
        x = agent.lhm_extract(agent.lhm_flatten(LHM_TREE))
        self.assertEqual(x["cpu_temp"], 64.0)
        self.assertEqual(x["cpu_power"], 82.4)
        self.assertEqual(x["cpu_volt"], 1.081)
        self.assertEqual(x["cpu_fan"], 1236.0)                       # the CPU fan, not Fan #1
        self.assertEqual(x["disk"], {"name": "Samsung SSD 990 PRO 2TB", "temp": 49.0, "act": 72.0})
        self.assertEqual(len(x["gpus"]), 1)
        self.assertEqual(x["gpus"][0]["load"], 35.0)

    def test_extract_nothing_available(self):
        for flat in (None, [], agent.lhm_flatten({"Text": "Sensor", "Children": []})):
            x = agent.lhm_extract(flat)
            self.assertIsNone(x["cpu_temp"])
            self.assertIsNone(x["cpu_power"])
            self.assertIsNone(x["cpu_volt"])
            self.assertIsNone(x["cpu_fan"])
            self.assertEqual(x["disk"], {})
            self.assertEqual(x["gpus"], [])

    def test_extract_intel_style_sensors(self):
        tree = {"Text": "S", "Children": [{"Text": "Intel Core i7", "ImageURL": "images_icon/cpu.png", "Children": [
            {"Text": "Voltages", "Children": [{"Text": "Core #1 VID", "Value": "1.20 V"},
                                              {"Text": "Core #2 VID", "Value": "1.31 V"}]},
            {"Text": "Powers", "Children": [{"Text": "CPU Package", "Value": "65.0 W"}]},
            {"Text": "Temperatures", "Children": [{"Text": "CPU Package", "Value": "71.0 °C"}]}]}]}
        x = agent.lhm_extract(agent.lhm_flatten(tree))
        self.assertEqual(x["cpu_temp"], 71.0)
        self.assertEqual(x["cpu_power"], 65.0)
        self.assertEqual(x["cpu_volt"], 1.31)                        # highest VID


class TestPayload(unittest.TestCase):
    def test_backward_compatible_keys(self):
        p = make_sampler().sample()
        # everything the first firmware read must still be present
        for k in ("load", "freq", "cores"):
            self.assertIn(k, p["cpu"])
        for k in ("pct", "used", "total"):
            self.assertIn(k, p["ram"])
        for k in ("pct", "r", "w"):
            self.assertIn(k, p["disk"])
        for k in ("dl", "ul"):
            self.assertIn(k, p["net"])
        self.assertIn("gpus", p)
        self.assertIn("name", p["host"])
        self.assertIn("os", p["host"])

    def test_new_fields(self):
        p = make_sampler().sample()
        self.assertEqual(p["cpu"]["per_core"], [10, 20, 30, 40])
        self.assertEqual(p["cpu"]["name"], "Test CPU")
        self.assertEqual(p["host"]["name"], "testhost")
        self.assertTrue(0 <= p["host"]["clock"] < 86400)
        self.assertTrue(0 <= p["disk"]["act"] <= 100)
        self.assertIn("r", p["disk"])
        self.assertEqual(p["cpu"]["load"], 25.0)

    def test_json_line_is_strict_and_small(self):
        s = make_sampler()
        line = json.dumps(s.sample(), separators=(",", ":"), allow_nan=False)
        self.assertLess(len(line), 1400)                             # the firmware's line buffer is 1536
        self.assertNotIn("null", line)
        self.assertNotIn("NaN", line)

    def test_missing_data_is_omitted_not_null(self):
        s = make_sampler(FakePsutil(per_core=(), freq=None))
        p = s.sample()
        self.assertNotIn("freq", p["cpu"])                           # unknown frequency
        self.assertNotIn("per_core", p["cpu"])
        self.assertNotIn("power", p["cpu"])
        self.assertNotIn("volt", p["cpu"])
        self.assertNotIn("fan", p["cpu"])
        json.dumps(p, allow_nan=False)

    def test_many_cores_are_grouped(self):
        s = make_sampler(FakePsutil(per_core=[50.0] * 64))
        self.assertEqual(len(s.sample()["cpu"]["per_core"]), 16)


if __name__ == "__main__":
    unittest.main()
