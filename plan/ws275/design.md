<!-- awesome-plan project=zedbsd record=ws275-design -->

# WS275 設計: sun4u の /dev/graphics と Xzed（改訂 2）

- 親: [ws.md](ws.md)、Phase: [p001](phase001/phase.md)（版 1: 2026-10-19 P1 q1303、改訂 2: 2026-10-19 P1 q1308）
- 起点（2026-10-19 ユーザー）:「では、sun4uでも/dev/graphicsを実装して、Xzedが動くようにします。」
- 範囲（2026-10-19 18:29 ユーザー「まずAのフレームバッファだけ実装して、Sun PGX64とかはあとで対応できるように口だけ残しておきます。」）:
  OBP（OpenBoot・OpenBIOS）の firmware framebuffer を受け皿にした /dev/graphics と Xzed（QEMU の `-machine sun4u` で判定）、
  既知の card の driver が後で引き取れる**口**（backend の登録と優先度）。個々の card の driver（PGX64・XVR-500/600 など）と実機
  （Sun Blade 150）への道は本 WS の外（§9）。
- 版 1 のレビュー [design-review-1.md](design-review-1.md)（Blocker 1・Major 8・Minor 12）への対応は末尾の §12。
- 未確定の点は §11 の判断。既定で書き、判断で変わる所に印（→D*）を付けた。

## 1. /dev/graphics の契約（変えない）

[include/uapi/graphics.h](../../include/uapi/graphics.h)・[docs/reference/control-devices.md](../../docs/reference/control-devices.md) の
group `g` の 11 の ioctl。要点:

- 独占の owner（2 つ目の open は `EBUSY`、backend が無ければ `ENODEV`、owner 以外は `EBADF`）。最後の close で graphics を出て console を戻す。
- `GET_CAPS`・`GET_MODES`・`ENTER` は ENTER 前から使える。他は ENTER 後（前は `ENXIO`）。
- **framebuffer の mmap は契約に無い。** user の描画は全て `BLIT`（RGB24・INDEX8・MONO1、stride 付き）・`FILL_RECT` などの ioctl と
  `FLUSH`。user の page に device を写すことはしない。
- Xzed が要る cap: `FLUSH`・`GLYPH`・`BLIT_RGB24`（[main.c](../../userland/x11/xzed/main.c) の initialize）。BLIT は R,G,B の byte 列
  なので endian に依らない。
- 枠: mode の capacity 16、flush の矩形 32。**行の buffer**（pcat は 4096 byte）は §4.1 の分割で扱う（M5）。

## 2. pcat の実装の要点

- 前段 [pcat-graphics.c](../../src/drivers/platform/pcat/graphics/pcat-graphics.c)（816 行）: cdev `graphics` の登録
  （`drv_graphics_device_register`、[vfs.c](../../src/kern/vfs.c) が `CONFIG_DRIVER_GRAPHICS_DEVICE` の時に呼ぶ）、owner・lock・
  ioctl の引数の検証と user からの copy、`kern_text_suspend()`／`kern_text_resume()`。
- 後段 [backend.h](../../src/drivers/platform/pcat/graphics/backend.h) の `drv_pcat_graphics_backend_*`。
- glyph は [font.c](../../src/drivers/platform/pcat/graphics/font.c)・vgafont.c（8x16、symbol `drv_pcat_vgafont16`）。
- PC-98 の前段は pcat の前段の写し（名前を置き換えると差は約 10 行、レビューの確かめ）。
- どの platform が前段を持つかは top の [Makefile](../../Makefile) の `ZEDBSD_GRAPHICS_BACKEND`（pcat・amd64 → pcat、pc98 → pc98）。

## 3. sun4u の今

- HAL の [framebuffer.c](../../src/hal/sparcv9/bsp-sun4u/framebuffer.c)（WS267）: std VGA を Bochs dispi で 640x400x32 にし、
  QEMU の拡張の byte order の register（qext、MMIO の 0x604）を LE に書き、80x25 の text を ASI の LE の物理 write で描く。
  注記（l.15〜17）のとおり、QEMU の std VGA は big endian の target では**既定で framebuffer を BE で読む**。
