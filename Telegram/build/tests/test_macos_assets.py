import base64
import hashlib
import json
import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


class MacosAssetsTests(unittest.TestCase):
    def test_window_title_icons_use_explicit_cross_module_references(self):
        widgets = (ROOT / "Telegram/lib_ui/ui/widgets/widgets.style").read_text(
            encoding="utf-8")
        schema = re.search(r"WindowTitle \{(.*?)\n\}", widgets, re.S).group(1)
        fields = re.findall(r"^\s*(\w+): icon;", schema, re.M)
        self.assertTrue(fields)
        native = (ROOT / "Telegram/SourceFiles/jel/ui/jel_styles.style").read_text(
            encoding="utf-8")
        title = re.search(
            r"macosWindowTitle: WindowTitle\(defaultWindowTitle\) \{(.*?)\n\}",
            native, re.S).group(1)
        for field in fields:
            with self.subTest(field=field):
                self.assertRegex(title, rf"\b{field}: defaultWindowTitle\.{field};")

    def test_compose_icons_override_foreign_unnamed_icons(self):
        source = (ROOT / "Telegram/SourceFiles/chat_helpers/chat_helpers.style").read_text(
            encoding="utf-8")
        parent = source.split("defaultComposeIcons: ComposeIcons {", 1)[1].split(
            "\n}", 1)[0]
        fields = re.findall(r"^\t(\w+): icon\s*\{", parent, re.M)
        self.assertTrue(fields)
        native = (ROOT / "Telegram/SourceFiles/jel/ui/jel_styles.style").read_text(
            encoding="utf-8")
        child = native.split("macosComposeIcons: ComposeIcons(defaultComposeIcons) {", 1)[1].split(
            "\n}", 1)[0]
        for field in fields:
            with self.subTest(field=field):
                self.assertRegex(child, rf"(?m)^\t{field}:")

    def test_original_assets_match_pinned_source_hashes(self):
        manifest = json.loads(
            (ROOT / "docs/assets/macos-interface/sources.json").read_text(
                encoding="utf-8"))
        self.assertRegex(manifest["revision"], r"^[0-9a-f]{40}$")
        self.assertTrue(
            (ROOT / "docs/assets/macos-interface/LICENSE").is_file())
        names = set()
        for asset in manifest["assets"]:
            with self.subTest(asset=asset["name"]):
                self.assertNotIn(asset["name"], names)
                names.add(asset["name"])
                if asset.get("usage") in ("full-color-settings", "full-color-call"):
                    name = asset["name"].removeprefix("settings-")
                    path = ROOT / f"Telegram/Resources/art/jel/macos/{name}.png"
                    data = path.read_bytes()
                else:
                    path = ROOT / (
                        "Telegram/Resources/icons/jel/macos/"
                        + asset["name"] + ".svg")
                    svg = ET.parse(path).getroot()
                    self.assertEqual(svg.attrib.get("data-desktop-alpha-mask"), "true")
                    self.assertEqual(
                        [int(svg.attrib[key]) for key in ("width", "height")],
                        asset["canvas"])
                    images = svg.findall("{http://www.w3.org/2000/svg}image")
                    self.assertEqual(len(images), 1)
                    image = images[0]
                    x, y = (float(image.attrib.get(key, 0)) for key in ("x", "y"))
                    width, height = (float(image.attrib[key]) for key in ("width", "height"))
                    self.assertGreaterEqual(x, 0)
                    self.assertGreaterEqual(y, 0)
                    self.assertGreater(width, 0)
                    self.assertGreater(height, 0)
                    self.assertLessEqual(x + width, asset["canvas"][0])
                    self.assertLessEqual(y + height, asset["canvas"][1])
                    uri = image.attrib[
                        "{http://www.w3.org/1999/xlink}href"]
                    self.assertTrue(uri.startswith("data:image/png;base64,"))
                    data = base64.b64decode(uri.split(",", 1)[1], validate=True)
                self.assertTrue(data.startswith(b"\x89PNG\r\n\x1a\n"))
                self.assertEqual(
                    hashlib.sha256(data).hexdigest(), asset["sha256"])

    def test_player_animations_match_pinned_source_and_resource_aliases(self):
        manifest = json.loads(
            (ROOT / "docs/assets/macos-interface/sources.json").read_text(
                encoding="utf-8"))
        resources = ET.parse(ROOT / "Telegram/Resources/qrc/telegram/animations.qrc")
        aliases = {entry.attrib.get("alias") for entry in resources.iter("file")}
        for asset in manifest["animations"]:
            with self.subTest(animation=asset["name"]):
                name = asset["name"] + ".tgs"
                data = (ROOT / "Telegram/Resources/animations/jel/macos" / name).read_bytes()
                self.assertEqual(hashlib.sha256(data).hexdigest(), asset["sha256"])
                # Upstream's .tgs files contain plain Bodymovin JSON.
                animation = json.loads(data)
                self.assertEqual([animation["w"], animation["h"]], asset["canvas"])
                self.assertEqual([animation["ip"], animation["op"]], asset["frames"])
                self.assertEqual(animation["fr"], asset["fps"])
                self.assertGreater(animation["op"], animation["ip"])
                self.assertTrue(animation["layers"])
                self.assertIn("jel/macos/" + name, aliases)

    def test_all_style_icons_have_assets_and_colored_icons_are_bundled(self):
        style = (ROOT / "Telegram/SourceFiles/jel/ui/jel_styles.style").read_text(
            encoding="utf-8")
        masks = set(re.findall(r'"jel/macos/([a-z]+)(?:-[0-9]+x[0-9]+)?"', style))
        directory = ROOT / "Telegram/Resources/icons/jel/macos"
        self.assertEqual(masks, {path.stem for path in directory.glob("*.svg")})
        resources = ET.parse(
            ROOT / "Telegram/Resources/qrc/telegram/telegram.qrc")
        aliases = {entry.attrib.get("alias") for entry in resources.iter("file")}
        for path in (ROOT / "Telegram/Resources/art/jel/macos").glob("*.png"):
            self.assertIn(f"art/jel/macos/{path.name}", aliases)


if __name__ == "__main__":
    unittest.main()
