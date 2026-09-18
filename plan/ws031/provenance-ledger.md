# WS031 出典台帳（provenance ledger）

目的：ファイルを移動・統合する前に「どこから取り込んだか」を残す。ライセンス整理・配布可否の最終判断はここでは行わない（描画工程の後の独立工程）。ここに書くのは確認できた事実だけで、未確認は「未監査」と書く。

取り込み方の区分：**コピー**（原文をそのまま）／**改変**（原文を元に書き換え）／**生成物**（generator の出力）／**独立実装**（API 契約や仕様に合わせた zedBSD 側の実装）。

固定した参照元：
- Linux i915：`plan/ws031/linux-parity/linux-reference/ubu-i915-src`（公開 v6.8.12 相当。移植台帳 `plan/ws031/linux-parity/ledger.md` に revision 固定の記録）。ファイル単位でライセンスが異なる（一括で GPL／MIT としない）。
- Mesa：`plan/ws031/mesa-refs/mesa` = main @ `ab691a1cc7bcd264bec8f735deb2127861ad15ef`（2026-09-14）。既存監査 `plan/ws031/i915-vk-license-audit.md`（転記対象ファイルの MIT 確認と SHA-256）。
- DMC firmware：`i915/adlp_dmc.bin` v2.20、79088 bytes、sha256 `3516de2e134ddcf3b319c75d2e437779fecbd58cbb77234bd6f297c544e92ccb`。

## 1. 今回（E-98〜E-103）新たに取り込んだ／作ったもの

| zedBSD 側のファイル／範囲 | 区分 | 元 project・path・revision | 元の copyright／license | 備考 |
|---|---|---|---|---|
| `src/drivers/gpu/i915/tex_fixture_gen.inc` 全体 | 生成物 | generator `plan/ws031/handover/tools/reftex.c`（zedBSD 側で書いた NIR builder プログラム）。入力：Mesa @ab691a1c の `brw_compile_fs`（shader ISA と prog_data）、`isl`（texture layout と RENDER_SURFACE_STATE）、`genxml gen120`（SAMPLER_STATE、3DSTATE_PS DW3、3DSTATE_SAMPLER_STATE_POINTERS_PS） | Mesa `src/intel`：MIT（`brw_compiler.h` は SPDX MIT、`isl.h` は MIT permission notice、`gen120.xml` は repo の MIT data）。Copyright © Intel Corporation | 出力は数値表（shader 命令語、state 語、定数）。ファイル冒頭に generator・入力 revision・PS の sha256 を記載。`SPDX-License-Identifier: MIT` を付与 |
| `plan/ws031/handover/tools/reftex.c` | 独立実装（Mesa の公開 API を呼ぶ試験用ツール） | 既存の `refps_marker.c`（同じく zedBSD 側で書いた generator）を土台にした | zedBSD project | Mesa tree 内でビルドするが Mesa へは取り込まない。Mesa のコードは複製していない（API 呼出しのみ） |
| `src/drivers/gpu/i915/draw_fixture.h`、`selftest.c` の wrapper／texture fixture 関数、`parity/eu_test.{c,h}` の各試験 harness | 独立実装 | — | zedBSD project | テスト画像の式（R=16+32u …）は専門家の提案（本 WS の指示書）による |
| `src/drivers/gpu/i915/vk/linux/3dstate-gen12.inc` の `GEN12_CMD_PIPELINE_SELECT`（0x6104→0x6904 の修正） | 改変（定数 1 個） | Linux `gt/intel_gpu_commands.h` の `PIPELINE_SELECT` 定義、Mesa `genxml`（Type 3／SubType 1／Opcode 1／SubOpcode 4） | Linux 該当ファイル：SPDX MIT。Mesa genxml：MIT | 値の照合であって原文の複製ではない |
| `selftest.c` の `i915_draw_const_color_ps[]`（既存、E-101 で再利用） | 生成物 | generator `plan/ws031/handover/tools/gen_refps_marker.py`→`refps_marker.c`、入力 Mesa @ab691a1c `brw_compile_fs` | Mesa：MIT | E-13 期に生成。今回は変更なし |

