<!-- awesome-plan project=zedbsd record=ws035p138 -->

# ws035-p138: 全画面・最大化を解いた窓の title bar を system bar の下に収める（BUG-114）

Phase ID: `ws035-p138`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-30、サブエージェント、worktree `wt/ws035`。QEMU の Venus、実機は未実施）
Phase disposition: normal
Bug: [BUG-114](../../bugs/BUG-114.md)
Queue: なし（2026-09-30 main の割り当て。ユーザーの報告「Notesをスワイプでフルスクリーン起動したあと、Escキーでフルスクリーンを解除すると、
タイトルバーがスクリーン上部のバーにかぶってしまい、移動操作ができなくなります。スクリーンサイズを求めて、画面の中央に配置するのがいいかも。」）

## 原因

全画面で始まった窓（右上の角の swipe が起こす `/bin/notes --fullscreen`）は、map のときに `place_window` が (0,0) に置き、
`zwl_window_enter_fullscreen` の記録する戻り先（`window_x/y`）も (0,0) のまま。Esc（Notes の `xdg_toplevel.unset_fullscreen`、`protocol.c`）で
(0,0) に戻り、浮いた title bar（本体の 52 px 上）が system bar の下・画面の外になって掴めなかった。

## 実装

- `zwl.h`: 窓に `placed`（窓として置かれた。`window_x/y` が戻り先として使える）と `place_pending`（次の、出力と違う大きさの image で中央に置く）。
- `display.c`: `place_window` が窓として置くときに `placed = 1`。`adopt_commit` で `place_pending` の窓は、出力と違う大きさの最初の image で
  `zwl_window_centre`（新: glass では作業の領域の中央に置き `zwl_glass_fit` で収める、plain では出力の中央）に置く（log `ZWL WINDOW centred`）。
- `protocol.c`: `zwl_window_leave_fullscreen`（新、unset_fullscreen から呼ぶ）: `placed` なら戻り先に戻し、glass では `zwl_glass_fit` で作業の領域に
  収める。置かれたことが無ければ今の大きさで中央に置き、`place_pending` を立てる（client が窓の大きさで描き直したら中央に置き直す）。log `ZWL WINDOW unfullscreen`。
- `shell.c`: `glass_fit` を `zwl_glass_fit`（公開）にした（作業の領域は system bar と浮いた title bar の下から始まるので、収めれば title bar は bar に重ならない）。
  `window_undock`（最大化を解く）も戻り先を `zwl_glass_fit` で収める。
- 変えていない: IME の file と seat.c の IME の hook（2026-09-30 の指示）。
- 規約: `style-check.py display.c protocol.c shell.c` 0、`git diff --check` 0。

## 検証

**QEMU（amd64、Venus の guest 1280x800）**。この Phase の確認は Notes の入った ws079 の lean image（`plan/ws079/tests/build-notes-image.sh build/amd64` →
`build/p138-notes.img`、desktop の warning 0）。graphical の login の image には `/bin/notes` が無い（`ZWL CORNER notes missing errno=6`）。

`plan/ws035/tests/zdesktop-p138.sh`（新）→ `p138: PASS`:
1. swipe（QMP の pointer、右上から左下）で Notes が全画面で起動 → Esc: `ZWL WINDOW unfullscreen ... placed=0`、Notes が 1024x690 で描き直した後
   `ZWL WINDOW centred surface=9 x=128 y=98`（左右の中央 640、y は作業の領域の上端 98 = title bar が system bar の下）。
2. title bar の空いた所を drag: `ZWL GLASS moved x=228 y=158`（動かせる）。
3. もう一度 swipe（compositor の側の経路: 既存の窓を `zwl_window_enter_fullscreen`）→ Esc: `unfullscreen x=228 y=98 placed=1`
   （戻り先 y=158 では高さ 690 の本体が作業の領域の下をはみ出すので、`zwl_glass_fit` が上へ寄せた。x は元のまま）。

画面（`build/ws035-shots/p138/`）: `fullscreen.png`、`unfullscreen.png`（中央の Notes、title bar と menu が見える）、`moved.png`、`back.png`。

回帰（graphical の login の image `build/p138-after.img`、同じ source）: `zdesktop-p052.sh`・`zdesktop-p053.sh`・`zdesktop-p076.sh`（maximize・unmaximize を含む）PASS、
boot test PASS（`build/ws035-shots/p138-20260930-boot-test.png`）。build の desktop の warning 0。

試験の途中の失敗（試験の側）: login の image に Notes が無かった。title bar の中央が Notes の Tool の menu の上で、drag が menu を開いた。
戻り先の y を収めた後の値で比べるように直した。

未実施: 実機、touch での swipe（pointer で注入）、変更前の image での Notes の再現（変更前の login の image には Notes が無く、notes の image は変更後だけ build した。
原因は code から）。

## Resume point

2026-09-30: cleared。
