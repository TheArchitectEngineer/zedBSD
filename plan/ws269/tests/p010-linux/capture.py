# gdb script: captures the TA and 3D command bytes the closed Vulkan driver
# hands the kernel in each RGXKickTA3D2 bridge call on the StarFive Linux
# board (riscv64), and writes them to a text file for decode.py.
#
# Values referenced:
# StarFive Linux JH7110_VisionFive2_6.12.y_devel 4cecf169f38e
# drivers/gpu/drm/img/img-rogue: include/drm/pvr_drm.h (struct
# drm_pvr_srvkm_cmd: bridge_id 0, bridge_func_id 4, in_data_ptr 8,
# out_data_ptr 16, in_data_size 24, out_data_size 28;
# DRM_IOCTL_PVR_SRVKM_CMD = DRM_IOWR(0x40 + 0, 32 bytes) = 0xc0206440);
# services/include/rgx_bridge.h (PVRSRV_BRIDGE_RGXTA3D 130);
# generated/rogue/rgxta3d_bridge/common_rgxta3d_bridge.h
# (PVRSRV_BRIDGE_RGXTA3D_RGXKICKTA3D2 12; struct
# PVRSRV_BRIDGE_IN_RGXKICKTA3D2, packed: pui83DCmd 104, pui83DPRCmd 112,
# pui8TACmd 120, ui323DCmdSize 192, ui323DPRCmdSize 196, ui32TACmdSize
# 248, bbAbort 252, bbKick3D 253, bbKickPR 254, bbKickTA 255, 256 bytes).
#
# Use: gdb -q -x capture.py --args /tmp/vkstage draw   (see README.md)

import gdb

IOCTL_SRVKM_CMD = 0xc0206440
BRIDGE_RGXTA3D = 130
FUNC_KICKTA3D2 = 12
OUT = "/tmp/ta3d-capture.txt"

state = {"kicks": 0}


def u32(buf, off):
    return int.from_bytes(buf[off:off + 4], "little")


def u64(buf, off):
    return int.from_bytes(buf[off:off + 8], "little")


def read(addr, size):
    return bytes(gdb.selected_inferior().read_memory(addr, size))


def put(f, name, addr, size):
    f.write("%s %d bytes at 0x%x\n" % (name, size, addr))
    if addr == 0 or size == 0:
        return
    data = read(addr, size)
    for off in range(0, size, 32):
        words = [u32(data, o) for o in range(off, min(off + 32, size) - 3, 4)]
        f.write("  +%03x: %s\n" % (off, " ".join("%08x" % w for w in words)))


class IoctlBreak(gdb.Breakpoint):
    def stop(self):
        frame = gdb.selected_frame()
        request = int(frame.read_register("a1")) & 0xffffffff
        if request != IOCTL_SRVKM_CMD:
            return False
        packet = read(int(frame.read_register("a2")), 32)
        if u32(packet, 0) != BRIDGE_RGXTA3D or u32(packet, 4) != FUNC_KICKTA3D2:
            return False
        size = u32(packet, 24)
        bridge_in = read(u64(packet, 8), size)
        state["kicks"] += 1
        with open(OUT, "a") as f:
            f.write("kick %d: in_data %d bytes, abort %d kick3d %d kickpr %d kickta %d\n" % (
                state["kicks"], size, bridge_in[252], bridge_in[253], bridge_in[254], bridge_in[255]))
            put(f, "ta", u64(bridge_in, 120), u32(bridge_in, 248))
            put(f, "3d", u64(bridge_in, 104), u32(bridge_in, 192))
            put(f, "3dpr", u64(bridge_in, 112), u32(bridge_in, 196))
        gdb.write("capture: kick %d written to %s\n" % (state["kicks"], OUT))
        return False


gdb.execute("set pagination off")
gdb.execute("set breakpoint pending on")
gdb.execute("start", to_string=True)
IoctlBreak("ioctl")
gdb.execute("continue")
