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
