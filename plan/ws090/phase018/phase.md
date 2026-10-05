<!-- awesome-plan project=zedbsd record=ws090-p018 -->
# ws090-p018: 設計 — pointer の追従の再描画を一定の frame rate に（hover・drag の範囲選択）

Parent: [WS090](../ws.md)
Status: planned（2026-10-06 Q1 の設計の第 1 版。実装に取りかかれる）
Disposition: normal
Related: [BUG-221](../../bugs/BUG-221.md)・[BUG-226](../../bugs/BUG-226.md)

## 由来（ユーザー、2026-10-06 UAT）

BUG-221「マウス移動イベントすべてで再描画していることがインタラクションの遅れの原因だと思いますが、これは概ね一定のフレームレートに丸めてアニメーションするのが…30fpか15fpsでいいと思います。」BUG-226「Mailアプリの左ペインで…描画が遅れます…何かGPU描画でない合成とかをやっている気がしました…左ペインのあるFiles,Mail, Calendar,Settings、すべて修正対象です。」

## 設計

1. **まず測る**: 左の pane の hover の 1 回の再描画で、CPU の時間（行の描画・icon の rasterize・文字の rasterize・blur）と GPU の時間を分けて log（`KL PERF redraw ms=... cpu=... upload=...`）。選択の色の地に icon を CPU で描き直している（毎回の rasterize や texture の作り直し）なら、それを cache する（icon と文字は 1 回だけ texture にし、選択の色は地の矩形で描く）。
2. **frame rate の丸め**: libkeiland の window の再描画の要求を「dirty にするだけ」にし、実際の描画は wl_surface の frame の callback が来た時に 1 回（最新の pointer の位置で）。pointer の移動の event では描かない。callback が来ない間に要求が続いても描画は 1 回。上限は output の更新の速さ、重い app は 30 fps（33 ms）に間引く option。
3. **部分の再描画**: hover の行が変わった時は、前の行と新しい行だけを damage にする（全体を描き直さない）。
4. 対象: Files（範囲選択の drag・左の pane）・Mail・Calendar・Settings（左の pane）・desktop の範囲選択（compositor、同じ考えで compositor の frame に合わせる）。

## 試験と Phase

- host: 要求の集約（10 回の移動で描画 1 回）。
- AAT・実機: `apps.files.drag-select-follow`（注入の移動と描画の log の遅れ）、ユーザーの感触。
- 実装: p018a（測定と CPU の合成の除去、1 LW）、p018b（frame の callback での集約と部分の再描画、各 app、0.7 LW）。
