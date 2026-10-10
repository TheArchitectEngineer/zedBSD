#!/usr/bin/env python3
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""Exercise extracted production IRQ/event functions with bounded register models."""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[3]
output = root / "build/ws048-irq-host"
output.mkdir(parents=True, exist_ok=True)
source = (root / "src/drivers/pci/pci-xhci.c").read_text()
functions = []
for name in ("xhci_event_dequeue_publish_locked", "event_take", "xhci_interrupter_write_locked", "xhci_irq_fail", "xhci_irq"):
    match = re.search(r"static (?:void|int)\n" + name + r"\(", source)
    if match is None:
        raise RuntimeError(name)
    end = source.index("\n}\n", match.start()) + 3
    functions.append(source[match.start():end])
(output / "irq-under-test.inc").write_text("\n\n".join(functions) + "\n")
binary = output / "irq-host"
subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I" + str(output), str(root / "plan/ws048/tests/irq-host.c"), "-o", str(binary)], check=True)
subprocess.run([str(binary)], check=True, timeout=5)
