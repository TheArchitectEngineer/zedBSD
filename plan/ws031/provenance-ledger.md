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
