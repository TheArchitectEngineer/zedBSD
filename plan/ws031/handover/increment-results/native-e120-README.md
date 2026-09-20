# zedBSD native test image (E-120, N0 native precheck)

- File: `zedbsd-native-e120.img` (253,755,392 bytes)
- SHA-256: `fc83e1af84cf2dcf21884d61a0f9829260231661ee709dd34bd6b0791d0beef2`
- Build: `build/native-e120` on agent-1, `CONFIG_DRIVER_PCI_I915_PARITY=y -DPARITY_LCDD_TEST=1`, E-120 final source plus the end-of-run N0 summary (round 59). Verified in the VM with the real GPU: N0 PROCEED, then LCD-D PASS, GPU-free tests 536/0. The native STOP path (early teardown) was also tested in the VM with a separate test-only build.
- Target: Dell Latitude 5330 (this machine is also the KVM host `solaris10-man`: the Linux host is down while zedBSD runs; the internal disk is not mounted by zedBSD, and fstab lists nothing extra).

## Writing the USB stick
Write the image to the whole device, not to a partition. **Everything on the stick is erased.**
- Windows: Rufus (DD image mode) or balenaEtcher.
- Linux: `dd if=zedbsd-native-e120.img of=/dev/sdX bs=4M conv=fsync` (check `sdX` with lsblk).

## Booting
1. Secure Boot is disabled on this machine (checked with `mokutil --sb-state`).
2. Press F12 at power-on → select the USB stick (UEFI).
3. The boot loader sets the firmware (GOP) mode to 640×480, so the LCD shows large text.

## What is expected
- N0 runs before any display write and prints `i915: parity N0 ...` lines. On this machine the firmware leaves the LCD lit (pipe A), so the expected decision is **`STOP before any display write -- a pipe is active ...`**. The driver then tears down cleanly (`probe=BLOCKED blocked_at=native-precheck`). The LCD keeps showing the firmware / console picture; zedBSD does **not** draw its own images in this run.
- The N0 block is printed again as the very last driver output (`N0 ---- summary (repeated at the end of the run) ----`), just before `parity runner thread end`.
- If N0 says PROCEED (not expected), the driver continues; on this machine it would then stop at `intel_opregion_register` (not ported yet).

## Please capture
1. **Photos** of the screen when the `N0 ---- summary` block and `runner-result` are visible. Several photos are fine if the text scrolls.
2. If you reach the login prompt: log in as `root` (no password), then `dmesg | grep -E "N0|runner-result"`, and photograph that too. The full log is also kept in `/var/log/messages` on the stick (syslogd). You can bring the stick back so it can be read later.
3. If the machine hangs or the screen goes black: a photo of the last screen and a note of the time.

## Lines of interest (for the photos)
- `N0 opregion:` — ASLS, OpRegion version, **VBT via RVDA**, `matches explicit pin=1` expected.
- `N0 vt-d (GPU unit):` — `view=native`, `VER`, `GSTS` (TES bit 31), `PMEN`. If translation or a PMR is on, the decision is STOP for that reason.
- `N0 firmware framebuffer:` — base 0x4000000000 expected (in the aperture), GGTT pages 0..
- `N0 pipe A:` — TRANSCONF / PLANE_CTL / PLANE_SURF / STRIDE / SIZE (the firmware display).
- `N0 decision:` and `runner-result:`.

To return to the normal setup, remove the stick and reboot (Linux KVM host).
