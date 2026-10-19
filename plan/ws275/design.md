<!-- awesome-plan project=zedbsd record=ws275-design -->

# WS275 設計: sun4u の /dev/graphics と Xzed

- 親: [ws.md](ws.md)、Phase: [p001](phase001/phase.md)（2026-10-19 P1、q1303）
- 起点（2026-10-19 ユーザー）:「では、sun4uでも/dev/graphicsを実装して、Xzedが動くようにします。」
- 範囲: OBP（OpenBoot・OpenBIOS）の firmware framebuffer を受け皿にした /dev/graphics と Xzed（QEMU の `-machine sun4u` で判定）、
  既知の card の driver が引き取る仕組み、個々の card の driver（別の Phase、最初の card は D8、実機は Sun Blade 150）。
- 未確定の点は §10 の判断（D1〜D6）。既定で書き、判断で変わる所に印（→D*）を付けた。

## 1. /dev/graphics の契約（変えない）

[include/uapi/graphics.h](../../include/uapi/graphics.h)・[docs/reference/control-devices.md](../../docs/reference/control-devices.md) の
group `g` の 11 の ioctl。要点:

- 独占の owner（2 つ目の open は `EBUSY`、backend が無ければ `ENODEV`、owner 以外は `EBADF`）。最後の close で graphics を出て console を戻す。
- `GET_CAPS`・`GET_MODES`・`ENTER` は ENTER 前から使える。他は ENTER 後（前は `ENXIO`）。
- **framebuffer の mmap は契約に無い。** user の描画は全て `BLIT`（RGB24・INDEX8・MONO1、stride 付き）・`FILL_RECT` などの ioctl と
  `FLUSH`。よって「framebuffer の user への写像」は作らない（user の page に device を写す MMU の属性の問題は生じない）。
- Xzed が要る cap: `FLUSH`・`GLYPH`・`BLIT_RGB24`（[main.c](../../userland/x11/xzed/main.c) の initialize）。Xzed は自分の
  `uint32_t` の画面を持ち、damage を RGB24 の byte 列（R,G,B の順の byte）に詰めて BLIT する。**byte 列なので endian に依らない。**
- 枠: mode の capacity 16、flush の矩形 32、行の buffer 4096 byte（pcat の実装の値。sun4u も同じ値にする）。

## 2. pcat の実装の要点

- 前段 [pcat-graphics.c](../../src/drivers/platform/pcat/graphics/pcat-graphics.c)（816 行）: cdev `graphics` の登録
  （`drv_graphics_device_register`、[vfs.c](../../src/kern/vfs.c) が `CONFIG_DRIVER_GRAPHICS_DEVICE` の時に呼ぶ）、owner・lock・
  ioctl の引数の検証と user からの copy、`kern_text_suspend()`／`kern_text_resume()` で text の console を止める・戻す。
- 後段 [backend.h](../../src/drivers/platform/pcat/graphics/backend.h) の `drv_pcat_graphics_backend_*`（ready・get_modes・enter・
  leave・fill・line・pattern_fill・blit・flush・get_glyph）。boot の framebuffer を `hal_space_map_device(…, HAL_SPACE_NOCACHE)` で
  kernel に写し、`volatile uint32_t *` に XRGB8888 を書く。
- glyph は [font.c](../../src/drivers/platform/pcat/graphics/font.c)・vgafont.c（8x16）。
- PC-98 の [pc98-graphics.c](../../src/drivers/platform/pc98/graphics/pc98-graphics.c) は pcat の前段の写し（名前を除く差は 18 行）で、
  後段だけが違う。前段が 2 つ写しで在るのが今の形。
- どの platform が前段を持つかは top の [Makefile](../../Makefile) の `ZEDBSD_GRAPHICS_BACKEND`（pcat・amd64 → pcat、pc98 → pc98）。
  それ以外では `CONFIG_DRIVER_GRAPHICS_DEVICE` が n に落ちる。

## 3. sun4u の今