## 2. 既存の取り込み（今回の作業で触れた範囲。全量の監査ではない）

| zedBSD 側 | 区分 | 元 | 元の license（確認した範囲） | 状態 |
|---|---|---|---|---|
| `parity/gt_lrc_offsets.inc` | 生成物 | `tools/gen_lrc_offsets.py` ← Linux `gt/intel_lrc.c` | SPDX MIT | 確認済み（SPDX 行） |
| `parity/gt_fw_ranges.inc` | 生成物 | `tools/gen_fw_ranges.py` ← Linux `intel_uncore.c` | MIT permission notice（SPDX 行なし） | 確認済み（header の notice） |
| `parity/gt_wa_adlp.c`、`gt_mocs` 相当、`gt_submit.c`、`gt_mem.c`（gen8 ppgtt） | 改変／独立実装が混在 | Linux `gt/intel_workarounds.c`、`gt/intel_mocs.c`、`gt/intel_execlists_submission.c`、`gt/gen8_ppgtt.c` | いずれも SPDX MIT | 関数単位の区分は**未監査**（移植台帳の状態語 PORTED／VERIFIED と対応付けて後で確定） |
| `parity/dmc.c`（DMC ロード手順） | 改変 | Linux `display/intel_dmc.c` | MIT permission notice（SPDX 行なし） | driver source の license であり firmware 本体には適用しない |
| `parity/firmware_adlp_dmc.c`（C 配列） | 生成物（blob の byte 写し） | linux-firmware `i915/adlp_dmc.bin` v2.20 | **firmware は driver とは別ライセンス**（linux-firmware `WHENCE`／`LICENSE.i915` 系）。全文と配布条件は**未監査** | 元 blob・変換手順・生成配列・配布 image の対応を追跡する。配列化しても独自著作物にはならない |
| `parity/osdep/*`、`wait.c`、`drm_device.c`（workqueue／timer／list／completion 相当） | 独立実装（Linux の API 契約に対応する zedBSD 実装）と理解しているが | Linux `kernel/workqueue.c` 等は GPL-2.0-only | — | **未監査**。関数本体の複製・改変が無いことを後工程で確認する |
| `vk/linux/*.inc`、`linux/i915-commands.inc`、`linux/i915-workarounds.inc` | 改変（定数・レジスタ定義の転記） | Linux i915 header、Mesa genxml | 既存監査 `i915-vk-license-audit.md` 参照 | 既存監査の範囲外の定数は**未監査** |

## 3. 規約（リファクタ開始まで）

1. 取り込み時に元の copyright／license 表示を落とさない。後で整理することと、今落としてよいことは別。
2. 生成物は「入力・generator・その revision」を生成ファイルの冒頭に書く。
3. 新しく取り込むたびに本台帳の §1 に 1 行足す。
4. ライセンスの互換性・配布可否の判断、`LICENSES/`／SPDX の整備は、テクスチャ更新・再利用（T3）の後の独立工程で行う。

## 4. 対象機固有の firmware データ（E-106 追記）

| 置き場所 | 区分 | 元 | 扱い |
|---|---|---|---|
| `plan/ws031/display-ref/i915_vbt.bin`、`i915_opregion.bin`、`edid-eDP-1.bin`、`dpcd-*.bin` | コピー（対象機 Dell Latitude 5330 の firmware／panel から Linux i915 の debugfs・sysfs・AUX 経由で読んだ byte 列） | 機体の firmware（Dell／Intel）と panel（AUO）のデータ | 参照用の試験入力。**ライセンスと配布可否は未監査**。配布 image へ入れるかどうかは別途判断する。source tree（`src/`）には入れていない |

## 5. E-107（2026-09-18）: VBT parser の取り込みと、対象機 VBT の source tree への配置

