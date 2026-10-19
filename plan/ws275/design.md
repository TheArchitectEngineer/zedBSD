<!-- awesome-plan project=zedbsd record=ws275-design -->

# WS275 設計: sun4u の /dev/graphics と Xzed（改訂 3）

- 親: [ws.md](ws.md)、Phase: [p001](phase001/phase.md)（版 1: 2026-10-19 P1 q1303、改訂 2: 2026-10-19 P1 q1308、改訂 3: 2026-10-19 P1 q1309）
- 起点（2026-10-19 ユーザー）:「では、sun4uでも/dev/graphicsを実装して、Xzedが動くようにします。」
- 範囲（2026-10-19 18:29 ユーザー「まずAのフレームバッファだけ実装して、Sun PGX64とかはあとで対応できるように口だけ残しておきます。」）:
  OBP（OpenBoot・OpenBIOS）の firmware framebuffer を受け皿にした /dev/graphics と Xzed（QEMU の `-machine sun4u` で判定）、
  既知の card の driver が後で引き取れる**口**（backend の登録と優先度）。個々の card の driver（PGX64・XVR-500/600 など）と実機
  （Sun Blade 150）への道は本 WS の外（§9）。
- 版 1 のレビュー [design-review-1.md](design-review-1.md)（Blocker 1・Major 8・Minor 12）への対応は §12、再レビュー [design-review-2.md](design-review-2.md)（Blocker 0・Major 6・Minor 12）への対応は §13。
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
の関数の表）。sun4u が最初の利用者。pcat・pc98 の移行は Future Work（F-111、§9）。移すまで
generic は実質 3 つ目の前段である。時期は pcat の graphics を次に触る WS、遅くとも Beta 2 の後（F-111 の再考の契機をこれに揃える、Q1 に依頼、m-7）。

- **backend の登録と優先度（ユーザーの「口だけ残しておきます」）**: `drv_graphics_backend_register(const struct drv_graphics_backend *,
  int priority)`。firmware framebuffer は最低の優先度。選択は boot の時に 1 度だけ、`/dev/graphics` の cdev の登録（vfs.c が呼ぶ）の
  **前**に済ませる。引き取りは boot の時にしか起きないので、graphics 中の引き取りの保留は作らない（m5）。
- 本 WS で登録する backend は firmware framebuffer の 1 つだけ。card の driver を後で足す時の手順（§4.2b）を header の comment に書く。
- **行の分割（M5、M-1）**: 前段は user の 1 行を chunk に分けて backend の blit を呼ぶ。chunk の pixel 数は **copyin の buffer と
  展開後の buffer（`row_buffer`、4096 byte）の両方**に入る数で決める:
  - RGB24: 展開しない（3 byte/pixel のまま渡す）。1365 pixel（1365×3＝4095 ≤ 4096）。
  - INDEX8: 1 byte/pixel。4096 pixel。
  - MONO1: copyin の `packed` を 512 byte にし、展開後は 1 pixel 1 byte なので **4096 pixel**（packed 512 byte＝4096 bit と
    `row_buffer` 4096 byte の両方に入る）。chunk の境界は **8 の倍数**（src の bit offset を byte で進めるため。行の頭の bit の offset が
    0 でない時は、最初の chunk を次の 8 の倍数の境界で切る）。
  - 各 chunk で src の offset と dst の x を進める。幅による `EINVAL` は無くなり、上限は mode の幅で検証する。`packed` を
    `row_buffer` より大きくしない（展開で越えて書くため、header の comment に書く）。

### 4.2 後段: firmware framebuffer（本 WS で唯一の backend）

ユーザーの決定（2026-10-19）:「…OpenBootが用意したframebufferを使うのがコンソールとしてはいいですね。/dev/graphicsはfirmware framebufferにします。」

- **出所（loader、stage2）**: `/chosen` の `stdout` の ihandle → `instance-to-package` → その node の `device_type` が `display` なら
  使う。違えば（実機で stdout が ttya の時、m7）`/aliases` の `screen` の path を `finddevice` し、loader が `open` して display を
  初期化してから使う（ihandle は close しない）。どちらも無ければ framebuffer は渡さない。
- property: `width`・`height`・`depth`・`linebytes`。物理 address は `address`（firmware の仮想）を `/chosen` の `mmu` の `translate`
  で物理に引くのを第 1、使えなければ node の `assigned-addresses` の最大の memory の BAR に `pci_memory_base` を足す。