- HAL の [console.c](../../src/hal/sparcv9/bsp-sun4u/console.c): `hal_putc` は serial と画面の両方に書き、`kernel_putc` が publish
  されていれば委ねる（l.62〜66）。kernel の [sun4u-console.c](../../src/drivers/platform/sun4u/sun4u-console.c) は teletype で、
  `console_putc` は `hal_putc` を呼ぶ（l.157〜164）。入力は 16550 を 10 ms ごとに poll。
- [klog.c](../../src/kern/klog.c) は `kernel_putc` に書いた後 `kern_platform_debug_write` も呼ぶ（l.128〜150）。sun4u の
  `kern_platform_debug_write`（[sun4u.c](../../src/kern/platform/sun4u.c) l.140〜153）は `hal_putc` に渡す。
- HAL の IRQ は無い（[irq.c](../../src/hal/sparcv9/irq.c)、devices are polled）。
- `hal_space_map_device`（[space.c](../../src/hal/sparcv9/space.c)）の窓の TTE は uncached・side effect・**invert endian（IE）**。
  窓の VA は bump で戻らない（l.452〜456）。
- kernel に PCI の config の access も PCI core も無い（`platform/sparcv9/vmunix.mk` に PCI は無い）。
- QEMU sun4u（§10 の出典）: ebus の ISA の I/O は PCI の I/O 空間そのもの、`TYPE_I8042`（keyboard と PS/2 mouse）、std VGA は PCI の
  2.0、USB は既定で無い。`graphic_depth` は fw_cfg で OpenBIOS に渡る（sun4u.c l.709〜711）。既定の depth は未確認（8 と推測）。

## 4. 実装の形

### 4.1 前段と backend の口（→D1）

既定: 前段を **generic** にする。`src/drivers/generic/graphics-device.c` と `include/kern/graphics-backend.h`（`struct drv_graphics_backend`
の関数の表）。sun4u が最初の利用者。pcat・pc98 の移行は Future Work（案 `fw-graphics-generic-frontend`、§9、Q1 が登録）。移すまで
generic は実質 3 つ目の前段である。時期は pcat の graphics を次に触る WS、遅くとも Beta 2 の後。

- **backend の登録と優先度（ユーザーの「口だけ残しておきます」）**: `drv_graphics_backend_register(const struct drv_graphics_backend *,
  int priority)`。firmware framebuffer は最低の優先度。選択は boot の時に 1 度だけ、`/dev/graphics` の cdev の登録（vfs.c が呼ぶ）の
  **前**に済ませる。引き取りは boot の時にしか起きないので、graphics 中の引き取りの保留は作らない（m5）。
- 本 WS で登録する backend は firmware framebuffer の 1 つだけ。card の driver を後で足す時の手順（§4.2b）を header の comment に書く。
- **行の分割（M5）**: 前段は user の 1 行を、行の buffer（4096 byte）に入る pixel 数ごとに分けて backend の blit を呼ぶ
  （RGB24 は 1365 pixel、INDEX8 は 4096、MONO1 は 32768 pixel ごと。src の offset と dst の x を進める）。幅による `EINVAL` は
  無くなり、上限は mode の幅で検証する。

### 4.2 後段: firmware framebuffer（本 WS で唯一の backend）

ユーザーの決定（2026-10-19）:「…OpenBootが用意したframebufferを使うのがコンソールとしてはいいですね。/dev/graphicsはfirmware framebufferにします。」

- **出所（loader、stage2）**: `/chosen` の `stdout` の ihandle → `instance-to-package` → その node の `device_type` が `display` なら
  使う。違えば（実機で stdout が ttya の時、m7）`/aliases` の `screen` の path を `finddevice` し、loader が `open` して display を
  初期化してから使う（ihandle は close しない）。どちらも無ければ framebuffer は渡さない。
- property: `width`・`height`・`depth`・`linebytes`。物理 address は `address`（firmware の仮想）を `/chosen` の `mmu` の `translate`
  で物理に引くのを第 1、使えなければ node の `assigned-addresses` の最大の memory の BAR に `pci_memory_base` を足す。
