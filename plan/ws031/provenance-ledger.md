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
