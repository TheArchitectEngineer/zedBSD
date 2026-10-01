<!-- awesome-plan project=zedbsd record=ws105-p002 -->

# ws105-p002: build の土台（`keiland-linux.mk`・top-level の goal・library の `Makefile.linux`）

Status: planned
Disposition: normal
Parent: [WS105](../ws.md)
Queue: なし
依存: WS104 の p001（header が `userland/desktop/keiland/`）・p003（libkeiland の `zedbsd/`）・p007（`userland/desktop/paths.h`）
実行者: phase-runner（high）

## 目的

決定 D1・D2。Linux の build を、zedBSD の build と完全に別の GNU make の file で作る（[design.md](../design.md) §2・§3）。この Phase では library と小さな部品だけを
build・install し、program（compositor・app）は後の Phase で足す。

## 作る・変える file

| file | 内容 |
| --- | --- |
| `Makefile`（top-level） | goal `keiland-linux`・`keiland-linux-install`・`keiland-linux-install-session`・`keiland-linux-clean`（中身は `$(MAKE) -f userland/desktop/keiland-linux.mk <goal を keiland-linux- を除いた名前>`、`all`・`install`・`install-session`・`clean`）。`ZEDBSD_CONFIG_OPTIONAL_GOALS`（44-53 行）に 4 つを足す。`help` に 1 行ずつ。置く場所は `managed-lan-host-test`（426 行の近く）の後 |
| `userland/desktop/keiland-linux.mk` | design §3.2 の全部: 変数、flag、`KEILAND_LINUX_LIBRARY`・`KEILAND_LINUX_PROGRAM`・`KEILAND_LINUX_DATA` の macro、`KEILAND_LINUX_PACKAGES` の include、header の複写（`include/libc/compat/`・`pdf.h`・`sha2.h`・`md5.h` → `$(KEILAND_LINUX_BUILD)/include/`）、`all`・`install`・`install-session`（p009 まで空）・`clean` |
| `userland/base/libz-compat/Makefile.linux`・`libpng-compat`・`libjpeg-compat`・`libgif-compat` | 各 compat の library（SONAME は zedBSD と同じ `libz-compat.so` など。zedBSD の link の規則 `platform/amd64/vmunix.mk:899-961` と同じ依存と exports.map） |
| `userland/desktop/linux-compat/Makefile.linux` | design §3.4。`src/libc/openbsd-sha2.c`・`openbsd-digest.c` を compile して `$(KEILAND_LINUX_BUILD)/lib/libkeiland-compat.a`。install しない |
| `userland/desktop/libwayland/Makefile.linux` | `libwayland-client.so`（zedBSD の `Makefile` の source の一覧と同じ 20 file、exports.map） |
| `userland/desktop/libtruetype/Makefile.linux` | `libtruetype.so` |
| `userland/desktop/libkeiland/Makefile.linux` | `libkeiland.so`: 共通の source と、Linux の仮の backend（下）。依存 `libwayland-client.so libtruetype.so` |
| `userland/desktop/libkeiland/wpa/network-wpa.c`（仮） | `keiland_network_open`・`close`・`update`・`get_state`・`get_scan`・`request`・`get_request` の**最小の実装**: open は記録を作るだけ、state は `reachable = 0`・`wifi = KEILAND_WIFI_ABSENT`、scan は 0 件、request は `ENOTCONN`。本物は p010 |
| `userland/desktop/libkeiland/linux/network-link-linux.c`（仮） | `keiland_network_get_links`・`get_dns`・`save_key`・`get_saved` の最小: links 0 件、DNS は `/etc/resolv.conf` を読む（zedBSD の物と同じ読み方で書いてよい）、save_key は `ENOTSUP`、saved 0 件 |
| `userland/desktop/libkeiland/linux/audio-linux.c`（仮） | `keiland_audio_*` の最小: open は記録を作る、`fd` は -1、update は変化なし、state は `reachable = 0`、set_volume・feedback は `ENOTCONN`、`keiland_audio_available` は 0 |
| `plan/tools/keiland-linux/makefile-sync.sh` | design §3.3 の source の一覧の確かめ（main が merge） |
| `plan/tools/keiland-linux/elf-check.sh` | design §9.1 の ELF の確かめ: `STAGE` の下の全ての ELF について `readelf -d` で RUNPATH・SONAME・NEEDED を出し、規則に外れたら FAIL（main が merge） |

注意:

- 仮の backend の file の先頭の注釈に「placeholder until ws105-p010」と書く。仮でも `keiland.h` の約束（返す errno の種類、NULL を返す条件）に従う。
- `KEILAND_LINUX_PACKAGES` の順: `linux-compat`、`libz-compat`、`libpng-compat`、`libjpeg-compat`、`libgif-compat`、`libwayland`、`libtruetype`、`libkeiland`。
- 共通の source が gcc の `-Werror` で落ちたら（clang だけで書かれた code の gcc の warning）、**振る舞いを変えない最小の直し**をし（例: 未使用の変数、符号の比較の cast）、
  zedBSD の回帰（design §9.2 の build と boot test）を流す。直した file と warning を結果に書く。warning を消すための `-Wno-...` の追加はしない（理由があれば main に報告）。

## 確かめ（完了の条件）

1. `make keiland-linux` が通る（warning 0）。`make keiland-linux CC=clang KEILAND_LINUX_BUILD=build/keiland-linux-clang` も通る。
2. `make keiland-linux-install DESTDIR=$PWD/build/keiland-linux/stage` で `build/keiland-linux/stage/opt/keiland/lib/` に 8 つの `.so`（compat 4・libwayland-client・libtruetype・libkeiland と、無いはずの物が無いこと）。
3. `sh plan/tools/keiland-linux/elf-check.sh build/keiland-linux/stage` が PASS。
4. 小さな確かめの program（`plan/tools/keiland-linux/lib-smoke.c`: `keiland_version()` が 21 を返す、`keiland_network_open()` が NULL でなく state の reachable が 0、
   `keiland_audio_available()` が 0）を stage の library に link して走らせる:
   ```
   cc -o build/keiland-linux/test/lib-smoke plan/tools/keiland-linux/lib-smoke.c -Iuserland/desktop/keiland \
      -Lbuild/keiland-linux/stage/opt/keiland/lib -l:libkeiland.so -Wl,-rpath,/opt/keiland/lib
   LD_LIBRARY_PATH=build/keiland-linux/stage/opt/keiland/lib build/keiland-linux/test/lib-smoke   # "lib-smoke: PASS"
   ```
5. `sh plan/tools/keiland-linux/makefile-sync.sh` が PASS。
6. zedBSD の build に影響が無い: `make -j16 BUILD=build/amd64 disk-image` が通る（`Makefile.linux` は include されない）。共通の source を直したなら §9.2 の回帰も。
7. `make keiland-linux-clean` で `build/keiland-linux/` の build の物が消え、`build/keiland-linux/guest`（p001 の guest）は消えない（clean は `obj/`・`lib/`・`bin/`・`libexec/`・`include/`・`share/`・`gen/`・`stage/` だけを消す）。

## 結果

（実行の後に書く）
