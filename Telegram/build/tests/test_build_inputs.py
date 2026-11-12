import contextlib
import importlib.util
import io
import unittest
from pathlib import Path
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "validate_build_inputs", Path(__file__).resolve().parents[1] / "validate_build_inputs.py",
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class BuildInputsTests(unittest.TestCase):
    def test_missing_embedded_contract_fails_before_configuration(self):
        original = Path.is_file

        def is_file(path):
            return False if path.name == "contract.generated.h" else original(path)

        output = io.StringIO()
        with patch.object(Path, "is_file", is_file), contextlib.redirect_stderr(output):
            self.assertEqual(module.main(), 1)
        self.assertIn("Missing JellyPlugins source: client/contract.generated.h", output.getvalue())

    def test_uninitialized_quickjs_fails_before_configuration(self):
        original = Path.is_file

        def is_file(path):
            if path.name == "quickjs.c" and "widget-sandbox" in path.parts:
                return False
            return original(path)

        output = io.StringIO()
        with patch.object(Path, "is_file", is_file), contextlib.redirect_stderr(output):
            self.assertEqual(module.main(), 1)
        self.assertIn("Missing QuickJS source: quickjs/quickjs.c", output.getvalue())
        self.assertIn("initialize widget-sandbox submodules recursively", output.getvalue())


if __name__ == "__main__":
    unittest.main()
