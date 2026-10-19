<!-- awesome-plan project=zedbsd record=ws275-p005 -->

# ws275-p005 userland の config・Makefile・vmunix.mk・libX11 の BE、build

- 親: [ws275](../ws.md)、Queue: q1318 / q1318-i01（P1、2026-10-19）
- Status: cleared（2026-10-19 Q1 の判定: build warning 0・host 試験 PASS（X の動作は p006、BUG-337）。ユーザーの「全部 merge する」で main に merge（50bd10c5f）。QEMU のみ・実機は未実施）
- 設計: [design.md](../design.md) §4.6・§4.7・§5・§6・§7、決定 D6

## 受け入れの条件

1. top の Makefile の `ZEDBSD_GRAPHICS_BACKEND` に sparcv9 → sun4u、注記を直す。
2. sparcv9 の vmunix.mk: `CONFIG_DRIVER_GRAPHICS_DEVICE=y` の時だけ graphics-device.c、`$(ZEDBSD_GRAPHICS_CONFIG_STAMP)` の依存（object と vmunix）。
3. 試験の config `plan/ws275/tests/config-sun4u-x11.mk`（config-sun4u.mk の写しに graphics と Xzed・zterm・zwm・zshell）。
4. libX11 の property の 16・32 bit の単位を要素ごとに `w16`/`w32` で詰め、`XGetWindowProperty` で CPU の順へ戻す（m3）。
5. root の大きさを確かめる。graphics を含む sun4u の kernel が link する。build warning 0（sun4u・amd64・pcat・riscv64）、host 試験。
6. p006 の判定の値（zterm の prompt、mouse の座標、色の画素の点、shell のエラーの行）を決める。

## 行った事（2026-10-19 P1）

- **Makefile**: `ZEDBSD_GRAPHICS_BACKEND` に `$(if $(filter sparcv9,$(ZEDBSD_PLATFORM_DIR)),sun4u)`、注記を「PC/AT, PC-98 and sun4u
  (the firmware framebuffer)」に。
- **vmunix.mk**: graphics-device.c の条件付きの追加と firmware fb・vgafont.c・ps2-8042.c の source は p004 で入済み。p005 は
  `$(SPARCV9_VMUNIX_OBJS)` と `$(BUILD)/vmunix` に `$(ZEDBSD_GRAPHICS_CONFIG_STAMP)` の依存を足した（pcat l.82・362 と同じ形）。
- **root の大きさ**: X の 4 program は Xzed 96288・zterm 89632・zwm 71192・zshell 70064 byte、rootfs の tree は 7.4 MiB。60 MiB のまま（`--size-mib` は変えない）。
- **libX11**（`userland/x11/libX11/xlib.c`）: `XChangeProperty` は format 16・32 の各単位を native の `uint16_t`/`uint32_t` として読み（in-tree の
  `Atom` は `uint32_t`）、`w16`/`w32` で LE に詰める。`XGetWindowProperty` は受けた data を `r16`/`r32` で読み CPU の順に置き直す。format 8 は今まで通り。
- 試験の config と host 試験 `plan/ws275/tests/host-xlib-property.{c,sh}`（socketpair で wire の byte 順と往復を確かめる。LE の host では旧の
  memcpy も同じ byte になるので、BE の CPU の確かめにはならない）。

## 確かめ（2026-10-19 P1、host のみ）

- `make ZEDBSD_CONFIG=plan/ws275/tests/config-sun4u-x11.mk BUILD=build/ws275-p005 -j16 build/ws275-p005/hdd-image.img` rc 0、`warning:`/`error:` 0 件。
  graphics-device.o を含む vmunix 1105712 byte、`SPARC V9 kernel check: PASS`、`SPARC V9 image check: PASS`、stamp `CONFIG_DRIVER_GRAPHICS_DEVICE=y`。
  rootfs に `/bin/Xzed`・`/bin/startx`・`/etc/Xzed/Xzedrc`・`zwm.conf`・`/usr/share/zwm/backgrounds/*.ppm`。