### 5.1 Linux 由来（元の表示を保持）
| zedBSD 側 | 区分 | 元（Linux 6.8.12 固定参照） | 元の copyright／license | 備考 |
|---|---|---|---|---|
| `parity/vbt/intel_bios_port.c` | **生成物（元 text の複製＋記録した改変）** | `drivers/gpu/drm/i915/display/intel_bios.c`。generator `plan/ws031/handover/tools/port_intel_bios.py`。元ファイルの sha256 と、削除した関数・置換の一覧を生成ファイルの冒頭に記録 | 元ファイル冒頭の Intel copyright 表示＋MIT permission notice（SPDX 行なし）を**そのまま保持**。author 行も原文どおり | 関数本体は再入力していない。zedBSD 側の追加は末尾の `#include "parity_vbt_glue.inc"` と provider 呼出し 1 箇所 |
| `parity/vbt/intel_vbt_defs.h` | コピー（include 1 行と guard 周辺のみ変更） | `display/intel_vbt_defs.h` | 元の Intel copyright＋MIT permission notice を保持 | VBT／BDB の構造体定義 |
| `parity/vbt/intel_bios.h` | コピー（同上） | `display/intel_bios.h` | 同上 | |
| `parity/vbt/vbt_ref_types.h` | 生成物（enum／struct の text 抽出） | `display/intel_display_limits.h`（enum port）、`display/intel_display.h`（enum aux_ch／phy）、`soc/intel_pch.h`（enum intel_pch）、`display/intel_display_types.h`（struct intel_vbt_panel_data）、`display/intel_display_core.h`（struct intel_vbt_data）。抽出元と範囲は生成ファイルの冒頭に記録 | 各元ファイルの表示（MIT permission notice または SPDX MIT）。**ファイルごとの表示の突合せは未監査** | 抽出は generator が行い手入力なし |

### 5.2 zedBSD 側で書いたもの
| ファイル | 区分 | 備考 |
|---|---|---|
| `parity/vbt/vbt_compat.h` | 独立実装（正本が使う kernel API の契約に合わせた最小の型・list・割当・log。定数値 `DP_*`／`GMBUS_PIN_*`／platform 判定は Linux header の値と照合） | 定数の転記元: `include/drm/display/drm_dp.h`、`display/intel_gmbus.h`（いずれも MIT 系表示。値の照合であって本文の複製ではない） |
| `parity/vbt/parity_vbt.h`、`parity/vbt/parity_vbt_glue.inc` | 独立実装 | 正本の private list を平坦な record へ写す glue と arena |
| `parity/bios.c` の `parity_sha256()` | 独立実装 | FIPS 180-4 の仕様から実装。"abc" の公式 vector で検査（ktest VBT-SHA）。既存実装の複製ではない |
| `plan/ws031/tests/vbt-host-test.c` | 独立実装 | 期待値の出所は igt `intel_vbt_decode` の出力（`display-ref/vbt-decode.txt`） |

### 5.3 対象機固有の firmware データ（§4 の続き）
| 置き場所 | 区分 | 元 | 扱い |
|---|---|---|---|
| `parity/firmware_vbt_dell_latitude_5330.c`（C 配列、8704 byte、sha256 `3bff4a0920d55c9aee0ea3c678904f982f8671c0e7b5bc97a335e429b29624cd`） | 生成物（`display-ref/i915_vbt.bin` の byte 写し） | 対象機 Dell Latitude 5330（PCI subsystem 1028:0b02）の firmware が持つ VBT。Linux i915 debugfs `i915_vbt` から採取（E-106） | **E-107 で初めて `src/` に入った**。build flag `PARITY_VBT_EXPLICIT=1` のときだけ参照され、かつ subsystem が一致する機体でだけ採用される。既定 build では image に含まれても使われない。**ライセンスと配布可否は未監査**（機体 firmware のデータであり、配列化しても独自著作物にはならない）。配布 image へ入れるかどうかはライセンス整理の工程で判断。native では OpRegion／RVDA から読む経路へ置き換える予定 |

## 6. E-108（2026-09-19）: eDP の PPS／AUX／DPCD／EDID 取得の取り込み

