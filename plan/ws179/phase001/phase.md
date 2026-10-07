<!-- awesome-plan project=zedbsd record=ws179-p001 -->

# ws179-p001: アクセントカラーの選択

Status: in-progress（q833、P1。2026-10-07 設計の第 2 版（design-reviewer の blocking 4・should-fix 10・minor 6 を反映）、人の判断 6 点（design.md §10）を Q1 経由で待つ）
Disposition: normal
Parent: [WS179](../ws.md)

## 範囲

ws.md の目標のとおり（8 色の固定、ライト・ダークで contrast を確かめた値、Settings の Appearance、libkeiland と compositor の UI が従う）。KL_VERSION は「次の番号」。

## 確認

host の描画（各色 × ライト・ダーク）、build、T1 で Settings の Appearance で色を変えた時の Files・Settings・bar の撮影。

## 記録

- 2026-10-07 範囲（Q1 の ACK）: p001 は libkeiland・compositor の UI・Settings・Files。6 app は p002。App Home の app の tile の色は従わせない。
- 設計: [design.md](../design.md)（§1〜§8 が第 1 版、§9・§10 が review の反映と人の判断。§9 が §2〜§7 を読み替える）。
