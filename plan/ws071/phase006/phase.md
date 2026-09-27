<!-- awesome-plan project=zedbsd record=ws071p006 -->

# ws071-p006: Home の dashboard

Phase ID: `ws071-p006`
Parent: [WS071](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main の Queue への反映は main の session）

## 範囲

[design.md](../design.md) §3.1、spec §5、§19、§28: Home の場所を $HOME の一覧から dashboard に替える。hero card（壁紙
`/usr/share/zdesktop/wallpaper.ppm` を card の形に切り出して縮小、無ければ描いた山と湖、時刻の挨拶と利用者名、今日開いた
file の数と空き容量）、folder の card（Documents・Pictures・Music・Movies・Downloads・Projects、項目数）、Recent Files
（libzdesktop の recent、click でその folder に行って選ぶ、double click で開く）、Recent Folders（file manager の中の list）、
「Show all →」（Home の folder の一覧、Recents）。

## 受け入れ

1. host の画面と Venus（QEMU）の画面・log で dashboard が出て、card・recent・Show all が動く。
2. warning 0、`style-check.py` 0。回帰（p002〜p005）PASS。

## 結果（2026-09-27）

cleared。

- 実装: `ui-home.c`（新規: 集める・描く・click・PPM の読み込み・描いた風景・最近の folder の list）、`ui.c`（Home は一覧を読まず
  dashboard を集める、folder に行くたびに最近の folder へ、hero の画像の解放）、`ui-grid.c`（Home は panel 全体を dashboard に）、
  `ui-input.c`（card・recent・Show all の click）、`actions.c`（dashboard は folder でない: 貼り付け・新しい folder は何もしない）、
  `main.c`（`--wallpaper=PATH`）、`files.h`。design.md §6.2 に最近の folder の置き場の変更を書いた。
- 途中で直した不具合: 壁紙の切り出しの位置を行の途中の pixel から始めていて、絵が縦にずれて継ぎ目が出た（行の単位に）。
- host の画面: dash0.png（壁紙なしの描いた風景）、dash.png（壁紙、挨拶、folder の card、Recent Files 2 件）を見た。
- **QEMU（Venus）**: `files-p006.sh` **PASS**（dashboard、Pictures の card、file を開いて Home に Recent Files、その行の click で
  Documents に行って選択、Show all で Home の folder の一覧）。回帰 `files-p002.sh`（Home の期待を dashboard に更新: 項目 0、
  Documents は card で開く）〜`p005.sh` PASS。画面 build/ws071-p006/dashboard.png・dashboard-recent.png を見た（壁紙の hero、
  「Good morning, root」、6 つの card、Report.pdf の行）。
- build warning 0、`style-check.py` 0、host の build 0。
- 実機（i915）: 未実施。
- 制限: mockup の宣伝文句の card（右下の絵と文）は置かない（design §15-8）。dashboard の pin（おすすめ・ピン留め）は Favorites で
  代える。
