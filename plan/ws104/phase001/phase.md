<!-- awesome-plan project=zedbsd record=ws104-p001 -->

# ws104-p001: desktop の公開の header を `userland/desktop/keiland/` へ移す

Status: planned
Disposition: normal
Parent: [WS104](../ws.md)
Queue: なし
実行者: **main（Q1）だけ**（`toolchain/llvm/sysroot.mk` は toolchain の範囲。始める前にユーザーの許可を確かめる）

## 目的

決定 D3（[WS105 の ws.md](../../ws105/ws.md)）: `keiland.h` などの desktop の header は C library の物ではない。Linux の build（WS105）は zedBSD の
libc の header の directory（`include/libc/`）を読めない（glibc と衝突する）ので、desktop の header を libc から分けた場所に置く。
zedBSD の image と sysroot の中では、header は今と同じ名前・同じ場所（`/usr/include/keiland.h` など）に入り続ける。

## 移す物と移さない物

`git mv` で `include/libc/` から `userland/desktop/keiland/` へ移す（中身は変えない）:

| 移す物 | 実装している library |
| --- | --- |
| `keiland.h` | libkeiland |
| `keiui.h` | libkeiui |
| `truetype.h` | libtruetype（`userland/base/libpdf` も使う。sysroot に入り続けるので問題ない） |
| `browser.h` | libbrowser |
| `wayland-client.h`・`wayland-client-core.h`・`wayland-client-protocol.h`・`wayland-util.h`・`xdg-shell-client-protocol.h`・`primary-selection-unstable-v1-client-protocol.h`・`tablet-unstable-v2-client-protocol.h`（`wayland/` の物への 15 行の転送） | libwayland |
| `wayland-egl.h`・`wayland-egl-core.h` | libwayland-egl |
| directory `wayland/`（`API-PROVENANCE.md` と 10 個の header） | libwayland |

移さない物（理由）:

- `vulkan/`: OS の API と見なす（ユーザー 2026-10-01「zedBSDではこれがOSのAPIです」）。kernel の i915 も使う。
- `EGL/`・`GLES2/`・`GLES3/`・`KHR/`: Khronos の header のそのままの複写（hash は `include/libc/EGL/API-PROVENANCE.md`）。vulkan と同じく OS の API の扱い。
- `GL/`・`X11/`: userland/retro の物で desktop ではない。
- `pdf.h`: `userland/base/libpdf`（base の library）の物。
- `compat/`: base の compat の library（libz・libpng・libjpeg・libgif）の物。
- `catalog-format.h`・`locale-format.h`: libc の物。

## 手順

1. **前の状態を記録する**（比較の基準）。今の main で sysroot を最新にしてから、中身の hash を取る:
   ```
   make -j16 BUILD=build/amd64 build/amd64/sysroot/.zedbsd-sysroot-complete
   (cd build/amd64/sysroot/usr/include && find . -type f | LC_ALL=C sort | xargs sha256sum) > build/ws104/p001-before.txt
   ```
   （`build/ws104/` は自分の作業用。無ければ作る。）
2. `mkdir -p userland/desktop/keiland` し、上の表の物を `git mv` する（`include/libc/wayland` は directory ごと `git mv include/libc/wayland userland/desktop/keiland/wayland`）。
3. `toolchain/llvm/sysroot.mk`（toolchain の範囲。ユーザーの許可の後に）:
   - 89〜90 行の `find include/libc include/libc include/uapi` に `userland/desktop/keiland` を足す。
   - 97〜98 行の `ZEDBSD_SYSROOT_INCLUDE_NAMES` の `patsubst` に `userland/desktop/keiland/%` → `%` を足す。
   - 143〜147 行の `case` に `userland/desktop/keiland/*) relative=$$$${header#userland/desktop/keiland/} ;;` を足す。
   - 他の行（`include/libc` が 2 回書かれている所など）は触らない（範囲の外）。
   - `plan/tools/toolchain-lock.sh` の lock は build の tree の物で、この file には関係しない。
4. path を名指しする所を直す。次で探し、出た物を全て直す:
   ```
   grep -rn --exclude-dir=.claude --exclude-dir=build --exclude-dir=.internal --exclude-dir=history \
     -e 'include/libc/\(keiland\|keiui\|truetype\|browser\|wayland\|xdg-shell\|primary-selection\|tablet-unstable\)' .
   ```
   直す物の例（2026-10-01 の調査で分かっている物）:
   - `userland/desktop/libwayland/*.c` の 8 file と `libwayland/README.md` の注釈（`include/libc/wayland/API-PROVENANCE.md` → `userland/desktop/keiland/wayland/API-PROVENANCE.md`）。
   - `userland/desktop/libbrowser/Makefile:154` の header の引数 `include/libc/browser.h` → `userland/desktop/keiland/browser.h`。
   - host の試験の script（header を複写・link している）: `plan/tools/files/host-build.sh`・`host-png.sh`・`plan/tools/textedit/host-core.sh`・
     `plan/tools/keiui/host-chooser.sh`・`plan/tools/imageview/run-host.sh`、`plan/ws0NN/tests/*.sh`（約 30 本、ws035・ws073・ws074・ws079・ws081・ws089・ws090・ws100・ws102）。
     path の文字列だけを直す。`include/libc/compat` や `vulkan` を指す所は変えない。
   - `plan/history/` は履歴なので直さない。
5. build する: `make -j16 BUILD=build/amd64 disk-image`（sysroot が作り直され、desktop の全 object が作り直される）。warning 0。
6. **後の状態を比べる**:
   ```
   (cd build/amd64/sysroot/usr/include && find . -type f | LC_ALL=C sort | xargs sha256sum) > build/ws104/p001-after.txt
   diff build/ws104/p001-before.txt build/ws104/p001-after.txt   # 差が無いこと
   ```
7. boot test: `plan/tools/boot-test.sh build/amd64/hdd-image.img`（撮れた PNG をユーザーに見せる）。
8. 4 の host の script のうち、header の path を直した物から 2 本を走らせて通ることを確かめる（例 `sh plan/tools/textedit/host-core.sh`、`sh plan/tools/files/host-build.sh`。使い方は各 script の先頭）。

## 完了の条件

- `include/libc/` に表の物が無く、`userland/desktop/keiland/` にある（`git status` で rename として見える）。
- 手順 4 の grep が `plan/history/` の外で 0 件。
- build の warning 0、手順 6 の diff が空、boot test PASS、手順 8 の 2 本が PASS。

## 範囲の外

- header の中身の変更、`#include` の書き方の変更（`<keiland.h>` のまま）。
- pcat・pc98・arm64 の build（同じ sysroot.mk を使うので同じく動くはずだが、この Phase では amd64 だけ確かめる。未実施と書く）。

## 結果

（実行の後に書く: 実行日、commit、command と結果、未実施の確かめ）
