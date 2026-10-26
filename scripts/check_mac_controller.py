#!/usr/bin/env python3
"""Compare SDL2 HID input with Apple GameController without running a game. Requires macOS, Xcode tools and `brew install sdl2`."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=int, default=30, help='capture duration, 1..300 seconds (default: 30)')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('this check requires macOS')
    if not 1 <= args.seconds <= 300:
        parser.error('--seconds must be in 1..300')
    host = Path(__file__).resolve().parents[1] / 'dm' / 'host'
    with tempfile.TemporaryDirectory(prefix='nxnative-pad-') as tmp:
        binary = str(Path(tmp) / 'probe')
        subprocess.run(['xcrun', '--sdk', 'macosx', 'clang', '-fobjc-arc', '-O1', '-Wall', '-Wextra', '-Werror',
                        str(host / 'tests' / 'probe_pad_mac.m'), str(host / 'pad_sdl_mac.c'),
                        '-framework', 'Foundation', '-framework', 'GameController', '-o', binary], check=True)
        # SDL touches device state as any controller driver does; it does not write calibration or firmware.
        return subprocess.run([binary, str(args.seconds)], env=os.environ).returncode


if __name__ == '__main__':
    sys.exit(main())
