<!-- awesome-plan project=zedbsd record=ws290-p017 -->

# ws290-p017 boot framebuffer の使用終了の通知と解放

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §13（版 4.15 の「p012 の実装」の boot framebuffer）と、この phase.md の設計の節（着手で書く）。
- 状態: planned
- 依存: p012（表示）、p011（local の allocator と `ghci_memory_boot_release`）

## ユーザーの決定（2026-10-28、原文は ws.md）

「GPUの使用開始は、consoleドライバに通知しています。consoleドライバは、/dev/gprahicsドライバを使ってGOPに描画していると思います。そこで、/dev/graphicsデバイスに使用終了の口を作るのがいいと思います。GPUの使用開始時に、/dev/consoleに使用終了を通知、/dev/consoleが/dev/graphicsに使用終了を通知、/dev/graphicsがカーネル内APIを呼んでマッピング解除を要求。カーネル内APIがHALを呼んでマッピングを実際に解除。ここまではWS290で実装。将来的に、Keilandが終了してGPU使用を終了するときに、/dev/gpu0のscanoutを/dev/consoleのバックエンドに切り替える機能を作るのがいいです。後者は今回はやらず、future workにすればいいです。」

解釈（Q1 の確かめにユーザーが「その理解でよい」）: GPU の使用開始の時に kernel の text-display（`kern_text_*`、console）へ「使用終了」を通知 → text-display が graphics の backend（amd64 は `src/drivers/platform/pcat/graphics/pcat-graphics.c`、他の platform の backend も同じ口）へ通知 → backend が kernel 内 API で framebuffer の mapping の解除を要求 → kernel 内 API が HAL で実際に解除。panic の表示は v1 で無し（serial と klog）。scanout を console の backend に戻す機能と panic の hook は Future Work。

## 範囲

1. 設計（code の前、design-reviewer の review を通す）: 今の `kern_text_suspend` の呼び手（amdrdna・i915・powervr など）との関係（suspend と「使用終了」の違い）、他の GPU driver が使わない時に今の振る舞いが変わらない事、`/dev/graphics` の firmware の backend の扱い（使用終了の後の open・mmap・描画）、kernel 内 API と HAL の呼び方（HAL の API の宣言・契約を変える要があれば差分の案を Q1 へ、承認が要る）、GHCI の driver の呼ぶ時（最初の自分の flip が取られた後、lock を持たずに）と `ghci_memory_boot_release` の条件（`boot_console` を「使用終了の通知が成功した」に置き換える）。
2. 実装: kernel の text-display、platform の graphics の backend（amd64 の pcat と、同じ口を持つ他の platform の backend）、kernel 内 API と HAL の呼び出し、GHCI の driver。
3. 試験: host 試験（GHCI の場面: 通知が成功した時だけ解放、失敗・未対応の時は解放しない）と、kernel の側の試験（ある物の範囲で）。amd64・riscv64（GHCI 有り・無し）の build の warning 0。

## 受け入れ

- 設計の review の H が 0。
- 場面 PASS、build の warning 0。他の GPU driver（amdrdna・i915・powervr）の使い方で振る舞いが変わらない事を build と既存の試験で確かめる。
- 実機（GOP の framebuffer の解除の後に console が描かない事）は p014 か Q1 の割当の QEMU で。
- 見積り: 14 LW。

## 設計

2026-10-28 q1644-i01 の P3 の helper が worktree `p3-ws290g`（head 7811b5562）の code を読んで書いた。design-reviewer の review は未（P3 が通す）。
code の事実には `file:line`（この head の行）を付けた。source は変えていない。

### 0. 要旨

- 鎖: GHCI の driver（最初の自分の flip が取られた後、lock 無し）→ `kern_text_end()`（text-display、/dev/console の顔）→ table の新しい省略可の op `end`（PC/AT は `drv_pcat_text_end()`、text 層を描かない状態に）→ `drv_pcat_graphics_end()`（/dev/graphics の node、owner の描画を断る）→ `drv_pcat_graphics_backend_end()`（firmware の framebuffer を忘れる）→ kernel 内 API `kern_device_unmap()`（既存、`src/kern/pmem.c:169-176`）→ HAL `hal_space_unmap_device()`（既存、`include/hal/hal.h:787-793`）。戻り 0 の時だけ GHCI は `ghci_memory_boot_release()` を呼ぶ。
- 「使用終了」は suspend と違い戻らない（v1）。`kern_text_resume`・`reveal`・`reveal_fatal` は終わった text 層を描かない。scan-out を console に戻すのは Future Work。
- 他の GPU driver（amdrdna・i915・powervr・bcm2711・venus）と drv_gpu の core は呼ばないので振る舞いは変わらない。`end` を持たない platform（pc98・sun4u・x68k・rpi4・serial の console の板、VF2 を含む）では `ENOTSUP` で、GHCI は範囲を今のまま保つ。
- HAL の API の差分は要らない（既存の `hal_space_unmap_device` で足りる）。HAL が early console 用に持つ恒久の窓（amd64）を消すかは判断の項目 Q1（推しは残す。消す場合の差分の案は §11）。
- user の空間に firmware の framebuffer を map する道は今無い（/dev/graphics に mmap が無く、/dev/mem も無い）。終わった後の /dev/graphics は open も ioctl も `ENODEV`。

### 1. 今の code（調べた事実）

#### 1.1 text-display（`kern_text_*`）

- `include/kern/text-display.h:70-123` の `struct kern_text_ops` は板ごとの表。`suspend`・`resume`（:96-98）は「graphics の mode が画面を持つ間」止めて再開する、戻せる操作。`snapshot`・`reveal`・`reveal_fatal`・`progress`（:100-122）は省略可（NULL）。`kern_text_register()`（:132、`src/kern/text-display.c:48-60`）は表の差し替えで、atomic に公開し lock を取らない（:11-14、:537-543）。
- `kern_text_suspend()`・`kern_text_resume()`（`src/kern/text-display.c:367-406`）は表の op を呼ぶだけで、数え（参照の数）を持たない。
- `kern_text_kernel_putc()`（:125-142）が `kernel_putc`（`include/hal/hal.h:1541-1548`、`src/kern/entry.c:498`）として公開される。quiet の間は log へ、他は `kern_text_putc()`。
- 表を持つ板（全部 designated initializer）: PC/AT `src/drivers/platform/pcat/graphics/text.c:462-477`（amd64・i386 で共有）、PC-98 `src/drivers/platform/pc98/graphics/text.c:82`、sun4u `src/drivers/platform/sun4u/sun4u-console.c:114`、x68k `src/drivers/platform/x68k/x68k-console.c:43`、rpi4 `src/drivers/platform/rpi4/rpi4-console.c:92`、serial の板 `src/drivers/generic/serial-console.c:59`（VF2 はこれを写して `reveal_fatal` を足す、`src/kern/platform/visionfive2.c:342-348`）。

