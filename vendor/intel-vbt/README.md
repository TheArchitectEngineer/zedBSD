# vendor/intel-vbt

`dell-latitude-5330-1028-0b02.inc` is the Video BIOS Table (VBT) of one Dell
Latitude 5330 (GPU 8086:46a8 rev 0c, PCI subsystem 1028:0b02, panel AUO
B133HAN), captured from the machine's ACPI OpRegion through a Linux 6.8 i915
guest (debugfs `i915_vbt`) on 2026-09-18.  The file is the array body only:
the 8704 bytes of the table (the VBT header's `vbt_size` is 8701), written as
C initializer lines and included inside an initializer.

- size: 8704 bytes
- sha256: `3bff4a0920d55c9aee0ea3c678904f982f8671c0e7b5bc97a335e429b29624cd`
- raw copy: `plan/ws031/display-ref/i915_vbt.bin`

It is Dell/Intel platform data.  No redistribution licence has been audited
for it (see `plan/ws031/license-inventory.md` and
`plan/ws031/provenance-ledger.md`).

It is used only by the i915 test build (`I915_TEST_VBT=y`, which defines
`I915_TEST_VBT`): the QEMU passthrough test runs the driver in a guest whose
firmware (OVMF) presents ASLS=0, so the guest sees no OpRegion and therefore no
VBT.  `src/drivers/gpu/i915/display/vbt.c` then uses this table when the PCI
subsystem id is 1028:0b02 and the bytes match the SHA-256 above.  A production
kernel does not contain it; on real hardware the driver reads the VBT from the
OpRegion.

XXX: delete this directory, and the `I915_TEST_VBT` path in `display/vbt.c`,
once the GPU tests run on bare metal.
