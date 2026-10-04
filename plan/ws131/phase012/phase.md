<!-- awesome-plan project=zedbsd record=ws131-p012 -->

# ws131-p012: libkeiui を libkeiland へ移す（名前は変えない）

Status: in-progress（q673、P2、2026-10-04。ユーザーの承認「WS13(1),WS128は進めてOKです。」）
Disposition: normal
Parent: [WS131](../ws.md)、計画の正本 [design.md](../design.md)
Queue: [q673](../../queue.md)
委任（Q1、2026-10-04）: 所有 path の「Q1 の委任が要る」の項（`platform/amd64/vmunix.mk`、`config/ci/config-amd64.mk` と試験の config 4 本、`plan/tools/keiland-linux/elf-check.sh`、§2.4 の host の試験 13 file の path）を q673 の P2 に委任する。toolchain・HAL の API は範囲外
依存: p011 cleared（D8 の番号の順）、p003 が main に統合済み。WS090 は WS131 の間は動かない（D9 の決定）。判断 D10（承認済み）
目安: 4h（1 Queue）。実行者: Q1 が割り当てる（high）
所有 path: `userland/desktop/libkeiui/` → `userland/desktop/libkeiland/ui/`、`userland/desktop/libkeiland/`（Makefile 3 本・exports.map）、利用者の build の file（Text Editor・Image Viewer・PDF Viewer・Notes・Terminal・keiland-ime・kuidemo・**Files**）、`userland/desktop/files/ui-scrollbar.c` の include、`keiland-linux.mk`・`keiland-freebsd.mk`（package の一覧と公開の header の表 `:184-187`）、`plan/ws131/`。（Q1 の委任が要る: Q1 が Phase の前に委任を記録する）: `platform/amd64/vmunix.mk`（libkeiui.so の規則と 7 つの link）、`config/ci/config-amd64.mk:37` と試験の config 4 本の package 名、`plan/tools/keiland-linux/elf-check.sh`、§2.4 の host の試験 13 file の path

## 目的と結果

libkeiui の source を `libkeiland/ui/` に移し、`libkeiui.so` を無くす。名前はまだ `kui_` のまま（改名は p013）。app の source は include を除き無変更。

## 範囲

1. `git mv userland/desktop/libkeiui/* userland/desktop/libkeiland/ui/`。`picture/color-glyph.c` を libkeiland の source 一覧へ。
2. build: libkeiland の Makefile 3 本に `ui/` と依存（libpng-compat・libz-compat・libvulkan、Linux では libtruetype も）。`vmunix.mk` の libkeiui.so の規則（`:882-896`）と image の data を除き、7 つの app の link から `libkeiui.so` を除く。Files の 3 本の Makefile の `libkeiui/scroll-bar.c` を新しい path に（または libkeiland の link に替える）。`keiland-linux.mk:109`・FreeBSD の同等の include を除く。
3. 取りこぼし（review 2）: package 名 `libkeiui` の 5 か所（design.md §2.4）、FreeBSD の公開の header の表（`keiui.h` は p013 まで残す）、`elf-check.sh:17`、Linux・FreeBSD の source の install で古い `libkeiui.so` を消す規則。最初に `grep -rn libkeiui` を tree 全体で流し、全ての所を phase.md に列挙してから直す。
4. exports.map を公開の header の関数の一覧から生成する形に（design.md §5.4、B4）。

## 受け入れ

- 3 OS の build（zedBSD の amd64 は exit 0・自前の warning 0、`make keiland-linux` の gcc と clang、FreeBSD の native build と `native-build-audit.py`）と `plan/tools/keiland-os-boundary/check.sh` PASS。
- `grep -rn libkeiui` が tree（history と plan の文書を除く）で 0。`nm -D libkeiland.so` が旧 libkeiui.so と旧 libkeiland.so の公開の関数の和と一致し、内部の名前を出さない（B4）。
- zedBSD: ws090 の host 試験（host-draw・host-input・host-widgets）・`plan/tools/keiui/host-chooser.sh`・`plan/ws127/tests/scroll-bar-test.sh`、`textinput-p013.sh`、`viewers-p008.sh`、`demo-s8-s9.sh`、Files の `files-regress.sh`、boot-test。Linux: 8 つの app の起動の PNG、`elf-check.sh`。

