# WS031 licence inventory (first pass, facts only)

自動抽出した事実のみ。判断（区分・互換性・配布可否）は含まない。`linux refs`／`mesa refs` は本文中で上流のファイル名や用語に言及している箇所の数で、複製の有無を示すものではない。

**2026-09-22 更新（i915 再構築の後）**: 旧 `src/drivers/gpu/i915/` は `src/drivers/gpu/i915-old/` へ待避され（専門家レビュー用、build 外）、
新しい `src/drivers/gpu/i915/` が本番になった。§1 は初回棚卸しの 196 行をそのまま残し、各行の現在の置き場を先頭列に加えた
（対応は [被覆監査](i915-rebuild-coverage.md) §2 の「実際の行き先」。関数単位で分かれたものは全部、定義の移動は主な置き場だけを書いた。
監査の機械照合が gt 系の小さな static `fail()` を `display/dp-sink.c` の `i915_edp_fail()` に当てた 4 行は、その対応を除いた）。
§1 の `lines` 以降の列は**旧ファイルの**初回棚卸し時の事実で、再計算していない。新ファイルの表示は先頭列の括弧に書いた
（`Zlib`＝zedBSD の SPDX、`MIT`・`GPL-2.0`＝上流の SPDX 行、`MIT 文`＝MIT permission notice の本文、`DRM 文`＝DRM の
"Permission to use, copy, modify, distribute, and sell" 型の permission notice の本文、`表示なし`＝いずれもない）。
§2 は新ツリーで上流の表示を持ち §1 に現れないファイル、§3 はユーザー判断待ちの licence 問題。
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
| `data/i915-commands.inc` (MIT+MIT 文) | moved | `linux/i915-commands.inc` | 244 | MIT | Copyright © 2003-2018 Intel Corporation | 132 | 0 | yes | yes |
| `data/i915-ids.inc` (MIT+MIT 文) | moved | `linux/i915-ids.inc` | 82 | MIT | Copyright 2013 Intel Corporation | 0 | 0 | yes | yes |
| `data/i915-lrc-offsets.inc` (MIT+MIT 文) | moved | `linux/i915-lrc-offsets.inc` | 190 | MIT | Copyright © 2014 Intel Corporation | 8 | 0 | yes | yes |
| `data/i915-mocs.inc` (MIT+MIT 文) | moved | `linux/i915-mocs.inc` | 236 | MIT | Copyright © 2015 Intel Corporation | 10 | 0 | yes | yes |
| `data/i915-regs.inc` (MIT+MIT 文) | moved | `linux/i915-regs.inc` | 580 | MIT | Copyright © 2019 Intel Corporation; Copyright © 2022 Intel Corporation | 356 | 0 | - | yes |
| `data/i915-workarounds.inc` (Zlib) | moved | `linux/i915-workarounds.inc` | 138 | Zlib | Copyright (C) 2026 Awe Morris | 4 | 5 | - | yes |
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
| `data/display-dp-ref-types.inc` (MIT+MIT 文) | moved | `parity/dp/dp_ref_types.h` | 54 | - | Copyright Intel Corporation; the full | 3 | 0 | yes | yes |
| `data/display-drm-dp.inc` (DRM 文) | moved | `parity/dp/drm_dp.h` | 1746 | - | Copyright © 2008 Keith Packard | 1 | 0 | - | yes |
| `display/dp-sink.c` (Zlib+DRM 文) | moved | `parity/dp/drm_dp_helper_port.c` | 653 | - | Copyright © 2009 Keith Packard | 2 | 0 | yes | yes |
| `display/edid-read.c` (Zlib+MIT 文) | moved | `parity/dp/drm_edid_port.c` | 158 | - | Copyright (c) 2006 Luc Verhaegen (quirks list); Copyright (c) 2007-2008 Intel Corporation | 1 | 0 | yes | yes |
| `tests/display/edp-ktest.c` (Zlib) | test-only (S5 T4b) | `parity/dp/edp_ktest.c` | 174 | - | - | 0 | 0 | - | yes |
| `tests/display/edp-ktest.h` (Zlib) | test-only (S5 T4b) | `parity/dp/edp_ktest.h` | 11 | - | - | 0 | 0 | - | yes |
| `tests/display/edp-sync-ktest.c` (Zlib) | test-only (S5 T4b) | `parity/dp/edp_sync_ktest.c` | 312 | - | - | 0 | 0 | - | NEW |
| `data/display-intel-dp-aux.inc` (MIT) | moved | `parity/dp/intel_dp_aux.h` | 32 | MIT | Copyright © 2020-2021 Intel Corporation | 2 | 0 | - | yes |
| `display/aux.c` (Zlib+MIT 文) | moved | `parity/dp/intel_dp_aux_port.c` | 511 | MIT | Copyright © 2020-2021 Intel Corporation | 4 | 0 | yes | yes |
| `data/display-intel-dp-aux-regs.inc` (MIT) | moved | `parity/dp/intel_dp_aux_regs.h` | 103 | MIT | Copyright © 2023 Intel Corporation | 3 | 0 | - | yes |
| `data/display-intel-pps.inc` (MIT) | moved | `parity/dp/intel_pps.h` | 61 | MIT | Copyright © 2020 Intel Corporation | 3 | 0 | - | yes |
| `display/panel.c` (Zlib+MIT 文) | moved | `parity/dp/intel_pps_port.c` | 1289 | MIT | Copyright © 2020 Intel Corporation | 4 | 0 | yes | yes |
| `data/display-intel-pps-regs.inc` (MIT) | moved | `parity/dp/intel_pps_regs.h` | 85 | MIT | Copyright © 2023 Intel Corporation | 3 | 0 | - | yes |
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
| `data/firmware/adlp-dmc.c` (Zlib) | moved | `parity/firmware_adlp_dmc.c` | 4952 | - | - | 0 | 0 | yes | yes |
| `data/firmware/vbt-dell-latitude-5330.c` (Zlib) | moved | `parity/firmware_vbt_dell_latitude_5330.c` | 559 | - | - | 0 | 0 | yes | yes |
| `defaults.c` (Zlib)<br>`engine.c` (Zlib) | moved | `parity/gt_defaults.c` | 316 | - | - | 0 | 0 | - | yes |
| `defaults.h` (Zlib) | moved | `parity/gt_defaults.h` | 98 | - | - | 0 | 0 | - | yes |
| `submit.c` (Zlib)<br>`engine.c` (Zlib) | moved | `parity/gt_engine.c` | 337 | - | - | 0 | 0 | - | yes |
| `data/i915-regs.inc` (MIT+MIT 文) | moved | `parity/gt_engine.h` | 152 | - | - | 5 | 0 | - | yes |
| `data/forcewake-ranges.inc` (表示なし) | moved | `parity/gt_fw_ranges.inc` | 52 | - | - | 1 | 0 | yes | yes |
| `workarounds.h` (Zlib) | moved | `parity/gt_init.h` | 249 | - | - | 6 | 0 | - | yes |
| `workarounds.c` (Zlib)<br>`gt-power.c` (Zlib) | moved | `parity/gt_init_base.c` | 680 | - | - | 4 | 0 | - | yes |
| `context.c` (Zlib) | moved | `parity/gt_lrc.c` | 600 | - | - | 0 | 0 | - | yes |
| `context.c` (Zlib) | moved | `parity/gt_lrc.h` | 216 | - | - | 4 | 0 | - | yes |
| `data/i915-lrc-offsets.inc` (MIT+MIT 文) | moved (merged) | `parity/gt_lrc_offsets.inc` | 156 | - | - | 1 | 0 | yes | yes |
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
| `submit.h` (Zlib)<br>`submit.c` (Zlib)<br>`data/i915-execution.inc` (MIT+MIT 文) | moved | `parity/gt_submit.h` | 97 | - | - | 2 | 0 | - | yes |
| `verify-workarounds.c` (Zlib) | moved | `parity/gt_verify_wa.c` | 367 | - | - | 1 | 0 | - | yes |
| `verify-workarounds.c` (Zlib) | moved | `parity/gt_verify_wa.h` | 124 | - | - | 3 | 0 | - | yes |
| `workarounds.c` (Zlib) | moved | `parity/gt_wa_adlp.c` | 340 | - | - | 0 | 0 | - | yes |
| `display/interrupts.c` (Zlib)<br>`irq.c` (Zlib) | moved | `parity/irq.c` | 937 | - | - | 8 | 0 | - | yes |
| `irq.h` (Zlib) | moved | `parity/irq.h` | 164 | - | - | 1 | 0 | - | yes |
| `tests/execution/ktest.c` (Zlib) | test-only (S5 T4a) | `parity/ktest.c` | 5437 | - | - | 0 | 0 | - | yes |
| `tests/execution/ktest.h` (Zlib) | test-only (S5 T4a) | `parity/ktest.h` | 16 | - | - | 0 | 0 | - | yes |
| `display/edid.c` (Zlib+MIT 文) | moved | `parity/lcd/drm_edid_mode_port.c` | 215 | - | Copyright (c) 2006 Luc Verhaegen (quirks list); Copyright (c) 2007-2008 Intel Corporation | 1 | 0 | yes | NEW |
| `data/display-edid-ref-types.inc` (MIT 文) | moved | `parity/lcd/edid_ref_types.h` | 264 | - | Copyright © 2007-2008 Intel Corporation | 0 | 0 | yes | NEW |
| `display/clock.c` (Zlib+MIT 文) | moved | `parity/lcd/intel_dpll_port.c` | 174 | - | Copyright © 2006-2016 Intel Corporation | 2 | 0 | yes | NEW |
| `display/dp.c` (Zlib+MIT 文+DRM 文) | moved | `parity/lcd/intel_link_port.c` | 235 | - | Copyright © 2008 Intel Corporation; Copyright Intel Corporation): intel_reduce_m_n_ratio, compute_m_n, | 4 | 0 | yes | NEW |
| `display/modeset-internal.h` (MIT+Zlib+MIT 文) | moved | `parity/lcd/lcd_compat.h` | 137 | - | - | 2 | 0 | - | NEW |
| `data/display-ref-types.inc` (MIT+MIT 文) | moved | `parity/lcd/lcd_ref_types.h` | 82 | - | Copyright Intel Corporation; the full notices are kept in intel_link_port.c and | 5 | 0 | yes | NEW |
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
| `data/display-intel-bios.inc` (MIT 文) | moved | `parity/vbt/intel_bios.h` | 292 | - | Copyright © 2016-2019 Intel Corporation | 3 | 0 | - | yes |
| `display/vbt.c` (Zlib+MIT 文) | moved | `parity/vbt/intel_bios_port.c` | 2615 | - | Copyright © 2006 Intel Corporation | 3 | 0 | yes | yes |
| `data/display-intel-vbt-defs.inc` (MIT 文) | moved | `parity/vbt/intel_vbt_defs.h` | 1070 | - | Copyright © 2006-2016 Intel Corporation | 7 | 0 | - | yes |
| `display/vbt.h` (MIT+Zlib+MIT 文) | moved | `parity/vbt/parity_vbt.h` | 106 | - | - | 1 | 0 | - | yes |
| `display/vbt.c` (Zlib+MIT 文)<br>`display/edid.c` (Zlib+MIT 文) | moved | `parity/vbt/parity_vbt_glue.inc` | 274 | - | - | 1 | 0 | - | yes |
| `display/vbt.h` (MIT+Zlib+MIT 文)<br>`display/takeover.c` (MIT+Zlib)<br>`display/vbt.c` (Zlib+MIT 文) | moved | `parity/vbt/vbt_compat.h` | 314 | - | - | 8 | 0 | yes | yes |
| `data/display-vbt-ref-types.inc` (MIT+MIT 文) | moved | `parity/vbt/vbt_ref_types.h` | 216 | - | Copyright Intel Corporation; see the notice in intel_bios_port.c) by | 8 | 0 | yes | yes |
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
| `data/i915-3dstate-gen12.inc` (表示なし) | moved | `vk/linux/3dstate-gen12.inc` | 310 | - | - | 2 | 15 | - | yes |
| `data/eu-encoding-gen12.inc` (MIT) | moved | `vk/linux/eu-encoding-gen12.inc` | 125 | - | - | 0 | 4 | - | yes |
| — | retired (plan §5.1: not reachable from libvulkan); the one macro production used, `GEN12_SURFACE_ALIGN_4`, is in `data/i915-3dstate-gen12.inc` | `vk/linux/surface-state-gen12.inc` | 57 | - | - | 0 | 3 | - | yes |
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
そのまま保持している（S4 で復元、[s4-reports](i915-rebuild-s4-reports.md)）。

