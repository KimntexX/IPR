from pathlib import Path
import tempfile
import unittest
from campaign_artifacts import sha256
from dru_execution import numerical_update_ast,validate_sources


class ExecutionContractTests(unittest.TestCase):
    def test_monitoring_only_ast_change_is_distinct_from_numeric_change(self):
        original='def update(x):\n    check()\n    return x*2\n'
        observed='def update(x):\n    phase("boundary")\n    check()\n    return x*2\n'
        self.assertEqual(numerical_update_ast(original),numerical_update_ast(observed))
        self.assertNotEqual(numerical_update_ast(original),numerical_update_ast(observed.replace('x*2','x*3')))

    def test_source_change_requires_exact_explicit_amendment(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);folder=root/'tests/neural';folder.mkdir(parents=True)
            path=folder/'worker.py';path.write_text('before',encoding='utf-8');prior=sha256(path)
            plan=dict(tools={'worker.py':prior});validate_sources(plan,None,root)
            path.write_text('after',encoding='utf-8');current=sha256(path)
            with self.assertRaises(ValueError):validate_sources(plan,None,root)
            execution=dict(tool_amendments={'worker.py':dict(from_sha256=prior,to_sha256=current)})
            validate_sources(plan,execution,root)
            path.write_text('unexpected',encoding='utf-8')
            with self.assertRaises(ValueError):validate_sources(plan,execution,root)


if __name__=='__main__':unittest.main()
