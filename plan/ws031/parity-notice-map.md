# WS031 parity/ の出典対応表（自動抽出、事実のみ）

各 parity ファイルが本文中で名指ししている Linux i915 のファイル（固定参照 tree に実在するもののみ）と、その上流ファイルが持つ表示。`port 文言` は冒頭 4000 字に port/transcribe/generated 等の語があるか。**名指し＝複製の証明ではない**（API 契約の参照だけの場合もある）。区分の確定は人が行う。

| parity file | lines | port 文言 | 名指ししている上流ファイル（回数） |
|---|---|---|---|
| `parity/backend.h` | 42 | - | — |
| `parity/backend_dma.c` | 47 | - | — |
| `parity/backend_mmio.c` | 159 | yes | — |
| `parity/backend_pci.c` | 139 | - | — |
| `parity/backend_sync.c` | 264 | - | — |
| `parity/backend_sync.h` | 81 | - | — |
| `parity/bios.c` | 507 | yes | `display/intel_vbt_defs.h`×1 |
| `parity/bios.h` | 96 | yes | — |
| `parity/cdclk.c` | 522 | - | `display/intel_cdclk.c`×2, `i915_reg.h`×2 |
| `parity/cdclk.h` | 101 | yes | — |
| `parity/combo_phy.c` | 202 | yes | `display/intel_combo_phy_regs.h`×1 |
| `parity/combo_phy.h` | 40 | - | — |
| `parity/display_core.c` | 397 | yes | `i915_reg.h`×1 |
| `parity/display_core.h` | 60 | - | — |
| `parity/display_nogem.c` | 1532 | - | `display/intel_gmbus.h`×1, `display/skl_watermark.c`×1, `display/intel_dpll_mgr.c`×1, `display/intel_crtc.c`×1, `display/intel_display_wa.c`×1, `display/intel_cdclk.c`×1 |
| `parity/display_nogem.h` | 357 | - | — |
| `parity/display_state.c` | 861 | - | `display/skl_watermark.c`×2, `i915_reg.h`×1, `display/intel_global_state.c`×1, `display/intel_display_driver.c`×1, `display/intel_cdclk.c`×1, `display/intel_color.c`×1 |
| `parity/display_state.h` | 293 | - | `display/intel_global_state.h`×1 |
| `parity/dmc.c` | 629 | yes | `display/intel_dmc.c`×2, `display/intel_dmc_regs.h`×1 |
| `parity/dmc.h` | 142 | yes | `display/intel_dmc.c`×1 |
| `parity/dp/dp_compat.h` | 337 | - | `i915_reg_defs.h`×1, `i915_reg.h`×1, `i915_utils.h`×1, `display/intel_display_power.c`×1, `display/intel_pps.h`×1, `display/intel_dp_aux.h`×1 |
| `parity/dp/dp_fake_hw.c` | 364 | - | — |
| `parity/dp/dp_fake_hw.h` | 77 | - | — |
| `parity/dp/dp_fixture_latitude5330.h` | 85 | - | — |
| `parity/dp/dp_ref_types.h` | 54 | - | `display/intel_display_types.h`×2 |
| `parity/dp/drm_dp.h` | 1746 | - | — |
| `parity/dp/drm_dp_helper_port.c` | 653 | yes | — |
| `parity/dp/drm_edid_port.c` | 158 | yes | — |
| `parity/dp/edp_ktest.c` | 144 | - | — |
| `parity/dp/edp_ktest.h` | 9 | - | — |
| `parity/dp/intel_dp_aux.h` | 32 | - | `display/intel_dp_aux.h`×1 |
| `parity/dp/intel_dp_aux_port.c` | 511 | yes | `display/intel_dp_aux_regs.h`×2, `display/intel_dp_aux.c`×1 |
| `parity/dp/intel_dp_aux_regs.h` | 103 | - | `display/intel_dp_aux_regs.h`×1, `display/intel_display_reg_defs.h`×1 |
| `parity/dp/intel_pps.h` | 61 | - | `display/intel_pps.h`×1, `intel_wakeref.h`×1 |
| `parity/dp/intel_pps_port.c` | 1289 | yes | `display/intel_pps_regs.h`×2, `display/intel_pps.c`×1 |
| `parity/dp/intel_pps_regs.h` | 85 | - | `display/intel_pps_regs.h`×1, `display/intel_display_reg_defs.h`×1 |
| `parity/dp/parity_dp_aux_glue.inc` | 22 | - | — |
| `parity/dp/parity_dp_kernel.c` | 244 | - | — |
| `parity/dp/parity_dp_kernel.h` | 38 | - | — |
| `parity/dp/parity_drm_dp_glue.inc` | 15 | - | — |
| `parity/dp/parity_drm_edid_glue.inc` | 54 | - | — |
| `parity/dp/parity_edp.c` | 329 | - | `display/intel_pps_regs.h`×1 |
| `parity/dp/parity_edp.h` | 143 | - | — |
| `parity/dram_bw.c` | 301 | yes | `i915_reg.h`×1 |
| `parity/dram_bw.h` | 90 | - | — |
| `parity/driver_probe.c` | 397 | - | `display/intel_display_limits.h`×1, `i915_reg.h`×1 |
| `parity/driver_probe.h` | 181 | - | `i915_driver.c`×1, `display/intel_display_driver.c`×1, `display/intel_display_limits.h`×1, `i915_reg.h`×1 |
| `parity/drm_device.c` | 195 | yes | — |
| `parity/drm_device.h` | 88 | - | — |
| `parity/eu_test.c` | 1794 | - | — |
| `parity/eu_test.h` | 359 | - | `gem/i915_gem_execbuffer.c`×1, `gt/gen8_engine_cs.c`×1 |
| `parity/firmware_adlp_dmc.c` | 4952 | - | — |
| `parity/firmware_vbt_dell_latitude_5330.c` | 559 | - | — |
| `parity/gt_defaults.c` | 316 | - | — |
| `parity/gt_defaults.h` | 98 | - | — |
| `parity/gt_engine.c` | 337 | - | — |
| `parity/gt_engine.h` | 152 | - | `gt/intel_engine_cs.c`×1, `gt/intel_execlists_submission.c`×1, `gt/intel_engine_regs.h`×1, `gt/intel_engine.h`×1, `gt/intel_lrc.h`×1 |
| `parity/gt_fw_ranges.inc` | 52 | yes | `intel_uncore.c`×1 |
| `parity/gt_init.h` | 249 | yes | `gt/intel_workarounds.c`×2, `gt/intel_mocs.c`×1, `gt/intel_gt.c`×1, `gt/intel_rc6.c`×1, `gt/intel_rps.c`×1 |
| `parity/gt_init_base.c` | 680 | - | `gt/intel_workarounds.c`×1, `gt/intel_mocs.c`×1, `gt/intel_rc6.c`×1, `gt/intel_rps.c`×1 |
| `parity/gt_lrc.c` | 600 | - | — |
| `parity/gt_lrc.h` | 216 | - | `gt/intel_lrc.c`×1, `gt/intel_lrc_reg.h`×1, `gt/intel_gpu_commands.h`×1, `gt/intel_lrc.h`×1 |
| `parity/gt_lrc_offsets.inc` | 156 | yes | `gt/intel_lrc.c`×1 |
| `parity/gt_mem.c` | 781 | - | `gt/gen8_ppgtt.c`×1 |
| `parity/gt_mem.h` | 216 | yes | `gt/intel_gtt.h`×1 |
| `parity/gt_migrate.c` | 141 | - | — |
| `parity/gt_migrate.h` | 79 | - | `gt/intel_migrate.c`×1, `gt/intel_engine_cs.c`×1 |
| `parity/gt_mmio.c` | 478 | - | `gt/intel_engine_cs.c`×2, `i915_reg.h`×1, `gt/intel_gt_regs.h`×1, `gt/intel_gt_clock_utils.c`×1, `gt/intel_sseu.c`×1, `gt/intel_gt.c`×1 |
| `parity/gt_mmio.h` | 124 | - | — |
| `parity/gt_request.c` | 416 | - | — |
| `parity/gt_request.h` | 139 | yes | `gt/gen8_engine_cs.c`×1, `gt/intel_workarounds.c`×1, `gt/intel_execlists_submission.c`×1, `gt/intel_engine_types.h`×1, `gt/intel_lrc.h`×1, `gt/intel_engine.h`×1 |
| `parity/gt_resume.c` | 163 | - | — |
| `parity/gt_resume.h` | 76 | - | — |
| `parity/gt_submit.c` | 300 | - | — |
| `parity/gt_submit.h` | 97 | - | `gt/intel_execlists_submission.c`×2 |
| `parity/gt_verify_wa.c` | 367 | - | `gt/intel_workarounds.c`×1 |
| `parity/gt_verify_wa.h` | 124 | - | `gt/intel_gt.c`×1, `gt/intel_workarounds.c`×1, `gt/intel_gpu_commands.h`×1 |
| `parity/gt_wa_adlp.c` | 340 | yes | — |
| `parity/irq.c` | 937 | - | `gt/intel_gt_regs.h`×2, `gt/intel_gt_irq.c`×2, `display/intel_display_irq.c`×2, `i915_reg.h`×1, `i915_irq.c`×1 |
| `parity/irq.h` | 164 | - | `i915_irq.c`×1 |
| `parity/ktest.c` | 5328 | - | — |
| `parity/ktest.h` | 16 | - | — |
| `parity/osdep/address_types.h` | 50 | - | — |
| `parity/osdep/dma.c` | 271 | - | — |
| `parity/osdep/dma.h` | 175 | - | — |
| `parity/osdep/firmware.c` | 68 | - | — |
| `parity/osdep/firmware.h` | 34 | - | — |
| `parity/osdep/mmio.c` | 237 | - | — |
| `parity/osdep/mmio.h` | 131 | - | — |
| `parity/osdep/pci.c` | 287 | - | — |
| `parity/osdep/pci.h` | 118 | - | — |
| `parity/osdep/runtime_pm.c` | 123 | - | — |
| `parity/osdep/runtime_pm.h` | 72 | - | — |
| `parity/osdep/sync.c` | 227 | - | — |
| `parity/osdep/sync.h` | 106 | - | — |
| `parity/osdep/trace.c` | 98 | - | — |
| `parity/osdep/trace.h` | 74 | - | — |
| `parity/parity.h` | 48 | - | — |
| `parity/pch.c` | 270 | - | `soc/intel_pch.c`×1, `soc/intel_pch.h`×1 |
| `parity/pch.h` | 87 | - | `soc/intel_pch.c`×1 |
| `parity/pcode.c` | 197 | yes | `intel_pcode.c`×1, `i915_reg.h`×1 |
| `parity/pcode.h` | 42 | yes | `intel_pcode.c`×1 |
| `parity/power_domains.c` | 753 | - | `i915_reg.h`×2 |
| `parity/power_domains.h` | 221 | - | — |
| `parity/probe.c` | 2340 | - | — |
| `parity/pte.c` | 45 | - | — |
| `parity/pte.h` | 36 | - | — |
| `parity/pxp.c` | 91 | - | — |
| `parity/pxp.h` | 59 | - | `pxp/intel_pxp.c`×1 |
| `parity/reset.c` | 88 | yes | — |
| `parity/reset.h` | 28 | - | — |
| `parity/runner.c` | 195 | - | — |
| `parity/runner.h` | 20 | - | — |
| `parity/tests/dma_contract_test.c` | 178 | - | — |
| `parity/tests/mmio_contract_test.c` | 135 | yes | — |
| `parity/tests/mock_dma.c` | 189 | - | — |
| `parity/tests/mock_dma.h` | 26 | - | — |
| `parity/tests/mock_mmio.c` | 131 | - | — |
| `parity/tests/mock_mmio.h` | 28 | - | — |
| `parity/tests/mock_pci.c` | 208 | - | — |
| `parity/tests/mock_pci.h` | 29 | - | — |
| `parity/tests/pci_contract_test.c` | 154 | - | — |
| `parity/tests/pte_contract_test.c` | 86 | - | — |
| `parity/tests/rpm_contract_test.c` | 95 | - | — |
| `parity/tests/sync_contract_test.c` | 229 | - | — |
| `parity/timer_calc.c` | 62 | - | — |
| `parity/timer_calc.h` | 48 | - | — |
| `parity/vbt/intel_bios.h` | 292 | - | `display/intel_bios.h`×1, `display/intel_vbt_defs.h`×1 |
| `parity/vbt/intel_bios_port.c` | 2615 | yes | `display/intel_bios.c`×1, `display/intel_vbt_defs.h`×1 |
| `parity/vbt/intel_vbt_defs.h` | 1070 | - | `display/intel_vbt_defs.h`×2, `display/intel_bios.h`×2, `display/intel_bios.c`×1 |
| `parity/vbt/parity_vbt.h` | 106 | - | — |
| `parity/vbt/parity_vbt_glue.inc` | 274 | - | — |
| `parity/vbt/vbt_compat.h` | 314 | yes | `display/intel_bios.h`×2, `display/intel_gmbus.h`×1, `display/intel_display.c`×1, `display/intel_gmbus.c`×1, `display/intel_opregion.c`×1 |
| `parity/vbt/vbt_ref_types.h` | 216 | - | `display/intel_display.h`×2, `display/intel_display_types.h`×2, `display/intel_display_limits.h`×1, `soc/intel_pch.h`×1, `display/intel_display_core.h`×1 |
| `parity/vga.c` | 222 | - | — |
| `parity/vga.h` | 79 | - | `display/intel_vga.c`×1 |
| `parity/wait.c` | 265 | - | — |
| `parity/wait.h` | 56 | - | — |