- **形の判別（m8、m-4）**: `depth` 8 → RGB332（下の `color!`）、`depth` 24 か 32 で `linebytes >= width*4` → 4 byte の箱、`depth` 24 で
  `linebytes < width*4`（3 byte の packed）→ 対応しない（F-110）。`depth` 15・16、その他の値 → 対応しない（framebuffer を渡さない）。
- **byte の並び（M2、M-6）**: 以下「X,R,G,B の順」は **memory の address の昇順**の byte の並び。 OBP の property に byte order は無い。既定: 4 byte の箱は **BE の xRGB（byte が X,R,G,B の順）**
  ＝ `KERN_BOOT_FRAMEBUFFER_XRGB8888`（§4.4）。例外は QEMU の std VGA（node の PCI の id が 1234:1111）だけ: loader が qext の
  byte order の register（MMIO の BAR の 0x604）を**読み**、0x1e1e1e1e（LE）なら `BGRX8888`、0xbebebebe（BE）なら `XRGB8888` を
  渡す（loader は書き換えない）。**それ以外の値**（register が無い、古い QEMU）は既定の `XRGB8888`。
- **qext の読み方（m-4）**: loader は OBP の client interface で物理を読む helper を持たない（`bootloader/sparcv9/` に無く、`ofw.h` に
  `instance-to-package`・汎用の `call-method` も無い）。p002 で `ofw.h` に `instance-to-package` と `call-method` の wrapper を足し、
  物理の load は stage2 の中の ASI_PHYS_BYPASS_EC_E_L（LE）の `lduwa` で行う（loader は firmware の上で特権で走る。可否は T1 の観察 p002b
  で確かめる、§8）。p002 の LW を 2 → 3 にした。
- **8 bpp（M6）**: loader が firmware を出る前に display の ihandle の `color!` で RGB332 の 256 色を設定する。`color!` の
  `call-method` が失敗したら framebuffer を渡さない（2 色の扱いは作らない）。kernel は RGB24 を RGB332 に落とす（Xzed は TrueColor の
  まま、PseudoColor は Future Work）。
- **mode**: firmware が決めた 1 つだけ。`GET_MODES` は 1 件、`ENTER` はその mode を返す（契約上、要求と違ってよい）。
- **写像**: `hal_space_map_device(phys, size, READ|WRITE|NOCACHE)`、`size = linebytes × height`。窓は IE なので、`BGRX8888`（LE）は
  native の `uint32_t` の store、`XRGB8888`（BE）は byte を逆にして store（1 箇所の関数）。RGB332 は byte の store で IE の影響を
  受けない。**IE の窓に 64 bit の store は使わない**（2 画素の順が入れ替わるため、m-2）。
- cap: FILL・LINE・PATTERN・BLIT_INDEX8・BLIT_RGB24・BLIT_MONO1・FLUSH（no-op）・GLYPH。
- 置き場所: `src/drivers/platform/sun4u/sun4u-firmware-fb.c`。
- **HAL の早期の console（→D7）**: 既定は (b)。handoff に framebuffer があればそこへ描き、無い時だけ今の Bochs の 640x400 の直の
  設定を fallback に残す。
- **fallback の時も名前を返す（M-3、案 (i)）**: `sun4u.framebuffer.*` の意味を「**HAL が表示している framebuffer**」とする。HAL が
  Bochs の fallback で画面を出した時も、HAL は同じ名前で phys＝`pci_memory_base`＋BAR0、640x400、pitch 2560、format `BGRX8888`
  （HAL が qext を LE に書くので）を返す。kernel は名前の出所を区別せず、両方の道で kernel_putc を publish して画面に login を出す。
  HAL が画面を出せなかった時だけ名前は NULL で、その時 kernel は kernel_putc を publish せず、今の `console_putc`→`hal_putc` の道を残す
  （案 (ii) を「画面が無い時」に限って併用）。

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
- grid と THR の更新は **IRQ-safe の spinlock 1 つ**で守る。panic の判別の API は無い（`include/kern/lock.h` は `spin_trylock`
  だけ）ので、**常に `spin_trylock` を試し、失敗したら lock 無しで書く**（sun4u は UP、取れないのは同じ CPU の割り込みか panic の再入だけ、m-2）。
