import sys
from pathlib import Path
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import audit_render_server_parity as audit  # noqa: E402

STATE = dict(scene="a", source="a.CR3", feature="contrast", value=10)


def renders(pixels):
    return lambda source, feature, value: (pixels, dict(profile="p"))


def fails(source, feature, value):
    raise ValueError("no decode")


class RenderServerParity(unittest.TestCase):
    def setUp(self):
        self.pixels = np.arange(12, dtype=np.uint16).reshape(2, 2, 3)

    def test_same_bytes_agree(self):
        self.assertTrue(audit.compare_state(STATE, renders(self.pixels), renders(self.pixels.copy()))["identical"])

    def test_one_changed_value_differs(self):
        changed = self.pixels.copy()
        changed[1, 1, 2] += 1
        self.assertFalse(audit.compare_state(STATE, renders(self.pixels), renders(changed))["identical"])

    def test_same_bytes_in_another_shape_differ(self):
        self.assertFalse(audit.compare_state(STATE, renders(self.pixels),
                                             renders(self.pixels.reshape(1, 4, 3)))["identical"])

    def test_a_changed_declaration_differs(self):
        other = lambda source, feature, value: (self.pixels, dict(profile="q"))
        self.assertFalse(audit.compare_state(STATE, renders(self.pixels), other)["identical"])

    def test_the_time_and_the_build_of_a_render_do_not_count(self):
        first = lambda source, feature, value: (self.pixels, dict(declaration=dict(
            created_at_utc="10:00", executable_sha256="a", dcp_profile="p")))
        second = lambda source, feature, value: (self.pixels, dict(declaration=dict(
            created_at_utc="11:00", executable_sha256="b", dcp_profile="p")))
        self.assertTrue(audit.compare_state(STATE, first, second)["identical"])

    def test_the_rest_of_the_declaration_counts(self):
        first = lambda source, feature, value: (self.pixels, dict(declaration=dict(dcp_profile="p")))
        second = lambda source, feature, value: (self.pixels, dict(declaration=dict(dcp_profile="q")))
        self.assertFalse(audit.compare_state(STATE, first, second)["identical"])

    def test_a_failure_on_either_side_differs(self):
        self.assertFalse(audit.compare_state(STATE, fails, renders(self.pixels))["identical"])
        self.assertFalse(audit.compare_state(STATE, renders(self.pixels), fails)["identical"])

    def test_a_failure_on_both_sides_is_not_a_proof(self):
        self.assertFalse(audit.compare_state(STATE, fails, fails)["identical"])

    def test_summary_lists_what_differs(self):
        same = audit.compare_state(STATE, renders(self.pixels), renders(self.pixels))
        other = audit.compare_state(STATE, fails, renders(self.pixels))
        summary = audit.summarize([same, other])
        self.assertEqual((summary["states"], summary["identical"], summary["different"]), (2, 1, [other]))


if __name__ == "__main__":
    unittest.main()
