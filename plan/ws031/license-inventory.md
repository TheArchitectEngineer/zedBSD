# WS031 licence inventory (first pass, facts only)

自動抽出した事実のみ。判断（区分・互換性・配布可否）は含まない。`linux refs`／`mesa refs` は本文中で上流のファイル名や用語に言及している箇所の数で、複製の有無を示すものではない。

**2026-09-22 更新（i915 再構築の後）**: 旧 `src/drivers/gpu/i915/` は `src/drivers/gpu/i915-old/` へ待避され（専門家レビュー用、build 外）、
新しい `src/drivers/gpu/i915/` が本番になった。§1 は初回棚卸しの 196 行をそのまま残し、各行の現在の置き場を先頭列に加えた
（対応は [被覆監査](i915-rebuild-coverage.md) §2 の「実際の行き先」。関数単位で分かれたものは全部、定義の移動は主な置き場だけを書いた。
監査の機械照合が gt 系の小さな static `fail()` を `display/dp-sink.c` の `i915_edp_fail()` に当てた 4 行は、その対応を除いた）。
§1 の `lines` 以降の列は**旧ファイルの**初回棚卸し時の事実で、再計算していない。新ファイルの表示は先頭列の括弧に書いた
（`Zlib`＝zedBSD の SPDX、`MIT`・`GPL-2.0`＝上流の SPDX 行、`MIT 文`＝MIT permission notice の本文、`DRM 文`＝DRM の
"Permission to use, copy, modify, distribute, and sell" 型の permission notice の本文、`表示なし`＝いずれもない）。
§2 は新ツリーで上流の表示を持ち §1 に現れないファイル、§3 は GPL-2.0 由来コードの件（解決済み）。

**2026-09-22 更新（`data/` → `external/`）**: Linux／Mesa 由来の定義と表は `src/drivers/gpu/i915/external/` に移り、系統ごとに統合された（GT は `external/i915.h`、表示は `external/display/<family>.h`、DisplayPort は `intel/dp.h`、配列の本体は `external/*.inc`、firmware は `external/firmware/`）。各ファイルの先頭は zedBSD の著作権行（licence 行なし）、続いて licence 文ごとに一つの block（出典の Copyright 行を統合し、MIT 文・DRM 文は一度だけ。SPDX だけの出典は `SPDX-License-Identifier: MIT` と Copyright 行）、説明（出典の版・ファイル・sha256）、定義。§1 の先頭列の括弧は統合後のファイルの表示で、同じファイルが複数行に現れる。libvulkan から生成する `vulkan-codec.inc` は zedBSD 自身のコードなので `render/` へ移した。
**2026-09-22 更新（`external/` → `intel/`）**: `src/drivers/gpu/i915/external/` を `intel/` に改めた。表示の header は `display/` を挟まず `intel/` 直下（`intel/dp.h`、`intel/mreg.h` など、名前は不変）、配列の本体は `i915-` を外した `intel/engine-table.inc`・`forcewake-ranges.inc`・`mocs-table.inc`・`mcr-ranges.inc`、`eu-encoding-gen12.h`・`provenance/`・`firmware/`（一時。どちらも同日のうちに削除: firmware は §4・§5、`provenance/` はユーザー決定）も `intel/` へ。寄せ集めになっていた `external/i915.h` は内容ごとに分割して削除した: `intel/bits.h`（Linux の bit helper の置換。zedBSD 自身の定義なので Zlib）、`gt-regs.h`（GT・engine・interrupt・context・GTT の register と execution path の register）、`commands.h`（MI・blitter・PIPE_CONTROL と execution path の command）、`lrc-offsets.h`（Gen12 context image の offset 表）、`pci-ids.h`、`gt-power.h`（RC6／RPS）、`workarounds.h`、`mocs.h`（MOCS と PAT）、`genxml.h`（Mesa genxml の Gen12 3D state、Mesa 自身の表示）。各 header は zedBSD の著作権行（licence 行なし）、その header が写した出典の Copyright 行だけを統合した licence block 一つ（SPDX だけの出典なら `SPDX-License-Identifier: MIT`＋Copyright 行、`i915_reg.h`・`i915_drm.h`・`pciids.h` を含むものはその MIT 文）、出典（版・ファイル・sha256）を書いた説明、include guard、定義の順で、使う header（`bits.h`、`<stdint.h>`）を自分で include する。各 .c は使う header だけを include する。kernel の vmunix（本番と `I915_TESTS=y`）は再構成の前とバイト一致。
新ツリーの事実は `python3 plan/ws031/handover/tools/license_inventory.py`（新ツリーを走査）と `notice_map.py` で再抽出できる。

## 1. 初回棚卸しの行（旧ファイル → 現在の置き場）