- suspend の flag は **この spinlock の中で**立て・落とす（ENTER の後に console が X の上に描かないため、m-2）。
- grid: 80 列 × 行数（fb の高さ / 16）、8x16 の font、firmware fb の写像に描く。`suspend` で描画を止め、`resume` で **fb の全体を
  消してから**全 cell を描き直す（m6）。
- THR の port は `sun4u.serial.port`（既存の名前付きの値）。
- **quiet の時（m-1）**: klog は quiet の時に kernel_putc を飛ばし（`klog.c:128-131`）、debug_write は publish 後に何もしないので、
  quiet の record は serial にも出ず dmesg にだけ残る。今の sun4u（quiet でも serial に出る）から挙動が変わるが、pcat（debugcon だけ）
  と揃うのでこれを採る。
- **`reveal_fatal`（m-3）**: sun4u の text の ops に `reveal_fatal` を足す（`include/kern/text-display.h:110-114`）。suspend を lock 無しで
  解き、fb の全体を消して grid を描き直す。X の動作中の panic も画面に出る。
- **HAL からの引き継ぎ（m-9）**: kernel_putc の publish の時に fb の全体を消し、kernel の grid（80 × (高さ / 16)）の先頭から描く
  （HAL の 80×25 の行は続けない。HAL の行は serial と dmesg に残る）。

### 4.4 HAL の承認の差分（M6、→ [proposed/hal-sun4u-names.md](proposed/hal-sun4u-names.md)）

hal.h は変えない。承認を求めるのは (1) `include/kern/boot.h` の定数 2 つと定義文、(2) bsp-sun4u の `hal_get_arch_handoff` の名前 10。
具体的な差分は proposed に置いた（未適用、改訂 3 で定義文と名前の意味を変えたので、改めてユーザーに示す）。要点:

- 定義文に「bytes in increasing address order」と書く（M-6）。`KERN_BOOT_FRAMEBUFFER_XRGB8888 = 3U` は memory で X,R,G,B の順
  （DRM の `XRGB8888`＝zedBSD の `BGRX8888` とは**逆**であることも定義文に書く）。`KERN_BOOT_FRAMEBUFFER_RGB332 = 4U` は 1 byte、
  red は bit 7-5、green は 4-2、blue は 1-0、palette の index i の色は red＝((i>>5)&7)×255/7、green＝((i>>2)&7)×255/7、
  blue＝(i&3)×255/3（pcat の `rgb332()` と DAC の ramp `backend.c:824-826` と同じ並び）。
- `sun4u.framebuffer.*` は「HAL が表示している framebuffer」（M-3、fallback の時も返す）。
- sun4u の framebuffer.c を触る時に、tree の「XRGB8888」の comment（l.94・l.251、DRM の意味）を「LE の word、memory では blue が先」と
  明記する（M-6）。
- `color!` の失敗は framebuffer を渡さない。`sun4u.framebuffer.size` は `linebytes × height`。

### 4.5 入力（→D4・D5）

- QEMU sun4u の i8042 を **poll**（kthread で 10 ms ごと）。
- 既定 D5(a): pcat の [ps2-8042.c](../../src/drivers/platform/pcat/ps2-8042.c) を `src/drivers/isa/ps2-8042.c` へ移し、port の I/O を
  `kern_io_in8`/`kern_io_out8`、port の番号を引数、IRQ か poll かを選べる形にする。host 試験の hook（l.190〜198）は保つ。
- D4 は p002 の前にユーザーに聞く（m-12）。proposed には (a) の差分だけを置き、(b) を選ばれたら差分を書き直して改めて示す。
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
  のはユーザーの担当（`plan/master.md` l.652）で、本 WS の外部の条件。
- 試験の config は `plan/ws275/tests/config-sun4u-x11.mk`（[config-sun4u.mk](../tools/ports/config-sun4u.mk) の写しに Xzed・zterm・
  zwm・zshell）。image は config の build と個別の file の複写だけで作る（2026-10-04 の規則）。
- top の Makefile: `ZEDBSD_GRAPHICS_BACKEND` に sparcv9 → sun4u、「Only PC/AT and PC-98」の注記（l.519〜526）を直す（m12）。
- sparcv9 の vmunix.mk: `CONFIG_DRIVER_GRAPHICS_DEVICE=y` の時だけ graphics・ps2 の file を足し、`$(ZEDBSD_GRAPHICS_CONFIG_STAMP)`
  の依存を足す（`platform/pcat/vmunix.mk` l.82・362 と同じ形、m12）。
