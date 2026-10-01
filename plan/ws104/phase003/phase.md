<!-- awesome-plan project=zedbsd record=ws104-p003 -->

# ws104-p003: libkeiland の OS の 3 file を `libkeiland/zedbsd/` へ

Status: planned
Disposition: normal
Parent: [WS104](../ws.md)
Queue: なし
実行者: phase-runner（high）でよい

## 目的

決定 D14: libkeiland の OS に依存する部分を、OS ごとの C の source に分ける。libkeiland の 15 個の source のうち、OS に依存するのは次の 3 つで、
どれも **file の全体** が zedBSD の物である（app は `keiland.h` の関数だけを使う）。WS105 は同じ関数を Linux の source で実装する。

| 今の file | 移す先 | 中身 |
| --- | --- | --- |
| `libkeiland/network.c` | `libkeiland/zedbsd/network-zedbsd.c` | zedBSD の networkd と、`userland/base/net/protocol.h` の protocol で話す（`keiland_network_open`・`update`・`get_state`・`get_scan`・`request`・`get_request`・`close`） |
| `libkeiland/network-link.c` | `libkeiland/zedbsd/network-link-zedbsd.c` | interface の情報（socket の ioctl `SIOCGIFCONF` など）、`/etc/resolv.conf`、zedBSD の鍵の file（`userland/base/net/wifi-store.h`）（`keiland_network_get_links`・`get_dns`・`save_key`・`get_saved`） |
| `libkeiland/audio.c` | `libkeiland/zedbsd/audio-zedbsd.c` | zedBSD の audiod（`keiland_audio_*`、p002 で足した `keiland_audio_available` を含む） |

## 手順

1. `mkdir userland/desktop/libkeiland/zedbsd` し、3 つを `git mv` する（中身は変えない）。
2. 移した file の `#include` を確かめる。`"userland/base/..."` の形（repo の root から）なら直さなくてよい。同じ directory の header を `"..."` で読んでいる所があれば
   root からの path に直す。
3. `userland/desktop/libkeiland/Makefile`:
   - 共通の source の変数（今の `LIBZDESKTOP_SOURCES`）から 3 つと `userland/base/net/protocol.c`・`wifi-conf.c`・`wifi-store.c` を外す。
   - 新しい変数 `LIBKEILAND_ZEDBSD_SOURCES` に、移した 3 つと `userland/base/net/*.c` の 3 つを並べる。注釈: 「the zedBSD side of the library: networkd, the
     interfaces and the saved keys, audiod (WS104). WS105 has the Linux side in Makefile.linux.」
   - package の call には `$(LIBZDESKTOP_SOURCES) $(LIBKEILAND_ZEDBSD_SOURCES)` を渡す。変数の名前 `LIBZDESKTOP_SOURCES` は他で参照されていないか
     `grep -rn LIBZDESKTOP_SOURCES` で確かめ、参照が無ければ `LIBKEILAND_SOURCES` に改名してよい。
4. 移した file の path を名指しする script を直す:
   ```
   grep -rn --exclude-dir=.claude --exclude-dir=build --exclude-dir=.internal --exclude-dir=history \
     -e 'libkeiland/\(network\|network-link\|audio\)\.c' .
   ```
   2026-10-01 に分かっている物: `plan/ws089/tests/host-build.sh:37`（audio.c）、`plan/ws100/tests/host-audio.sh:13`（audio.c）。
   ws089 の host-network（`plan/ws089/tests/host-network.c` を build する script）も network-link.c を名指ししていれば直す。
5. 共通の source に OS の物が残っていないことを確かめる:
   ```
   grep -n '#include' userland/desktop/libkeiland/*.c | grep -e 'userland/base' -e 'uapi/' -e 'sys/ioctl.h' -e 'net/if.h'
   ```
   0 件であること（`preferences.c`・`recent.c` の `<sys/file.h>`・`<sys/stat.h>`・`<pwd.h>` は POSIX なので残してよい）。

## 確かめ

1. build: `make -j16 BUILD=build/amd64 disk-image`、warning 0。`nm -D build/amd64/dynamic/libkeiland.so` の export の一覧が移す前と同じ
   （前後で `nm -D --defined-only build/amd64/dynamic/libkeiland.so | awk '{print $3}' | sort` を取って diff）。
2. host の試験: `sh plan/ws100/tests/host-audio.sh`、ws089 の host の試験（`plan/ws089/tests/host-build.sh` の使い方に従う）。
3. Settings の回帰: `plan/ws089/tests/settings-regress.sh`（p002 と同じ手順）。network の頁の試験が含まれる。
4. boot test。

## 完了の条件

- 3 file が `libkeiland/zedbsd/` にあり、手順 5 の grep が 0 件、export の一覧が同じ、確かめ 1〜4 が PASS。

## 結果

（実行の後に書く）
