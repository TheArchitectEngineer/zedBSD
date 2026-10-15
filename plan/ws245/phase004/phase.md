<!-- awesome-plan project=zedbsd record=ws245-p004 -->

# ws245-p004: VBT・OpRegion・EDID（xd-vbt、xd-edid）

Status: in-progress（2026-10-15 q1207 P1: 実装と host 試験は完了、受け入れ条件は全部 PASS。Q1 の merge と判定待ち）
Parent: [WS245](../ws.md)
Queue: q1207 / q1207-i01（P1、worktree p1、branch agent/p1-ws245、基点 main 6d07bee3d、途中で main dd1cd51a6・c76a71eda を merge）
Approval: 2026-10-15 ユーザー「WS245は、ロールバックできる体制にしておいて、今から実装します。」「WS245はいつでもロールバックできるので、今マージします」

## 範囲

[design.md](../design.md) §6.2 の p004 の行: `xd-vbt`（OpRegion を含む、4 platform の V2/V3/V4 の表）、`xd-edid`。受け入れ: 合成の VBT の bytes（5330 の値と、RKL・ADL-S・TGL の DVO/DDC の値の 4 組）と合成の OpRegion と EDID の fixture で、今の parser と同じ値（port の表、panel の DTD、PPS の T、backlight 200 Hz、eDP の bpp/rate/lanes、DDC → GMBUS の pin、AUX）を返す host 試験（`host-xd-vbt-test.c`）；VBT 無しの既定；壊れた VBT（size・offset・block の overrun）の拒否。

kernel の振る舞いは変えない（xd の file は `AMD64_I915_SOURCES` で compile するが呼ばれない）。§7 #18 は 2026-10-15 の回答（「今のまま（efifb）」）のとおり `device.c` は不変。

## 結果（2026-10-15、P1）

作った file（行数）:

- `src/drivers/gpu/i915/intel/xd-vbt-layout.h`（556）: VBT の layout の表 V1（VBT・BDB・block の header、general features・definitions、child device、driver features、eDP、LFP options・pointers・data（DVO timing）、backlight、power conservation、generic DTD の offset と mask、DVO・AUX・HDMI rate の code、BDB の version の境）。block ごとに複数行の Values referenced（26 block）。
- `src/drivers/gpu/i915/intel/xd-opregion-layout.h`（72）: 表 O1（header、mailbox の bit、mailbox 4 と ASLE の RVDA/RVDS）。
- `src/drivers/gpu/i915/display/xd-vbt.h/.c`（230・2114）: `drv_i915_xd_opregion_find_vbt`（raw VBT（RVDA、2.1 から相対）と mailbox 4 の場所）、`drv_i915_xd_vbt_check`（header・size・BDB の範囲）、`drv_i915_xd_vbt_parse`（general features・definitions・driver features、VBT 無し／壊れた VBT は既定の出力）、`drv_i915_xd_vbt_child`、`drv_i915_xd_vbt_panel`（panel type: VBT → EDID の PnP → 最初の行、panel options、generic DTD → LFP data の DVO timing、backlight、DRRS、power、eDP）、`drv_i915_xd_sha256`（N0 の報告用、FIPS 180-4）。確保なし、bytes は読むだけ、block の外は 0 として読む（Linux の padding と同じ）。
- `src/drivers/gpu/i915/display/xd-edid.h/.c`（118・303）: block の確かめ（header・checksum）、base block（PnP の 10 byte、製造者、digital と bpc、画面の大きさ、最初の detailed timing は own の `output.c` の `drv_i915_output_edid_timing`）、CTA 拡張（音・YCbCr の flag、video block の VIC と native、HDMI の VSDB と TMDS の上限）。
- 試験: `src/drivers/gpu/i915/tests/display/host-xd-vbt-test.c`（975）、`plan/ws245/tests/check-xd-vbt-layout-linux.c/.sh`（318・19）。