#### 1.2 PC/AT の /dev/graphics（3 つの file）

- `src/drivers/platform/pcat/graphics/pcat-graphics.c` が /dev/graphics の node そのもの（`drv_graphics_device_register`、:68-88、`cdev_register("graphics", …)` :84）。`include/kern/graphics-backend.h` の `struct drv_graphics_backend` は使わない（それは generic の front end `src/drivers/generic/graphics-device.c` 用で、build は sparcv9 だけ、`platform/sparcv9/vmunix.mk:93-94`。PC-98 は pcat と同じ形の別の file `src/drivers/platform/pc98/graphics/pc98-graphics.c:68`）。どの front end を作るかは `Makefile:590-596`（pcat・amd64 → pcat、pc98 → pc98、sparcv9 → sun4u、他の platform は /dev/graphics 無し。riscv64 の VF2 にも無い）。
- cdev の op は open・close・ioctl だけ（`pcat-graphics.c:59-63`）。mmap は無い（generic も `graphics-device.c:128-132`、pc98 も `pc98-graphics.c:58-62`）。描画は全て ioctl で、kernel が backend の mapping に書く。
- lock: sleep する mutex `graphics_lock`（LOCK_RANK_DEVICE、:40-41、:119-129）。open は 1 つだけ（`graphics_owner`、:132-159）。ioctl は owner 以外 `EBADF`（:680-681）、ENTER の後でないと `ENXIO`（:187-193）。ENTER は `graphics_lock` の下で `kern_text_suspend()`（:239）、失敗・close・`drv_graphics_device_restore_text()`（:93-116）で `kern_text_resume()`（:99、:113、:173、:244、:261）。順は graphics_lock → text_lock。
- backend `src/drivers/platform/pcat/graphics/backend.c`: 起動の handoff の framebuffer を `kern_boot_framebuffer("pcat.framebuffer", …)`（:1050、`src/kern/pmem.c:231-` が HAL の名前付きの値 `pcat.framebuffer.*` を読む、`src/hal/amd64/bsp-pcat/boot.c:76-81`）で読み、`hal_space_map_device(phys, size, READ|WRITE|NOCACHE)`（:1062-1063）で kernel に map して `linear_pixels`（:57）に持つ。map の後 `text_console_start()`（:1070、:1123-1143）が text 層を作り、text が描ける時 `kernel_putc` を `kern_text_kernel_putc` にする（:1133-1139）。`drv_pcat_graphics_backend_ready()` は `backend_prepared`（:563-569）。`leave` は linear なら状態だけ（:226-233）、Cirrus・VGA は VGA の register を書く（:236-247）。
- `drv_pcat_graphics_framebuffer_moved()`（:1205-1234）は PCI の BAR が動いた時に新しい所を map し、古い mapping は「書き手が持つかも」として残す（:1198-1204）。呼ぶのは `src/drivers/pci/pci-pcat-assign.c:742-746`（BAR の拡大、`drv_pci_pcat_grow_bar` の呼び手は `src/drivers/gpu/amdrdna/gmc.c:1123`）で、起動の `kern_platform_init` の PCI の attach と deferred の割当（`src/kern/platform/pcat.c:392-430`）の中。backend の `drv_pcat_graphics_prepare()` はその後（:480）。
- text 層 `src/drivers/platform/pcat/graphics/text.c`: spinlock `text_lock`（LOCK_RANK_CONSOLE_TEXT、:81、:492）。backend の mapping を `drv_pcat_graphics_backend_get_framebuffer()`（`backend.c:533-561`）で受けて `text_pixels` に持つ（:499）。framebuffer が無ければ VGA の text memory（:563-590、`kern_device_map(0xa0000, 128 KiB)` の +0x18000）。live の surface に書くのは `draw_cell_locked()`（:283 で `text_ready` を見る）・`reveal`（:907-912）・`reveal_fatal`（:971-978）だけで、全部 `text_ready` を条件にする。`text_ready` を 1 にするのは init（:535）・`resume`（:843）・`reveal_fatal`（:969）・`snapshot`（:217、終わりに戻す :224-230）。`putc_locked()` は先に serial の mirror に書き（:408）、`!text_ready` なら cell を変えずに戻る（:411-412）。
- `reveal_fatal`（:936-991）は panic で、`text_lock` をこの CPU が持つか TEXT_FATAL_LOCK_SPINS（:950）の間取れなければ lock 無しで、suspend された surface にも `text_ready = 1` にして全画面を消して描く（BUG-158 の対処）。
- quiet boot の splash（`splash.c`）は独自の pointer `splash.pixels` に描く。HAL の窓で始まり（`src/hal/amd64/bsp-pcat/cons.c:383`）、text 層の init で kernel の mapping に移る（`text.c:544`、`splash.c:225-235`）。`drv_pcat_splash_stop()`（`splash.c:267-272`）で止まる。ticker の thread（`text.c:1016-1050`）は `text_hidden && !text_splash_ended` の間だけ回る。
- `snapshot`（`text.c:138-233`）は RAM へ描き、live の surface を読まない。`text_surface == NONE` なら `ENODEV`（:161-166）。使うのは venus の display（`src/drivers/gpu/venus/display.c:531`、:619、:661）。

#### 1.3 firmware の framebuffer の mapping（amd64）

- kernel の mapping: 上の `linear_pixels`（`backend.c:1062`）。`hal_space_map_device` は `device_fixed_range()`（`src/hal/amd64/space.c:2737-2771`）の固定の窓（0xa0000〜、0xf0000000〜0xf1000000、LAPIC・IOAPIC）に入れば恒久の窓を返し、他は参照の数を持つ動的な view（:350-360、`device_window_map` :2775-）。
- 解除: `hal_space_unmap_device()`（`space.c:366-410`）。固定の窓なら何もせず `HAL_OK`（:398-401）。動的な view は参照を減らし、0 で PTE を消して全 CPU の TLB を shootdown（`device_window_unmap` :3018-3057、shootdown :3052、`shootdown()` :2236- は自分で割り込みを止めて ack を待つ）。kernel の側の口は `kern_device_map()`・`kern_device_unmap()`（`include/kern/pmem.h:68-76`、`src/kern/pmem.c:145-176`。`KERN_DEVICE_UNCACHED` は `HAL_SPACE_READ|WRITE|NOCACHE` で backend の今の属性と同じ、:154-161）。
- HAL の early console の窓: loader の page table と HAL の system の page table が framebuffer を `0xffffffffc2000000`（`bootloader/include/amd64-handoff.h:128`）に恒久に map する（`src/hal/amd64/space.c:586-593`、`framebuffer-map.c`、global・NX・UC）。HAL の console（`cons.c:349-385`）と locore（`src/hal/amd64/locore.S:40`）が使う。`hal_putc()` は `kernel_putc` が公開された後は委ねて画面に触らない（`include/hal/hal.h:81-86`、`cons.c:158-175`）。HAL の `hal_printf`・`hal_fatal` も `hal_putc` を通る（`src/hal/amd64/lib.c:175-183`、:395-397）。この窓を消す HAL の API は無い（`hal_space_unmap_device` は動的な view と固定の 4 つだけを知る、:384-404）。
- 物理: UMA・RAM の framebuffer は HAL が allocator から外す（`src/hal/amd64/page.c:187-189`、:831-833）。discrete の GHCI では boot framebuffer は local memory（BAR2 の window の向こう）で、GHCI の allocator が持つ（p011、`src/drivers/gpu/ghci/memory.c:562-610`）。

