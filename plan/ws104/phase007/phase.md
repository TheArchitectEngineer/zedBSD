<!-- awesome-plan project=zedbsd record=ws104-p007 -->

# ws104-p007: install の path を `userland/desktop/paths.h` の macro に

Status: planned
Disposition: normal
Parent: [WS104](../ws.md)
Queue: なし
依存: p003・p006（patch が p003 の後の tree に当たるように作ってあり、p004〜p006 は `wayland/*.c` を変えるので、それらの後に当てる）
実行者: phase-runner（high）か phase-runner-mid。`plan/` の script の 7 つの `-I.` は他の WS の file なので **main が当てる**

## 目的

desktop の source は、自分たちが install される場所を文字列で直書きしている（`/bin/terminal`・`/usr/share/fonts/keiland.ttf`・`/etc/keiland/apps.conf`・
`/usr/libexec/keiland-ime` など、26 file・61 行）。Linux（WS105）では全てが `/opt/keiland/` の下に入る（決定 D1）。path を 1 つの header の macro にし、
zedBSD の build では**今と全く同じ文字列**になり、Linux の build（`keiland-linux.mk`）は `-D` で上書きする。

## 用意してある物

- [`../patches/p007.patch`](../patches/p007.patch): 新しい `userland/desktop/paths.h` と 26 file の置き換え・include の追加、host の試験の script 7 つの `-I.`。
- [`../patches/p007-table.txt`](../patches/p007-table.txt): 置き換えた全ての行の前と後。
- survey の確かめ（2026-10-01、copy の tree）: 置き換えた 26 個の `.c`（と header を通して影響を受ける `ui.c`・`textedit/text.c`・`browser/view/view.c`）を target で前後に
  compile し、**object が byte で同じ**（文字列が同じ）。host の試験（files `host-build`・`host-default`、ws094 `host-thumb`、textedit `host-core`、ws089 `host-build`、ws074 `host-build`）が通る。
  ws094 `host-desktop` は font の確かめ 2 つで落ちるが、変える前の copy でも同じく落ちる（この Phase の原因ではない）。
- `paths.h` の macro と zedBSD の値: `KEILAND_BINDIR` `"/bin"`、`KEILAND_LIBEXECDIR` `"/usr/libexec"`、`KEILAND_DATADIR` `"/usr/share"`、`KEILAND_SYSCONFDIR` `"/etc"`
  （それぞれ `#ifndef` で囲み、Linux の build が `-D` で上書きする）。path は「macro + 残りの文字列」で書く（`KEILAND_BINDIR "/terminal"` は zedBSD では `"/bin/terminal"`）。

残す物（2026-10-01 の Q1 の決定）:

- `"/bin/sh"`（system の shell）と、command の文字列の先頭の `"/bin/sh "`。
- `userland/desktop/keiland/keiui.h:73` の `KUI_TEXT_EMOJI`（公開の header は repo の `paths.h` を include できない。WS105 p008 で `libkeiui/text.c` の側で扱う）。
- `files/apps.c:111` の `apps_program_folders[]`（Open With の program を探す directory の一覧。WS105 p008 で `KEILAND_BINDIR` を足す）。
- `/tmp/wayland-0`・`/tmp/.X11-unix`・`/run`・`/dev`・`/etc/resolv.conf` などの OS の path、`sessiond/`。

## 手順（`<W>` は `ws104-p007`）

1. 前の文字列を取る（今の build の物）:
   ```
   mkdir -p build/ws104-p007
   make -j64 disk-image > build/ws104-p007/build-before.log 2>&1; echo "make exit=$?"
   for f in build/amd64/bin/* build/amd64/dynamic/*.so; do echo "== $f"; strings -a "$f" | grep -E '^/(usr|etc|bin)/' | LC_ALL=C sort -u; done > build/ws104-p007/strings-before.txt
   ```
