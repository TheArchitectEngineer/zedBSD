<!-- awesome-plan project=zedbsd record=ws078-p003 -->

# ws078-p003: Keiland の部品を userland/desktop へ移し、実行ファイルを改名する

Status: cleared（2026-09-28、main が実施。commit 6d8ca152）
Parent: [WS078](../ws.md)

## 実施

- `userland/base` から `userland/desktop` へ移した（git mv）: zdesktop → wayland、zdesktop-x11server → xserver、
  zdesktop-browser → browser、zdesktop-terminal → terminal、zdesktop-files → files、zsessiond → sessiond、libzdesktop → libkeiland、
  libvulkan・libegl・libglesv2・libwayland・libwayland-egl・libtruetype・mview・egltest・wltest・wlshm・vkdemo（名前は同じ）。
- 実行ファイル: `/bin/wayland`・`/bin/xserver`・`/bin/browser`・`/bin/terminal`・`/bin/files`・`/sbin/sessiond`、`/lib/libkeiland.so`。
- package の登録名と id（`desktop/<name>`）、make の program の一覧（config/ci、試験の config）、試験の script の path と process 名、
  file 名（zsessiond.h → sessiond.h 等、`userland/base/licenses/browser`）、相対の include を合わせた。
  `userland/desktop/package.mk` は `userland/base/package.mk` を読む。
- plan/history と plan/ws078・master.md の決定の文は書き換えていない。

## 検証

- desktop の image（build/ws035-sq）の build: 成功（-Werror）。`make list-user-programs` に新しい名前と id。
- boot test: PASS（build/main-kei1-boot1/login.png。greeter を起こし、GPU の無い試験の image では console の login に戻る）。
- 未実施: Venus での graphical な login と session（`/bin/wayland` の起動、App Home からの terminal・files・browser の起動）、
  menuconfig の host 試験、pcat・rpi4 の build。

## 残り（後の Phase）

- menu の分類はまだ `base`（BUG-080 で Desktop の分類を作る）。
- データの path と名前: `/etc/zdesktop/session`、`/usr/share/zdesktop/`、font の `zdesktop*.ttf`、`/usr/libexec/zdesktop-x11`。
- API と protocol: `include/libc/zdesktop.h` と `zdesktop_` の接頭辞、`ZDESKTOP_*`、`zed_*` の protocol → `keiland_`（決定済み）。
- 見える文字列（label の "(zdesktop)" 等）は p004。