追加した固定参照: `plan/ws031/linux-parity/linux-reference/drm-v6.8.12/`（i915 の固定参照 tree に DRM core が無かったため、kernel.org の stable tree `v6.8.12` から 2026-09-18 に取得）。`drm_dp_helper.c` sha256 `030568524ac5db3fbd09725df196b22a430ec18fc1432dbb952298ce7c791a73`、`drm_dp.h` `306a1a47ba001c417baa3dab1f3a58c7e5de3c7a0537d26806a130603b599c5f`、`drm_edid.c` `a01138078180d234149ac4403839a99b9c85232955a9830ad5552637cd8661a7`。**正本環境（Ubuntu 6.8.0-139）の同ファイルとの差分は未照合**。

### 6.1 参照由来（元の表示を保持）
| zedBSD 側（`src/drivers/gpu/i915/parity/dp/`） | 区分 | 元 | 元の copyright／license | 備考 |
|---|---|---|---|---|
| `intel_pps_port.c` | 生成物（元 text の複製＋記録した削除・置換） | Linux 6.8.12 `display/intel_pps.c`。generator `tools/port_dp_aux_pps.py`。元の sha256 と変更一覧は生成ファイル冒頭 | `SPDX-License-Identifier: MIT`、Copyright © 2020 Intel Corporation（原文保持） | 関数本体は再入力なし |
| `intel_dp_aux_port.c` | 同上 | `display/intel_dp_aux.c` | SPDX MIT、Copyright © 2020-2021 Intel Corporation（原文保持） | |
| `intel_dp_aux_regs.h`、`intel_pps_regs.h`、`intel_pps.h`、`intel_dp_aux.h` | コピー（include 行のみ変更） | `display/` の同名 header | 各ファイルの SPDX MIT＋Intel copyright（原文保持） | |
| `dp_ref_types.h` | 生成物（`struct intel_pps` の text 抽出） | `display/intel_display_types.h` | MIT permission notice、Copyright Intel Corporation（元ファイルの表示。抽出ファイルには出所を記載） | |
| `drm_dp_helper_port.c` | 生成物（keep-list で関数を抜粋、module parameter 2 組を削除） | upstream v6.8.12 `drivers/gpu/drm/display/drm_dp_helper.c` | Copyright © 2009 Keith Packard、**HPND 系の permission notice**（"Permission to use, copy, modify, distribute, and sell …"。MIT とは別文面）。原文保持 | license の分類と互換性は**未監査** |
| `drm_dp.h` | コピー（include 1 行のみ変更） | upstream v6.8.12 `include/drm/display/drm_dp.h` | Copyright © 2008 Keith Packard、同じ HPND 系 notice（原文保持） | DPCD address と AUX の request／reply code |
| `drm_edid_port.c` | 生成物（keep-list: DDC の EDID block 読出しと header／checksum helper） | upstream v6.8.12 `drivers/gpu/drm/drm_edid.c` | 複数の copyright 行（Luc Verhaegen、Intel Corporation／Jesse Barnes、Red Hat, Inc.、Dennis Munsie）＋MIT permission notice。**全行を原文のまま保持**（名前は推測せず元ファイルから複製） | |

### 6.2 zedBSD 側で書いたもの（独立実装）
| ファイル | 備考 |
|---|---|
| `dp_compat.h` | 正本 text が kernel に求める契約（レジスタ macro、待ち、sleep、clock、電源参照、mutex、delayed work、I2C adapter、DP AUX object の member）の最小実装。`SOUTH_CHICKEN1`／`SOUTH_DSPCLK_GATE_D` と bit、I2C の flag／functionality 値は Linux header の値と照合した定数（値の照合であって本文の複製ではない） |
| `parity_edp.{h,c}`、`parity_dp_kernel.{c,h}`、`parity_*_glue.inc` | 呼出し順は `intel_dp.c` の `intel_edp_init_connector()`／`intel_edp_init_dpcd()` に従うが、本文は複製していない。`parity_drm_edid_glue.inc` は `drm_edid.c` の `edid_block_read()` の契約（試行回数と打切り条件）に従う独立実装 |
| `dp_fake_hw.{c,h}`、`edp_ktest.{c,h}`、`plan/ws031/tests/dp-host-test.c` | 試験用。register model の offset／bit は driver 側 header と独立に記述 |