#### 1.4 user の mapping

- /dev/graphics の 3 つの front end に mmap は無い（1.2）。`src/drivers/generic/memory-device.c:92-130` は null・zero・full・random・urandom だけで /dev/mem は無い。cdev の mmap（`include/kern/cdev.h:48`、`src/kern/cdev.c:72`）を持つのは /dev/gpu（`src/drivers/gpu/gpu.c:1312`）と audio（`src/drivers/audio/audio.c:595`）。GHCI は client の local の read・write・map を断る（p011 の L1）。よって firmware の framebuffer の user の mapping は今無く、取り上げる物は ioctl の描画だけ。
- /dev/graphics の user: `userland/x11/xzed/main.c`・`zterm/main.c`・`zwm/main.c`（X11）。

#### 1.5 `kern_text_suspend` の呼び手と期待

| 呼び手 | 止める | 戻す | 期待 |
| --- | --- | --- | --- |
| pcat /dev/graphics の ENTER | `pcat-graphics.c:239` | :99・:113・:173・:244・:261 | ENTER の間だけ描かない。leave・close で再描画 |
| pc98 /dev/graphics | `pc98-graphics.c:238` | :98・:112・:172・:243・:260 | 同上 |
| generic /dev/graphics（sun4u） | `graphics-device.c:386` | :224・:236・:292・:389・:405 | 同上 |
| amdrdna の lease の frame | `src/drivers/gpu/amdrdna/present.c:1383`（node の display lock の下） | :1404 | lease の frame の間描かない、終わりで console の surface に戻す |
| amdrdna の VRAM の training | `src/drivers/gpu/amdrdna/training.c:249` | :286 | 低い VRAM を上書きする間だけ |
| powervr の lease | `src/drivers/gpu/powervr/display/display.c:927`（mutex の下） | :1602 | lease の間 |
| i915 bootfb の lease | `src/drivers/gpu/i915/display/bootfb.c:834`（mutex の下） | :1471 | lease の間 |
| bcm2711 の lease | `src/drivers/gpu/bcm2711/display/display-device.c:816` | :454・:884 | lease の間 |

- どれも「戻せる・数えない」前提で、自分の mutex の下で呼ぶ（text の lock は spinlock なので可）。drv_gpu の core は `GPU_DISPLAY_CLAIM` で `tty_console_input_hold()` と `kern_text_progress_end()` を呼ぶ（`src/drivers/gpu/gpu.c:3841-3847`）。ユーザーの言う「GPU の使用開始の通知」はこれ。GHCI の driver は `kern_text_*` を今呼ばない。

#### 1.6 panic と klog

- `__libc_panic`・`kern_fatal`（`src/kern/panic.c:51-81`、:86-117）は ring に記録（`kern_log_record_fatal`）してから `kern_text_reveal_fatal()`（:71、:114）、その後 `hal_fatal`（`hal_putc` → `kernel_putc`）。`src/kern/user-probe.c:281` も reveal_fatal。
- klog は ring に足し、`kernel_putc` に写し（quiet で無ければ）、debug の port に書く（`src/kern/klog.c:110-151`）。PC/AT の text 層の `putc_locked` は serial の mirror に先に書く（`text.c:408`）。よって text 層が描かなくても serial と klog の ring・debug port には出る。

#### 1.7 GHCI の今

- `boot_take()`（`src/drivers/gpu/ghci/display-scan.c:1226-1296`）が boot display を取り `boot_scanning = boot_kept = boot_console = 1`（:1293-1295）。意味は `ghci.h:565-571`、field は :637-641。
- `taken()`（:1457-）の中で最初の自分の image が取られると `boot_scanning = 0`、`boot_kept && !boot_console` の時だけ `boot_release = 1`（:1501-1507）。今は `boot_console` が 0 にならないので解放されない（p012 の「残り」1、design §13 版 4.15）。
- `scan_flush()`（:2508-2581）: service（:494、`service = 1`）は lock を try だけで解放はしない。他（`ghci_scan_flush`、:1014-1019、呼び手は `display.c:391`・:517・:753・:1113・:1188・:1236 と detach の `display-scan.c:387`）は `device.lock`・`display_lock` を取り（:2533-2534）、`boot_release` を取り（:2557-2561）、全部を離した後（:2575）lock 無しで `ghci_memory_boot_release()`（:2578-2580）。
- `ghci_memory_boot_release()`（`memory.c:562-610`）は lock を持たずに呼ぶ約束（:568-570）。範囲を local の allocator に返し、window の使い手を減らす。
- detach・停止は `boot_kept` の間 boot の image を点け直す（`display-scan.c:2136-2141`、host の手で :356-364）。panic の `ghci_display_panic`（:659-680）は `boot_kept` の間だけ boot の image を点ける（v1 は呼ぶ物が無い）。
- host 試験 `plan/ws290/tests/ghci-driver-host-test.c:3399-3455`（`scene_display_boot`）は解放の道を `device.display.boot_console = 0`（:3443）を直に置いて試している。stub は持っている lock の名前を数える（`plan/ws290/tests/ghci-stubs.c:54-55`、:219-265）。

### 2. 責務と境界

