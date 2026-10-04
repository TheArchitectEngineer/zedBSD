<!-- awesome-plan project=zedbsd record=ws052p009 -->

# ws052-p009: i915 の suspend・resume

Phase ID: `ws052-p009`
Parent: [WS052](../ws.md)
Status: planning（2026-10-05 P1 generation17。設計の第 1 版 [design-p009-i915.md](../design-p009-i915.md)、Q1 のレビュー待ち。code は設計の後）
Phase disposition: normal
Queue: Q1 の 2026-10-05 の指示（p004 から i915 を分けた: QEMU で試せず規模が大きい。設計から、検証は 5330 の UAT）

## 範囲

display の suspend（窓を出る）、request worker の park、GT（forcewake・RPS・RC6・engine）、割り込み、display の power（DC9、DMC の再 load、display の
core の再初期化）、GGTT、resume で suspend の前の出力先（panel か HDMI、Keiland の lease はそのまま）へ戻す。HAL に依らない。

## 受け入れ

- 設計（Q1 のレビュー済み）、build（vmunix の link、warning 0）、host の試験（worker の park の状態機械、DC9 の前提と書き順）。
- 5330 の UAT（p004 の `sleepctl devices`）: 画面が消えて同じ出力先に戻り、GPU の描画が続く。dmesg に RC6 と DC9。

## 設計

[design-p009-i915.md](../design-p009-i915.md)（§6 に判断の点: GGTT の書き直し、DC9 の前提、park 中の request、出力先の記憶、commit の分け方）。

## 依存

- p004（PCI の口、`KERN_SYSTEM_SLEEP` の devices だけの mode）。
- HAL は不要（H1〜H4 の承認を待たない）。
