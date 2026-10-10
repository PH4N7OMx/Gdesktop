import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def main():
    telegram = Path(__file__).resolve().parents[1]
    errors = []
    source_count = 0
    resource_count = 0
    cmake = (telegram / "CMakeLists.txt").read_text(encoding="utf-8")
    for block in re.findall(
        r"nice_target_sources\([^\s]+\s+\$\{src_loc\}(.*?)\n\s*\)",
        cmake,
        re.S,
    ):
        for line in block.splitlines():
            relative = line.strip()
            if re.fullmatch(r"[\w./ -]+\.(?:cpp|h|mm|m|c)", relative):
                source_count += 1
                if not (telegram / "SourceFiles" / relative).is_file():
                    errors.append(f"Missing CMake source: {relative}")

    for qrc in (telegram / "Resources" / "qrc").rglob("*.qrc"):
        try:
            tree = ET.parse(qrc)
        except ET.ParseError as error:
            errors.append(f"Invalid resource manifest {qrc}: {error}")
            continue
        for entry in tree.iter("file"):
            resource_count += 1
            relative = (entry.text or "").strip()
            if not relative or not (qrc.parent / relative).is_file():
                errors.append(f"Missing resource in {qrc.name}: {relative}")

    version = dict(
        line.split(None, 1)
        for line in (telegram / "build" / "version").read_text().splitlines()
        if line.strip()
    )
    number = version["AppVersion"]
    name = version["AppVersionStr"]
    header = (telegram / "SourceFiles" / "core" / "version.h").read_text()
    if not re.search(rf"AppVersion = {re.escape(number)};", header):
        errors.append("AppVersion differs between core/version.h and build/version")
    if f'AppVersionStr = "{name}";' not in header:
        errors.append("AppVersionStr differs between core/version.h and build/version")
    numeric = name.replace(".", ",") + ",0"
    for filename in ("Telegram.rc", "Updater.rc"):
        resource = (telegram / "Resources" / "winrc" / filename).read_text()
        for field in ("FILEVERSION", "PRODUCTVERSION"):
            if not re.search(rf"{field}\s+{re.escape(numeric)}\b", resource):
                errors.append(f"{filename}: {field} differs from {name}")
        for field in ("FileVersion", "ProductVersion"):
            if f'VALUE "{field}", "{name}.0"' not in resource:
                errors.append(f"{filename}: {field} differs from {name}")
    manifest = ET.parse(telegram / "Resources" / "uwp" / "AppX" / "AppxManifest.xml")
    identity = next(e for e in manifest.iter() if e.tag.endswith("}Identity"))
    if identity.attrib["Version"] != name + ".0":
        errors.append("AppxManifest.xml version differs from build/version")

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"Build inputs OK: {source_count} sources, {resource_count} resources, version {name}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
