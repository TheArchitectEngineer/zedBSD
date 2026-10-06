# ws090-p023: Files・Settings の残りの自前の UI 部品を libkeiland の部品へ

Status: planned（2026-10-06 Q1 が作成）
WS: [WS090](../ws.md)
Related: ws090-p007・p009・p010（q817、canvas と文字の欄）

## 出典

2026-10-06 ユーザー（クリック）「置き換える（別の Phase）」: Files と Settings の残りの自前の UI 部品（Settings の button・switch・slider、Files の list・sidebar・dialog・chip）も libkeiland の部品に置き換える。見た目の差は libkeiland の部品に合わせ、前後の撮影をユーザーに見せる。

## 範囲

- q817（p009 → p010 → p007）の後。Settings の button・switch・slider・card・row、Files の list・grid・sidebar・dialog・chip を libkeiland の部品（ws090-p005 の部品、kl_ui）へ。足りない部品・機能は libkeiland に足す。
- 試験: host の描画の前後の比較、既存の Settings・Files の試験の回帰、T1 で前後の撮影をユーザーへ（`build/review/`）。
