#!/usr/bin/env python3
"""Build the default Pumper Rust firmware; forwards options to the pinned builder."""
from pathlib import Path
import subprocess
import sys

firmware = Path(__file__).resolve().parent
command = [sys.executable, str(firmware / 'tools/build_firmware.py'),
           '--output', str(firmware / 'artifacts/pumper-rust-5.0.3.uf2'), *sys.argv[1:]]
raise SystemExit(subprocess.call(command))
