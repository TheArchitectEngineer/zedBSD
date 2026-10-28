<!-- awesome-plan project=zedbsd record=ws290-p017 -->

# ws290-p017 boot framebuffer の使用終了の通知と解放

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §13（版 4.15 の「p012 の実装」の boot framebuffer）と、この phase.md の設計の節（着手で書く）。
- 状態: planned
- 依存: p012（表示）、p011（local の allocator と `ghci_memory_boot_release`）

## ユーザーの決定（2026-10-28、原文は ws.md）

「GPUの使用開始は、consoleドライバに通知しています。consoleドライバは、/dev/gprahicsドライバを使ってGOPに描画していると思います。そこで、/dev/graphicsデバイスに使用終了の口を作るのがいいと思います。GPUの使用開始時に、/dev/consoleに使用終了を通知、/dev/consoleが/dev/graphicsに使用終了を通知、/dev/graphicsがカーネル内APIを呼んでマッピング解除を要求。カーネル内APIがHALを呼んでマッピングを実際に解除。ここまではWS290で実装。将来的に、Keilandが終了してGPU使用を終了するときに、/dev/gpu0のscanoutを/dev/consoleのバックエンドに切り替える機能を作るのがいいです。後者は今回はやらず、future workにすればいいです。」

解釈（Q1 の確かめにユーザーが「その理解でよい」）: GPU の使用開始の時に kernel の text-display（`kern_text_*`、console）へ「使用終了」を通知 → text-display が graphics の backend（amd64 は `src/drivers/platform/pcat/graphics/pcat-graphics.c`、他の platform の backend も同じ口）へ通知 → backend が kernel 内 API で framebuffer の mapping の解除を要求 → kernel 内 API が HAL で実際に解除。panic の表示は v1 で無し（serial と klog）。scanout を console の backend に戻す機能と panic の hook は Future Work。

設計の判断の項目への答え（2026-10-28、Q1 経由で P3 が伝えた。原文は P3 の伝言の引用。ws.md への記録は P3）:

- Q1（HAL の恒久の窓）: 「HALはそのマッピングをカーネルに委譲しています。HAL APIは追加せず、カーネルがアンマップしてOKです。」
- Q2（実装する platform）: 「全部の backend」
- Q4（VF2）: VF2 の backend にも実装し、p014 の機材で解放を確かめる計画を立てる（P3 の伝言の要約。原文は伝言に無く、P3 が補う）。
- Q3（UMA は通知しない）・Q5（試験だけの引き金）: 推しの案のまま（P3 の伝言の要約）。

解釈（この helper、P3 の確かめが要る）: Q1 は「HAL の API（hal.h の宣言）を足さず、kernel が既存の口で HAL の early console の窓も外す」。既存の HAL の関数ではその窓を外せない事を §1.3 で code から確かめたので、hal.h を変えずに既存の `hal_space_map_device`・`hal_space_unmap_device` の amd64 の実装（`src/hal/`）でその窓を kernel に渡す形にした（§11、判断の項目 Q7）。Q2 は「console が画面を持つ全部の板の表に `end` を足す」（§10）。

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

2026-10-28 q1644-i01 の P3 の helper が worktree `p3-ws290g`（head 7811b5562）の code を読んで書き、同じ日に design-reviewer（Opus）の review の H1・M1・M2・L1〜L6 と、ユーザーの Q1・Q2・Q4 の答えを反映して直した（head 6250b7f97。引いた `src/`・`include/` は 7811b5562 から変わっていない事を `git diff --stat` で確かめた）。
code の事実には `file:line`（この head の行）を付けた。source は変えていない。

### 0. 要旨

- 鎖: GHCI の driver（最初の自分の flip が取られた後、lock 無し）→ `kern_text_end(ranges, count)`（text-display、/dev/console の顔。ranges は呼び手の device が decode する CPU の物理の範囲で、GHCI は自分の BAR0・BAR2 の全体）→ 板の表の新しい省略可の op `end` → 板が console の表示の memory（PC/AT は handoff の GOP・今の linear・VGA の text の窓）が ranges と重なるかを決め、重なる時だけ text 層を描かない状態に → /dev/graphics（PC/AT は `drv_pcat_graphics_end()`）が owner の描画を断る → backend が kernel の mapping を忘れる → kernel 内 API `kern_device_unmap()`（既存、`src/kern/pmem.c:169-176`）→ HAL `hal_space_unmap_device()`（既存、`include/hal/hal.h:787-793`）。
- 重ならなければ `ENOENT` で何も変えない（console は別の device の上。QEMU に KC705 の GHCI を渡した `config-amd64-ghci.mk` の guest、i915・amdrdna が console を持つ機械、VF2）。
- GHCI: 0 → boot の範囲を返す。`ENOENT` → console はこの device に何も描かないので console を止めずに範囲を返す（Q6）。`ENOTSUP`・`EBUSY`・他 → 範囲を保つ（今と同じ）。
- 「使用終了」は suspend と違い戻らない（v1）。`kern_text_resume`・`reveal`・`reveal_fatal` は終わった text 層を描かない。cell は更新し続ける（snapshot と将来の scan-out の切り替えのため）。scan-out を console に戻すのは Future Work。
- HAL の early console の窓（amd64、VA `0xffffffffc2000000`）: ユーザーの答え（Q1）で kernel が外す。既存の HAL の関数では外せない（§1.3）ので、hal.h を変えず、amd64 の HAL の実装で `hal_space_map_device()` が handoff の framebuffer にはその窓を返し、`hal_space_unmap_device()` がそれを外す形にする（§11、Q7）。framebuffer が固定の窓（0xf0000000〜0xf1000000）にもある時は、その窓は他の device と共有の 2 MiB の page なので外せず、HAL は `HAL_ERR_BUSY` を返し GHCI は範囲を保つ（安全側）。
- `end` を全部の板の表に足す（Q2・Q4、§10）: PC/AT（amd64・i386）・PC-98・generic の front end と sun4u は鎖の全部、rpi4・x68k・VF2 は /dev/graphics の無い表だけの `end`（重ならなければ `ENOENT`、重なれば止められる物を止める）。serial だけの板の共通の表（`serial-console.c`）は表示を持たないので `ENOENT`。
- 他の GPU driver（amdrdna・i915・powervr・bcm2711・venus）と drv_gpu の core は呼ばないので振る舞いは変わらない。
- user の空間に firmware の framebuffer を map する道は今無い（/dev/graphics に mmap が無く、/dev/mem も無い）。終わった後の /dev/graphics は open も ioctl も `ENODEV`。

### 1. 今の code（調べた事実）

#### 1.1 text-display（`kern_text_*`）

- `include/kern/text-display.h:70-123` の `struct kern_text_ops` は板ごとの表。`suspend`・`resume`（:96-98）は「graphics の mode が画面を持つ間」止めて再開する、戻せる操作。`snapshot`・`reveal`・`reveal_fatal`・`progress`（:100-122）は省略可（NULL）。`kern_text_register()`（:132、`src/kern/text-display.c:48-60`）は表の差し替えで、atomic に公開し lock を取らない（:11-14、:537-543）。
- `kern_text_suspend()`・`kern_text_resume()`（`src/kern/text-display.c:367-406`）は表の op を呼ぶだけで、数え（参照の数）を持たない。
- `kern_text_kernel_putc()`（:125-142）が `kernel_putc`（`include/hal/hal.h:1541-1548`、`src/kern/entry.c:498`）として公開される。quiet の間は log へ、他は `kern_text_putc()`。
- 表を持つ板（全部 designated initializer）: PC/AT `src/drivers/platform/pcat/graphics/text.c:462-477`（amd64・i386 で共有）、PC-98 `src/drivers/platform/pc98/graphics/text.c:82-92`、sun4u `src/drivers/platform/sun4u/sun4u-console.c:114`、x68k `src/drivers/platform/x68k/x68k-console.c:43-54`、rpi4 `src/drivers/platform/rpi4/rpi4-console.c:92-103`、serial の板 `src/drivers/generic/serial-console.c:59-70`（VF2 はこれを写して `reveal_fatal` を足す、`src/kern/platform/visionfive2.c:342-348`）。

#### 1.2 PC/AT の /dev/graphics（3 つの file）

- `src/drivers/platform/pcat/graphics/pcat-graphics.c` が /dev/graphics の node そのもの（`drv_graphics_device_register`、:68-88、`cdev_register("graphics", …)` :84）。登録は VFS の init の中（`src/kern/vfs.c:451-457`、`CONFIG_DRIVER_GRAPHICS_DEVICE`）で、そこで sleep する mutex `graphics_lock` を作り `graphics_lock_ready = 1`（`pcat-graphics.c:118-129`）。`include/kern/graphics-backend.h` の `struct drv_graphics_backend` は使わない（それは generic の front end `src/drivers/generic/graphics-device.c` 用で、build は sparcv9 だけ、`platform/sparcv9/vmunix.mk:93-94`）。どの front end を作るかは `Makefile:590-596`（pcat・amd64 → pcat、pc98 → pc98、sparcv9 → sun4u、他の platform は /dev/graphics 無し。riscv64 の VF2 にも無い）。
- cdev の op は open・close・ioctl だけ（`pcat-graphics.c:59-63`）。mmap は無い（generic も `graphics-device.c:128-132`、pc98 も `pc98-graphics.c:58-62`）。描画は全て ioctl で、kernel が backend の mapping に書く。
- lock: `graphics_lock`（LOCK_RANK_DEVICE、:40-41、:119-129）。open は 1 つだけ（`graphics_owner`、:132-159）。ioctl は owner 以外 `EBADF`（:680-681）、ENTER の後でないと `ENXIO`（:187-193）。ENTER は `graphics_lock` の下で `kern_text_suspend()`（:239）、失敗・close・`drv_graphics_device_restore_text()`（:93-116）で `kern_text_resume()`（:99、:113、:173、:244、:261）。`restore_text` は `graphics_lock_ready` が 0 なら lock を取らずに `kern_text_resume()` だけを呼ぶ（:96-103）。順は graphics_lock → text_lock。
- backend `src/drivers/platform/pcat/graphics/backend.c`: 起動の handoff の framebuffer を `kern_boot_framebuffer("pcat.framebuffer", &boot_framebuffer)`（:1050、`boot_framebuffer` :55、`src/kern/pmem.c:231-` が HAL の名前付きの値 `pcat.framebuffer.*` を読む、`src/hal/amd64/bsp-pcat/boot.c:72-95`）で読み、`hal_space_map_device(phys, size, READ|WRITE|NOCACHE)`（:1062-1063）で kernel に map して `linear_pixels`・`linear_physical`・`linear_bytes`（:57、:63-64、:1065-1067）に持つ。map が失敗すると `linear_pixels` は NULL のまま、`boot_framebuffer` は handoff の値を保つ（:1060、:1074-）。map の後 `text_console_start()`（:1070、:1123-1143）が text 層を作り、text が描ける時 `kernel_putc` を `kern_text_kernel_putc` にする（:1133-1139）。`leave` は linear なら状態だけ（:226-233）、Cirrus・VGA は VGA の register を書く（:236-247）。
- `drv_pcat_graphics_framebuffer_moved()`（:1205-1234）は PCI の BAR が動いた時に新しい所を map し `linear_physical` を新しい所にする（:1232）。古い mapping は「書き手が持つかも」として残す（:1198-1204）。新しい所の map が失敗すると何も変えない（:1222-1227）。呼ぶのは `src/drivers/pci/pci-pcat-assign.c:742-746`（BAR の拡大、`drv_pci_pcat_grow_bar` の呼び手は `src/drivers/gpu/amdrdna/gmc.c:1123`）で、起動の `kern_platform_init` の PCI の attach と deferred の割当（`src/kern/platform/pcat.c:392-430`）の中。backend の `drv_pcat_graphics_prepare()` はその後（:480）。
- text 層 `src/drivers/platform/pcat/graphics/text.c`: spinlock `text_lock`（LOCK_RANK_CONSOLE_TEXT、:81、:492）。backend の mapping を `drv_pcat_graphics_backend_get_framebuffer()`（`backend.c:533-561`）で受けて `text_pixels` に持つ（:499）。framebuffer が無ければ VGA の text memory（`vga_text_attach_locked` :563-584、`kern_device_map(0xa0000, 128 KiB)` の +0x18000、:58-60）。cell は `text_cells[]`（:79）。live の surface に書くのは `draw_cell_locked()`（:283 で `text_ready` を見る。画素は :340 で `text_pixels` を毎回読む）・`reveal`（:907-912）・`reveal_fatal`（:971-978）と、VGA の CRTC の `vga_cursor_locked()`（:600 で `text_ready` を見る）だけ。`text_ready` を 1 にするのは init（:535）・`resume`（:843）・`reveal_fatal`（:969）・`snapshot`（:217、終わりに戻す :223-230。間は `text_pixels` を RAM に向ける :211）。cell を変える道 `putc_locked()`（:411）・`write`（:684）・`clear`（:711）・`set_cursor`（:737）は `!text_ready` なら cell を変えずに戻る。`putc_locked()` は先に serial の mirror に書く（:408）。
- `reveal_fatal`（:936-991）は panic で、`text_lock` をこの CPU が持つか TEXT_FATAL_LOCK_SPINS（:950）の間取れなければ lock 無しで、suspend された surface にも `text_ready = 1` にして全画面を消して描く（BUG-158 の対処）。
- quiet boot の splash（`splash.c`）は独自の pointer `splash.pixels`（:77）に描く。HAL の early console の窓で始まり（`src/hal/amd64/bsp-pcat/cons.c:383`）、text 層の init で kernel の mapping に移るのは surface が framebuffer の時だけ（`text.c:543-544`、`splash.c:225-235`）。**linear の map が失敗して VGA の text を使う quiet boot では `splash.pixels` は HAL の窓のまま**で、`putc_locked()` の改行（`text.c:415-416`）と ticker の thread（:1016-1050、`text_hidden && !text_splash_ended` の間 :1034-1036）が handover の後も HAL の窓を通して GOP に描く。`drv_pcat_splash_stop()`（`splash.c:267-272`）で止まる。
- `snapshot`（`text.c:138-233`）は RAM へ描き、live の surface を読まない。`text_surface == NONE` なら `ENODEV`（:161-166）。使うのは venus の display（`src/drivers/gpu/venus/display.c:531`、:619、:661）。

