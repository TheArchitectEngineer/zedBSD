<!-- awesome-plan project=zedbsd record=ws087-p005 -->

# ws087-p005: PS/2 keyboard の E0 の key が捨てられる（BUG-103 の第 2 の原因）

Status: cleared（2026-09-29、subagent の worktree `wt/ws087`）
Disposition: normal
Parent: [WS087](../ws.md)
Queue: main が割り当てた（2026-09-29「原因を特定したあなたに任せます」。HAL の API は変えない）

## 目的と受け入れ条件

[p002](../phase002/phase.md) の調査で見つけた不具合の修正: `src/drivers/platform/pcat/ps2-8042.c` の `keyboard_build_capabilities()` が E0 の付かない
scan の表だけから capability を作るため、input layer（`drv_input_device_emit()` は capability に無い code を捨てる）が矢印・Home・End・PageUp/Down・
Insert・Delete・右 Ctrl/Alt・Meta の event を evdev にも console にも出さない。

受け入れ: PS/2 だけの QEMU で矢印・Home・End・Delete が evdev と Terminal に届く。USB keyboard の回帰が無い。boot test。

## 実装

`keyboard_build_capabilities()` を、`extended` の 0 と 1 の両方について `scan_symbol(scan, extended)` を引き、得た code を重複なく capability に足す形にした
（`keyboard_capabilities[256]` に収まる）。上限に達したときの `break` は、二重の loop を抜ける `return` にした（以前も関数の終わりだった）。HAL の API は変えていない。

## 試験の結果（QEMU、amd64）

- build（`plan/tools/guest/build-ssh-image.sh build/amd64`、`-Werror`）: exit 0、warning 0。
- evdev（ssh の image、QMP の `device_del` で usb-kbd を外して PS/2 だけ、`dd if=/dev/input/event2`）: a・上・下・左・右・Home・End・Delete・PageUp・
  PageDown・Insert の押下と離しがすべて出た（修正前の image では a だけ）。
- Terminal: main の `build/ws035-sq/hdd-image.img` の複写（`build/ws087/zd-fixed/hdd-image.img`）の ESP の `vmunix` を、この修正の kernel に
  `mcopy` で差し替えて起動（Venus）。`stty raw -echo; dd bs=1` に a・上・下・左・右・Home・End・Delete・Tab・b を送った結果:
  - USB keyboard（回帰）: `a 033 [ A 033 [ B 033 [ D 033 [ C 033 [ H 033 [ F 033 [ 3 ~ \t b`。
  - usb-kbd を外した PS/2 だけ: 同じ byte 列（修正前は `a \t b` だけ）。
  - PS/2 だけで Terminal の sh に `echo ps2 >> /tmp/count`、上 Enter、上 Enter → 3 行（worktree の `build/ws087/shot-ps2-fixed.png`）。
- boot test（`OUTPUT=build/ws087/boot-p005 plan/tools/boot-test.sh build/amd64/hdd-image.img`）: PASS。

## 注記: zdesktop の image を worktree で build しようとした件

Terminal の試験のため `plan/ws035/tests/build-zdesktop-image.sh build/ws087-zd` を走らせたところ、`userland/packages/devel/libcxx` が共有の
`build/llvm-source`（worktree の symlink）に patch を当てようとし、toolchain の lock（読み取り専用）で `Permission denied` になって止まった。
共有の `build/llvm-source`・`build/llvm`・`build/NoctLang` にこの build より新しい file が無いことを `find -newer` で確かめた（変更なし）。
worktree の中にできた `build/llvm-build`（cmake の configure だけ）・`build/packages`（libcxx の source の複写）・`build/ws087-zd` は削除した。
以後、zdesktop の image が要る試験は main の image の ESP の kernel を差し替える方法を使う。

## 未実施

- 実機（5330 の内蔵 keyboard）での確認（手順は p002、main が預かった）。