- **形の判別（m8）**: `depth` 8 → RGB332（下の `color!`）、`depth` 24 か 32 で `linebytes >= width*4` → 4 byte の箱、`depth` 24 で
  `linebytes < width*4`（3 byte の packed）→ 対応しない（framebuffer を渡さない、§9 の Future Work の案）。
- **byte の並び（M2）**: OBP の property に byte order は無い。既定: 4 byte の箱は **BE の xRGB（byte が X,R,G,B の順）**
  ＝ `KERN_BOOT_FRAMEBUFFER_XRGB8888`（§4.4）。例外は QEMU の std VGA（node の PCI の id が 1234:1111）だけ: loader が qext の
  byte order の register（MMIO の BAR の 0x604）を**読み**、LE なら `BGRX8888`、BE なら `XRGB8888` を渡す（loader は書き換えない）。
- **8 bpp（M6）**: loader が firmware を出る前に display の ihandle の `color!` で RGB332 の 256 色を設定する。`color!` の
  `call-method` が失敗したら framebuffer を渡さない（2 色の扱いは作らない）。kernel は RGB24 を RGB332 に落とす（Xzed は TrueColor の
  まま、PseudoColor は Future Work）。
- **mode**: firmware が決めた 1 つだけ。`GET_MODES` は 1 件、`ENTER` はその mode を返す（契約上、要求と違ってよい）。
- **写像**: `hal_space_map_device(phys, size, READ|WRITE|NOCACHE)`、`size = linebytes × height`。窓は IE なので、`BGRX8888`（LE）は
  native の `uint32_t` の store、`XRGB8888`（BE）は byte を逆にして store（1 箇所の関数）。RGB332 は byte の store で IE の影響を
  受けない。
- cap: FILL・LINE・PATTERN・BLIT_INDEX8・BLIT_RGB24・BLIT_MONO1・FLUSH（no-op）・GLYPH。
- 置き場所: `src/drivers/platform/sun4u/sun4u-firmware-fb.c`。
- **HAL の早期の console（→D7）**: 既定は (b)。handoff に framebuffer があればそこへ描き、無い時だけ今の Bochs の 640x400 の直の
  設定を fallback に残す。

### 4.2b card の driver の口（driver は今回は作らない、ユーザーの決定）

- 後の WS で card の driver を足す時: (1) 照合は **loader が OBP の tree から列挙した display の node**（PCI の id と
  `assigned-addresses`、HAL の名前付きの値）に限る。kernel の PCI の走査はしない（sun4u に PCI core が無い、M3）。UPA・SBus の
  display には PCI の id が無く、名前付きの値は NULL（m8）。(2) driver は boot の時、`/dev/graphics` の登録の前に、より高い優先度で
  backend を登録する。(3) text の console は `kern_text_register` の置き換え（「A second call replaces the first」）で新しい描き先に
  移す。(4) firmware の写像はその後で使わなくなる（窓の VA は戻らない。`reveal_fatal` は (3) の後は新しい描き先を使う）。
- QEMU の std VGA の card の driver を入れると QEMU で firmware fb の道が通らなくなるので、入れる時は引き取りを止める boot
  parameter を同時に作る（m9）。
- 本 WS の成果物は (2) の登録と優先度の関数、header の comment、選択の host 試験（偽の backend 2 つ）だけ。

### 4.3 text の console と出力の道（→D2、M1）

既定: **kernel が画面の text の console を持つ**（hal.h の「kernel_putc を publish したら HAL は display に触れない」どおり、HAL は不変）。
出力の道を 1 本にする:

| 書き手 | 行き先 |
| --- | --- |
| tty の出力（sun4u-console.c の `console_putc`） | grid と 16550 の THR に**直に**（`hal_putc` を通さない） |
| `kernel_putc`（sun4u-console.c が publish） | 同じ関数（grid と THR） |
| `kern_platform_debug_write`（sun4u.c） | `kernel_putc` の publish の後は何もしない（klog が既に `kernel_putc` に書いたので二重にしない）。publish の前は今の `hal_putc` のまま |

