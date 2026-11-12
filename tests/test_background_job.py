import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location("background_job",Path(__file__).resolve().parents[1]/"scripts/background_job.py")
job=importlib.util.module_from_spec(spec);spec.loader.exec_module(job)


class BackgroundJobTests(unittest.TestCase):
    def test_success_failure_and_timeout_are_persisted(self):
        for command,timeout,status in (([sys.executable,"-c","print('done')"],10,"succeeded"),
                                       ([sys.executable,"-c","raise SystemExit(3)"],10,"failed"),
                                       ([sys.executable,"-c","import time;time.sleep(5)"],.05,"failed")):
            with tempfile.TemporaryDirectory() as directory:
                path=Path(directory)/"spec.json"
                job.save(path,dict(name="unit",cwd=directory,argv=command,timeout_seconds=timeout))
                if sys.platform=="win32":
                    with patch("winsound.MessageBeep"):job.worker(path)
                else:job.worker(path)
                state=json.loads(path.with_name("status.json").read_text(encoding="utf-8"))
                self.assertEqual(state["status"],status)
                self.assertIn("finished_at_utc",state)
                self.assertTrue(path.with_name("stdout.log").is_file())


if __name__=="__main__":unittest.main()
