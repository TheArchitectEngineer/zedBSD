<!-- awesome-plan project=zedbsd record=ws073p021 -->

# ws073-p021: header を改名した後の最初の image の build で rootfs の複写が消えた header を読む（BUG-084）

Status: cleared（2026-09-28）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-084](../../bugs/BUG-084.md)（main の依頼、2026-09-28）

## 目的と受け入れ

`include/libc` の header を改名・移動した直後の `-j` の image の build が、rootfs の rule で古い header の `cp: cannot stat` を出さずに成功する。
修正前の tree で再現し、修正後の tree で同じ手順が通ること。

## 原因

clang の package の Makefile が image の `/usr/include` に入れる header の一覧を `$(shell find <sysroot>/usr/include ...)` で作っていた。rootfs の
rule の recipe は `$(eval)` で作られるので、一覧は make が読む時の古い sysroot のもの。同じ make の中で sysroot の rule が sysroot を作り直すと、
rootfs の `--file` の `cp -f` が消えた古い名前を読んで止まる。rule の順序（sysroot → rootfs）は正しかった（[BUG-084](../../bugs/BUG-084.md)）。

## 修正

- `toolchain/llvm/sysroot.mk`: `ZEDBSD_SYSROOT_INCLUDE_NAMES`（header の manifest を sysroot の `usr/include` の中の名前に写したもの）。
- `userland/packages/lang/clang/Makefile`: header の一覧をこの名前から作る（遅延の展開、sysroot.mk は後で読まれる）。

## 検証（host、この worktree の build/amd64 と build/ws073-b084。package の stage は main の build/packages から複写し `make -o` で固定）

- [tests/bug084.sh](../tests/bug084.sh): 修正前の Makefile で `cp: cannot stat '.../sysroot/usr/include/fmtmsg.h'`（`rootfs/.stamp`）で FAIL（再現）。
  修正後は 2 つの build（逆向きの改名の後、改名の後）とも成功し、rootfs に新しい名前だけがある。PASS。
- 改名の無い tree での一覧は修正の前後で同一（227 件）。
- 未実施: `build-zdesktop-image.sh` での全体の build（clang 等の package の再 build を含む）。package の build は原因に関わらないため。

## 残り

clang の resource の header の一覧（`$(wildcard)` の stage）も parse 時の一覧で、新しい checkout の最初の image に入らない可能性がある（未再現、
別の bug の候補として main に ID を依頼）。