| current file(s)（`src/drivers/gpu/i915/` 相対、括弧は表示） | status | old file（`src/drivers/gpu/i915-old/` 相対） | lines | SPDX | copyright lines in header | linux refs | mesa refs | generated | tracked |
|---|---|---|---|---|---|---|---|---|---|
| `tests/fixtures/draw-fixture.h` (Zlib) | test-only (S5 T4a) | `draw_fixture.h` | 100 | - | - | 0 | 2 | - | yes |
| — | retired (plan §5: not reachable in production) | `engine.c` | 548 | Zlib | Copyright (C) 2026 Awe Morris | 5 | 0 | - | yes |
| `memory.c` (Zlib) | moved | `gem.c` | 269 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| — | retired (plan §5: not reachable in production) | `ggtt.c` | 500 | Zlib | Copyright (C) 2026 Awe Morris | 1 | 0 | - | yes |
| `command.c` (Zlib)<br>`resource.c` (Zlib)<br>`i915.c` (Zlib)<br>`session.c` (Zlib)<br>`job.c` (Zlib)<br>`reset.c` (Zlib) | moved | `i915.c` | 1983 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `i915.h` (Zlib)<br>`device.h` (Zlib)<br>`session.h` (Zlib)<br>`request-queue.h` (Zlib)<br>`command.c` (Zlib)<br>`job.c` (Zlib) | moved | `internal.h` | 452 | Zlib | Copyright (C) 2026 Awe Morris | 1 | 0 | - | yes |
| — | retired (plan §5: not reachable in production) | `irq.c` | 365 | Zlib | Copyright (C) 2026 Awe Morris | 2 | 0 | - | yes |
| `intel/commands.h` (MIT) | moved | `linux/i915-commands.inc` | 244 | MIT | Copyright © 2003-2018 Intel Corporation | 132 | 0 | yes | yes |
| `intel/pci-ids.h` (MIT 文) | moved | `linux/i915-ids.inc` | 82 | MIT | Copyright 2013 Intel Corporation | 0 | 0 | yes | yes |
| `intel/lrc-offsets.h` (MIT) | moved | `linux/i915-lrc-offsets.inc` | 190 | MIT | Copyright © 2014 Intel Corporation | 8 | 0 | yes | yes |
| (deleted 2026-09-22: unused by the new driver; old `external/i915-superseded.h`) (MIT+MIT 文) | moved | `linux/i915-mocs.inc` | 236 | MIT | Copyright © 2015 Intel Corporation | 10 | 0 | yes | yes |
| `intel/gt-regs.h` (MIT 文)<br>`intel/mocs.h` (MIT) | moved | `linux/i915-regs.inc` | 580 | MIT | Copyright © 2019 Intel Corporation; Copyright © 2022 Intel Corporation | 356 | 0 | - | yes |
| (deleted 2026-09-22: unused by the new driver; old `external/i915-superseded.h`) (MIT+MIT 文) | moved | `linux/i915-workarounds.inc` | 138 | Zlib | Copyright (C) 2026 Awe Morris | 4 | 5 | - | yes |
| — | retired (plan §5: not reachable in production) | `lrc.c` | 537 | Zlib | Copyright (C) 2026 Awe Morris | 2 | 0 | - | yes |
| `pci.h` (Zlib)<br>`gt.h` (Zlib)<br>`device.c` (Zlib) | moved (renamed) | `parity/backend.h` | 42 | - | - | 0 | 0 | - | yes |
| `workqueue.c` (Zlib) | moved | `parity/backend_delayed.c` | 237 | - | - | 0 | 0 | - | NEW |
| `workqueue.h` (Zlib) | moved (renamed) | `parity/backend_delayed.h` | 61 | - | - | 0 | 0 | - | NEW |
| `dma.c` (Zlib) | moved | `parity/backend_dma.c` | 47 | - | - | 0 | 0 | - | yes |
| `mmio.c` (Zlib)<br>`runtime-pm.c` (Zlib) | moved | `parity/backend_mmio.c` | 159 | - | - | 0 | 0 | yes | yes |
| `pci.c` (Zlib) | moved | `parity/backend_pci.c` | 139 | - | - | 0 | 0 | - | yes |
| `workqueue.c` (Zlib)<br>`sync.c` (Zlib) | moved | `parity/backend_sync.c` | 286 | - | - | 0 | 0 | - | yes |
| `sync.h` (Zlib)<br>`workqueue.h` (Zlib) | moved (renamed) | `parity/backend_sync.h` | 85 | - | - | 0 | 0 | - | yes |
| `display/vbt.c` (Zlib+MIT 文) | moved | `parity/bios.c` | 507 | - | - | 1 | 0 | - | yes |
| `display/internal.h` (MIT+Zlib)<br>`display/vbt.c` (Zlib+MIT 文)<br>`display/takeover.c` (MIT+Zlib) | moved (test knobs → S5) | `parity/bios.h` | 96 | - | - | 1 | 0 | - | yes |
| `display/clock.c` (Zlib+MIT 文)<br>`device-info.c` (Zlib) | moved | `parity/cdclk.c` | 522 | - | - | 4 | 0 | - | yes |
| `display/internal.h` (MIT+Zlib) | moved | `parity/cdclk.h` | 101 | - | - | 0 | 0 | - | yes |
| `display/phy.c` (MIT+Zlib)<br>`display/hotplug.c` (Zlib+MIT 文+DRM 文) | moved | `parity/combo_phy.c` | 202 | - | - | 1 | 0 | - | yes |
| `display/internal.h` (MIT+Zlib) | moved | `parity/combo_phy.h` | 40 | - | - | 0 | 0 | - | yes |
| `display/power.c` (MIT+Zlib)<br>`display/hotplug.c` (Zlib+MIT 文+DRM 文)<br>`reset.c` (Zlib) | moved | `parity/display_core.c` | 397 | - | - | 1 | 0 | - | yes |
| `display/power.c` (MIT+Zlib) | moved | `parity/display_core.h` | 60 | - | - | 0 | 0 | - | yes |
| `display/takeover.c` (MIT+Zlib)<br>`display/clock.c` (Zlib+MIT 文)<br>`display/pipe.c` (MIT+Zlib+MIT 文)<br>`display/hotplug.c` (Zlib+MIT 文+DRM 文) | moved | `parity/display_nogem.c` | 1553 | - | - | 7 | 0 | - | yes |
| `display/internal.h` (MIT+Zlib) | moved | `parity/display_nogem.h` | 368 | - | - | 0 | 0 | - | yes |
| `display/watermark.c` (MIT+Zlib)<br>`display/state.c` (Zlib)<br>`display/hotplug.c` (Zlib+MIT 文+DRM 文) | moved | `parity/display_state.c` | 861 | - | - | 9 | 0 | - | yes |
| `display/internal.h` (MIT+Zlib) | moved | `parity/display_state.h` | 293 | - | - | 1 | 0 | - | yes |
| `display/dmc.c` (Zlib+MIT 文)<br>`mmio.c` (Zlib) | moved | `parity/dmc.c` | 629 | - | - | 3 | 0 | - | yes |
| `display/dmc.h` (Zlib) | moved | `parity/dmc.h` | 142 | - | - | 1 | 0 | - | yes |
| `display/dp-internal.h` (Zlib)<br>`display/modeset-internal.h` (MIT+Zlib+MIT 文)<br>`display/internal.h` (MIT+Zlib)<br>`display/dp.c` (Zlib+MIT 文+DRM 文)<br>`display/pipe.c` (MIT+Zlib+MIT 文) | moved | `parity/dp/dp_compat.h` | 323 | - | - | 8 | 0 | - | yes |
| `tests/display/dp-fake-hw.c` (Zlib) | test-only (S5 T3) | `parity/dp/dp_fake_hw.c` | 481 | - | - | 0 | 0 | - | yes |
| `tests/display/dp-fake-hw.h` (Zlib) | test-only (S5 T3) | `parity/dp/dp_fake_hw.h` | 91 | - | - | 0 | 0 | - | yes |
| `tests/display/dp-fixture-latitude5330.h` (Zlib) | test-only (S5 T3) | `parity/dp/dp_fixture_latitude5330.h` | 85 | - | - | 0 | 0 | - | yes |
| `intel/dp.h` (MIT+MIT 文+DRM 文) | moved | `parity/dp/dp_ref_types.h` | 54 | - | Copyright Intel Corporation; the full | 3 | 0 | yes | yes |
| `intel/dp.h` (MIT+MIT 文+DRM 文) | moved | `parity/dp/drm_dp.h` | 1746 | - | Copyright © 2008 Keith Packard | 1 | 0 | - | yes |
| `display/dp-sink.c` (Zlib+DRM 文) | moved | `parity/dp/drm_dp_helper_port.c` | 653 | - | Copyright © 2009 Keith Packard | 2 | 0 | yes | yes |
| `display/edid-read.c` (Zlib+MIT 文) | moved | `parity/dp/drm_edid_port.c` | 158 | - | Copyright (c) 2006 Luc Verhaegen (quirks list); Copyright (c) 2007-2008 Intel Corporation | 1 | 0 | yes | yes |
| `tests/display/edp-ktest.c` (Zlib) | test-only (S5 T4b) | `parity/dp/edp_ktest.c` | 174 | - | - | 0 | 0 | - | yes |
| `tests/display/edp-ktest.h` (Zlib) | test-only (S5 T4b) | `parity/dp/edp_ktest.h` | 11 | - | - | 0 | 0 | - | yes |
| `tests/display/edp-sync-ktest.c` (Zlib) | test-only (S5 T4b) | `parity/dp/edp_sync_ktest.c` | 312 | - | - | 0 | 0 | - | NEW |
| `intel/dp.h` (MIT+MIT 文+DRM 文) | moved | `parity/dp/intel_dp_aux.h` | 32 | MIT | Copyright © 2020-2021 Intel Corporation | 2 | 0 | - | yes |
| `display/aux.c` (Zlib+MIT 文) | moved | `parity/dp/intel_dp_aux_port.c` | 511 | MIT | Copyright © 2020-2021 Intel Corporation | 4 | 0 | yes | yes |
| `intel/dp.h` (MIT+MIT 文+DRM 文) | moved | `parity/dp/intel_dp_aux_regs.h` | 103 | MIT | Copyright © 2023 Intel Corporation | 3 | 0 | - | yes |
| `intel/dp.h` (MIT+MIT 文+DRM 文) | moved | `parity/dp/intel_pps.h` | 61 | MIT | Copyright © 2020 Intel Corporation | 3 | 0 | - | yes |
| `display/panel.c` (Zlib+MIT 文) | moved | `parity/dp/intel_pps_port.c` | 1289 | MIT | Copyright © 2020 Intel Corporation | 4 | 0 | yes | yes |
| `intel/dp.h` (MIT+MIT 文+DRM 文) | moved | `parity/dp/intel_pps_regs.h` | 85 | MIT | Copyright © 2023 Intel Corporation | 3 | 0 | - | yes |
| `display/aux.c` (Zlib+MIT 文) | moved | `parity/dp/parity_dp_aux_glue.inc` | 22 | - | - | 1 | 0 | - | yes |
| `display/dp-sink.c` (Zlib+DRM 文)<br>`display/hotplug.c` (Zlib+MIT 文+DRM 文) | moved | `parity/dp/parity_dp_kernel.c` | 590 | - | - | 0 | 0 | - | yes |
| `display/dp-sink.h` (Zlib) | moved | `parity/dp/parity_dp_kernel.h` | 89 | - | - | 0 | 0 | - | yes |
| `display/dp-sink.c` (Zlib+DRM 文) | moved | `parity/dp/parity_drm_dp_glue.inc` | 15 | - | - | 0 | 0 | - | yes |
| `display/edid-read.c` (Zlib+MIT 文) | moved | `parity/dp/parity_drm_edid_glue.inc` | 54 | - | - | 0 | 0 | - | yes |
| `display/dp-sink.c` (Zlib+DRM 文) | moved | `parity/dp/parity_edp.c` | 393 | - | - | 1 | 0 | - | yes |
| `display/internal.h` (MIT+Zlib) | moved | `parity/dp/parity_edp.h` | 171 | - | - | 2 | 0 | - | yes |
| `display/watermark.c` (MIT+Zlib) | moved | `parity/dram_bw.c` | 301 | - | - | 1 | 0 | - | yes |
| `display/internal.h` (MIT+Zlib) | moved | `parity/dram_bw.h` | 90 | - | - | 0 | 0 | - | yes |
| `display/hotplug.c` (Zlib+MIT 文+DRM 文)<br>`display/display.c` (Zlib)<br>`display/power.c` (MIT+Zlib) | moved | `parity/driver_probe.c` | 397 | - | - | 2 | 0 | - | yes |
| `display/internal.h` (MIT+Zlib) | moved | `parity/driver_probe.h` | 181 | - | - | 4 | 0 | - | yes |
| `display/display.c` (Zlib) | moved | `parity/drm_device.c` | 195 | - | - | 0 | 0 | - | yes |
| `display/internal.h` (MIT+Zlib) | moved | `parity/drm_device.h` | 88 | - | - | 0 | 0 | - | yes |
| `tests/execution/eu-test.c` (Zlib) | test-only (S5 T4a) | `parity/eu_test.c` | 1794 | - | - | 0 | 1 | - | yes |
| `tests/execution/eu-test.h` (Zlib) | test-only (S5 T4a) | `parity/eu_test.h` | 359 | - | - | 2 | 0 | - | yes |
| (deleted 2026-09-22: DMC bytes removed from the kernel; read from `/lib/firmware/i915/`, §4) | removed | `parity/firmware_adlp_dmc.c` | 4952 | - | - | 0 | 0 | yes | yes |
| `vendor/intel-vbt/dell-latitude-5330-1028-0b02.inc` (test build only, licence unaudited, §5) | moved (test-only) | `parity/firmware_vbt_dell_latitude_5330.c` | 559 | - | - | 0 | 0 | yes | yes |
| `defaults.c` (Zlib)<br>`engine.c` (Zlib) | moved | `parity/gt_defaults.c` | 316 | - | - | 0 | 0 | - | yes |
| `defaults.h` (Zlib) | moved | `parity/gt_defaults.h` | 98 | - | - | 0 | 0 | - | yes |
| `submit.c` (Zlib)<br>`engine.c` (Zlib) | moved | `parity/gt_engine.c` | 337 | - | - | 0 | 0 | - | yes |
| `intel/gt-regs.h` (MIT 文) | moved | `parity/gt_engine.h` | 152 | - | - | 5 | 0 | - | yes |
| `intel/forcewake-ranges.inc` (MIT 文) | moved | `parity/gt_fw_ranges.inc` | 52 | - | - | 1 | 0 | yes | yes |
| `workarounds.h` (Zlib) | moved | `parity/gt_init.h` | 249 | - | - | 6 | 0 | - | yes |
| `workarounds.c` (Zlib)<br>`gt-power.c` (Zlib) | moved | `parity/gt_init_base.c` | 680 | - | - | 4 | 0 | - | yes |
| `context.c` (Zlib) | moved | `parity/gt_lrc.c` | 600 | - | - | 0 | 0 | - | yes |
| `context.c` (Zlib) | moved | `parity/gt_lrc.h` | 216 | - | - | 4 | 0 | - | yes |
| `intel/lrc-offsets.h` (MIT) | moved (merged) | `parity/gt_lrc_offsets.inc` | 156 | - | - | 1 | 0 | yes | yes |
| `ggtt.c` (Zlib)<br>`ppgtt.c` (Zlib)<br>`memory.c` (Zlib) | moved | `parity/gt_mem.c` | 781 | - | - | 0 | 0 | - | yes |
| `ppgtt.c` (Zlib) | moved | `parity/gt_mem.h` | 216 | - | - | 1 | 0 | - | yes |
| `migrate.c` (Zlib) | moved | `parity/gt_migrate.c` | 141 | - | - | 0 | 0 | - | yes |
| `migrate.h` (Zlib) | moved | `parity/gt_migrate.h` | 79 | - | - | 2 | 0 | - | yes |
| `device-info.c` (Zlib) | moved | `parity/gt_mmio.c` | 478 | - | - | 7 | 0 | - | yes |
| `device-info.h` (Zlib) | moved | `parity/gt_mmio.h` | 124 | - | - | 0 | 0 | - | yes |
| `request.c` (Zlib) | moved | `parity/gt_request.c` | 416 | - | - | 0 | 0 | - | yes |
| `request.c` (Zlib) | moved | `parity/gt_request.h` | 139 | - | - | 7 | 0 | - | yes |
| `engine.c` (Zlib) | moved | `parity/gt_resume.c` | 163 | - | - | 0 | 0 | - | yes |
| `engine.c` (Zlib) | moved | `parity/gt_resume.h` | 76 | - | - | 0 | 0 | - | yes |
| `submit.c` (Zlib)<br>`request.c` (Zlib) | moved | `parity/gt_submit.c` | 300 | - | - | 0 | 0 | - | yes |
| `submit.h` (Zlib)<br>`submit.c` (Zlib)<br>`intel/gt-regs.h` (MIT 文) | moved | `parity/gt_submit.h` | 97 | - | - | 2 | 0 | - | yes |
| `verify-workarounds.c` (Zlib) | moved | `parity/gt_verify_wa.c` | 367 | - | - | 1 | 0 | - | yes |
| `verify-workarounds.c` (Zlib) | moved | `parity/gt_verify_wa.h` | 124 | - | - | 3 | 0 | - | yes |
| `workarounds.c` (Zlib) | moved | `parity/gt_wa_adlp.c` | 340 | - | - | 0 | 0 | - | yes |
| `display/interrupts.c` (Zlib)<br>`irq.c` (Zlib) | moved | `parity/irq.c` | 937 | - | - | 8 | 0 | - | yes |
| `irq.h` (Zlib) | moved | `parity/irq.h` | 164 | - | - | 1 | 0 | - | yes |
| `tests/execution/ktest.c` (Zlib) | test-only (S5 T4a) | `parity/ktest.c` | 5437 | - | - | 0 | 0 | - | yes |
| `tests/execution/ktest.h` (Zlib) | test-only (S5 T4a) | `parity/ktest.h` | 16 | - | - | 0 | 0 | - | yes |
| `display/edid.c` (Zlib+MIT 文) | moved | `parity/lcd/drm_edid_mode_port.c` | 215 | - | Copyright (c) 2006 Luc Verhaegen (quirks list); Copyright (c) 2007-2008 Intel Corporation | 1 | 0 | yes | NEW |
| `intel/ref.h` (MIT+MIT 文) | moved | `parity/lcd/edid_ref_types.h` | 264 | - | Copyright © 2007-2008 Intel Corporation | 0 | 0 | yes | NEW |
| `display/clock.c` (Zlib+MIT 文) | moved | `parity/lcd/intel_dpll_port.c` | 174 | - | Copyright © 2006-2016 Intel Corporation | 2 | 0 | yes | NEW |
| `display/dp.c` (Zlib+MIT 文+DRM 文) | moved | `parity/lcd/intel_link_port.c` | 235 | - | Copyright © 2008 Intel Corporation; Copyright Intel Corporation): intel_reduce_m_n_ratio, compute_m_n, | 4 | 0 | yes | NEW |
| `display/modeset-internal.h` (MIT+Zlib+MIT 文) | moved | `parity/lcd/lcd_compat.h` | 137 | - | - | 2 | 0 | - | NEW |
| `intel/ref.h` (MIT+MIT 文) | moved | `parity/lcd/lcd_ref_types.h` | 82 | - | Copyright Intel Corporation; the full notices are kept in intel_link_port.c and | 5 | 0 | yes | NEW |
| `display/clock.c` (Zlib+MIT 文) | moved | `parity/lcd/parity_dpll_glue.inc` | 35 | - | - | 1 | 0 | - | NEW |
| `display/edid.c` (Zlib+MIT 文) | moved | `parity/lcd/parity_edid_mode_glue.inc` | 61 | - | - | 0 | 0 | - | NEW |
| `display/state.c` (Zlib) | moved | `parity/lcd/parity_lcd_calc.c` | 122 | - | - | 0 | 0 | - | NEW |
| `display/internal.h` (MIT+Zlib) | moved | `parity/lcd/parity_lcd_calc.h` | 60 | - | - | 0 | 0 | - | NEW |
| `dma.h` (Zlib) | moved | `parity/osdep/address_types.h` | 50 | - | - | 0 | 0 | - | yes |
| `dma.c` (Zlib) | moved | `parity/osdep/dma.c` | 271 | - | - | 0 | 0 | - | yes |
| `dma.h` (Zlib) | moved (renamed) | `parity/osdep/dma.h` | 175 | - | - | 0 | 0 | - | yes |
| `firmware.c` (Zlib) | moved | `parity/osdep/firmware.c` | 68 | - | - | 0 | 0 | - | yes |
| `firmware.h` (Zlib)<br>`firmware.c` (Zlib) | moved | `parity/osdep/firmware.h` | 34 | - | - | 0 | 0 | - | yes |
| `mmio.c` (Zlib) | moved | `parity/osdep/mmio.c` | 237 | - | - | 0 | 0 | - | yes |
| `mmio.h` (Zlib) | moved (renamed) | `parity/osdep/mmio.h` | 131 | - | - | 0 | 0 | - | yes |
| `pci.c` (Zlib) | moved | `parity/osdep/pci.c` | 287 | - | - | 0 | 0 | - | yes |
| `pci.h` (Zlib) | moved | `parity/osdep/pci.h` | 118 | - | - | 0 | 0 | - | yes |
| `runtime-pm.c` (Zlib) | moved | `parity/osdep/runtime_pm.c` | 123 | - | - | 0 | 0 | - | yes |
| `runtime-pm.h` (Zlib) | moved (renamed) | `parity/osdep/runtime_pm.h` | 72 | - | - | 0 | 0 | - | yes |
| `workqueue.c` (Zlib)<br>`sync.c` (Zlib) | moved | `parity/osdep/sync.c` | 227 | - | - | 0 | 0 | - | yes |
| `sync.h` (Zlib)<br>`workqueue.h` (Zlib) | moved; test part → S5 T2 | `parity/osdep/sync.h` | 106 | - | - | 0 | 0 | - | yes |
| `trace.c` (Zlib) | moved | `parity/osdep/trace.c` | 98 | - | - | 0 | 0 | - | yes |
| `trace.h` (Zlib) | moved | `parity/osdep/trace.h` | 74 | - | - | 0 | 0 | - | yes |
| `device.c` (Zlib) | moved (replaced) | `parity/parity.h` | 48 | - | - | 0 | 0 | - | yes |
| `display/display.c` (Zlib) | moved | `parity/pch.c` | 270 | - | - | 2 | 0 | - | yes |
| `display/internal.h` (MIT+Zlib) | moved | `parity/pch.h` | 87 | - | - | 1 | 0 | - | yes |
| `power.c` (Zlib) | moved | `parity/pcode.c` | 197 | - | - | 2 | 0 | - | yes |
| `power.h` (Zlib) | moved (declarations) | `parity/pcode.h` | 42 | - | - | 1 | 0 | - | yes |
| `display/power.c` (MIT+Zlib) | moved | `parity/power_domains.c` | 1032 | - | - | 3 | 0 | - | yes |
| `display/internal.h` (MIT+Zlib) | moved | `parity/power_domains.h` | 259 | - | - | 0 | 0 | - | yes |
| `device.c` (Zlib)<br>`display/takeover.c` (MIT+Zlib) | moved | `parity/probe.c` | 2347 | - | - | 0 | 0 | - | yes |
| `ggtt.c` (Zlib) | moved | `parity/pte.c` | 45 | - | - | 0 | 0 | - | yes |
| `ggtt.h` (Zlib) | moved | `parity/pte.h` | 36 | - | - | 0 | 0 | - | yes |
| `pxp.c` (Zlib) | moved | `parity/pxp.c` | 91 | - | - | 0 | 0 | - | yes |
| `pxp.h` (Zlib) | moved | `parity/pxp.h` | 59 | - | - | 1 | 0 | - | yes |
| `reset.c` (Zlib) | moved | `parity/reset.c` | 88 | - | - | 0 | 0 | - | yes |
| `reset.h` (Zlib) | moved (declarations) | `parity/reset.h` | 28 | - | - | 0 | 0 | - | yes |
| `device.c` (Zlib) | moved | `parity/runner.c` | 195 | - | - | 0 | 0 | - | yes |
| `device.h` (Zlib) | moved (declarations) | `parity/runner.h` | 20 | - | - | 0 | 0 | - | yes |
| `tests/contracts/dma_contract_test.c` (Zlib) | test-only (S5 T2) | `parity/tests/dma_contract_test.c` | 178 | - | - | 0 | 0 | - | yes |
| `tests/contracts/mmio_contract_test.c` (Zlib) | test-only (S5 T2) | `parity/tests/mmio_contract_test.c` | 135 | - | - | 0 | 0 | - | yes |
| `tests/contracts/mock_dma.c` (Zlib) | test-only (S5 T2) | `parity/tests/mock_dma.c` | 189 | - | - | 0 | 0 | - | yes |
| `tests/contracts/mock_dma.h` (Zlib) | test-only (S5 T2) | `parity/tests/mock_dma.h` | 26 | - | - | 0 | 0 | - | yes |
| `tests/contracts/mock_mmio.c` (Zlib) | test-only (S5 T2) | `parity/tests/mock_mmio.c` | 131 | - | - | 0 | 0 | - | yes |
| `tests/contracts/mock_mmio.h` (Zlib) | test-only (S5 T2) | `parity/tests/mock_mmio.h` | 28 | - | - | 0 | 0 | - | yes |
| `tests/contracts/mock_pci.c` (Zlib) | test-only (S5 T2) | `parity/tests/mock_pci.c` | 208 | - | - | 0 | 0 | - | yes |
| `tests/contracts/mock_pci.h` (Zlib) | test-only (S5 T2) | `parity/tests/mock_pci.h` | 29 | - | - | 0 | 0 | - | yes |
| `tests/contracts/pci_contract_test.c` (Zlib) | test-only (S5 T2) | `parity/tests/pci_contract_test.c` | 154 | - | - | 0 | 0 | - | yes |
| `tests/contracts/pte_contract_test.c` (Zlib) | test-only (S5 T2) | `parity/tests/pte_contract_test.c` | 86 | - | - | 0 | 0 | - | yes |
| `tests/contracts/rpm_contract_test.c` (Zlib) | test-only (S5 T2) | `parity/tests/rpm_contract_test.c` | 95 | - | - | 0 | 0 | - | yes |
| `tests/contracts/sync_contract_test.c` (Zlib) | test-only (S5 T2) | `parity/tests/sync_contract_test.c` | 229 | - | - | 0 | 0 | - | yes |
| `sync.c` (Zlib) | moved | `parity/timer_calc.c` | 62 | - | - | 0 | 0 | - | yes |
| `sync.h` (Zlib) | test-only (S5 T4a) | `parity/timer_calc.h` | 48 | - | - | 0 | 0 | - | yes |
| `intel/vbt.h` (MIT 文) | moved | `parity/vbt/intel_bios.h` | 292 | - | Copyright © 2016-2019 Intel Corporation | 3 | 0 | - | yes |
| `display/vbt.c` (Zlib+MIT 文) | moved | `parity/vbt/intel_bios_port.c` | 2615 | - | Copyright © 2006 Intel Corporation | 3 | 0 | yes | yes |
| `intel/vbt-defs.h` (MIT 文) | moved | `parity/vbt/intel_vbt_defs.h` | 1070 | - | Copyright © 2006-2016 Intel Corporation | 7 | 0 | - | yes |
| `display/vbt.h` (MIT+Zlib+MIT 文) | moved | `parity/vbt/parity_vbt.h` | 106 | - | - | 1 | 0 | - | yes |
| `display/vbt.c` (Zlib+MIT 文)<br>`display/edid.c` (Zlib+MIT 文) | moved | `parity/vbt/parity_vbt_glue.inc` | 274 | - | - | 1 | 0 | - | yes |
| `display/vbt.h` (MIT+Zlib+MIT 文)<br>`display/takeover.c` (MIT+Zlib)<br>`display/vbt.c` (Zlib+MIT 文) | moved | `parity/vbt/vbt_compat.h` | 314 | - | - | 8 | 0 | yes | yes |
| `intel/vbt.h` (MIT 文) | moved | `parity/vbt/vbt_ref_types.h` | 216 | - | Copyright Intel Corporation; see the notice in intel_bios_port.c) by | 8 | 0 | yes | yes |
| `display/takeover.c` (MIT+Zlib) | moved | `parity/vga.c` | 222 | - | - | 0 | 0 | - | yes |
| `display/internal.h` (MIT+Zlib) | moved | `parity/vga.h` | 79 | - | - | 1 | 0 | - | yes |
| `sync.c` (Zlib) | moved | `parity/wait.c` | 265 | - | - | 0 | 0 | - | yes |
| `sync.h` (Zlib) | moved; test part → S5 T4a | `parity/wait.h` | 56 | - | - | 0 | 0 | - | yes |
| `ppgtt.c` (Zlib) | moved | `ppgtt.c` | 376 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `request-queue.c` (Zlib) | moved; 5 unreachable functions retired (plan §5.1) | `request.c` | 501 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| — | retired (legacy HW test; S5 T4a) | `selftest.c` | 2535 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 3 | - | yes |
| `tests/fixtures/tex-fixture-gen.inc` (MIT) | test-only (S5 T4a) | `tex_fixture_gen.inc` | 100 | MIT | - | 0 | 7 | yes | yes |
| — | retired (plan §5: not reachable in production) | `uncore.c` | 394 | Zlib | Copyright (C) 2026 Awe Morris | 2 | 0 | - | yes |
| `render/codec.c` (Zlib)<br>`render/object.c` (Zlib)<br>`render/dispatch.c` (Zlib)<br>`render/transport.c` (Zlib)<br>`render/fence.c` (Zlib) | moved | `vk/cmd.c` | 479 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `render/codec.h` (Zlib)<br>`render/object.h` (Zlib)<br>`render/dispatch.h` (Zlib)<br>`render/transport.h` (Zlib) | moved (declarations) | `vk/cmd.h` | 136 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| — | retired (plan §5.1: not reachable from libvulkan) | `vk/cmdbuf.c` | 853 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| — | retired (plan §5.1: not reachable from libvulkan) | `vk/cmdbuf.h` | 92 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `compiler/compile.c` (Zlib) | moved | `vk/compile.c` | 257 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `compiler/compiler.h` (Zlib) | moved | `vk/compile.h` | 53 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| — | retired (plan §5.1: not reachable from libvulkan) | `vk/display.c` | 69 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| — | retired (plan §5.1: not reachable from libvulkan) | `vk/display.h` | 45 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `compiler/eu.c` (Zlib) | moved | `vk/eu.c` | 420 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 2 | - | yes |
| `compiler/eu.h` (Zlib) | moved | `vk/eu.h` | 142 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `intel/genxml.h` (MIT 文) | moved | `vk/linux/3dstate-gen12.inc` | 310 | - | - | 2 | 15 | - | yes |
| `intel/eu-encoding-gen12.h` (MIT) | moved | `vk/linux/eu-encoding-gen12.inc` | 125 | - | - | 0 | 4 | - | yes |
| — | retired (plan §5.1: not reachable from libvulkan); the one macro production used, `GEN12_SURFACE_ALIGN_4`, is in `intel/genxml.h` | `vk/linux/surface-state-gen12.inc` | 57 | - | - | 0 | 3 | - | yes |
| — | retired (plan §5.1: not reachable from libvulkan) | `vk/pipe.c` | 695 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| — | retired (plan §5.1: not reachable from libvulkan) | `vk/pipe.h` | 63 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| — | retired (plan §5.1: not reachable from libvulkan) | `vk/res.c` | 898 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 2 | - | yes |
| — | retired (plan §5.1: not reachable from libvulkan) | `vk/res.h` | 190 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `compiler/spirv.c` (Zlib) | moved | `vk/spirv.c` | 610 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `compiler/ir.h` (Zlib) | moved (renamed) | `vk/spirv.h` | 112 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `render/fence.c` (Zlib) | partial: fence opcodes 35–38 moved; the rest retired (plan §5.1) | `vk/sync.c` | 411 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `render/fence.c` (Zlib) | partial: fence opcodes 35–38 moved; the rest retired (plan §5.1) | `vk/sync.h` | 81 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `render/internal.h` (Zlib)<br>`render/gfx.h` (Zlib)<br>`compiler/ir.h` (Zlib) | moved (renamed) | `vk/vk-internal.h` | 160 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `render/vulkan.c` (Zlib) | moved | `vk/vk.c` | 194 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `render/render.h` (Zlib) | moved (declarations) | `vk/vk.h` | 52 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| — | retired (plan §5.1: not reachable from libvulkan) | `vk/wsi.c` | 173 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| — | retired (plan §5.1: not reachable from libvulkan) | `vk/wsi.h` | 51 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |

