import importlib.util
from pathlib import Path
import tempfile
import unittest


spec = importlib.util.spec_from_file_location(
    'windows_sdk', Path(__file__).resolve().parents[1] / 'windows_sdk.py',
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class WindowsSdkTests(unittest.TestCase):
    def test_sdk_servicing_changes_the_fingerprint_without_changing_path(self):
        with tempfile.TemporaryDirectory() as directory:
            header = Path(directory) / 'winrt/base.h'
            header.parent.mkdir()
            header.write_text('2.0.250303.1', encoding='utf-8')
            environment = {'INCLUDE': directory}
            previous = module.winrt_fingerprint(environment)
            header.write_text('2.0.250303.5', encoding='utf-8')
            self.assertNotEqual(module.winrt_fingerprint(environment), previous)

    def test_fingerprint_follows_compiler_include_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('first', 'second'):
                header = root / name / 'winrt/base.h'
                header.parent.mkdir(parents=True)
                header.write_text(name, encoding='utf-8')
            first = str(root / 'first')
            second = str(root / 'second')
            expected = module.winrt_fingerprint({'INCLUDE': first})
            self.assertEqual(module.winrt_fingerprint({
                'INCLUDE': f';"{root / "missing"}";"{first}";{second};',
            }), expected)
            self.assertNotEqual(module.winrt_fingerprint({
                'INCLUDE': f'{second};{first}',
            }), expected)

    def test_missing_headers_fail_before_using_a_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            for environment in ({}, {'INCLUDE': directory}):
                with self.subTest(environment=environment):
                    with self.assertRaisesRegex(RuntimeError, 'initialize Visual Studio'):
                        module.winrt_fingerprint(environment)


if __name__ == '__main__':
    unittest.main()
