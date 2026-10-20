# gdb script: captures each RGXKickCDM2 (the compute kick) of a program
# on the StarFive Linux board with IMG's closed driver, and at the next
# ioctl walks the GPU page table (walk.py, /dev/mem) to dump the compute
# control stream, the data sequencer program each kernel names, and the
# shading cluster program its task issue starts.  Run as root.  The
# output, /tmp/cdm-capture.txt, is read by decode-cdm.py.
#
# Values referenced:
# StarFive Linux JH7110_VisionFive2_6.12.y_devel 4cecf169f38e
# drivers/gpu/drm/img/img-rogue: services/include/rgx_bridge.h
# (PVRSRV_BRIDGE_RGXCMP 129); generated/rogue/rgxcmp_bridge/
# common_rgxcmp_bridge.h (PVRSRV_BRIDGE_RGXCMP_RGXKICKCDM2 5; struct
# PVRSRV_BRIDGE_IN_RGXKICKCDM2, packed: pui8DMCmd 40, ui32CmdSize 84,
# ui32NumOfWorkgroups 92, ui32NumOfWorkitems 96, 108 bytes);
# include/rogue/rgxheapconfig.h (RGX_PDSCODEDATA_HEAP_BASE 0xDA00000000,
# RGX_USCCODE_HEAP_BASE 0xE000000000); Linux v7.3
# drivers/gpu/drm/imagination/pvr_rogue_fwif_client_check.h (struct
# rogue_fwif_cmd_compute: regs 8, cdm_ctrl_stream_base at regs + 32; the
# closed driver's command may differ, see CDM_STREAM_OFFSET); Mesa 25.0.7
# src/imagination/csbgen/rogue_cdm.xml (KERNEL0..8, STREAM_TERMINATE).
#
# Use: sudo gdb -q -batch -x capture-cdm.py --args /tmp/vkcompute
#      CDM_STREAM_OFFSET=N (environment) overrides the command offset of
#      the control stream's address (default 40).

import os
import re
import sys

import gdb

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import walk  # noqa: E402

IOCTL_SRVKM_CMD = 0xc0206440
BRIDGE_RGXCMP = 129
FUNC_KICKCDM2 = 5
OUT = "/tmp/cdm-capture.txt"
TRACE = "/sys/kernel/debug/pvr/gpu00/firmware_trace"
PDS_HEAP = 0xDA00000000
USC_HEAP = 0xE000000000
STREAM_OFFSET = int(os.environ.get("CDM_STREAM_OFFSET", "40"), 0)

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


def section(f, pc, name, va, size):
    log = lambda line: f.write(line + "\n")
    log("%s 0x%x %d bytes" % (name, va, size))
    try:
        data = walk.read_va(pc, va, size, lambda line: f.write("  # " + line + "\n"))
    except (OSError, LookupError) as error:
        log("  not read: %s" % error)
        return None
    walk.dump(data, va, log)
    return data


def kernel_words(stream):
    """Yields (offset, words) of each kernel, until a terminate or the end."""
    at = 0
    while at + 4 <= len(stream):
        word0 = walk.u32(stream, at)
        block = word0 >> 30
        if block == 2:
            return
        if block != 0:
            return
        count = 3 + (2 if word0 >> 29 & 1 else 3)
        count += 3 if word0 >> 28 & 1 else 0
        count += 2 if word0 >> 27 & 1 else 0
        count += 1
        yield at, [walk.u32(stream, at + 4 * i) for i in range(count) if at + 4 * i + 4 <= len(stream)]
        at += 4 * count


def dump_stream(f, stream_base):
    pc, note = latest_pc()
    f.write("stream 0x%x, %s\n" % (stream_base, note))
    if pc is None:
        return
    stream = section(f, pc, "stream", stream_base, 256)
    if stream is None:
        return
    for at, words in kernel_words(stream):
        data_size = (words[0] >> 2 & 0x3f) * 16
        data_addr = PDS_HEAP + (words[1] & ~0xf)
        code_addr = PDS_HEAP + (words[2] & ~0xf)
        f.write("kernel at +%d: %s\n" % (at, " ".join("%08x" % w for w in words)))
        data = section(f, pc, "pds-data", data_addr, max(data_size, 16))
        code = section(f, pc, "pds-code", code_addr, 128)
        if data is None or code is None:
            continue
        for index in range(0, len(code) - 3, 4):
            word = walk.u32(code, index)
            if word >> 28 == 0xf and word & 0x7 == 2:
                const = (word >> 8) & 0xff
                if const * 8 + 8 <= len(data):
                    task = walk.u64(data, const * 8)
                    usc = USC_HEAP + (task & 0xfffffffc)
                    f.write("doutu at code +%d: const64 %d task 0x%016x usc 0x%x temps-field %d\n" % (index, const, task, usc, task >> 35 & 0x3f))
                    section(f, pc, "usc", usc, 1024)


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
        if walk.u32(packet, 0) != BRIDGE_RGXCMP or walk.u32(packet, 4) != FUNC_KICKCDM2:
            return False
        bridge_in = read(walk.u64(packet, 8), walk.u32(packet, 24))
        size = walk.u32(bridge_in, 84)
        state["kicks"] += 1
        with open(OUT, "a") as f:
            f.write("kick %d: cdm command %d bytes, workgroups %d, workitems %d\n" % (
                state["kicks"], size, walk.u32(bridge_in, 92), walk.u32(bridge_in, 96)))
            command = read(walk.u64(bridge_in, 40), size) if size else b""
            f.write("command %d bytes\n" % size)
            walk.dump(command, 0, lambda line: f.write(line + "\n"))
            if size >= STREAM_OFFSET + 8:
                state["pending"] = walk.u64(command, STREAM_OFFSET) & 0xfffffffffc
        gdb.write("capture-cdm: kick %d\n" % state["kicks"])
        return False


gdb.execute("set pagination off")
gdb.execute("set breakpoint pending on")
gdb.execute("start", to_string=True)
IoctlBreak("ioctl")
gdb.execute("continue")