2. patch を当てる:
   - phase-runner: `git apply --exclude='plan/*' plan/ws104/patches/p007.patch`、main に「`git apply --include='plan/*' plan/ws104/patches/p007.patch`」を頼む。
   - main: `git apply plan/ws104/patches/p007.patch`
   - **当たらないとき**（p004〜p006 で `wayland/*.c` の行が動いたため）: `git apply --reject` で当たらない hunk を `.rej` に出し、`p007-table.txt` の OLD/NEW の行を文字列で探して手で直す。
     include の追加の場所は下の表。全て直したら `.rej` を消す。
3. 残りが無いこと:
   ```
   grep -rn --include=*.c --include=*.h -E '"/(usr/share|usr/libexec|etc/keiland|bin/|usr/bin/)' userland/desktop \
     | grep -v -e '^userland/desktop/sessiond/' -e '"/bin/sh"' -e '"/bin/sh "' -e '^userland/desktop/paths.h:' -e '^userland/desktop/keiland/' | wc -l
   ```
   `0`。
4. build と warning の数え（[commands.md](../commands.md) §1、`build/ws104-p007/build.log`）。後の文字列が同じ:
   ```
   for f in build/amd64/bin/* build/amd64/dynamic/*.so; do echo "== $f"; strings -a "$f" | grep -E '^/(usr|etc|bin)/' | LC_ALL=C sort -u; done > build/ws104-p007/strings-after.txt
   diff build/ws104-p007/strings-before.txt build/ws104-p007/strings-after.txt && echo STRINGS-SAME
   ```
5. host の試験: `sh plan/tools/files/host-build.sh`、`sh plan/tools/textedit/host-core.sh`、`sh plan/ws089/tests/host-build.sh`（いずれも exit 0）。
6. compositor の基準（commands.md §5。App Home からの app の起動を含む）: 全て PASS。
7. boot test（`OUTPUT=build/ws104-p007/boot`）。
8. commit: `git commit -m WIP -- userland/desktop`（main は `plan/tools/files plan/tools/textedit plan/ws074/tests/host-build.sh plan/ws089/tests/host-build.sh plan/ws094/tests` も）。

## include を足す場所（patch が当たらないときの手作業の目安）

`KEILAND_*` を書いた file は自分で `#include "userland/desktop/paths.h"` する（3 つの header（`files/files.h`・`textedit/textedit.h`・`browser/text/text.h`）を含む）。

| 場所 | file |
| --- | --- |
| その file 自身の quote の include の後（前後に空行） | `files/apps.c`・`files/ui-desktop-actions.c`・`files/ui-desktop.c`（`"files.h"` の後）、`files/files.h`（`"ops.h"`）、`files/main.c`・`imageview/main.c`・`libkeiui/chooser.c`・`pdfviewer/main.c`・`settings/main.c`・`textedit/main.c`（`"window.h"`）、`ime/main.c`（`"program.h"`）、`mview/main.c`（`"mview.h"`）、`notes/main.c`（`"app.h"`）、`settings/look.c`（`"settings.h"`）、`terminal/main.c`（`"terminal.h"`）、`wayland/corner.c`（`"menu.h"`）、`wayland/desktop.c`（`"popup.h"`）、`wayland/home.c`（`"glass.h"`）、`wayland/input-method.c`（`"titlebar.h"`） |
| 公開の header の group の後 | `browser/main.c`（`<browser.h>`）、`wayland/glass.c`（`<truetype.h>`）、`kuidemo/main.c`・`textedit/textedit.h`（`<keiui.h>`） |
| 同じ group（空行無し） | `browser/text/text.h`（`"base/base.h"`）、`wayland/main.c`（`"data.h"`）、`xserver/server.c`（`"userland/desktop/xserver/internal.h"`） |

## 完了の条件

- 手順 3 が 0、手順 4 の `STRINGS-SAME` と warning 0、手順 5〜7 が PASS。

## 結果

（実行の後に書く）