#### 1.3 firmware の framebuffer の mapping（amd64）

- kernel の mapping: 上の `linear_pixels`（`backend.c:1062`）。`hal_space_map_device`（`space.c:314-360`）は `device_fixed_range()`（`src/hal/amd64/space.c:2737-2771`）の固定の窓（0xa0000〜0x100000、0xf0000000〜0xf1000000、LAPIC・IOAPIC、:2750-2757）に入れば恒久の窓を返し（:350-352）、他は参照の数を持つ動的な view（:355-357、`device_window_map` :2775-、表は `device_mappings` :141）。0xf0000000 の窓は 2 MiB の large page 8 つ（`system_mmio_pd[0..7]`、:573）。
- 解除: `hal_space_unmap_device()`（`space.c:366-410`）。固定の窓なら何もせず `HAL_OK`（:398-401）。動的な view は参照を減らし、0 で PTE を消して全 CPU の TLB を shootdown（`device_window_unmap` :3018-3057、表に無い番地は `HAL_ERR_INVALID` :3033-3036、shootdown :3052、`shootdown()` :2236- は自分で割り込みを止めて ack を待つ）。kernel の側の口は `kern_device_map()`・`kern_device_unmap()`（`include/kern/pmem.h:68-76`、`src/kern/pmem.c:145-176`。`KERN_DEVICE_UNCACHED` は `HAL_SPACE_READ|WRITE|NOCACHE` で backend の今の属性と同じ、:154-161）。
- HAL の early console の窓: HAL の system の page table が handoff の framebuffer を `0xffffffffc2000000`（`bootloader/include/amd64-handoff.h:128`、2 MiB に揃えた先頭から）に恒久に map する（`src/hal/amd64/space.c:587-594`、`system_mmio_pd[16..]`（`AMD64_FRAMEBUFFER_PD_FIRST` :67）と端の 4 KiB の表 `system_framebuffer_edges`、`framebuffer-map.c:15-`、global・NX・UC）。loader の page table にも同じ窓がある（`locore.S:38-40`）。HAL の console は `framebuffer_pixels = 0xffffffffc2000000 + (physical の 2 MiB の中の offset)`（`cons.c:356-363`）で描き、quiet boot の splash の始めもこれ（`cons.c:383`）。
- **委譲の事実（Q1 の答えの確かめ）**: HAL が kernel に渡しているのは「描く事」で、`hal_putc()` は `kernel_putc` が公開された後は委ねて画面に触らない（`include/hal/hal.h:81-86`、`cons.c:158-175`）。HAL の `hal_printf`・`hal_fatal` も `hal_putc` を通る（`src/hal/amd64/lib.c:219`・:395-397、`pcat_cons_output_begin` は lock だけ）。`framebuffer_pixels` を使う HAL の関数（`cons.c` の :310・:350-393・:473-475・:770-819・:850・:880・:979）は全部 `hal_putc` の early の道か prekern の init の中で、handover の後は呼ばれない。**mapping（page table の entry）は HAL の system の page table に残り、kernel に渡す記録は無い**（kernel が読むのは名前付きの物理の値 `pcat.framebuffer.*` だけ、`boot.c:72-95`）。
- **既存の HAL の関数ではこの窓を外せない**: `hal_space_unmap()`・`hal_space_prot()` は system の空間（`HAL_SPACE_SYS` = NULL）と kernel の番地を断る（`space.c:1360-1365`、`hal_space_prot_query` :1216-1221）。`hal_space_unmap_device(0xffffffffc2000000 + offset, size)` は固定の 4 つの窓のどれでもなく（:384-396）、動的な view の表にも無いので `HAL_ERR_INVALID`（:3033-3036）。kernel はこの VA を知る口も持たない（`ZBL6_FRAMEBUFFER_VIRTUAL_BASE` は bootloader の header）。
- 物理: UMA・RAM の framebuffer は HAL が allocator から外す（`src/hal/amd64/page.c:187-189`、:831-833）。discrete の GHCI では boot framebuffer は local memory（BAR2 の window の向こう）で、GHCI の allocator が持つ（p011、`src/drivers/gpu/ghci/memory.c:562-610`）。
- i386 の HAL（`src/hal/i386/bsp-pcat`）は framebuffer を持たない（BIOS の VGA の text だけ。`framebuffer` を map する code が無い）。

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
- drv_gpu の core が driver の op を呼ぶ時の lock: `gpu.c` の mutex は `monitor_lock`（worker の作成の間だけ、:7231-7256、:7274）で、registry・session の spinlock は op の呼び出しの前に離す（例 :2240-2254 の `events`）。driver の display の op の周りに mutex を持つ所は見当たらない（L5。実装の段 5 で全部の道を確かめる）。i915（`xd-display.c:261-270`）と amdrdna（`readout.c:659-680`）は `pcat.framebuffer` の物理を読むだけで map しない。

#### 1.6 panic と klog

- `__libc_panic`・`kern_fatal`（`src/kern/panic.c:51-81`、:86-117）は ring に記録（`kern_log_record_fatal`）してから `kern_text_reveal_fatal()`（:71、:114）、その後 `hal_fatal`（`hal_putc` → `kernel_putc`）。`src/kern/user-probe.c:281` も reveal_fatal。
- klog は ring に足し、`kernel_putc` に写し（quiet で無ければ）、debug の port に書く（`src/kern/klog.c:110-151`）。PC/AT の text 層の `putc_locked` は serial の mirror に先に書く（`text.c:408`）。よって text 層が描かなくても serial と klog の ring・debug port には出る。

#### 1.7 GHCI の今

- bus の glue `src/drivers/gpu/ghci/pci-attach.c`: BAR0 は `drv_pci_device_bar()` で大きさを見て最初の page（`REGISTER_BYTES`）だけを map（:293-334）、BAR2 は placed なら claim して先頭から最大 1 GiB（`WINDOW_BYTES_MAX` :74、:373-375）を map（:342-392）。`struct drv_pci_mapping.physical_address` は「host bridge の変換・BAR の移動の後の CPU の物理」（`include/drivers/pci/pci.h:87-94`）で、`window_physical` に写す（:390）。`struct drv_pci_bar` は `bus_address` と `size`（:79-85）で CPU の物理を持たない。BAR の全体の大きさ（`bar.size`）は今 `struct ghci_device` に残らない。class は 03h/02h か 80h（:9-11、:53）で VGA（03h/00h）ではないので legacy の VGA の窓を decode しない。
- `boot_take()`（`src/drivers/gpu/ghci/display-scan.c:1226-1296`）が boot display を取り `boot_scanning = boot_kept = boot_console = 1`（:1293-1295）。意味は `ghci.h:565-571`、field は :637-641。
- `taken()`（:1457-1516）の中で最初の自分の image が取られると `boot_scanning = 0`、`boot_kept && !boot_console` の時だけ `boot_release = 1`（:1501-1507）。boot の image（`memory == NULL`・`copy_frame < 0`・`base == boot.base`、:1495-1500）が取られると `boot_kept` の間 `boot_scanning = 1`（:1509-1510）。今は `boot_console` が 0 にならないので解放されない（p012 の「残り」1、design §13 版 4.15）。
- boot の image の点け直し: `light(device, &display->boot)`（:1646-、`flips[0].placed = 1`・`flips[0].image = *image`、:1712-1719）は lease の終わりの `detach_step()`（:2093-2150、`boot_kept` の間 :2136-2141）が呼ぶ。`boot_scanning` が 1 になるのはその flip が取られた時（:1509-1510）。host の手の点け直し（:356-364）は直に `boot_scanning = 1`。
- `scan_flush()`（:2508-2581）: service（:494、`service = 1`）は lock を try だけで解放はしない。他（`ghci_scan_flush`、:1014-1019、呼び手は `display.c` の `display_release`（:391）・`display_wait`（:517）・`display_adoption_wait`（:753）・`present_shared`（:1113）・`present_copied`（:1188・:1236）と detach の `display-scan.c:387`）は `device.lock`・`display_lock` を取り（:2533-2534）、`boot_release` を取って `boot_kept = 0`（:2557-2561、今は boot の image が点け直されていないかを見ない）、全部を離した後（:2575）lock 無しで `ghci_memory_boot_release()`（:2578-2580）。
- detach（:380-388）は `display_lock` の下で `present = 0` にし、離してから `ghci_scan_flush()` と `buffers_give_back()`。
- `ghci_memory_boot_release()`（`memory.c:562-610`）は lock を持たずに呼ぶ約束（:568-570）。範囲を local の allocator に返し、window の使い手を減らす。
- 停止・panic: `ghci_display_panic`（:659-680）は `boot_kept` の間だけ boot の image を点ける（v1 は呼ぶ物が無い）。
- host 試験 `plan/ws290/tests/ghci-driver-host-test.c:3399-3455`（`scene_display_boot`）は解放の道を `device.display.boot_console = 0`（:3443）を直に置いて試している。stub は持っている lock の名前を数える（`plan/ws290/tests/ghci-stubs.c:54-55`、:219-265）。

#### 1.8 他の板の console と表示（Q2・Q4）

