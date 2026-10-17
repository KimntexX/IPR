"""Selection rules only; no compiler, personal preferences or photo corpus required."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_windows", ROOT / "scripts/build_windows.py")
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)
RULES = json.loads((ROOT / "scripts/test-rules.json").read_text(encoding="utf-8"))


class SelectionTests(unittest.TestCase):
    def test_all_group_entrypoints_are_targeted_without_losing_wb_dependents(self):
        for group in ("raw", "lumiere", "courbes", "couleur", "melangeur", "grading", "effets", "bruit", "nettete", "optique"):
            plan = build.select_tests(["tests/analyse_" + group + ".py"], RULES)
            self.assertFalse(plan["full"])
            self.assertEqual(set(plan["python"]), {"group_analysis", "wb_comparison", "wb_adaptation"})

    def test_tone_recovery_selects_production_and_export_regressions(self):
        plan = build.select_tests(["src/core/ToneRecovery.h"], RULES)
        self.assertFalse(plan["full"])
        self.assertEqual(set(plan["tests"]), {"toneRecoveryTests", "imageExportTests"})
        self.assertIn("toneRecoveryTests", build.select_tests(["src/editor/imageeditor.cpp"], RULES)["tests"])

    def test_generator_is_targeted_and_registered(self):
        plan = build.select_tests(["src/tools/generate_test_images.cpp"], RULES)
        self.assertFalse(plan["full"])
        self.assertEqual(set(plan["tests"]), {"testImageGeneratorTests", "imageExportTests"})
        isolated = build.select_tests(["tests/test_image_generator_test.cpp"], RULES)
        self.assertEqual(isolated["targets"], ["testImageGeneratorTests"])

    def test_viewport_is_targeted_and_main_includes_zoom_regressions(self):
        plan = build.select_tests(["src/qml/ImageViewport.qml"], RULES)
        self.assertFalse(plan["full"])
        self.assertEqual(set(plan["tests"]), {"imageViewportQmlTests", "uiPreferencesQmlTests"})
        main = build.select_tests(["src/qml/Main.qml"], RULES)
        self.assertIn("imageViewportQmlTests", main["tests"])
        isolated = build.select_tests(["tests/image_viewport_qml_test.cpp"], RULES)
        self.assertEqual(isolated["tests"], ["imageViewportQmlTests"])

    def test_detail_processing_selects_native_regressions_and_noise_analysis(self):
        plan = build.select_tests(["src/core/DetailProcessing.cpp"], RULES)
        self.assertFalse(plan["full"])
        self.assertEqual(set(plan["tests"]), {"detailProcessingTests", "colorPipelineTests", "imageExportTests"})
        self.assertEqual(plan["python"], ["noise_analysis"])
        isolated = build.select_tests(["tests/detail_processing_test.cpp"], RULES)
        self.assertEqual(isolated["tests"], ["detailProcessingTests"])

    def test_noise_analysis_and_shared_reader_dependencies(self):
        plan = build.select_tests(["tests/analyse_noise.py"], RULES)
        self.assertFalse(plan["full"])
        self.assertEqual(plan["tests"], [])
        self.assertEqual(plan["python"], ["noise_analysis"])
        shared = build.select_tests(["tests/analyse_color_mixer.py"], RULES)
        self.assertEqual(set(shared["python"]), {"color_mixer_analysis", "noise_analysis", "base_colorimetry_analysis"})
        colour = build.select_tests(["tests/analyse_base_colorimetrique_v2.py"], RULES)
        self.assertFalse(colour["full"])
        self.assertEqual(colour["tests"], [])
        self.assertEqual(colour["python"], ["base_colorimetry_analysis"])
        diagnostic = build.select_tests(["tests/investigate_tonal_base.py"], RULES)
        self.assertFalse(diagnostic["full"])
        self.assertEqual(diagnostic["python"], ["base_colorimetry_analysis"])
        for path in ("tests/investigate_dcp_order.py", "tests/audit_dng_baseline.py", "tests/investigate_dcp_normalization.py", "tests/investigate_standard_normalization.py", "tests/prepare_raw_normalization.py"):
            diagnostic = build.select_tests([path], RULES)
            self.assertFalse(diagnostic["full"])
            expected = ["base_colorimetry_analysis", "raw_opening_levels"] if path == "tests/audit_dng_baseline.py" else ["base_colorimetry_analysis"]
            self.assertEqual(diagnostic["python"], expected)
        probe = build.select_tests(["tests/tonal_base_probe.cpp"], RULES)
        self.assertFalse(probe["full"])
        self.assertEqual(set(probe["tests"]), {"rawDecoderTests", "rawBlackLevelEstimatorTests", "cameraProfileEngineTests"})

    def test_documentation_and_no_changes_skip_tests(self):
        self.assertEqual(build.select_tests([], RULES)["tests"], [])
        self.assertEqual(build.select_tests(["docs/example.md"], RULES)["tests"], [])

    def test_raw_opening_tools_are_targeted(self):
        plan=build.select_tests(['tests/raw_opening_probe.cpp'],RULES)
        self.assertFalse(plan['full'])
        self.assertIn('rawOpeningProbeTests',plan['tests'])
        self.assertIn('rawDecoderTests',plan['tests'])
        summary=build.select_tests(['tests/summarize_raw_opening.py'],RULES)
        self.assertFalse(summary['full'])
        self.assertEqual(set(summary['python']),{'raw_opening_analysis','neutral_colour_inspection'})

    def test_raw_normalization_is_targeted(self):
        for path in ("src/raw/RawNormalization.cpp", "src/raw/RawDecoder.cpp", "tests/raw_normalization_test.cpp"):
            plan = build.select_tests([path], RULES)
            self.assertFalse(plan['full'])
            self.assertIn('rawNormalizationTests', plan['tests'])

    def test_raw_development_ui_is_targeted(self):
        plan=build.select_tests(['src/qml/RawDevelopmentModule.qml'],RULES)
        self.assertFalse(plan['full'])
        self.assertIn('editorRawDevelopmentTests',plan['tests'])
        self.assertIn('settingsQmlTests',plan['tests'])

    def test_preferences_are_targeted(self):
        plan = build.select_tests(["src/app/UiPreferences.cpp"], RULES)
        self.assertFalse(plan["full"])
        self.assertEqual(set(plan["tests"]), {"uiPreferencesTests", "uiPreferencesQmlTests", "settingsQmlTests"})

    def test_test_file_and_mixer_mapping(self):
        self.assertEqual(build.select_tests(["tests/image_export_test.cpp"], RULES)["tests"], ["imageExportTests"])
        plan = build.select_tests(["src/core/ColorMixer.cpp"], RULES)
        self.assertFalse(plan["full"])
        self.assertIn("colorMixerNeutralProbe", plan["targets"])
        self.assertIn("color_mixer_analysis", plan["python"])

    def test_full_guards(self):
        for paths in (["CMakeLists.txt"], ["src/editor/imageeditor.h"], ["src/new_component.cpp"],
                      ["scripts/test-rules.json"], [".github/workflows/ci.yml"]):
            self.assertTrue(build.select_tests(paths, RULES)["full"])
        self.assertTrue(build.select_tests([], RULES, force_full=True)["full"])
        self.assertTrue(build.select_tests([], RULES, bootstrap=True)["full"])
        self.assertTrue(build.select_tests([f"src/new{i}.cpp" for i in range(20)], RULES)["full"])

    def test_opening_trials_are_targeted(self):
        plan=build.select_tests(['tests/run_raw_opening_trials.py'],RULES)
        self.assertFalse(plan['full'])
        self.assertIn('rawOpeningSplineTests',plan['tests'])
        self.assertIn('raw_opening_trials',plan['python'])

    def test_slashes_and_deleted_path(self):
        plan = build.select_tests([r"src\raw\RawDecoder.cpp"], RULES)
        self.assertFalse(plan["full"])
        self.assertIn("rawDecoderTests", plan["tests"])

    def test_hashes_ignore_photos_and_detect_untracked_code(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            (root / "tests/image").mkdir(parents=True)
            source = root / "src/example.cpp"
            source.write_text("before", encoding="utf-8")
            (root / "tests/image/private.cpp").write_text("ignored", encoding="utf-8")
            before = build.source_hashes(root)
            self.assertEqual(set(before), {"src/example.cpp"})
            source.write_text("after", encoding="utf-8")
            self.assertNotEqual(before, build.source_hashes(root))
            source.unlink()
            self.assertEqual(build.source_hashes(root), {})


if __name__ == "__main__":
    unittest.main()