- `make ZEDBSD_CONFIG=plan/tools/ports/config-sun4u.mk BUILD=build/ws275-p005/sun4u vmunix bootloader` rc 0・0 件（stamp `=n`、graphics 無し）。
- `make ZEDBSD_CONFIG=config/ci/config-{pcat,amd64,riscv64-virt}.mk BUILD=build/ws275-p005/<名前> vmunix -j16` 各 rc 0・0 件。
- host 試験: `host-xlib-property.sh` PASS、`host-graphics-device.sh`・`host-firmware-fb.sh`・`host-handoff.sh` rc 0。
- 未実施: QEMU（p006 の T1）、BE の CPU での libX11 の往復（p006 の zterm の貼り付けは format 8 なので直接は確かめない）。

## p006 の判定の値（コードから決めた、未確認）

- 画面: startx は `Xzed --size 800x600` だが、firmware fb の mode は 1 つなので Xzed の `choose_mode` は寸法を緩めて firmware の 1024x768 を選ぶ
  （xzed/main.c の pass 2・3）。wallpaper は 1024x768.ppm（zwm の選び方は未確認、PNG で確かめる）。
- zterm の原点 (22,34)、cell 8x16。prompt は sh の既定 `%s@%s:%s$ `（sh/main.c）で root なら `root@<host>:~$ `。
  期待: `expect:^root@\S+:\S*\$`。
- 出力の行 `ws275-ok` は row 1。語の中央（列 4）: **`mouse:58,58`**（`mouse:-2000,-2000` で左上に寄せた後、x=22+4×8+4、y=34+16+8）。
- 貼って Enter の後の行: 対話の sh の `sh_warn` は `sh: ` を前に付ける（sh/util.c）ので **`expect:^sh: ws275-ok: not found`**。
- zshell の bar: window は y=744〜767（`height - TASKBAR_HEIGHT`）、task は x=34 から 160 ずつ、status は x≥912。**(800,756)** は bar の地の
  0x17212b（8 bpp の期待 (0x00,0x24,0x00) ±4）。wallpaper の点は design §5 の (1016,200)（PPM の値を T1 の script が読む）。

## T1 への依頼文（p006 で Q1 が投げる）

```
WS275 p006（sun4u の /dev/graphics と Xzed、QEMU のみ）。image: make ZEDBSD_CONFIG=plan/ws275/tests/config-sun4u-x11.mk BUILD=build/<T1 の名前> <BUILD>/hdd-image.img
3 本: QEMU_EXTRA='-g 1024x768x8'、'-g 1024x768x32'、無し。各本 BOOT_TEXT_ORIGINS="22,34 20,8"、BOOT_PATTERN='^(?:\S+ )?login: ?'
BOOT_THEN: type:root / type:startx & / expect:^root@\S+:\S*\$ / keys:echo ws275-''ok / expect:^ws275-ok(\s|$) /
  mouse:-2000,-2000 / mouse:58,58 / mouse:0,0,left / mouse:0,0,left / mouse:0,0,middle / keys: / expect:^sh: ws275-ok: not found
最後の PNG で画素: (800,756) が 0x17212b（8 bpp は (0x00,0x24,0x00) ±4）、(1016,200) が userland/x11/zwm/backgrounds/1024x768.ppm の同じ点
（8 bpp は RGB332 の量子化 ±4）。原点が合わなければ PNG から原点を求めて返す。PNG は全部 Q1 へ。
回帰: pcat bios-ide login、pcat PS/2（keys: を含む試験と plan/ws081/tests/ps2-wheel-qemu.sh）、amd64 uefi-nvme login、sun4u login（config-sun4u.mk）。
```

## 変えた file

- sparcv9・sun4u: `platform/sparcv9/vmunix.mk`（stamp の依存）。
- 他: `Makefile`、`userland/x11/libX11/xlib.c`、`plan/ws275/tests/config-sun4u-x11.mk`・`host-xlib-property.{c,sh}`（新）、本 file、ws.md、design.md §6 の 1 行。

## 残り

- QEMU の確認（p006）。BE での libX11 の往復は実の format 16/32 の property の利用者が出た時。
- 片付け（Q1）: `build/ws275-p005/`（worktree の中）。