- 再帰しない: `kernel_putc` は `kern_log_write` も `hal_putc` も呼ばない（pcat の `kern_text_kernel_putc` は使わない）。
- grid と THR の更新は **IRQ-safe の spinlock 1 つ**で守る（panic の時は try にし、取れなくても書く）。
- grid: 80 列 × 行数（fb の高さ / 16）、8x16 の font、firmware fb の写像に描く。`suspend` で描画を止め、`resume` で **fb の全体を
  消してから**全 cell を描き直す（m6）。
- THR の port は `sun4u.serial.port`（既存の名前付きの値）。

### 4.4 HAL の承認の差分（M6、→ [proposed/hal-sun4u-names.md](proposed/hal-sun4u-names.md)）

hal.h は変えない。承認を求めるのは (1) `include/kern/boot.h` の定数 2 つと定義文、(2) bsp-sun4u の `hal_get_arch_handoff` の名前 10。
具体的な差分は proposed に置いた（未適用）。要点:

- `KERN_BOOT_FRAMEBUFFER_XRGB8888 = 3U`（4 byte、byte が X,R,G,B の順）、`KERN_BOOT_FRAMEBUFFER_RGB332 = 4U`（1 byte、loader が
  firmware の palette に RGB332 を設定済み）。LE の xRGB は既存の `BGRX8888`。
- `color!` の失敗は framebuffer を渡さない。`sun4u.framebuffer.size` は `linebytes × height`。

### 4.5 入力（→D4・D5）

- QEMU sun4u の i8042 を **poll**（kthread で 10 ms ごと）。
- 既定 D5(a): pcat の [ps2-8042.c](../../src/drivers/platform/pcat/ps2-8042.c) を `src/drivers/isa/ps2-8042.c` へ移し、port の I/O を
  `kern_io_in8`/`kern_io_out8`、port の番号を引数、IRQ か poll かを選べる形にする。host 試験の hook（l.190〜198）は保つ。
- **初期化（m1）**: reset 直後の translate の有無に頼らず、driver が config byte を自分で書く（translate の bit を立て、poll の時は
  IRQ の bit を落とす）。pcat の set 1 の表をそのまま使う。
- keyboard と mouse は evdev（`drv_input_device_register`）に出す。kernel と user は同じ endian なので変換は要らない。
- **console にも入る（m2）**: 汎用の console は全 evdev の keyboard を subscribe する（[console.c](../../src/drivers/generic/console.c)
  l.1052）。画面の login も keyboard で打てるようになる。Xzed は grab しないので `keys:` は背後の root の shell にも届く（pcat と
  同じと推測）。試験で打つ文字列は shell に入っても無害な物（`echo ws275-ok`）に限る。

### 4.6 userland と build

- [platform/sparcv9/vmunix.mk](../../platform/sparcv9/vmunix.mk) は選ばれた basic の program を static に link する。Xzed・zterm・zwm・
  zshell は class basic・platform `*`・既定 n で、config で選べば build の対象になる。startx・Xzedrc・zwm.conf は zwm を選ぶと
  `ZEDBSD_PACKAGE_FILES` で rootfs に入る。
- **arch の分岐を足さない（M8）**: X11 の program は platform `*` のまま、arch の分岐を足さない。base の全部を sparcv9 で build する
  のはユーザーの担当（`plan/master.md` l.651）で、本 WS の外部の条件。
- 試験の config は `plan/ws275/tests/config-sun4u-x11.mk`（[config-sun4u.mk](../tools/ports/config-sun4u.mk) の写しに Xzed・zterm・
  zwm・zshell）。image は config の build と個別の file の複写だけで作る（2026-10-04 の規則）。
- top の Makefile: `ZEDBSD_GRAPHICS_BACKEND` に sparcv9 → sun4u、「Only PC/AT and PC-98」の注記（l.519〜526）を直す（m12）。
- sparcv9 の vmunix.mk: `CONFIG_DRIVER_GRAPHICS_DEVICE=y` の時だけ graphics・ps2 の file を足し、`$(ZEDBSD_GRAPHICS_CONFIG_STAMP)`
  の依存を足す（`platform/pcat/vmunix.mk` l.82・362 と同じ形、m12）。
