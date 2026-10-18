<!-- awesome-plan project=zedbsd record=ws259 -->

# WS259 RTL8822B の表（rtl8822b-tables.inc）を値の引用に書き換える

- ID: ws259、Master: [master.md](../master.md)、Primary Milestone: MG005
- Status: planning（2026-10-18 Q1 作成。担当は WS244 の後に割り当てる）
- 起点（2026-10-18 ユーザー、クリック）: WS258 の監査で、`src/drivers/wifi/rtl8822b/rtl8822b-tables.inc` の先頭に上流の著作権表示と抜粋の宣言があった件に「WS244 と同じく書き換える」。
- 規則: [Guardrail](../guardrail.md) の「MIT・GPL の出典のデータの引用」。値はデータとして block ごとに `Values referenced:` で引用し、上流の名前は zedBSD の名前に、file の先頭の表示・出典の一覧を外す。AGENTS.md の「RTL8822B の `.inc` は license を分けるために独立させており、別の file のまま保つ」は守る（file は分けたまま）。

## 完了の条件

1. rtl8822b-tables.inc の先頭に上流の著作権表示・抜粋の宣言・出典の一覧が無く、表は block ごとの引用になっている。
2. 表の値（driver が書く register の値と順序）は変わらない（変更の前後で表の中身を機械で比べる）。
3. build（amd64 の既定）warning 0。WiFi の実機の確かめはユーザーの一覧に載せる。
4. license の package に RTL8822B の分があれば内容に合わせて直す。

## Phase

| Phase | 目的 | 状態 |
| --- | --- | --- |
| p001 | 書き換えと表の値の比べ | planning |
- 決定（2026-10-18 ユーザー、クリック）: 実機の WiFi の確かめは「実機は残して待つ」（host の表の比べと build まで済ませ、実機の Phase は in-progress（実機待ち））。
