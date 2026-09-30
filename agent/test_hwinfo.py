"""Tests for hwinfo.py:   python -m unittest discover -s agent -v

Every vendor / OS path is exercised with recorded or synthetic data, so the parsers are
checked on any machine. test_live_* run only where the real hardware API exists.
"""
import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hwinfo  # noqa: E402


class TestNames(unittest.TestCase):
    def test_short_gpu_name(self):
        cases = {
            "NVIDIA GeForce RTX 4070": "GeForce RTX 4070",
            "NVIDIA GeForce RTX 4070 Ti SUPER": "RTX 4070 Ti SUPER",
            "AMD Radeon(TM) Graphics": "Radeon Graphics",
            "AMD Radeon RX 7800 XT": "Radeon RX 7800 XT",
            "Navi 32 [Radeon RX 7700 XT / 7800 XT]": "RX 7700 XT / 7800 XT",
            "Intel(R) UHD Graphics 770": "UHD Graphics 770",
            "Intel(R) Arc(TM) A770 Graphics": "Arc A770 Graphics",
            "Apple M1": "Apple M1",
            "": "GPU",
        }
        for raw, want in cases.items():
            got = hwinfo.short_gpu_name(raw)
            self.assertEqual(got, want, raw)
            self.assertLessEqual(len(got), 23, raw)          # firmware buffer is 24 incl. NUL


class TestMerge(unittest.TestCase):
    def test_same_card_from_two_sources_is_one_gpu(self):
        nv = [{"name": "GeForce RTX 4070", "discrete": True, "_strong": True, "load": 40.0, "temp": 60.0}]
        lhm = [{"name": "RTX 4070", "load": 39.0, "temp": 61.0, "power": 120.0}]
        g = hwinfo.merge_gpus(nv, lhm)
        self.assertEqual(len(g), 1)
        self.assertEqual((g[0]["load"], g[0]["temp"], g[0]["power"]), (40.0, 60.0, 120.0))   # NVML wins, LHM fills
        self.assertNotIn("_strong", g[0])

    def test_discrete_sorted_first_and_missing_sources_ok(self):
        g = hwinfo.merge_gpus(None, [], [{"name": "UHD Graphics 770", "discrete": False, "load": 5.0},
                                         {"name": "Radeon RX 7800 XT", "discrete": True, "load": 80.0}])
        self.assertEqual([x["name"] for x in g], ["Radeon RX 7800 XT", "UHD Graphics 770"])

    def test_single_unnamed_reading_folds_into_single_gpu(self):
        # Windows AMD: PDH (registry name) + ADL (driver name spelled differently)
        g = hwinfo.merge_gpus([{"name": "Radeon RX 6600", "load": 30.0}], [{"name": "AMD Radeon Adapter", "temp": 55.0}])
        self.assertEqual(len(g), 1)
        self.assertEqual(g[0]["temp"], 55.0)


class TestNvidiaSmi(unittest.TestCase):
    def test_parse(self):
        text = ("NVIDIA GeForce RTX 3080, 57, 66, 4321, 10240, 220.55, 1905, 48\n"
                "NVIDIA GeForce GTX 1650, 3, 41, 300, 4096, [N/A], 300, [N/A]\n")
        a, b = hwinfo.parse_nvidia_smi(text)
        self.assertEqual((a["name"], a["load"], a["temp"], a["vram_total"], a["power"]),
                         ("GeForce RTX 3080", 57.0, 66.0, 10.0, 220.55))
        self.assertIsNone(b["power"])                        # laptop GPUs report [N/A]
        self.assertIsNone(b["fan"])

    def test_garbage(self):
        self.assertEqual(hwinfo.parse_nvidia_smi(""), [])
        self.assertEqual(hwinfo.parse_nvidia_smi("NVIDIA-SMI has failed"), [])


def _write(root, rel, text):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(str(text))


