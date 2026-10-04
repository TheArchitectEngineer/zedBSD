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