- **font（M4、D3）**: sun4u の kernel は pcat の `src/drivers/platform/pcat/graphics/vgafont.c` を**そのまま compile する**（移さない）。
  boot-test.py・amd64 の HAL の cons.c・各 WS の試験が path と symbol `drv_pcat_vgafont16` を読むため。
  - **Guardrail の例外（m-10）**: pcat の platform の file を sun4u の kernel で compile するのは driver の配置の方針（guardrail.md l.18）を
    越える。本 WS の例外として Q1 に Guardrail の例外の表への記録を依頼する（範囲: vgafont.c の 1 file、理由: 上の参照の追従、期限: F-111 で
    font を generic へ移す時に boot-test.py の参照と一緒に移す）。
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
- **boot-test の口（m-7、main に入済み）**: QEMU の引数は `QEMU_EXTRA`（`plan/tools/boot-test.sh:389-397`、例
  `QEMU_EXTRA='-g 1024x768x32'`）、mouse は `mouse:DX,DY[,BUTTON]` の step（同 l.31・432）。外部の依存は残らない。
- **zterm の原点（m-8）**: zwm は frame を client の要求の x,y に置き（`zwm/main.c:980`）、client を (2,26) に入れる（:1020）ので
  原点は **(22,34)**。(20,8) は zwm が管理に失敗した時だけの候補として残す。
- 試験は 3 本（M2、M-5）: `-g 1024x768x8`（RGB332）、`-g 1024x768x32`（qext で決まる 32 bpp）、`-g` 無し（既定の depth）。
- 手順（各本）:

```
QEMU_EXTRA='-g 1024x768x8'   （または x32、または無し）
BOOT_TEXT_ORIGINS="22,34 20,8"
BOOT_PATTERN: ^(?:\S+ )?login: ?
BOOT_THEN:
  type:root
  type:startx &
  expect:<zterm の prompt の正規表現（p005 で zterm の画面で決める）>
  keys:echo ws275-ok | tr a-z A-Z
  expect:^WS275-OK$
  mouse:-2000,-2000
  mouse:<DX>,<DY>              （左上から zterm の「WS275-OK」の行の語の上へ。DX・DY は p005 で原点と行から計算）
  mouse:0,0,left
  mouse:0,0,left               （double click、zterm の DOUBLE_CLICK_MS は 400）
  mouse:0,0,middle             （PRIMARY を貼る、zterm/main.c:1458-1485。貼る先は pointer の位置に依らず shell の入力）
  keys:<Enter>
  expect:<WS275-OK を command として打った shell のエラーの行（例 `WS275-OK: not found`、p005 で zterm の shell の文言に合わせる）>
```

  - `expect:^WS275-OK$` は打った行（`echo ws275-ok | tr …`、小文字）に合わず、出力の行にだけ合う（m-8）。
  - mouse の判定（M-2）: double click で選んだ語を中の click で貼り、shell がその語を command として実行した行で確かめる。zwm に
    menu は無いので menu は使わない。`mouse:-2000,-2000` で Xzed が座標を左上に clamp すること、加速が無いこと、2 回の click が 400 ms に
    入ることは未確認（§8）。
- **既知の色の画素の判定（M-4）**: 各本の PNG（T1 の最後の画面）で、T1 が既知の色の画素を読む（`python3 -I` の小さな script で PNG を
  解き、値で比べる。目視ではない）:
  - Xzed の root（0x203040）: zterm の frame の外、例えば (1016,400)。
  - zshell の bar（0x17212b）: bar の中の文字の無い点（p005 で座標を決める）。
  - 32 bpp は各色の R・G・B が完全一致。8 bpp は RGB332 に量子化し ramp で戻した期待値と一致（0x203040 → index 0x25 →
    (0x24,0x24,0x55)、0x17212b → index 0x04 → (0x00,0x24,0x00)。式は §4.4。値は p005 で script の出力と照らす）。
    R と B の入れ替わり・X の byte の混入はここで見える。
- **代わりの判定**: 原点が合わない・認識が不安定な時は、T1 が撮った PNG を Q1 が目視し、zterm の窓・`WS275-OK`、mouse の結果として
  (1) double click で反転した語（選択の表示）、(2) 貼られて実行された行、(3) Xzed の pointer が移動前と後の PNG で動いたこと、の
  どれが見えるかで判定する（PNG はユーザーにも見せる）。色の判定は目視に代えない。