class TestLinuxDrm(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        r = self.tmp.name
        # AMD discrete card
        _write(r, "card0/device/vendor", "0x1002")
        _write(r, "card0/device/gpu_busy_percent", "73")
        _write(r, "card0/device/mem_info_vram_used", 5 * 2**30)
        _write(r, "card0/device/mem_info_vram_total", 16 * 2**30)
        _write(r, "card0/device/hwmon/hwmon3/temp1_label", "edge")
        _write(r, "card0/device/hwmon/hwmon3/temp1_input", "64000")
        _write(r, "card0/device/hwmon/hwmon3/temp2_label", "junction")
        _write(r, "card0/device/hwmon/hwmon3/temp2_input", "78000")
        _write(r, "card0/device/hwmon/hwmon3/power1_average", "187000000")
        _write(r, "card0/device/hwmon/hwmon3/fan1_input", "1450")
        _write(r, "card0/device/hwmon/hwmon3/freq1_input", "2400000000")
        # Intel iGPU
        _write(r, "card1/device/vendor", "0x8086")
        _write(r, "card1/gt/gt0/rc6_residency_ms", "1000")
        _write(r, "card1/gt/gt0/rps_act_freq_mhz", "1300")
        # connector entries and an NVIDIA card must be ignored here
        _write(r, "card0-DP-1/status", "connected")
        _write(r, "card2/device/vendor", "0x10de")
        self.probe = hwinfo.LinuxDrmProbe(root=r)

    def tearDown(self):
        self.tmp.cleanup()

    def test_cards_found(self):
        self.assertEqual([c[2] for c in self.probe.cards], ["amd", "intel"])

    def test_amd(self):
        amd = self.probe.read()[0]
        self.assertEqual((amd["load"], amd["temp"], amd["vram_used"], amd["vram_total"]), (73.0, 64.0, 5.0, 16.0))
        self.assertEqual((amd["power"], amd["fan"], amd["clock"]), (187.0, 1450.0, 2400.0))
        self.assertTrue(amd["discrete"])

    def test_intel_busy_from_rc6(self):
        self.probe.read()                                     # first read only primes the counter
        time.sleep(0.2)
        _write(self.tmp.name, "card1/gt/gt0/rc6_residency_ms", "1050")   # idle 50 of ~200 ms
        intel = self.probe.read()[1]
        self.assertAlmostEqual(intel["load"], 75.0, delta=12.0)
        self.assertEqual(intel["clock"], 1300.0)
        self.assertFalse(intel["discrete"])


class TestMacIoreg(unittest.TestCase):
    APPLE = ('| |   "PerformanceStatistics" = {"In use system memory (driver)"=0,"Alloc system memory"=2964094976,'
             '"Tiler Utilization %"=23,"Renderer Utilization %"=23,"Device Utilization %"=23,'
             '"In use system memory"=755056640}\n')
    AMD = ('"PerformanceStatistics" = {"GPU Activity(%)"=41,"vramUsedBytes"=2147483648,'
           '"vramFreeBytes"=6442450944}\n')

    def test_apple_silicon(self):
        g = hwinfo.parse_ioreg_accelerators(self.APPLE, [("Apple M1", False)])
        self.assertEqual(g, [{"name": "Apple M1", "vendor": "apple", "discrete": False, "load": 23.0}])

    def test_intel_mac_with_radeon(self):
        g = hwinfo.parse_ioreg_accelerators(self.APPLE + self.AMD,
                                            [("UHD Graphics 630", False), ("Radeon Pro 5500M", True)])
        self.assertEqual(g[1]["load"], 41.0)
        self.assertEqual((g[1]["vram_used"], g[1]["vram_total"]), (2.0, 8.0))


class TestWinPdh(unittest.TestCase):
    def test_aggregation(self):
        L1, L2 = "luid_0x00000000_0x0000D1E5", "luid_0x00000000_0x0000A001"
        eng = [
            (f"pid_100_{L1}_phys_0_eng_0_engtype_3D", 30.0),
            (f"pid_200_{L1}_phys_0_eng_0_engtype_3D", 25.0),       # 3D summed over processes = 55
            (f"pid_100_{L1}_phys_0_eng_3_engtype_VideoDecode", 20.0),
            (f"pid_300_{L2}_phys_0_eng_0_engtype_3D", 4.0),
            ("bogus instance", 99.0),
        ]
        mem = [(f"{L1}_phys_0", 6 * 2**30), (f"{L2}_phys_0", 0.2 * 2**30)]
        adapters = [{"name": "UHD Graphics 770", "vendor": "intel", "discrete": False, "vram_total": 0.5},
                    {"name": "Radeon RX 7800 XT", "vendor": "amd", "discrete": True, "vram_total": 16.0}]
        g = hwinfo.pdh_gpus(eng, mem, adapters)
        self.assertEqual((g[0]["name"], g[0]["load"], g[0]["vram_used"], g[0]["vram_total"]),
                         ("Radeon RX 7800 XT", 55.0, 6.0, 16.0))
        self.assertEqual((g[1]["name"], g[1]["load"]), ("UHD Graphics 770", 4.0))

    def test_empty(self):
        self.assertEqual(hwinfo.pdh_gpus([], [], []), [])


class TestAppleTemps(unittest.TestCase):
    def test_classify(self):
        readings = [("pACC MTR Temp Sensor3", 81.0), ("eACC MTR Temp Sensor0", 76.2), ("PMU tdie1", 78.8),
                    ("GPU MTR Temp Sensor1", 52.4), ("GPU MTR Temp Sensor4", 49.9),
                    ("PMGR SOC Die Temp Sensor0", 70.0), ("NAND CH0 temp", 58.0),
                    ("PMU tdev1", -22.2), ("gas gauge battery", 33.0)]
        self.assertEqual(hwinfo.classify_apple_temps(readings), {"cpu": 81.0, "gpu": 52.4, "ssd": 58.0})

    def test_stuck_gpu_sensor_falls_back_to_soc(self):
        readings = [("pACC MTR Temp Sensor2", 78.9), ("GPU MTR Temp Sensor1", 30.0),
                    ("PMGR SOC Die Temp Sensor0", 75.8), ("PMGR SOC Die Temp Sensor1", 75.7)]
        self.assertEqual(hwinfo.classify_apple_temps(readings)["gpu"], 75.8)

    def test_nothing(self):
        self.assertEqual(hwinfo.classify_apple_temps([]), {})


class TestHub(unittest.TestCase):
    def test_hub_runs_probes_and_survives_errors(self):
        class Good:
            interval = 0.05

            def available(self):
                return True

            def read(self):
                return [{"name": "X", "load": 1.0}]

        class Broken(Good):
            def read(self):
                raise RuntimeError("driver went away")

        class Absent(Good):
            def available(self):
                raise OSError("no such API")

        hub = hwinfo.HardwareHub([Good(), Broken(), Absent()], {"lhm": (lambda: {"cpu_temp": 50.0}, 0.05)})
        hub.start()
        time.sleep(0.3)
        hub.stop_evt.set()
        snap = hub.snapshot()
        self.assertEqual(snap["Good"][0]["load"], 1.0)
        self.assertIsNone(snap["Broken"])
        self.assertNotIn("Absent", snap)
        self.assertEqual(snap["lhm"]["cpu_temp"], 50.0)


@unittest.skipUnless(hwinfo.IS_MAC, "macOS only")
class TestLiveMac(unittest.TestCase):
    def test_live_gpu_load(self):
        g = hwinfo.MacGpuProbe().read()
        self.assertTrue(g and 0.0 <= g[0]["load"] <= 100.0, g)

    @unittest.skipUnless(os.uname().machine == "arm64", "Apple Silicon only")
    def test_live_temps(self):
        t = hwinfo.MacThermalProbe().read()
        self.assertTrue(20.0 < t.get("cpu", 0) < 110.0, t)


if __name__ == "__main__":
    unittest.main()