- HAL の [framebuffer.c](../../src/hal/sparcv9/bsp-sun4u/framebuffer.c)（WS267）: loader の handoff（`display_framebuffer`・
  `_size`・`display_registers`、PCI memory の address、`pci_memory_base` を足して物理）から std VGA を Bochs dispi で 640x400x32 の
  linear にし、QEMU の拡張の byte order の register（MMIO の 0x604）を little endian に、80x25 の text を ASI の物理 write
  （`sparcv9_phys_write32_le`）で描く。
- HAL の [console.c](../../src/hal/sparcv9/bsp-sun4u/console.c): `hal_putc` が serial と画面の両方に書く。kernel の
  [sun4u-console.c](../../src/drivers/platform/sun4u/sun4u-console.c) は teletype で、putc は `hal_putc` に渡し、入力は 16550 を
  10 ms ごとに poll（kthread）。`kernel_putc` は publish していない（→ 画面は kernel が `hal_putc` を呼ぶ限り HAL が描き続ける）。
- HAL の IRQ は無い（[irq.c](../../src/hal/sparcv9/irq.c) の `hal_irq_register` は `HAL_ERR_UNSUPPORTED`、「devices are polled」）。
- HAL の `hal_space_map_device`（[space.c](../../src/hal/sparcv9/space.c)）は 16 slot の device の窓（1 TiB）を作り、TLB の miss で
  **uncached・side effect・invert endian（IE）** の 8 KiB の TTE を入れる。名前付きの値は `sun4u.cmd646.primary.*` と
  `sun4u.serial.port` だけ（[boot.c](../../src/hal/sparcv9/bsp-sun4u/boot.c)）。