初回棚卸しの集計（旧ファイル）: 196 files, 68762 lines. SPDX tag present: 47. Copyright line present: 59. Marked generated: 23.

## 2. 上流の表示を持つ新ファイルで §1 に現れないもの

2026-09-22 に新ツリー（`src/drivers/gpu/i915/`、`tests/` を含む）を走査した事実。多くは初回棚卸しの後に旧ツリーへ加わった
表示系（`parity/lcd/` の生成物など）の移動先。`display/*.c` は zedBSD の Zlib 表示の後に、由来する Linux ファイルの表示を
そのまま保持している（S4 で復元、[s4-reports](i915-rebuild-s4-reports.md)）。`intel/` の行は 2026-09-22 の再構成の後に走査した事実（`i915.h` を分割してできた `gt-regs.h`・`commands.h`・`lrc-offsets.h`・`pci-ids.h`・`gt-power.h`・`workarounds.h`・`mocs.h`・`genxml.h` は `intel/` への再構成の後に走査。上流の表示を持たない `intel/bits.h` は載せない）（`third-party copyright lines` は zedBSD の行を除いた Copyright 行の数と最初の一行）。

| file（`src/drivers/gpu/i915/` 相対） | status | lines | notice | third-party copyright lines in header | linux refs | mesa refs | generated |
|---|---|---|---|---|---|---|---|
| `intel/clock.h` | production | 224 | zedBSD copyright line + MIT permission text | 2 lines, first: Copyright © 2006-2017 Intel Corporation | 14 | 0 | - |
| `intel/commands.h` | production | 265 | zedBSD copyright line + SPDX MIT | 2 lines, first: Copyright © 2003-2018 Intel Corporation | 142 | 0 | yes |
| `intel/connector.h` | production | 113 | zedBSD copyright line + DRM/X11 permission text ("Permission to use, copy, modify, distribute, and sell") | 1 lines, first: Copyright (c) 2016 Intel Corporation | 0 | 0 | - |
| `intel/ddi.h` | production | 112 | zedBSD copyright line + MIT permission text | 1 lines, first: Copyright © 2012 Intel Corporation | 8 | 0 | - |
| `intel/dp.h` | production | 2203 | zedBSD copyright line + SPDX MIT; MIT permission text; DRM/X11 permission text ("Permission to use, copy, modify, distribute, and sell") | 7 lines, first: Copyright © 2008 Keith Packard | 45 | 0 | - |
| `intel/engine-table.inc` | production | 60 | zedBSD copyright line + MIT permission text | 1 lines, first: Copyright © 2016 Intel Corporation | 3 | 0 | - |
| `intel/eu-encoding-gen12.h` | production | 173 | zedBSD copyright line + SPDX MIT | 1 lines, first: Copyright (C) 2025 Intel Corporation | 0 | 4 | - |
| `intel/fixed.h` | production | 163 | zedBSD copyright line + SPDX MIT | 1 lines, first: Copyright © 2018 Intel Corporation | 3 | 0 | - |
| `intel/forcewake-ranges.inc` | production | 83 | zedBSD copyright line + MIT permission text | 1 lines, first: Copyright © 2013 Intel Corporation | 1 | 0 | - |
| `intel/fourcc.h` | production | 1585 | zedBSD copyright line + MIT permission text | 1 lines, first: Copyright 2011 Intel Corporation | 0 | 0 | - |
| `intel/genxml.h` | production | 365 | zedBSD copyright line + MIT permission text | 2 lines, first: Copyright (C) 2016 Intel Corporation | 4 | 21 | - |
| `intel/gt-power.h` | production | 105 | zedBSD copyright line + MIT permission text | 4 lines, first: Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas. | 16 | 0 | - |
| `intel/gt-regs.h` | production | 644 | zedBSD copyright line + MIT permission text | 8 lines, first: Copyright © 2019 Intel Corporation | 374 | 0 | yes |
| `intel/hotplug-types.h` | production | 80 | zedBSD copyright line + SPDX MIT | 1 lines, first: Copyright © 2022 Intel Corporation | 3 | 0 | - |
| `intel/hotplug.h` | production | 207 | zedBSD copyright line + SPDX MIT; MIT permission text | 6 lines, first: Copyright © 2022 Intel Corporation | 25 | 0 | - |
| `intel/lrc-offsets.h` | production | 181 | zedBSD copyright line + SPDX MIT | 1 lines, first: Copyright © 2014 Intel Corporation | 10 | 0 | yes |
| `intel/mcr-ranges.inc` | production | 49 | zedBSD copyright line + MIT permission text | 1 lines, first: Copyright © 2014-2018 Intel Corporation | 3 | 0 | - |
| `intel/mocs-table.inc` | production | 109 | zedBSD copyright line + MIT permission text | 1 lines, first: Copyright © 2015 Intel Corporation | 3 | 0 | - |
| `intel/mocs.h` | production | 124 | zedBSD copyright line + SPDX MIT | 3 lines, first: Copyright © 2022 Intel Corporation | 29 | 0 | yes |
| `intel/mreg.h` | production | 656 | zedBSD copyright line + SPDX MIT; MIT permission text | 7 lines, first: Copyright © 2022 Intel Corporation | 55 | 0 | - |
| `intel/opregion.h` | production | 351 | zedBSD copyright line + SPDX MIT; MIT permission text | 4 lines, first: Copyright © 2008-2017 Intel Corporation | 17 | 0 | - |
| `intel/pci-ids.h` | production | 105 | zedBSD copyright line + MIT permission text | 1 lines, first: Copyright 2013 Intel Corporation | 0 | 0 | yes |
| `intel/phy.h` | production | 308 | zedBSD copyright line + SPDX MIT | 1 lines, first: Copyright © 2020 Intel Corporation | 13 | 0 | - |
| `intel/plane.h` | production | 523 | zedBSD copyright line + MIT permission text | 3 lines, first: Copyright © 2006-2019 Intel Corporation | 13 | 0 | - |
| `intel/power-set.h` | production | 42 | zedBSD copyright line + SPDX MIT | 1 lines, first: Copyright © 2019 Intel Corporation | 3 | 0 | - |
| `intel/power.h` | production | 139 | zedBSD copyright line + SPDX MIT | 1 lines, first: Copyright © 2019 Intel Corporation | 9 | 0 | - |
| `intel/psr.h` | production | 70 | zedBSD copyright line + SPDX MIT | 1 lines, first: Copyright © 2023 Intel Corporation | 2 | 0 | - |
| `intel/ref-inlines.h` | production | 77 | zedBSD copyright line + MIT permission text | 1 lines, first: Copyright © 2006-2007 Intel Corporation | 6 | 0 | - |
| `intel/ref.h` | production | 548 | zedBSD copyright line + SPDX MIT; MIT permission text | 4 lines, first: Copyright © 2007-2008 Intel Corporation | 24 | 0 | - |
| `intel/trans.h` | production | 466 | zedBSD copyright line + MIT permission text | 2 lines, first: Copyright © 2006-2007 Intel Corporation | 13 | 0 | - |
| `intel/vbt-defs.h` | production | 1290 | zedBSD copyright line + MIT permission text | 2 lines, first: Copyright © 2006-2016 Intel Corporation | 18 | 0 | - |
| `intel/vbt.h` | production | 522 | zedBSD copyright line + MIT permission text | 2 lines, first: Copyright © 2016-2019 Intel Corporation | 15 | 0 | - |
| `intel/wm-dbuf.h` | production | 308 | zedBSD copyright line + SPDX MIT | 1 lines, first: Copyright © 2022 Intel Corporation | 5 | 0 | - |
| `intel/wm.h` | production | 124 | zedBSD copyright line + SPDX MIT; MIT permission text | 4 lines, first: Copyright © 2019 Intel Corporation | 15 | 0 | - |
| `intel/workarounds.h` | production | 101 | zedBSD copyright line + SPDX MIT | 2 lines, first: Copyright © 2014-2018 Intel Corporation | 7 | 0 | - |
| (deleted 2026-09-22: unused by the new driver; old `external/i915-superseded.h`) | not included | 386 | zedBSD copyright line + SPDX MIT; MIT permission text | 3 lines, first: Copyright © 2015 Intel Corporation | 23 | 5 | - |
| (deleted 2026-09-22: unused; old `external/display/mreg-reg-defs.h`) | not included | 32 | zedBSD copyright line + SPDX MIT | 1 lines, first: Copyright © 2022 Intel Corporation | 4 | 0 | - |
| `display/color.c` | production | 352 | Zlib (zedBSD) + MIT permission text | Copyright © 2016 Intel Corporation | 3 | 0 | - |
| `display/ddi.c` | production | 4236 | Zlib (zedBSD) + MIT permission text | Copyright © 2012 Intel Corporation | 4 | 0 | - |
| `display/gmbus.c` | production | 1030 | Zlib (zedBSD) + MIT permission text | Copyright (c) 2006 Dave Airlie <airlied@linux.ie> | 1 | 0 | - |
| `display/hdmi-mode.c` | production | 286 | Zlib (zedBSD) + MIT permission text | Copyright 2006 Dave Airlie <airlied@linux.ie> | 3 | 0 | - |
| `display/hdmi.c` | production | 539 | Zlib (zedBSD) + MIT permission text | Copyright 2006 Dave Airlie <airlied@linux.ie> | 1 | 0 | - |
| `display/hotplug-internal.h` | production | 1420 | Zlib (zedBSD) + MIT permission text | Copyright (c) 2006 Dave Airlie <airlied@linux.ie> | 11 | 0 | - |
| `display/opregion.c` | production | 2591 | Zlib (zedBSD) + MIT permission text | Copyright 2008 Intel Corporation <hong.liu@intel.com> | 6 | 0 | - |
| `display/panel-backlight.c` | production | 1309 | Zlib (zedBSD) + SPDX MIT | Copyright © 2021 Intel Corporation | 3 | 0 | - |
| `display/plane.c` | production | 1632 | Zlib (zedBSD) + SPDX MIT; MIT permission text | Copyright © 2014 Intel Corporation | 5 | 0 | - |
| `display/takeover-internal.h` | production | 744 | Zlib (zedBSD) + SPDX MIT; MIT permission text | Copyright © 2019 Intel Corporation | 9 | 0 | - |
| `display/vblank.c` | production | 826 | Zlib (zedBSD) + SPDX MIT | Copyright © 2022-2023 Intel Corporation | 3 | 0 | - |
| `tests/fixtures/tex-fixture-fhd-gen.inc` | test-only | 115 | SPDX MIT | - | 0 | 7 | yes |
| `tests/fixtures/vkref-generated.inc` | test-only | 107 | SPDX MIT | - | 0 | 3 | yes |