| 層 | file | 責務（使用終了） | しない事 |
| --- | --- | --- | --- |
| GHCI の driver | `src/drivers/gpu/ghci/display-scan.c`・`ghci.h` | 返す boot の範囲がある時、最初の自分の flip が取られた後に 1 度だけ lock 無しで `kern_text_end()` を呼び、0 なら範囲を返す | console・/dev/graphics・HAL に直に触らない |
| text-display（/dev/console の顔） | `include/kern/text-display.h`・`src/kern/text-display.c` | 表の `end` を呼び、無ければ `ENOTSUP`。snapshot の消費者に知らせる | 板の状態を持たない |
| PC/AT の text 層 | `src/drivers/platform/pcat/graphics/text.c`・`text.h` | 戻らない「終わった」状態にし、live の surface に二度と書かない（resume・reveal・reveal_fatal・splash も）。cell は保つ。続けて /dev/graphics に知らせる | mapping を消さない |
| PC/AT の /dev/graphics | `src/drivers/platform/pcat/graphics/pcat-graphics.c` | `graphics_lock` の下で ENTER の状態を捨て、以後の open・ioctl を `ENODEV`。backend に終わりを渡す。結果を覚えて 2 度目に返す | hardware に書かない |
| PC/AT の backend | `src/drivers/platform/pcat/graphics/backend.c`・`backend.h` | `linear_pixels` を NULL にし、kernel の mapping（今の物と、BAR の移動で残した古い物）を `kern_device_unmap()` で消す。ready を 0 に | VGA・Cirrus の register を書かない、legacy の窓は消さない |
| kernel 内 API | `src/kern/pmem.c`（既存の `kern_device_unmap`） | HAL を呼ぶ | 変えない |
| HAL | `hal_space_unmap_device`（既存） | PTE を消し TLB を shootdown | 変えない |

- ユーザーの言う「/dev/console」は `kern_text_*`（/dev/console の `src/drivers/generic/console.c` は `kern_text_*` の上の多重化で、画面を持つのは表）、「/dev/graphics」は PC/AT では text 層を含む `src/drivers/platform/pcat/graphics/` の driver（text 層は `text.h:10-14` の通り /dev/graphics が持つ）。

### 3. interface

#### 3.1 公開（include/kern、HAL ではない）

`include/kern/text-display.h` の `struct kern_text_ops` の末尾（`progress` の後）に省略可の op を足し、関数を 1 つ足す。全部の表が designated initializer なので（1.1）他の板は NULL のまま build が通る。

```c
	/*
	 * Optionally ends the console's use of the display for good.  From
	 * the return on nothing of the console (its text, a quiet boot's
	 * splash, a graphics mode the board's /dev/graphics entered, the
	 * reveal of a fatal stop) writes to the display's memory or keeps a
	 * kernel mapping of it; the console's characters still reach its
	 * other outputs (a serial line, the log).  The retained cells and
	 * snapshot stay.  Returns 0, or the error of a mapping that could not
	 * be removed (the console stays off the display either way).  A
	 * second call returns the first result.  Called in thread context
	 * with no spinlock held; it may sleep.
	 */
	int (*end)(void);
```

```c
/*
 * Ends the console's use of the display for good, for a display driver
 * that is about to give the display's memory to others.
 *
 * Returns 0 once nothing of the console writes to or maps the display,
 * ENOTSUP when no console is published or the board's console cannot
 * leave the display, EBUSY when the hardware layer still draws the early
 * console, or the error of a mapping that could not be removed.  The
 * caller holds no lock; the call may sleep.  There is no way back.
 */
int kern_text_end(void);
```

`src/kern/text-display.c`: `ops()` が NULL か `end` が NULL なら `ENOTSUP`。他は `table->end()` を呼び、`text_changed()`（:546-571）で snapshot の消費者を起こしてから結果を返す。lock は取らない（今の他の関数と同じ）。

#### 3.2 PC/AT の内部

- `text.h`: `int drv_pcat_text_end(void);`（表の `.end`）。
- `backend.h`: `int drv_pcat_graphics_end(void);`（/dev/graphics の node の分、`pcat-graphics.c` に置く）と `int drv_pcat_graphics_backend_end(void);`（`backend.c`、`graphics_lock` の下で呼ぶ）。backend.h は既に text 層と backend の間の口（`get_framebuffer`・`framebuffer_moved`、:51-73）を持つので同じ所に置く。
- 新しい状態: `text.c` `static int text_ended;`（atomic、`reveal_fatal` が lock 無しで読む）。`pcat-graphics.c` `static int graphics_ended; static int graphics_end_error;`（`graphics_lock` の下）。`backend.c` `static int backend_ended;` と、BAR の移動で残した古い mapping の表 `linear_retired[PCAT_RETIRED_MAX]`（4 個、大きさは `linear_bytes`）と数。
- backend の map を `hal_space_map_device(…, READ|WRITE|NOCACHE)`（`backend.c:1062`、:1222）から `kern_device_map(…, KERN_DEVICE_UNCACHED, …)` に替える（HAL の属性は同じ、1.3）。解除と対にして、ユーザーの決定の「kernel 内 API → HAL」の形にするため。

#### 3.3 GHCI

- `ghci.h` の `struct ghci_display`: `boot_console` の意味を「kernel の console がまだ描き得る（使用終了の通知が成功していない）」にし、`int boot_end_due;`（通知が要る）と `int boot_end_tried;`（1 度だけ）を足す。comment（:565-571）を直す。
- `display-scan.c` に static の `boot_end(struct ghci_device *device)`（lock 無しで呼ぶ。`kern_text_end()` を呼び、0 なら範囲を返す）。`#include <kern/text-display.h>`。
- test の stub（`plan/ws290/tests/ghci-stubs.c`・`.h`）: `int kern_text_end(void)`（結果を置ける、呼ばれた数、呼ばれた時に lock を 1 つも持っていない事を `expect`）。

### 4. 状態遷移

PC/AT の text 層（`text_ended` は終端）:

```
(無) --init--> READY --suspend--> SUSPENDED --resume--> READY
  READY・SUSPENDED・HIDDEN(quiet) --end--> ENDED
  ENDED: text_ready = 0、text_pixels = text_vram = NULL、splash 停止、cell・geometry・surface の種類は保つ
         suspend・resume・reveal・reveal_fatal は live の surface に書かない、snapshot は RAM に描く
```

/dev/graphics（PC/AT）:

```
IDLE --open--> OWNED --ENTER--> ENTERED --close/leave--> IDLE
  どれでも --end--> ENDED: open・ioctl は ENODEV、close は owner を外すだけ（resume しない）、restore_text は何もしない
```

backend: `PREPARED（linear を map）--end--> ENDED（ready 0、linear NULL、mapping 解除済み、framebuffer_moved は何もしない）`。

GHCI の boot display:

```
boot_take: scanning = kept = console = 1, end_due = end_tried = 0
taken（最初の自分の image）: scanning = 0
   kept && !console          → release = 1（今と同じ）
   kept && console && !tried → end_due = 1
scan_flush（service 以外、device.lock と display_lock の下）:
   end_due && present && boot_reserved → end = 1, end_due = 0, end_tried = 1
   end_due && !boot_reserved（UMA など、返す物が無い）→ end_due = 0（呼ばない、今と同じ）
   全 lock を離した後 end なら boot_end():
     kern_text_end() == 0 → lock を取り直し console = 0、kept && !scanning && present なら kept = 0 で範囲を返す
     != 0                → 範囲は保つ（今と同じ）、log 1 行
```

