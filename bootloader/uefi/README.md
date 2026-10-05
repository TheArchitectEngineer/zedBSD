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

Two keys change one boot without editing the file (ws174).  The loader reads
the firmware's extended console input (`EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL`)
when it starts, asking for exposed modifier keys, and again right after it
parses the configuration: each time it drains the key queue and takes the
modifier keys held at that moment from the key state the input reports with
an empty queue.  It never waits for a key.  Ctrl held removes every
`kmsg=` and `logo=` token from the record and appends `kmsg=console`, and the
loader wishes for a 640x480 GOP mode when `video=` names none, so the early
console's 80 columns fill the screen.  Shift held removes every `login=` token
and appends `login=console`.  Tokens are matched by whole name, removed before
appending (the kernel refuses a repeated known name), and an appended token
that would pass 3071 bytes is left out.  The rewrite
(`bootloader/common/boot-override.c`) runs before the logo and the video mode
are chosen.  The loader then prints `Boot: kernel messages (Ctrl)`,
`Boot: console login (Shift)` and `A64 PARAMS OVERRIDE <record>`; on screen
they are best effort.  The way to press the keys is to hold the modifier and
tap Space repeatedly after power-on: a modifier with Space is reported by
every firmware with the extended input, a modifier alone only by firmware
that reports the held state or honours exposed modifiers.  See
`docs/reference/kernel-boot-parameters.md` Section 7c.

After validating and loading the configured restricted ELF64 kernel, the
loader captures a ZBL6 v2 memory map, exits boot services, installs private
four-level bootstrap page tables, and enters the kernel using the System V
AMD64 ABI.  Firmware disk services are not used after `ExitBootServices()`;
the kernel resolves the selected FAT and configured root through its native
storage drivers.