## 3. 解決済み: GPL-2.0 由来のコード（2026-09-22）

- 旧 `data/display-acpi-display.inc`（先頭が `SPDX-License-Identifier: GPL-2.0`、Linux v6.8.12 `intel_acpi.c` の
  `ACPI_DISPLAY_*` など 18 個の #define）は、統合担当が `display/opregion.c` 自身の `I915_ACPI_DISPLAY_*` 定義に置き換えて削除した。
- 同じ `intel_acpi.c` から書き直していた `display/opregion.c` の 2 関数（`drv_i915_acpi_device_id_update()`、`i915_acpi_display_type()`）も
  統合担当が書き直し、ファイル冒頭の GPL-2.0 の表示はなくなった（冒頭は zedBSD の Zlib と `intel_opregion.c` の MIT 表示）。
- その結果、新ツリー（`src/drivers/gpu/i915/`、`intel/` と `tests/` を含む）に GPL 由来のコードは残っていない。
  `GPL` の語が残るのは `display/internal.h` の注記（GPL-2.0 の Linux ファイルからは写していない、という記述）だけ。
- 旧ツリー（`i915-old/parity/lcd/intel_acpi_port.c`、生成物）は専門家レビュー用に残しており、build 外。

新ツリー全体の集計（2026-09-22、`intel/` 再構成の後、`license_inventory.py`）: 309 files, 192590 lines. SPDX tag present: 291. Copyright line present: 305. Marked generated: 15.

