<!-- awesome-plan project=zedbsd record=ws052p001 -->

# ws052-p001: 調査と設計（電源管理、S0i3）

Phase ID: `ws052-p001`
Parent: [WS052](../ws.md)
Status: in-progress（2026-10-04、P1 generation16。[design.md](../design.md) 第 1 版。レビューと §10 の判断待ち）
Phase disposition: normal
Queue: q681 / q681-i01（P1）

## 経過（2026-10-04）

- 5330 の DSDT の逆アセンブルで LPS0 の device `\_SB.PEPD`（`INT33A1`/`PNP0D80`）、Intel の UUID の `_DSM`（function 0 = 0x7F、3 display off、
  4 display on、5 entry、6 exit）、Microsoft の UUID（空の実装）、`S0ID` の前提を確かめた。FACP・LPIT は取り出していない。
- 今の zedBSD に無いもの: MWAIT の idle（`hal_cpu_idle` は hlt だけ）、tick の停止、device の suspend・resume の口、wake の GPE だけを有効にする口。
  HAL の API の追加が要る（承認が要る）。
- [design.md](../design.md) を書いた（仕組み、5330 の事実、device の表、CPU と timer、`/dev/system` の口、確かめ方（PMC の SLP_S0 の residency）、
  Phase の案 p001〜p006、判断の点）。

## 再開点

- design-reviewer の敵対的レビュー、§10 の判断（HAL の差分、契機、入れない時、FACP・LPIT の取り出しの許可、beta1 の範囲）を Q1 経由で。
