<!-- awesome-plan project=zedbsd record=ws252-p003 -->

# ws252-p003: RKL・ADL-S・RPL-S の attach

Status: in-progress（2026-10-26 q1599 P4: attach は実装・build・5330 の回帰 PASS。実機の受け入れ（D1=C の起動・表示・desktop）は実機が無く未確認。cleared は Q1）
Parent: [WS252](../ws.md)
Queue: q1599-i01（P4）
設計: [design.md](../design.md) §3・§5（D1-R=C「既定で付ける」、2026-10-19 ユーザー）

## やった事（f8b65c839）

- `i915.c` の `identifiers[]` に `I915_PCI_RKL_IDS`・`I915_PCI_ADLS_IDS`・`I915_PCI_RPLS_IDS`（既定で付く）。display は xd の表が既に 3 つの platform の行を持つ。
- DMC の firmware（rkl_dmc_ver2_03.bin・adls_dmc_ver2_01.bin）は足さない（2026-10-26 Q1 の判断: Future Work fw-i915-dmc-rkl-adls-icl。DMC が無くても表示は動き、DC5/DC6 の省電力だけが無い）。build option（D1=A）は D1=C なので作らない。

## 確かめた事

- build（p002 と同じ）warning 0、5330 の回帰 PASS（p002 の節）。RKL・ADL-S・RPL-S の実機での起動・GT の初期化・表示・desktop は実機が無く**未確認**（p007）。