変えた file: `display/xd-platform.h`（`struct i915_xd_vbt_dvo_row`・`struct i915_xd_vbt_maps`、記述に `vbt_maps`）、`intel/display-tables-xd.h`（表 V2・V3・V4 の 4 platform 分と 9 行の記述の pointer、+149）、`plan/ws245/tests/run-xd-host-tests.sh`（host-xd-vbt-test を足す）、`plan/ws245/tests/check-xd-tables-linux.py`（V2/V3/V4 を Linux の .c と比べる、+58）、`platform/amd64/vmunix.mk`（`xd-vbt.c`・`xd-edid.c`）。

## 受け入れ条件ごとの結果

| 条件 | 結果 |
| --- | --- |
| 合成の VBT 4 platform（TGL・RKL・ADL-S・ADL-P の 5330 の値）で出力の表（DDI、AUX、GMBUS の pin）、panel の DTD、PPS、backlight、eDP の bpp/rate/lanes | **PASS**。`host-xd-vbt-test` 119 検査 0 失敗。ADL-P: DVO DP_F/DP_G → TC1/TC2（DDI 3/4）、AUX 0x60/0x70 → USBC1/2、DDC 3/4 → GMBUS 9/10、HDMI B は DDC 2 → pin 2。TGL: DP_D → TC1、DP_I → TC6（DDC 9 → pin 14）。RKL: HDMI_C/DP_D → TC1/TC2（combo、DDC 3/4 → 9/10）。ADL-S: HDMI_B → TC1（DDC 2 → 9）、DP_E → TC4（AUX 0x50 → ch 6、DDC 5 → 12）。panel は generic DTD（ADL-P・RKL・ADL-S）と LFP data の DVO timing（TGL は pointers block 無しで作った pointers、ADL-P の EDID の版は pointers block）の両方の道、backlight 200 Hz と 16 bit の最小値、eDP HBR×2・18 bpp・PPS の 5 値 |
| 今の parser と同じ値 | **PASS**（ADL-P の 2 組）。今の `vbt.c`（ADL-P 専用）と出力ごとに 18 field、panel の 33 field（行、eDP、PPS、backlight、DRRS、VRR、mode）が一致。PnP の照合（VBT が 0xff、EDID の id が行 5）も同じ行 |
| VBT 無しの既定 | **PASS**。4 platform（ADL-P/TGL は DDI A・B・C、RKL は A・B・TC1・TC2 と無い port 2 つ、ADL-S は A・TC1〜4 と無い port 1 つ。Linux の既定と同じ規則）、ADL-P は今の parser の既定と一致 |
| 壊れた VBT の拒否 | **PASS**。短い header、署名、VBT が buffer より長い、BDB header が VBT の外、BDB の offset の桁あふれ、BDB が VBT の外（今の parser の判定とも一致）、拒否で既定と EINVAL、BDB を越える block（その block と後の block が見えない）、child の大きさ < 33、panel の表の terminator の欠け（mode なし、PnP の照合なし）、出力なしの panel |
| OpRegion | **PASS**。7 組（2.0 mailbox 4、ASLE ext、2.0 の物理 RVDA、2.1・3.0 の相対 RVDA、ASLE 無し、1.0）が今の `drv_i915_opregion_locate_vbt` と一致、短い copy と署名の拒否 |
| EDID | **PASS**。5330 の panel（AUO 0x2b99、digital 6 bpc、290×170 mm、1920×1080 140.8 MHz を output.c と同じ値で）、合成の HDMI sink（CTA、HDMI 340 MHz、音、VIC 16 native・4・3）、壊れた拡張・base、bytes の外の拡張 |
| SHA-256 | **PASS**。FIPS 180-2 の 3 例と今の `drv_i915_sha256` の値 |
| Linux との機械の照合 | **PASS**。`check-xd-vbt-layout-linux.sh`（Linux の `intel_vbt_defs.h` の struct に field を立てて場所と mask を読む）193 値 0 差、`check-xd-tables-linux.py` の V2/V3/V4 12 表が Linux の `intel_bios.c` の表と一致（enum の値は Linux の header から計算） |
| build warning 0、`git diff --check` | **PASS**。`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk vmunix` rc=0、warning 0（kernel include check・vmunix check PASS）。`git diff --check` 0 |