## 上流ファイルの表示（名指しされたもの）

| 上流ファイル | 名指しする parity ファイル数 | license | copyright 行 |
|---|---|---|---|
| `i915_reg.h` | 11 | MIT (permission notice text, no SPDX tag) | Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas. |
| `display/intel_vbt_defs.h` | 5 | MIT (permission notice text, no SPDX tag) | Copyright © 2006-2016 Intel Corporation |
| `gt/intel_workarounds.c` | 5 | MIT | Copyright © 2014-2018 Intel Corporation |
| `display/intel_cdclk.c` | 3 | MIT (permission notice text, no SPDX tag) | Copyright © 2006-2017 Intel Corporation |
| `display/intel_pps_regs.h` | 3 | MIT | Copyright © 2023 Intel Corporation |
| `display/intel_display_limits.h` | 3 | MIT | Copyright © 2022 Intel Corporation |
| `gt/intel_engine_cs.c` | 3 | MIT | Copyright © 2016 Intel Corporation |
| `gt/intel_execlists_submission.c` | 3 | MIT | Copyright © 2014 Intel Corporation |
| `gt/intel_lrc.h` | 3 | MIT | Copyright © 2014 Intel Corporation |
| `gt/intel_gt.c` | 3 | MIT | Copyright © 2019 Intel Corporation |
| `gt/intel_gpu_commands.h` | 3 | MIT | Copyright © 2003-2018 Intel Corporation |
| `display/intel_bios.h` | 3 | MIT (permission notice text, no SPDX tag) | Copyright © 2016-2019 Intel Corporation |
| `display/intel_gmbus.h` | 2 | MIT | Copyright © 2019 Intel Corporation |
| `display/skl_watermark.c` | 2 | MIT | Copyright © 2022 Intel Corporation |
| `display/intel_gmbus.c` | 2 | MIT (permission notice text, no SPDX tag) | Copyright (c) 2006 Dave Airlie <airlied@linux.ie>; Copyright © 2006-2008,2010 Intel Corporation |
| `display/intel_display_driver.c` | 2 | MIT | Copyright © 2022-2023 Intel Corporation |
| `display/intel_dmc.c` | 2 | MIT (permission notice text, no SPDX tag) | Copyright © 2014 Intel Corporation |
| `display/intel_pps.h` | 2 | MIT | Copyright © 2020 Intel Corporation |
| `display/intel_dp_aux.h` | 2 | MIT | Copyright © 2020-2021 Intel Corporation |
| `display/intel_display_types.h` | 2 | MIT (permission notice text, no SPDX tag) | Copyright (c) 2006 Dave Airlie <airlied@linux.ie>; Copyright (c) 2007-2008 Intel Corporation |
| `display/intel_dp_aux_regs.h` | 2 | MIT | Copyright © 2023 Intel Corporation |
| `display/intel_display_reg_defs.h` | 2 | MIT | Copyright © 2022 Intel Corporation |
| `gt/gen8_engine_cs.c` | 2 | MIT | Copyright © 2014 Intel Corporation |
| `gt/intel_engine.h` | 2 | MIT | — |
| `gt/intel_mocs.c` | 2 | MIT | Copyright © 2015 Intel Corporation |
| `gt/intel_rc6.c` | 2 | MIT | Copyright © 2019 Intel Corporation |
| `gt/intel_rps.c` | 2 | MIT | Copyright © 2019 Intel Corporation |
| `gt/intel_lrc.c` | 2 | MIT | Copyright © 2014 Intel Corporation |
| `gt/intel_gt_regs.h` | 2 | MIT | Copyright © 2022 Intel Corporation |
| `i915_irq.c` | 2 | MIT (permission notice text, no SPDX tag) | Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas. |
| `soc/intel_pch.c` | 2 | MIT | Copyright 2019 Intel Corporation. |
| `soc/intel_pch.h` | 2 | MIT | Copyright 2019 Intel Corporation. |
| `intel_pcode.c` | 2 | MIT | Copyright © 2013-2021 Intel Corporation |
| `display/intel_bios.c` | 2 | MIT (permission notice text, no SPDX tag) | Copyright © 2006 Intel Corporation |
| `display/intel_combo_phy_regs.h` | 1 | MIT | Copyright © 2022 Intel Corporation |
| `display/intel_dpll_mgr.c` | 1 | MIT (permission notice text, no SPDX tag) | Copyright © 2006-2016 Intel Corporation |
| `display/intel_crtc.c` | 1 | MIT | Copyright © 2020 Intel Corporation |
| `display/intel_display_wa.c` | 1 | MIT | Copyright © 2023 Intel Corporation |
| `display/intel_global_state.c` | 1 | MIT | Copyright © 2020 Intel Corporation |
| `display/intel_color.c` | 1 | MIT (permission notice text, no SPDX tag) | Copyright © 2016 Intel Corporation |
| `display/intel_bw.c` | 1 | MIT | Copyright © 2019 Intel Corporation |
| `display/intel_pmdemand.c` | 1 | MIT | Copyright © 2023 Intel Corporation |
| `display/intel_quirks.c` | 1 | MIT | Copyright © 2018 Intel Corporation |
| `display/intel_fbc.c` | 1 | MIT (permission notice text, no SPDX tag) | Copyright © 2014 Intel Corporation |
| `display/intel_global_state.h` | 1 | MIT | Copyright © 2020 Intel Corporation |
| `display/intel_dmc_regs.h` | 1 | MIT | Copyright © 2022 Intel Corporation |
| `i915_reg_defs.h` | 1 | MIT | Copyright © 2022 Intel Corporation |
| `i915_utils.h` | 1 | MIT (permission notice text, no SPDX tag) | Copyright © 2016 Intel Corporation |
| `display/intel_display_power.c` | 1 | MIT | Copyright © 2019 Intel Corporation |
| `display/intel_dp_aux.c` | 1 | MIT | Copyright © 2020-2021 Intel Corporation |
| `intel_wakeref.h` | 1 | MIT | Copyright © 2019 Intel Corporation |
| `display/intel_pps.c` | 1 | MIT | Copyright © 2020 Intel Corporation |
| `i915_driver.c` | 1 | MIT (permission notice text, no SPDX tag) | Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas. |
| `gem/i915_gem_execbuffer.c` | 1 | MIT | Copyright © 2008,2010 Intel Corporation |
| `gt/intel_engine_regs.h` | 1 | MIT | Copyright © 2022 Intel Corporation |
| `intel_uncore.c` | 1 | MIT (permission notice text, no SPDX tag) | Copyright © 2013 Intel Corporation |
| `gt/intel_lrc_reg.h` | 1 | MIT | Copyright © 2014-2018 Intel Corporation |
| `gt/gen8_ppgtt.c` | 1 | MIT | Copyright © 2020 Intel Corporation |
| `gt/intel_gtt.h` | 1 | MIT | Copyright © 2020 Intel Corporation |
| `gt/intel_migrate.c` | 1 | MIT | Copyright © 2020 Intel Corporation |
| `gt/intel_gt_clock_utils.c` | 1 | MIT | Copyright © 2020 Intel Corporation |
| `gt/intel_sseu.c` | 1 | MIT | Copyright © 2019 Intel Corporation |
| `gt/intel_engine_types.h` | 1 | MIT | Copyright © 2019 Intel Corporation |
| `gt/intel_gt_irq.c` | 1 | MIT | Copyright © 2019 Intel Corporation |
| `display/intel_display_irq.c` | 1 | MIT | Copyright © 2023 Intel Corporation |
| `pxp/intel_pxp.c` | 1 | MIT | Copyright(c) 2020 Intel Corporation. |
| `display/intel_display.c` | 1 | MIT (permission notice text, no SPDX tag) | Copyright © 2006-2007 Intel Corporation |
| `display/intel_opregion.c` | 1 | MIT (permission notice text, no SPDX tag) | Copyright 2008 Intel Corporation <hong.liu@intel.com>; Copyright 2008 Red Hat <mjg@redhat.com> |
| `display/intel_display.h` | 1 | MIT (permission notice text, no SPDX tag) | Copyright © 2006-2019 Intel Corporation |
| `display/intel_display_core.h` | 1 | MIT | Copyright © 2022 Intel Corporation |
| `display/intel_vga.c` | 1 | MIT | Copyright © 2019 Intel Corporation |

138 parity files; 50 name at least one upstream file; 26 carry port wording; 71 distinct upstream files named.
