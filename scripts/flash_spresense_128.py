"""Bounded 128-byte XMODEM fallback using Sony's installed protocol implementation.

Only use after identifying the port. Bootloader packages additionally require
the official installer's completed license/version marker. No erase commands.
"""
import argparse
import collections
import collections.abc
import hashlib
import json
from pathlib import Path
import sys
import time

import serial

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / '.tools/arduino-data/packages/SPRESENSE/tools/spresense-sdk/3.4.7/spresense/firmware'
WRITER = ROOT / '.tools/arduino-data/packages/SPRESENSE/tools/spresense-tools/3.4.7/flash_writer/scripts'


def read_until(port, marker, seconds=15):
    data = bytearray()
    deadline = time.monotonic() + seconds
    port.timeout = .1
    while marker not in data and time.monotonic() < deadline:
        data.extend(port.read(1))
    if marker not in data:
        raise RuntimeError(f'Missing {marker!r}: {bytes(data)!r}')
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('packages', nargs='+', type=Path)
    args = parser.parse_args()
    packages = [p.resolve(strict=True) for p in args.packages]
    for package in packages:
        if package.suffix not in ('.spk', '.espk'):
            parser.error('Expected a Sony SPK/ESPK package')
        if package.suffix == '.espk':
            if package.parent != SDK.resolve():
                parser.error('Boot packages must come from the installed official SDK')
            expected = json.loads((SDK / 'version.json').read_text())['LoaderVersion']
            marker = SDK / 'stored_version.json'
            if not marker.exists() or json.loads(marker.read_text())['LoaderVersion'] != expected:
                parser.error('Complete the official license dialog before transferring boot packages')
    sys.path.insert(0, str(WRITER))
    collections.Callable = collections.abc.Callable  # Sony tool compatibility with Python >=3.10.
    import xmodem

    with serial.Serial(args.port, 115200, timeout=.1, write_timeout=3) as port:
        # Same reset and auto-boot cancellation as Sony flash_writer; bounded wait.
        port.dtr = False
        port.dtr = True
        port.dtr = False
        boot = bytearray()
        deadline = time.monotonic() + 10
        while b'updater' not in boot and time.monotonic() < deadline:
            boot.extend(port.read(256))
            port.write(b'r')
        if b'updater' not in boot:
            raise RuntimeError(f'Updater not detected: {bytes(boot)!r}')
        port.write(b'\n')
        time.sleep(.3)
        port.reset_input_buffer()

        def getc(size, timeout=1):
            port.timeout = min(timeout, 3)
            return port.read(size)

        def putc(data, timeout=1):
            return port.write(data)

        for package in packages:
            print(f'INSTALL {package.name} bytes={package.stat().st_size} '
                  f'sha256={hashlib.sha256(package.read_bytes()).hexdigest()}', flush=True)
            port.write(b'install\n')
            print(read_until(port, b'to cancel.\r\n').decode(errors='replace'), flush=True)
            last_error = -1

            def progress(total, good, bad):
                nonlocal last_error
                if good % 200 == 0 or bad != last_error:
                    print(f'blocks={good} retries={bad}', flush=True)
                    last_error = bad

            with package.open('rb') as source:
                ok = xmodem.XMODEM(getc, putc, mode='xmodem').send(
                    source, retry=20, timeout=3, callback=progress)
            if not ok:
                raise RuntimeError(f'Transfer failed: {package.name}')
            response = read_until(port, b'updater', seconds=60)
            print(response.decode(errors='replace'), flush=True)
            if b'Package validation is OK.' not in response or b'Saving package' not in response:
                raise RuntimeError(f'Board did not confirm package validation: {package.name}')
            time.sleep(.1)
            port.reset_input_buffer()
        port.write(b'reboot\n')
        print('Validated all requested packages; reboot requested.', flush=True)


if __name__ == '__main__':
    main()
