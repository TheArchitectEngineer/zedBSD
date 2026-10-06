<!-- awesome-plan project=zedbsd record=ws090-p018 -->
# ws090-p018: 設計 — pointer の追従の再描画を一定の frame rate に（hover・drag の範囲選択）

Parent: [WS090](../ws.md)
Status: test-wait（T1 依頼中。q791-i01、P1、2026-10-06 実装済み。Files の host での一致の確かめは安全の判定で保留、下の「残り」）
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

## q791-i01（P1、2026-10-06）: 測定と実装

### 測定（host、Settings の描画を perf で）

Settings の host の描画（`plan/ws089/tests/host-build.sh` の settings-render、Wi-Fi の頁、1180x800、host の -O2）: 1 frame の全体の再描画は約 7 ms
（host の CPU）。時間の大半は **CPU の canvas の半透明の角丸の矩形の塗り**（`fm_canvas_round_gradient` → `canvas_run` → `canvas_blend_premultiplied`、
全体の 55〜60%）。glass の card・行の地が半透明なので、画素ごとに 4 channel の割り算付きの合成をしていた。icon の rasterize や blur ではない。
target の build は -Os で CPU も遅いので、1 frame は数十 ms の見込み。**全ての app で、pointer の hover が変わるたびに窓の全体を CPU で描き直していた**
（kl_ui の app は pointer の移動の event ごとに）。

### 直し

| 変更 | commit | 効果（host） |
| --- | --- | --- |
| `files/canvas.c`・`libkeiland/ui/canvas.c` の `canvas_blend_premultiplied` を 2 channel ずつ 16 bit の lane で計算（÷255 は正確な `(t + (t >> 8) + 1) >> 8`、飽和も同じ） | 82bb75de | 旧と新の結果は 0 から 255 の全ての組と 5000 万の乱数の組で完全一致（scratch の試験）。full frame は約 25% 速い |
| Settings: hover の変化は前と今の行（hit の矩形 ＋ 6 px）だけを clip して描き直す（`hover_pending`・`hover_damage`、glass の時はその矩形だけ透明に） | 82bb75de | hover の 1 frame は約 0.2 ms（full は約 7 ms）。**画素は full と完全一致**（wifi・ethernet・home × light・dark × 3 点、settings-render の新しい `peek=` で確認） |
| Files: hover の変化と rubber band の drag は、前と今の hover の矩形、前と今の band、選択の変わった item の矩形だけを描き直す（`damage_pending`・`fm_ui_damage`） | 82bb75de | build は通る。host の描画での一致は**未確認**（下） |
| Mail・Calendar・Phone: pointer の移動は widget の hover が変わる時か button を押している時だけ frame を描く（今までは移動の event ごとに全体を再描画） | 7897390b | — |

host の描画の道具: `plan/ws089/tests/host-render.c` と `plan/tools/files/host-render.c` に `peek=PATH`（前の action が描いた frame を描き直さずに書く）を追加、
Files の renderer は damage の frame も描く。

確認: build（files・settings・phone・mailer・calendar、zedBSD）warning 0、keiland-linux.mk exit 0、`plan/ws170/tests/run-host-phone.sh` PASS。
QEMU は T1 に依頼。

## 残り

- **Files の部分の再描画の画素の一致は未確認**: `plan/tools/files/host-run.sh` が中に `rm -rf "$home"` を持つため、Claude Code の安全の判定で実行を止められた
  （2026-10-06、Q1 に報告済み）。承認の後に `host-run.sh --start=HOME 動作 peek=A` と `… draw=B` の一致を確かめる。
- Mail・Calendar・Phone の hover も部分の再描画にする（kl_ui に hot の矩形を出す口が要る）。desktop（compositor）の範囲選択の frame の丸めは未着手。
- frame の callback での集約（設計の 2）は、描画が軽くなったので未実施。測った遅れ（注入から再描画の log まで）は未測定。