### 5. 資源の所有と寿命

- firmware の framebuffer の物理: UMA は HAL が永久に予約（1.3）。discrete は GHCI の local memory で、`boot_reserved` の間は allocator の外。`kern_text_end()` が 0 を返し、さらに最初の自分の flip が取られた後にだけ allocator に戻る。
- kernel の mapping: backend が prepare から end まで持つ（今の物と `linear_retired[]`）。end で全部 `kern_device_unmap`。BAR の移動で残す古い mapping は、今は永久に残る（`backend.c:1201-1202`）が、表に入れて end で消す（表が満ちたら今と同じく残し log）。固定の窓に入る framebuffer は HAL が恒久に持ち、解除は HAL_OK で何も変わらない（`space.c:398-401`）。kernel の pointer は全部 NULL にするので kernel は使わない。
- HAL の early console の窓（`0xffffffffc2000000`）: HAL が永久に持つ（Q1）。
- text の cell・geometry: text 層が永久に持つ（Future Work の scan-out の戻しと venus の snapshot のため）。end の後の `putc` は今の suspend と同じく cell を変えない（`text.c:411-412`）。
- /dev/graphics の file: owner は close まで残るが、何もできない。

### 6. 並行性と lock

- 呼ぶ文脈: thread、割り込み可、spinlock 無し、sleep してよい（`graphics_lock` の mutex と shootdown の ack の待ち）。割り込み・panic・service thread からは呼ばない。
- 順: `drv_pcat_text_end()` は `text_lock`（spin、137）を取り「終わり」を書いて離す → `drv_pcat_graphics_end()` が `graphics_lock`（mutex、125）を取る → `kern_device_unmap` → HAL の registry の lock と shootdown → `graphics_lock` を離す。text_lock を持ったまま graphics_lock を取らないので、今の順（ENTER の graphics_lock → text_lock、`pcat-graphics.c:239`）と逆にならない。kern_logf は全部の lock を離してから。
- text の書き手（putc・write・clear・cursor・ticker・reveal・progress）は全部 `text_lock` の下で `text_ready` を見る（1.2）。end は lock の下でまず `text_ended` を atomic に 1（release）にしてから `text_ready = 0`・pointer NULL・splash 停止・`text_splash_ended = 1`。lock を離した時点で以後の書き手は書かない。
- `reveal_fatal` の lock 無しの道: 最初に `text_ended` を acquire で読み、1 なら `text_ready` を 1 にせず全画面の消去も描画もしない（splash の停止と `text_hidden = 0` は今まで通り）。panic の CPU が `text_ended` を 0 と読んで描き始め、同時に別の CPU が end を進める狭い窓では、end の CPU の shootdown が panic の CPU（割り込みを止めている）の ack を待ち続けるので解除も GHCI の解放も起きない（memory の破壊は無い、機械は止まる）。受け入れる残り（§8）。
- /dev/graphics の描画（ioctl）は全部 `graphics_lock` の下で `linear_pixels` を使う（1.2）。end は同じ lock の下で ENTER を捨て `graphics_ended = 1`、backend が pointer を NULL にしてから unmap。lock を離した後の ioctl は `ENODEV`。
- `framebuffer_moved` は lock を持たない（`backend.c:1205-1234`）。呼ぶのは起動の PCI の attach の中（1.2）で、GHCI の最初の flip（user の present の後）と重ならない。念のため冒頭で `backend_ended` を見て何もしない。重なりを防ぐ lock は足さない（`graphics_lock` はその時まだ初期化されていない事がある、`pcat-graphics.c:118-129`）。
- 2 つの GHCI device が同時に呼ぶ: text の end は lock の下で冪等、/dev/graphics の end は `graphics_lock` の下で冪等で最初の結果を返す。
- GHCI: `boot_end()` は `scan_flush` が `device.lock`・`display_lock` を離した後（`display-scan.c:2575` の後）で呼ぶ。`end_due` の取り出しと `end_tried` の印は lock の下なので 1 度だけ。結果を戻す時に `device.lock` → `display_lock` を今と同じ順に取り直す。service（`scan_flush(device, 1)`）は呼ばない（今の解放と同じ。service は眠らない、p012 の H1）。

### 7. 割り込み・DMA・panic

- 割り込みの文脈の変更は無い。`kernel_putc` の道（割り込み・panic から呼ばれ眠らない、`hal.h:1541-1548`）は今と同じ spinlock だけ。
- DMA: boot の範囲を返すのは、表示の controller が自分の image を走査し始めた（最初の flip が取られた）後だけ（今の条件、`display-scan.c:1501-1507`）。console の CPU の書き込みは end の shootdown の完了で止まっている。
- panic（v1 は表示無し）: end の後の `kern_fatal` → `reveal_fatal` は framebuffer に触らない（§6）→ `hal_fatal` → `kernel_putc` → text 層の `putc` は serial の mirror だけ。ring と debug port にも残る（1.6）。`ghci_display_panic` は呼ぶ物が無いまま（Future Work）。

### 8. 失敗と回復

| 場面 | `kern_text_end()` | console | GHCI |
| --- | --- | --- | --- |
| PC/AT、kernel の text が `kernel_putc` を持つ | 0 | 終わり（serial・klog だけ） | 範囲を返す、log「boot framebuffer given back」 |
| PC/AT、`kernel_putc` が `kern_text_kernel_putc` でない（HAL がまだ early console を描く。text の init が失敗した等） | `EBUSY`、何も変えない | 今のまま | 範囲を保つ、log 1 行 |
| PC/AT、unmap の失敗（HAL_ERR_INVALID 等、`pmem.c` の errno） | その errno（2 度目も同じ） | 終わり（描かない、mapping は残り得る） | 範囲を保つ |
| PC/AT、framebuffer が VGA の text memory（linear 無し） | 0（text を終え、legacy の窓は消さない） | 終わり | 範囲を返す（GHCI の範囲を console が map していない） |
| 表が無い・`end` の無い板（pc98・sun4u・x68k・rpi4・serial の板、VF2） | `ENOTSUP` | 今のまま | 範囲を保つ（今と同じ） |
| GHCI が UMA（`boot_reserved` 0） | 呼ばない | 今のまま | 今のまま |
| flip が取られる前の detach・LOST の後の detach | 呼ばない（`present` 0） | 今のまま | detach の今の扱い |
| end が 0 の後、detach が boot の image を点け直した（`boot_scanning` 1） | — | 終わり | 範囲は返さず、detach が allocator ごと捨てる（phase011 の「解放の前の detach」） |

- 回復は無い（戻らない）。GHCI は 1 度だけ試し（`end_tried`）、失敗なら boot の範囲を今と同じに保つ（ユーザーの決定の「通知を断られたら範囲を保つ」）。