- kernel の port I/O は `kern_io_in8`／`kern_io_out8`（[device-io.h](../../include/kern/device-io.h)、PCI の I/O 空間）。
- QEMU sun4u（[§9 の出典](#9-出典)）: ebus の ISA bus の I/O は PCI の I/O 空間そのもの。ebus に `TYPE_I8042`（keyboard と
  PS/2 mouse）、ISA の IRQ 1・12 は sabre の pbm の `OBIO_KBD_IRQ`・`OBIO_MSE_IRQ` へ。std VGA は PCI の 2.0。USB の controller は
  既定では無い。→ **keyboard と mouse は i8042 の 1 つ**で賄える。

## 4. 実装の形

### 4.1 前段（→D1）

既定: 前段を **generic** にする。`src/drivers/generic/graphics-device.c`（pcat の前段を、後段の関数の表 `struct drv_graphics_backend` を
呼ぶ形に書き直した物）と `include/kern/graphics-backend.h`。sun4u は最初の利用者。pcat・pc98 の移行は本 WS では行わず Future Work
（pcat の回帰を巻き込まないため）。代案は §10 D1。

### 4.2 後段 (1): firmware framebuffer（既定の受け皿）

ユーザーの決定（2026-10-19）:「…OpenBootが用意したframebufferを使うのがコンソールとしてはいいですね。/dev/graphicsはfirmware framebufferにします。」
訂正（同日）:「あ、待って。firmware framebufferを使うけど、XVR-500などの既知のデバイスがあるなら、そっちを初期化することにします。」

- **出所**: boot loader（stage2、OpenFirmware の client interface が使える最後の所）が `/chosen` の `stdout` の ihandle →
  `instance-to-package` → screen の node の property `width`・`height`・`depth`・`linebytes` と、framebuffer の物理 address
  （`address` は firmware の仮想 address なので使わず、node の `assigned-addresses`／`reg` の framebuffer の BAR を
  `pci_memory_base` で物理にする。card ごとに BAR が違うので「どの BAR か」は `address` を firmware の translation で物理に引く
  `translate` の method を第 1、無ければ最大の memory の BAR）を読む。node の PCI の vendor・device も記録する。
- **pixel の形**: depth 8・24（32 bit の箱）・32 を受ける。big endian の card（Sun の firmware の 24 bit は通常 xRGB の BE の箱、未確認）と
  QEMU の VGA（LE）を区別するため、loader は「byte の並び」を format として渡す（`KERN_BOOT_FRAMEBUFFER_*` に倣い xRGB の BE／LE、
  INDEX8）。QEMU の VGA は今の HAL の通り LE の register を立てる（OpenBIOS が既に立てていれば不要。p002 で確かめる）。
- **8 bpp**: loader が firmware を出る前に screen の package の `color!` で RGB332 の palette を設定する（IEEE 1275 の display の
  method。pcat の 8 bpp と同じ RGB332）。kernel は RGB24 の BLIT を RGB332 に落とす。**Xzed の 8 bpp の PseudoColor は要らない**
  （Xzed は TrueColor のまま、量子化は kernel。色は 256 色に落ちる。PseudoColor の visual は Future Work）。
- **mode**: firmware が決めた 1 つだけ（`GET_MODES` は 1 件、`ENTER` は要求の大きさに依らずその mode を返す。契約上「返る mode は
  要求と違ってよい」）。Xzed の `choose_mode` は列挙から選ぶので 1 件で動く（startx の `--size 800x600` は希望）。
- **写像**: `hal_space_map_device(phys, linebytes×height, READ|WRITE|NOCACHE)`。sparcv9 の device の窓は IE（invert endian）。
  LE の framebuffer には native の `uint32_t` の store、BE の framebuffer は IE を打ち消すため byte を逆にして store する（format で
  分ける、1 箇所の関数）。
- **HAL の早期の console**: 同じ firmware framebuffer に描く（今の Bochs の 640x400 の直の設定をやめる、→D7）。HAL も名前付きの値と
  同じ handoff の値を使う。QEMU で OpenBIOS が screen の property を出さない時だけ、今の Bochs の設定を fallback に残すかは D7。
- cap: FILL・LINE・PATTERN・BLIT_INDEX8・BLIT_RGB24・BLIT_MONO1・FLUSH（no-op）・GLYPH。
- 置き場所: `src/drivers/platform/sun4u/sun4u-firmware-fb.c`（PCI の card に依らない、OBP の machine 全般の物）。

### 4.2b 後段 (2): 既知の card の driver による引き取り

- 前段（generic、§4.1）は backend の登録を受け、**優先度**で選ぶ: firmware framebuffer は最低。PCI の走査で既知の vendor・device
  （表は各 card の driver が持つ）が見つかり、その driver の初期化が成功したら、その backend が `/dev/graphics` と画面の text を
  引き取る。
- 引き取りの順: (1) 前段の lock を取り、owner が居れば（graphics 中）引き取りは close まで待つ（保留の flag）。(2) text の console を
  suspend。(3) card の driver が自分の mode を設定（firmware の mode を引き継いでもよい）。(4) firmware の backend の写像を外す。
  (5) text の console の描き先を新しい backend にして resume（grid を全部描き直す）。失敗したら firmware の backend に戻す。
- 引き取った card が firmware framebuffer と同じ device なら、同じ BAR を driver が自分で写し直す（firmware の写像は外す）。
- 個々の card の driver は別の Phase（§7 の p008 以降）。候補: XVR-500（3Dlabs Wildcat VP 系）・XVR-600（同系）・XVR-100
  （ATI Radeon 7000）・QEMU の std VGA（Bochs dispi、mode の切り替えの試し台にもなる）。資料: Radeon は公開の register の資料と
  Linux・Mesa（値の引用の規則）がある、3Dlabs の Wildcat は公開の資料が乏しい（未調査）。最初の card は D8。
- 実機: Sun Blade 150 と XVR-500 か 600（OBP の framebuffer の depth は未確認）。実機の確かめは card の Phase で、ユーザーの実機の
  一覧に足す。

### 4.3 text の console（→D2）

graphics の間、画面に kernel の文字が出てはならず、出た後は text が戻る必要がある。今の sun4u の kernel は `hal_putc` 経由で
HAL に描かせているので、kernel からは止められない・描き直せない。

既定: **kernel が画面の text の console を持つ**（hal.h の `hal_putc` の契約「kernel_putc を publish したら HAL は display に触れない」
どおり）。sun4u-console.c が `kernel_putc` を publish し、serial（16550 の THR、`sun4u.serial.port`）と、kernel 側の 80x25 の grid
（同じ 8x16 の font、選ばれた backend の写像に描く。card の driver が引き取った時は描き先を移す）に書く。`suspend`／`resume`（`kern_text_suspend` が呼ぶ）で grid の描画を止め、
resume で全 cell を描き直す。HAL は hal.h を変えず、HAL の実装も変えない（publish 後は既に描かない）。

### 4.4 HAL の名前付きの値（承認が要る、→ [proposed/hal-sun4u-names.md](proposed/hal-sun4u-names.md)）

hal.h は変えない。HAL の責務の追加（`hal_get_arch_handoff` の新しい名前）なので、WS049・WS256 の例に倣い承認を求める:

| 名前 | 値（64 bit の word） |
| --- | --- |
| `sun4u.framebuffer.phys`・`.size` | firmware framebuffer の物理 address と byte 数（無ければ NULL） |
| `sun4u.framebuffer.width`・`.height`・`.pitch` | pixel・byte（pitch = OBP の `linebytes`） |
| `sun4u.framebuffer.format` | `KERN_BOOT_FRAMEBUFFER_*`（INDEX8・xRGB の BE・LE。足す値は include/kern/boot.h への追加、→ proposed） |
| `sun4u.framebuffer.pci.vendor`・`.device` | screen の node の PCI の id（card の driver の引き取りの照合用） |
| `sun4u.i8042.data`・`sun4u.i8042.command` | i8042 の I/O port（0x60・0x64、→D4） |

i8042 の port は loader が OpenBIOS の device tree から取る形（`bootloader/sparcv9/handoff.c` に discover を足し、handoff の版を
3 に）と、ISA の固定の値を HAL が返す形がある（→D4、既定は loader が取る。実機の機種差に備え、serial と同じ形）。

### 4.5 入力（→D5）

- QEMU sun4u の i8042 は ebus の ISA、IRQ は HAL に無いので **poll**（sun4u-console の serial と同じく kthread で 10 ms ごと）。
- 既定: pcat の [ps2-8042.c](../../src/drivers/platform/pcat/ps2-8042.c)（1269 行、x86 の `inb`/`outb` と IRQ）を
  `src/drivers/isa/ps2-8042.c` へ移し、port の I/O を `kern_io_in8`/`kern_io_out8`、port の番号を引数、IRQ か poll かを選べる形にする。
  pcat は IRQ のまま、sun4u は poll。keyboard と mouse は今の pcat と同じく evdev（[input.c](../../src/drivers/generic/input.c)、
  `drv_input_device_register`）に出す。`/dev/input/event*` の `struct input_event` は kernel と user が同じ endian なので変換は要らない。
- QEMU の i8042 の既定は scan code set 2 を set 1 に translate（config byte の bit 6）。pcat の driver の set 1 の表をそのまま使える
  （p004 で monitor の `info` と gdb で確かめる）。
- console の keyboard 入力（画面の login に keyboard で打つ）は範囲外（serial のまま）。Xzed は evdev を直に読む。

### 4.6 platform の userland（sparcv9 の rule は basic の形）

- [platform/sparcv9/vmunix.mk](../../platform/sparcv9/vmunix.mk) は `USERLAND_SELECTED_BASIC_PROGRAMS` を static に link する。
  Xzed・zterm・zwm・zshell は class basic・platform `*`・既定 n（libX11 の xlib.c を source に含む）なので、config で選べば
  そのまま build の対象になる（rule の追加は要らない見込み。p005 で build して確かめる）。
- 2026-10-19 の Guardrail（userland/base の program は arch ごとに有無を分けない）: X11 の group は base ではなく既定 n の group。
  試験の config（[plan/tools/ports/config-sun4u.mk](../tools/ports/config-sun4u.mk) の写しを `plan/ws275/tests/config-sun4u-x11.mk`）で
  Xzed・zterm・zwm・zshell を選ぶ。base の全部を sparcv9 で build する形（x68k の P2 q1301 と同じ）はユーザーの担当（→D6）。
- top の Makefile の `ZEDBSD_GRAPHICS_BACKEND` に sparcv9 → sun4u を足し、sparcv9 の kernel の source に
  `CONFIG_DRIVER_GRAPHICS_DEVICE=y` の時だけ graphics の 2 file と ps2 を足す。
- UFS の root は 60 MiB（vmunix.mk）。static の X の 4 program の大きさを p005 で見て、足りなければ `--size-mib` を上げる。

### 4.7 big endian の X11

- Xzed の setup の reply は client の byte order（`'l'`/`'B'`）で書く（`wr16`/`wr32` が order を取る）。image byte order も client の
  order。visual は TrueColor 24 bit、mask 0xff0000/0xff00/0xff（数値なので endian に依らない）。
- libX11（[xlib.c](../../userland/x11/libX11/xlib.c)）は常に `'l'` で、`w16`/`w32`/`r16`/`r32` は byte で LE を組む。**欠陥の候補**:
  `XChangeProperty` の format 16・32 の data を `memcpy`（native の順）で送る（2026-10-19 P1 の読み）。big endian では LE と宣言した
  のに BE の値が行く。property を Xzed や他の client が数値として読む所（WM_PROTOCOLS・_NET_WM_* など）で値が壊れる。p005 で
  format 16・32 を要素ごとに `w16`/`w32` で詰め、`XGetWindowProperty` の逆も直す。他に native の順の `memcpy` が無いかを p005 で
  全部読む（xzed/main.c・input.c・zterm・zwm・zshell は grep で native の cast は見当たらない。input.c の `memcpy(&event…)` は
  kernel と同じ endian の evdev なので正しい）。

## 5. boot-test の判定

`BOOT_MODE=sun4u plan/tools/boot-test.sh`（std VGA の画面を QMP で撮る）。

```
BOOT_PATTERN: ^(?:\S+ )?login: ?
BOOT_THEN:
  type:root
  type:startx &
  expect:<zterm の prompt の正規表現（p005 で zterm の画面で決める）>
  keys:echo ws275-ok
  expect:ws275-ok
  mouse:…（→ 下）
```

- startx は `/bin/Xzed --size 800x600 -- /bin/sh /etc/Xzed/Xzedrc`（zwm・zterm・zshell）。
- `keys:` は QMP の send-key で、sun4u では i8042 の keyboard に入る（USB の keyboard は無い）。zterm に文字が出れば keyboard の道が通る。
- mouse: boot-test.sh に mouse の step は無い。既定: `mouse:DX,DY[,BUTTON]`（QMP の `input-send-event` の rel と btn）を足す依頼を
  Q1 に出し（plan/tools は WS の外）、zwm の menu か zterm の focus の変化を `expect:` で見る。代わりに evdev を読む小さな host の
  確かめは QEMU の画面の判定ではないので使わない。
- 回帰: pcat の bios-ide で Xzed（keys: で zterm に打つ）と PS/2（D1・D3・D5 で pcat の file を移すため）、amd64 の uefi-nvme の
  login、sun4u の login（text の console の引き継ぎ）。T1 が 1 つの QEMU ずつ流す。

## 6. 変える file（sparcv9 はユーザーの担当なので、触る前に Q1 がユーザーに確かめる）

| file | 変更 | sparcv9 |
| --- | --- | --- |
| `bootloader/sparcv9/handoff.h`・`handoff.c`・`stage2` | screen の node の property と BAR、8 bpp の `color!`、i8042 の discover、handoff 版 3 | はい |
| `src/hal/sparcv9/bsp-sun4u/framebuffer.c` | firmware framebuffer に描く、Bochs の直の設定の扱い（D7） | はい |
| `src/drivers/platform/sun4u/sun4u-firmware-fb.c`（新） | firmware framebuffer の backend | はい |
| `src/drivers/gpu/<card>/`（新、p008〜） | card の driver | いいえ |
| `src/hal/sparcv9/bsp-sun4u/boot.c` | 名前付きの値 5 つ（承認後） | はい |
| `src/drivers/platform/sun4u/sun4u-console.c`・`.h` | kernel_putc の publish、grid、suspend/resume | はい |
| `src/kern/platform/sun4u.c` | ps2 の起動（`kern_platform_input_init`） | はい |
| `platform/sparcv9/vmunix.mk` | kernel の source の追加、root の大きさ | はい |
| `src/drivers/generic/graphics-device.c`（新）・`include/kern/graphics-backend.h`（新） | generic の前段（D1） | いいえ |
| `src/drivers/isa/ps2-8042.c`（移動）・`src/drivers/platform/pcat/ps2-8042.c`・`.h` | 共有の 8042（D5） | いいえ（pcat） |
| `src/drivers/generic/font8x16/`（移動）・pcat の font | 共有の font（D3） | いいえ（pcat） |
| `platform/pcat/vmunix.mk`・`platform/amd64/vmunix.mk`・`platform/pc98/vmunix.mk` | 移動の path | いいえ |
| top の `Makefile` | `ZEDBSD_GRAPHICS_BACKEND` に sun4u | いいえ |
| `userland/x11/libX11/xlib.c` | property の endian | いいえ |
| `plan/tools/boot-test.sh` | `mouse:` の step（Q1 に依頼） | いいえ |
| `docs/reference/control-devices.md` | sun4u の driver を足す | いいえ |
| `plan/ws275/tests/config-sun4u-x11.mk`（新） | 試験の config | いいえ |

## 7. Phase と LW

| Phase | 中身 | 依存 | LW |
| --- | --- | --- | --- |
| p001 | この設計、design-reviewer | — | 1 |
| p002 | loader の screen の property・BAR・`color!`・i8042、HAL の名前付きの値、HAL の早期の console を firmware framebuffer に（承認後、D4・D6・D7） | p001、承認 | 2 |
| p003 | generic の前段（優先度と引き取りの口を含む）・font の共有（D1・D3）、firmware framebuffer の backend、text の console の引き継ぎ（D2）。host の試験 | p002 | 4 |
| p004 | 8042 の共有（D5）と sun4u の poll、evdev | p002 | 3 |
| p005 | userland: 試験の config、Makefile の backend、libX11 の BE の修正、root の大きさ、build | p003・p004 | 2 |
| p006 | boot-test の mouse の step（Q1）と T1 の試験（QEMU sun4u の X、pcat の回帰） | p005 | 1 |
| p007 | 規約の全文の見直し（p002〜p006 の分） | p006 | 1 |
| p008 | 最初の card の driver（D8）: 資料の調べ、PCI の id、mode・引き取り。QEMU に model が無い card は実機（Sun Blade 150）だけで確かめる | p003 | 4〜8（card と資料による） |
| p009 | 2 つ目以降の card（必要なら） | p008 | 未見積 |
| p010 | card の分の規約の全文の見直し | p008 | 1 |

p001〜p007 で計 14 LW（約 4〜5 時間、8 LW ≈ 2〜3 時間）。card は別に 4〜8 LW ずつ。

## 8. 危険と未確認

- IE の写像で VRAM に 32 bit の store を連ねた時の QEMU の VGA の振る舞い（HAL は ASI の LE の store）。p003 の最初に gdb で
  1 pixel を書き、monitor の `xp` で VRAM の byte を見る。
- ebus の ISA の I/O が PCI の I/O の 0x60/0x64 に見えるか（QEMU の source では `pci_address_space_io` そのもの、OpenBIOS の
  device tree の `reg` は未確認）。p002 で monitor の `info mtree` で見る。
- TCG の描画の速さ（全面の BLIT が遅いと boot-test の timeout に響く）。
- OpenBIOS（QEMU）の screen の node が `width`・`depth`・`linebytes` を出すか、`translate` が使えるか（p002 で loader から読み、
  gdb で handoff を見る）。出さなければ D7 の fallback。
- 実機の OBP の XVR-500/600 の firmware framebuffer の depth・byte の並び・BAR（未確認、ユーザーの実機の一覧に OBP の
  `show-displays`・`.properties` の写しを頼む）。
- 8 bpp の `color!` が OBP・OpenBIOS の両方で効くか。

## 9. 出典

- QEMU v10.0.0 の `hw/sparc64/sun4u.c`（https://gitlab.com/qemu-project/qemu/-/raw/v10.0.0/hw/sparc64/sun4u.c、SHA-256
  e75ff4abf985a0f198ef9dfc4c1b7386930e29639895798954ba6cf5675916ae）: ebus の ISA（l.304〜330 の `isa_bus_new(…, pci_address_space_io)`・
  `TYPE_I8042`）、IRQ（l.610〜619）、VGA（l.621〜624）。
- QEMU v10.0.0 の `hw/pci-host/sabre.c`（同 URL の形、SHA-256 73856ada0597743ff8c3bb73d29814d689ff8e233e04a89fca4fb1e1ad312850）。
- 置き場所: `/home/awe/zedBSD-worktrees/p1/build/tmp/ws275-docs/`（commit しない）。host の QEMU は Debian の 10.0.11。
- code に値を写す時は Guardrail の「Value(s) referenced:」の block の comment で示す。

## 10. ユーザーに聞く判断

| ID | 問い | 選択肢 | 既定と理由 |
| --- | --- | --- | --- |
| D1 | /dev/graphics の前段 | (a) generic の前段を作り sun4u だけ使う（pcat・pc98 は後で移す） (b) pcat の前段を写す（3 つ目の写し） (c) generic にして pcat・pc98 も今移す | (a)。写しを増やさず、pcat・pc98 の回帰を本 WS に入れない |
| D2 | graphics の間の text の console | (a) kernel が kernel_putc を publish して画面の text を持つ（HAL は不変） (b) HAL に描き直しの仕組みを足す（HAL の責務の追加、承認） (c) graphics の間は serial だけ、戻った時は画面を消すだけ | (a)。hal.h の契約どおりで、戻った時に text が描き直せる |
| D3 | glyph の font | (a) pcat の font を generic へ移し共有 (b) sun4u が pcat の path の file を直に compile (c) sun4u に写し | (a) |
| D4 | i8042 の port の出所 | (a) loader が OpenBIOS の tree から取り handoff 版 3 (b) HAL が ISA の固定値を名前で返す | (a)。serial と同じ形で、sparcv9 の loader を触るのでユーザーの確認が要る |
| D5 | 8042 の driver | (a) pcat の driver を `src/drivers/isa/` へ移し port I/O と poll を抽象化（pcat の回帰が要る） (b) sun4u 用に新しく書く（約 600 行の重複） | (a) |
| D6 | sparcv9 の file の変更 | (a) P の担当が §6 の sparcv9 の file を変える (b) ユーザーが変える／ユーザーの作業の後に | ユーザーの担当なので必ず聞く。既定なし |
| D7 | QEMU での Bochs の直の設定 | (a) やめる（OpenBIOS の screen の property だけ） (b) property が無い時の QEMU だけの fallback に残す | (b) を p002 で property を確かめるまでの既定。property が揃えば (a) に |
| D8 | 最初の card の driver | (a) XVR-500 (b) XVR-600 (c) XVR-100（Radeon、資料が多い） (d) QEMU の std VGA（試し台） | 既定なし。ユーザーの実機の card（Sun Blade 150 の XVR-500 か 600）に合わせ、まず実機の OBP で model を確かめる |
| HAL | §4.4 の名前付きの値と boot.h の format の追加 | 承認／不承認 | [proposed/hal-sun4u-names.md](proposed/hal-sun4u-names.md) |
