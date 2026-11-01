import json
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import audit_raw_render_parity as audit


class ParityProtocolTests(unittest.TestCase):
    def payload(self, pixels=b"\x01\0\x02\0\x03\0"):
        declaration = {key: None for key in (
            "source_sha256", "settings", "dcp_profile", "lcp_profile", "lcp_sha256",
            "raw_normalization", "lens_geometry_applied", "lens_vignetting_applied",
            "chromatic_aberration_applied",
        )}
        header = dict(ok=True, width=1, height=1, dtype="<u2", bytes=6,
                      icc_base64="cHJvZmlsZQ==", declaration=declaration)
        return json.dumps(header).encode() + b"\n" + pixels

    @patch.object(audit.subprocess, "run")
    def test_signed_state_is_explicit_and_hash_covers_all_samples(self, run):
        run.return_value = SimpleNamespace(stdout=self.payload())
        before = audit.render(Path("server"), Path("source.cr3"), True, Path("."))
        request = json.loads(run.call_args.kwargs["input"].splitlines()[0])
        self.assertEqual(request["cameraDenoise"], dict(luminance=0, chrominance=0))
        self.assertEqual(run.call_args.kwargs["timeout"], 240)
        run.return_value = SimpleNamespace(stdout=self.payload(b"\x01\0\x02\0\x04\0"))
        after = audit.render(Path("server"), Path("source.cr3"), False, Path("."))
        self.assertNotEqual(before["pixel_sha256"], after["pixel_sha256"])
        request = json.loads(run.call_args.kwargs["input"].splitlines()[0])
        self.assertNotIn("cameraDenoise", request)

    @patch.object(audit.subprocess, "run")
    def test_partial_and_excess_payloads_are_rejected(self, run):
        for pixels in (b"\x00", b"\x00" * 7):
            run.return_value = SimpleNamespace(stdout=self.payload(pixels))
            with self.assertRaisesRegex(RuntimeError, "payload"):
                audit.render(Path("server"), Path("source.cr3"), False, Path("."))

    @patch.object(audit.subprocess, "run")
    def test_server_error_is_not_a_valid_empty_render(self, run):
        run.return_value = SimpleNamespace(stdout=b'{"ok":false,"error":"bad profile"}\n')
        with self.assertRaisesRegex(RuntimeError, "Render failed"):
            audit.render(Path("server"), Path("source.cr3"), False, Path("."))