| 板 | console の表 | 表示の memory と mapping | `kernel_putc` | /dev/graphics |
| --- | --- | --- | --- | --- |
| PC-98（i386） | `pc98_text_ops`（`pc98/graphics/text.c:82-92`、suspend :317・resume :339） | text の VRAM を固定の kernel の番地 `0x800a0000`・`0x800a2000`（:32-33、HAL の低い memory の map）で直に書く。backend が GDC の 4 plane（0xa8000・0xb0000・0xb8000・0xe0000、各 32 KiB）と Cirrus の窓（0xf0000000、4 MiB）を `kern_device_map`（`pc98/graphics/backend.c:26-29`、:365-385）、失敗の時 `kern_device_unmap`（:416） | text 層（`backend.c:346`） | `pc98-graphics.c`（pcat と同じ形） |
| sun4u（sparcv9） | `sun4u_console_ops`（`sun4u-console.c:114`、`kernel_putc` :230、画面の状態 `screen_on` :138・`screen_suspended` :165） | `sun4u-firmware-fb.c` が `sun4u.framebuffer.*`（:118-119）を `kern_device_map`（:152）して `fb_pixels` に持つ。HAL の early console は物理の番地で描く（`src/hal/sparcv9/bsp-sun4u/framebuffer.c:97`、:216・:240・:389） | console（:230） | generic の front end（`graphics-device.c`）に `sun4u_fb_backend`（`sun4u-firmware-fb.c:70`） |
| rpi4（arm64） | `rpi4_console_ops`（teletype、suspend は HAL の `rpi_cons_suspend_output()`、`rpi4-console.c:309-317`） | HAL の console が firmware の framebuffer（VideoCore の RAM、最初の 1 GiB）を direct map（`ARM64_DIRECT_BASE + pixels_phys`、`src/hal/arm64/bsp-rpi-common/framebuffer.c:289-315`）で描く。名前は `rpi4.framebuffer`（`src/kern/platform/rpi4.c:208`）。`rpi_cons_suspend_output()` は以後の framebuffer の書き込みを止め、serial は残す（`cons.c:175-195`、header `cons.h:8-22` は driver から使う既存の口） | 公開しない（HAL が描く） | 無し |
| x68k（m68k） | `x68k_console_ops`（teletype、suspend・resume は何もしない、`x68k-console.c:43-54`） | HAL の console が text の VRAM（`X68K_TVRAM_PHYSICAL` 0x00e00000、`src/hal/m68k/bsp-x68k/mmio.h:14`、plane 0x20000 ずつ `console.c:32`・:135）に描く。kernel の名前付きの値は無い | 公開しない | 無し |
| VF2（riscv64） | serial の表の写し + `reveal_fatal`（`visionfive2.c:342-348`、`reveal_fatal` :1370-1378 は powervr の画面の取り戻し） | HAL の fbcons（`src/hal/riscv64/fbcons.c`）が loader の画面を direct map か HAL の device window で描く（`bsp-visionfive2/board.c:372-432`）。HAL は描くかに関わらず画面を `riscv64.framebuffer.*` と名付ける（:366-395）。画面が無ければ名付けず描かない（:384-386）。fbcons を止める口・その mapping を外す口は kernel に無い（`fbcons.h:40-68` は attach・ready・描画だけ） | 公開しない（`serial-console.c:8-14`） | 無し |
| 他の serial の板（`serial-console.c` の表をそのまま使う物） | `serial_console_ops`（:59-70） | 板により HAL が RAM の framebuffer に描く（例 `src/hal/riscv64/bsp-virt/framebuffer.c:68`・:176、`src/hal/m68k/bsp-virt/framebuffer.c:169`）が、どれも PCIe の GHCI の BAR とは別の RAM | 公開しない | 無し |

### 2. 責務と境界

| 層 | file | 責務（使用終了） | しない事 |
| --- | --- | --- | --- |
| GHCI の bus の glue | `src/drivers/gpu/ghci/pci-attach.c` | BAR0・BAR2 の CPU の物理の全体（先頭は mapping の `physical_address`、大きさは `bar.size`）を `struct ghci_device` に残す | 解放の判断をしない |
| GHCI の driver | `src/drivers/gpu/ghci/display-scan.c`・`ghci.h` | 返す boot の範囲がある時、最初の自分の flip が取られた後に 1 度だけ lock 無しで自分の BAR の範囲を付けて `kern_text_end()` を呼び、0・`ENOENT` なら範囲を返す | console・/dev/graphics・HAL に直に触らない |
| text-display（/dev/console の顔） | `include/kern/text-display.h`・`src/kern/text-display.c` | 引数を確かめ、表の `end` を呼び、無ければ `ENOTSUP`。0 の時 snapshot の消費者に知らせる | 板の状態・範囲の判断を持たない |
| 板の text 層（PC/AT・PC-98・sun4u・rpi4・x68k・VF2・serial） | §10 | console の表示の memory が範囲と重なるかを決め（重ならなければ `ENOENT`）、重なれば戻らない「終わった」状態にし、表示の memory に二度と書かない（resume・reveal・reveal_fatal・splash も）。cell は更新し続ける。/dev/graphics がある板は続けて知らせる | mapping を自分で HAL に頼まない |
| /dev/graphics（PC/AT・PC-98・generic） | `pcat-graphics.c`・`pc98-graphics.c`・`graphics-device.c` | lock の下で（lock が未だ無ければ lock 無しで）ENTER の状態を捨て、以後の open・ioctl を `ENODEV`。backend に終わりを渡す。結果を覚えて 2 度目に返す | hardware に書かない |
| backend（PC/AT・PC-98・sun4u） | `pcat/graphics/backend.c`・`pc98/graphics/backend.c`・`sun4u-firmware-fb.c` | 範囲との重なりの問いに答える。end で kernel の mapping を `kern_device_unmap()` で消し、pointer を NULL・ready を 0 に | VGA・Cirrus の register を書かない |
| kernel 内 API | `src/kern/pmem.c`（既存の `kern_device_map`・`kern_device_unmap`） | HAL を呼ぶ | 変えない |
| HAL（amd64 の実装） | `src/hal/amd64/space.c`・`bsp-pcat/cons.c` | 既存の `hal_space_map_device` が handoff の framebuffer には early console の窓を返し、既存の `hal_space_unmap_device` がその窓の PTE を消して TLB を shootdown し、HAL の console の pointer を NULL に（§11） | hal.h を変えない。固定の窓は外さない（`HAL_ERR_BUSY`） |

- ユーザーの言う「/dev/console」は `kern_text_*`（/dev/console の `src/drivers/generic/console.c` は `kern_text_*` の上の多重化で、画面を持つのは表）、「/dev/graphics」は PC/AT では text 層を含む `src/drivers/platform/pcat/graphics/` の driver（text 層は `text.h:10-14` の通り /dev/graphics が持つ）。
- 範囲を付けるのはユーザーの鎖（console → graphics → kernel 内 API → HAL）を変えず、「どの GPU の使用開始か」を通知に載せるだけ（Q6）。

### 3. interface

#### 3.1 公開（include/kern、HAL ではない）

`include/kern/text-display.h` に範囲の型と上限を足し、`struct kern_text_ops` の末尾（`progress` の後）に省略可の op を足し、関数を 1 つ足す。全部の表が designated initializer なので（1.1）、`end` を足さない表は NULL のまま build が通る。

```c
/* One range of processor physical addresses a display driver decodes. */
struct kern_text_range {
	uint64_t physical;
	uint64_t bytes;
};

#define KERN_TEXT_END_RANGES	4U
```

```c
	/*
	 * Optionally ends the console's use of a display for good, when the
	 * console's display memory lies in one of the count ranges.  From a
	 * return of 0 nothing of the console (its text, a quiet boot's splash,
	 * a graphics mode the board's /dev/graphics entered, the reveal of a
	 * fatal stop, the hardware layer's early console it took over) writes
	 * to that memory again, and the mappings of it the console was given
	 * are removed; the console's characters still reach its other outputs
	 * (a serial line, the log), and its cells keep following them.
	 * Returns ENOENT, changing nothing, when no range holds the console's
	 * display memory; ENOTSUP when it does but the board cannot stop the
	 * drawing; EBUSY while the hardware layer still draws its early console
	 * there or keeps a mapping it cannot give up; or the error of a mapping
	 * that could not be removed.  The console stays off the display after
	 * any return but ENOENT and ENOTSUP.  A later call for an overlapping
	 * range returns the first result.  Called in thread context with no
	 * lock held; it may sleep.
	 */
	int (*end)(const struct kern_text_range *ranges, unsigned count);
```

```c
/*
 * Ends the console's use of a display for good, for a display driver that
 * is about to give the memory behind ranges (the processor physical
 * ranges the driver's device decodes, count of them) to others.
 *
 * Returns 0 once nothing of the console writes to that memory or keeps a
 * mapping of it, ENOENT when the console's display is not in the ranges
 * (nothing changes; the console draws elsewhere), ENOTSUP when no console
 * is published or the board's console cannot leave a display in the
 * ranges, EBUSY when the hardware layer still draws there or keeps a
 * mapping of it, EINVAL for an empty, oversized or wrapping range list, or
 * the error of a mapping that could not be removed.  The caller holds no
 * lock; the call may sleep.  There is no way back.
 */
int kern_text_end(const struct kern_text_range *ranges, unsigned count);
```

`src/kern/text-display.c`: `ranges` が NULL・`count` が 0 か `KERN_TEXT_END_RANGES` を超える・ある範囲の `bytes` が 0 か `physical + bytes` が桁あふれ → `EINVAL`。`ops()` が NULL か `end` が NULL なら `ENOTSUP`。他は `table->end(ranges, count)` を呼び、0 の時だけ `text_changed()`（:546-571）で snapshot の消費者を起こしてから結果を返す。lock は取らない（今の他の関数と同じ）。重なりの式は 1 つの inline の関数 `kern_text_range_overlaps(ranges, count, physical, bytes)` として header に置き、全部の板が使う（両方とも空でない、桁あふれは前で断る: `a < b + bn && b < a + an`）。

#### 3.2 PC/AT の内部

- `text.h`: `int drv_pcat_text_end(const struct kern_text_range *ranges, unsigned count);`（表の `.end`）。
- `backend.h`: `int drv_pcat_graphics_backend_overlaps(const struct kern_text_range *ranges, unsigned count);`（handoff の `boot_framebuffer` の範囲（handoff が有れば、linear の map の成否に関わらず）と、`linear_pixels` が有る時の `linear_physical`/`linear_bytes` のどれかが ranges の 1 つと重なれば 1）、`int drv_pcat_graphics_end(void);`（/dev/graphics の node の分、`pcat-graphics.c` に置く）と `int drv_pcat_graphics_backend_end(void);`（`backend.c`、`graphics_lock` の下か、lock が未だ無い時は lock 無しで呼ぶ）。backend.h は既に text 層と backend の間の口（`get_framebuffer`・`framebuffer_moved`、:51-73）を持つので同じ所に置く。
- text 層は surface が VGA の text の時、自分の aperture `[0xa0000, 0xc0000)`（`text.c:58-59`）も重なりの候補にする。
- 新しい状態: `text.c` `static int text_ended;`（atomic、`reveal_fatal` が lock 無しで読む）と `static int text_end_error;`。`pcat-graphics.c` `static int graphics_ended; static int graphics_end_error;`（`graphics_lock` の下）。`backend.c` `static int backend_ended;`、BAR の移動で残した古い mapping の表 `linear_retired[PCAT_RETIRED_MAX]`（4 個、大きさは `linear_bytes`）と数、HAL の early console の窓を受けた mapping `boot_window`（下）。
- backend の map を `hal_space_map_device(…, READ|WRITE|NOCACHE)`（`backend.c:1062`、:1222）から `kern_device_map(…, KERN_DEVICE_UNCACHED, …)` に替える（HAL の属性は同じ、1.3）。解除と対にして、ユーザーの決定の「kernel 内 API → HAL」の形にするため。
- HAL の early console の窓を外す（Q1 の答え）: §11 の HAL の実装の後、`kern_device_map(handoff の物理, handoff の大きさ, UNCACHED)` は HAL の early console の窓（`0xffffffffc2000000 + offset`）を返す。prepare の今の map がそのまま「HAL から委譲された mapping」を受け取る（`linear_pixels` がその番地になる）ので、backend の end の `kern_device_unmap(linear_pixels, linear_bytes)` がその窓を外す。BAR の移動の後（`framebuffer_moved` が新しい所を map した後）は、古い `linear_pixels`（= 委譲された窓）が `linear_retired[]` に入り、end で同じく外れる。linear の map が失敗した時（VGA の text で動く時）は窓を受けていないので、backend の end は `kern_device_map(handoff, UNCACHED)` で窓を受けて直ちに `kern_device_unmap` する（受ける事が委譲の受け取り）。handoff が無ければ何もしない。

`drv_pcat_text_end()` の順:

1. 重なり（backend の 2 つと VGA の aperture）が無い → `ENOENT`、何も変えない（2 度目以降も同じ。終わった後でも重ならない device には `ENOENT`）。
2. `text_ended` が 1 → `text_end_error`（最初の結果）。
3. `kernel_putc` が `kern_text_kernel_putc` でない（HAL がまだ early console を描く）→ `EBUSY`、何も変えない（`text_ended` は立てないので後で試せる。GHCI は 1 度しか呼ばない）。
4. `text_lock` の下で終わりにする（§4、§6）。離す。
5. `drv_pcat_graphics_end()` の結果を `text_end_error` に置いて返す。

#### 3.3 GHCI