### 6.3 対象機固有データ
| 置き場所 | 区分 | 扱い |
|---|---|---|
| `parity/dp/dp_fixture_latitude5330.h`（DPCD 0x000／0x100／0x700 各 256 byte、EDID 128 byte の C 配列。generator `tools/gen_dp_fixture.py`、各 sha256 を記載） | 生成物（`display-ref/` の byte 写し）。対象機の panel（AUO B133HAN）から Linux i915 経由で読んだデータ | GPU-free 試験の入力と、実機取得値の**照合**にだけ使う（実 AUX 応答の代用にはしない）。**ライセンスと配布可否は未監査** |

## 7. E-109（2026-09-19）: LCD-A 計算の取り込み、async power put、EU の negate bit

固定参照の追加: `linux-reference/drm-v6.8.12/` に `drm_edid.h`（sha256 `e61def12761bc325265437b33e91a7c0cc99ef3f7c60a4265eadb112ff66759a`）と `drm_dp_helper.h`（`1969846dd3fdb5d7511319caf5f8e8481eacecad610fa429669450b9e5775b73`）。5 file の hash は同 directory の `SHA256SUMS`。`tools/check_generated.sh` が再生成して `src/` と byte 比較する（生成物の手編集・generator の未再実行・参照の変化を検出）。

### 7.1 参照由来（元の表示を保持、`src/drivers/gpu/i915/parity/lcd/`、generator `tools/port_lcd_calc.py`、manifest `port_lcd_calc.manifest.json`）
| zedBSD 側 | 区分 | 元 | 元の copyright／license | 備考 |
|---|---|---|---|---|
| `drm_edid_mode_port.c` | 生成物（keep-list: EDID_QUIRK_* の bit 定義、`drm_mode_do_interlace_quirk`、`drm_mode_detailed`） | upstream v6.8.12 `drivers/gpu/drm/drm_edid.c` | 複数の copyright 行＋MIT permission notice（原文保持。§6.1 の `drm_edid_port.c` と同じ元 file） | |
| `edid_ref_types.h` | 生成物（構造体と bit 定義の text 抽出） | upstream v6.8.12 `include/drm/drm_edid.h` | 元 file 冒頭の copyright＋MIT permission notice をそのまま先頭に保持 | EDID block の layout |
| `intel_link_port.c` | 生成物（keep-list） | Linux 6.8.12 `display/intel_dp.c`（5 関数）、`display/intel_display.c`（3 関数）、upstream v6.8.12 `drm_dp_helper.[ch]`（2 関数） | 先頭に `intel_dp.c` の表示（Copyright © 2008 Intel Corporation＋MIT permission notice、author 行）を保持。`intel_display.c` 分は MIT permission notice／Copyright © 2006-2007 Intel Corporation、`drm_dp_helper` 分は Keith Packard の HPND 系 notice（§6.1）で、**同一 file に 3 つの元が混在**することを生成 header に明記 | `drm_dp_helper` 由来 2 関数の notice 本文は同 file に無く参照だけ → **次の再生成で元ごとに file を分ける**（回帰済み source を変えないため今回は据置き）。**未監査** |
| `intel_dpll_port.c` | 生成物（keep-list: `skl_wrpll_params`、DP combo PLL 表 2 本、`ehl_combo_pll_div_frac_wa_needed`、`icl_calc_dp_combo_pll`、`icl_calc_dpll_state`） | Linux 6.8.12 `display/intel_dpll_mgr.c` | Copyright © 2006-2016 Intel Corporation＋MIT permission notice（原文保持） | |
| `lcd_ref_types.h` | 生成物（`struct intel_link_m_n`、`struct intel_dpll_hw_state`、M/N と DPLL_CFGCR の field macro の text 抽出） | `display/intel_display_types.h`、`display/intel_dpll_mgr.h`、`i915_reg.h` | 各元 file の MIT permission notice／Copyright Intel Corporation（抽出 file には出所を記載。**file ごとの表示の突合せは未監査**） | |

