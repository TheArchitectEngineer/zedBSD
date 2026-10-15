<!-- awesome-plan project=zedbsd record=ws245-p003 -->

# ws245-p003: EDID（design.md の P02）

Status: cleared（2026-10-16、main に merge、提出 a8f2b0c11）
Parent: [WS245](../ws.md)
Approval: 2026-10-16 ユーザー「D1〜D6全部推奨で。」（D5: P01〜P05 から始める）

## 範囲

[design.md](../design.md) の P02 の行。新しい file は display/adl/adl-edid.c、adl.h と i915.mk には自分の block だけを足す。host 試験は plan/ws245/tests/ に別の file で。

## 結果

- **q993 P1（提出 a8f2b0c11）**: display/adl/adl-edid.c（1194 行）。`adl_edid_parse`（byte 列だけを受ける純粋な関数、0・-EINVAL・-ENODATA）、`adl_edid_preferred`、`adl_edid_mode_fits`。基本 block の header・checksum（壊れていれば拒む）、拡張 block の checksum の不一致はその block だけを捨てる、block 0 の DTD（最初を preferred）、CTA の DTD・VIC（native・Y420）、DisplayID 1.3 type I・2.x type VII（拡張 block と独立の区画）、standard・established。mode は最大 48、重複なし。
- 出典: VESA E-EDID Release A Rev 2、CTA-861-H、VESA DisplayID 1.3・2.0、VESA DMT 1.0 Rev 13（block ごとに Value(s) referenced:）。
- 確かめ（Q1、main）: build rc 0・warning 0、host 試験 78/0（P1 の報告は 91 件だったが、同じ file を P1 の branch と main で流して 78 件。報告の誤り）。
- 逸脱: VIC の表は 1〜64 と 93〜107（4K）だけ、他の VIC は捨てる。
- 未実施: 5330 の panel の実 EDID は repo に無く corpus は合成。点 A の試験で 5330 から取って corpus に足す。