- `ghci.h` の `struct ghci_device` の bus の field に `struct ghci_bus_range { uint64_t physical; uint64_t bytes; } bars[2]; unsigned bar_count;` を足す（glue が start の前に埋め、変えない。comment の「bus fields」に足す）。`pci-attach.c` の `map_registers()` と `map_window()` が map に成功した時、`physical_address` と `bar.size`（BAR2 も 1 GiB で切らない全体）を入れる。host bridge の変換は BAR の中で線形なので、BAR の CPU の物理は先頭の `physical_address` から `bar.size` 続く。
- `struct ghci_display`: `boot_console` の意味を「kernel の console がまだ描き得る（`kern_text_end()` が 0 も `ENOENT` も返していない）」にし、`int boot_end_due;`（通知が要る）と `int boot_end_tried;`（1 度だけ）を足す。comment（:565-571）を直す。
- `display-scan.c` に static の `boot_end(struct ghci_device *device)`（lock 無しで呼ぶ。§4・§9）と、static の `boot_in_use(const struct ghci_display *display)`（`boot_scanning`、または `flips[0].placed` で `flips[0].image` が boot の image（`taken()` の :1495-1500 と同じ判定、関数に出す））。`#include <kern/text-display.h>`。
- test の stub（`plan/ws290/tests/ghci-stubs.c`・`.h`）: `int kern_text_end(const struct kern_text_range *, unsigned)`（結果を置ける、呼ばれた数、受けた範囲の写し、呼ばれた時に lock を 1 つも持っていない事を `expect`）。`ghci_read` の stub は読んだ register を記録する（L4 の確かめ）。

#### 3.4 他の板（Q2・Q4）

| 板 | `end` の関数 | 重なりの候補 | 重なった時 |
| --- | --- | --- | --- |
| PC-98 | `drv_pc98_text_end()`（`pc98/graphics/text.c`）→ `drv_pc98_graphics_end()`（`pc98-graphics.c`）→ `drv_pc98_graphics_backend_end()`（`backend.c`） | text の VRAM `[0xa0000, 0xa4000)`、GDC の 4 plane、Cirrus の窓 `[0xf0000000, +4 MiB)`（backend が map している時） | text 層は pcat と同じ形で終わる（`text_ended`、cell は更新、`kernel_putc` が `drv_pc98_text_putc` でなければ `EBUSY`）。/dev/graphics は `ENODEV`。backend は 4 plane と Cirrus の窓を `kern_device_unmap`（:416 の失敗の道と同じ呼び方）。text の VRAM の固定の番地（HAL の低い memory の map）は外せない → 結果は `EBUSY`（console は止まる、呼び手は範囲を保つ） |
| generic の front end（sun4u） | `include/kern/graphics-backend.h` の `struct drv_graphics_backend` の末尾に省略可の `int (*end)(void);` を足し、`drv_graphics_device_end()`（`graphics-device.c`、`graphics_lock` の下、lock 前の道も pcat と同じ）が選ばれた backend の `end` を呼ぶ。sun4u の表に `.end = console_end`（`sun4u-console.c`）、`sun4u-firmware-fb.c` に `drv_sun4u_fb_end()` | `sun4u.framebuffer.phys` から `pitch × height` | console は `screen_on` を 0 に固定し（suspend と違い戻さない）、/dev/graphics を終え、`fb_pixels` を NULL にして `kern_device_unmap`。HAL の early console は handover の後は描かない（`kernel_putc` :230）。HAL は物理の番地で描くので外す mapping は無い |
| rpi4 | `console_end()`（`rpi4-console.c`） | `rpi4.framebuffer` の物理と大きさ（`kern_boot_framebuffer`） | 既存の `rpi_cons_suspend_output()` を呼び、以後 `console_resume` は `rpi_cons_resume_output()` を呼ばない（driver の側の印）。framebuffer は RAM で HAL の direct map の中なので外す mapping は無い → 0。注: HAL の `hal_fatal` も `rpi_cons_output_suspended()` を見て描かない事を段 8 で確かめる（見なければ `EBUSY`） |
| x68k | `console_end()`（`x68k-console.c`） | text の VRAM `[0x00e00000, +0x80000)`（4 plane。範囲の定数は HAL の bsp の header に有り driver から見えないので、driver に同じ値の定数を置く） | HAL の text screen を止める口が無い → `ENOTSUP`（x68k に PCI は無く、GHCI は呼ばない。重なる事は無い） |
| VF2 | `vf2_text_end()`（`visionfive2.c`、`vf2_text_ops.end` に置く） | `riscv64.framebuffer` の物理と大きさ（名前が無ければ HAL は描いていないので候補無し） | HAL の fbcons を止める口も、その mapping を外す口も kernel に無い → `ENOTSUP`（範囲を保つ）。止めるには kernel が serial だけの `kernel_putc` を公開して HAL を黙らせ、HAL の device window の画面の map を外す口が要る（v1 の外、§8.1） |
| serial だけの板 | `serial-console.c` の表に `.end = console_end`（常に `ENOENT`） | 無し（表示を持たない） | — |

- PCIe の GHCI が付く板では、console の表示の memory は全部 GHCI の BAR の外（PC-98 の legacy・Cirrus の固定の番地、rpi4 の VideoCore の RAM、VF2 の SoC の DRAM、serial の板の RAM）なので、実際の答えは `ENOENT` で、GHCI は console を止めずに範囲を返す。重なった時の道は host 試験だけで通す。

### 4. 状態遷移

PC/AT の text 層（`text_ended` は終端。PC-98 も同じ形）:

```
(無) --init--> READY --suspend--> SUSPENDED --resume--> READY
  READY・SUSPENDED・HIDDEN(quiet) --end(重なる)--> ENDED
  どれでも --end(重ならない)--> 変わらない（ENOENT）
  ENDED: text_ready = 0、splash 停止、text_splash_ended = 1、
         text_pixels・text_vram・geometry・surface の種類は保つ（pointer は NULL にしない）
         putc・write・clear・set_cursor は cell と cursor を更新し、描かない（draw_cell・vga_cursor は text_ready 0 で何もしない）
         suspend は今と同じ（text_ready 0）、resume・reveal・reveal_fatal は text_ended を見て live の surface に書かない
         snapshot は RAM に描く（間だけ text_ready 1・text_pixels を RAM に、終わりに戻す。今と同じ）
```

/dev/graphics（PC/AT・PC-98・generic）:

```
(未登録: graphics_lock_ready 0) --end--> ENDED（lock 無しで backend の end、結果を覚える）
IDLE --open--> OWNED --ENTER--> ENTERED --close/leave--> IDLE
  どれでも --end--> ENDED: open・ioctl は ENODEV、close は owner を外すだけ（resume しない）、restore_text は何もしない
```

backend（PC/AT）: `PREPARED（linear を map = HAL から委譲された窓、または handoff だけ）--end--> ENDED（ready 0、linear NULL、委譲された窓と動的な view の mapping 解除済み、framebuffer_moved は何もしない）`。

HAL の early console の窓（amd64）: `HAL が持つ --kernel_putc の公開--> HAL は描かない --map_device(handoff の範囲, UC)--> 委譲（参照 1）--unmap_device--> 外れた（PTE 無し、HAL の framebuffer_pixels NULL、以後の map_device は動的な view）`。

GHCI の boot display:

```
boot_take: scanning = kept = console = 1, end_due = end_tried = 0
taken（最初の自分の image）: scanning = 0
   kept && !console          → release = 1（今と同じ）
   kept && console && !tried → end_due = 1
scan_flush（service 以外、device.lock と display_lock の下）:
   release && !boot_in_use                       → kept = 0、全 lock の後で範囲を返す（今の道に boot_in_use を足す）
   release && boot_in_use                        → release = 0（次の自分の image が取られた時に taken が立て直す）
   end_due && present && boot_reserved && bar_count → end = 1, end_due = 0, end_tried = 1
   end_due && (!boot_reserved || !bar_count)     → end_due = 0（呼ばない、今と同じ）
   全 lock を離した後 end なら boot_end():
     r = kern_text_end(bars, bar_count)
     r == 0 または ENOENT → device.lock・display_lock を取り直す
         present なら r == 0 の時だけ device の register を 1 つ読む（L4）
         console = 0
         kept && present && !boot_in_use → kept = 0、lock を離して範囲を返す
         kept && present && boot_in_use  → 何もしない（boot の image が取られ、次に自分の image が取られた時 release = 1）
         !present                        → 何もしない（detach が allocator ごと捨てる）
     他（ENOTSUP・EBUSY・EINVAL・unmap の errno）→ 範囲は保つ（今と同じ）、log 1 行
```

### 5. 資源の所有と寿命

- firmware の framebuffer の物理: UMA は HAL が永久に予約（1.3）。discrete は GHCI の local memory で、`boot_reserved` の間は allocator の外。`kern_text_end()` が 0 か `ENOENT` を返し、さらに最初の自分の flip が取られ boot の image が使われていない時にだけ allocator に戻る。
- kernel の mapping（console の層）: backend が prepare から end まで持つ（今の物、`linear_retired[]`、HAL から委譲された窓）。end で全部 `kern_device_unmap`。BAR の移動で残す古い mapping は、今は永久に残る（`backend.c:1198-1204`）が、表に入れて end で消す（表が満ちたら今と同じく残し、end は `EBUSY` を返す）。kernel の pointer のうち backend の `linear_pixels` は NULL にし、text 層の `text_pixels` は値を保つが `text_ready`・`text_ended` で使わない（L2、§6）。
- kernel の mapping（HAL）: early console の窓は §11 の実装で kernel に委譲され、end で外れる。固定の窓（`space.c:2750-2757`）に入る framebuffer は、その窓が他の device と共有の large page なので HAL が恒久に持ち、外せない（`HAL_ERR_BUSY` → `EBUSY` → GHCI は範囲を保つ）。
- text の cell・geometry: text 層が永久に持つ。end の後も putc 等で更新する（L1、Future Work の scan-out の切り替えと venus の snapshot が今の文を見るため）。
- /dev/graphics の file: owner は close まで残るが、何もできない。
- GHCI の `bars[]`: glue が start の前に書き、`ghci_device_stop()` まで変えない。`boot_end()` は driver の op（present・close・wait）の中から呼ばれ、drv_gpu の core は op の間 device を消さない（段 5 で `gpu.c` の withdrawal の道を確かめる）。

### 6. 並行性と lock

