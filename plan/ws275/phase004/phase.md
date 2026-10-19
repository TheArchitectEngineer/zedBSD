<!-- awesome-plan project=zedbsd record=ws275-p004 -->

# ws275-p004 firmware fb の backend、text の console と出力の道、8042 の共有と sun4u の poll

- 親: [ws275](../ws.md)、Queue: q1314 / q1314-i01（P1、2026-10-19）
- Status: cleared 待ち（実装・build warning 0・host 試験 PASS、2026-10-19 P1。QEMU の確認は T1 の依頼（下）。sparcv9 の差分はユーザーに見せてから merge）
- 設計: [design.md](../design.md) §4.2・§4.3・§4.5・§6・§7、決定 D2 (a)・D5 (a)・D6（P が変え、merge の前に差分を見せる）

## 受け入れの条件

1. `sun4u-firmware-fb.c`: HAL の `sun4u.framebuffer.*` を写像し、BGRX8888・XRGB8888・RGB332 を描く（IE の窓に 32 bit の store、64 bit は使わない）。
   `/dev/graphics` の backend を firmware の優先度で登録（`CONFIG_DRIVER_GRAPHICS_DEVICE` の時）。mode は 1 つ、glyph は vgafont.c の 8x16。
2. `sun4u-console.c`: framebuffer がある時は grid（80 列 × 高さ/16 行）と 16550 の THR に直に書き、`kernel_putc` を publish。
   suspend/resume（resume は全体を消して描き直す）、`reveal_fatal`。無い時は今の `hal_putc` の道。
3. `sun4u.c`: `kern_platform_debug_write` は publish 後は何もしない。backend の登録、8042 を HAL の port で poll で起動。
4. 8042 の driver を `src/drivers/isa/ps2-8042.c` へ移し、port・IRQ か poll を引数に。poll の時は driver が config byte を書く。pcat・amd64 は今の動作。
5. build warning 0（sun4u・amd64・pcat・riscv64）、host 試験。hal.h は不変。

## 行った事（2026-10-19 P1）

- **8042（D5 (a)）**: `git mv` で `src/drivers/isa/ps2-8042.c` へ。`struct drv_isa_ps2_8042_config`（data・status の port、keyboard・mouse の IRQ、
  名前と path）と `drv_isa_ps2_8042_init()`（`include/drivers/isa/ps2-8042.h`）。port の I/O は `kern_io_in8`/`kern_io_out8`（x86 の inline asm と
  `pause` を外した）。IRQ のどちらかが負なら poll: driver が `0xae`（keyboard を有効）と config byte（translate を立て、keyboard・aux の IRQ と
  keyboard の off の bit を落とす）を書き、kthread が 10 ms ごとに status を見て AUX の byte は mouse、他は keyboard の既存の処理へ渡す（EOI と
  IRQ の mask は IRQ の時だけ）。pcat は `include/drivers/platform/pcat/ps2-8042.h` の inline の `drv_pcat_ps2_8042_init()` が 0x60/0x64・IRQ 1/12・
  今の名前と path で呼ぶ。IRQ の時は config byte を書かない（pcat の挙動を変えない、設計 m1 の「自分で書く」は poll の時だけに限った）。
  host 試験の hook（`WS018_INPUT_HID_HOST_TEST`）は保った。
- **firmware fb**（`src/drivers/platform/sun4u/sun4u-firmware-fb.c`・`.h`、新）: 名前 5 つ（phys・width・height・pitch・format）を読み、形と
  pitch を検証して `kern_device_map(UNCACHED)`。console 用の fill・glyph と、backend（fill・line・pattern_fill・blit_row（pattern は
  `pattern_x + i`, `pattern_y`）・flush は no-op・glyph は ASCII の外は `?`）。backend の表は `#if CONFIG_DRIVER_GRAPHICS_DEVICE`。
- **console**（`sun4u-console.c`）: 設計 §4.3 の形。lock は**設計から変えた**: `spin_trylock` は同じ CPU の再入で trap する
  （`src/kern/lock.c` の `spin_trylock`）ので、`hal_irq_disable()` と atomic の busy の印にした（UP。印が立っていれば同じ CPU の再入なので
  lock 無しで書く、設計の意図「取れなければ lock 無しで書く」と同じ）。suspend の flag はこの中で立て・落とす。描いた cell を
  `screen_drawn` に覚え、scroll は変わった cell だけ描き直す。publish の時に全体を消して grid の先頭から（m-9）。色は 0xaaaaaa の上に黒、cursor は白の 2 行。
- **sun4u.c**: console の後に `drv_sun4u_fb_register_backend()`、`kern_platform_input_init` で `sun4u.i8042.data`・`.command` があれば poll で
  8042 を起動（失敗は log だけ）、`kern_platform_debug_write` は `kernel_putc` が publish された後は何もしない。
