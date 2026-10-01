<!-- awesome-plan project=zedbsd record=ws104-p001 -->

# ws104-p001: desktop の公開の header を `userland/desktop/keiland/` へ移す

Status: cleared
Disposition: normal
Parent: [WS104](../ws.md)
Queue: [q515 / q515-i01](../../queue.md) finished（cleared）
実行者: **main（Q1）だけ**（`toolchain/llvm/sysroot.mk` は AGENTS.md の toolchain の範囲。2026-10-01 にユーザーが `p001-sysroot.patch` の適用を許可。q515 は finished）

## 判断（2026-10-01）

ユーザーは [p001-sysroot.patch](../patches/p001-sysroot.patch) の適用を明示して許可した。toolchain の変更の判断は解決した。Phase の範囲・達成基準は変わらず、同日、ユーザー「では、Queueを実行してください。」で q515 の実行も承認。

## 目的

決定 D3（[WS105 の ws.md](../../ws105/ws.md)）: `keiland.h` などの desktop の header は C library の物ではない。Linux の build（WS105）は zedBSD の
libc の header の directory（`include/libc/`）を読めない（glibc と衝突する）ので、desktop の header を libc から分けた場所に置く。
zedBSD の image と sysroot の中では、header は今と同じ名前・同じ場所（`/usr/include/keiland.h` など）に入り続ける（sysroot の manifest に directory を足す）。

## 移す物と移さない物

| 移す物（`include/libc/` → `userland/desktop/keiland/`） | 実装している library |
| --- | --- |
| `keiland.h` | libkeiland |
| `keiui.h` | libkeiui |
| `truetype.h` | libtruetype（`userland/base/libpdf` も使う。sysroot に入り続けるので問題ない） |
| `browser.h` | libbrowser |
| `wayland-client.h`・`wayland-client-core.h`・`wayland-client-protocol.h`・`wayland-util.h`・`xdg-shell-client-protocol.h`・`primary-selection-unstable-v1-client-protocol.h`・`tablet-unstable-v2-client-protocol.h`（`wayland/` の物への 15 行の転送） | libwayland |
| `wayland-egl.h`・`wayland-egl-core.h` | libwayland-egl |
| directory `wayland/`（`API-PROVENANCE.md` と 10 個の header） | libwayland |

移さない物（理由）: `vulkan/`（OS の API、kernel の i915 も使う）、`EGL/`・`GLES2/`・`GLES3/`・`KHR/`（Khronos のそのままの複写、OS の API の扱い）、`GL/`・`X11/`（userland/retro の物）、
`pdf.h`（base の libpdf の物）、`compat/`（base の compat の library の物）、`catalog-format.h`・`locale-format.h`（libc の物）。

## 用意してある物（2026-10-01 の survey、`git apply --check` 済み）

- [`../patches/p001-sysroot.patch`](../patches/p001-sysroot.patch): `toolchain/llvm/sysroot.mk` の 3 箇所（manifest の `find`、`ZEDBSD_SYSROOT_INCLUDE_NAMES` の `patsubst`、header の複写の `case`）。
- [`../patches/p001-paths.patch`](../patches/p001-paths.patch): 移した path を名指しする 44 file（libwayland の source の注釈 8・README、libbrowser の Makefile の header の引数、host の試験の script 34）。
  host の試験の script には、header が見つからないと**黙って host の `/usr/include/wayland-*.h` を使ってしまう**物（ws068・ws075・ws101 の GLES の shim、ws073 の
  `wayland-dispatch-once.sh`、ws035 の `p075/run-host.sh`）があり、それも直してある。
- survey は copy の tree でこの 2 つを当て、sysroot の `usr/include` の 241 file の hash が前後で同じこと、host の試験 9 本（textedit `host-core` 34/34、files `host-build`、
  ws089 `host-build`、ws100 `host-audio` 14/14、ws073 `wayland-dispatch-once`、ws068 `spirv-host`、keiui `host-chooser` 85/85、ws089 `host-preferences`、ws035 `p075`）が通ることを確かめた。

## 手順（repo の root で、上から順に実行する。`<W>` は `ws104-p001`）