- 回帰（T1、1 つの QEMU ずつ）: pcat の bios-ide の login、pcat の PS/2（D5 で ps2 を移すため。bios-ide の login の試験は `type:` が
  serial なので PS/2 を通らない。`keys:` の step を含む試験と `plan/ws081/tests/ps2-wheel-qemu.sh`（T1-015 で使用）を流す、m-11）、
  amd64 の uefi-nvme の login、sun4u の login（text の console の引き継ぎ）。

## 6. 変える file（sparcv9 はユーザーの担当、→D6）

| file | 変更 | sparcv9 |
| --- | --- | --- |
| `bootloader/sparcv9/handoff.h`・`handoff.c`・stage2・`ofw.h` | display の node（stdout・`screen` の fallback と open）、property・`translate`・BAR、qext の読み、`color!`、i8042 の discover（D4）、handoff 版 3 | はい |
| `src/hal/sparcv9/bsp-sun4u/framebuffer.c` | handoff の framebuffer に描く、Bochs の直の設定は fallback（D7） | はい |
| `src/hal/sparcv9/bsp-sun4u/boot.c` | 名前付きの値 10（承認後、fallback の時も返す） | はい |
| `src/hal/sparcv9/bsp-sun4u/framebuffer.h` | handoff の framebuffer と fallback の値を boot.c に渡す宣言 | はい |
| `src/hal/sparcv9/cmain.c` | framebuffer の初期化の呼び出し、handoff の版の完全一致の確かめ（l.113-128）を 3 に | はい |
| `src/drivers/platform/sun4u/sun4u-firmware-fb.c`（新） | firmware framebuffer の backend | はい |
| `src/drivers/platform/sun4u/sun4u-console.c`・`.h` | kernel_putc の publish、grid と THR に直に、spinlock、suspend/resume | はい |
| `src/kern/platform/sun4u.c` | `kern_platform_debug_write` を publish 後は何もしない、ps2 の起動 | はい |
| `platform/sparcv9/vmunix.mk` | kernel の source（graphics・ps2・pcat の vgafont.c）、config の stamp の依存、root の大きさ | はい |
| `include/kern/boot.h` | 定数 2 つと定義文（承認後） | いいえ |
| `src/drivers/generic/graphics-device.c`（新）・`include/kern/graphics-backend.h`（新） | generic の前段、backend の登録と優先度、行の分割 | いいえ |
| `src/drivers/isa/ps2-8042.c`（移動）・`src/drivers/platform/pcat/ps2-8042.c`・`.h` | 共有の 8042（D5） | いいえ（pcat） |
| `platform/pcat/vmunix.mk`・`platform/amd64/vmunix.mk` | ps2 の移動の path | いいえ |
| `plan/ws081/tests/host-ps2-wheel.sh`・`host-ps2-wheel.c`・`plan/ws035/tests/refactor-refs.py`（l.63）の ps2 の path | 移動に追従（他の WS なので Q1 に依頼、参照が無ければ削除の基準を当てる） | いいえ（m10、m-5） |
| top の `Makefile` | `ZEDBSD_GRAPHICS_BACKEND` に sun4u、注記 | いいえ |
| `userland/x11/libX11/xlib.c` | property の endian（潜在の欠陥） | いいえ |
| `docs/reference/control-devices.md` | sun4u の driver を足す（l.43〜46） | いいえ |
| `plan/ws275/tests/config-sun4u-x11.mk`（新） | 試験の config | いいえ |

## 7. Phase と LW

