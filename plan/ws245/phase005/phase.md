<!-- awesome-plan project=zedbsd record=ws245-p005 -->

# ws245-p005: Opregion（design.md の P04）

Status: cleared（2026-10-16、main に merge、提出 8f2c9578e）
Parent: [WS245](../ws.md)
Approval: 2026-10-16 ユーザー「D1〜D6全部推奨で。」（D5: P01〜P05 から始める）

## 範囲

[design.md](../design.md) の P04 の行。新しい file は display/adl/adl-opregion.c、adl.h と i915.mk には自分の block だけを足す。host 試験は plan/ws245/tests/ に別の file で。

## 結果

- **q995 P3（提出 66e5bd4b0・8f2c9578e）**: display/adl/adl-opregion.c。opregion の header と mailbox 1〜5 の parse（呼び出し側が map した buffer から）、`adl_opregion_vbt`（mailbox 3 の RVDA・RVDS（2.1 以降は opregion からの offset、RVDS ≤ 6 MiB）→ mailbox 4 の 0x400 の 6 KiB → 無し（P03 は PCI の option ROM へ）、`$VBT` の署名と大きさを検査）、ASLE の要求・返事、backlight・panel の on の通知、lid。hardware には `struct adl_opregion_io` で（mock 可）。
- 出典: Intel IGD OpRegion Specification 2.0/2.1/3.0（値の block ごとに Values referenced:）。
- 確かめ: build rc 0・warning 0（担当）、host 試験 64/0（Q1、main）。
- 逸脱: ACPI の `_DSM`（GBDA・SBCB）は ACPI の evaluator が要るので P06（panel）以降へ。ASLC の割り込みの handler は未配線（takeover の Phase で）。
