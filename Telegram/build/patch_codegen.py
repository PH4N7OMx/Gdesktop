import re
from pathlib import Path


def patch_source(source):
    pattern = (
        r"if \(data\[i\] != 'l'\s*"
        r"\|\| data\[i \+ 1\] != 'n'\s*"
        r"\|\| data\[i \+ 2\] != 'g'\s*"
        r"\|\| data\[i \+ 3\] != '_'"
    )
    replacement = (
        "const auto isLng = (data[i] == 'l' && data[i + 1] == 'n'"
        " && data[i + 2] == 'g' && data[i + 3] == '_');\n\t\t"
        "const auto isJel = (data[i] == 'j' && data[i + 1] == 'e'"
        " && data[i + 2] == 'l' && data[i + 3] == '_');\n\t\t"
        "if ((!isLng && !isJel)"
    )
    patched, count = re.subn(pattern, lambda _: replacement, source)
    fork_scanner = (
        "const auto isKeyPrefix =" in source
        and "|| (data[i] == 'j'" in source
        and "if (!isKeyPrefix" in source
    )
    if count != 1 and not (count == 0 and (replacement in source or fork_scanner)):
        raise RuntimeError("Unexpected codegen scanner; jel_ patch was not applied.")
    patched, versions = re.subn(
        r"constexpr auto kCacheVersion = quint32\([123]\);",
        "constexpr auto kCacheVersion = quint32(3);", patched,
    )
    if versions != 1:
        raise RuntimeError("Unexpected codegen cache version.")
    return patched


def main():
    path = Path(__file__).resolve().parents[1] / "codegen/codegen/lang/subsets.cpp"
    source = path.read_text(encoding="utf-8")
    patched = patch_source(source)
    if patched != source:
        path.write_bytes(patched.replace("\n", "\r\n").encode("utf-8"))
    print("Codegen scanner supports lng_ and jel_; cache version 3.")


if __name__ == "__main__":
    main()
