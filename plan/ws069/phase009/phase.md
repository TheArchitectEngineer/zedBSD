<!-- awesome-plan project=zedbsd record=ws069p009 -->

# ws069-p009: Xzed をレトロ用（`/dev/graphics`）に戻す

Phase ID: `ws069-p009`
Parent: [WS069](../ws.md)
Status: cleared（q490-i01、2026-09-27）
Phase disposition: normal
Queue: q490-i01
承認: 2026-09-27 ユーザー「Zxedはレトロコンピュータ用のデモなので、元に戻してOKです。」、「続けてください。」

## 範囲

1. `userland/X11/xzed` を ws069 の前（`cc4433d4`）へ: `main.c`・`input.c`・`input.h`・`Makefile` を戻し、ws069 で足した `wayland.c`・`wayland.h`・
   `glyphs.c`・`glyphs.h`・`glx.c`・`glx.h` を消す（Wayland・rootless・GLX は zdesktop-x11server に移った、p008）。
2. `platform/amd64/vmunix.mk` の amd64 の動的な Xzed（`-DXZED_WAYLAND`、libwayland・libtruetype に link）の規則を消し、Xzed は他の
   platform と同じ普通の道で build する。
3. rootful の試験 `plan/ws069/tests/x11-p002.sh`（Xzed の Wayland backend）を消す（動かない試験は削除、2026-09-24 の判断）。
4. libX11（client 側の `XPending`・`wr()` の修正、`XzedPutImageRGB24` 等）はそのまま（zdesktop-x11server と libGL が使う）。

## 受け入れ

1. build: amd64 の zdesktop の image（Xzed を含む）と PC/AT の Xzed・zterm・zshell・zwm が warning 0。
2. Venus の guest で zdesktop を止め、`/dev/graphics` の上で `Xzed --size 800x600` と zterm が描かれる（画面を撮る）。boot test。
3. zdesktop-x11server の回帰（x11-p005）が変わらず PASS（Xzed を使わないことの確認）。

## 結果（2026-09-27、q490-i01）

- `userland/X11/xzed` の `main.c`・`input.c`・`input.h`・`Makefile` を `cc4433d4` に戻し、`wayland.[ch]`・`glyphs.[ch]`・`glx.[ch]` を消した
  （`git diff cc4433d4 -- userland/X11/xzed` は空）。`platform/amd64/vmunix.mk` の amd64 の動的な Xzed の規則（`-DXZED_WAYLAND`）と
  filter-out の Xzed を消し、Xzed は他と同じ道で build する（NEEDED は libc.so だけ）。`plan/ws069/tests/x11-p002.sh`（rootful）を削除。

## 検証

- build: amd64 の zdesktop の image warning 0（`/bin/Xzed` は libwayland を持たない）。PC/AT の Xzed・zterm・zshell・zwm warning 0。
- QEMU（std VGA、`plan/tools/guest/guest.py start build/ws035-sq/hdd-image.img`）: `/bin/Xzed --size 800x600 -- /bin/zterm` が `/dev/graphics`
  に描く（`build/ws069-p009/xzed-vga.png`: 背景、zterm の prompt、Xzed の cursor。std VGA の近い mode 640x480）。
  Venus の guest の virtio-gpu には `/dev/graphics` は描かない（`Display output is not active`、道が違うため。問題ではない）。
- Venus: x11-p005 PASS（zdesktop-x11server は Xzed を使わない）。boot test PASS（`build/ws069-p009-boot/login.png`）。実機は未実施。
