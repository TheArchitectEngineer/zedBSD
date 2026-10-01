<!-- awesome-plan project=zedbsd record=ws105-p011 -->

# ws105-p011: 規約の全文の見直し、境界の確かめの拡張、回帰、install の文書

Status: planned
Disposition: normal
Parent: [WS105](../ws.md)
Queue: なし
依存: p001〜p010
実行者: phase-runner（high）。`plan/tools/` の変更と master の更新は main

## 目的

WS105 の完了の前の、規約の全文による見直し（Awesome Plan の code を作る WS の必須の Phase）、OS の境界の確かめを Linux の側に広げること、Linux と zedBSD の回帰、install の文書。

## 手順

1. **見直し**: WS105 の全ての source の変更（`userland/desktop/libvulkan-compat/`・`*/linux/`・`*/wpa/`・`linux-compat/`・`keiland-linux.mk`・全ての `Makefile.linux`・共通の file の変更・
   `plan/tools/keiland-linux/`）を [coding-style.md](../../coding-style.md) の全文に照らす。違反を直す。理由のある例外は記録する。
2. **境界の確かめ**（`plan/tools/keiland-os-boundary/check.sh`、WS104 p008 の物を main が直す）に足す:
   - L1: 共通の source（`zedbsd/`・`linux/`・`wpa/` の外）に `#if defined(__linux__)`・`#ifdef __linux__` が無い（`wayland/zwl-evdev.h` を除く）。
   - L2: `linux/`・`wpa/` の source が `<uapi/`・`"userland/base/net/`・`"userland/base/audiod/` を include しない（zedBSD の物を Linux に持ち込まない）。
   - L3: `zedbsd/` の source が `<linux/`・`<drm/`・`<sound/` を include しない。
   - L4: libvulkan-compat が zedBSD の `userland/desktop/libvulkan/` の file を include・compile しない（D7）。
3. **install の文書** `userland/desktop/LINUX.md`: 要る host の package（Debian 13 の名前: `build-essential`・`libvulkan-dev`・`linux-libc-dev`・`python3`、試験には
   `libwayland-dev`・`wayland-protocols`・`mesa-vulkan-drivers`・`vulkan-tools`・`qemu-system-x86`・`mmdebstrap`）、build（`make keiland-linux`）、install
   （`sudo make keiland-linux-install`、`keiland-linux-install-session`）、起動（text console から root で `openvt`、gdm）、環境変数（`KEILAND_VULKAN_BACKEND`・
   `KEILAND_VULKAN_DEEPBIND`・`KEILAND_DRM_DEVICE`・`KEILAND_SEAT`）、範囲の外（design §8）、仕組みへの link（design.md）。
4. **回帰（Linux）**: `make keiland-linux-clean` の後に gcc と clang で build、`elf-check.sh`・`makefile-sync.sh`、host の p003・p004 の試験、guest の p005〜p010 の確かめを
   新しい guest の image で通しで（`build-guest.sh --force`）。
5. **回帰（zedBSD）**: design §9.2 の全部（build、gpu-boundary の一式、`criteria.sh ... C1 C2 C9`、`zdesktop-p059.sh`、`settings-regress.sh`、`host-audio.sh`、
   keiland-os-boundary の check、boot test）。
6. WS105 を完了の形にする（main）: ws.md を書き直す（結果・制限・移管）、Phase の directory を削除し、続けて使う試験は `plan/tools/keiland-linux/` に残して master の Tools 節に登録、
   [F-065](../../future/F-065-keiland-portable.md) と Future Work の表を更新（Linux は WS105 で済んだ、FreeBSD と §8 の範囲の外は残る）、Master の registry・Past Log。

## 完了の条件

- 見直しの違反が 0（または理由つきの例外）。check.sh が PASS。Linux と zedBSD の回帰が全て PASS（未実施の物は理由つき）。LINUX.md がある。

## 結果

（実行の後に書く）