- **font（M4、D3）**: sun4u の kernel は pcat の `src/drivers/platform/pcat/graphics/vgafont.c` を**そのまま compile する**（移さない）。
  boot-test.py・amd64 の HAL の cons.c・各 WS の試験が path と symbol `drv_pcat_vgafont16` を読むため。
- UFS の root は 60 MiB。static の X の 4 program の大きさを p005 で見て、足りなければ `--size-mib` を上げる。

### 4.7 big endian の X11

- Xzed の protocol は client の byte order で読み書きし、visual の mask は数値なので endian に依らない。
- libX11（[xlib.c](../../userland/x11/libX11/xlib.c)）は常に `'l'` で、`XChangeProperty` の format 16・32 の data を native の順の
  `memcpy` で送る。BE では誤りだが、Xzed は format 8 の WM_NAME と icon path しか保持せず（main.c l.2075〜2100）format 32 は捨てるので、
  **今は潜在の欠陥**（m3）。p005 で要素ごとに `w16`/`w32` で詰め、`XGetWindowProperty` の逆も直す。完了の条件には効かない。

## 5. 判定（B1・M2・M7）

確かめは全部 **T1** に依頼する（実装の担当は QEMU を起動しない、AGENTS.md 2026-10-03）。§8 の観察も T1 への依頼にし、待つ間は
担当は別の作業へ移る。

- **文字の原点（B1、Q1 の道具）**: boot-test.py は `BOOT_TEXT_ORIGINS="x,y x,y …"` で読む文字の原点を追加で受け、既存の原点と並べて
  認識の多い読みを取る（2026-10-19 Q1 が main に入れた。全 offset の探索は遅いので採らない）。zterm の原点の計算:
  - zterm は `XCreateSimpleWindow(…, 20, 8, …)`（[zterm/main.c](../../userland/x11/zterm/main.c) l.346）、内側の余白は無い
    （cell (c,r) は窓の (c×8, r×16) から、`XDrawString16` の baseline は (r+1)×16。8x16 の glyph の上端は r×16 と見る）。
  - zwm は client を frame の (`FRAME_BORDER`=2, `TITLE_HEIGHT`=26) に入れる（[zwm/main.c](../../userland/x11/zwm/main.c) l.30〜33）。
  - frame の位置は未確認なので候補を 2 つ渡す: frame が要求の (20,8) に置かれる時は **(22,34)**、client が要求の (20,8) に残る形に
    frame を置く時は **(20,8)**。p006 の T1 の依頼は `BOOT_TEXT_ORIGINS="22,34 20,8"`。合うかは T1 の最初の走りで確かめ、ずれたら
    PNG から原点を求めて依頼を直す。
- **Q1 への残りの依頼（外部の依存）**: (1) `plan/tools/boot-test.sh` に QEMU の引数を足す口（例 `BOOT_QEMU_EXTRA='-g 1024x768x32'`）。
  (2) `mouse:DX,DY[,BUTTON]` の step（QMP の `input-send-event` の rel と btn）。
- 試験は 2 本（M2）: `-g 1024x768x8`（RGB332）と `-g 1024x768x32`（qext で決まる 32 bpp）。
- 手順（各本）:

```
BOOT_TEXT_ORIGINS="22,34 20,8"
BOOT_PATTERN: ^(?:\S+ )?login: ?
BOOT_THEN:
  type:root
  type:startx &
  expect:<zterm の prompt の正規表現（p005 で zterm の画面で決める）>
  keys:echo ws275-ok
  expect:ws275-ok
  mouse:…（zwm の menu を開く）
  expect:<zwm の menu の文字>
```

- **代わりの判定**: 原点が合わない・認識が不安定な時は、T1 が撮った PNG を Q1 が目視し、zterm の窓・`ws275-ok`・zwm の menu が
  見えるかで判定する（PNG はユーザーにも見せる）。
- 回帰（T1、1 つの QEMU ずつ）: pcat の bios-ide の login と PS/2（D5 で ps2 を移すため）、amd64 の uefi-nvme の login、sun4u の
  login（text の console の引き継ぎ）。

## 6. 変える file（sparcv9 はユーザーの担当、→D6）

