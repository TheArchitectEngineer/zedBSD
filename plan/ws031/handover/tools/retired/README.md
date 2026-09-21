# Retired tools

Tools that wrote into, or patched, the old i915 tree (`src/drivers/gpu/i915/parity/`, now kept as
`src/drivers/gpu/i915-old/parity/` for the expert review) and cannot reproduce anything in the rebuilt
`src/drivers/gpu/i915/` (2026-09-22). They are kept as the record of how the old files were made.
Do not run them against the current tree: they write old paths and old names.

| tool | what it made | why retired |
| --- | --- | --- |
| `port_lcd_calc.py` + `port_lcd_modeset.json` | `parity/lcd/*` (Linux v6.8.12 display functions, macros and types extracted into glue files) and `parity/lcd/port_lcd_calc.manifest.json` | Retired by the S4 integration decision (`plan/ws031/i915-rebuild-s4.md` section 9-6, design R5): the display code was rewritten by hand into `display/*.c` and the quoted definitions (now the display headers of `intel/`) with new names and layout, and is maintained by hand from now on. The manifest (source files, sha256, extracted functions) was kept for a while as `src/drivers/gpu/i915/intel/provenance/port_lcd_calc.manifest.json` and deleted with that directory on 2026-09-22 (user decision). |
| `port_dp_aux_pps.py` | `parity/dp/*` (Linux `intel_dp_aux.c`, `intel_pps.c`, DRM DPCD/EDID helpers) | Same decision (S4 section 9-6). The code now lives in `display/aux.c`, `dp-sink.c`, `panel.c`, `edid-read.c` and the display headers of `intel/` (the DisplayPort definitions in `intel/dp.h`), rewritten by hand. |
| `port_intel_bios.py` | `parity/vbt/*` (Linux `intel_bios.c`, `intel_vbt_defs.h`) | Same decision (S4 section 9-6). The code now lives in `display/vbt.c` and `intel/vbt.h`, `intel/vbt-defs.h`, rewritten by hand. |
| `gen_lrc_offsets.py` | `parity/gt_lrc_offsets.inc` (6.8.12 `gen12_{rcs,xcs}_offsets`, `PARITY_LRC_*` names) | Superseded. The table that replaced it (now in `intel/lrc-offsets.h`) was produced by `gen-inc.py lrc` (below) from Linux v6.19 with `I915_LRC_*` names and the MIT notice; the audit found the two tables compile to the same bytes (`plan/ws031/i915-rebuild-coverage.md` section 4). This tool cannot reproduce the new file. |
| `gen-inc.py` (was `plan/ws029/tests/gen-inc.py`) | `data/i915-{regs,ids,commands,lrc-offsets,mocs}.inc` (definitions and tables excerpted from the fetched Linux v6.19 tree, with the MIT notice, the source SHA-256 and the rewrite rules) | Retired 2026-09-22 when `data/` became `external/`: its five outputs are merged with the hand transcriptions into `external/i915.h` (and the unused MOCS table into `external/i915-superseded.h`), with one licence block and duplicate macros removed, so a per-file generator no longer matches (`external/i915.h` was later split by topic into `intel/gt-regs.h`, `commands.h`, `lrc-offsets.h`, `pci-ids.h` and the rest when `external/` became `intel/`); its input tree (`plan/ws029/temp/linux/v6.19`) is also not kept on the build host. The provenance it recorded (sources, SHA-256, rewrites) is kept in the descriptions of the headers `external/i915.h` was later split into (`intel/gt-regs.h`, `commands.h`, `lrc-offsets.h`, `pci-ids.h`, `mocs.h`). |
| `engine_sseu.py` | a one-time source patch of `parity/gt_engine.{c,h}` (engine SSEU fields) | One-shot patch script, not a generator. Its result lives on in `engine.c`/`engine.h`; the text it searches for no longer exists. |

Still in use (in `..`): `gen_fw_ranges.py` (writes `intel/forcewake-ranges.inc`), `gen_vk_server_codec.py`
(`render/vulkan-codec.inc`), `check_generated.sh` (re-runs both into a scratch directory and compares),
`notice_map.py`, `license_inventory.py` and `vk_opcode_survey.py` (read-only surveys of the new tree).

## Old-tree sweep scripts (retired 2026-09-22)

`sweep_e108.sh` … `sweep_e123f.sh` and `build_verify.sh` built the old tree with `CONFIG_DRIVER_PCI_I915_PARITY=y`
and one `PARITY_*_TEST` flag per run.  The option no longer exists; the new driver has no parity build.  Test
scenarios now run with `plan/ws031/tests/vkloop-hw.sh test <scenario>` (`I915_TESTS=y`).