QEMU・実機の確かめは無い（host だけの Phase。xd の file は呼ばれない）。

## 設計との差（判断と理由）

1. **OpRegion の map と PCI ROM**: design §3.2 は xd-vbt に「OpRegion の map、VBT の探索（mailbox 4 / RVDA、PCI ROM）」を置くが、p004 は bytes の読みだけ（`drv_i915_xd_opregion_find_vbt` は場所を返し、map・RVDA の map・PCI ROM の読みは呼ぶ側）。kernel の map を使う段は xd-display（p009）で組む。
2. **V2/V3/V4 の表の置き場所**: platform の記述（`struct i915_xd_platform` の `vbt_maps`）から引く。xd-platform.h と display-tables-xd.h（p002 の file）に足した。`kind` での分岐は無い。
3. **panel type の範囲外**: 2 つ目の panel の `panel_type2` が 16 以上で 0xff でない時、Linux は検査せず添字に使うが、xd は「VBT の行なし」として扱う（範囲外を読まない）。
4. **DSI の DVO port（MIPI A/C）**: display 11〜13 の対象機種に DSI は無いので、MIPI の DVO port の child は DDI 無し（`I915_XD_VBT_NONE`）として持つ（Linux は port A/B に割り当てる）。
5. **eDP の pre-emphasis/swing**: 今の parser は DPCD の training の値（pre-emphasis は level << 3）で持つが、xd は level の番号 0〜3 で持つ（試験は換算して比べる）。
6. **EDID の detailed timing**: design のとおり own の `output.c` の decode を呼ぶ。`output.h` が今は `internal.h` を include するので、xd-edid.c は p003/p013 で `xd.h` 側に付け替えるまで `internal.h` を間接に読む。
7. **SHA-256**: 今の `drv_i915_sha256` は `vbt.c`（旧 code）にあるので、xd-vbt に FIPS 180-4 から自前の実装を置いた（今の値と一致を確かめた）。

## 出典の書き方の確かめ

- 新しい file の先頭の comment に出典・派生の宣言は無い（`Linux`・`referenced` は block の直前の comment だけ）。
- 値の block ごとに複数行の Values referenced: xd-vbt-layout.h 26、xd-opregion-layout.h 3、display-tables-xd.h の V2/V3/V4 3、xd-vbt.c 1（FIPS 180-4）、xd-edid.h/.c 4（VESA E-EDID、CTA-861、HDMI 1.4）。手順は xd-vbt.c の関数ごとに Procedure referenced（Linux v6.8.12 intel_bios.c・intel_opregion.c、FIPS 180-4）。
- Linux の symbol の名前・struct・packed の配置は使っていない（offset と mask の define、`I915_XD_*`・`i915_xd_*`・`drv_i915_xd_*`）。手順の分け方（block の探索、child の読み、panel の段）は自分で決めた。
- 開発の管理情報（WS・Phase の番号）は source に無い（試験の道具の先頭の行 `ws245-p004:` は plan/ の script の説明で、tree の source ではない）。

## 実行したコマンド

```
make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk vmunix                 # rc=0, warning 0
sh plan/ws245/tests/run-xd-host-tests.sh build/ws245-p004/host2           # regs 495/0, io 48/0, platform 80/0, vbt 119/0
sh plan/ws245/tests/check-xd-vbt-layout-linux.sh                         # 193 values, 0 differ
python3 -I plan/ws245/tests/check-xd-tables-linux.py                     # all tables equal (V2/V3/V4 の 12 表を含む)
git diff --check                                                         # 0
```

使い捨ての出力は worktree p1 の `build/ws245-p004/`（Q1 が掃除してよい）。

## 残り

- Q1 の merge と判定。
- OpRegion・RVDA・PCI ROM の map は p009（xd-display）で。