### 7.2 改変・独立実装
| ファイル／範囲 | 区分 | 元 | 備考 |
|---|---|---|---|
| `parity/power_domains.c` の async put 一式（`grab_async_put_ref`、`put_async`、`async_work`、`flush_work[_sync]`、`verify_async_put_domains_state`、domain use count） | **改変**（正本の関数構造と順序に沿って zedBSD の型へ書き直した手移植。既存 `power_domains.c` と同じ流儀） | Linux 6.8.12 `display/intel_display_power.c` | 元は `SPDX-License-Identifier: MIT`、Copyright © 2019 Intel Corporation。`power_domains.c` 自体の出典表示は既存 file の整理対象（§2「改変／独立実装が混在」）に含める |
| `parity/backend_delayed.{c,h}`、`backend_sync.c` の `parity_kcancel_work`／`parity_kwork_is_pending` | 独立実装 | — | Linux の delayed_work の**契約**（queue／cancel／cancel_sync／flush の区別）に合わせた zedBSD 実装。`kernel/workqueue.c`（GPL-2.0-only）の本文は参照していない |
| `parity/lcd/lcd_compat.h`、`parity_lcd_calc.{h,c}`、`parity_edid_mode_glue.inc`、`parity_dpll_glue.inc`、`parity/dp/edp_sync_ktest.c`、`plan/ws031/tests/lcd-host-test.c` | 独立実装 | — | EDID 1.4 の colour depth 欄の解釈は VESA E-EDID 1.4 の field 定義による。比較先の数値は `display-ref/`（対象機で Linux が設定した値） |
| `vk/linux/eu-encoding-gen12.inc` の `EU_SRC0_NEGATE_BIT`=45、`EU_SRC1_NEGATE_BIT`=121 | 改変（定数 2 個の転記） | Mesa main @ab691a1c `src/intel/compiler/gen/xe.json`（SRC0_NEGATE [45]、SRC1_NEGATE [121]） | Mesa `src/intel`: MIT。値の照合であって本文の複製ではない |

## 8. E-110（2026-09-19）: LCD 生成物の source 別分割、register 書込み関数、scanout、scalar IR

固定参照の追加: `linux-reference/drm-v6.8.12/drm_modes.c`（hash は同 directory の `SHA256SUMS`。`check_generated.sh` が検査）。

### 8.1 参照由来（`src/drivers/gpu/i915/parity/lcd/`、generator `tools/port_lcd_calc.py`）
§7.1 の `intel_link_port.c` は 3 つの元が 1 file に混在していた。**E-110 で source file ごとに分割**し、各生成 file が中身の text の表示だけを持つようにした。
| zedBSD 側 | 区分 | 元 | 元の copyright／license | 備考 |
|---|---|---|---|---|
| `intel_link_port.c` | 生成物（keep-list 5 関数: `intel_dp_link_symbol_size`／`_symbol_clock`／`intel_dp_link_required`／`intel_dp_effective_data_rate`／`intel_dp_max_data_rate`） | Linux 6.8.12 `display/intel_dp.c` のみ | 元 file 冒頭の表示を原文保持 | §7.1 の同名 file を置換 |
| `intel_display_port.c` | 生成物（`intel_reduce_m_n_ratio`、`compute_m_n`、`intel_link_compute_m_n`、`intel_set_m_n`、`intel_cpu_transcoder_set_m1_n1`、`intel_set_transcoder_timings`、`intel_set_pipe_src_size`） | Linux 6.8.12 `display/intel_display.c` | 元 file 冒頭の表示を原文保持 | register 書込みは `lcd_compat.h` の emit hook 経由。末尾で `parity_display_emit_glue.inc`（zedBSD）を include |
| `drm_dp_bw_port.c` | 生成物（`drm_dp_bw_channel_coding_efficiency`、inline `drm_dp_is_uhbr_rate`） | upstream v6.8.12 `drm_dp_helper.c`／`drm_dp_helper.h` | Keith Packard の HPND 系 notice（§6.1 と同じ。原文保持、**未監査**） | `static inline` → external の 1 置換を header に記録 |
| `drm_modes_port.c` | 生成物（`drm_mode_set_crtcinfo`） | upstream v6.8.12 `drivers/gpu/drm/drm_modes.c` | 元 file 冒頭の複数 copyright 行＋permission notice を原文保持（**未監査**） | EXPORT_SYMBOL 行を除去 |
| `lcd_trans_regs.h` | 生成物（`enum transcoder`、transcoder timing／PIPE_DATA・LINK M/N／TRANS_SET_CONTEXT_LATENCY の定義の text 抽出） | `display/intel_display_limits.h`（SPDX MIT）、`i915_reg.h`（MIT permission notice） | 抽出 file には出所を記載、完全な表示は `intel_display_port.c` 側 | |