| Phase | 中身 | 依存 | LW |
| --- | --- | --- | --- |
| p001 | この設計、design-reviewer（改訂 2 の再レビュー） | — | 1 |
| p002 | loader（display の node・property・qext・`color!`・i8042、`ofw.h` の wrapper）、boot.h と HAL の名前付きの値（fallback の時も）、HAL の早期の console（D7） | p001、HAL の承認（改訂 3 の文面）、D4・D6・D7 | 3 |
| p002b | **T1 の観察**（M-5）: gdbstub で handoff の framebuffer の値（phys・width・height・pitch・format・qext）と i8042 の port を読む。HAL の早期の console の PNG を `-g 1024x768x8`・`-g 1024x768x32`・既定の 3 条件で撮る。loader で物理の load が通るか | p002 | 1 |
| p003 | generic の前段（登録・優先度・行の分割、`graphics-device.c`・`graphics-backend.h`）と host 試験（選択: 偽の backend 2 つ、分割: 幅 1025・1366・4097 × RGB24・INDEX8・MONO1、画素の詰め方: 3 形と IE の窓の byte の逆転） | p002b、D1 | 3 |
| p004 | firmware fb の backend（`sun4u-firmware-fb.c`）、text の console と出力の道（`sun4u-console.c`）、8042 の共有と sun4u の poll・config byte、evdev、`src/kern/platform/sun4u.c`（debug_write と ps2 の起動を 1 つの Phase で）、ps2 の試験の path の追従 | p003、D2・D5・D6 | 4 |
| p005 | userland: 試験の config、Makefile・vmunix.mk（ここで初めて sun4u の kernel が p003・p004 の file を compile する。warning 0 を確かめる）、libX11 の BE、root の大きさ、色の画素と mouse の座標の決定 | p004、D6 | 2 |
| p006 | T1 の試験（sun4u の 8 bpp・32 bpp・既定の X、色の画素、mouse、回帰）、結果の判定 | p005 | 1 |
| p007 | 規約の全文の見直し（p002〜p005 の分） | p006 | 1 |

- 分け方の理由（m-6）: generic の前段は host 試験だけで確かめられるので p003 に分ける。sun4u の kernel でしか compile されない
  file（firmware fb・console・ps2・sun4u.c）は p004 に集め、`sun4u.c` を 2 つの Phase が編集しないようにした。kernel の build の
  warning 0 は p005 の vmunix.mk の後で確かめる（p003・p004 は host 試験と、可能なら host の compiler の `-fsyntax-only` まで）。
- D6 は sparcv9 の file を変える p002・p004・p005 の依存（決定済み）。p003 は sparcv9 の file を変えない。
- 外部の依存（boot-test の `QEMU_EXTRA`・`mouse:`・`BOOT_TEXT_ORIGINS`）は全部 main に入済みで、残らない。

計 16 LW（約 4.5〜6 時間）。T1 の待ちの間、担当は別の作業へ移る。

## 8. 危険と未確認（観察は T1 に依頼）

- IE の写像で VRAM に 32 bit の store を連ねた時の QEMU の VGA の振る舞い（T1: gdbstub で 1 pixel を書いて monitor の `xp`）。
- ebus の ISA の I/O が PCI の I/O の 0x60/0x64 に見えるか（T1: monitor の `info mtree`）。
- OpenBIOS の display の node が `width`・`height`・`depth`・`linebytes`・`address` を出すか、`translate`・`color!` が効くか、
  `color!` の後に palette が保たれるか、qext に残す値、loader で ASI の物理の load が通るか（**p002b の T1 の観察、p003 の依存**、M-5）。
- QEMU の既定の `graphic_depth`（8 と推測）、depth 32 への対応（p002b の 3 条件の PNG）。
- Xzed の pointer の clamp・加速、2 回の `mouse:` の click が 400 ms に入るか（p006、だめなら §5 の代わりの判定）。
- TCG の描画の速さ（全面の BLIT が boot-test の timeout に響くか）。

## 9. 本 WS の外（M3、Future Work に登録済み）

| ID | 中身 | 理由 |
| --- | --- | --- |
| F-107 | Sun Blade 150 で起動する: loader の CMD646 の限定（handoff.c l.103〜109・416）を外し ALi M5229 の IDE、USB（OHCI）の keyboard・mouse、実機の ebus の扱い | 実機は PS/2 が無く CMD646 でないと推測（未確認） |
| F-108 | sun4u の PCI の config の access（sabre・IIe）と PCI core | card の driver が BAR・mode を扱う時に要る |
| F-109 | 既知の card の driver（onboard の PGX64＝Rage XL の可能性、XVR-100・500・600）、§4.2b の口を使う | ユーザー「Sun PGX64とかはあとで対応できるように口だけ残しておきます。」 |
| F-110 | depth 24 の 3 byte の packed の framebuffer | 該当の firmware が出るまで不要 |
| F-111 | pcat・pc98 の前段を generic へ移す | §4.1 |

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
| D6 | sparcv9 の file（§6 の「はい」の 9 つ）を誰が変えるか | — | **決定（2026-10-19 19:06 ユーザー）: 「P の担当が変える」**（merge の前に差分をユーザーに見せる） |
| HAL | boot.h の 2 定数と bsp-sun4u の名前 10（proposed の差分） | — | **決定（同 19:06）: 「承認する」**。ただし改訂 3 で boot.h の定義文と名前の意味（fallback の時も返す）が変わったので、改めてユーザーに示す（ws.md の条件「文面が変われば改めて示す」） |
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
| M3 実機の依存 | 実機・sun4u の PCI・card の driver を §9 の Future Workに分けた。照合は loader が列挙した display の node に限る。card の driver は口だけ | §4.2b、§9、版 1 の p008〜p010 を外した |
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