### 9. GHCI の側

- `boot_console = 1` の置き換え: `boot_take()` は今まで通り 1 にするが、0 にするのは `kern_text_end()` が 0 を返した時だけ（試験の `boot_console = 0` の直書きは stub の結果 0 に替える）。
- 呼ぶ時: `taken()` で最初の自分の image が取られ、boot の範囲が allocator の外にある（discrete、`device->boot_reserved`）時に `end_due`。実際の呼び出しは次の service 以外の `ghci_scan_flush()` の終わりで、lock 無し。その後 `ghci_memory_boot_release()`（lock 無し、`memory.c:568-570` の約束通り）。
- UMA は呼ばない: 返す範囲が無く（HAL が永久に予約）、console を止める利点が無いので今の振る舞いを保つ（Q3）。
- detach・停止・panic の `boot_kept` の条件（`display-scan.c:356`・:667・:2136）は変えない。end が成功すれば同じ lock の中で `boot_kept = 0` にするので「終わった console の image を点け直す」窓は §8 の最後の行の detach だけで、点くのは古い console の image（害は無い）。
- log: 成功「ghci: %s: boot framebuffer given back」（今の文、`display-scan.c:2580`）、失敗「ghci: %s: boot framebuffer kept: the console cannot leave it (%d)」。
- 出典の block の comment（`Procedure referenced: amaterrace-gpu d9252e4 docs/ghci.md (section 13.3 …)`、`display-scan.c:2515-2516`）はそのまま。

### 10. platform ごと

| platform | text の表 | end | 備考 |
| --- | --- | --- | --- |
| amd64（pcat） | `pcat_text_ops` | 実装 | GHCI 有り（`plan/ws290/tests/config-amd64-ghci.mk`） |
| i386（pcat） | 同じ file | 実装（同じ code） | GHCI 無し、build だけ |
| riscv64 VF2 | serial の写し + `reveal_fatal`（`visionfive2.c:342-348`） | 無し → `ENOTSUP` | HAL が firmware の framebuffer（SoC の DRAM）に永久に描く（`serial-console.c:8-14`、`kernel_putc` は公開されない）。GHCI は範囲を保つ（Q4） |
| pc98 | `pc98_text_ops` | 無し | GHCI 無し |
| sparcv9 sun4u | `sun4u_console_ops` | 無し | ユーザーの物、触らない |
| x68k・rpi4・他の serial の板 | 各表 | 無し | GPU が呼ばない |

- firmware の framebuffer の無い platform: 表が serial なら `ENOTSUP`（範囲を保つ。v1 は安全側）、PC/AT で linear が無ければ VGA の text を終えて 0。

### 11. HAL

- **HAL の API の差分は不要（推し、案 A）**。理由: (1) kernel の mapping の解除は既存の `hal_space_unmap_device()`（宣言 `include/hal/hal.h:787-793`、amd64 の実装 `space.c:366-410`）で足り、kernel 内 API は既存の `kern_device_unmap()`（`pmem.c:169-176`）。(2) 新しい呼び方は既存の契約の中（`hal_space_map_device` で作った物を同じ大きさで外す）。(3) HAL の early console の窓は `hal.h:81-86` の契約で handover の後は HAL が触らず、全部の HAL の出力が `kernel_putc` を通る（1.3）。supervisor だけの UC の mapping で、user から見えず投機の read もしない。
- 残り（案 A で受け入れる物）: amd64 の HAL の恒久の窓は GOP の物理を map したまま残る（kernel の誤った write が当たれば GHCI が渡した local memory を壊し得る、という kernel の bug の時だけの危険）。固定の窓（0xf0000000〜0xf1000000 等）に入る framebuffer は `hal_space_unmap_device` が何もしない。
- 案 B（推しではない、承認の項目 Q1）: HAL の early console の窓を消す関数を足す。hal.h の差分の案（全部の port に実装が要る。amd64 は `system_mmio_pd` の framebuffer の entry と edge の table を消して shootdown、他の port は HAL_OK を返すだけ。+3 LW）:

```diff
--- a/include/hal/hal.h
+++ b/include/hal/hal.h
@@ -87,6 +87,19 @@
 void
 hal_putc(
 	int c);
 
+/*
+ * Give up the early console's own view of the boot display.
+ *
+ * The kernel calls this once kernel_putc is published and nothing of the
+ * kernel draws to the boot framebuffer any more.  The HAL removes every
+ * mapping it made for its early console, completes the TLB
+ * synchronization, and never touches that display again, hal_fatal()
+ * included.  A port whose early console has no such mapping returns
+ * HAL_OK.  Thread context; the caller holds no spinlock.
+ */
+int
+hal_console_release_display(void);
+
 int
 hal_puts(
 	const char *s);
```

  案 B を選ぶ時は、PC/AT の backend の end が `kern_device_unmap` の後に kernel 内の口（例 `kern_console_release_display()`、`src/kern/pmem.c` か `src/kern/text-display.c`）を通して呼ぶ。承認の前は `plan/ws290/proposed/` に置く（今は置いていない。Q1 の答えで P3 が置く）。

### 12. 既存の framework との関係

- drv_gpu の core（`gpu.c`）は変えない。`GPU_DISPLAY_CLAIM` の通知（1.5）はそのまま。
- `kern_text_suspend`・`resume` の意味は変えない。end を呼ぶのは GHCI だけなので、amdrdna・i915・powervr・bcm2711・venus・pcat/pc98/generic の /dev/graphics の今の使い方は同じ道を通る。end の後に他の driver が resume を呼んでも何も描かない（PC/AT の同じ機械で GHCI と他の GPU が表示を取り合う構成は v1 で扱わない）。
- cdev: /dev/graphics の node は消さない（file を開いた者がいるかもしれないため）。`ENODEV` で断る。
- `include/kern/graphics-backend.h`（generic の front end）の struct は変えない（使う platform の sparcv9 に GPU が無く、ユーザーの物でもある）。`:21-28` の「card の driver の乗っ取りは boot の時だけ、firmware の mapping は返さない」の記述もそのまま。
- PCI: `framebuffer_moved` の古い mapping を end で消すようにするだけ。

### 13. 試験の計画

#### 13.1 host（GHCI）: `plan/ws290/tests/ghci-driver-host-test.c`・`ghci-stubs.c`・`ghci-stubs.h`