### 8.2 独立実装
| ファイル／範囲 | 区分 | 備考 |
|---|---|---|
| `parity/gt_mem.{c,h}` の表示用窓（`parity_gt_display_*`、`parity_gt_ggtt_read_pte`） | 独立実装 | 数値の根拠（256 KiB 整列 = `intel_linear_alignment`、guard 168 = VT-d guard、max stride、linear は DPT 不使用 = `intel_fb_modifier_uses_dpt`）は正本の関数を**読んで得た値**で、本文の複製ではない。出所は `scanout.h` の comment に記載 |
| `parity/lcd/scanout.{c,h}`、`scanout_ktest.{c,h}`、`lcd_pattern.{c,h}`、`lcd_hw_check.{c,h}`、`parity_display_emit_glue.inc`、`plan/ws031/tests/lcd-pattern-host.c` | 独立実装 | |
| `vk/spirv.c`（全面書直し）、`vk/spirv.h`、`vk/compile.c`、`vk/eu.{c,h}` の `i915_vk_eu_grf_scalar`、`plan/ws031/tests/i915-vk-lower-test.c` | 独立実装 | SPIR-V の opcode／enumerant 番号は Khronos の公開仕様（SPIR-V 1.0 §3、GLSL.std.450）。Mesa の compiler 本文は参照していない。新しい hardware 定数の追加なし（region の `<0;1,0>` は既存 `.inc` の `EU_VSTRIDE_0`／`EU_WIDTH_1`／`EU_HSTRIDE_0`） |

## 9. E-111（2026-09-19）: cpu transcoder の呼出し元、DDI／VRR の writer

固定参照の追加（`drm-v6.8.12/`、hash は `SHA256SUMS`）: `drm_connector.h`、`drm_fourcc.h`、`drm_blend.h`、`drm_color_mgmt.h`、`uapi_drm_mode.h`（kernel.org stable v6.8.12、無改変）。

| zedBSD 側（`parity/lcd/`） | 区分 | 元 | 元の copyright／license | 備考 |
|---|---|---|---|---|
| `intel_ddi_port.c` | 生成物（keep-list 7 関数） | Linux 6.8.12 `display/intel_ddi.c` | 元 file 冒頭の表示を原文保持 | 末尾で `parity_ddi_emit_glue.inc`（zedBSD）を include |
| `intel_vrr_port.c` | 生成物（2 関数） | `display/intel_vrr.c` | 元 file 冒頭の表示（SPDX MIT＋Copyright 行）を原文保持 | |
| `intel_display_port.c`（追加分 7 関数） | 生成物 | `display/intel_display.c` | §8.1 と同じ | |
| `intel_link_port.c`（追加分 2 関数） | 生成物 | `display/intel_dp.c` | §8.1 と同じ | |
| `lcd_ddi_types.h`、`lcd_ref_inlines.h`、`lcd_ddi_regs.h` | 生成物（enum／inline／register 定義の text 抽出） | `display/intel_display_limits.h`、`display/intel_display.h`、`display/intel_display_types.h`、`i915_reg.h` | MIT／Copyright Intel Corporation（抽出 file には出所を記載、完全な表示は `intel_ddi_port.c`／`intel_display_port.c` 側。**file ごとの突合せは未監査**） | |
| `lcd_dp_msa.h` | 生成物（DP_MSA_MISC_* の text 抽出） | upstream v6.8.12 `include/drm/display/drm_dp.h` | 元 file 冒頭の notice を先頭に原文保持（未監査） | |
| `lcd_drm_colorspace.h` | 生成物（enum drm_colorspace） | upstream v6.8.12 `include/drm/drm_connector.h` | 元 file 冒頭の notice（Copyright (c) 2016 Intel Corporation＋permission notice）を先頭に原文保持（未監査） | |
| `parity_ddi_emit_glue.inc`、`lcd_compat.h` の追加分（`drm_atomic_crtc_needs_modeset` の 1 行、encoder／digital port の member、rmw hook） | 独立実装 | — | `drm_atomic_crtc_needs_modeset` は drm_atomic.h の契約（3 つの flag の OR）を自前で記述 | |