## 検証の方法

[zedbsd-commands.md](../../tools/keiland-linux/zedbsd-commands.md)、[keiland-linux/README.md](../../tools/keiland-linux/README.md)、[keiland-freebsd/README.md](../../tools/keiland-freebsd/README.md)。QEMU の console・serial の log で判定しない（不具合は gdbstub・monitor・QMP）。build は自分の `BUILD=build/ws131-pNNN/`、共有の `build/` を消さない。FreeBSD の起動の確認は判断 D11 の範囲（passthrough なし）。QEMU と実機の証拠を分ける。

## 衝突・危険・rollback

- 衝突: 全 app の build の file と `vmunix.mk` を触る。D8 の単独走行の間は他の担当が動かない。 順は Q1 が決める（design.md §7.2）。
- rollback: 前へ直すのを基本にし、後ろに同じ file の変更が無い時だけ統合の commit を revert する（design.md §7.4）。

## Resume

依存の Phase の cleared と main への統合、関係する判断の決定の後に、Q1 が Queue を作る。

## 着手前の調べ（P2、2026-10-04、main 3f059b5）

範囲 3 の「最初に tree 全体を grep し、全ての所を列挙する」の結果。`git grep -l libkeiui` で history・plan の文書・docs を除くと 106 file ある。参照の多い file（件数）:

