import tempfile
import unittest
from pathlib import Path

from study_demosaic import main as demosaic_main
from study_sources import sources


class StudySourcesTests(unittest.TestCase):
    def test_sources_are_the_raws_at_the_base_root(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            for name in ("A.CR3", "B.cr2", "C.tif", "D.CR3.ipr"):
                (base / name).write_bytes(b"x")
            (base / "neutre").mkdir()
            (base / "neutre" / "E.CR3").write_bytes(b"x")
            self.assertEqual([p.name for p in sources(base, [])], ["A.CR3", "B.cr2"])
            self.assertEqual([p.name for p in sources(base, ["B"])], ["B.cr2"])

    def test_a_scene_in_several_folders_is_taken_once_from_the_first(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second = Path(directory) / "base", Path(directory) / "other"
            first.mkdir()
            second.mkdir()
            for folder, name in ((first, "A.CR3"), (second, "A.CR3"), (second, "B.CR3")):
                (folder / name).write_bytes(b"x")
            self.assertEqual([p.parent.name + "/" + p.name for p in sources([first, second], [])],
                             ["base/A.CR3", "other/B.CR3"])

    def test_missing_scene_or_empty_base_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            with self.assertRaises(SystemExit):
                sources(base, [])
            (base / "A.CR3").write_bytes(b"x")
            with self.assertRaises(SystemExit):
                sources(base, ["Z"])

    def test_missing_demosaic_probe_is_refused_before_any_output(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(SystemExit):
                demosaic_main(["--data-version", "1", "--probe", str(Path(directory) / "none.exe"),
                               "--output-dir", directory])
            self.assertEqual(list(Path(directory).iterdir()), [])


if __name__ == "__main__":
    unittest.main()
