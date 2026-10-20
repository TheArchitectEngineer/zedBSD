# The closed driver's TA command on the StarFive Linux board

On the StarFive Debian board (DDK 1.19, IMG's closed Vulkan driver), this captures the bytes of the TA (geometry) command and the 3D command that the user-mode driver hands the kernel in each `RGXKickTA3D2` bridge call. The decoder then prints the TA command's fields, to compare with the geometry command zedBSD builds.

1. Copy `capture.py` and `decode.py` to the board (`/tmp`). gdb and python3 are needed (`sudo apt install gdb`).
2. `rm -f /tmp/ta3d-capture.txt`
3. `gdb -q -batch -x /tmp/capture.py --args /tmp/vkstage draw`
   - gdb stops at main, then sets a breakpoint on `ioctl`. On every `DRM_IOCTL_PVR_SRVKM_CMD` (0xc0206440) whose bridge is RGXTA3D (130), function 12 (`RGXKickTA3D2`), it appends the TA, 3D and 3D partial render command bytes to `/tmp/ta3d-capture.txt` and carries on. Each kick prints one `capture:` line.
   - If `ioctl` cannot be resolved before libc loads, the pending breakpoint resolves when it loads.
4. `python3 -I /tmp/decode.py /tmp/ta3d-capture.txt` prints each TA command's fields (the Linux v7.3 `rogue_fwif_cmd_geom` layout, 112 bytes). The raw words stay in the capture file.

## Limits

- `vdm_ctrl_stream_base` is a GPU virtual address. Its words are not read here: the user-mode driver's CPU mapping of that buffer is not known from the bridge call.
- If the closed driver's command is not 112 bytes, the decoder says so. Read the raw words instead.
- The client CCB header and the fences are added by the kernel. They are not part of these bytes.

## The control stream and the PPP state (walk.py, capture2.py)

The bytes a GPU virtual address holds are read from physical memory by walking the GPU's page table from the page catalogue. The firmware trace gives the catalogue's physical address on each kick (`BIF Setup register set=…, PC address=0x…`).

- `sudo python3 -I walk.py PC_PHYS GPU_VA BYTES` prints the PC, PD and PT entries (with the page size and the flags), the physical address, and the words.
- `sudo gdb -q -batch -x capture2.py --args /tmp/vkstage draw` does what `capture.py` does and more:
  - It takes the TA command's `vdm_ctrl_stream_base` (TA command +16, the 64-byte IMG layout).
  - At the next ioctl, it reads `/sys/kernel/debug/pvr/gpu00/firmware_trace` for the latest `PC address=`.
  - It writes 256 bytes of the stream to `/tmp/ta3d-capture2.txt`, and the PPP state that the stream's first block (PPP_STATE0/1) names.
  - It needs debugfs mounted (`sudo mount -t debugfs none /sys/kernel/debug`) and the firmware trace on (the DDK's `PVRDebug` / apphint `EnableLogGroup`).

### When /dev/mem cannot read RAM

A kernel with `CONFIG_STRICT_DEVMEM` (Debian's default) refuses `/dev/mem` reads of RAM; `walk.py` then fails with `Operation not permitted`. Options:
- Boot with `iomem=relaxed` on the kernel command line. This lifts the restriction for I/O ranges only. On most kernels it does not open RAM, so try it first and check.
- `/proc/kcore`: an ELF core of the kernel's address space. Its program headers map the linear map's virtual addresses to physical ranges (`readelf -l /proc/kcore`). To read physical address P, take the PT_LOAD segment whose physical range holds P, and read at `offset + (P - paddr)`. To use it, change `walk.read_phys` to open `/proc/kcore` and make that conversion. It needs root and `CONFIG_PROC_KCORE`, which Debian enables.
- `devmem2` and `dd if=/dev/mem` go through the same `/dev/mem` and have the same restriction.
- A small kernel module or `drgn` (`drgn` reads physical memory through `/proc/kcore` with `prog.read(address, size, physical=True)`) is the most direct way when kcore is present.
