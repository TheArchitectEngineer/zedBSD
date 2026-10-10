# RPi4 boot0 read-only investigation / 2026-10-11

User asked whether RPi4 lacks amd64's `/boot/boot0` and whether `boot0:vmunix` is necessary. This is investigation only; no implementation Queue, source change, remote mount, swap activation, deployment or reboot.

## Physical read-back

Authorized SSH to RPi4 succeeded. `mount` shows `/dev/mmcblk0p2 on / type ufs (rw)` and runtime tmpfs/devfs mounts, with no FAT bind. `/boot` exists; `/boot/boot0` does not. `/dev/mmcblk0p1` and `/dev/mmcblk0p2` exist. Runtime boot parameters contain firmware-added Linux words followed by `login=graphical`, with no rootpart/overlay/swap parameters. Kernel reports `no root named by the parameters; using legacy autoroot`. `/etc/fstab` contains comments only, including a commented swap example. `sysctl -a` reports firmware/configuration provenance unavailable, login graphical. These provenance fields describe loader identity handoff, not proof that UFS is absent.

`swapctl` is not installed; this sysctl implementation neither supports subtree queries nor exposes a swap-total counter. No runtime swap counter was obtained. Source and the default fstab establish that this boot path does not automatically activate the FAT swapfile; no claim about manually added runtime swap sources.

## Source comparison

- `platform/arm64/vmunix.mk` generates only login= in cmdline.txt. Firmware loads `vmunix` from the FAT partition using config.txt and forwards cmdline.txt through FDT /chosen/bootargs.
- `src/kern/vfs.c` legacy autoroot finds the marked sibling UFS partition, then skips boot-source context mounting and boot swap preparation. It initializes an empty runtime swap manager. The special ARM legacy boot FAT publication only applies when the old image-overlay setup retained its FAT mount; native UFS selection does not retain it.
- Normal explicit-root boot invokes `kern_boot_source_context_mount`; slot0 defaults to the loader-origin FAT even without explicit boot0=. `vfs_publish_boot_filesystems` binds slots referenced by overlay/swap files. A kernel filename is not among those runtime references.
- amd64 overlay configuration names overlay-root/data and swap files, normalized to boot0 references by its loader. amd64 native UEFI configuration instead uses rootpart=PARTLABEL=zedBSD-root and swap0=PARTLABEL=zedBSD-swap; a boot slot used only by a direct selector remains private unless adopted through fstab. `/boot/boot0` therefore also depends on layout and runtime references on amd64.

Conclusion: RPi4's missing `/boot/boot0` is a confirmed gap in the current explicit boot-parameter generation / legacy native-root path. `boot0:vmunix` is not needed to load the kernel and would not cause kernel VFS publication. A bounded follow-up could name the existing native UFS root and FAT swap source (for the generated fixed RPi4 layout, rootpart=/dev/mmcblk0p2 swap0=boot0:swapfile), then verify slot0 publication and swap preparation through the common path. That source/build change and physical acceptance have not been performed in this investigation. Shared/remote projection remains Q1 pending; Master unchanged.