- 呼ぶ文脈: thread、割り込み可、lock 無し、sleep してよい（`graphics_lock` の mutex と shootdown の ack の待ち）。割り込み・panic・service thread からは呼ばない。
- 重なりの判断: backend の `boot_framebuffer`・`linear_physical`・`linear_bytes`・`linear_pixels` は起動の中（prepare と `framebuffer_moved`、user の空間の前）だけで書かれ、その後は読むだけ。GHCI の最初の flip は user の present の後なので lock 無しで読む（end の中の `linear_pixels` の NULL は、重なりの判断が handoff の範囲も見るので結果を変えない）。text の surface の種類は init の後変わらない。他の板の候補（名前付きの値・固定の番地・`fb_pixels` の範囲）も起動の後変わらない。
- 順: `drv_pcat_text_end()` は `text_lock`（spin、137）を取り「終わり」を書いて離す → `drv_pcat_graphics_end()` が `graphics_lock`（mutex、125）を取る → `kern_device_unmap` → HAL の registry の lock と shootdown → `graphics_lock` を離す。text_lock を持ったまま graphics_lock を取らないので、今の順（ENTER の graphics_lock → text_lock、`pcat-graphics.c:239`）と逆にならない。kern_logf は全部の lock を離してから。PC-98・generic も同じ順。
- text の書き手（putc・write・clear・set_cursor・ticker・reveal・progress）は全部 `text_lock` の下で `text_ready` を見る（1.2）。end は lock の下でまず `text_ended` を atomic に 1（release）にしてから `text_ready = 0`、VGA なら CRTC の cursor を今の suspend と同じに消し（:820-824）、`drv_pcat_splash_stop()`・`text_splash_ended = 1`。lock を離した時点で以後の書き手は描かない（cell は更新する）。ticker は次の回で `text_splash_ended` を見て終わる（:1034-1046）。
- `text_pixels` を NULL にしない（L2）: `reveal_fatal` の lock 無しの道は `draw_cell_locked()` を通して画素ごとに `text_pixels` を読む（:340）。end が同時に NULL にすると panic の CPU が NULL に書く。止めるのは `text_ended` だけにする。
- `reveal_fatal` の lock 無しの道: 最初に `text_ended` を acquire で読み、1 なら `text_ready` を 1 にせず、全画面の消去・redraw・`vga_cursor_locked` をしない（splash の停止と `text_hidden = 0` は今まで通り）。panic の CPU が `text_ended` を 0 と読んで描き始め、同時に別の CPU が end を進める狭い窓では、end の CPU の shootdown が panic の CPU（割り込みを止めている）の ack を待ち続けるので解除も GHCI の解放も起きない（memory の破壊は無い、機械は止まる）。受け入れる残り（§8）。
- HAL の窓の解除: HAL の console が窓を使うのは `hal_putc` の early の道（`kernel_putc` が NULL の間）と prekern の init だけ（1.3）。end は `kernel_putc` が text 層の物である事を確かめてから進む（3.2 の 3）ので、外す時に HAL の console が窓に書く道は無い。HAL は外す前に自分の `console_output_lock()` の下で `framebuffer_pixels = NULL` にする（同じ CPU の再入や将来の呼び手が外れた窓に書かない）。PTE の消去と shootdown は `device_window_unmap` と同じ（registry の lock の外で `shootdown(HAL_SPACE_SYS, NULL, 0)`、global の entry も消える）。
- /dev/graphics の描画（ioctl）は全部 `graphics_lock` の下で `linear_pixels` を使う（1.2）。end は同じ lock の下で ENTER を捨て `graphics_ended = 1`、backend が pointer を NULL にしてから unmap。lock を離した後の ioctl は `ENODEV`。
- `graphics_lock_ready` が 0 の時（L3）: `drv_graphics_device_restore_text()` の `!graphics_lock_ready` の道（`pcat-graphics.c:96-103`）に倣い、lock を取らずに `graphics_ended = 1` と backend の end をする。この時は /dev/graphics が未だ登録されていない（登録は VFS の init の中、`vfs.c:451-457`）ので owner も ENTER も無く、他の書き手は起動の `framebuffer_moved` だけ（user の空間の前）。後の登録で lock ができた後の open は `graphics_ended` を見て `ENODEV`。GHCI の最初の flip は user の present の後なので、この道は実際には通らない（試験 13.2 の (d) で通す）。PC-98・generic も同じ。
- `framebuffer_moved` は lock を持たない（`backend.c:1205-1234`）。呼ぶのは起動の PCI の attach の中（1.2）で、GHCI の最初の flip（user の present の後）と重ならない。念のため冒頭で `backend_ended` を見て何もしない。重なりを防ぐ lock は足さない（`graphics_lock` はその時まだ初期化されていない事がある、`pcat-graphics.c:118-129`）。
- 2 つの GHCI device が同時に呼ぶ: 重なるのは高々 1 つ（console の表示の memory は 1 つの BAR の中）。他は `ENOENT`。text の end は lock の下で冪等、/dev/graphics の end は lock の下で冪等で最初の結果を返す。
- rpi4: `rpi_cons_suspend_output()` は HAL の output lock の下で印を書く（`cons.c:184-195`）。driver の「戻さない」の印は atomic で、`console_resume` が読む。
- GHCI: `boot_end()` は `scan_flush` が `device.lock`・`display_lock` を離した後（`display-scan.c:2575` の後）で呼ぶ。`end_due` の取り出しと `end_tried` の印は lock の下なので 1 度だけ。結果を戻す時に `device.lock` → `display_lock` を今と同じ順に取り直し、`present`・`boot_in_use` を取り直した後の値で見る（lock を離している間に detach・lease の終わりの点け直しが起き得る、§8）。service（`scan_flush(device, 1)`）は呼ばない（今の解放と同じ。service は眠らない、p012 の H1）。`ghci_scan_flush()` の呼び手は今も lock を持たない約束（`ghci_memory_boot_release` の約束、`memory.c:568-570`）で、drv_gpu の core も op の周りに mutex を持たない（1.5、L5）。段 5 で全部の呼び手を読み直し、stub の lock の数え 0 で確かめる。

### 7. 割り込み・DMA・panic

- 割り込みの文脈の変更は無い。`kernel_putc` の道（割り込み・panic から呼ばれ眠らない、`hal.h:1541-1548`）は今と同じ spinlock だけ。
- DMA: boot の範囲を返すのは、表示の controller が自分の image を走査し始めた（最初の flip が取られた）後で、boot の image の flip が置かれても取られてもいない時だけ（`boot_in_use` が 0）。
- console の CPU の書き込みの順（L4）: end の shootdown の完了で CPU からの新しい書き込みは止まるが、UC の mapping で出した posted write が PCIe の道にまだ居るかもしれない。`kern_text_end()` が 0 を返した後、範囲を返す前に GHCI は自分の register を 1 つ読む（`ghci_read(device, GHCI_MAGIC)` など副作用の無い物。`present` の時だけ、`device.lock` の下）。PCIe の順序（読みの要求は前の posted write を追い越さない）で、console の最後の書き込みは allocator に返す前に device に届く。`ENOENT` では console はこの device に書いていないので読まない。
- panic（v1 は表示無し）: end の後の `kern_fatal` → `reveal_fatal` は framebuffer に触らない（§6）→ `hal_fatal` → `kernel_putc` → text 層の `putc` は serial の mirror と cell だけ。ring と debug port にも残る（1.6）。HAL の窓は外れているが、HAL は handover の後その窓を使わない（1.3）。`ghci_display_panic` は呼ぶ物が無いまま（Future Work）。`ENOENT` の時は console は今のままなので、別の device の上の panic の表示（BUG-158）も今のまま。

### 8. 失敗と回復

| 場面 | `kern_text_end()` | console | GHCI |
| --- | --- | --- | --- |
| PC/AT、console の framebuffer が GHCI の BAR の中、kernel の text が `kernel_putc` を持つ、固定の窓の外 | 0（委譲された HAL の窓も外れる） | 終わり（serial・klog・cell だけ） | 範囲を返す、log「boot framebuffer given back」 |
| PC/AT、console の framebuffer が別の device（QEMU の stdvga、i915・amdrdna の GOP、legacy の VGA の text、handoff 無し） | `ENOENT`、何も変えない | 今のまま（panic の表示も） | console を止めずに範囲を返す（Q6）、log「boot framebuffer given back (console elsewhere)」 |
| PC/AT、重なり、`kernel_putc` が `kern_text_kernel_putc` でない（HAL がまだ early console を描く。text の init が失敗した等） | `EBUSY`、何も変えない | 今のまま | 範囲を保つ、log 1 行 |
| PC/AT、重なり、framebuffer が固定の窓（0xf0000000〜0xf1000000）の中 | `EBUSY`（HAL の窓は外れ、固定の窓は残る） | 終わり | 範囲を保つ（固定の窓の mapping が残るため） |
| PC/AT、重なり、unmap の失敗（`pmem.c` の errno）・`linear_retired[]` が満ちていた | その errno・`EBUSY`（2 度目も同じ） | 終わり（描かない、mapping は残り得る） | 範囲を保つ |
| PC/AT、linear の map が失敗し VGA の text、handoff の GOP が BAR の中（quiet boot の splash は HAL の窓で GOP に描いていた） | 0（text と splash を終え、委譲された HAL の窓を受けて外す。legacy の窓は消さない） | 終わり | 範囲を返す |
| PC/AT、BAR が動き `framebuffer_moved` が新しい所を map できなかった（`linear_physical` も handoff も古い所） | `ENOENT` | 今のまま（古い所に描く。GHCI の memory には届かない） | 範囲を返す |
| PC-98・sun4u・rpi4・x68k・VF2・serial の板で重ならない（GHCI が付き得る全部の実際の場面） | `ENOENT` | 今のまま | 範囲を返す |
| VF2・x68k で重なる（host 試験だけ） | `ENOTSUP` | 今のまま | 範囲を保つ |
| PC-98 で重なる（host 試験だけ） | `EBUSY`（text の VRAM の固定の番地が残る） | 終わり | 範囲を保つ |
| 表が無い（console 未公開） | `ENOTSUP` | 今のまま | 範囲を保つ |
| 引数の誤り（GHCI の `bar_count` 0 は呼ばないので起きない） | `EINVAL` | 今のまま | 範囲を保つ |
| GHCI が UMA（`boot_reserved` 0） | 呼ばない | 今のまま | 今のまま |
| flip が取られる前の detach・LOST の後の detach | 呼ばない（`present` 0） | 今のまま | detach の今の扱い |
| `boot_end` が lock を離している間に lease の終わりが boot の image を点け直した（`flips[0]` に boot、または `boot_scanning` 1） | 0 か `ENOENT` | 終わり・今のまま | `console = 0`、`kept = 1` のまま。次に自分の image が取られた時 `boot_release`、次の flush で返す |
| `boot_end` が lock を離している間に detach（`present` 0） | 0 か `ENOENT` | 終わり・今のまま | `console = 0`、範囲は返さない（detach が allocator ごと捨てる。phase011 の「解放の前の detach」） |
| 範囲を返した後に lease の終わり・detach | — | — | `kept` は 0 なので点け直さず dark（8.1 の 2） |

- 回復は無い（戻らない）。GHCI は 1 度だけ試し（`end_tried`）、0・`ENOENT` 以外なら boot の範囲を今と同じに保つ（ユーザーの決定の「通知を断られたら範囲を保つ」）。

#### 8.1 既知の制限（v1）

1. GOP が GHCI の上にある機械では、end の後 console は画面に何も出さない（serial と klog だけ）。panic の表示も無い（ユーザーの決定、Future Work の panic の hook）。
2. 範囲を返した後（0 でも `ENOENT` でも）は `boot_kept` が 0 なので、Keiland（lease の持ち主）が終わると GHCI の画面は dark のまま（`detach_step` の「dark, unless it is」、`display-scan.c:2144-2149`）。Future Work の「/dev/gpu0 の scan-out を console の backend に切り替える」までの制限（L6）。
3. amd64 で framebuffer が固定の窓（0xf0000000〜0xf1000000）に入る機械は、その窓を外せないので範囲を保つ（`EBUSY`）。外すには HAL の実装でその窓の 2 MiB の page を 4 KiB に分ける要がある（Future Work 候補）。
4. VF2・x68k は console の表示が GHCI の BAR と重なる事が無いので `ENOENT` で解放される。重なった時に止める口（VF2 の HAL の fbcons を止め、その画面の map を外す）は無い（`ENOTSUP`）。
5. PC-98 の text の VRAM の固定の番地は HAL の低い memory の map で、外せない（重なる事は無い）。
6. PC/AT の同じ機械で GOP が GHCI の上にあり、i915・amdrdna も居る時、end の後の彼らの `kern_text_resume` は何も描かない（console の表示の memory はもう GPU の client の物。描かないのが正しい）。

### 9. GHCI の側

- `boot_console = 1` の置き換え: `boot_take()` は今まで通り 1 にするが、0 にするのは `kern_text_end()` が 0 か `ENOENT` を返した時だけ（試験の `boot_console = 0` の直書きは stub の結果に替える）。
- 呼ぶ時: `taken()` で最初の自分の image が取られ、boot の範囲が allocator の外にあり（discrete、`device->boot_reserved`）、BAR の範囲がある（`bar_count` > 0）時に `end_due`。実際の呼び出しは次の service 以外の `ghci_scan_flush()` の終わりで、lock 無し。その後 `ghci_memory_boot_release()`（lock 無し、`memory.c:568-570` の約束通り）。
- 渡す範囲: `bars[0..bar_count)` をそのまま `struct kern_text_range` に写す（BAR0 の全体と BAR2 の全体）。boot framebuffer の CPU の物理を window から計算しない（§15）。
- UMA は呼ばない: 返す範囲が無く（HAL が永久に予約）、console を止める利点が無いので今の振る舞いを保つ（Q3、ユーザーの答えで推しのまま）。
- 今の解放の道（`boot_release`、`scan_flush` :2557-2561）にも `boot_in_use` の確かめを足す（boot の image の flip が置かれた後に範囲を返さない。§4）。
- detach・停止・panic の `boot_kept` の条件（`display-scan.c:356`・:667・:2136）は変えない。
- log: 成功「ghci: %s: boot framebuffer given back」（今の文、`display-scan.c:2580`）、`ENOENT`「ghci: %s: boot framebuffer given back (console elsewhere)」、失敗「ghci: %s: boot framebuffer kept: the console cannot leave it (%d)」。
- 出典の block の comment（`Procedure referenced: amaterrace-gpu d9252e4 docs/ghci.md (section 13.3 …)`、`display-scan.c:2515-2516`）はそのまま。

### 10. platform ごと

