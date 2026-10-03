#!/usr/bin/env python3
"""Build and run one model viewer attempt on the Venus host (WS031 p013).

The shared runner (plan/ws014/tests/run-venus-remote.py) builds the image, switches the host's
iGPU to its i915 driver for the attempt, runs plan/ws031/tests/mview-qemu.py there, switches the
iGPU back to vfio-pci and fetches the evidence.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import importlib.util
from pathlib import Path
import sys

_runner = Path(__file__).resolve().parents[2] / 'ws014/tests/run-venus-remote.py'
_spec = importlib.util.spec_from_file_location('venus_remote', _runner)
common = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(common)

if __name__ == '__main__':
    sys.exit(common.main(profile='mview'))