## 10. E-112（2026-09-19）: universal plane の語

固定参照の追加: `drm-v6.8.12/i915_drm.h`（kernel.org stable v6.8.12 `include/uapi/drm/i915_drm.h`、無改変、hash は `SHA256SUMS`）。

| zedBSD 側（`parity/lcd/`） | 区分 | 元 | 元の copyright／license | 備考 |
|---|---|---|---|---|
| `skl_plane_port.c` | 生成物（keep-list 27 関数） | Linux 6.8.12 `display/skl_universal_plane.c` | 元 file 冒頭の表示（SPDX MIT＋Copyright 行）を原文保持 | 末尾で `parity_plane_emit_glue.inc`（zedBSD）を include |
| `lcd_plane_regs.h`、`lcd_plane_types.h` | 生成物（text 抽出） | `i915_reg.h`、`display/intel_display_limits.h` | MIT／Copyright Intel Corporation（抽出 file に出所、完全な表示は `intel_ddi_port.c`／`skl_plane_port.c` 側。未監査） | |
| `lcd_psr_selfetch_regs.h` | 生成物（text 抽出） | `display/intel_psr_regs.h` | 元 file 冒頭の表示を先頭に原文保持 | |
| `lcd_i915_colorkey.h` | 生成物（構造体 1＋define 2 の text 抽出） | upstream v6.8.12 `include/uapi/drm/i915_drm.h` | 元 file 冒頭の notice（Copyright 2003 Tungsten Graphics＋permission notice）を先頭に原文保持（未監査） | |
| `lcd_drm_fourcc.h` | 生成物（**file 全体**、`#include "drm.h"` の 1 行だけ除去） | upstream v6.8.12 `include/uapi/drm/drm_fourcc.h` | 元 file の notice がそのまま先頭に残る（Copyright 2011 Intel Corporation＋permission notice） | 置換は manifest の `substitutions` に記録 |
| `lcd_drm_plane_defs.h` | 生成物（3 file からの text 抽出） | upstream v6.8.12 `include/drm/drm_blend.h`、`include/uapi/drm/drm_mode.h`、`include/drm/drm_color_mgmt.h` | **3 つの元が 1 file に混在**。各元の notice は固定参照 directory に無改変で保持し、抽出 file には出所と hash を記載。file 分割と notice の転記は次の再生成で行う（**未監査・要整理**） | DRM_MODE_BLEND_*、ROTATE／REFLECT、colour enum、`drm_rotation_90_or_270` |
| `lcd_plane_compat.h`、`parity_plane_emit_glue.inc`、`parity_lcd_calc.c` の step 記録 | 独立実装 | — | — | |

## 11. E-113（2026-09-19）: enable 列の呼出し元

| zedBSD 側（`parity/lcd/`） | 区分 | 元 | 備考 |
|---|---|---|---|
| `intel_display_port.c`（追加: `hsw_crtc_enable`） | 生成物 | Linux 6.8.12 `display/intel_display.c` | notice は §8.1 と同じ |
| `intel_ddi_port.c`（追加 7 関数: `intel_ddi_config_transcoder_func`、`tgl_ddi_pre_enable_dp`、`intel_ddi_pre_enable_dp`、`intel_ddi_pre_enable`、`intel_enable_ddi_dp`、`intel_enable_ddi`、`intel_ddi_pre_pll_enable`） | 生成物 | `display/intel_ddi.c` | notice は §9 と同じ |
| `lcd_seq_compat.h`、glue の dispatcher（`intel_encoders_*`）と hook の束ね | 独立実装 | — | callee の**名前**だけを step として記録（本文は取り込んでいない）。hook の対応は `intel_ddi_init()` の代入を読んで合わせた |