- stub `kern_text_end()`: 結果（既定 `ENOTSUP`）、呼ばれた数、呼ばれた時の持っている lock の数が 0 を `expect`。
- 場面（`scene_display_boot` を直し `scene_display_boot_end` を足す）:
  1. discrete、通知 0: flip が置かれただけでは呼ばない、取られた後の flush で 1 回、lock 無し → `boot_kept` 0・`boot_reserved` 0・`boot_count` 0・`window_users` 0。次の flush で 2 回目を呼ばない。detach で dark。
  2. discrete、通知 `ENOTSUP`・`EBUSY`・`EINVAL`: 1 回だけ呼び、範囲を保つ（`boot_reserved`・window 開・`boot_kept`）。次の flush で呼ばない。detach で boot の image を点け直す（今の期待と同じ）。
  3. UMA の boot display: 呼ばない（数 0）、今の期待のまま。
  4. service の flush（`scan_flush(device, 1)` の道）だけでは呼ばない。
  5. 取られる前の detach: 呼ばない。
  6. 2 つの device: 両方 1 回ずつ。
- 既存の `ghci-host-test.sh` の全部（words・ref・model・driver・vk・pipeline・execute・meta・gsi1）が PASS。

#### 13.2 host（kernel の側）: 新しい `plan/ws290/tests/text-end-host-test.c`・`text-end-stubs.c`・`text-end-host-test.sh`

- 本物の `src/kern/text-display.c`・`src/drivers/platform/pcat/graphics/{text.c,pcat-graphics.c,backend.c,font.c,vgafont.c,splash.c}` を host で compile し、stub（spinlock・mutex・cdev_register・copyin/copyout・`kern_device_map`/`unmap`（RAM の配列を返し解除を記録、失敗を注入）・`kern_boot_framebuffer`（偽の framebuffer）・`drv_pci_*`（Cirrus 無し）・`kern_io_in8/out8`（記録）・`kern_logf`・`kthread_create`・`hal_cpu_current`・`hal_rtc_read_counter`・`drv_pcat_serial_mirror`（記録）・`kern_log_quiet`）。backend.c の依存が重すぎれば backend の stub に替え、`drv_pcat_graphics_backend_end` だけ別の小さな harness で試す（実装の段で判断し記録）。
- 場面: (a) end 無し: putc が偽の framebuffer に描く、suspend・resume が戻せる（今の振る舞い）。(b) end: 0、`kern_device_unmap` が map した番地と大きさで 1 回。後の putc は serial の記録に出て framebuffer の checksum は変わらない。resume・reveal・reveal_fatal（lock 無しの道も）が framebuffer に書かない。snapshot は geometry と image を返す。2 回目の end は 0 で unmap を呼ばない。(c) /dev/graphics: open・ENTER の後に end → FILL が `ENODEV` で framebuffer 不変、close は 0 で resume しない、end の後の open は `ENODEV`、`drv_graphics_device_restore_text` は何もしない。(d) unmap の失敗の注入: end がその errno、2 回目も同じ errno で unmap を再び呼ばない、console は描かない。(e) 表無し・`end` 無しの表 → `ENOTSUP`。(f) linear 無し（VGA の text）: 0、legacy の窓を unmap しない、VGA の port に書かない。(g) `kernel_putc` が別の物 → `EBUSY` で何も変わらない。(h) BAR の移動の後: 古い mapping と新しい mapping を両方 unmap。
- 既存の回帰: `plan/ws014/tests/run-text-notification-test.sh`（`text-display.c` を末尾の field 付きで compile）、`plan/ws275/tests/host-graphics-device.sh`（generic の front end、header の変更の影響が無い事）、i915 の `plan/ws237/tests/run-gen9-*-host-test.sh`（`kern_text_suspend` の stub、不変）。

#### 13.3 build（warning 0）

`plan/ws290/tests/config-amd64-ghci.mk`、`plan/ws290/tests/config-visionfive2-ghci.mk`、`config/ci/config-amd64.mk`（i915・amdrdna 有り）、`config/ci/config-pcat.mk`（i386 が pcat を共有）、`config/ci/config-visionfive2.mk`（powervr）、`config/ci/config-pc98.mk`、`config/ci/config-rpi4.mk`（bcm2711・rpi4 の表）。sparcv9 は Q1 の判断（変更は無い、header だけ）。

#### 13.4 QEMU（Q1 が割り当てる。1 本の script `plan/ws290/tests/text-end-qemu.sh`）

- 回帰（end を呼ばない build）: `config/ci/config-amd64.mk` の image を `plan/tools/boot-test.sh`（`uefi-usb`）、i386 を `BOOT_MODE=bios-ide` で login prompt。
- end の道（QEMU に GHCI は無いので試験だけの引き金）: `plan/ws290/tests/config-amd64-text-end.mk`（`config/ci/config-amd64.mk` を include、`ZEDBSD_GRAPHICAL_BOOT := n`、`ZEDBSD_BOOT_KERNEL_MESSAGES := y`、`CONFIG_PCAT_SERIAL_MIRROR := y`、`ZEDBSD_TEST_CPPFLAGS := -DKERN_TEST_TEXT_END_SECONDS=45`（`Makefile:890`））。`text.c` に `#ifdef KERN_TEST_TEXT_END_SECONDS` の kernel thread を置き、text の init から 45 s 後に `kern_text_end()` を呼んで結果を log（Q5）。script: QMP と gdbstub（`-gdb tcp::<port>`）付きで起動 → `plan/tools/guest/serial.py` で login → 20 s の時点で gdb で `linear_pixels` の値を記録 → end の結果の行を serial で待つ → QMP の screendump A → serial で `echo MARKER > /dev/console` → screendump B → A と B が画素で同じ（console が描かない）、serial に MARKER が出る → `plan/ws290/tests/graphics-probe.c`（`--file` で入れる）が /dev/graphics の open で `ENODEV` → gdb で `text_ended` 1、`linear_pixels` 0、記録した番地の `x/1wx` が読めない。要約だけを出す。console の log では判定しない。
- 未実施になる物: panic の後の画面（host の (b) で代える）。

#### 13.5 実機

- p014（VF2 + KC705）は VF2 に end が無いので `ENOTSUP` の道（範囲を保つ）だけを通る。段 3 で KC705 の DISP2 が attach の時に点いているか（`boot_take` の log）を記録する。
- amd64 + discrete の GHCI（GOP が GHCI の上）での解放の確かめは今の機材の計画に無い。未実施として、ユーザーが拾える試験の一覧に置く（Q1 に頼む）。

### 14. 実装の段（p017 の中、合計 14 LW）