| platform | text の表 | end | GHCI | 備考 |
| --- | --- | --- | --- | --- |
| amd64（pcat） | `pcat_text_ops` | 鎖の全部 + HAL の窓の委譲（§11） | 有り（`plan/ws290/tests/config-amd64-ghci.mk`） | GOP が GHCI の上なら 0、他は `ENOENT` |
| i386（pcat） | 同じ file | 同じ code（HAL の窓は無い） | 無し | build と host 試験 |
| PC-98（i386） | `pc98_text_ops` | 鎖の全部（3.4） | 無し | build と host 試験 |
| sparcv9 sun4u | `sun4u_console_ops` + generic の front end | 鎖の全部（3.4） | 無し | ユーザーの物（main の checkout でユーザーが編集する）。段 7 の着手の前に Q1 がユーザーと file の扱いを決める（Q8） |
| rpi4 | `rpi4_console_ops` | 表だけ（HAL の既存の suspend を戻さない形で） | 無し（PCIe はあるが GHCI の config は無い） | build と host 試験 |
| x68k | `x68k_console_ops` | 表だけ（重なれば `ENOTSUP`） | 無し | build |
| riscv64 VF2 | serial の写し + `reveal_fatal` + `end` | 表だけ（重ならなければ `ENOENT`、重なれば `ENOTSUP`） | 有り（`plan/ws290/tests/config-visionfive2-ghci.mk`、p014 の機材） | p014 で `ENOENT` → 解放の道を実機で確かめる（13.5） |
| serial だけの板 | `serial_console_ops` | 常に `ENOENT` | — | build |

- firmware の framebuffer の無い PC/AT（BIOS の VGA の text だけ）: 重なるのは legacy の VGA の窓だけで、GHCI の BAR とは重ならない（GHCI は VGA の class でない、1.7）ので `ENOENT`。

#### 10.1 複数の GPU（H1）

| 構成 | console の表示の memory | GHCI の `kern_text_end` | 他の driver |
| --- | --- | --- | --- |
| QEMU の guest に KC705 の GHCI を渡す（`config-amd64-ghci.mk`、GOP は QEMU の stdvga） | stdvga の BAR | `ENOENT` → 範囲を返す、console は描き続ける | panic の表示（BUG-158）も今のまま |
| amd64 で GOP が i915・amdrdna の上、GHCI も居る | i915・amdrdna の BAR | `ENOENT` → 範囲を返す | i915・amdrdna の suspend・resume は今のまま |
| amd64 で GOP が GHCI の上、i915・amdrdna も居る | GHCI の BAR2 | 0 → console を終え範囲を返す | 彼らの resume は何も描かない（8.1 の 6） |
| GHCI が 2 枚 | 1 枚の BAR2 か別の device | その 1 枚だけ 0、他は `ENOENT`（両方とも範囲を返す） | — |
| VF2 + KC705（p014） | SoC の DRAM | `ENOENT` → 範囲を返す | powervr の lease は今のまま |

### 11. HAL

- **ユーザーの答え（Q1）**: 「HALはそのマッピングをカーネルに委譲しています。HAL APIは追加せず、カーネルがアンマップしてOKです。」 → hal.h に関数を足さず、kernel が既存の口で HAL の early console の窓も外す。
- **code の事実**（1.3）: HAL が kernel に委ねているのは描く事（`hal.h:81-86`）で、page table の entry は HAL の system の page table に残り、kernel にそれを渡す記録は無い。**既存の HAL の関数ではこの窓を外せない**（`hal_space_unmap`・`hal_space_prot` は system の空間を断り、`hal_space_unmap_device` はこの VA を知らず `HAL_ERR_INVALID`）。よって「kernel がアンマップ」には HAL の側の変更が要る。
- **案（推し、Q7 で P3・ユーザーの確かめ）: hal.h の宣言を変えず、amd64 の HAL の実装（`src/hal/amd64/space.c`・`bsp-pcat/cons.c`）で既存の 2 つの関数の振る舞いを足す**:
  1. 起動の page table を作る時（`space.c:587-594`）、HAL は early console の窓を「委譲できる窓」として覚える: 物理 `framebuffer->physical_base`、大きさ `framebuffer->size`、VA `ZBL6_FRAMEBUFFER_VIRTUAL_BASE + (physical − 2 MiB に揃えた所)`、entry の範囲（`system_mmio_pd[AMD64_FRAMEBUFFER_PD_FIRST ..]` と端の表）、状態（HAL が持つ・委譲済み・外れた）。
  2. `hal_space_map_device(paddr, size, attr)`: 窓が「HAL が持つ」状態で、`paddr` が handoff の物理と同じ、`size` が handoff の大きさ以下、`attr` が `READ|WRITE|NOCACHE`（窓と同じ UC）の時、固定の窓の判定より先にその VA を返し「委譲済み・参照 1」にする。2 度目の同じ map は参照を足す。他の要求（大きさ・属性の違い、外れた後）は今と同じ道（固定の窓か動的な view）。
  3. `hal_space_unmap_device(vaddr, size)`: 委譲された窓の VA と同じ大きさなら参照を減らし、0 で HAL の console の `framebuffer_pixels` を自分の output lock の下で NULL にし、窓の PD の entry と端の表の entry を消し、`shootdown(HAL_SPACE_SYS, NULL, 0)` で全 CPU の TLB（global を含む）を消して「外れた」にする。framebuffer の物理が固定の窓（`device_fixed_range` の範囲）にも入っていれば、委譲された窓は外すが固定の窓は外せないので `HAL_ERR_BUSY` を返す（kernel は `EBUSY` を受け、GHCI は範囲を保つ）。
  - hal.h の契約の文の中: `hal_space_map_device` は「HAL が選んだ番地を返す」（`hal.h:774-779`）、`hal_space_unmap_device` は「`hal_space_map_device` で作った物を外す」（:788）。HAL の責務の変化は「handover の後、HAL は early console の窓を kernel に渡し、外れた後は持たない」で、`hal_putc` の契約（handover の後は画面に触らない、:81-86）の延長。guardrail の「`src/hal/` の実装の修正・補完は承認不要」に当たると判断したが、HAL の責務に触れるので Q7 で確かめる。
  - 他の port: early console の窓を kernel の番地に持つ port は amd64 だけ（i386 の HAL は framebuffer を持たない 1.3。sun4u は物理の番地で描く。rpi4・riscv64 の VF2 は direct map か HAL の device window）。他の port の HAL は変えない。
- **残る物と危険（M1、答えの後の版）**: 委譲された窓と console の層の mapping は全部外れる。残るのは (1) framebuffer が固定の窓に入る時のその恒久の mapping（`EBUSY` で GHCI は範囲を返さないので、client の memory を指す mapping は生まれない）、(2) loader の page table の同じ窓（HAL が自分の system の page table に CR3 を替えた後は使われない、と読んだ。段 3a で CR3 の切り替えと loader の表の memory の扱いを確かめる。使われていれば HAL の実装で同じく消す）、(3) PC-98 の text の VRAM の固定の番地（GHCI と重ならない）。よって範囲を返した後に kernel が client の memory を指す書ける mapping を持つ道は、(2) の確かめを除いて無い。
- 前の版の案 B（hal.h に `hal_console_release_display()` を足す差分）はユーザーの答え（「HAL APIは追加せず」）で取り下げ。`plan/ws290/proposed/` には置かない。

### 12. 既存の framework との関係

- drv_gpu の core（`gpu.c`）は変えない。`GPU_DISPLAY_CLAIM` の通知（1.5）はそのまま。
- `kern_text_suspend`・`resume` の意味は変えない。end を呼ぶのは GHCI だけで、重ならない時は何も変えないので、amdrdna・i915・powervr・bcm2711・venus・pcat/pc98/generic の /dev/graphics の今の使い方は同じ道を通る。GOP が GHCI の上の時だけ、end の後に他の driver が resume を呼んでも何も描かない（10.1）。
- cdev: /dev/graphics の node は消さない（file を開いた者がいるかもしれないため）。`ENODEV` で断る。
- `include/kern/graphics-backend.h`（generic の front end）: 末尾に省略可の `end` を足す（3.4、Q2 の答え）。全部の backend の表（今は `sun4u_fb_backend` だけ）は designated initializer か確かめ、そうでなければ段 7 で直す。`:21-28` の「card の driver の乗っ取りは boot の時だけ、firmware の mapping は返さない」の記述に「使用終了の後は返す」を足す。
- PCI: `framebuffer_moved` の古い mapping を end で消すようにするだけ。GHCI の glue は PCI の既存の `drv_pci_device_bar()` と mapping の `physical_address` を読むだけで PCI の API は変えない。
- HAL: hal.h は変えない。amd64 の `hal_space_map_device`・`hal_space_unmap_device` の実装に委譲の窓を足す（§11）。rpi4 は既存の bsp の口（`rpi_cons_suspend_output`、driver が既に使う）だけ。

### 13. 試験の計画

#### 13.1 host（GHCI）: `plan/ws290/tests/ghci-driver-host-test.c`・`ghci-stubs.c`・`ghci-stubs.h`

- stub `kern_text_end()`: 結果（既定 `ENOTSUP`）、呼ばれた数、受けた範囲、呼ばれた時の持っている lock の数が 0 を `expect`。試験は `device.bars[]`・`bar_count` を置く。
- 場面（`scene_display_boot` を直し `scene_display_boot_end` を足す）:
  1. discrete、通知 0: flip が置かれただけでは呼ばない、取られた後の flush で 1 回、lock 無し、範囲は置いた BAR と同じ → register の読みが 1 回（L4）、`boot_kept` 0・`boot_reserved` 0・`boot_count` 0・`window_users` 0。次の flush で 2 回目を呼ばない。detach で dark。
  2. discrete、通知 `ENOENT`: 1 回、register を読まない、範囲を返す（1 と同じ結果）、log の文が「console elsewhere」。
  3. discrete、通知 `ENOTSUP`・`EBUSY`・`EINVAL`: 1 回だけ呼び、範囲を保つ（`boot_reserved`・window 開・`boot_kept`）。次の flush で呼ばない。detach で boot の image を点け直す（今の期待と同じ）。
  4. UMA の boot display・`bar_count` 0: 呼ばない（数 0）、今の期待のまま。
  5. service の flush（`scan_flush(device, 1)` の道）だけでは呼ばない。
  6. 取られる前の detach: 呼ばない。
  7. 2 つの device: 両方 1 回ずつ、それぞれ自分の BAR。
  8. (a) `end_due` が取られた後、lock を取り直す前に lease の終わりが boot の image を点け直す（stub の `kern_text_end` の中で `light(device, &display->boot)` の道を走らせる、または `flips[0]` に boot を置き、別に `boot_scanning = 1` も試す）: 0 → `boot_console` 0・`boot_kept` 1・`ghci_memory_boot_release` を呼ばない。その後 boot の flip が取られ、次に自分の image が取られると `boot_release` 1、次の flush で 1 回だけ返す。
  9. (b) 同じ窓で detach（stub の中で `present = 0`）: `boot_console` 0、`ghci_memory_boot_release` を呼ばない、register を読まない、二重の解放が無い（ASan）。
  10. 今の解放の道（`boot_release`）で、flush の前に boot の flip が置かれた: 返さず、`boot_release` 0、次の自分の image で立て直し。
- glue（`pci-attach.c`）の `bars[]` の埋め方は、BAR の stub（`drv_pci_device_bar`・`map_bar_region` の `physical_address`）を持つ既存の試験が有れば足し、無ければ段 5 で小さな場面を足す（BAR2 の 1 GiB を超える `bar.size` が切られない事）。
- 既存の `ghci-host-test.sh` の全部（words・ref・model・driver・vk・pipeline・execute・meta・gsi1）が PASS。

#### 13.2 host（kernel の側、PC/AT）: 新しい `plan/ws290/tests/text-end-host-test.c`・`text-end-stubs.c`・`text-end-host-test.sh`

