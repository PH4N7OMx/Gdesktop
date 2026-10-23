from pathlib import Path


def patch_source(source):
    for old, new in (
        ('Debug/zlibstaticd.lib', 'Debug/libzsd.lib'),
        ('Release/zlibstatic.lib', 'Release/libzs.lib'),
    ):
        if old not in source and new not in source:
            raise RuntimeError(f'Unexpected Qt dependency path: {old}')
        source = source.replace(old, new)
    anchor = '            set(WebP_INCLUDE_DIR ${libs_loc}/libwebp/src)'
    if '/release-static/x86/' in source:
        if source.count(anchor) != 1:
            raise RuntimeError('Unexpected Qt WebP configuration')
        source = source.replace(anchor, '''            if (build_winarm)
                set(webp_arch ARM64)
            elseif (build_win64)
                set(webp_arch x64)
            else()
                set(webp_arch x86)
            endif()
''' + anchor)
        source = source.replace('/release-static/x86/', '/release-static/${webp_arch}/')
    if source.count('/release-static/${webp_arch}/') != 3:
        raise RuntimeError('Unexpected Qt WebP library paths')
    return source


def main():
    path = Path(__file__).resolve().parents[2] / 'cmake/external/qt/package.cmake'
    source = path.read_text(encoding='utf-8')
    patched = patch_source(source)
    if patched != source:
        path.write_bytes(patched.replace('\n', '\r\n').encode('utf-8'))


if __name__ == '__main__':
    main()
