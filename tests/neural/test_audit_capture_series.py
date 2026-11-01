"""Safety and preview contracts of the capture screening tool, without RAWs."""
import io
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import MagicMock, patch

from PIL import Image
import rawpy

import audit_capture_series as audit


class CaptureAuditTests(unittest.TestCase):
    def test_output_cannot_escape_dedicated_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)/'output'
            with self.assertRaises(ValueError):
                audit.run(Path(temporary), 'first', 'last', output)
            self.assertFalse(output.exists())

    def test_empty_input_is_rejected_before_output_creation(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(audit.__file__).resolve().parents[2]/'build-patch_neural_restoration'/'absent-input-test'
            with self.assertRaises(ValueError):
                audit.run(Path(temporary), 'first', 'last', output)
            self.assertFalse(output.exists())

    def test_preview_is_bounded_and_raw_is_not_unpacked(self):
        payload = io.BytesIO()
        Image.new('RGB',(1600,1200),'gray').save(payload,format='JPEG')
        context = MagicMock()
        raw = context.__enter__.return_value
        raw.extract_thumb.return_value=SimpleNamespace(format=rawpy.ThumbFormat.JPEG,data=payload.getvalue())
        with patch.object(audit.rawpy,'RawPy',return_value=context):
            image, size = audit.small_preview(Path('fixture.CR3'))
        self.assertEqual(size,[1600,1200])
        self.assertLessEqual(max(image.size),800)
        raw.unpack.assert_not_called()
        raw.postprocess.assert_not_called()
        context.__exit__.assert_called_once()

    def test_invalid_thumbnail_closes_raw(self):
        context=MagicMock()
        context.__enter__.return_value.extract_thumb.return_value=SimpleNamespace(format=None)
        with patch.object(audit.rawpy,'RawPy',return_value=context), self.assertRaises(ValueError):
            audit.small_preview(Path('fixture.CR3'))
        context.__exit__.assert_called_once()


if __name__=='__main__':
    unittest.main()
