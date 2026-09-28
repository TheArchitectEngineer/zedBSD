<!-- awesome-plan project=zedbsd record=ws035p121 -->

# ws035-p121: Keiland の見える所に残る旧名（zdesktop・zedBSD・zed）の除去（WS078 の残り）

Phase ID: `ws035-p121`
Parent: [WS035](../ws.md)（改名は [WS078](../../ws078/ws.md) の p004 の残り）
Status: cleared（2026-09-29、サブエージェント。QEMU の Venus と host。実機は未実施）
Phase disposition: normal
Queue: なし（2026-09-29 main の割り当て「WS035/WS078 の p121〜: 見える旧名の除去」、demo critical）

## 範囲（依頼）

- compositor・Keiland の app の、ユーザーや client に見える旧名（zdesktop・zedBSD・zed）を grep で洗い出し、見えるものから直す。
  例: compositor の `wl_output` の名前 `ZDESKTOP-1`（p116 の main への一覧の 4）。
- 試験が grep する log の印（`ZWL`・`ZFILES` 等）は変えない。一覧にして main に報告するだけ。
- kernel の内部名 zedbsd のシンボル、source の copyright の header（`* zedBSD`）は残す（WS078 の決定）。
- 範囲外: compositor の入力の file（touch.c・tablet.c・corner.c・input.c）、libkeiland/motion.*、browser・libbrowser、kernel・base の userland。

## 洗い出し（2026-09-29、`userland/desktop/`、browser・libbrowser を除く）

文字列の定数で旧名を含むもの（注釈を除く）:

| 所 | 前 | 誰に見えるか | 後 |
| --- | --- | --- | --- |
| `wayland/protocol.c` の `wl_output.name` | `ZDESKTOP-1` | client（browser・toolkit が画面の名前として出しうる）。`zdesktop-p078.sh` が期待値に持つ | `DISPLAY-1` |
| `wayland/protocol.c` の `wl_output.description` | `zdesktop output WxH` | client（同上） | `Display WxH` |
| `wayland/protocol.c` の `wl_output.geometry` の make・model | `zed`・`fullscreen` | client（同上） | `Unknown`・`Unknown` |
| `wayland/compose.c` の Vulkan の `pApplicationName` | `zdesktop` | GPU の driver の記録（UI には出ない） | `wayland` |
| `wayland/main.c` の usage と誤りの文 | `usage: zdesktop ...`・`zdesktop: --greeter needs ...` | `/bin/wayland` を手で起動した人 | `wayland` |
| `wayland/keymap.c` の xkb の節の名前 | `xkb_keycodes "zedbsd"` 等 4 つ | client に渡す keymap の文（xkbcli 等で見える） | `evdev`・`basic`・`basic`・`us` |
| `plan/ws035/demo/rc.conf`（p069 の古い demo の image）の hostname | `zedbsd` | shell の prompt 等 | `kei` |

他の app（files・terminal・notes・pdfviewer・mview・sessiond・libkeiland・artwork）の文字列には旧名は無い（WS078 p004 で済み）。
注釈の `zdesktop` は約 250 箇所（wayland・files・terminal・pdfviewer 等）で、画面には出ない。

## 決定

- `wl_output` の名前には Kei を入れない。`name`・`description`・`make`・`model` は表示装置（connector と monitor）を表す値であり、
  OS の名前を入れると「monitor の製造元が Kei」と読める。compositor は connector の種類も EDID も知らない（`/dev/gpu` の scanout だけ）ので、
  中立な `DISPLAY-1`、description は `Display WxH`、make・model は `Unknown`（他の compositor が不明のときに送る値）にする。
- Vulkan の application の名前は program の名前 `wayland` に（内部名の Keiland は UI に出さない規則。GPU の記録にも program 名が分かりやすい）。
- keymap の節の名前は内容を表す名前に: keycodes は evdev の番号 + 8 なので `evdev`、types・compatibility は `basic`、symbols は `us`。
- 注釈の旧名は、この Phase と p122・p123 で触る file の中だけ直す。一斉の置き換えは他の subagent（WS081 の入力、WS074 の browser）の
  作業中の file と衝突するので行わない（残り）。

## 変えないもの（試験の log の印・protocol の識別子。main への一覧）

試験が grep するので今回は変えない。変えるなら全試験を同時に直す必要がある。

