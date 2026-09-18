# WS031 licence inventory (first pass, facts only)

自動抽出した事実のみ。判断（区分・互換性・配布可否）は含まない。`linux refs`／`mesa refs` は本文中で上流のファイル名や用語に言及している箇所の数で、複製の有無を示すものではない。

| file | lines | SPDX | copyright lines in header | linux refs | mesa refs | generated | tracked |
|---|---|---|---|---|---|---|---|
| `src/drivers/gpu/i915/draw_fixture.h` | 100 | - | - | 0 | 2 | - | yes |
| `src/drivers/gpu/i915/engine.c` | 548 | Zlib | Copyright (C) 2026 Awe Morris | 5 | 0 | - | yes |
| `src/drivers/gpu/i915/gem.c` | 269 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/ggtt.c` | 500 | Zlib | Copyright (C) 2026 Awe Morris | 1 | 0 | - | yes |
| `src/drivers/gpu/i915/i915.c` | 1983 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/internal.h` | 452 | Zlib | Copyright (C) 2026 Awe Morris | 1 | 0 | - | yes |
| `src/drivers/gpu/i915/irq.c` | 365 | Zlib | Copyright (C) 2026 Awe Morris | 2 | 0 | - | yes |
| `src/drivers/gpu/i915/linux/i915-commands.inc` | 244 | MIT | Copyright © 2003-2018 Intel Corporation | 132 | 0 | yes | yes |
| `src/drivers/gpu/i915/linux/i915-ids.inc` | 82 | MIT | Copyright 2013 Intel Corporation | 0 | 0 | yes | yes |
| `src/drivers/gpu/i915/linux/i915-lrc-offsets.inc` | 190 | MIT | Copyright © 2014 Intel Corporation | 8 | 0 | yes | yes |
| `src/drivers/gpu/i915/linux/i915-mocs.inc` | 236 | MIT | Copyright © 2015 Intel Corporation | 10 | 0 | yes | yes |
| `src/drivers/gpu/i915/linux/i915-regs.inc` | 580 | MIT | Copyright © 2019 Intel Corporation; Copyright © 2022 Intel Corporation | 356 | 0 | - | yes |
| `src/drivers/gpu/i915/linux/i915-workarounds.inc` | 138 | Zlib | Copyright (C) 2026 Awe Morris | 4 | 5 | - | yes |
| `src/drivers/gpu/i915/lrc.c` | 537 | Zlib | Copyright (C) 2026 Awe Morris | 2 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/backend.h` | 42 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/backend_dma.c` | 47 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/backend_mmio.c` | 159 | - | - | 0 | 0 | yes | yes |
| `src/drivers/gpu/i915/parity/backend_pci.c` | 139 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/backend_sync.c` | 264 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/backend_sync.h` | 81 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/bios.c` | 507 | - | - | 1 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/bios.h` | 96 | - | - | 1 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/cdclk.c` | 522 | - | - | 4 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/cdclk.h` | 101 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/combo_phy.c` | 202 | - | - | 1 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/combo_phy.h` | 40 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/display_core.c` | 397 | - | - | 1 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/display_core.h` | 60 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/display_nogem.c` | 1532 | - | - | 7 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/display_nogem.h` | 357 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/display_state.c` | 861 | - | - | 9 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/display_state.h` | 293 | - | - | 1 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/dmc.c` | 629 | - | - | 3 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/dmc.h` | 142 | - | - | 1 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/dp/dp_compat.h` | 337 | - | - | 8 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/dp/dp_fake_hw.c` | 364 | - | - | 0 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/dp/dp_fake_hw.h` | 77 | - | - | 0 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/dp/dp_fixture_latitude5330.h` | 85 | - | - | 0 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/dp/dp_ref_types.h` | 54 | - | Copyright Intel Corporation; the full | 3 | 0 | yes | NEW |
| `src/drivers/gpu/i915/parity/dp/drm_dp.h` | 1746 | - | Copyright © 2008 Keith Packard | 1 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/dp/drm_dp_helper_port.c` | 653 | - | Copyright © 2009 Keith Packard | 2 | 0 | yes | NEW |
| `src/drivers/gpu/i915/parity/dp/drm_edid_port.c` | 158 | - | Copyright (c) 2006 Luc Verhaegen (quirks list); Copyright (c) 2007-2008 Intel Corporation | 1 | 0 | yes | NEW |
| `src/drivers/gpu/i915/parity/dp/edp_ktest.c` | 144 | - | - | 0 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/dp/edp_ktest.h` | 9 | - | - | 0 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/dp/intel_dp_aux.h` | 32 | MIT | Copyright © 2020-2021 Intel Corporation | 2 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/dp/intel_dp_aux_port.c` | 511 | MIT | Copyright © 2020-2021 Intel Corporation | 4 | 0 | yes | NEW |
| `src/drivers/gpu/i915/parity/dp/intel_dp_aux_regs.h` | 103 | MIT | Copyright © 2023 Intel Corporation | 3 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/dp/intel_pps.h` | 61 | MIT | Copyright © 2020 Intel Corporation | 3 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/dp/intel_pps_port.c` | 1289 | MIT | Copyright © 2020 Intel Corporation | 4 | 0 | yes | NEW |
| `src/drivers/gpu/i915/parity/dp/intel_pps_regs.h` | 85 | MIT | Copyright © 2023 Intel Corporation | 3 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/dp/parity_dp_aux_glue.inc` | 22 | - | - | 1 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/dp/parity_dp_kernel.c` | 244 | - | - | 0 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/dp/parity_dp_kernel.h` | 38 | - | - | 0 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/dp/parity_drm_dp_glue.inc` | 15 | - | - | 0 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/dp/parity_drm_edid_glue.inc` | 54 | - | - | 0 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/dp/parity_edp.c` | 329 | - | - | 1 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/dp/parity_edp.h` | 143 | - | - | 2 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/dram_bw.c` | 301 | - | - | 1 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/dram_bw.h` | 90 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/driver_probe.c` | 397 | - | - | 2 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/driver_probe.h` | 181 | - | - | 4 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/drm_device.c` | 195 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/drm_device.h` | 88 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/eu_test.c` | 1794 | - | - | 0 | 1 | - | yes |
| `src/drivers/gpu/i915/parity/eu_test.h` | 359 | - | - | 2 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/firmware_adlp_dmc.c` | 4952 | - | - | 0 | 0 | yes | yes |
| `src/drivers/gpu/i915/parity/firmware_vbt_dell_latitude_5330.c` | 559 | - | - | 0 | 0 | yes | NEW |
| `src/drivers/gpu/i915/parity/gt_defaults.c` | 316 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_defaults.h` | 98 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_engine.c` | 337 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_engine.h` | 152 | - | - | 5 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_fw_ranges.inc` | 52 | - | - | 1 | 0 | yes | yes |
| `src/drivers/gpu/i915/parity/gt_init.h` | 249 | - | - | 6 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_init_base.c` | 680 | - | - | 4 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_lrc.c` | 600 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_lrc.h` | 216 | - | - | 4 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_lrc_offsets.inc` | 156 | - | - | 1 | 0 | yes | yes |
| `src/drivers/gpu/i915/parity/gt_mem.c` | 781 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_mem.h` | 216 | - | - | 1 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_migrate.c` | 141 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_migrate.h` | 79 | - | - | 2 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_mmio.c` | 478 | - | - | 7 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_mmio.h` | 124 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_request.c` | 416 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_request.h` | 139 | - | - | 7 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_resume.c` | 163 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_resume.h` | 76 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_submit.c` | 300 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_submit.h` | 97 | - | - | 2 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_verify_wa.c` | 367 | - | - | 1 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_verify_wa.h` | 124 | - | - | 3 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/gt_wa_adlp.c` | 340 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/irq.c` | 937 | - | - | 8 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/irq.h` | 164 | - | - | 1 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/ktest.c` | 5328 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/ktest.h` | 16 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/osdep/address_types.h` | 50 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/osdep/dma.c` | 271 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/osdep/dma.h` | 175 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/osdep/firmware.c` | 68 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/osdep/firmware.h` | 34 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/osdep/mmio.c` | 237 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/osdep/mmio.h` | 131 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/osdep/pci.c` | 287 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/osdep/pci.h` | 118 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/osdep/runtime_pm.c` | 123 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/osdep/runtime_pm.h` | 72 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/osdep/sync.c` | 227 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/osdep/sync.h` | 106 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/osdep/trace.c` | 98 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/osdep/trace.h` | 74 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/parity.h` | 48 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/pch.c` | 270 | - | - | 2 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/pch.h` | 87 | - | - | 1 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/pcode.c` | 197 | - | - | 2 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/pcode.h` | 42 | - | - | 1 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/power_domains.c` | 753 | - | - | 2 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/power_domains.h` | 221 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/probe.c` | 2340 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/pte.c` | 45 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/pte.h` | 36 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/pxp.c` | 91 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/pxp.h` | 59 | - | - | 1 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/reset.c` | 88 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/reset.h` | 28 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/runner.c` | 195 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/runner.h` | 20 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/tests/dma_contract_test.c` | 178 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/tests/mmio_contract_test.c` | 135 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/tests/mock_dma.c` | 189 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/tests/mock_dma.h` | 26 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/tests/mock_mmio.c` | 131 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/tests/mock_mmio.h` | 28 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/tests/mock_pci.c` | 208 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/tests/mock_pci.h` | 29 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/tests/pci_contract_test.c` | 154 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/tests/pte_contract_test.c` | 86 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/tests/rpm_contract_test.c` | 95 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/tests/sync_contract_test.c` | 229 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/timer_calc.c` | 62 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/timer_calc.h` | 48 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/vbt/intel_bios.h` | 292 | - | Copyright © 2016-2019 Intel Corporation | 3 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/vbt/intel_bios_port.c` | 2615 | - | Copyright © 2006 Intel Corporation | 3 | 0 | yes | NEW |
| `src/drivers/gpu/i915/parity/vbt/intel_vbt_defs.h` | 1070 | - | Copyright © 2006-2016 Intel Corporation | 7 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/vbt/parity_vbt.h` | 106 | - | - | 1 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/vbt/parity_vbt_glue.inc` | 274 | - | - | 1 | 0 | - | NEW |
| `src/drivers/gpu/i915/parity/vbt/vbt_compat.h` | 314 | - | - | 8 | 0 | yes | NEW |
| `src/drivers/gpu/i915/parity/vbt/vbt_ref_types.h` | 216 | - | Copyright Intel Corporation; see the notice in intel_bios_port.c) by | 8 | 0 | yes | NEW |
| `src/drivers/gpu/i915/parity/vga.c` | 222 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/vga.h` | 79 | - | - | 1 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/wait.c` | 265 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/parity/wait.h` | 56 | - | - | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/ppgtt.c` | 376 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/request.c` | 501 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/selftest.c` | 2535 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 3 | - | yes |
| `src/drivers/gpu/i915/tex_fixture_gen.inc` | 100 | MIT | - | 0 | 7 | yes | yes |
| `src/drivers/gpu/i915/uncore.c` | 394 | Zlib | Copyright (C) 2026 Awe Morris | 2 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/cmd.c` | 479 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/cmd.h` | 136 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/cmdbuf.c` | 853 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/cmdbuf.h` | 92 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/compile.c` | 232 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/compile.h` | 53 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/display.c` | 69 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/display.h` | 45 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/eu.c` | 409 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 2 | - | yes |
| `src/drivers/gpu/i915/vk/eu.h` | 136 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/linux/3dstate-gen12.inc` | 310 | - | - | 2 | 15 | - | yes |
| `src/drivers/gpu/i915/vk/linux/eu-encoding-gen12.inc` | 121 | - | - | 0 | 3 | - | yes |
| `src/drivers/gpu/i915/vk/linux/surface-state-gen12.inc` | 57 | - | - | 0 | 3 | - | yes |
| `src/drivers/gpu/i915/vk/pipe.c` | 693 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/pipe.h` | 63 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/res.c` | 898 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 2 | - | yes |
| `src/drivers/gpu/i915/vk/res.h` | 190 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/spirv.c` | 523 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/spirv.h` | 92 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/sync.c` | 411 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/sync.h` | 81 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/vk-internal.h` | 160 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/vk.c` | 194 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/vk.h` | 52 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/wsi.c` | 173 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |
| `src/drivers/gpu/i915/vk/wsi.h` | 51 | Zlib | Copyright (C) 2026 Awe Morris | 0 | 0 | - | yes |

183 files, 65483 lines. SPDX tag present: 47. Copyright line present: 54. Marked generated: 18.
