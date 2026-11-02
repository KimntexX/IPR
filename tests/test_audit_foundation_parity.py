import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import audit_foundation_parity as audit


def payload(*renders):
    out = b""
    for pixels in renders:
        out += json.dumps(dict(ok=True, width=2, height=1, bytes=len(pixels))).encode() + b"\n" + pixels
    return out


class DigestTests(unittest.TestCase):
    def test_each_render_has_its_digest(self):
        results = audit.digests(payload(b"abcd", b"\n\n"), 2)
        self.assertEqual(results[0]["pixel_sha256"], hashlib.sha256(b"abcd").hexdigest())
        self.assertEqual(results[1]["bytes"], 2, "a newline among the pixels does not end them")

    def test_a_failed_render_stops_the_comparison(self):
        with self.assertRaisesRegex(RuntimeError, "Render failed"):
            audit.digests(b'{"ok":false,"error":"x"}\n', 1)

    def test_a_truncated_payload_stops_the_comparison(self):
        with self.assertRaisesRegex(RuntimeError, "Incomplete"):
            audit.digests(b'{"ok":true,"width":1,"height":1,"bytes":9}\nabc', 1)

    def test_requests_cover_the_states_and_the_signed_development(self):
        lines = audit.requests(Path("a.CR3")).splitlines()
        self.assertEqual(len(lines), len(audit.STATES) + 2)
        self.assertIn("cameraDenoise", lines[-2])
        self.assertEqual(json.loads(lines[-1]), {"op": "quit"})


class CompareTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.source = Path(self.directory.name) / "a.CR3"
        self.source.write_bytes(b"raw")
        self.count = len(audit.STATES) + 1
        self.same = [dict(width=1, height=1, bytes=1, pixel_sha256="x")] * self.count

    def test_equal_renders_count_no_difference(self):
        records = []
        with patch.object(audit, "render", return_value=self.same):
            self.assertEqual(audit.compare("b", "c", [self.source], ".", records.append), 0)
        self.assertEqual(len(records), self.count)
        self.assertTrue(all(record["identical"] for record in records))

    def test_a_different_render_is_counted_under_its_state(self):
        other = list(self.same)
        other[1] = dict(self.same[1], pixel_sha256="y")
        records = []
        with patch.object(audit, "render", side_effect=[self.same, other]):
            self.assertEqual(audit.compare("b", "c", [self.source], ".", records.append), 1)
        self.assertFalse(records[1]["identical"])
        self.assertEqual(records[1]["state"], "exposure=1")

    def test_a_modified_source_stops_the_comparison(self):
        def changing(*_):
            self.source.write_bytes(b"changed")
            return self.same
        with patch.object(audit, "render", side_effect=changing):
            with self.assertRaisesRegex(RuntimeError, "Source changed"):
                audit.compare("b", "c", [self.source], ".", lambda _: None)


if __name__ == "__main__":
    unittest.main()