- 本物の `src/kern/text-display.c`・`src/drivers/platform/pcat/graphics/{text.c,pcat-graphics.c,backend.c,font.c,vgafont.c,splash.c}` を host で compile し、stub（spinlock・mutex・cdev_register・copyin/copyout・`kern_device_map`/`unmap`（RAM の配列を返し解除を記録、失敗・`EBUSY` を注入）・`kern_boot_framebuffer`（偽の framebuffer、物理の値を置ける）・`drv_pci_*`（Cirrus 無し）・`kern_io_in8/out8`（記録）・`kern_logf`・`kthread_create`・`hal_cpu_current`・`hal_rtc_read_counter`・`drv_pcat_serial_mirror`（記録）・`kern_log_quiet`）。backend.c の依存が重すぎれば backend の stub に替え、`drv_pcat_graphics_backend_end`・`_overlaps` だけ別の小さな harness で試す（実装の段で判断し記録）。
- 場面:
  - (a) end 無し: putc が偽の framebuffer に描く、suspend・resume が戻せる（今の振る舞い）。
  - (b) end（範囲が handoff と重なる）: 0、`kern_device_unmap` が map した番地と大きさで 1 回。後の putc は serial の記録に出て framebuffer の checksum は変わらず、cell は更新される（snapshot の image に後の文字が出る、L1）。resume・reveal・reveal_fatal（lock 無しの道も）が framebuffer に書かず VGA の port にも書かない。`text_pixels` は NULL にならない（L2）。2 回目の end は 0 で unmap を呼ばない。
  - (c) quiet boot・linear の map の失敗・VGA の text・handoff の GOP あり: end の前は putc の改行と ticker が `splash.pixels`（HAL の窓の代わりの RAM）に描く。end（範囲が handoff と重なる）は 0、その後 putc の改行・ticker の 1 回・reveal_fatal で `splash.pixels` の checksum が変わらない、ticker が終わる。backend の end が handoff の範囲で `kern_device_map` と `kern_device_unmap` を 1 回ずつ呼ぶ（委譲の窓の受け取りと解除）。legacy の窓を unmap しない。
  - (d) /dev/graphics: open・ENTER の後に end → FILL が `ENODEV` で framebuffer 不変、close は 0 で resume しない、end の後の open は `ENODEV`、`drv_graphics_device_restore_text` は何もしない。登録の前（`graphics_lock_ready` 0）の end: 0、その後の登録と open が `ENODEV`（L3）。
  - (e) unmap の失敗の注入・`EBUSY`（固定の窓の代わり）の注入: end がその errno、2 回目も同じ errno で unmap を再び呼ばない、console は描かない。
  - (f) 表無し・`end` の無い表 → `ENOTSUP`。引数の誤り（NULL・0 個・5 個・`bytes` 0・桁あふれ）→ `EINVAL`。
  - (g) 重ならない範囲 → `ENOENT`、unmap も状態の変化も無く、その後の putc は描き、suspend・resume・reveal_fatal も今のまま。handoff 無しの VGA の text と、範囲が legacy の VGA の窓と重なる時（0）も。
  - (h) 重なり・`kernel_putc` が別の物 → `EBUSY` で何も変わらない。
  - (i) BAR の移動の後: 範囲が新しい所だけと重なる → 0、古い mapping（委譲の窓）と新しい mapping を両方 unmap。移動の map が失敗した後（`linear_physical` が古い所）、新しい所の範囲 → `ENOENT`。
- 既存の回帰: `plan/ws014/tests/run-text-notification-test.sh`（`text-display.c` を末尾の field 付きで compile）、`plan/ws275/tests/host-graphics-device.sh`（generic の front end、header の変更の影響が無い事）、i915 の `plan/ws237/tests/run-gen9-*-host-test.sh`（`kern_text_suspend` の stub、不変）。

#### 13.3 host（HAL、amd64）

- `space.c` の委譲の窓の部分（覚える・map で返す・unmap で外す・固定の窓の時 `HAL_ERR_BUSY`・外れた後の map は動的な view）を host で試す小さな harness `plan/ws290/tests/hal-fbwindow-host-test.c`（page table を RAM の配列にし、shootdown を記録の stub に）。`space.c` の依存が重すぎる時は、委譲の窓の判定と entry の消去を `space.c` の中の static でない小さな関数に分けて試す（段 3a で判断し記録）。既存の amd64 の HAL の host 試験が有ればそれも PASS（段 3a で `plan/tools.md` から探す）。

#### 13.4 host（他の板）: `plan/ws290/tests/text-end-boards-host-test.c`・`.sh`

- PC-98（`pc98/graphics/{text.c,pc98-graphics.c,backend.c}` と stub）: 重ならない → `ENOENT`、重なる → text と /dev/graphics が終わり、4 plane と Cirrus の窓を unmap、結果 `EBUSY`（text の VRAM の固定の番地）。
- generic の front end + sun4u（`graphics-device.c`・`sun4u-console.c`・`sun4u-firmware-fb.c` と stub）: 重ならない → `ENOENT`、重なる → 0、`fb_pixels` の unmap が 1 回、`end` の無い backend の表 → /dev/graphics は `ENODEV` にして 0（backend に mapping が無い）。既存の `plan/ws275/tests/host-graphics-device.sh` も PASS。sparcv9 の file の扱いは Q8 の後。
- rpi4（`rpi4-console.c` と `rpi_cons_*` の stub）: 重ならない → `ENOENT`、重なる → `rpi_cons_suspend_output` が 1 回、その後の `kern_text_resume` が `rpi_cons_resume_output` を呼ばない。
- x68k・VF2・serial: 重ならない → `ENOENT`、重なる → `ENOTSUP`（x68k・VF2）、serial は常に `ENOENT`。VF2 は名前の無い時も `ENOENT`。

#### 13.5 build（warning 0）

`plan/ws290/tests/config-amd64-ghci.mk`、`plan/ws290/tests/config-visionfive2-ghci.mk`、`config/ci/config-amd64.mk`（i915・amdrdna 有り）、`config/ci/config-pcat.mk`（i386 が pcat を共有）、`config/ci/config-visionfive2.mk`（powervr）、`config/ci/config-pc98.mk`、`config/ci/config-rpi4.mk`（bcm2711・rpi4 の表）、x68k の config（段 10 で `config/ci/` から名を確かめる）、sparcv9（sun4u、Q8 の後）。

#### 13.6 QEMU（Q1 が割り当てる。1 本の script `plan/ws290/tests/text-end-qemu.sh`）

- 回帰（end を呼ばない build）: `config/ci/config-amd64.mk` の image を `plan/tools/boot-test.sh`（`uefi-usb`）、i386 を `BOOT_MODE=bios-ide` で login prompt。
- end の道（QEMU に GHCI は無いので試験だけの引き金）: `plan/ws290/tests/config-amd64-text-end.mk`（`config/ci/config-amd64.mk` を include、`ZEDBSD_GRAPHICAL_BOOT := n`、`ZEDBSD_BOOT_KERNEL_MESSAGES := y`、`CONFIG_PCAT_SERIAL_MIRROR := y`、`ZEDBSD_TEST_CPPFLAGS := -DKERN_TEST_TEXT_END_SECONDS=45`（`Makefile:890`））。`text.c` に `#ifdef KERN_TEST_TEXT_END_SECONDS` の kernel thread を置き（Q5 の推しのまま）、text の init から 45 s 後にまず重ならない範囲（handoff の終わりの次の 1 page、backend から `drv_pcat_graphics_backend_overlaps` の相手の値を読む）で `kern_text_end()` を呼び（`ENOENT` を期待）、次に handoff の範囲で呼んで（0 を期待）両方の結果を log。
- script: QMP と gdbstub（`-gdb tcp::<port>`）付きで起動 → `plan/tools/guest/serial.py` で login → 20 s の時点で gdb で `linear_pixels` と `linear_physical` を記録し、**`linear_physical` が `device_fixed_range` の窓（`space.c:2750-2757`）の外で、`linear_pixels` が委譲された HAL の窓（`0xffffffffc2000000` から 2 MiB の中）である事を先に確かめる**（固定の窓なら end は `EBUSY` を期待し「固定の窓」と出す）→ QMP の HMP `gva2gpa <linear_pixels>` が `linear_physical` を返す事を記録 → end の 2 行を serial で待つ → 直後に `gva2gpa` が unmapped（PTE 無し）、gdb で HAL の委譲の窓の状態が「外れた」、`device_mappings`（`space.c:141`）に `linear_physical` の entry が無い → QMP の screendump A → serial で `echo MARKER > /dev/console` → screendump B → A と B が画素で同じ（console が描かない）、serial に MARKER が出る → `plan/ws290/tests/graphics-probe.c`（`--file` で入れる）が /dev/graphics の open で `ENODEV` → gdb で `text_ended` 1。解除は再利用され得る VA を `x/1wx` で読む方法では判定しない（PTE・HAL の状態で見る）。要約だけを出す。console の log では判定しない。
- 未実施になる物: panic の後の画面（host の (b) で代える）、GHCI の上の GOP（13.7）。

#### 13.7 実機

- p014（VF2 + KC705、Q4 の答え）: VF2 の `end` は SoC の DRAM の画面と KC705 の BAR が重ならないので `ENOENT` → GHCI は範囲を返す。p014 の段 3 に足す確かめ（Q1 が実機を割り当てる。1 本の script で SSH から読む）: (1) attach の時の `boot_take` の log で KC705 の DISP2 が点いていたか（点いていなければ boot の範囲は無く、この確かめは「該当無し」）、(2) 最初の client の flip の後に klog（`dmesg`）に「ghci: …: boot framebuffer given back (console elsewhere)」が 1 行、(3) VF2 の HDMI の console（serial と画面）が描き続ける事（画面はユーザーが拾える試験の一覧に置く）、(4) local の allocator の空きが boot の範囲の分増えた事（driver に空きを読む口が有れば。無ければ「未確認」と書き、log の行だけで判定）。
- amd64 + discrete の GHCI（GOP が GHCI の上）での 0 の道の確かめは今の機材の計画に無い。未実施として、ユーザーが拾える試験の一覧に置く（Q1 に頼む）。`config-amd64-ghci.mk` の QEMU の guest（GOP は stdvga）を Q1 が割り当てた時は `ENOENT` の道（console が描き続け、範囲が返る）を確かめられる。

### 14. 実装の段（p017 の中、合計 27 LW）

| 段 | 内容 | path | LW | 受け入れ |
| --- | --- | --- | --- | --- |
| 1 | text-display の範囲の型・重なりの関数・op・`kern_text_end`（引数の確かめ） | `include/kern/text-display.h`・`src/kern/text-display.c` | 2 | ws014 の試験 PASS、amd64 の build の warning 0 |
| 2 | PC/AT の text 層の end（重なり、`text_ended`、cell の更新を続け描かない、resume・reveal・reveal_fatal の条件、splash・ticker の停止）と試験の引き金の `#ifdef` | `src/drivers/platform/pcat/graphics/text.c`・`text.h` | 3 | 13.2 の (b)(c)(f)(g)(h) |
| 3 | PC/AT の /dev/graphics と backend の end（`_overlaps`、`ENODEV`、lock 前の道、`kern_device_map/unmap` への置き換え、`linear_retired[]`、委譲の窓の受け取り、`backend_ended`） | `src/drivers/platform/pcat/graphics/pcat-graphics.c`・`backend.c`・`backend.h` | 3 | 13.2 の (d)(e)(i) |
| 3a | amd64 の HAL の委譲の窓（覚える・map で返す・unmap で外し shootdown・HAL の console の pointer・固定の窓の `HAL_ERR_BUSY`）と loader の page table の確かめ（Q7 の答えの後） | `src/hal/amd64/space.c`・`bsp-pcat/cons.c`、`plan/ws290/tests/hal-fbwindow-host-test.c` | 2.5 | 13.3 PASS、amd64 の build の warning 0 |
| 4 | kernel の側の host 試験（PC/AT） | `plan/ws290/tests/text-end-host-test.c`・`text-end-stubs.c`・`text-end-host-test.sh` | 2.5 | 13.2 の全場面 PASS（ASan・UBSan も） |
| 5 | GHCI（glue の `bars[]`、`boot_end`、`boot_in_use`、field、comment、L4 の読み）と host 試験。`ghci_scan_flush` の全部の呼び手と `gpu.c` の op の呼び方を読み、lock を持たない事を確かめて記録（L5） | `src/drivers/gpu/ghci/ghci.h`・`display-scan.c`・`pci-attach.c`、`plan/ws290/tests/ghci-driver-host-test.c`・`ghci-stubs.c`・`ghci-stubs.h` | 3.5 | 13.1 の場面 PASS、`ghci-host-test.sh` 全 PASS |
| 6 | PC-98 の text・/dev/graphics・backend の end | `src/drivers/platform/pc98/graphics/text.c`・`text.h`・`pc98-graphics.c`・`backend.c`・`backend.h` | 2 | 13.4 の PC-98、pc98 の build の warning 0 |
| 7 | generic の front end の `end` と sun4u の console・firmware fb の end（Q8 の後） | `include/kern/graphics-backend.h`・`src/drivers/generic/graphics-device.c`・`src/drivers/platform/sun4u/sun4u-console.c`・`sun4u-firmware-fb.c`・`.h` | 2.5 | 13.4 の generic・sun4u、ws275 の試験 PASS、sparcv9 の build の warning 0 |
| 8 | rpi4・x68k・VF2・serial の表の `end` | `src/drivers/platform/rpi4/rpi4-console.c`・`src/drivers/platform/x68k/x68k-console.c`・`src/kern/platform/visionfive2.c`・`src/drivers/generic/serial-console.c` | 1.5 | 13.4 の各板 |
| 9 | 他の板の host 試験 | `plan/ws290/tests/text-end-boards-host-test.c`・`.sh` | 2 | 13.4 の全場面 PASS |
| 10 | build の全部と QEMU の script・config・probe、p014 の確かめの手順（実行は Q1 の割当で） | `plan/ws290/tests/config-amd64-text-end.mk`・`text-end-qemu.sh`・`graphics-probe.c`、p014 の段 3 の script への足し（p014 の持ち主と合わせる） | 2.5 | 13.5 の warning 0、13.6 の要約 PASS（QEMU は Q1 の割当の後）、13.7 は p014 で |

