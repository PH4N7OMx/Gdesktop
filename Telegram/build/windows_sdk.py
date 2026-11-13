import hashlib
import os
from pathlib import Path


def winrt_fingerprint(environment=None):
    environment = os.environ if environment is None else environment
    for directory in environment.get('INCLUDE', '').split(';'):
        directory = directory.strip().strip('"')
        if not directory:
            continue
        header = Path(directory) / 'winrt/base.h'
        if header.is_file():
            return hashlib.sha256(header.read_bytes()).hexdigest()
    raise RuntimeError('C++/WinRT headers are missing from INCLUDE; initialize Visual Studio first.')


if __name__ == '__main__':
    print(winrt_fingerprint())