| file | 変更 | sparcv9 |
| --- | --- | --- |
| `bootloader/sparcv9/handoff.h`・`handoff.c`・stage2 | display の node（stdout・`screen` の fallback と open）、property・`translate`・BAR、qext の読み、`color!`、i8042 の discover（D4）、handoff 版 3 | はい |
| `src/hal/sparcv9/bsp-sun4u/framebuffer.c` | handoff の framebuffer に描く、Bochs の直の設定は fallback（D7） | はい |
| `src/hal/sparcv9/bsp-sun4u/boot.c` | 名前付きの値 10（承認後） | はい |
| `src/drivers/platform/sun4u/sun4u-firmware-fb.c`（新） | firmware framebuffer の backend | はい |
| `src/drivers/platform/sun4u/sun4u-console.c`・`.h` | kernel_putc の publish、grid と THR に直に、spinlock、suspend/resume | はい |
| `src/kern/platform/sun4u.c` | `kern_platform_debug_write` を publish 後は何もしない、ps2 の起動 | はい |
| `platform/sparcv9/vmunix.mk` | kernel の source（graphics・ps2・pcat の vgafont.c）、config の stamp の依存、root の大きさ | はい |
| `include/kern/boot.h` | 定数 2 つと定義文（承認後） | いいえ |
| `src/drivers/generic/graphics-device.c`（新）・`include/kern/graphics-backend.h`（新） | generic の前段、backend の登録と優先度、行の分割 | いいえ |
| `src/drivers/isa/ps2-8042.c`（移動）・`src/drivers/platform/pcat/ps2-8042.c`・`.h` | 共有の 8042（D5） | いいえ（pcat） |
| `platform/pcat/vmunix.mk`・`platform/amd64/vmunix.mk` | ps2 の移動の path | いいえ |
| `plan/ws081/tests/host-ps2-wheel.sh`・`plan/ws073/tests` の ps2 の path | 移動に追従（他の WS なので Q1 に依頼、参照が無ければ削除の基準を当てる） | いいえ（m10） |
| top の `Makefile` | `ZEDBSD_GRAPHICS_BACKEND` に sun4u、注記 | いいえ |
| `userland/x11/libX11/xlib.c` | property の endian（潜在の欠陥） | いいえ |
| `docs/reference/control-devices.md` | sun4u の driver を足す（l.43〜46） | いいえ |
| `plan/ws275/tests/config-sun4u-x11.mk`（新） | 試験の config | いいえ |
| `plan/tools/boot-test.sh` | QEMU の引数の口、`mouse:`（Q1） | いいえ |

## 7. Phase と LW

| Phase | 中身 | 依存 | LW |
| --- | --- | --- | --- |
| p001 | この設計、design-reviewer（改訂 2 の再レビュー） | — | 1 |
| p002 | loader（display の node・property・qext・`color!`・i8042）、boot.h と HAL の名前付きの値、HAL の早期の console（D7） | p001、HAL の承認、D4・D6 | 2 |
| p003 | generic の前段（登録・優先度・行の分割）、firmware fb の backend、text の console と出力の道（D2）、host 試験 | p002 | 4 |
| p004 | 8042 の共有（D5）と sun4u の poll・config byte、evdev、ps2 の試験の path の追従 | p002 | 3 |
| p005 | userland: 試験の config、Makefile・vmunix.mk、libX11 の BE、root の大きさ、build（warning 0） | p003・p004 | 2 |
| p006 | T1 の試験（sun4u の 8 bpp・32 bpp の X、回帰）、結果の判定 | p005、**外部: Q1 の boot-test.sh の QEMU の引数の口と `mouse:`**（`BOOT_TEXT_ORIGINS` は入った。無ければ PNG の目視で代える） | 1 |
| p007 | 規約の全文の見直し（p002〜p005 の分） | p006 | 1 |

計 14 LW（約 4〜5 時間）。T1 の待ちの間、担当は別の作業へ移る。

## 8. 危険と未確認（観察は T1 に依頼）

