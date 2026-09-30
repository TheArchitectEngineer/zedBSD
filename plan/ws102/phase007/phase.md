<!-- awesome-plan project=zedbsd record=ws102-p007 -->

# ws102-p007: 作業の領域（L2 の (d)）

Status: cleared（2026-09-30、QEMU の Venus。実機は未実施。(d) の animation の frame の間隔と client の新しい buffer の時間は L3 で測る）
Disposition: normal
Parent: [WS102](../ws.md)
Queue: main の依頼（2026-09-30、p020 の後。shell.c・desktop.c の変更は `zwl_keyboard_reserved` を引く所に絞る（P6 が shell.c の Wiseview の周りを作業中））

## 範囲と受け入れ

design §2.8（2026-09-30 ユーザーの方針）の作業の領域を作る。

- keyboard が出ている間は、作業の領域を panel の分だけ縮める（flick は右の列、QWERTY は下の行）。
- 最大化した窓は、最大化のまま animation で縮めて戻す。
- 浮いた窓は大きさを変えず、animation で収まる位置へ動かす。はみ出た分はそのままにする。
- 閉じたら、compositor が動かした窓だけを元の位置へ戻す。
- 全画面の窓は大きさを変えない。
- keyboard の inset の知らせ（`keiland_keyboard_inset_v1`）は p015 の範囲。

受け入れ（design §3 の L2 の (d)）:

- QWERTY・flick を開くと、最大化した Text Editor が keyboard に重ならない大きさに縮む（log の configure と画面）。
- 浮いた窓は収まる位置へ動く。

## 実装

| file | 内容 |
| --- | --- |
| `keyboard.c` | `zwl_keyboard_reserved(right, bottom)`: 開いた panel が取る列か行（flick は幅、QWERTY は高さ）。<br>`zwl_keyboard_reserved_now`: slide の今の分（出る時は増え、戻る時は減る）。<br>panel を開く・閉じる・替える時の `keyboard_work_area`:<br>・最大化した窓: `window_width/height` を縮んだ大きさ（閉じたら全体）にして、`zwl_window_send_configure` を 1 回だけ送る。<br>・浮いた窓: 右端と下端が領域に入るよう、大きさはそのまま位置だけを決める。上端（body）は `ZWL_GLASS_TOP`、左端は margin より外に出さない。大きすぎる窓は keyboard の下にはみ出る。<br>・浮いた窓の位置は、slide（200 ms・ease-out）に合わせて tick で補間する。閉じたら、動かした窓を元へ戻す。その間に利用者が動かした・最大化した窓は戻さない（`work kept`）。<br>・覚えた窓は address を比べるだけで、live な窓の中に見つかるまで辿らない。<br>・全画面の窓は変えない。<br>log: `ZWL OSK work-area right= bottom=`・`work docked surface= width= height=`・`work moved surface= from= to=`・`work back`・`work kept` |
| `shell.c`（`zwl_keyboard_reserved` を引く所だけ） | `zwl_glass_space`（窓の置き場・fit・configure_bounds）から `zwl_keyboard_reserved` を差し引く。<br>`docked_rect`（最大化の窓を描く場所）から `zwl_keyboard_reserved_now` を差し引く。そのため、最大化の窓は panel と一緒に animation で縮み、戻る。<br>`DOCK_TOP` を `ZWL_GLASS_DOCK_TOP`（zwl.h）に（keyboard.c と同じ値を使うため） |
| `desktop.c` | `desktop_place`（desktop の icon の層）から `zwl_keyboard_reserved` を差し引く（開閉の時に 1 回だけ configure） |
| `zwl.h` | `ZWL_GLASS_DOCK_TOP`、`zwl_keyboard_reserved`・`_now` の宣言 |

IME の file と seat.c は変えていない。

main の取り込みの後（P6 の p015、inset の知らせ）に、次の順にした。p015 の phase.md の引き継ぎどおり。

1. 窓ごとの終わりを決める（最大化の窓の大きさ、浮いた窓の行き先）。
2. `zwl_keyboard_inset_notify(server, panel)` で inset を知らせる（閉じる時は NULL）。動く窓は、この間だけ行き先に置いて計算させる。
3. 最大化の窓に configure を送る。

`keyboard_open`・`zwl_keyboard_close` に P6 が足した notify の行は、`keyboard_work_area` の中の 1 回に一本化した。両方を残すと 2 回知らせることになるため。

`inset.c` の `inset_covered` は、最大化の窓では、configure で伝える大きさ（`window_width/height`）で覆う幅を計算する。image はまだ前の大きさなので（Q1 の了解: 関数を広げてよい）。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make … bin/wayland …` | rc 0、warning 0 |
| style | `plan/tools/style-check.py keyboard.c shell.c desktop.c`、`git diff --check` | 0 件 |
| guest: 作業の領域（新しい手順 workarea、pen image の複写） | `osk-guest.sh build/ws102-shots/p007-final install start pointer flick edges touch send close qwerty hand extra workarea` | PASS（それまでの全ての手順を含む） |
| guest（1920x1080） | `… install large` | PASS |
| 回帰 | WS079-p010、WS099 の C9（10 本）、boot test | PASS（main の取り込みの後にもう一度: `build/ws102-shots/p007-final2`、PASS） |
| p015 の inset の試験（取り込みの後） | pen image の複写にこの compositor と Text Editor・library を入れ、`plan/ws102/tests/inset-guest.sh build/ws102-inset.img build/ws102-shots/p007-inset` | PASS: QWERTY で `ZWL INSET … bottom=314 reason=2`、caret の行は keyboard の上の範囲の中央から 0.54 行（±1 行以内）、閉じて `reason=0`、wlshm は inset 無しで動く |

手順 workarea で確かめたこと（Text Editor を最大化し、その上に浮いた wltest 500x400）:

- QWERTY: `work-area right=0 bottom=336`。
  - 最大化の Text Editor に configure `1280x426` を 1 回送った（全体は 1280x762）。画面では、Text Editor の下端（status の「Ln 1, Col 1」）が keyboard の上に見える（`workarea-qwerty.png`）。
  - 浮いた窓は `390,243 → 390,98` へ動いた。上端が領域の上端に当たり、下ははみ出る。
- 閉じると、最大化の窓は `1280x762` に戻り、浮いた窓は `390,243` へ戻った。
- flick: `work-area right=318 bottom=0`。最大化の Text Editor は `962x762` になった。右の列に重ならない（`workarea-flick.png`）。
- もう一度 QWERTY を出し、浮いた窓の title bar を利用者が drag して動かしてから閉じると、`work kept`（戻さない）になった。

## 見つけたこと・制限

- (d) のうち、「animation の frame の間隔 ≤ 20 ms、client の新しい buffer が animation の終わりから 100 ms 以内」は、design のとおり L3 で測る。この guest は 7 枚/秒前後で、200 ms の動きは 1〜2 frame になる。
- slide の途中で窓を最大化すると、その窓の大きさは途中の値になる（`docked_rect` が slide の今の値を使うため）。まれな場合で、次の開閉で直る。
- flick から QWERTY へ直に替えると、浮いた窓の「元の位置」が途中の位置になる場合がある（開いた panel ごとに覚えるため）。
- keyboard の inset の知らせ（caret を中央へ寄せる）は p015。
- 実機・Windows の上の QEMU は未実施。

## Resume point

2026-09-30: cleared。次は main の指示（L2 の残り: p015・p016・p017 ほか）。
