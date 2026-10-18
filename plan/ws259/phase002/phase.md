<!-- awesome-plan project=zedbsd record=ws259-p002 -->

# ws259-p002 規約の全文の見直し

Status: in-progress（2026-10-19 P2 実施済み、Q1 の判定待ち）

- 親: [WS259](../ws.md)。範囲: p001 が変えた `rtl8822b-tables.inc`（全体）と `rtl8822b.c` の名前の付け替えの行。
- 見たこと: file の先頭の copyright と説明（出典を書かない）、型の役割の comment、file-scope の変数（表）ごとの役割の comment、出典は block ごとの複数行の `Values referenced:`（Guardrail、file の先頭には書かない）、上流の名前を code に使わない（comment の出典の中だけ）。`rtl8822b.c` は名前だけの変更で、関数の形は範囲外（driver 全体の規約の見直しは別）。
- 結果: 直す所なし。amd64 の build は p001 の通り warning 0。
