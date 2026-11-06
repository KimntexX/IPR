"""Decision and sleep-call contracts; no real suspension in this test module."""
from contextlib import nullcontext
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock,patch

from study_sleep_watch import decision,evidence
import windows_sleep


class StudySleepTests(unittest.TestCase):
    def test_four_hours_are_a_decision_point_and_finished_work_can_sleep(self):
        self.assertEqual(decision(True,99,100,False),'wait')
        self.assertEqual(decision(True,100,100,False),'stop_and_sleep')
        self.assertEqual(decision(True,100,100,True),'continue_promising')
        self.assertEqual(decision(False,99,100,True),'stop_and_sleep')

    def test_partial_candidate_requires_both_completed_initializations(self):
        status={'status':'completed_dru_bootstrap_not_photographic_qualification'}
        assessment={'promising_candidates':['noise'],'strict_candidates':[]}
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for arm,seed in [('reference',71),('reference',73),('noise',71)]:
                folder=root/arm/f'seed{seed}';folder.mkdir(parents=True)
                (folder/'result.json').write_text(json.dumps(status),encoding='utf-8')
            with patch('study_sleep_watch.assess',return_value=assessment) as assess:
                self.assertEqual(evidence({},root),[])
                assess.assert_not_called()
                folder=root/'noise/seed73';folder.mkdir(parents=True)
                (folder/'result.json').write_text(json.dumps(status),encoding='utf-8')
                self.assertEqual(evidence({},root),[assessment])
                assess.assert_called_once_with({'reference':root/'reference','noise':root/'noise'},256)
                assess.side_effect=ValueError('Unpaired controls')
                self.assertEqual(evidence({},root),[])

    def test_sleep_is_not_hibernation_and_keeps_wake_events_enabled(self):
        power=Mock();power.SetSuspendState.return_value=1
        with patch.object(windows_sleep.ctypes,'WinDLL',return_value=power),patch.object(windows_sleep,'shutdown_privilege',return_value=nullcontext()) as privilege:
            windows_sleep.suspend()
        privilege.assert_called_once_with()
        power.SetSuspendState.assert_called_once_with(False,False,False)

    def test_failed_sleep_is_reported_instead_of_assumed(self):
        power=Mock();power.SetSuspendState.return_value=0
        with patch.object(windows_sleep.ctypes,'WinDLL',return_value=power),patch.object(windows_sleep,'shutdown_privilege',return_value=nullcontext()):
            with self.assertRaises(OSError):windows_sleep.suspend()


if __name__=='__main__':unittest.main()