## 4. Intel display DMC firmware（`LICENSE.i915`、2026-09-22）

- 対象: `i915/adlp_dmc.bin`（ADL-P DMC v2.20、79088 bytes、sha256 `3516de2e134ddcf3b319c75d2e437779fecbd58cbb77234bd6f297c544e92ccb`）と
  `i915/tgl_dmc_ver2_12.bin`（TGL DMC v2.12、19760 bytes、sha256 `3c013ef0ad96ba73aee8e5bd04a8e27cc9b1c6e9183b1a83ce124485f325afca`）。
  いずれも Intel の binary firmware で、licence は linux-firmware の `LICENSE.i915`（2080 bytes、sha256 `8542aeabf2761935122d693561e16766ce1bcc2b0d003204f9040b7d6d929f2e`）:
  改変しない binary のみ再配布可、著作権表示と免責の添付が必要、reverse engineering 等は禁止。
- 配布の置き場: 任意 package `i915-firmware`（`userland/firmware/i915/`、既定 off）だけ。linux-firmware tag `20260410`
  （commit `dc85ccedc9c973682fbcf4d628ca61174bcc3120`）から取得し、4 file（DMC 2 個・`LICENSE.i915`・`WHENCE`）の大きさと sha256 を
  検証してから `/lib/firmware/i915/`、`/usr/share/licenses/i915-firmware/`、`/usr/share/zedbsd/packages/i915-firmware.manifest` に入れる。
  source tree と kernel には置かない（driver は `/lib/firmware/i915/` から読む）。
