<!-- awesome-plan project=zedbsd record=ws102-p016 -->

# ws102-p016: 右の列の道具の面

Status: cleared（2026-09-30、QEMU の Venus。実機は未実施）
Disposition: normal
Parent: [WS102](../ws.md)
Queue: main の依頼（2026-09-30、p007 の後。worktree `.claude/worktrees/ws102-keyboard`、branch `wt/ws102`）

## 範囲と受け入れ

design §2.10 の道具の面を作る。

- 常に出る列: 直前の app・BS・道具の面の tab。
- 編集の面: 矢印・行頭と行末・頁・選択の toggle・コピー・切り取り・貼り付け・取り消し・やり直し・全選択。
- 編集の操作と直前の app は、P6 の p017 の compositor の口（`zwl_edit_action`・`zwl_edit_state`・`zwl_focus_previous`）を呼ぶ。
- 候補（p012）・履歴（p018）・絵文字（p019）の面は後。

ついでに、p007 の制限（flick から QWERTY へ直に替えた時の、浮いた窓の元の位置）を直す。

受け入れ: Text Editor で「選択 → → ×5 → コピー → 直前の app → 貼り付け」をすると、同じ文字が別の app に入る（3 回とも）。

## 実装（keyboard.c だけ）

- 道具は `keyboard_tools` の表にした（label・種類・行・列・幅・行の列の数）。flick の panel の帯の下、key の上に並べる。
  - 1 行目（常に出る、灰）: 「前の app」「Del」。
  - 2 行目: tab（低い行）。「編集」だけが今の面。「候補」「履歴」「絵文字」は淡い（後の Phase）。
  - 3〜6 行目（編集の面）: ← ↑ ↓ →／行頭・行末・PgUp・PgDn／選択・全選択・取消・やり直し／コピー・切り取り・貼り付け。
- 動き:
  - 移動（矢印・行頭・行末・頁）は key で送る。「選択」が on の間は Shift を付けて送る（選択が広がる）。p017 のとおり、拡張の無い窓でも同じように働く。
  - 取消・やり直し・全選択・コピー・切り取り・貼り付けは `zwl_edit_action`。コピー・切り取りで「選択」は off になる。
  - 「前の app」は `zwl_focus_previous` を呼ぶ。keyboard は閉じない。
  - 焦点の窓が今出来ない操作は、`zwl_edit_state` の答えで淡く描く。押した道具は青。
- log: `ZWL OSK tool label=`・`tool selecting=`・`tool edit action= error=`・`tool previous error=`。
- p007 の制限の直し（`keyboard_fit_floating`）:
  - 前の panel で動かした窓（利用者がその後動かしていないもの）は、最初の位置から新しい panel の行き先を決める。閉じたら最初の位置へ戻る。
  - 新しい panel で最初の位置に収まるなら、戻る動きとして扱う。
  - 覚えた窓は、live な窓の中に見つかった時だけ触る。

IME の file と seat.c は変えていない。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make … bin/wayland bin/textedit …` | rc 0、warning 0 |
| style | `plan/tools/style-check.py keyboard.c`、`git diff --check` | 0 件 |
| guest: 道具（新しい手順 tools、pen image の複写） | `osk-guest.sh … install … tools` | PASS（下） |
| guest: 全ての手順 | `osk-guest.sh build/ws102-shots/p016-final install start pointer flick edges touch send close qwerty hand extra workarea roll tools` | PASS |
| guest（1920x1080） | `… install large` | PASS |
| 回帰 | WS079-p010、boot test | PASS |
| 回帰: WS099 の C9 | criteria image の複写で `criteria.sh … C9` | 9 本 PASS。p076 だけ FAIL（下） |

手順 tools で確かめたこと。Text Editor を 2 つ開いた（a.txt「hello world」が上、b.txt が空）。

- 次を 3 回くり返した: 行頭 → 選択 → → ×5 → コピー → 前の app（b.txt）→ 貼り付け →（次の回のために）前の app（a.txt）。
- log: `tool edit action=0 error=0`（コピー）3 回、`action=2 error=0`（貼り付け）3 回、`tool previous error=0` 5 回。
- b.txt を物理の Ctrl+S で保存すると、file は `hellohellohello` だった（3 回とも入った）。
- 画面: `tools.png`、`tools-pasted.png`（右の列の道具の面と、b.txt に 3 回入った文字）。

p007 の直しを手順 workarea に足して確かめた。

- 浮いた窓を右へ drag して 520,138 に置いた。
- flick を出すと 450,138 へ動いた。
- そのまま QWERTY に替えると、最初の位置から計算して 520,98 へ動いた。
- 閉じると 520,138 へ戻った。

## 見つけたこと・制限

- WS099 の C9 の p076（xdg_toplevel の resize の試験）が、全体の実行で FAIL し、単独では 5 回中 4 回 PASS した（取り込みの後の 2 回の全体の実行と、単独の 5 回）。
  - 落ちる所は、左端を広げる drag の後の画面の判定（`widened.png`）。log の判定は通っていて、画面では窓が広がる途中だった。
  - WS099 の記録にも、以前 1 回だけ不安定だったとある。
  - p016 は keyboard.c だけの変更で、keyboard は開いていない（作業の領域は 0）。p007 の変更（`zwl_glass_space`・`docked_rect`）は、keyboard が無い時は前と同じ値。
  - そのため、画面を撮る時の間合いの不安定さと判断した。p009 の touch.c を含む取り込みの後に目立つかどうかは、main で見てほしい。
- 「選択」は Shift の toggle で行う。拡張のある窓にも select_begin・select_end は送っていない（Text Editor で働いた）。
- 候補・履歴・絵文字の tab は、後の Phase まで淡い。
- 実機・Windows の上の QEMU は未実施。

## Resume point

2026-09-30: cleared。次は main の指示。
