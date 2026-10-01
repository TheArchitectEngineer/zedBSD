<!-- awesome-plan project=zedbsd record=ws104-p007 -->

# ws104-p007: install の path を `userland/desktop/paths.h` の macro に

Status: planned
Disposition: normal
Parent: [WS104](../ws.md)
Queue: なし
実行者: phase-runner（high）でよい（機械的な置き換え。phase-runner-mid でも可）

## 目的

desktop の source は、自分たちが install される場所を文字列で直書きしている（`/bin/terminal`・`/usr/share/fonts/keiland.ttf`・`/etc/keiland/apps.conf`・
`/usr/libexec/keiland-ime` など、約 70 箇所）。Linux（WS105）では全てが `/opt/keiland/` の下に入る（決定 D1）。path を 1 つの header の macro にし、
zedBSD の build では**今と全く同じ文字列**になり、Linux の build（`Makefile.linux`）は `-D` で上書きする形にする。

## 新しい header `userland/desktop/paths.h`

```c
/*
 * Where the desktop is installed (WS104 p007).  zedBSD's image puts it in the system's own directories, the defaults
 * here.  The Linux build (WS105, Makefile.linux) puts all of it under /opt/keiland and defines each macro on the
 * compiler's command line.  A path is written as the macro followed by the rest of the path, so that the string
 * the program sees is the same as before:  KEILAND_BINDIR "/terminal"  is  "/bin/terminal"  on zedBSD.
 */
#ifndef KEILAND_BINDIR
#define KEILAND_BINDIR		"/bin"		/* the programs */
#endif
#ifndef KEILAND_LIBEXECDIR
#define KEILAND_LIBEXECDIR	"/usr/libexec"	/* the helpers (keiland-ime, keiland-x11) */
#endif
#ifndef KEILAND_DATADIR
#define KEILAND_DATADIR		"/usr/share"	/* fonts/, keiland/ (wallpapers, models), browser/, licenses/ */
#endif
#ifndef KEILAND_SYSCONFDIR
#define KEILAND_SYSCONFDIR	"/etc"		/* keiland/ (apps.conf, desktop, open-with) */
#endif
```

（注釈と書き方は coding-style.md に合わせる。header の guard を付ける。）

Linux の値（WS105 の `Makefile.linux` が渡す。この Phase では使わない）: `KEILAND_BINDIR="/opt/keiland/bin"`、`KEILAND_LIBEXECDIR="/opt/keiland/libexec"`、
`KEILAND_DATADIR="/opt/keiland/share"`、`KEILAND_SYSCONFDIR="/opt/keiland/etc"`。

## 置き換える物

次で探す（`sessiond/` は zedBSD だけの program なので対象の外。`/bin/sh` は system の shell なので対象の外）:

```
grep -rn --include=*.c --include=*.h -E '"/(usr/share|usr/libexec|etc/keiland|bin/|usr/bin/)' userland/desktop \
  | grep -v -e '^userland/desktop/sessiond/' -e '"/bin/sh"'
```

2026-10-01 に出た物（約 60 行）: `browser/main.c`・`browser/text/text.h`・`files/apps.c`・`files/files.h`・`files/main.c`・`files/ui-desktop-actions.c`・`files/ui-desktop.c`・
`imageview/main.c`・`ime/main.c`・`kuidemo/main.c`・`libkeiui/chooser.c`・`mview/main.c`・`notes/main.c`・`pdfviewer/main.c`・`settings/look.c`・`settings/main.c`・
`terminal/main.c`・`textedit/main.c`・`textedit/textedit.h`・`wayland/corner.c`・`wayland/desktop.c`・`wayland/glass.c`・`wayland/home.c`（`HOME_APPS_PATH`・
`HOME_BROWSER_START`・891〜902 行の app の一覧）・`wayland/input-method.c`・`wayland/main.c`（69〜70 行の font）・`xserver/server.c`。

置き換えの規則:

| 今 | 後 |
| --- | --- |
| `"/bin/terminal"` | `KEILAND_BINDIR "/terminal"` |
| `"/usr/libexec/keiland-ime"` | `KEILAND_LIBEXECDIR "/keiland-ime"` |
| `"/usr/share/fonts/keiland.ttf"` | `KEILAND_DATADIR "/fonts/keiland.ttf"` |
| `"/usr/share/keiland/wallpapers"` | `KEILAND_DATADIR "/keiland/wallpapers"` |
| `"/etc/keiland/apps.conf"` | `KEILAND_SYSCONFDIR "/keiland/apps.conf"` |
| `"/bin/sh /usr/libexec/keiland-x11 /bin/zterm ..."`（文字列の途中に path がある command） | `"/bin/sh " KEILAND_LIBEXECDIR "/keiland-x11 " KEILAND_BINDIR "/zterm ..."`（文字列の連結で同じ文字列にする） |

- 各 file に `#include "userland/desktop/paths.h"` を足す（include の並びの慣習に合わせる）。
- 文字列の中身を 1 文字も変えない。`/dev/`・`/tmp/`・`/proc`・`/sys`・`/run/`・`/var/`・`/etc/resolv.conf`・`/etc/passwd` などの OS の path は対象の外。
- `wayland/main.c:78` の既定の socket `/tmp/wayland-0` は対象の外（WS105 で Linux の既定を別に決める）。

## 確かめ（文字列が同じであること）

1. 置き換えの**前**に build し、対象の binary の文字列を取る:
   ```
   make -j16 BUILD=build/amd64 disk-image
   for f in build/amd64/bin/* build/amd64/dynamic/*.so; do echo "== $f"; strings -a "$f" | grep -E '^/(usr|etc|bin)/' | LC_ALL=C sort -u; done > build/ws104/p007-before.txt
   ```
2. 置き換えの後に同じことをして `build/ws104/p007-after.txt` にし、`diff` が空であること。
3. build の warning 0。
4. 上の grep が 0 件（sessiond と `/bin/sh` を除く）。
5. 回帰: `criteria.sh ... C1 C2 C9`（App Home からの app の起動を含む）、boot test。

## 完了の条件

- 確かめ 1〜5。

## 結果

（実行の後に書く）
