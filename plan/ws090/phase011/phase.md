<!-- awesome-plan project=zedbsd record=ws090-p011 -->

# ws090-p011: Terminal・Notes の窓を libkeiui へ

Status: uncleared（2026-09-30、サブエージェント P7、worktree `ws090-kui`。main に入れた。残りの試験は未実施）
Disposition: normal
Parent: [WS090](../ws.md)
Queue: なし（2026-09-30 Q1 の割り当て、p008 の次）
依存: p004（cleared）

## 範囲（Q1 の判断 2026-09-30）

- 窓だけ: Terminal・Notes の window.c の Wayland の部分（registry・xdg・seat・pointer・keyboard・touch）を `kui_window`（`KUI_PRESENT_NONE`、app が
  自分の Vulkan で描く）に替える。main.c・render.c・touch.c の中身は変えない。
- scroll の model（両方の touch.c の `keiland_scroller` を `kui_scroll` へ）は **この Phase から外し、p015（案）** にした（touch の挙動が変わる
  危険、WS081 の host 試験 2 本の compile の列の変更が要る）。

## 実装（commit 31d88d38）

- **libkeiui（KUI_VERSION 11）**: `kui_window_dispatch_fds`（compositor と app の他の descriptor（Terminal の shell の pty）を一緒に待つ。
  `kui_window_dispatch` はこれを 0 個で呼ぶ）、pointer の motion と button の `time_us` を compositor の時刻に（指と同じ `window_stamp`。
  Notes の線の点の時刻が今までどおり compositor の時刻になる。`time_us` を pointer で読む app は他に無いことを grep で確かめた）、
  `kui_window_options.fullscreen`（最初の configure の前に全画面を求める。Notes の `--fullscreen` の起動で最初の configure が全画面になる）。
- **Terminal**: `window.c` 1180 → 521 行。`kui_window` の event を今の app の入れ物に直す: key の press と repeat → shell への byte（keys.c、
  Shift+PgUp/PgDn は scroll）、pointer の press・release・中 button・motion（motion は続けば 1 つにまとめる、press の serial）、wheel は
  zdesktop の 1 notch（15×4 = 60 px）で notch に直す、指は `terminal_touch_event`（時刻は compositor の ms）、大きさ・close・全画面。
  clipboard・drag and drop（clipboard.c）と PRIMARY（primary.c）は app に残し、Terminal の registry で manager を bind して `kui_window` の seat
  に device を作る（`kui_window` に drag and drop が無いため。zdesktop は client の一番新しい data device に drag を送るので Terminal の device が受ける）。
  `terminal.h` の `struct terminal_window` から Wayland の object と repeat・bounds を除いた。`main.c` は key の repeat の待ち時間だけ
  `terminal_window_repeat_wait` に替えた。
- **Notes**: `window.c` 1046 → 435 行。pointer の左 button の線（motion は押している間だけ、compositor の時刻）、key は最初の press だけ（Notes
  は repeat しない）、指、大きさ・close・全画面。pen の tablet（tablet.c）は app に残し、Notes の registry で manager を bind して `kui_window` の
  seat に tablet の seat を作る。全画面の起動は `options.fullscreen`。
- build: 両方の Makefile の依存に `desktop/libkeiui`、`platform/amd64/vmunix.mk` の terminal・notes の link に `libkeiui.so`（Q1 の許可）。
- 大きさ: `/bin/terminal` 108 KB → 97 KB、`/bin/notes` 147 KB → 139 KB。

## 試験

| 試験 | 前 | 後 |
| --- | --- | --- |
| build（`-Werror`、libkeiui・terminal・notes・viewers・textedit） | — | exit 0、warning 0 |
| `plan/ws081/tests/run-termtouch.sh` | —（touch.c は変えていない） | ok（20） |
| `plan/ws081/tests/run-notestouch.sh` | — | ok（52） |
| `plan/ws079/tests/run-notes-host.sh` | — | ok |
| WS081 の guest 試験（`p011`・`p013`・`p014`・`p015`、この tree の demo の image、`build/ws090/p11-guest.sh`） | p011・p013・p014 PASS、p015 は SSH の put の失敗で FAIL（環境、再実行していない） | **4/4 PASS**（`build/ws090-shots/p11-new/`） |
| Terminal の zdesktop の回帰（`build/ws090/zterm-guest.sh`、同じ image） | 未実施 | p079（clipboard）・p093（選択と drag out）・p100（PRIMARY）・p114（scrollback）・p086（tabs）**PASS**。p088 **FAIL**（Files から Files の drag の段と Terminal への drop の段がともに MISSING。この image に p088 の前提の sample home が無い可能性。前の binary では未確認） |
| 規約 | — | `style-check.py`（terminal・notes の window.c と header、libkeiui の window.c）違反 0、`git diff --check` 0 |

## 残り（未実施）

- 理由: ユーザーの指示で優先を下げた（週間の使用量、2026-09-30）。ユーザーの指示で main には入れた。
- 再開の条件: ユーザーが再開を言うとき。残りの試験を流す。
- 残りの試験: demo-s8-s9 の S8（Notes の全画面と pen）、p088 の原因の切り分け（前の binary と files の image で比べる）、WS081 p015 の前の
  binary での再実行、WS099 の C9、boot test、画面（`build/ws090-shots/`）の確認。
- 後の候補: p015（案）Terminal・Notes の scroll を `kui_scroll` へ。Terminal の clipboard・PRIMARY を `kui_window` の物へ寄せる（drag and drop を
  `kui_window` が持てば clipboard.c を消せる）。
