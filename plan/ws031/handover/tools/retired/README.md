# Retired tools

Tools that wrote into, or patched, the old i915 tree (`src/drivers/gpu/i915/parity/`, now kept as
`src/drivers/gpu/i915-old/parity/` for the expert review) and cannot reproduce anything in the rebuilt
`src/drivers/gpu/i915/` (2026-09-22). They are kept as the record of how the old files were made.
Do not run them against the current tree: they write old paths and old names.

| tool | what it made | why retired |
| --- | --- | --- |
| `port_lcd_calc.py` + `port_lcd_modeset.json` | `parity/lcd/*` (Linux v6.8.12 display functions, macros and types extracted into glue files) and `parity/lcd/port_lcd_calc.manifest.json` | Retired by the S4 integration decision (`plan/ws031/i915-rebuild-s4.md` section 9-6, design R5): the display code was rewritten by hand into `display/*.c` and `data/display-*.inc` with new names and layout, and is maintained by hand from now on. The manifest (source files, sha256, extracted functions) is kept as `src/drivers/gpu/i915/data/provenance/port_lcd_calc.manifest.json`. |
| `port_dp_aux_pps.py` | `parity/dp/*` (Linux `intel_dp_aux.c`, `intel_pps.c`, DRM DPCD/EDID helpers) | Same decision (S4 section 9-6). The code now lives in `display/aux.c`, `dp-sink.c`, `panel.c`, `edid-read.c` and `data/display-*.inc`, rewritten by hand. |
| `port_intel_bios.py` | `parity/vbt/*` (Linux `intel_bios.c`, `intel_vbt_defs.h`) | Same decision (S4 section 9-6). The code now lives in `display/vbt.c` and `data/display-intel-bios.inc`, `data/display-intel-vbt-defs.inc`, rewritten by hand. |
| `gen_lrc_offsets.py` | `parity/gt_lrc_offsets.inc` (6.8.12 `gen12_{rcs,xcs}_offsets`, `PARITY_LRC_*` names) | Superseded. The new `data/i915-lrc-offsets.inc` is produced by `plan/ws029/tests/gen-inc.py lrc` from Linux v6.19 with `I915_LRC_*` names and the MIT notice; the audit found the two tables compile to the same bytes (`plan/ws031/i915-rebuild-coverage.md` section 4). This tool cannot reproduce the new file. |
| `engine_sseu.py` | a one-time source patch of `parity/gt_engine.{c,h}` (engine SSEU fields) | One-shot patch script, not a generator. Its result lives on in `engine.c`/`engine.h`; the text it searches for no longer exists. |

Still in use (in `..`): `gen_fw_ranges.py` (writes `data/forcewake-ranges.inc`), `gen_vk_server_codec.py`
(`data/vulkan-codec.inc`), `check_generated.sh` (re-runs both into a scratch directory and compares),
`notice_map.py`, `license_inventory.py` and `vk_opcode_survey.py` (read-only surveys of the new tree).
