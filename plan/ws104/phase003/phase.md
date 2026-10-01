<!-- awesome-plan project=zedbsd record=ws104-p003 -->

# ws104-p003: libkeiland の OS の 3 file を `libkeiland/zedbsd/` へ

Status: in-progress
Disposition: normal
Parent: [WS104](../ws.md)
Queue: q517 / q517-i01
依存: p002
実行者: phase-runner（high）でよい。`plan/ws089`・`plan/ws100` の script の 2 行は他の WS の file なので、**main が当てる**（下の手順 3）

## 目的

決定 D14: libkeiland の OS に依存する部分を、OS ごとの C の source に分ける。次の 3 つは file の全体が zedBSD の物（app は `keiland.h` の関数だけを使う）。
WS105 は同じ関数を Linux の source で実装する。

| 今の file | 移す先 | 中身 |
| --- | --- | --- |
| `libkeiland/network.c` | `libkeiland/zedbsd/network-zedbsd.c` | zedBSD の networkd と、`userland/base/net/protocol.h` の protocol で話す |
| `libkeiland/network-link.c` | `libkeiland/zedbsd/network-link-zedbsd.c` | interface の情報（socket の ioctl `SIOCGIFCONF` など。Linux では compile できない）、`/etc/resolv.conf`、zedBSD の鍵の file |
| `libkeiland/audio.c` | `libkeiland/zedbsd/audio-zedbsd.c` | zedBSD の audiod（`keiland_audio_*`、p002 で足した `keiland_audio_available` を含む） |

## 用意してある物

- [`../patches/p003-after-gitmv.patch`](../patches/p003-after-gitmv.patch): `git mv` の後に当てる（rename の hunk を含まない）。中身: `userland/desktop/libkeiland/Makefile`
  （`LIBZDESKTOP_SOURCES` を `LIBKEILAND_SOURCES` に改名して 3 file と `userland/base/net/*.c` を外し、新しい `LIBKEILAND_ZEDBSD_SOURCES` に並べる）と、
  `plan/ws089/tests/host-build.sh:37`・`plan/ws100/tests/host-audio.sh:13` の path。
- survey の確かめ: 移した 3 file は repo の root からの include だけを使い（`"userland/base/net/protocol.h"` など）、そのまま target で compile できる。
  make が計算する object の一覧は 3 file の場所の他は同じ。object は `build/amd64/dynamic/obj/userland/desktop/libkeiland/zedbsd/*-zedbsd.o` に作られる。

## 手順（`<W>` は `ws104-p003`）

1. 前の export の一覧を取る（p002 の build の `libkeiland.so`。無ければ先に `make -j64 build/amd64/dynamic/libkeiland.so`）:
   ```
   mkdir -p build/ws104-p003
   nm -D --defined-only build/amd64/dynamic/libkeiland.so | awk '{print $3}' | LC_ALL=C sort > build/ws104-p003/exports-before.txt
   ```
2. 移す:
   ```
   mkdir -p userland/desktop/libkeiland/zedbsd
   git mv userland/desktop/libkeiland/network.c userland/desktop/libkeiland/zedbsd/network-zedbsd.c
   git mv userland/desktop/libkeiland/network-link.c userland/desktop/libkeiland/zedbsd/network-link-zedbsd.c
   git mv userland/desktop/libkeiland/audio.c userland/desktop/libkeiland/zedbsd/audio-zedbsd.c
   ```
3. patch を当てる:
   - phase-runner（subagent）: `git apply --exclude='plan/*' plan/ws104/patches/p003-after-gitmv.patch`、そして main に「`git apply --include='plan/*' plan/ws104/patches/p003-after-gitmv.patch` を当ててください」と頼む。
   - main が実行するなら: `git apply plan/ws104/patches/p003-after-gitmv.patch`
4. 共通の source に OS の物が残っていない:
   ```
   grep -n '#include' userland/desktop/libkeiland/*.c | grep -e 'userland/base' -e 'uapi/' -e 'sys/ioctl.h' -e 'net/if.h' | wc -l
   ```
   `0`（`preferences.c`・`recent.c` の `<sys/file.h>`・`<sys/stat.h>`・`<pwd.h>` は POSIX なので対象の外）。
5. build と warning の数え（[commands.md](../commands.md) §1）。export の一覧が同じ:
   ```
   nm -D --defined-only build/amd64/dynamic/libkeiland.so | awk '{print $3}' | LC_ALL=C sort > build/ws104-p003/exports-after.txt
   diff build/ws104-p003/exports-before.txt build/ws104-p003/exports-after.txt && echo EXPORTS-SAME
   ```
6. host の試験: `sh plan/ws100/tests/host-audio.sh`、`sh plan/ws089/tests/host-build.sh`。
7. Settings の回帰（commands.md §8 の Settings の 5 行）: `settings-regress: PASS`。
8. boot test（`OUTPUT=build/ws104-p003/boot`）。
9. commit: `git commit -m WIP -- userland/desktop/libkeiland`（main は `plan/ws089/tests/host-build.sh plan/ws100/tests/host-audio.sh` も）。

## 完了の条件

- 3 file が `libkeiland/zedbsd/` にあり、手順 4 が 0、手順 5 の `EXPORTS-SAME` と warning 0、6〜8 が PASS。
- 古い `audio.o`・`network.o`・`network-link.o` が build の directory に残るのは無害（消さない）。`libkeiland.so` は object の順が変わるので byte では同じにならない（export が同じならよい）。

## 結果

（実行の後に書く）

Execution started UTC: 2026-10-01T02:25:22.606785+00:00。Approval: current user, 2026-10-01「お、いい調子ですね！その調子で、ws104の完了まで自律的に作業を進めてください。」。既存 WS104 p002〜p008 全範囲、依存順の 1 Phase Queue と検証・記録・WIP commit を承認。push / GitHub 公開は承認対象外。
