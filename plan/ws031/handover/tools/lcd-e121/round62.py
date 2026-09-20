#!/usr/bin/env python3
"""WS031 E-121 round 62: the N0 record states what ran before it (from the pre-N0 side-effect audit): the PCI COMMAND
word the firmware left vs now (bus mastering / decode turned on at P2), and the fixed list of GT-side steps already
done (GT reset, fence clears, MSI enabled without a handler, WC aperture view).  usage: round62.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
BS = chr(92)
p = open(P + "probe.c").read()
old = "		parity_native_log(&n0);\n"
assert p.count(old) == 1
new = old + (
    '\t\tkern_logf("i915: parity N0 before-N0: PCI COMMAND firmware=0x%04x now=0x%04x (BME/MEM set at P2; restored on '
    'teardown) | done already: GT reset (GDRST full, display untouched on ADL-P), GT fault/error-register clears, fence '
    'clears (0..31), PCODE reads, MSI enabled (no handler), WC CPU view of the aperture | not done: any display register, '
    'any GGTT PTE, any D-state change' + BS + 'n",\n'
    '\t\t\t(unsigned)pci.saved_command, (unsigned)osdep_pci_read16(&pci, 0x04u));\n')
p = p.replace(old, new)
open(P + "probe.c", "w").write(p)
print("done")