## 13. レビュー 2 への対応

| 指摘 | 対応 | 所 |
| --- | --- | --- |
| M-1 MONO1 の chunk | chunk を copyin と展開後の buffer の両方で決める（MONO1 は packed 512 byte・4096 pixel、境界は 8 の倍数）、`packed` を `row_buffer` より大きくしない。host 試験に幅 1025・1366・4097 × 3 形式 | §4.1、§7 p003 |
| M-2 mouse の判定 | zwm の menu をやめ、zterm の double click で語を選び中の click で貼り、shell の実行の行を `expect:` で読む。代わりに PNG の目視（選択の反転・貼られた行・pointer の移動） | §5 |
| M-3 fallback で kernel が画面に描けない | 案 (i): 名前を「HAL が表示している framebuffer」にし、Bochs の fallback の時も phys＝`pci_memory_base`＋BAR0・640x400・pitch 2560・`BGRX8888` を返す。画面が無い時だけ NULL で、kernel は publish せず今の道 | §4.2、§4.4、proposed |
| M-4 byte order が判定で見えない | p003 の host 試験で 3 形の詰め方、p006 で既知の色（root 0x203040・bar 0x17212b）の画素を PNG から読んで値で比べる（8 bpp は RGB332 の量子化の期待値） | §5、§7 |
| M-5 未確認が依存に無い | p002b（T1 の観察: gdbstub で handoff の値、-g x8・x32・既定の 3 条件の PNG）を置き、p003 の依存に | §7、§8 |
| M-6 定義文の曖昧さ | 定義文に「bytes in increasing address order」、DRM の XRGB8888 と逆であること、RGB332 の bit の並びと ramp を書いた。framebuffer.c の 2 つの comment を直す | §4.4、proposed |
| m-1 quiet | quiet の record は serial に出ない（pcat と揃う）と明記 | §4.3 |
| m-2 lock の細部 | suspend の flag は spinlock の中、常に trylock・失敗なら lock 無し、IE の窓に 64 bit の store を使わない | §4.2、§4.3 |
| m-3 reveal_fatal | sun4u の text の ops に足す | §4.3 |
| m-4 形の判別の穴 | depth 15・16 は渡さない、qext の他の値は XRGB8888、`ofw.h` の wrapper と物理の load、p002 を 3 LW に | §4.2、§7 |
| m-5 §6 の漏れ・誤り | cmain.c・framebuffer.h・host-ps2-wheel.c・refactor-refs.py を足し、ws073 を外した | §6 |
| m-6 Phase の分け方 | 新しい kernel の file を p004 に集め、sun4u.c を 1 Phase に。D1 は p003、D2・D5 は p004、D6 は p002・p004・p005 の依存 | §7、ws.md |
| m-7 古い記録 | `QEMU_EXTRA`・`mouse:` は main に入済み（外部の依存を削除）、§9 を F-107〜F-111 に、F-111 の再考の契機を §4.1 に揃える依頼、master.md の l.652 | §4.1、§4.6、§5、§7、§9、ws.md |
| m-8 試験の文字列 | `tr a-z A-Z` で出力を入力と違う形にし `^WS275-OK$`、原点は (22,34) と根拠（(20,8) は失敗時の候補） | §5 |
| m-9 HAL からの引き継ぎ | publish の時に fb を消して grid の先頭から | §4.3 |
| m-10 platform の境界 | vgafont.c の compile を Guardrail の例外として Q1 に記録を依頼、F-111 で移す | §4.6 |
| m-11 PS/2 の回帰 | `keys:` を含む試験と `ps2-wheel-qemu.sh` を回帰に | §5 |
| m-12 D4 の 2 案 | D4 を p002 の前に聞く。proposed は (a) の差分だけ、(b) なら書き直して改めて示す | §4.5、proposed |
