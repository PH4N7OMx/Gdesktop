import importlib.util
import pathlib
import plistlib
import struct
import tempfile
import unittest
from unittest.mock import patch


ROOT = pathlib.Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / (name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


SIGN = module('sign_jelly_host')
LAYOUT = module('verify_jelly_runtime')


class PackagingTests(unittest.TestCase):
    def test_helper_entitlements_survive_app_signing(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = pathlib.Path(temporary) / 'Client.app'
            helper = bundle / 'Contents/Helpers/jelly-plugin-host'
            helper.parent.mkdir(parents=True)
            helper.write_bytes(b'fixture')
            with patch.object(SIGN.subprocess, 'run') as run, patch.object(SIGN.subprocess, 'check_output',
                return_value=plistlib.dumps({'com.apple.security.app-sandbox': True})):
                SIGN.sign_bundle(bundle, 'Test identity', 'client.entitlements', True, True)
            calls = [call.args[0] for call in run.call_args_list]
            self.assertIn('--deep', calls[0])
            self.assertEqual(calls[1][-1], str(helper))
            self.assertTrue(calls[1][calls[1].index('--entitlements') + 1].endswith('host.entitlements'))
            self.assertNotIn('--deep', calls[2])
            self.assertEqual(calls[2][-1], str(bundle))
            self.assertIn('--verify', calls[3])

    def test_inherited_broad_helper_entitlements_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = pathlib.Path(temporary) / 'Client.app'
            helper = bundle / 'Contents/Helpers/jelly-plugin-host'
            helper.parent.mkdir(parents=True)
            helper.touch()
            with patch.object(SIGN.subprocess, 'run'), patch.object(SIGN.subprocess, 'check_output',
                return_value=plistlib.dumps({'com.apple.security.app-sandbox': True, 'com.apple.security.network.client': True})):
                with self.assertRaises(RuntimeError):
                    SIGN.sign_bundle(bundle, '-')

    def test_portable_requires_host_with_matching_architecture(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = pathlib.Path(temporary)
            main = folder / 'GummyGram'
            helper = folder / 'jelly-runtime/jelly-plugin-host'
            data = bytearray(64)
            data[:6] = b'\x7fELF\x02\x01'
            struct.pack_into('<H', data, 18, 62)
            main.write_bytes(data)
            with self.assertRaises(ValueError):
                LAYOUT.verify(folder)
            helper.parent.mkdir()
            helper.write_bytes(data)
            helper.chmod(0o755)
            self.assertEqual(LAYOUT.verify(folder), helper)
            struct.pack_into('<H', data, 18, 183)
            helper.write_bytes(data)
            with self.assertRaises(ValueError):
                LAYOUT.verify(folder)

    def test_windows_pe_machine_detection(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = pathlib.Path(temporary) / 'host.exe'
            data = bytearray(256)
            data[:2] = b'MZ'
            struct.pack_into('<I', data, 0x3c, 128)
            data[128:132] = b'PE\0\0'
            struct.pack_into('<H', data, 132, 0x8664)
            binary.write_bytes(data)
            self.assertEqual(LAYOUT.architecture(binary), {'pe:0x8664'})

    def test_universal_macos_machine_detection(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = pathlib.Path(temporary) / 'host'
            data = bytearray(64)
            data[:4] = b'\xca\xfe\xba\xbe'
            struct.pack_into('>I', data, 4, 2)
            struct.pack_into('>I', data, 8, 0x1000007)
            struct.pack_into('>I', data, 28, 0x100000c)
            binary.write_bytes(data)
            self.assertEqual(LAYOUT.architecture(binary), {'macho:0x1000007', 'macho:0x100000c'})


if __name__ == '__main__':
    unittest.main()