1. 前の状態（比較の基準）:
   ```
   mkdir -p build/ws104-p001
   make -j64 sysroot-amd64
   (cd build/amd64/sysroot/usr/include && find . -type f | LC_ALL=C sort | xargs sha256sum) > build/ws104-p001/sysroot-before.txt
   ```
2. 移す:
   ```
   mkdir -p userland/desktop/keiland
   git mv include/libc/keiland.h userland/desktop/keiland/keiland.h
   git mv include/libc/keiui.h userland/desktop/keiland/keiui.h
   git mv include/libc/truetype.h userland/desktop/keiland/truetype.h
   git mv include/libc/browser.h userland/desktop/keiland/browser.h
   git mv include/libc/wayland-client.h userland/desktop/keiland/wayland-client.h
   git mv include/libc/wayland-client-core.h userland/desktop/keiland/wayland-client-core.h
   git mv include/libc/wayland-client-protocol.h userland/desktop/keiland/wayland-client-protocol.h
   git mv include/libc/wayland-util.h userland/desktop/keiland/wayland-util.h
   git mv include/libc/xdg-shell-client-protocol.h userland/desktop/keiland/xdg-shell-client-protocol.h
   git mv include/libc/primary-selection-unstable-v1-client-protocol.h userland/desktop/keiland/primary-selection-unstable-v1-client-protocol.h
   git mv include/libc/tablet-unstable-v2-client-protocol.h userland/desktop/keiland/tablet-unstable-v2-client-protocol.h
   git mv include/libc/wayland-egl.h userland/desktop/keiland/wayland-egl.h
   git mv include/libc/wayland-egl-core.h userland/desktop/keiland/wayland-egl-core.h
   git mv include/libc/wayland userland/desktop/keiland/wayland
   ```
3. patch を当てる（ユーザーの toolchain の許可の後）:
   ```
   git apply plan/ws104/patches/p001-sysroot.patch
   git apply plan/ws104/patches/p001-paths.patch
   ```
4. **sysroot を必ず作り直させる**: `git mv` は file の時刻を変えず、`sysroot.mk` は sysroot の前提でないので、何もしないと sysroot は作り直されず、手順 6 の比較が
   空しく通ってしまう。
   ```
   touch userland/desktop/keiland/keiland.h
   ```
5. build（sysroot と全ての desktop の object と外部 package が作り直される。〜15 分）:
   ```
   make -j64 disk-image > build/ws104-p001/build.log 2>&1; echo "make exit=$?"
   grep -E ':[0-9]+:[0-9]+: warning:' build/ws104-p001/build.log | grep -vE '/packages/|^\.\./src/|userland/base/noct/noct/' | wc -l
   ```
   `make exit=0` と `0`。
6. 後の状態を比べる:
   ```
   (cd build/amd64/sysroot/usr/include && find . -type f | LC_ALL=C sort | xargs sha256sum) > build/ws104-p001/sysroot-after.txt
   diff build/ws104-p001/sysroot-before.txt build/ws104-p001/sysroot-after.txt && echo SYSROOT-SAME
   ls -la --time-style=full-iso build/amd64/sysroot/.zedbsd-sysroot-complete   # 手順 5 の時刻であること（作り直された証拠）
   ```
7. 残りの参照が無いこと（他の WS の記録 `plan/**/*.md` は変えないので除く）:
   ```
   grep -rn --exclude-dir=.claude --exclude-dir=build --exclude-dir=.internal --exclude-dir=history --exclude-dir=ws104 \
     -e 'include/libc/\(keiland\|keiui\|truetype\|browser\|wayland\|xdg-shell\|primary-selection\|tablet-unstable\)' . | grep -v '^\./plan/.*\.md:' | wc -l
   ```
   `0`。
8. boot test（[commands.md](../commands.md) §4）:
   ```
   OUTPUT=build/ws104-p001/boot plan/tools/boot-test.sh build/amd64/hdd-image.img; echo "exit=$?"
   ```
9. host の試験（header の path を直した物から）:
   ```
   sh plan/tools/textedit/host-core.sh
   sh plan/tools/files/host-build.sh
   sh plan/ws089/tests/host-build.sh
   sh plan/ws100/tests/host-audio.sh
   ```
