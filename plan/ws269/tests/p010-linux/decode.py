# Reads /tmp/ta3d-capture.txt (from capture.py) and prints each TA
# command's fields by the geometry command's layout.
#
# Values referenced:
# Linux v7.3 drivers/gpu/drm/imagination/pvr_rogue_fwif_client_check.h
# (struct rogue_fwif_cmd_geom, 112 bytes: cmd_shared 0, regs 16, flags
# 80, partial_render_geom_frag_fence 84, dummy_rgnhdr_init_geom_regs 96,
# brn61484_66333_live_rt 104; struct rogue_fwif_geom_regs:
# vdm_ctrl_stream_base 0, tpu_border_colour_table 8, vdm_draw_indirect0
# 16, vdm_draw_indirect1 24, ppp_ctrl 28, te_psg 32, tpu 36,
# vdm_context_resume_task0_size 40, vdm_context_resume_task3_size 44,
# pds_ctrl 48, view_idx 52, pds_coeff_free_prog 56) and
# pvr_rogue_fwif_shared_check.h (struct rogue_fwif_cmd_geom_frag_shared:
# frame_num 0, hwrt_data_fw_addr 4, pr_buffer_fw_addr 8).  The closed
# driver's command may be laid out differently; the raw words are always
# printed too.
#
# Use: python3 -I decode.py [/tmp/ta3d-capture.txt]

import re
import sys

FIELDS = [
    ("frame_num", 0, 4), ("hwrt_data_fw_addr", 4, 4), ("pr_buffer_fw_addr", 8, 4),
    ("vdm_ctrl_stream_base", 16, 8), ("tpu_border_colour_table", 24, 8),
    ("vdm_draw_indirect0", 32, 8), ("vdm_draw_indirect1", 40, 4),
    ("ppp_ctrl", 44, 4), ("te_psg", 48, 4), ("tpu", 52, 4),
    ("vdm_context_resume_task0_size", 56, 4), ("vdm_context_resume_task3_size", 60, 4),
    ("pds_ctrl", 64, 4), ("view_idx", 68, 4), ("pds_coeff_free_prog", 72, 4),
    ("flags", 80, 4), ("partial_render_geom_frag_fence", 84, 12),
    ("dummy_rgnhdr_init_geom_regs", 96, 8), ("brn61484_66333_live_rt", 104, 4),
]


def commands(path):
    kick, name, words = None, None, []
    for line in open(path):
        m = re.match(r"kick (\d+):", line)
        if m:
            kick = int(m.group(1))
            continue
        m = re.match(r"(\w+) (\d+) bytes at", line)
        if m:
            if name is not None:
                yield kick, name, words
            name, words = m.group(1), []
            continue
        m = re.match(r"\s+\+[0-9a-f]+: (.*)", line)
        if m:
            words += [int(w, 16) for w in m.group(1).split()]
    if name is not None:
        yield kick, name, words


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ta3d-capture.txt"
    for kick, name, words in commands(path):
        if name != "ta" or not words:
            continue
        data = b"".join(w.to_bytes(4, "little") for w in words)
        print("kick %d: ta command %d bytes%s" % (kick, len(data), "" if len(data) == 112 else " (not 112: the layout below may not fit)"))
        for field, off, size in FIELDS:
            if off + size <= len(data):
                value = int.from_bytes(data[off:off + size], "little")
                print("  %-32s +%03d  0x%0*x" % (field, off, size * 2, value))


main()