| file（`src/drivers/gpu/i915/` 相対） | status | lines | notice | third-party copyright lines in header | linux refs | mesa refs | generated |
|---|---|---|---|---|---|---|---|
| `data/display-acpi-display.inc` | production | 41 | SPDX GPL-2.0 **GPL-2.0 由来 — ユーザー判断待ち（§3）** | - | 4 | 0 | - |
| `data/display-buf-trans-types.inc` | production | 76 | SPDX MIT | Copyright © 2020 Intel Corporation | 2 | 0 | yes |
| `data/display-clock-tables.inc` | production | 215 | MIT permission text | Copyright © 2006-2017 Intel Corporation; Copyright © 2006-2016 Intel Corporation | 11 | 0 | - |
| `data/display-dbuf-slice-enum.inc` | production | 30 | SPDX MIT | Copyright © 2019 Intel Corporation | 2 | 0 | yes |
| `data/display-dbuf-types.inc` | production | 33 | SPDX MIT | Copyright © 2022 Intel Corporation | 2 | 0 | yes |
| `data/display-ddi-regs.inc` | production | 315 | SPDX MIT; MIT permission text | Copyright © 2012 Intel Corporation; Copyright Intel Corporation -- the full notice is kept in intel_ddi_port.c) by | 3 | 0 | yes |
| `data/display-ddi-types.inc` | production | 114 | SPDX MIT; MIT permission text | Copyright © 2012 Intel Corporation; Copyright Intel Corporation -- the full notices are kept in intel_ddi_port.c and | 6 | 0 | yes |
| `data/display-dp-helper-inlines.inc` | production | 59 | DRM/X11 permission text ("Permission to use, copy, modify, distribute, and sell") | Copyright © 2008 Keith Packard | 1 | 0 | yes |
| `data/display-dp-msa.inc` | production | 80 | DRM/X11 permission text ("Permission to use, copy, modify, distribute, and sell") | Copyright © 2008 Keith Packard | 1 | 0 | yes |
| `data/display-dp-phy-enum.inc` | production | 54 | DRM/X11 permission text ("Permission to use, copy, modify, distribute, and sell") | Copyright © 2008 Keith Packard | 1 | 0 | yes |
| `data/display-dpll-id-enum.inc` | production | 164 | MIT permission text | Copyright © 2012-2016 Intel Corporation | 2 | 0 | yes |
| `data/display-drm-colorspace.inc` | production | 64 | DRM/X11 permission text ("Permission to use, copy, modify, distribute, and sell") | Copyright (c) 2016 Intel Corporation | 0 | 0 | yes |
| `data/display-drm-fourcc.inc` | production | 1578 | MIT permission text | Copyright 2011 Intel Corporation | 0 | 0 | - |
| `data/display-drm-plane-defs.inc` | production | 86 | SPDX MIT; MIT permission text | Copyright © 2006-2019 Intel Corporation | 0 | 0 | yes |
| `data/display-hpd-drm-connector-status.inc` | production | 70 | DRM/X11 permission text ("Permission to use, copy, modify, distribute, and sell") | Copyright (c) 2016 Intel Corporation | 0 | 0 | yes |
| `data/display-hpd-for-each-pin.inc` | production | 44 | MIT permission text | Copyright © 2006-2019 Intel Corporation | 2 | 0 | yes |
| `data/display-hpd-hotplug-state.inc` | production | 48 | MIT permission text | Copyright (c) 2006 Dave Airlie <airlied@linux.ie>; Copyright (c) 2007-2008 Intel Corporation | 2 | 0 | yes |
| `data/display-hpd-hotplug-types.inc` | production | 75 | SPDX MIT | Copyright © 2022 Intel Corporation | 2 | 0 | yes |
| `data/display-hpd-mreg-drm-dp.inc` | production | 41 | DRM/X11 permission text ("Permission to use, copy, modify, distribute, and sell") | Copyright © 2008 Keith Packard | 1 | 0 | yes |
| `data/display-hpd-mreg-gmbus-pins.inc` | production | 25 | SPDX MIT | Copyright © 2019 Intel Corporation | 2 | 0 | yes |
| `data/display-hpd-mreg-gmbus.inc` | production | 52 | SPDX MIT | Copyright © 2022 Intel Corporation | 2 | 0 | yes |
| `data/display-hpd-mreg-i915-reg.inc` | production | 62 | MIT permission text | Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas. | 2 | 0 | yes |
| `data/display-hpd-pin-enum.inc` | production | 43 | SPDX MIT | Copyright © 2022 Intel Corporation | 2 | 0 | yes |
| `data/display-i915-colorkey.inc` | production | 54 | MIT permission text | Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas. | 1 | 0 | yes |
| `data/display-i915-fixed.inc` | production | 157 | SPDX MIT | Copyright © 2018 Intel Corporation | 2 | 0 | yes |
| `data/display-link-training-inlines.inc` | production | 27 | SPDX MIT | Copyright © 2019 Intel Corporation | 2 | 0 | yes |
| `data/display-mreg-backlight.inc` | production | 37 | SPDX MIT | Copyright © 2022 Intel Corporation | 2 | 0 | yes |
| `data/display-mreg-color.inc` | production | 40 | SPDX MIT | Copyright © 2023 Intel Corporation | 2 | 0 | yes |
| `data/display-mreg-combo-phy.inc` | production | 91 | SPDX MIT | Copyright © 2022 Intel Corporation | 2 | 0 | yes |
| `data/display-mreg-cx0.inc` | production | 34 | SPDX MIT | Copyright © 2023 Intel Corporation | 2 | 0 | yes |
| `data/display-mreg-display-device.inc` | production | 27 | SPDX MIT | Copyright © 2023 Intel Corporation | 2 | 0 | yes |
| `data/display-mreg-display-reg-defs.inc` | production | 27 | SPDX MIT | Copyright © 2022 Intel Corporation | 2 | 0 | yes |
| `data/display-mreg-display-types.inc` | production | 45 | MIT permission text | Copyright (c) 2006 Dave Airlie <airlied@linux.ie>; Copyright (c) 2007-2008 Intel Corporation | 2 | 0 | yes |
| `data/display-mreg-display.inc` | production | 47 | MIT permission text | Copyright © 2006-2019 Intel Corporation | 2 | 0 | yes |
| `data/display-mreg-dmc-c.inc` | production | 43 | MIT permission text | Copyright © 2014 Intel Corporation | 2 | 0 | yes |
| `data/display-mreg-dmc.inc` | production | 31 | SPDX MIT | Copyright © 2022 Intel Corporation | 2 | 0 | yes |
| `data/display-mreg-drm-dp.inc` | production | 152 | DRM/X11 permission text ("Permission to use, copy, modify, distribute, and sell") | Copyright © 2008 Keith Packard | 1 | 0 | yes |
| `data/display-mreg-hdmi-dip.inc` | production | 49 | MIT permission text | Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas. | 2 | 0 | yes |
| `data/display-mreg-i915-reg.inc` | production | 232 | MIT permission text | Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas. | 2 | 0 | yes |
| `data/display-mreg-link-training.inc` | production | 87 | MIT permission text | Copyright © 2008-2015 Intel Corporation | 2 | 0 | yes |
| `data/display-mreg-power.inc` | production | 29 | SPDX MIT | Copyright © 2019 Intel Corporation | 2 | 0 | yes |
| `data/display-mreg-reg-defs.inc` | production | 27 | SPDX MIT | Copyright © 2022 Intel Corporation | 2 | 0 | yes |
| `data/display-mreg-vdsc.inc` | production | 34 | SPDX MIT | Copyright © 2023 Intel Corporation | 2 | 0 | yes |
| `data/display-mreg-wm.inc` | production | 137 | SPDX MIT | Copyright © 2023 Intel Corporation | 2 | 0 | yes |
| `data/display-opreg-pci-config.inc` | production | 29 | SPDX MIT | Copyright © 2022 Intel Corporation | 2 | 0 | yes |
| `data/display-opreg-struct.inc` | production | 60 | MIT permission text | Copyright © 2008-2017 Intel Corporation | 2 | 0 | yes |
| `data/display-opregion-mailbox.inc` | production | 269 | MIT permission text | Copyright 2008 Intel Corporation <hong.liu@intel.com>; Copyright 2008 Red Hat <mjg@redhat.com> | 4 | 0 | - |
| `data/display-pch-enum.inc` | production | 41 | SPDX MIT | Copyright 2019 Intel Corporation. | 2 | 0 | yes |
| `data/display-phy-buf-trans.inc` | production | 232 | SPDX MIT | Copyright © 2020 Intel Corporation | 5 | 0 | - |
| `data/display-plane-regs.inc` | production | 402 | SPDX MIT; MIT permission text | Copyright © 2012 Intel Corporation; Copyright Intel Corporation -- the full notice is kept in | 3 | 0 | yes |
| `data/display-plane-types.inc` | production | 59 | SPDX MIT; MIT permission text | Copyright © 2006-2019 Intel Corporation; Copyright Intel Corporation -- full notice in skl_plane_port.c) | 1 | 0 | yes |
| `data/display-power-domain-enum.inc` | production | 116 | SPDX MIT | Copyright © 2019 Intel Corporation | 2 | 0 | yes |
| `data/display-power-domain-set-types.inc` | production | 37 | SPDX MIT | Copyright © 2019 Intel Corporation | 2 | 0 | yes |
| `data/display-psr-selfetch-regs.inc` | production | 67 | SPDX MIT | Copyright © 2023 Intel Corporation | 1 | 0 | yes |
| `data/display-ref-inlines.inc` | production | 76 | SPDX MIT; MIT permission text | Copyright © 2006-2007 Intel Corporation; Copyright Intel Corporation -- the full notice is kept in | 5 | 0 | yes |
| `data/display-trans-regs.inc` | production | 189 | SPDX MIT; MIT permission text | Copyright © 2006-2007 Intel Corporation; Copyright Intel Corporation -- the full notice is kept in | 4 | 0 | yes |
| `data/display-vbt-tables.inc` | production | 242 | MIT permission text | Copyright © 2006 Intel Corporation | 5 | 0 | - |
| `data/display-wm-dbuf-slices.inc` | production | 275 | SPDX MIT | Copyright © 2022 Intel Corporation | 3 | 0 | - |
| `data/display-wm-ddb-types.inc` | production | 40 | SPDX MIT | Copyright © 2021 Intel Corporation | 2 | 0 | yes |
| `data/display-wm-types.inc` | production | 67 | MIT permission text | Copyright (c) 2006 Dave Airlie <airlied@linux.ie>; Copyright (c) 2007-2008 Intel Corporation | 2 | 0 | yes |
| `data/engine-table.inc` | production | 50 | SPDX MIT; MIT permission text | Copyright © 2016 Intel Corporation | 2 | 0 | - |
| `data/i915-gt-mocs-table.inc` | production | 99 | SPDX MIT; MIT permission text | Copyright © 2015 Intel Corporation | 2 | 0 | - |
| `data/i915-gt-power.inc` | production | 88 | SPDX MIT; MIT permission text | Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.; Copyright © 2019 Intel Corporation | 12 | 0 | - |
| `data/i915-gt-workarounds.inc` | production | 165 | SPDX MIT; MIT permission text | Copyright © 2014-2018 Intel Corporation; Copyright © 2015 Intel Corporation | 15 | 0 | - |
| `data/i915-mcr-ranges.inc` | production | 38 | SPDX MIT; MIT permission text | Copyright © 2014-2018 Intel Corporation | 2 | 0 | - |
| `display/color.c` | production | 352 | Zlib (zedBSD) + MIT permission text | Copyright © 2016 Intel Corporation | 3 | 0 | - |
| `display/ddi.c` | production | 4236 | Zlib (zedBSD) + MIT permission text | Copyright © 2012 Intel Corporation | 4 | 0 | - |
| `display/gmbus.c` | production | 1030 | Zlib (zedBSD) + MIT permission text | Copyright (c) 2006 Dave Airlie <airlied@linux.ie> | 1 | 0 | - |
| `display/hdmi-mode.c` | production | 286 | Zlib (zedBSD) + MIT permission text | Copyright 2006 Dave Airlie <airlied@linux.ie> | 3 | 0 | - |
| `display/hdmi.c` | production | 539 | Zlib (zedBSD) + MIT permission text | Copyright 2006 Dave Airlie <airlied@linux.ie> | 1 | 0 | - |
| `display/hotplug-internal.h` | production | 1420 | Zlib (zedBSD) + MIT permission text | Copyright (c) 2006 Dave Airlie <airlied@linux.ie> | 11 | 0 | - |
| `display/opregion.c` | production | 2591 | Zlib (zedBSD) + SPDX GPL-2.0; MIT permission text **GPL-2.0 由来 — ユーザー判断待ち（§3）** | Copyright 2008 Intel Corporation <hong.liu@intel.com> | 6 | 0 | - |
| `display/panel-backlight.c` | production | 1309 | Zlib (zedBSD) + SPDX MIT | Copyright © 2021 Intel Corporation | 3 | 0 | - |
| `display/plane.c` | production | 1632 | Zlib (zedBSD) + SPDX MIT; MIT permission text | Copyright © 2014 Intel Corporation | 5 | 0 | - |
| `display/takeover-internal.h` | production | 744 | Zlib (zedBSD) + SPDX MIT; MIT permission text | Copyright © 2019 Intel Corporation | 9 | 0 | - |
| `display/vblank.c` | production | 826 | Zlib (zedBSD) + SPDX MIT | Copyright © 2022-2023 Intel Corporation | 3 | 0 | - |
| `tests/fixtures/tex-fixture-fhd-gen.inc` | test-only | 115 | SPDX MIT | - | 0 | 7 | yes |
| `tests/fixtures/vkref-generated.inc` | test-only | 107 | SPDX MIT | - | 0 | 3 | yes |

