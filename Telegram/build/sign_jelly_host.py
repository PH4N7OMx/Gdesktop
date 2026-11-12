import argparse
import pathlib
import plistlib
import subprocess


def sign_bundle(bundle, identity, app_entitlements=None, timestamp=False, runtime=False):
    bundle = pathlib.Path(bundle)
    helper = bundle / 'Contents/Helpers/jelly-plugin-host'
    if not helper.is_file() or helper.is_symlink():
        raise RuntimeError('JellyPlugins host is missing from the bundle: ' + str(helper))
    root = pathlib.Path(__file__).resolve().parents[2]
    entitlement = root / 'jellyplugins/v2/host/host.entitlements'
    base = ['codesign', '--force', '--sign', identity]
    if timestamp:
        base.append('--timestamp')
    if runtime:
        base.extend(['--options', 'runtime'])
    app = ['--entitlements', str(app_entitlements)] if app_entitlements else []
    subprocess.run([*base, '--deep', *app, str(bundle)], check=True)
    subprocess.run([*base, '--entitlements', str(entitlement), str(helper)], check=True)
    subprocess.run([*base, *app, str(bundle)], check=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(bundle)], check=True)
    data = subprocess.check_output(['codesign', '-d', '--entitlements', ':-', str(helper)], stderr=subprocess.PIPE)
    if plistlib.loads(data) != {'com.apple.security.app-sandbox': True}:
        raise RuntimeError('The plugin host inherited unexpected entitlements.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--bundle', required=True, type=pathlib.Path)
    parser.add_argument('--identity', required=True)
    parser.add_argument('--app-entitlements', type=pathlib.Path)
    parser.add_argument('--timestamp', action='store_true')
    parser.add_argument('--runtime', action='store_true')
    args = parser.parse_args()
    sign_bundle(args.bundle, args.identity, args.app_entitlements, args.timestamp, args.runtime)