- `platform/amd64/vmunix.mk`（33）
- `userland/desktop/libkeiui/Makefile.linux`（27）
- `userland/desktop/libkeiui/Makefile.freebsd`（27）
- `plan/ws114/evidence/q587/linux-build.log`（27）
- `userland/desktop/libkeiui/Makefile`（14）
- `userland/desktop/textedit/main.c`（12）
- `plan/ws106/programs-before.txt`（7）
- `userland/desktop/textedit/app.c`（5）
- `userland/desktop/pdfviewer/main.c`（5）
- `userland/desktop/terminal/window.c`（4）
- `userland/desktop/terminal/terminal.h`（4）
- `userland/desktop/notes/main.c`（4）
- `userland/desktop/imageview/main.c`（4）
- `plan/ws090/tests/host-draw.c`（4）
- `plan/master.md`（4）
- `userland/desktop/textedit/textedit.h`（3）
- `userland/desktop/notes/window.c`（3）
- `userland/desktop/imageview/window.h`（3）
- `plan/ws102/tests/host-inset.sh`（3）
- `plan/ws101/p011-toolchain/config.diff`（3）
- `plan/ws090/tests/viewers-p008.sh`（3）
- `plan/ws090/tests/host-input.sh`（3）
- `plan/ws090/tests/host-draw.sh`（3）
- `plan/ws090/tests/config-amd64-textinput.mk`（3）
- `plan/tools/textedit/host-core.sh`（3）
- `plan/tools/keiui/host-chooser.sh`（3）
- `userland/tests/kuidemo/Makefile`（2）
- `userland/desktop/textedit/window.h`（2）
- `userland/desktop/textedit/queue.c`（2）
- `userland/desktop/textedit/Makefile`（2）
- `userland/desktop/pdfviewer/window.h`（2）
- `userland/desktop/pdfviewer/Makefile`（2）
- `userland/desktop/notes/app.h`（2）
- `userland/desktop/libkeiui/shaders/regenerate.py`（2）
- `userland/desktop/keiland/keiland.h`（2）
- `userland/desktop/imageview/Makefile`（2）
- `userland/desktop/files/Makefile`（2）
- `plan/ws127/tests/scroll-bar-test.sh`（2）
- `plan/ws102/tests/inset-guest.sh`（2）
- `plan/ws090/tests/host-widgets.sh`（2）
- `plan/ws090/tests/host-widgets.c`（2）
- `plan/tools/files/host-build.sh`（2）
- `userland/tests/kuidemo/main.c`（1）
- `userland/tests/kuidemo/Makefile.linux`（1）
- `userland/tests/kuidemo/Makefile.freebsd`（1）
- `userland/packages/fonts/noto-color-emoji/Makefile`（1）
- `userland/desktop/wayland/inset.c`（1）
- `userland/desktop/wayland/edit.c`（1）
- `userland/desktop/textedit/draw.c`（1）
- `userland/desktop/textedit/Makefile.linux`（1）
- `userland/desktop/textedit/Makefile.freebsd`（1）
- `userland/desktop/terminal/main.c`（1）
- `userland/desktop/terminal/Makefile.linux`（1）
- `userland/desktop/terminal/Makefile.freebsd`（1）
- `userland/desktop/terminal/Makefile`（1）
- `userland/desktop/picture/color-glyph.h`（1）
- `userland/desktop/pdfviewer/viewer.h`（1）
- `userland/desktop/pdfviewer/view.c`（1）
- `userland/desktop/pdfviewer/touch.c`（1）
- `userland/desktop/pdfviewer/draw.c`（1）
- `userland/desktop/pdfviewer/Makefile.linux`（1）
- `userland/desktop/pdfviewer/Makefile.freebsd`（1）
- `userland/desktop/notes/Makefile.linux`（1）
- `userland/desktop/notes/Makefile.freebsd`（1）
- `userland/desktop/notes/Makefile`（1）
- `userland/desktop/monitor/main.c`（1）
- `userland/desktop/monitor/atlas.c`（1）
- `userland/desktop/monitor/Makefile.linux`（1）
- `userland/desktop/monitor/Makefile.freebsd`（1）
- `userland/desktop/monitor/Makefile`（1）
- `userland/desktop/libkeiui/list.c`（1）
- `userland/desktop/libkeiland/Makefile`（1）
- `userland/desktop/keiland-linux.mk`（1）
- `userland/desktop/keiland-freebsd.mk`（1）
- `userland/desktop/ime/Makefile.linux`（1）
- `userland/desktop/ime/Makefile.freebsd`（1）
- `userland/desktop/ime/Makefile`（1）
- `userland/desktop/imageview/view.c`（1）
- `userland/desktop/imageview/touch.c`（1）
- `userland/desktop/imageview/imageview.h`（1）
- `userland/desktop/imageview/draw.c`（1）
- `userland/desktop/imageview/Makefile.linux`（1）
- `userland/desktop/imageview/Makefile.freebsd`（1）
- `userland/desktop/files/ui-scrollbar.c`（1）
- `userland/desktop/files/Makefile.linux`（1）
- `userland/desktop/files/Makefile.freebsd`（1）
- `plan/ws134/tests/host/preview.sh`（1）
- `plan/ws128/tests/terminal-p011-ime.sh`（1）
- `plan/ws128/tests/notes-p002.sh`（1）
- `plan/ws127/tests/scroll-bar-test.c`（1）
- `plan/ws106/survey.json`（1）
- `plan/ws102/tests/osk-guest.sh`（1）
- `plan/ws102/tests/host-inset.c`（1）
- `plan/ws102/tests/edit-guest.sh`（1）
- `plan/ws090/tests/textinput-p013.sh`（1）
- `plan/ws090/tests/host-input.c`（1）
- `plan/ws081/tests/config-amd64-demo-win.mk`（1）
- `plan/ws079/tests/run-pdfviewer-host.sh`（1）
- `plan/ws079/tests/host-pdfviewer.c`（1）
- `plan/ws074/phase172/import/browser2-evidence/plan/history/index.md.source.txt`（1）
- `plan/ws035/tests/config-amd64-zdesktop.mk`（1）
- `plan/ws035/tests/config-amd64-userland.mk`（1）
- `plan/tools/keiland-linux/elf-check.sh`（1）
- `plan/tools/imageview/run-host.sh`（1）
- `plan/queue.md`（1）
- `config/ci/config-amd64.mk`（1）