## 3. 未決: GPL-2.0 由来のコード（ユーザー判断待ち）

- `src/drivers/gpu/i915/data/display-acpi-display.inc`: 先頭が `SPDX-License-Identifier: GPL-2.0`。Linux v6.8.12
  `drivers/gpu/drm/i915/display/intel_acpi.c`（sha256 `7abb35c4…c0d3`）の ACPI display id の定義（`ACPI_DISPLAY_*` など 18 個の #define）。
  `display/opregion.c` だけが include する。
- `src/drivers/gpu/i915/display/opregion.c` の ACPI 関数: `drv_i915_acpi_device_id_update()`（Linux `intel_acpi_device_id_update()`）と
  `i915_acpi_display_type()`（Linux `acpi_display_type()`）。同じ `intel_acpi.c` から関数ごとに書き直したもので、ファイル冒頭に
  GPL-2.0 の表示を保持している（ファイルの他の部分は MIT の `intel_opregion.c` 由来と zedBSD の Zlib）。
- 旧ツリーでも同じ状態だった（`i915-old/parity/lcd/intel_acpi_port.c`、生成物）。[s4-reports](i915-rebuild-s4-reports.md) L23 で
  ユーザー判断待ちとして報告済み。
- 選択肢の例（判断はユーザー）: GPL-2.0 部分を残して配布条件を記録する／ACPI 仕様（`_DOD` の device id 形式）から独立に書き直す／
  この機能を外す。

新ツリー全体の集計（2026-09-22、`license_inventory.py`）: 359 files, 193816 lines. SPDX tag present: 328. Copyright line present: 352. Marked generated: 71.
