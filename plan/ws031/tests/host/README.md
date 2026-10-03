# Host-side scripts (reference copies)

These live outside the repository; the copies here are for review and handover (2026-09-23).
The live versions are the ones used by the tests.

| File | Live location | Purpose |
| --- | --- | --- |
| `run-parity-vk.sh` | Latitude 5330 (`chaos`, 10.0.10.25): `~/bigbang/run-parity-vk.sh` | QEMU + i915 VFIO passthrough launcher used by `plan/ws031/tests/vkloop-hw.sh` (`QMP=1` adds QMP and USB input, `VK_STOP_RE` sets the end pattern) |
| `igpu-mode.sh` | Latitude 5330: `~/bigbang/igpu-mode.sh` | switches the iGPU between vfio-pci (passthrough tests) and the host i915 driver (Venus tests) |
| `capture_lcd.ps1` | Windows workstation: `C:\Work\qemu-work\tools\capture_lcd.ps1` | takes one photo of the 5330 LCD with the workstation camera |

If a live script changes, update the copy here too. See `plan/ws031/ws.md`, section 引き継ぎ（2026-09-23）.