- 実装は未着手。P2 のコンテキストが重くなったので、Q1 が新しい generation に引き継がせる（2026-10-04）。依存の「ベータ1 の app の区切り」は Q1 が判断する（D8 の単独走行で p011 の後に進める指示を受けた）。

## 止める理由（Q1、2026-10-04）

p012 の依存「ベータ1 の app の区切り」は Master の pending decision（WS131 の時期: backend を先に、app の移行は標準 app の区切りに）でユーザーの判断が要る。一方で WS127・WS128 は「標準 app の開発は WS131 の後」と書かれており、順の決めがユーザーに要る。ユーザーの夜の指示「WSが完了できないときは他のWSに移る」により、p012 は planning のまま止め、P2 は WS134 p004 へ移る。要る判断: WS131 の app の移行（p012 以降）と標準 app のベータ1 の作業（WS127・WS128）のどちらを先にするか。

2026-10-04 Q1（user「任せます」）: 標準 app のベータ1 の作業（WS127・WS128）を先にし、WS131 の p012 以降（app の移行）はベータ1（2026-10-17）の後に再開する。

2026-10-04 昼 user「WS13,WS128は進めてOKです。」（直前の「WS131,」と合わせて WS131 と読む）→ ベータ1 の後に回した Q1 の決めを取り消し、p012 以降を進めてよい。

## 実施（q673-i01、P2 generation10、2026-10-04、main c21f9ab の上）

### 1. 移動
- `git mv userland/desktop/libkeiui/* userland/desktop/libkeiland/ui/`（source・header・`shaders/`）。`ui/Makefile`・`Makefile.linux`・`Makefile.freebsd`・`exports.map` は除いた（root の Makefile は `userland/*/*/*/Makefile` を package として読むので、残すと別の package になる）。`ui/text.c` の `../picture/color-glyph.h` を `../../picture/color-glyph.h` に。`shaders/regenerate.py` の path の文。

### 2. build
- libkeiland の Makefile 3 本に `ui/` の 25 file と `picture/color-glyph.c`。zedBSD の package の REQUIRE に `desktop/libvulkan base/libpng-compat base/libz-compat`。Linux・FreeBSD の link に `libtruetype.so libpng-compat.so libvulkan.so.1`。
- `platform/amd64/vmunix.mk`: libkeiui.so の規則を除き、libkeiland.so に libvulkan・libpng-compat・libz-compat の link と NEEDED、link の前に `exports.py --check`。7 つの app（ime・ime-probe の client を含む）の link・依存・NEEDED から libkeiui.so を除いた。
- app の Makefile: Linux・FreeBSD の `libkeiui.so libkeiland.so` → `libkeiland.so`（textedit・imageview・pdfviewer・notes・terminal・ime・kuidemo・**monitor**（WS134、§2.4 の表に無かった））、zedBSD の REQUIRE の `desktop/libkeiui` を除く（ime は `desktop/libkeiland` に）、Files の 3 本の `scroll-bar.c` の path。
- `keiland-linux.mk`・`keiland-freebsd.mk`: libkeiui の Makefile の include を除き、install で古い `lib/libkeiui.so` を消す（`KEILAND_*_RETIRED`）。FreeBSD の公開の header の表の `keiui.h` は p013 まで残す。

### 3. 取りこぼし
- package 名: `config/ci/config-amd64.mk`（除く、libkeiland は既に在る）、`plan/ws035/tests/config-amd64-zdesktop.mk`・`config-amd64-userland.mk`・`plan/ws081/tests/config-amd64-demo-win.mk`・`plan/ws090/tests/config-amd64-textinput.mk` と、§2.4 の表に無かった `plan/ws128/tests/config-amd64-imageview.mk`（→ libkeiland）。
- `plan/tools/keiland-linux/elf-check.sh` の我々の library の表から除いた。host の試験の path（`$U/libkeiui` → `$U/libkeiland/ui`）。`plan/ws134/tests/host/preview.sh` の `-lkeiui` も除いた（未実行）。
- comment の `libkeiui` は `libkeiland` に（userland と plan の試験の script・C）。
- `grep -rn libkeiui`（plan の文書・history を除く）: 残りは install で古い library を消す 2 行（`keiland-linux.mk`・`keiland-freebsd.mk` の `*_RETIRED := lib/libkeiui.so`）だけ。消す規則そのものが名前を要る（意図した例外）。