| 印・識別子 | 出す所 | 数（printf の行） |
| --- | --- | --- |
| `ZWL ` | compositor（wayland）の log | 約 270 |
| `ZTERM ` | terminal | 36 |
| `ZFILES ` | files | 12 |
| `ZBROWSER ` | browser（WS074 の範囲、触らない） | 19 |
| `zed-unicode`（X の core font の名前） | xserver の ListFonts・OpenFont | retro の zwm・zterm・zshell・Xzed が名前で開く（`userland/retro/`、範囲外） |
| log の file の名前 `/tmp/zdesktop.log`（試験の中）、`/var/log/zdesktop.log`・service `zdesktop`・`/etc/keiland/run-zdesktop.sh`（p069 の古い demo の image、`plan/ws035/demo/`） | 試験と古い demo | 今の demo（WS075 の HDMI の image、graphical login）は `sessiond.log`・`greeter.log`・`session.log` で旧名なし |

`SESSIOND `・`NOTES `・`PDFVIEWER `・`MVIEW `・`X11 `・`X11SERVER `・`EGLTEST `・`WLTEST `・`WLSHM `・`VKDEMO `・`SEATPROBE ` は旧名ではない。

範囲外で見える旧名（main への一覧）:

- `userland/base/getty/main.c` の hostname の既定 `zedbsd`（hostname が無いとき）。base の userland。
- `userland/base/sh/main.c` の `TERM=zed`（sh が端末の種類を決めるとき）。terminfo の名前でもあり、base の userland。
- Terminal の `uname -a`（kernel の sysname `zedBSD`、WS078 の決定で残す）。

## 実装（2026-09-29）

- `wayland/protocol.c`: `wl_output.geometry` の make・model を `Unknown`（wire の長さ: make 8 byte・model 8 byte・transform で 48 byte）、
  `wl_output.name` を `DISPLAY-1`、description を `Display WxH`。
- `wayland/compose.c`: Vulkan の `pApplicationName` を `wayland`。
- `wayland/main.c`: usage と `--greeter` の誤りの文を `wayland`。
- `wayland/keymap.c`: xkb の節の名前を `evdev`・`basic`・`basic`・`us`。
- 注釈: 触った file（compose.c・main.c・keymap.c・protocol.c、p123 の home.c・glass.c、p122 の pdfviewer/main.c）の `zdesktop` を「the compositor」に。
- `plan/ws035/tests/zdesktop-p078.sh`: 期待値を新しい値に。`plan/ws035/demo/rc.conf` の hostname を `kei` に。

## 検証（2026-09-29、QEMU の Venus。実機は未実施）

- build: `make ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-demo-venus.mk ZEDBSD_USER_PROGRAMS="wayland notes pdfviewer" …` exit 0、warning 0。
  demo の Venus の image（`plan/ws035/tests/build-demo-venus-image.sh build/amd64`、この worktree）も warning 0。
- `plan/tools/style-check.py`（変えた C の file 全部）違反 0、`git diff --check` 0。
- host: `plan/ws035/tests/p078/run-host.sh`（host の libxkbcommon で keymap を compile して key と modifier を確かめる）PASS。
- QEMU: `zdesktop-p078.sh`（demo の image、greeter の service を止めて）PASS: `SEATPROBE output geometry x=0 y=0 make=Unknown model=Unknown`、
  `output name=DISPLAY-1`、`output description=Display 1280x800`、keymap・repeat・modifier の全段。
  1 回目は greeter の compositor が画面を持ったままで swapchain が作れず FAIL（試験は lean な image を前提）。`service stop greeter` の後に PASS。
- demo の通し（`demo-walk.sh`、1920x1280、利用者 kei、p122・p123 も入った最終の image）: 01〜11 の全段の log の待ちが ok。
  00（loader の splash）は撮れなかった（`FileNotFoundError`、guest の起動直後の QMP の screendump。今回の変更と無関係。再試行していない）。
  画面: `/home/awe/zedBSD-rpi4/build/ws035-shots/p121-20260929-demo-{01-greeter,02-desktop,03-apphome,04-files,05-terminal,05b-xterminal,06-notes,07-pdfviewer,08-wiseview,09-lock,10-unlocked,11-logout}.png`。
- 未実施: 実機（5330 + HDMI）、`boot-test.sh`（desktop の Phase。graphical の起動と login は demo の通しで確かめた）。

## 残り

- 注釈の `zdesktop`（約 240 箇所、wayland・files・terminal・pdfviewer 等）の一斉の置き換え。入力の file（WS081）と browser（WS074）の作業が
  merge された後に、main か 1 つの agent が一度に行う。
- 上の「範囲外で見える旧名」（getty の hostname の既定・sh の TERM）は base の userland の担当で判断。
- 試験の log の印（`ZWL` 等）の改名は、全試験を同時に直す別の Phase が要る（今は変えない）。
- 古い demo（p069 の service `zdesktop`、`run-zdesktop.sh`）は WS075 の demo に置き換わっており、廃止か改名かは main の判断。