- IE の写像で VRAM に 32 bit の store を連ねた時の QEMU の VGA の振る舞い（T1: gdbstub で 1 pixel を書いて monitor の `xp`）。
- ebus の ISA の I/O が PCI の I/O の 0x60/0x64 に見えるか（T1: monitor の `info mtree`）。
- OpenBIOS の display の node が `width`・`depth`・`linebytes` を出すか、`translate`・`color!` が効くか、qext の値（T1: gdb で handoff）。
- QEMU の既定の `graphic_depth`（8 と推測）。zwm の frame の位置（§5 の原点）。
- TCG の描画の速さ（全面の BLIT が boot-test の timeout に響くか）。

## 9. 本 WS の外（M3、Future Work の案、Q1 が登録）

| 案の ID | 中身 | 理由 |
| --- | --- | --- |
| fw-sun4u-real-machine | Sun Blade 150 で起動する: loader の CMD646 の限定（handoff.c l.103〜109・416）を外し ALi M5229 の IDE、USB（OHCI）の keyboard・mouse、実機の ebus の扱い | 実機は PS/2 が無く CMD646 でないと推測（未確認） |
| fw-sun4u-pci | sun4u の PCI の config の access（sabre・IIe）と PCI core | card の driver が BAR・mode を扱う時に要る |
| fw-sun4u-card-drivers | 既知の card の driver（onboard の PGX64＝Rage XL の可能性、XVR-100・500・600）、§4.2b の口を使う | ユーザー「Sun PGX64とかはあとで対応できるように口だけ残しておきます。」 |
| fw-sun4u-packed24 | depth 24 の 3 byte の packed の framebuffer | 該当の firmware が出るまで不要 |
| fw-graphics-generic-frontend | pcat・pc98 の前段を generic へ移す | §4.1 |

実機の確かめには、ユーザーの OBP の `show-displays` と display の node の `.properties` の写しが要る（ユーザーの実機の一覧に足す案）。

## 10. 出典

- QEMU v10.0.0 の `hw/sparc64/sun4u.c`（https://gitlab.com/qemu-project/qemu/-/raw/v10.0.0/hw/sparc64/sun4u.c、SHA-256
  e75ff4abf985a0f198ef9dfc4c1b7386930e29639895798954ba6cf5675916ae）: ebus の ISA（l.304〜330）、IRQ（l.610〜619）、VGA（l.621〜624）、
  fw_cfg の depth（l.709〜711）。
- QEMU v10.0.0 の `hw/pci-host/sabre.c`（SHA-256 73856ada0597743ff8c3bb73d29814d689ff8e233e04a89fca4fb1e1ad312850）。
- 置き場所: `/home/awe/zedBSD-worktrees/p1/build/tmp/ws275-docs/`（commit しない）。host の QEMU は Debian の 10.0.11。
- code に値を写す時は Guardrail の「Value(s) referenced:」の block の comment で示す。

## 11. ユーザーに聞く判断

| ID | 問い | 選択肢 | 既定と理由 |
| --- | --- | --- | --- |
| D6 | sparcv9 の file（§6 の「はい」の 7 つ）を誰が変えるか | (a) P の担当が変え、Q1 が merge の前にユーザーに差分を見せる (b) ユーザーが変える (c) ユーザーの今の sparcv9 の作業が終わってから P の担当が変える | 既定なし（ユーザーの担当の領域）。進めやすさでは (a)、ユーザーの作業と衝突するなら (c) |
| HAL | boot.h の 2 定数と bsp-sun4u の名前 10（proposed の差分） | 承認／一部承認／不承認 | 承認を求める。hal.h は不変、WS256 の VF2 と同じ形 |
| D4 | i8042 の port の出所 | (a) loader が OpenBIOS の tree から取り handoff 版 3 (b) HAL が ISA の固定値を名前で返す | (a)。serial と同じ形で機種差に備える |
| D5 | 8042 の driver | (a) pcat の driver を `src/drivers/isa/` へ移す（pcat の PS/2 の回帰が要る） (b) sun4u 用に新しく書く（約 600 行の重複） | (a)。重複を作らない。回帰は T1 の 1 本 |
| D7 | QEMU での Bochs の直の設定 | (a) やめる (b) handoff に framebuffer が無い時の fallback に残す | (b)。OpenBIOS の property が揃う事を T1 で確かめたら (a) に |
| D1 | /dev/graphics の前段 | (a) generic を作り sun4u だけ使う（pcat・pc98 は Future Work） (b) pcat の前段を写す (c) generic にして pcat・pc98 も今移す | (a)。backend の口を置く場所が要り、pcat の graphics の回帰を本 WS に入れない |
| D2 | graphics の間の text の console | (a) kernel が kernel_putc を publish し出力の道を 1 本に（§4.3、HAL 不変） (b) HAL に描き直しを足す（承認） (c) graphics の間は serial だけ | (a) |

