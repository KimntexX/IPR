import unittest
from queued_sleep_watch import blockers, finished


class QueuedSleepTests(unittest.TestCase):
    def test_missing_completion_or_live_worker_prevents_sleep(self):
        self.assertFalse(finished(dict(pid=10), lambda p: False))
        self.assertFalse(finished(dict(pid=10, finished_at_utc='now'), lambda p: True))
        state = dict(pid=10, stages=[dict(pid=20)], finished_at_utc='now')
        self.assertFalse(finished(state, lambda p: p == 20))
        self.assertTrue(finished(state, lambda p: False))

    def test_unidentified_python_and_build_block_sleep(self):
        rows = [dict(ProcessId=10, CommandLine=None), dict(ProcessId=20, CommandLine='cmake --build other')]
        self.assertEqual(blockers(rows, 30), rows)

    def test_ide_and_watcher_do_not_block(self):
        rows = [dict(ProcessId=10, CommandLine='python run-jedi-language-server.py'),
                dict(ProcessId=20, CommandLine='python queued_sleep_watch.py'),
                dict(ProcessId=30, CommandLine=None)]
        self.assertEqual(blockers(rows, 30), [])


if __name__ == '__main__':
    unittest.main()
