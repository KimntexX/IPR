import json
import os
from pathlib import Path
import subprocess
import sys
import unittest

from memory_budget import snapshot, violation, MemoryGuard


class MemoryBudgetTests(unittest.TestCase):
    def test_admission_thresholds_and_invalid_configuration(self):
        sample = dict(process_private_bytes=20, process_rss_bytes=30, system_used_fraction=.5)
        self.assertIsNone(violation(sample, 100, .8))
        self.assertEqual(violation(dict(sample, process_rss_bytes=100), 100, .8), 'process_soft_memory_limit')
        self.assertEqual(violation(dict(sample, system_used_fraction=.8), 100, .8), 'system_physical_memory_threshold')
        with self.assertRaises(ValueError):
            MemoryGuard(hard_bytes=100, soft_bytes=100)

    @unittest.skipUnless(os.name == 'nt', 'Windows memory API')
    def test_live_counters_are_sane(self):
        value = snapshot()
        self.assertGreater(value['process_private_bytes'], 0)
        self.assertGreater(value['process_rss_bytes'], 0)
        self.assertTrue(0 <= value['system_used_fraction'] <= 1)

    @unittest.skipUnless(os.name == 'nt', 'Windows OS quota')
    def test_hard_quota_denies_allocation_in_isolated_child_and_thread_joins(self):
        code = '''import json
from memory_budget import MemoryGuard
guard = MemoryGuard(hard_bytes=128*1024**2, soft_bytes=120*1024**2, system_fraction=.999)
denied = False
with guard:
    try:
        allocation = bytearray(256*1024**2)
    except MemoryError:
        denied = True
print(json.dumps(dict(denied=denied, joined=not guard.thread.is_alive(), peak=guard.stats()['os_job_peak_commit_bytes'])))
'''
        result = subprocess.run([sys.executable, '-c', code], cwd=Path(__file__).parent,
            capture_output=True, text=True, timeout=15, check=True)
        value = json.loads(result.stdout)
        self.assertTrue(value['denied'])
        self.assertTrue(value['joined'])
        self.assertLessEqual(value['peak'], 128*1024**2)


if __name__ == '__main__':
    unittest.main()