聞かない物: D3 は (b) pcat の vgafont.c をそのまま compile（M4 で既定を変えた。移すと boot-test が壊れる）。D8（最初の card）は
ユーザーの決定（18:29）で今は選ばない。8 bpp の `color!` の失敗は framebuffer を渡さない（設計の決定）。

## 12. レビュー 1 への対応

| 指摘 | 対応 | 所 |
| --- | --- | --- |
| B1 判定ができない | Q1 が入れた `BOOT_TEXT_ORIGINS` に zterm の原点の候補（22,34・20,8）を渡す形にし、T1 の最初の走りで確かめる。代わりに T1 の PNG の目視 | §5、§7 p006 |
| M1 二重の出力・再帰 | tty の出力と kernel_putc は grid と THR に直に、debug_write は publish 後は何もしない、IRQ-safe の spinlock 1 つ | §4.3、§6 |
| M2 32 bpp・LE の根拠 | 既定 BE の xRGB、1234:1111 の時だけ qext を読んで決める（書かない）、試験は 8・32 bpp の 2 本（QEMU の引数の口は Q1） | §4.2、§5 |
| M3 実機の依存 | 実機・sun4u の PCI・card の driver を §9 の Future Work の案に分けた。照合は loader が列挙した display の node に限る。card の driver は口だけ | §4.2b、§9、版 1 の p008〜p010 を外した |
| M4 font の移動 | D3 を (b) pcat の vgafont.c をそのまま compile に | §4.6、§11 |
| M5 行の buffer | 前段で 4096 byte ごとに分割 | §4.1 |
| M6 HAL の差分 | boot.h の `XRGB8888`=3・`RGB332`=4 と定義文、`color!` の失敗は fb を渡さない、`.size`=linebytes×height | §4.4、proposed |
| M7 担当が QEMU を起動 | 観察は全部 T1 に依頼 | §5、§8 |
| M8 条件 3 の矛盾 | 「X11 の program は platform * のまま、arch の分岐を足さない」に。base の全部はユーザーの担当の外部の条件 | §4.6、ws.md |
| m1 translate | driver が config byte を自分で書く | §4.5 |
| m2 console にも入る | 明記し、試験の文字列を無害な物に | §4.5 |
| m3 libX11 の過大 | 「潜在」に、完了の条件に効かないと明記 | §4.7 |
| m4 D1 の理由 | 理由を直し、移行の Future Work の ID と時期を書いた（D3 は移さないので font の回帰は無い。D5 の PS/2 の回帰は明記） | §4.1、§11 |
| m5 引き取りの保留 | 保留を削り、boot の時に登録の前で選ぶ、`kern_text_register` の置き換え、unmap の順 | §4.1、§4.2b |
| m6 resume の範囲 | fb の全体を消してから描き直す | §4.3 |
| m7 stdout が screen でない | `/aliases` の `screen` の fallback と loader の open | §4.2 |
| m8 packed 24・PCI の id | packed 24 は渡さない（Future Work）、UPA・SBus は id が NULL | §4.2、§4.2b、§9 |
| m9 QEMU の std VGA の driver | 入れる時は引き取りを止める boot parameter を同時に | §4.2b |
| m10 ps2 の移動の追従 | §6 に ws081・ws073 の試験の path（Q1 に依頼）と host 試験の hook | §4.5、§6 |
| m11 記録の不整合 | ws.md の Phase の表を p002〜p007 で埋め、条件 1 の「写像」を直した | ws.md |
| m12 build の細部 | config の stamp の依存、Makefile の注記、control-devices.md | §4.6、§6 |