- 合計 27 LW（2 + 3 + 3 + 2.5 + 2.5 + 3.5 + 2 + 2.5 + 1.5 + 2 + 2.5）。受け入れの「見積り: 14 LW」と ws.md の表の写しは P3 が直す（この helper は phase.md の設計の節と決定の節だけを直した）。27 LW は 1 つの Phase には大きいので、P3 が分けるなら: p017（段 1〜5 と 10 の amd64・VF2 の部分、GHCI の解放の本体、20 LW）と、新しい Phase（段 6〜9 の他の板、8 LW、段 1 だけに依存）。
- 段 1〜3 は順に依存、段 3a は段 3 と並べられる（結合は段 3 の委譲の窓の受け取り）。段 5 は段 1 の宣言だけに依存（stub で先に試せる）。段 6〜9 は段 1 だけに依存。他の helper が `src/drivers/gpu/ghci/vk/**` と一部の試験を直しているので、段 5 の `ghci-stubs.c`・`ghci-driver-host-test.c` は P3 が持ち主を確かめてから。

### 15. 選ばなかった案と理由

- **範囲を見ない全体の終わり（この設計の最初の版）**: QEMU に KC705 の GHCI を渡す `config-amd64-ghci.mk` の guest（GOP は stdvga）で、GHCI の最初の flip が関係の無い console を永久に止め、BUG-158 の panic の表示を失う。i915・amdrdna も居る機械で彼らの resume が何も描かなくなる（review の H1）。
- **GHCI が boot framebuffer の CPU の物理を window から計算して渡す（p012 の「残り」1 の `kern_framebuffer_leave(physical, bytes)` の案）**: `window_physical + (FB_BASE − LOCAL_BASE)` は、firmware が window を driver と違う形にした時に「重ならない」と誤り、console が描いている範囲を返す（危ない側に倒れる）。BAR の全体は PCI が知る事実で、CPU が device の memory に届くのは BAR を通してだけなので、BAR と重ならない console は device の memory に書けない。重なりの判断は console の側（handoff・今の linear・VGA の窓を知る側）で行う。
- **`ENOENT` でも範囲を保つ**: 安全だが、GOP が別の device の機械（`config-amd64-ghci.mk` の guest、VF2 + KC705 を含む）で boot の範囲が永久に使えない（p012 の「残り」1 のまま）。console はその device に書いていないので返して良い（Q6）。
- **hal.h に `hal_console_release_display()` を足す（前の版の案 B）**: ユーザーの答え（「HAL APIは追加せず」）で取り下げ。
- **kernel が bootloader の header の `ZBL6_FRAMEBUFFER_VIRTUAL_BASE` を読んで `hal_space_unmap_device` に渡す**: kernel が HAL の内部の番地を知る事になり、今の `hal_space_unmap_device` はその番地を知らない（`HAL_ERR_INVALID`）ので HAL の実装の変更は結局要る。既存の `hal_space_map_device` が番地を返す形の方が「HAL が選んだ番地を返す」の契約に合う。
- **固定の窓の framebuffer も外す（2 MiB を 4 KiB に分ける）**: 他の device と共有の窓の page table を起動の後に作り替える事になり、HAL の変更が大きい。framebuffer がそこに入る機械は稀で、`EBUSY` で範囲を保てば安全（8.1 の 3、Future Work 候補）。
- **VF2 の HAL の fbcons を kernel から止める（serial だけの `kernel_putc` を公開する）**: VF2 の console の表示は GHCI の BAR と重ならないので要らず、serial の出力の道を kernel に作る大きな変更になる（8.1 の 4）。
- **drv_gpu の core の `GPU_DISPLAY_CLAIM`（`gpu.c:3841-3847`）で呼ぶ**: 全 GPU driver の振る舞いが変わる。範囲の外。
- **`kern_text_suspend` に「戻さない」の印を足す**: suspend は数えず戻せる前提で 8 か所以上が使う（1.5）。意味を混ぜると resume の側の全部を見直す要がある。
- **`kern_text_register()` で serial の表に差し替える**: PC/AT の serial の mirror は text 層の `putc` の中（`text.c:408`）にあり、差し替えると serial の出力も cell も snapshot も失う。
- **end で `text_pixels` を NULL にする**: `reveal_fatal` の lock 無しの道が画素ごとに読む（:340）ので、競合で NULL に書く。`text_ended` で止める（L2）。
- **end の後は cell を変えない（suspend と同じ）**: snapshot（venus）と将来の scan-out の切り替えが古い文を見る（L1）。
- **backend が `hal_space_unmap_device` を直に呼ぶ**: 今の map は直に呼んでいる（`backend.c:1062`）が、ユーザーの決定の「kernel 内 API → HAL」に合わせ `kern_device_map/unmap` に揃える（HAL の属性は同じ）。
- **/dev/graphics の node を消す・file を強制で閉じる**: kernel に file の revoke の仕組みが無く、mmap も無いので `ENODEV` で足りる。
- **UMA でも通知する**: 返す物が無く、console を止める利点が無い（Q3、推しのまま）。
- **QEMU の引き金を gdb の `call kern_text_end()` にする**: 止めた vCPU の任意の文脈で眠る関数を呼ぶので不安定（Q5、推しのまま）。

### 16. 出典

- 外部の code・値の引用は無い。zedBSD の既存の code（上の `file:line`）と、GHCI の driver に既にある block の出典（`Procedure referenced: amaterrace-gpu d9252e4 docs/ghci.md (section 13.3 …)`、`display-scan.c:2515-2516`、`memory.c:572-574`）だけ。PCIe の順序（読みの要求が前の posted write を追い越さない）は一般の規則で、code の comment に規格の節を引く時は P3 が `plan/guardrail.md` の出典の規則に従う。新しい code の comment に開発の管理情報（WS・Phase の番号など）を書かない（AGENTS.md §10、Guardrail の「出典」）。

### 17. ユーザー・Q1 の判断の項目

答えの出た物（2026-10-28、P3 経由）: Q1（HAL の窓は kernel が外す、HAL API を足さない）、Q2（全部の backend）、Q3（UMA は通知しない、推し）、Q4（VF2 にも、p014 で確かめる）、Q5（試験の引き金を `text.c` に、推し）。設計に反映した（§3.4、§10、§11、§13.7、§14）。

残りの問い:

| ID | 問い | 推し | 他の案 |
| --- | --- | --- | --- |
| Q6 | 使用終了の通知に GPU の BAR の範囲を付け、console の表示がその GPU の上に無い時（`ENOENT`）は console を止めずに GHCI の boot の範囲を返してよいか（ユーザーの鎖は変えず、「どの GPU の使用開始か」を通知に載せる） | よい（QEMU の `config-amd64-ghci.mk` の guest、i915・amdrdna が console を持つ機械、VF2 + KC705 で console と panic の表示を保ち、GHCI の memory も返る） | `ENOENT` でも範囲を保つ（安全側、ただし VF2 + KC705 を含むその機械で boot の範囲は永久に使えない）／範囲を付けない（最初の版。関係の無い console を止める、H1） |
| Q7 | Q1 の答えの実現: 既存の HAL の関数ではその窓を外せない（§1.3）。hal.h を変えず、amd64 の HAL の実装で既存の `hal_space_map_device` が handoff の framebuffer にはその窓を返し、`hal_space_unmap_device` がそれを外す形にしてよいか（`src/hal/` の実装の変更。HAL の責務は「handover の後は窓を kernel に渡す」に変わる） | よい（hal.h の宣言・契約の文は変わらない。+2.5 LW） | HAL の責務の変化とみて hal.h の契約の文に一行足す（承認の差分を `plan/ws290/proposed/` に置く）／窓を外さない（前の版の案 A に戻る） |
| Q8 | sun4u（sparcv9）の file（`sun4u-console.c`・`sun4u-firmware-fb.c`）と generic の front end はユーザーが main の checkout で編集する物。段 7 をどう進めるか | 段 7 を最後に回し、着手の前に Q1 がユーザーに未 commit の変更の有無を聞き、無ければ P3 が worktree で直す | ユーザーが自分で直す（設計を渡す）／sun4u は v1 から外す（generic の front end の `end` だけ足す） |
| Q9 | 27 LW を 1 つの Phase のままにするか | 分ける: p017 は amd64・VF2・GHCI（段 1〜5・10、20 LW）、他の板（段 6〜9、8 LW）は新しい Phase | 1 つのまま |

### 18. design.md への反映（P3 へ）

- §13 の版 4.15 の段落の「v1 は boot framebuffer を解放しない（`boot_console` = 1 …）」と「両方の kernel の API は Future Work の候補」を、この設計（使用終了の鎖に BAR の範囲を付ける、`boot_console` は通知の 0 か `ENOENT` で 0、UMA は呼ばない、VF2 は `ENOENT` で解放、HAL の early console の窓は既存の口で kernel に委譲して外す）に直す（版 4.16）。p012 の phase.md の「残り」1 と「既知の制限」の `kern_framebuffer_leave` の行も「p017 で別の形（§15）」と注記。
- Future Work: GPU の使用の終わりに /dev/gpu0 の scan-out を console の backend に切り替える（ユーザーの決定の後者、8.1 の 2 の dark を解く）、panic の hook（`ghci_display_panic` の呼び手）、固定の窓の framebuffer の解除（8.1 の 3）、VF2 の HAL の fbcons を止める口（8.1 の 4）。

## 記録

- 2026-10-28 P3（q1644-i01）: ユーザーの決定（上）で立てた。
- 2026-10-28 P3 の helper（q1644-i01）: 上の「設計」を書いた（code は変えていない、design-reviewer の review は未、Q1〜Q5 は未決）。
- 2026-10-28 P3 の helper（q1644-i01）: design-reviewer（Opus）の review を反映した。H1（`kern_text_end` に呼び手の BAR の範囲を付け、console の表示が重ならなければ `ENOENT` で何も変えない。GHCI は 0・`ENOENT` で範囲を返す。10.1 に複数の GPU）、M1（§11・§3.1 の契約の文を、消える物と残る物に直した）、M2（GHCI の場面 8〜10、kernel の場面 (c)(g)(i)、QEMU の判定を委譲の窓の確かめと PTE・HAL の状態に）、L1〜L6（cell の更新、`text_pixels` を NULL にしない、lock 前の /dev/graphics、register の読み、lock の確かめを段 5 に、8.1 の既知の制限）。code は変えていない。
- 2026-10-28 P3 の helper（q1644-i01）: ユーザーの Q1・Q2・Q4 の答え（P3 経由、原文は上）を反映した。既存の HAL の関数では early console の窓を外せない事を code で確かめ（§1.3）、hal.h を変えずに amd64 の HAL の実装で既存の map・unmap の口から委譲する形にした（§11、Q7）。全部の板の表に `end` を足し（§1.8・§3.4・§10）、VF2 は `ENOENT` で解放、p014 の確かめを §13.7 に置いた。前の版の案 B と Q1〜Q5 の表を取り下げ、Q6〜Q9 を残した。見積りは 27 LW（分けるなら 20 + 8、Q9。受け入れの 14 LW と ws.md の写しは P3 が直す）。code は変えていない。
