# Prints the fields of the compute kicks /tmp/cdm-capture.txt holds
# (capture-cdm.py): each kernel's control stream words and the task issue
# word of its data sequencer program.
#
# Values referenced:
# Mesa 25.0.7 src/imagination/csbgen/rogue_cdm.xml (KERNEL0
# indirect_present 29, global_offsets_present 28, event_object_present
# 27, usc_common_size 18..26 in 64-byte units, usc_unified_size 12..17,
# pds_temp_size 8..11, pds_data_size 2..7 in 16-byte units, usc_target
# 1, fence 0; KERNEL1 data_addr 4..31, sd_type 2..3, usc_common_shared 1;
# KERNEL2 code_addr 4..31, one_wg_per_task 0; KERNEL3..5 workgroup
# counts less one; KERNEL8 max_instances 27..31, workgroup_size_x 18..26,
# _y 9..17, _z 0..8 less one); src/imagination/vulkan/pds/
# pvr_rogue_pds_defs.h (DOUTU src0: exe_off 2..31, sample_rate 33..34,
# temps 35..40 in units of 4, dual_phase 41).
#
# Use: python3 -I decode-cdm.py [/tmp/cdm-capture.txt]

import re
import sys


def bits(value, low, high):
    return value >> low & ((1 << (high - low + 1)) - 1)


def kernel(words):
    k0 = words[0]
    print("  KERNEL0 0x%08x: indirect %d, global offsets %d, event %d, usc_common_size %d (x64 B), usc_unified_size %d (x16 B), pds_temp_size %d (x16 B), pds_data_size %d (x16 B), usc_target %d, fence %d" % (
        k0, bits(k0, 29, 29), bits(k0, 28, 28), bits(k0, 27, 27), bits(k0, 18, 26), bits(k0, 12, 17),
        bits(k0, 8, 11), bits(k0, 2, 7), bits(k0, 1, 1), bits(k0, 0, 0)))
    if len(words) > 2:
        print("  KERNEL1 0x%08x: data_addr 0x%x, sd_type %d, usc_common_shared %d" % (words[1], words[1] & ~0xf, bits(words[1], 2, 3), bits(words[1], 1, 1)))
        print("  KERNEL2 0x%08x: code_addr 0x%x, one_wg_per_task %d" % (words[2], words[2] & ~0xf, bits(words[2], 0, 0)))
    rest = words[3:]
    if not bits(k0, 29, 29) and len(rest) >= 3:
        print("  KERNEL3..5: workgroups %d x %d x %d" % (rest[0] + 1, rest[1] + 1, rest[2] + 1))
        rest = rest[3:]
    elif len(rest) >= 2:
        print("  KERNEL6..7: indirect 0x%02x%08x" % (rest[0] & 0xff, rest[1] & ~3))
        rest = rest[2:]
    if bits(k0, 28, 28):
        print("  global offsets: %s" % " ".join("%08x" % w for w in rest[:3]))
        rest = rest[3:]
    if bits(k0, 27, 27):
        print("  event object: %s" % " ".join("%08x" % w for w in rest[:2]))
        rest = rest[2:]
    if rest:
        k8 = rest[0]
        print("  KERNEL8 0x%08x: max_instances %d, workgroup size %d x %d x %d" % (
            k8, bits(k8, 27, 31), bits(k8, 18, 26) + 1, bits(k8, 9, 17) + 1, bits(k8, 0, 8) + 1))


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/cdm-capture.txt"
    for line in open(path):
        line = line.rstrip("\n")
        if line.startswith("kick "):
            print(line)
            continue
        m = re.match(r"kernel at \+(\d+): (.*)", line)
        if m:
            print("kernel at stream +%s" % m.group(1))
            kernel([int(w, 16) for w in m.group(2).split()])
            continue
        m = re.match(r"doutu at code \+(\d+): const64 (\d+) task 0x([0-9a-f]+)", line)
        if m:
            task = int(m.group(3), 16)
            print("  DOUTU (code +%s, const64 %s): exe_off 0x%x, sample_rate %d, temps field %d (= %d registers at 4 each), dual_phase %d" % (
                m.group(1), m.group(2), task & 0xfffffffc, bits(task, 33, 34), bits(task, 35, 40), bits(task, 35, 40) * 4, bits(task, 41, 41)))


main()