10. commit: `git commit -m WIP -- userland/desktop/keiland include/libc toolchain/llvm/sysroot.mk userland/desktop/libwayland userland/desktop/libbrowser plan/tools plan/ws0*`
    （`git status` で他の人の変更を含めていないか確かめてから）。

## 完了の条件

- 手順 5 の `make exit=0` と warning `0`、手順 6 の `SYSROOT-SAME` と stamp の時刻、手順 7 の `0`、手順 8 の `exit=0` と `boot-test: PASS`（PNG をユーザーに見せる）、
  手順 9 の 4 本が PASS（`host-audio: N/N passed`、他は exit 0）。

## 範囲の外

- header の中身の変更、`#include` の書き方の変更（`<keiland.h>` のまま）。
- pcat・pc98・arm64 の build（同じ sysroot.mk の manifest を使う。この Phase では amd64 だけ確かめ、他は「未実施」と書く）。
- `plan/ws045/tests/target-check.sh` の arm64 の行（`-Iinclude/libc` で libpdf の `truetype.h` を探す）は直さない（survey の判断、影響は小さい）。

## 結果

2026-10-01 / q515-i01: **cleared**。実装 commit: `12d7efeea05917a0d12c50a93824a6b6dc990c59`（`WIP`）。ユーザーの実行指示で承認された範囲を完了した。所要時間は約 6.0 分（60 分の timebox 内）。

| 確認 | command / 方法 | 結果 |
| --- | --- | --- |
| 基準 sysroot | `make -j64 sysroot-amd64` | exit 0 |
| patch | `git apply --check` の後、p001 の 2 patch を適用 | 成功。sysroot.mk の実差分と承認済み patch の stable patch-id は同一 |
| ヘッダーの内容 | 移動前の SHA256 と新しい path の内容を比較 | 24 file 全て同一、git rename は全て R100 |
| amd64 build | `make -j64 disk-image` | exit 0、自前の source の warning 0。外部 package の warning 行 254（Phase の指定 filter で除外） |
| sysroot | 全 file の名前・SHA256 を前後で比較、completion stamp の mtime 比較 | 241 file 同一、stamp は更新。sysroot が再生成された証拠あり |
| 旧 path | `.internal/`・build・履歴・WS104 の資料・他 WS の Markdown を除いた指定範囲を `rg` | 0 件 |
| Text Editor | `timeout 300 sh plan/tools/textedit/host-core.sh` | exit 0、34/34、warning 0 |
| Files | `timeout 300 sh plan/tools/files/host-build.sh` | exit 0、render/model の build 成功、warning 0 |
| Settings | `timeout 300 sh plan/ws089/tests/host-build.sh` | exit 0、warning 0 |
| Audio | `timeout 60 sh plan/ws100/tests/host-audio.sh` | exit 0、14/14、warning 0 |
| boot | `OUTPUT=build/ws104-p001/boot plan/tools/boot-test.sh build/amd64/hdd-image.img` | exit 0、PASS。PNG で login prompt を確認 |
| 差分 | `git diff --check`、変更 path の範囲検査 | PASS。準備済み patch の変更 path とヘッダーの移動だけ |
| toolchain 保護 | `sh plan/tools/toolchain-lock.sh status` | 前後とも共有 4 tree の writable directory は 0 |

証拠: [result.json](evidence/result.json)、[sysroot manifest](evidence/sysroot-include.sha256)、[移動前のヘッダー hash](evidence/headers-original.json)、[起動画面](evidence/login.png)。詳細の build/host/boot の log は `build/ws104-p001/`（ignored、ローカル）。host compiler は GCC 14.2.0、GNU Make 4.4.1。

全文規約を用いて差分を確認した。ヘッダー本文・実行可能な C の処理は変更せず、C source の差分は参照 path のコメントだけ。`clang-format` はこの host に無く、整形は未実施。WS 全体の最終 source の全文規約の確認は p008 に残る。

未実施: pcat・pc98・arm64 と実機。QEMU の証拠を実機の受け入れとは扱わない。残りの Phase p002・p004 が選定可能になった。WS104 は incomplete、p002〜p008 が残る。GitHub の Issue/comment/Project は未公開（publication はユーザー指示待ち）。
