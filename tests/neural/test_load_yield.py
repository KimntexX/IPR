import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from load_control import YieldController
from dru_execution import load
from campaign_artifacts import sha256


class YieldAndCapTests(unittest.TestCase):
    def test_sustained_pressure_yields_and_recovery_resets_the_timer(self):
        high=dict(memory=dict(system_used_fraction=.86),cpu=dict(external_cpu_fraction=.1))
        low=dict(memory=dict(system_used_fraction=.6),cpu=dict(external_cpu_fraction=.1))
        with patch('load_control.LoadMonitor') as monitor,patch('load_control.time.monotonic',side_effect=[100.,110.,120.,130.,146.]):
            monitor.return_value.sample.side_effect=[high,low,high,high,high]
            controller=YieldController(dict(yield_memory_fraction=.85,yield_external_cpu_fraction=.8,pressure_seconds=15))
            self.assertEqual([controller.check() for _ in range(5)],[False,False,False,False,True])

    def test_execution_contract_cannot_raise_global_or_hard_caps(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);build=root/'build-patch_neural_restoration';build.mkdir()
            scientific=build/'scientific.json';scientific.write_text('{}',encoding='utf-8')
            path=build/'execution.json'
            for fraction,hard,soft in ((.93,6144,5120),(.92,7000,5120),(.92,6144,6144)):
                path.write_text(json.dumps(dict(scientific_plan_sha256=[sha256(scientific)],
                    resources=dict(system_fraction=fraction,hard_mib=hard,soft_mib=soft),runtime_tools={})),encoding='utf-8')
                with self.assertRaises(ValueError):load(path,scientific,root)


if __name__=='__main__':unittest.main()