- 取得物は、かつて `intel/firmware/adlp-dmc.c`・`tgl-dmc.c` に埋め込まれていた参照 blob と byte 単位で一致する（2026-09-22 確認）。
- **2026-09-22: 埋め込み配列は kernel から削除した**（ユーザー決定）。`firmware.c` の `drv_i915_firmware_request()` は
  `/lib/firmware/<name>` を VFS から読み、package が無ければ ENOENT で、表示は DMC なしで動く（Linux と同じ）。kernel には
  `LICENSE.i915` の対象物が入らない。`plan/ws031/tests/run-i915-firmware-package-test.sh` は、`src/drivers/gpu/i915` に DMC 配列が
  無いことを検査する（旧 i915-old は build 外）。

## 5. 対象機の VBT（試験 build だけ、2026-09-22）

- 対象: `vendor/intel-vbt/dell-latitude-5330-1028-0b02.inc`。Dell Latitude 5330（PCI subsystem 1028:0b02）の OpRegion から採取した
  VBT（8704 bytes、sha256 `3bff4a0920d55c9aee0ea3c678904f982f8671c0e7b5bc97a335e429b29624cd`）の配列本体。Dell／Intel の platform data。
- licence: **配布 licence は未監査**。
- 使い方: 試験 build（`make ... I915_TEST_VBT=y`、`-DI915_TEST_VBT=1`）だけが `display/vbt.c` に include し、PCI subsystem と
  sha256 が一致したときだけ使う。QEMU passthrough の guest（OVMF）は ASLS=0 で OpRegion が無いための仮の支え。
  本番 kernel には入らず、VBT は OpRegion／PCI ROM か Linux の既定値から取る。`plan/ws031/tests/vkloop-hw.sh` は常に `I915_TEST_VBT=y`。
- `XXX:` 実機で GPU 試験が走るようになったら `vendor/intel-vbt/` と `I915_TEST_VBT` の経路を削除する。
- Dell Latitude 5320 の VBT（旧 `intel/firmware/vbt-dell-latitude-5320.c`）は試験に使われておらず、削除した。
