"""Native CI argument isolation, without installing or changing dependencies."""
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import github_build as build


class GithubBuildTests(unittest.TestCase):
    def test_macos_dependency_headers_do_not_expose_global_homebrew_qt(self):
        for prefix, qt in (('/opt/homebrew', '/aqt/Qt/6.8.3/macos'),
                           ('/usr/local', '/usr/local/opt/qt')):
            with self.subTest(prefix=prefix), patch.object(build.platform,'system',return_value='Darwin'), patch.object(build.subprocess,'check_output',return_value=prefix+'\n'), patch.dict(os.environ,QT_ROOT_DIR=qt):
                args = build.native_arguments()
            config = dict(arg.removeprefix('--cmake-arg=-D').split('=',1) for arg in args)
            self.assertEqual(config['Qt6_DIR'], qt+'/lib/cmake/Qt6')
            self.assertEqual(config['LIBRAW_INCLUDE_DIR'],prefix+'/opt/libraw/include')
            self.assertEqual(config['TIFF_INCLUDE_DIR'],prefix+'/opt/libtiff/include')
            self.assertNotIn(prefix,config['CMAKE_PREFIX_PATH'].split(';'))
            self.assertEqual(config['CMAKE_PREFIX_PATH'].split(';')[0],qt)
            self.assertEqual(config['LIBRAW_LIBRARY'],prefix+'/opt/libraw/lib/libraw.dylib')

    def test_windows_manual_link_contract_is_unchanged(self):
        with patch.object(build.platform,'system',return_value='Windows'), patch.dict(os.environ,IPR_VCPKG_ROOT='vcpkg'):
            args=build.native_arguments()
        self.assertIn('--cmake-arg=-DVCPKG_TARGET_TRIPLET=x64-windows',args)
        self.assertIn('build-ci/vcpkg_installed/x64-windows/bin',args)
        self.assertTrue(any(arg.endswith('manual-link/raw.lib') or arg.endswith('manual-link\\raw.lib') for arg in args))

    def test_linux_requires_no_adapter_arguments(self):
        with patch.object(build.platform,'system',return_value='Linux'), patch.object(build.subprocess,'check_output') as call:
            self.assertEqual(build.native_arguments(),[])
        call.assert_not_called()

    def test_user_arguments_are_preserved_as_distinct_tokens(self):
        args=['--action','all','--profile','production','--mode','standalone']
        with patch.object(build,'native_arguments',return_value=['--cmake-arg=-DQt6_DIR=/SDK with space/Qt']), patch.object(build.subprocess,'call',return_value=7) as call:
            self.assertEqual(build.main(args),7)
        self.assertEqual(call.call_args.args[0][2:2+len(args)],args)
        self.assertEqual(call.call_args.args[0][-1],'--cmake-arg=-DQt6_DIR=/SDK with space/Qt')

    def test_missing_qt_is_an_error_not_an_implicit_homebrew_fallback(self):
        with patch.object(build.platform,'system',return_value='Darwin'), patch.object(build.subprocess,'check_output',return_value='/opt/homebrew'), patch.dict(os.environ,QT_ROOT_DIR=''), self.assertRaises(ValueError):
            build.native_arguments()


if __name__ == '__main__':
    unittest.main()
