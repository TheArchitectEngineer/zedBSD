# zedBSD x64 UEFI loader

`BOOTX64.EFI` is a freestanding fallback-path application.  It searches the
FAT16/FAT32 filesystems on the physical disk from which firmware loaded it for
root `/zedbsd.cfg`; the loaded filesystem is considered first and remaining
handles retain firmware order.  No match is fatal.  Multiple matches produce
a warning and use the first match.  Filesystems on other disks are ignored.

The selected file uses the common bounded configuration grammar.  Its single
`kernel=` value is a safe relative path on that same FAT; remaining lines form
the kernel-parameter record.  The loader synthesizes a missing `boot0=` from
the selected FAT UUID and normalizes relative overlay/data/swap files.  It
does not consume UEFI LoadOptions and has no fixed-kernel or embedded-parameter
fallback.

Two tokens of the parameter lines are also the loader's (ws035-p096).
`logo=PATH` names a binary PPM (P6, maximum value 255, at most 4096 pixels a
side, at most 16 MiB) on the same FAT; the loader fills the screen with the
colour of its top-left pixel, draws it in the middle, and from then on writes
its progress text only to the debug port (a failure is still shown).
`kmsg=quiet` makes the loader draw no progress blocks and enter the kernel
through the transition's quiet entry, which draws none either, so the logo
stays until the kernel takes the screen.  Both stay in the parameter record;
the kernel ignores `logo=` and reads `kmsg=`.  The images put
`tools/build/make-boot-logo.py`'s logo on the ESP as `/logo.ppm`.

After validating and loading the configured restricted ELF64 kernel, the
loader captures a ZBL6 v2 memory map, exits boot services, installs private
four-level bootstrap page tables, and enters the kernel using the System V
AMD64 ABI.  Firmware disk services are not used after `ExitBootServices()`;
the kernel resolves the selected FAT and configured root through its native
storage drivers.
