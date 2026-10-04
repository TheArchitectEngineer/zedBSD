<!-- awesome-plan project=zedbsd record=ws131-p026 -->

# ws131-p026: 公開の header の置き場所と名前の整理（keiland-headers/、<keiland/keiland.h>、Wayland の header）

Status: planning（2026-10-04 ユーザーの指示。Q1 の調べ（Explore）の結果で範囲を確定してから Queue を作る）
Disposition: normal
Parent: [WS131](../ws.md)
Queue: none
依存: p014（旧 libkeiland の名前の改名）が main に統合済み。p023 の「`keiland.h` 一本化」と重なる所はこの Phase に寄せ、p023 の範囲を直す。

## ユーザーの指示（2026-10-04）

「ヘッダは<keiland/keiland.h>の方がいいです。あとでリファクタをお願いします。userland/desktop/keiland/にヘッダしかないのは理解しづらかったです。keiland/からkeiland-headers/にリネームするのがいいと思います。kui.hは<keiland/ui.h>にして、でもそれは内部実装であり、アプリからは<keiland/keiland.h>だけインクルードすれば足りるようにリファクタをお願いします。waylandヘッダも調べて整理してみてください。」

## 範囲（案、調べの後に確定）

1. `userland/desktop/keiland/` を `userland/desktop/keiland-headers/` に改名する（公開の header だけの置き場所であることを名前で示す）。
2. Keiland の header を `<keiland/keiland.h>` で入れる（sysroot の `usr/include/keiland/`、Linux・FreeBSD の install も同じ）。
3. `keiui.h`（p013 の後は互換の header）と `keiland-ui.h` を `<keiland/ui.h>` にまとめる。`ui.h` は内部の header の扱いで、`keiland.h` が include する。app は `<keiland/keiland.h>` だけを include する（app の source の `keiui.h` の include を除く）。
4. Wayland の header: 直下と `wayland/` の下に同じ名前の 2 組（中身が違う）がある。どちらが正本か、Linux・FreeBSD の system の libwayland の header と衝突しないかを調べ、1 組にまとめる。
5. sysroot の写しの規則（`toolchain/llvm/sysroot.mk:89-103`）は toolchain なので、Q1 の許可の下で変える（unlock → 変更 → lock）。古い名前の header が sysroot に残る問題（BUG-084 の型）を避ける。

## 受け入れ（案）

- 3 OS の build（zedBSD amd64 exit 0・自前の warning 0、`make keiland-linux` の gcc と clang、FreeBSD の native build と audit）、`keiland-os-boundary/check.sh`・`elf-check.sh`・header-check PASS。
- tree の中の app の source が `<keiland/keiland.h>` だけを include する（`keiui.h`・`keiland-ui.h`・`<keiland.h>` の直接の include が 0）。
- Wayland の header が 1 組になり、重複が 0。
- QEMU（T）: boot-test と desktop の主な試験。Linux の app の起動の PNG。

## 調べ（2026-10-04、Q1 の Explore、main 7244b70）

- Wayland の header の 2 組は重複ではない。直下の 7 本（wayland-client.h・wayland-util.h・wayland-client-core.h・wayland-client-protocol.h・xdg-shell・primary-selection・tablet）は `#include <wayland/X>` だけの 15 行の wrapper（標準の綴り用）で、実体は `wayland/` の下。
- system の libwayland の header は使っていない（-I の順で自前が勝つ。`header-check.sh:20`・`native-build-audit.py:21,34` が監視）。ただし名前は system の libwayland と同じなので、-I の順に頼っている。
- `kl-system-protocol.h` は内部用（compositor と libkeiland の wire）なのに公開の置き場所にあり、`"userland/desktop/keiland/kl-system-protocol.h"` で 10 か所から読まれる。private な場所へ移す。
- `sysroot.mk:93` は `find -type f` なので `.md`・`.ppm`（wallpapers）も `usr/include` に入る。壁紙と文書は header の dir の外へ。
- FreeBSD の公開の表は `keiland-freebsd.mk:172-180`。wayland-egl・browser.h・kl-system-protocol.h は入れていない。
- path の直書き: sysroot.mk:89-93,103,152（toolchain）、clang の Makefile:254-261（BUG-084 の対策）、keiland-linux.mk:20、keiland-freebsd.mk:16,172-180、libbrowser/Makefile:5,187、plan/tools の host の script、plan/ws*/tests の約 77 本。
- main で `<keiui.h>` を直に include する app: files・imageview・ime・monitor・notes・pdfviewer・terminal・textedit・kuidemo（12 か所）。

## 移行の順（調べの案）

1. p013（edb4a69）・p014 を main に入れる。
2. `kl-system-protocol.h` を private な場所へ（10 か所の include）。
3. wallpapers・`.md` を header の dir の外へ、image の script の path を直す。sysroot の find を `*.h` に絞る。
4. dir を `keiland-headers/` に改名し、その下に `keiland/`（keiland.h・ui.h）・`wayland/`・標準の綴りの wrapper。Makefile・.mk・plan/tools・plan の試験の path を直す。
5. sysroot.mk の写しの規則（toolchain、Q1 の許可、unlock/lock）。manifest の方式を保ち、`plan/ws073/tests/bug084.sh` で古い header が残らないことを確かめる。
6. app を `<keiland/keiland.h>` だけに。`ui.h` は keiland.h の中からだけ（直に include したら `#error`）。
7. 検査（native-build-audit.py:40-41、keiland-os-boundary/check.sh:54,95、FreeBSD の表）を直す。
8. 互換の keiui.h と `<keiland.h>`（直下）を廃し、FreeBSD の install で古い header を消す。

決める点: truetype.h・browser.h を `<keiland/…>` に揃えるか（別の library の公開の header）。使われていない wrapper を消すか、全 protocol に揃えるか。
