import argparse
import os
import pathlib
import struct


def architecture(path):
    with pathlib.Path(path).open('rb') as file:
        data = file.read(4096)
        if data[:2] == b'MZ':
            offset = struct.unpack_from('<I', data, 0x3c)[0]
            file.seek(offset)
            header = file.read(6)
            if header[:4] != b'PE\0\0':
                raise ValueError('Invalid PE runtime: ' + str(path))
            return {'pe:' + hex(struct.unpack_from('<H', header, 4)[0])}
        if data[:4] == b'\x7fELF':
            order = '<' if data[5] == 1 else '>'
            return {'elf:' + hex(struct.unpack_from(order + 'H', data, 18)[0])}
        magic = data[:4]
        if magic in (b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xfe\xed\xfa\xce'):
            return {'macho:' + hex(struct.unpack_from('<I' if magic[0] in (0xcf, 0xce) else '>I', data, 4)[0])}
        if magic in (b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca', b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca'):
            order = '>' if magic[0] == 0xca else '<'
            count = struct.unpack_from(order + 'I', data, 4)[0]
            if not 0 < count <= 16:
                raise ValueError('Invalid universal runtime: ' + str(path))
            size = 32 if magic in (b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca') else 20
            return {'macho:' + hex(struct.unpack_from(order + 'I', data, 8 + i * size)[0]) for i in range(count)}
    raise ValueError('Not a native executable: ' + str(path))


def verify(folder, bundle=False):
    folder = pathlib.Path(folder)
    if bundle:
        import plistlib
        info = plistlib.loads((folder / 'Contents/Info.plist').read_bytes())
        main = folder / 'Contents/MacOS' / info['CFBundleExecutable']
        helper = folder / 'Contents/Helpers/jelly-plugin-host'
    else:
        main = next((folder / name for name in ('GummyGram.exe', 'Telegram.exe', 'GummyGram', 'Telegram') if (folder / name).is_file()), None)
        if main is None:
            raise ValueError('Application executable is missing: ' + str(folder))
        helper = folder / 'jelly-runtime' / ('jelly-plugin-host.exe' if main.suffix == '.exe' else 'jelly-plugin-host')
    if not helper.is_file() or helper.is_symlink():
        raise ValueError('Plugin host is missing or is a symbolic link: ' + str(helper))
    if os.name != 'nt' and not os.access(helper, os.X_OK):
        raise ValueError('Plugin host lost its executable permissions: ' + str(helper))
    if architecture(main) != architecture(helper):
        raise ValueError('Client and plugin host architectures differ: ' + str(helper))
    print('JellyPlugins runtime layout and architectures OK: ' + str(helper), flush=True)
    return helper


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('folder', type=pathlib.Path)
    parser.add_argument('--bundle', action='store_true')
    args = parser.parse_args()
    verify(args.folder, args.bundle)