- **vmunix.mk（sparcv9）**: p005 の予定から**前倒し**で、kernel の source に `sun4u-firmware-fb.c`・pcat の `vgafont.c`（D3、Guardrail の例外は
  Q1 に依頼済みの物）・`isa/ps2-8042.c` を足し、`CONFIG_DRIVER_GRAPHICS_DEVICE=y` の時だけ `graphics-device.c`。console が fb を使うので
  p004 の file を compile しないと sun4u の kernel が link できないため。top の Makefile の `ZEDBSD_GRAPHICS_BACKEND`、config の stamp の依存、root の大きさは p005 のまま。
- pcat・amd64 の vmunix.mk の ps2 の path、amd64 の HAL の `cons.c` の comment の path（2 行）。

## 確認（2026-10-19 P1）

- build（各 `BUILD=build/ws275-p004/<名前>`）: `make ZEDBSD_CONFIG=config/ci/config-{pcat,amd64,riscv64-virt}.mk vmunix -j8` rc 0・
  `warning:`/`error:` 0 件。`make ZEDBSD_CONFIG=plan/tools/ports/config-sun4u.mk vmunix bootloader -j8` rc 0・0 件（vmunix 1071400 byte）。
- sun4u の graphics 有りの道（Makefile がまだ sun4u で `CONFIG_DRIVER_GRAPHICS_DEVICE` を n にするので、build の gcc の行に
  `-UCONFIG_DRIVER_GRAPHICS_DEVICE -DCONFIG_DRIVER_GRAPHICS_DEVICE=1 -fsyntax-only`、`-Wall -Wextra -Werror`）: `sun4u-firmware-fb.c`・
  `graphics-device.c`・`sun4u.c`・`vfs.c` どれも rc 0。link は p005。
- host 試験 `plan/ws275/tests/host-firmware-fb.sh`: `host-firmware-fb: PASS`（28 の check）。BGRX8888・XRGB8888 の fill・RGB24・patterned INDEX8
  （chunk の x の位相）・line の byte の並び（host は LE なので IE の窓の 32 bit の store と同じ byte）、RGB332 の 0x17212b→0x04・0xff7f00→0xec
  （design §5 の期待値）、短い pitch と RGBX8888 の拒否、glyph。
- 8042 の host 試験（ws081 の `host-ps2-wheel.c` の写しを `build/ws275-p004/host/` で include の path だけ直して）: `host-ps2-wheel: PASS`。
  poll の道（config byte、thread）は host 試験に無い（T1 の sun4u の `keys:` で見る）。
- 未実施: QEMU・実機の全部（下の T1 の依頼）。sparcv9 の kernel の link で graphics を含む形（p005）。

## 他の WS への依頼（Q1）

- `plan/ws081/tests/host-ps2-wheel.c` l.31 の `#include "src/drivers/platform/pcat/ps2-8042.c"` を次に（`host-ps2-wheel.sh` l.3 の
  comment の path も `src/drivers/isa/ps2-8042.c` に）。この形で PASS を確かめた:

  ```c
  #include "src/drivers/isa/ps2-8042.c"
  #include "drivers/platform/pcat/ps2-8042.h"
  #define I8042_DATA PCAT_I8042_DATA_PORT
  #define I8042_STATUS PCAT_I8042_STATUS_PORT
  #define I8042_COMMAND PCAT_I8042_STATUS_PORT
  #define PS2_MOUSE_IRQ PCAT_I8042_MOUSE_IRQ
  #define PS2_KEYBOARD_IRQ PCAT_I8042_KEYBOARD_IRQ
  ```

- `plan/ws035/tests/refactor-refs.py` l.63 の ps2 の path: master.md の一覧に無く ws035 の過去の試験なので、2026-10-06 の基準で削除の候補（Q1 の判断）。

## T1 への依頼（p004、QEMU のみ）

sun4u の text の console の引き継ぎと PS/2 の poll、pcat の PS/2 の回帰。image は `plan/tools/ports/config-sun4u.mk`（変えない）と p004 の tree。
graphics（X）と色の点は p005 の後の p006（`/dev/graphics` は sun4u ではまだ build されない）。

1. sun4u、`QEMU_EXTRA` 無し・`'-g 1024x768x8'`・`'-g 1024x768x32'` の 3 本、各本:
   ```
   BOOT_PATTERN: ^(?:\S+ )?login: ?
   BOOT_THEN:
     type:root
     keys:echo ws275-''ok
     expect:^ws275-ok(\s|$)
   ```
   判定: 画面（PNG）に login と `ws275-ok` の行。`keys:` は QEMU の sendkey＝i8042 なので、`expect` が合えば PS/2 の poll の道が通った。
   合わない時は PNG と、gdbstub で `screen_on`・`i8042_polled`・`fb_format`・`fb_width` の値（`vmunix` の symbol）を返してほしい。
   x8・x32 の画面の字が灰色（0xaaaaaa）で読めること。色の R と B の順はこの画面では見えない（灰色と白だけ）ので p006。
2. pcat の回帰: bios-ide の login と `keys:` の 1 本、`plan/ws081/tests/ps2-wheel-qemu.sh`（T1-015 で使用）。