| 段 | 内容 | path | LW | 受け入れ |
| --- | --- | --- | --- | --- |
| 1 | text-display の op と `kern_text_end` | `include/kern/text-display.h`・`src/kern/text-display.c` | 1.5 | ws014 の試験 PASS、amd64 の build の warning 0 |
| 2 | PC/AT の text 層の end（`text_ended`、resume・reveal・reveal_fatal の条件、splash の停止）と試験の引き金の `#ifdef` | `src/drivers/platform/pcat/graphics/text.c`・`text.h` | 2.5 | 13.2 の (b)(e)(f)(g) |
| 3 | /dev/graphics と backend の end（`ENODEV`、`kern_device_map/unmap` への置き換え、`linear_retired[]`、`backend_ended`） | `src/drivers/platform/pcat/graphics/pcat-graphics.c`・`backend.c`・`backend.h` | 3 | 13.2 の (c)(d)(h) |
| 4 | kernel の側の host 試験 | `plan/ws290/tests/text-end-host-test.c`・`text-end-stubs.c`・`text-end-host-test.sh` | 2 | 13.2 の全場面 PASS（ASan・UBSan も） |
| 5 | GHCI（`boot_end`、field、comment）と host 試験 | `src/drivers/gpu/ghci/ghci.h`・`display-scan.c`、`plan/ws290/tests/ghci-driver-host-test.c`・`ghci-stubs.c`・`ghci-stubs.h` | 3 | 13.1 の場面 PASS、`ghci-host-test.sh` 全 PASS |
| 6 | build の全部と QEMU の script・config・probe、Q1 の割当で実行 | `plan/ws290/tests/config-amd64-text-end.mk`・`text-end-qemu.sh`・`graphics-probe.c` | 2 | 13.3 の warning 0、13.4 の要約 PASS（QEMU は Q1 の割当の後） |

- 段 1〜3 は順に依存、段 5 は段 1 の宣言だけに依存（stub で先に試せる）。他の helper が `src/drivers/gpu/ghci/vk/**` と一部の試験を直しているので、段 5 の `ghci-stubs.c`・`ghci-driver-host-test.c` は P3 が持ち主を確かめてから。
- 案 B（§11）を選んだ時は +3 LW（hal.h、全 port の HAL、kernel の口）と HAL の承認。

### 15. 選ばなかった案と理由

- **物理の範囲を渡す `kern_framebuffer_leave(physical, bytes)`（p012 の「残り」1 の案）**: discrete の boot framebuffer の CPU の物理を window から計算する（`window_physical + (FB_BASE − LOCAL_BASE)`）のは、firmware が window を driver と違う形にした時に「重ならない」と誤り、console が描いている範囲を返す（危ない側に倒れる）。ユーザーの決定も console を通す鎖。範囲を見ない全体の終わりは安全側に倒れる。損は、GOP が別の card にある機械で GHCI が console を止める事（serial と klog は残る）。
- **drv_gpu の core の `GPU_DISPLAY_CLAIM`（`gpu.c:3841-3847`）で呼ぶ**: 全 GPU driver の振る舞いが変わる。範囲の外。
- **`kern_text_suspend` に「戻さない」の印を足す**: suspend は数えず戻せる前提で 8 か所以上が使う（1.5）。意味を混ぜると resume の側の全部を見直す要がある。
- **`kern_text_register()` で serial の表に差し替える**: PC/AT の serial の mirror は text 層の `putc` の中（`text.c:408`）にあり、差し替えると serial の出力も cell も snapshot も失う。
- **backend が `hal_space_unmap_device` を直に呼ぶ**: 今の map は直に呼んでいる（`backend.c:1062`）が、ユーザーの決定の「kernel 内 API → HAL」に合わせ `kern_device_map/unmap` に揃える（HAL の属性は同じ）。
- **/dev/graphics の node を消す・file を強制で閉じる**: kernel に file の revoke の仕組みが無く、mmap も無いので `ENODEV` で足りる。
- **UMA でも通知する**: 返す物が無く、console を止める利点が無い（Q3）。
- **pc98・generic（sun4u）・VF2 にも end を実装する**: GPU が呼ばず試せない code になる。sun4u はユーザーの物（Q2）。
- **QEMU の引き金を gdb の `call kern_text_end()` にする**: 止めた vCPU の任意の文脈で眠る関数を呼ぶので不安定（Q5）。

### 16. 出典

- 外部の code・値の引用は無い。zedBSD の既存の code（上の `file:line`）と、GHCI の driver に既にある block の出典（`Procedure referenced: amaterrace-gpu d9252e4 docs/ghci.md (section 13.3 …)`、`display-scan.c:2515-2516`、`memory.c:572-574`）だけ。新しい code の comment に開発の管理情報（WS・Phase の番号など）を書かない（AGENTS.md §10、Guardrail の「出典」）。

### 17. ユーザー・Q1 の判断の項目

| ID | 問い | 推し | 他の案 |
| --- | --- | --- | --- |
| Q1 | amd64 の HAL が early console 用に持つ framebuffer の恒久の mapping（`space.c:586-593`）も消すか | 消さない（HAL の API を変えない。handover の後は HAL が触らない契約、`hal.h:81-86`） | 消す: §11 の hal.h の差分（承認が要る、+3 LW、全 port） |
| Q2 | 使用終了を実装する platform | PC/AT（amd64・i386 の共有 code）だけ。他は `ENOTSUP` で今のまま | pc98・generic にも（試せない code、sun4u はユーザーの物） |
| Q3 | UMA の GHCI でも通知するか | しない（返す範囲が無い） | する（console は GHCI の最初の flip で止まる） |
| Q4 | VF2 + KC705（p014 の機材）では end が無く解放は起きない | v1 はこのまま。p014 の段 3 で DISP2 が点いているかを記録し、点いていれば別の WS で VF2 の扱い | v1 で VF2 の表に end を足す（HAL が SoC の framebuffer に描き続けるので「終わり」と言えない） |
| Q5 | QEMU の試験だけの引き金（`KERN_TEST_TEXT_END_SECONDS`、試験の config だけで有効）を `text.c` に置くか | 置く（先例 `KERN_TEST_CHECKPOINTS`、`Makefile:856-858`） | 置かず QEMU の end の道は host 試験だけ（QEMU は回帰だけ） |

### 18. design.md への反映（P3 へ）

- §13 の版 4.15 の段落の「v1 は boot framebuffer を解放しない（`boot_console` = 1 …）」と「両方の kernel の API は Future Work の候補」を、この設計（使用終了の鎖、`boot_console` は通知の成功で 0、UMA は呼ばない、VF2 は ENOTSUP）に直す（版 4.16）。p012 の phase.md の「残り」1 と「既知の制限」の `kern_framebuffer_leave` の行も「p017 で別の形（§15）」と注記。
- Future Work: GPU の使用の終わりに /dev/gpu0 の scan-out を console の backend に切り替える（ユーザーの決定の後者）、panic の hook（`ghci_display_panic` の呼び手）、VF2 の扱い（Q4）、範囲を見る end。

## 記録

- 2026-10-28 P3（q1644-i01）: ユーザーの決定（上）で立てた。
- 2026-10-28 P3 の helper（q1644-i01）: 上の「設計」を書いた（code は変えていない、design-reviewer の review は未、Q1〜Q5 は未決）。
