import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from memory_budget import MemoryGuard,BudgetExceeded
from memory_trace import MemoryTrace


class MemoryTraceTests(unittest.TestCase):
    def test_first_crossing_preserved_with_phase_even_after_recovery(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'memory.jsonl'
            with MemoryTrace(path,ring_size=2) as trace:
                guard=MemoryGuard(hard_bytes=100,soft_bytes=90,system_fraction=.92,observer=trace.observe)
                sample=dict(process_private_bytes=20,process_rss_bytes=10,process_peak_rss_bytes=10,system_used_fraction=.6)
                trace.phase('backward',7,2,dict(allocated_bytes=123))
                for fraction in (.6,.93,.5):
                    with patch('memory_budget.snapshot',return_value=dict(sample,system_used_fraction=fraction)):guard._observe()
                with self.assertRaises(BudgetExceeded):guard.check()
                stats=trace.stats();event=stats['first_crossing']['event']
                self.assertEqual(event['phase'],'backward');self.assertEqual(event['step'],7)
                self.assertEqual(event['counters']['system_used_fraction'],.93)
                self.assertEqual(guard.stats()['first_violation']['sample_index'],2)
                self.assertEqual(len(stats['last_samples']),2)
            self.assertEqual(len([json.loads(x) for x in path.read_text().splitlines()]),3)

    def test_bounded_file_keeps_crossing_metadata_when_full_and_closes(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'memory.jsonl';trace=MemoryTrace(path,maximum_bytes=1024,ring_size=2)
            with trace:
                for _ in range(20):trace.observe(dict(data='x'*200),None)
                trace.observe(dict(data='crossing'),'system_physical_memory_threshold')
                self.assertTrue(trace.stats()['truncated'])
                self.assertIsNotNone(trace.stats()['first_crossing'])
            self.assertLessEqual(path.stat().st_size,1024)
            with self.assertRaises(RuntimeError):trace.observe({},None)


if __name__=='__main__':unittest.main()
