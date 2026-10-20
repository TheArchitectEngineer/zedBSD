# gdb script: as capture.py, and after each RGXKickTA3D2 with a TA command
# stops at the next ioctl, reads the firmware trace for the latest page
# catalogue address, and dumps 256 bytes of the TA's control stream and
# the PPP state its first block names (walk.py, /dev/mem).  Run as root.
#
# Values referenced:
# the same headers as capture.py; Mesa 25.0.7
# src/imagination/csbgen/rogue_vdm.xml (VDMCTRL PPP_STATE0: block_type
# 29..31 = 0, word_count 8..15, addrmsb 0..7; PPP_STATE1: addrlsb 2..31).
#
# Use: sudo gdb -q -batch -x capture2.py --args /tmp/vkstage draw

import os
import re
import sys

import gdb

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import walk  # noqa: E402

IOCTL_SRVKM_CMD = 0xc0206440
BRIDGE_RGXTA3D = 130
FUNC_KICKTA3D2 = 12
OUT = "/tmp/ta3d-capture2.txt"
TRACE = "/sys/kernel/debug/pvr/gpu00/firmware_trace"

state = {"kicks": 0, "pending": None}


def read(addr, size):
    return bytes(gdb.selected_inferior().read_memory(addr, size))


def latest_pc():
    try:
        text = open(TRACE).read()
    except OSError as error:
        return None, "trace not read: %s" % error
    found = re.findall(r"PC address=0x([0-9a-fA-F]+)", text)
    if not found:
        return None, "no PC address in the trace"
    return int(found[-1], 16), "PC 0x%s (%d in the trace)" % (found[-1], len(found))


def dump_stream(f, stream_base):
    log = lambda line: f.write(line + "\n")
    pc, note = latest_pc()
    log("stream 0x%x, %s" % (stream_base, note))
    if pc is None:
        return
    try:
        data = walk.read_va(pc, stream_base & ~3, 256, log)
    except (OSError, LookupError) as error:
        log("stream not read: %s" % error)
        return
    walk.dump(data, stream_base, log)
    word0, word1 = walk.u32(data, 0), walk.u32(data, 4)
    if word0 >> 29 == 0:
        count = (word0 >> 8) & 0xff
        address = ((word0 & 0xff) << 32) | (word1 & ~3)
        log("PPP_STATE0/1: %d words at 0x%x" % (count, address))
        try:
            walk.dump(walk.read_va(pc, address, max(count, 1) * 4, log), address, log)
        except (OSError, LookupError) as error:
            log("PPP state not read: %s" % error)
    else:
        log("first block type %d, not a PPP state" % (word0 >> 29))


class IoctlBreak(gdb.Breakpoint):
    def stop(self):
        frame = gdb.selected_frame()
        request = int(frame.read_register("a1")) & 0xffffffff
        if state["pending"] is not None:
            with open(OUT, "a") as f:
                dump_stream(f, state["pending"])
            state["pending"] = None
        if request != IOCTL_SRVKM_CMD:
            return False
        packet = read(int(frame.read_register("a2")), 32)
        if walk.u32(packet, 0) != BRIDGE_RGXTA3D or walk.u32(packet, 4) != FUNC_KICKTA3D2:
            return False
        bridge_in = read(walk.u64(packet, 8), walk.u32(packet, 24))
        ta_size = walk.u32(bridge_in, 248)
        state["kicks"] += 1
        with open(OUT, "a") as f:
            f.write("kick %d: ta %d bytes, kickta %d\n" % (state["kicks"], ta_size, bridge_in[255]))
            if ta_size >= 24:
                ta = read(walk.u64(bridge_in, 120), ta_size)
                walk.dump(ta, 0, lambda line: f.write(line + "\n"))
                state["pending"] = walk.u64(ta, 16)
        gdb.write("capture2: kick %d\n" % state["kicks"])
        return False


gdb.execute("set pagination off")
gdb.execute("set breakpoint pending on")
gdb.execute("start", to_string=True)
IoctlBreak("ioctl")
gdb.execute("continue")