### 4. exports.map
- `userland/desktop/libkeiland/exports.py` が `keiland.h`・`keiui.h` の top level の関数の宣言を名前で並べて `exports.map` を書く（`--check` で照合、`--list`）。glob は無い。
- 変わったこと: 旧 libkeiland の `keiland_desktop_*` の glob が protocol の表 `keiland_desktop_manager_v1_interface`・`keiland_desktop_surface_v1_interface` を出していた。tree の中に外の利用者は無く、design.md §5.4 どおり出さない。

### 確かめ
- zedBSD: `make ZEDBSD_CONFIG=plan/ws129/tests/config-amd64-ci-noclang.mk BUILD=build/p2-ci build/p2-ci/rootfs/.stamp` exit 0・自前の warning 0。kuidemo・monitor も build exit 0。rootfs に libkeiui.so 無し。`llvm-nm -D` の libkeiland.so（271）= header の一覧 = 旧 libkeiland（122）と旧 libkeiui（151）の和から上の protocol の表 2 つを除いたもの。libkeiland.so の NEEDED は libwayland-client・libtruetype・libvulkan・libpng-compat・libz-compat・libc。8 つの app と keiland-ime の NEEDED に libkeiui 無し。
- Linux: `make keiland-linux` の gcc・clang とも exit 0・warning 0。install の stage に libkeiui.so 無し（前からあった物は install で消えた）、`elf-check.sh` PASS（24 ELF）、`makefile-sync.sh` PASS、`header-check.sh` PASS（381）、`nm -D` = header の一覧。
- FreeBSD: `make -n keiland-freebsd` で `libkeiland/ui` の compile と link の規則まで。native build・`native-build-audit.py` は未実施（FreeBSD の guest は T に依頼）。
- `plan/tools/keiland-os-boundary/check.sh` PASS。`menuconfig-target-host-test.py` PASS。
- host の試験: `host-draw` 13/13、`host-input` 63/63、`host-widgets` 94/94、`host-chooser` 85/85、`scroll-bar-test` PASS、`host-inset` PASS、`textedit/host-core` 53/53、`files/host-default` PASS、`imageview/run-host` PASS。`run-pdfviewer-host.sh` は前提の `run-pdf-render.sh` が要り未実施。
- 古い試験の直し（main c21f9ab の素の tree でも同じく失敗していた）: host-draw・host-widgets・host-chooser に `-I.`（`ui/text.c` が `userland/desktop/paths.h` を include する）、host-input・host-inset・host-widgets・host-chooser に `scroll-bar.c`（`ui/scroll.c` が `kui_scroll_bar_*` を呼ぶ）。
- QEMU（`textinput-p013.sh`・`viewers-p008.sh`・`demo-s8-s9.sh`・`files-regress.sh`・boot-test）と Linux の 8 app の PNG、FreeBSD の native build は T1/T2 に最後にまとめて依頼する。

## Q1 の判定（2026-10-04）

q673 は報告どおり受け入れ（Q1）。`grep -rn libkeiui` の残り 2 行（`keiland-linux.mk`・`keiland-freebsd.mk` の `*_RETIRED := lib/libkeiui.so`、install で古い library を消す規則）は受け入れの例外として認められた。委任の外の直し（ws128 の imageview の config、ws134 の preview.sh、comment の文字）も受け入れ。merge は UAT の image の build とユーザーの `config/ci/config-amd64.mk` の変更の整理の後に Q1 が行う（CI の config の `libkeiui` の行は Q1 が merge で解く）。QEMU・Linux の PNG・FreeBSD は T2 の結果待ち。
