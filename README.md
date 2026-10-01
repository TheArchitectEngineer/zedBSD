Kei/zedBSD
==========

`zedBSD` is a modern, redesigned BSD-based kernel and base system that
aims to implement all `POSIX.1-2024` and `Single UNIX Specification
version 4 (SUSv4)` features with a sophisticated architecture.

`Kei` is a zedBSD-based operating system with the `Keiland` desktop
environment. `Keiland` consists of a Wayland compositor and its client
applications, designed for computers with touch displays.

Both `zedBSD` and `Kei` are designed and directed by a single human
developer and implemented with AI coding agents.

`Kei` runs on modern computers. The current targets are 64-bit x86 PCs
and the Raspberry Pi series.

## Design Architecture

```
+----------------------------------------------------------------+
| Official Packages (/usr)                                       |
+----------------------------------------------------------------+
| Wayland desktop (/bin/wayland, ...)                            |
+----------------------------------------------------------------+
| Base programs (/bin, /lib)                                     |
+----------------------------------------------------------------+
| /sbin/networkd                                                 |
+----------------------------------------------------------------+
| /sbin/init                                                     |
+----------------------------------------------------------------+
| Drivers (PCI, USB, GPU, disk, ethernet, wifi, filesystem, ...) |
+----------------------------------------------------------------+
| Kernel (platform-neutral)                                      |
+----------------------------------------------------------------+
| HAL (CPU + BSP)                                                |
+----------------------------------------------------------------+
```

The kernel is built on a HAL. It keeps the platform-neutral kernel
completely portable across substantially different machines.

## GPU Support

`zedBSD` has a modern GPU drivers that features a true native Vulkan
stack. It doesn't require Linux DRM/KMS or Mesa.  Currently, Intel
iGPU (Xe-LP) is supported, and NVIDIA/AMD support is planned.

## Retro Computing

For the purpose of a demonstration of the strong compatibility, we
partially support the following:

- NEC PC-9800 i386
- IBM PC/AT i386
- sun4u/sparcv9
- Sharp X68000/m68k

## Building

The complete prerequisite, configuration, image, and QEMU procedure is
in the [build-from-source guide](docs/howto/build-from-source.md).

On amd64 the default disk image is the `native` layout: a GPT disk whose ESP
holds the UEFI loader and the kernel, a read-write UFS root partition, and a
swap partition.  It boots through UEFI (`make run` starts QEMU with OVMF and an
NVMe disk).  The `hybrid` (UEFI and BIOS), `uefi` and `bios` layouts can be
chosen in `make menuconfig`.

The build commands are:

```sh
make                   # equals to disk-image
make menuconfig        # run menuconfig to make config.mk
make disk-image        # build a disk image
make world             # build vmunix and rootfs
make rootfs            # build rootfs
make vmunix            # build vmunix kernel
make run               # build a disk image and start QEMU
make toolchain-cache   # install pinned rev-0 LLVM cache (x86_64 Linux)
make toolchain         # build a toolchain
make help              # show a short command summary
```

## Layout

| Directory            | Description                                            |
|----------------------|--------------------------------------------------------|
| `include/`           | Public HAL, kernel, and user ABI interfaces            |
| `src/hal/`           | Architecture HALs and board support                    |
| `src/kern/`          | Platform-neutral kernel                                |
| `src/drivers/`       | Device and bus driver implementations                  |
| `src/libc/`          | zedBSD `libc`                                          |
| `userland/`          | Userland programs                                      |
| `userland/base/`     | Base programs and libraries (`/bin`, `/lib`)           |
| `userland/comp/`     | Compilers                                              |
| `userland/desktop/`  | Keiland programs                                       |
| `userland/firmware/` | Optional per-device firmware packages                  |
| `userland/packages/` | Third-party packages (`/usr`)                          |
| `platform/`          | Target Makefiles and tools                             |
| `vendor/`            | External programs                                      |
| `tools/`             | Development scripts                                    |
| `tests/`             | Tests                                                  |

## What Made This Project Possible

An operating system cannot be built simply by issuing prompts to an AI
agent. I brought this OS to life by leveraging the following areas of
expertise:

- **OS Specialist:** Capable of designing not just the kernel, but the
    entire userland architecture.

- **Senior Software Architect:** Able to define high-level
    architectures and guide execution effectively.

- **Senior Programmer:** Capable of hand-writing code that surpasses
    AI-generated output in quality and precision.

- **Software Testing Expert:** Experienced in defining test oracles
    and designing automated test suites executable by AI agents.

- **Game Engine Specialist:** Skilled in architecting low-latency
    graphics and audio stacks.

- **Compiler & Toolchain Expert:** Capable of designing and
    configuring custom OS toolchains.

- **BPO (Business Process Outsourcing) Professional:** Experienced in
    designing standard operating procedures (SOPs) that ensure
    consistent delivery quality, regardless of who or what executes
    the tasks.

- **Requirements Engineering Professional:** Adept at clarifying and
    structuring requirements before a single component is built.

- **Software Design Professional:** Capable of drafting comprehensive
    design specifications prior to writing a single line of code.

- **Professional Project Manager:** Experienced in defining
    milestones, managing schedules, and keeping projects on track.

- **Veteran in Quality Engineering:** Extensive background in
    designing end-to-end quality across the entire product lifecycle.

- **MOT (Management of Technology) Background:** Experienced in
    transforming innovative technologies into structured, viable
    product designs.

## License

- `Kei`, `Keiland`, and `zedBSD` are distributed under the zlib License (see `LICENSE`).
