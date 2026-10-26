<!-- awesome-plan project=zedbsd record=ws252-p006 -->

# ws252-p006: ICL の video・PXP の除外・attach・DMC

Status: in-progress（2026-10-26 q1599 P4: attach と除外は実装・build・5330 の回帰。DMC は q1601。ICL の実機は無く未確認。cleared は Q1）
Parent: [WS252](../ws.md)
Queue: q1599-i01（P4）
設計: [design.md](../design.md) §6.2 の「直す（p006）」、D1-I「C 既定で付ける」（2026-10-19 ユーザー）

## やった事

- 557c9de05: `i915.c` の `identifiers[]` に `I915_PCI_ICL_IDS`（既定で付く）。display は WS245 p045 の display 11。
- 4e316feac・e68361995: video（worker の video engine、Vulkan の video の申告）は版 12 以上だけ（executor は Gen12 の MFX の command を書く）、PXP は版 12 未満で作らない。suspend の D0 の枝は Gen9 だけ（ICL の display は xd）。
- DMC（icl_dmc_ver1_09.bin）は q1601 で i915-firmware の package に入れる（2026-10-26 ユーザー「zedBSD-firmware/ツリーに、i915の依存としてすべて入れられませんか？」）。

## 確かめた事

- build warning 0。5330（素の起動、kernel 557c9de05 = p004〜p006 の全部、2026-10-26）: ssh まで 30 s、survey lit（frames +135、CRC 2 回同値、underrun 0）、i21 の基準と steady の差 0 → PASS。書き込みの digest は 13594 → 13591（display の code は変えていない。起動ごとに揺れる: 同じ main の 1 回目は 13602）。画面（keiland-shot）は desktop と Settings の初回の画面。証拠 `plan/ws245/tests/evidence/p4-ws252-p006-557c9de05.md`、PNG `plan/ws245/temp/5330/p4-ws252-p006-557c9de05.png`（git の外）。ICL の実機での attach・起動・表示・desktop は未確認（p007）。
