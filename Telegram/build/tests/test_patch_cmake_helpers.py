import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location(
    'patch_cmake_helpers', ROOT / 'Telegram/build/patch_cmake_helpers.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
SOURCE = (ROOT / 'cmake/external/qt/package.cmake').read_text(encoding='utf-8')


class QtDependencyPathsTests(unittest.TestCase):
    def test_zlib_matches_preparation_and_linker(self):
        patched = module.patch_source(SOURCE)
        recipe = (ROOT / 'Telegram/build/prepare/prepare.py').read_text(encoding='utf-8')
        linker = (ROOT / 'cmake/external/zlib/CMakeLists.txt').read_text(encoding='utf-8')
        for name in ('libzsd.lib', 'libzs.lib'):
            self.assertIn(name, recipe)
            self.assertIn(name, linker)
            self.assertIn(name, patched)
        self.assertNotIn('zlibstatic', patched)

    def test_webp_matches_all_prepared_architectures(self):
        patched = module.patch_source(SOURCE)
        recipe = (ROOT / 'Telegram/build/prepare/prepare.py').read_text(encoding='utf-8')
        for arch in ('ARM64', 'x64', 'x86'):
            self.assertIn(f"'X8664': '{arch}'", recipe)
            self.assertIn(f'set(webp_arch {arch})', patched)
        self.assertIn('if (build_winarm)', patched)
        self.assertIn('elseif (build_win64)', patched)
        self.assertEqual(patched.count('/release-static/${webp_arch}/'), 3)
        self.assertNotIn('/release-static/x86/', patched)

    def test_repeated_configuration_is_idempotent(self):
        patched = module.patch_source(SOURCE)
        self.assertEqual(module.patch_source(patched), patched)

    def test_unknown_helper_fails_before_configuration(self):
        with self.assertRaises(RuntimeError):
            module.patch_source('unrecognized helpers')


if __name__ == '__main__':
    unittest.main()
