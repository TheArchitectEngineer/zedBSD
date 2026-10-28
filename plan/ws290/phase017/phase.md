<!-- awesome-plan project=zedbsd record=ws290-p017 -->

# ws290-p017 boot framebuffer の使用終了の通知と解放

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §13（版 4.15 の「p012 の実装」の boot framebuffer）と、この phase.md の設計の節（着手で書く）。
- 状態: in-progress（2026-10-28 P3 q1656-i01）
- 依存: p012（表示）、p011（local の allocator と `ghci_memory_boot_release`）

## ユーザーの決定（2026-10-28、原文は ws.md）

「GPUの使用開始は、consoleドライバに通知しています。consoleドライバは、/dev/gprahicsドライバを使ってGOPに描画していると思います。そこで、/dev/graphicsデバイスに使用終了の口を作るのがいいと思います。GPUの使用開始時に、/dev/consoleに使用終了を通知、/dev/consoleが/dev/graphicsに使用終了を通知、/dev/graphicsがカーネル内APIを呼んでマッピング解除を要求。カーネル内APIがHALを呼んでマッピングを実際に解除。ここまではWS290で実装。将来的に、Keilandが終了してGPU使用を終了するときに、/dev/gpu0のscanoutを/dev/consoleのバックエンドに切り替える機能を作るのがいいです。後者は今回はやらず、future workにすればいいです。」

解釈（Q1 の確かめにユーザーが「その理解でよい」）: GPU の使用開始の時に kernel の text-display（`kern_text_*`、console）へ「使用終了」を通知 → text-display が graphics の backend（amd64 は `src/drivers/platform/pcat/graphics/pcat-graphics.c`、他の platform の backend も同じ口）へ通知 → backend が kernel 内 API で framebuffer の mapping の解除を要求 → kernel 内 API が HAL で実際に解除。panic の表示は v1 で無し（serial と klog）。scanout を console の backend に戻す機能と panic の hook は Future Work。

設計の判断の項目への答え（2026-10-28。ユーザーの答えを Q1 が伝えた形の引用。ws.md への記録は P3）:

- Q1（HAL の恒久の窓）: 「HALはそのマッピングをカーネルに委譲しています。HAL APIは追加せず、カーネルがアンマップしてOKです。」
- Q2（実装する platform）: 「全部の backend」
- Q4（VF2）: 「VF2 にも実装」
- Q3（UMA は通知しない）・Q5（試験だけの引き金）: 推しどおり（Q1 が決めた）。
- Q6（console の表示が GPU の BAR と重ならない時）: 「常に止める」（クリック。推しの「重ならなければ止めずに範囲を返す」と違う）
- Q7（HAL の窓の委譲の形）: 「実装だけで直す (推し)」（クリック）
- Q8（sun4u・generic の file）: 「最後に、確かめてから (推し)」（クリック）
- Q9（Phase の分け方）: 「1 つの Phase で」（クリック。推しの「分ける」と違う）
- hal.h の契約の文: 「変えない、実装だけ (推し)」（クリック）
- Q10（x68k）・Q11（serial だけの板）: 「x68k は止める、serial だけは足さない (推し)」（クリック）
- Q12（discrete で boot display が無い時）: 「常に止める」（クリック。推しの「止めない」と違う）
- Q13（全体の決定、2026-10-28 ユーザーの原文）: 「GHCIに限らず、GPUで最初のフリップをしたら、コンソールはフレームバッファをバックにする点では使用終了します。シリアルバックは終了しません。すべてのアーキテクチャ、すべてのGPUドライバでそうです。」

解釈（この helper、P3 の確かめが要る所は §17）:

- Q1・Q7: hal.h の宣言・契約の文を変えず、既存の `hal_space_map_device`・`hal_space_unmap_device` の amd64 の実装（`src/hal/`）で HAL の early console の窓を kernel に渡し、kernel の unmap で外す（§11）。今の hal.h の文はこの振る舞いを既に許す（§11 の「契約の文」）。念のため作った契約の文の差分 [`plan/ws290/proposed/p017-hal-fb-handover.diff`](../proposed/p017-hal-fb-handover.diff) は参考で、当てない。
- Q2: console が画面を持つ全部の板の表に `end` を足す（§10）。
- Q4・Q6: GHCI の最初の自分の flip の後、console は（重なりに関わらず）止まる。VF2 も、kernel の側の手段だけで HAL の画面の console を止める（§3.4）。通知の範囲は「止めるか」を決めず、「止めた後に残る mapping が呼び手の memory に掛かるか」だけを決める（§3.1）。
- Q8: 段 7（sun4u・generic の front end）を最後に回し、着手の前に Q1 がユーザーに main の checkout の未 commit の変更の有無を確かめる。
- Q9: 段 1〜10 を全部 p017 で行う（§14、28 LW）。

## 範囲

1. 設計（code の前、design-reviewer の review を通す）: 今の `kern_text_suspend` の呼び手（amdrdna・i915・powervr など）との関係（suspend と「使用終了」の違い）、他の GPU driver が使わない時に今の振る舞いが変わらない事、`/dev/graphics` の firmware の backend の扱い（使用終了の後の open・mmap・描画）、kernel 内 API と HAL の呼び方（HAL の API の宣言・契約を変える要があれば差分の案を Q1 へ、承認が要る）、GHCI の driver の呼ぶ時（最初の自分の flip が取られた後、lock を持たずに）と `ghci_memory_boot_release` の条件（`boot_console` を「使用終了の通知が成功した」に置き換える）。
2. 実装: kernel の text-display、platform の graphics の backend（amd64 の pcat と、同じ口を持つ他の platform の backend）、kernel 内 API と HAL の呼び出し、GHCI の driver。
3. 試験: host 試験（GHCI の場面: 通知が成功した時だけ解放、失敗・未対応の時は解放しない）と、kernel の側の試験（ある物の範囲で）。amd64・riscv64（GHCI 有り・無し）の build の warning 0。

## 受け入れ

- 設計の review の H が 0。
- 場面 PASS、build の warning 0。他の GPU driver（amdrdna・i915・nvrtx・powervr・bcm2711・venus）は最初の client の flip までは振る舞いが変わらず、最初の flip で console が framebuffer の backend の使用を終える（Q13。build と既存の試験と 13.6 の (c)）。
- 実機（GOP の framebuffer の解除の後に console が描かない事）は p014 か Q1 の割当の QEMU で。
- VF2 の実機の解放の道（§13.7）: U-Boot が KC705 の DISP2 を点けていなければ boot の範囲は無く、その確かめは「該当無し」と記録する（失敗ではない）。
- 見積り: 29.5 LW（§14。立てた時の 14 LW を設計の後に 28 LW、Q13 の全 GPU driver の分 1.5 LW を足した）。

## 設計

2026-10-28 q1644-i01 の P3 の helper が worktree `p3-ws290g`（head 7811b5562）の code を読んで書き、同じ日に design-reviewer（Opus）の review の H1・M1・M2・L1〜L6 と、ユーザーの Q1・Q2・Q4 の答えを反映して直した（head 6250b7f97）。
同じ日に 2 度目の review（Opus、H 0）の M1〜M4・L1〜L8 と、ユーザーの Q6〜Q9 の答えを反映した（head e18c42e75。7811b5562 からの `src/`・`include/` の差は `src/drivers/gpu/ghci/` の ghci.c・layout.h・recover.c と vk/ だけで、ここで引く file ではない事を `git diff --stat` で確かめた）。
code の事実には `file:line`（この head の行）を付けた。source は変えていない。
2026-10-28 q1656-i01 の P3 が Q10〜Q13 と hal.h の文の答えを反映した（版 3: Q13 の全 GPU driver は drv_gpu の core の最初の flip の呼び出し、§3.5）。`gpu.c` の行は worktree `p3-ws290g` の f133a8c51。

### 0. 要旨

- 鎖: GHCI の driver（最初の自分の flip が取られた後、lock 無し）→ `kern_text_end(ranges, count)`（text-display、/dev/console の顔。ranges は呼び手の device が decode する CPU の物理の範囲で、GHCI は自分の BAR0・BAR2 の全体）→ 板の表の新しい省略可の op `end` → 板が console を止める（戻らない）→ /dev/graphics（PC/AT は `drv_pcat_graphics_end()`）が owner の描画を断る → backend が kernel の mapping を忘れる → kernel 内 API `kern_device_unmap()`（既存、`src/kern/pmem.c:169-176`）→ HAL `hal_space_unmap_device()`（既存、`include/hal/hal.h:787-793`）。
- 止めるのは常に（Q6「常に止める」）。ranges は止めるかを決めず、結果だけを決める: 止めた後に外せずに残る mapping・描き手が ranges に掛かれば `EBUSY`、掛からなければ 0。`ENOENT`（前の版の「重ならなければ何も変えない」）は無くした。
- GHCI: 0 → boot の範囲を返す。`ENOTSUP`・`EBUSY`・他 → 範囲を保つ（今と同じ）。
- 「使用終了」は suspend と違い戻らない（v1）。`kern_text_resume`・`reveal`・`reveal_fatal` は終わった console を描かない。cell は更新し続ける（snapshot と将来の scan-out の切り替えのため）。scan-out を console に戻すのは Future Work。
- HAL の early console の窓（amd64、VA `0xffffffffc2000000`）: Q1・Q7 で kernel が外す。hal.h を変えず、amd64 の HAL の実装で、`kernel_putc` の公開の後に handoff の framebuffer と同じ物理・同じ大きさ・UC の `hal_space_map_device()` を 1 度だけその窓で返し、その `hal_space_unmap_device()` が全 CPU から global の entry も含めて外す（§11）。framebuffer が固定の窓（0xa0000〜0x100000、0xf0000000〜0xf1000000、0xfec00000〜0xff000000）に掛かる時は外せず `HAL_ERR_BUSY`（何も外さない）。
- `end` を画面を持つ全部の板の表に足す（Q2・Q4、§10）: PC/AT（amd64・i386）・PC-98・generic の front end と sun4u は鎖の全部。rpi4 は HAL の既存の suspend の口。VF2 と x68k は kernel が `kernel_putc` を公開して HAL の画面の console を黙らせる（VF2 は kernel の UART の出力を足して serial を保つ、x68k は他の出力が無いので klog の ring だけ）。serial だけの板の共通の表は v1 で `end` を持たない（`ENOTSUP`、§17 の Q11）。
- **Q13（全 GPU driver）**: drv_gpu の core（`src/drivers/gpu/gpu.c`）が、どの driver でも最初に成功した client の flip（`GPU_PRESENT`・`GPU_DISPLAY_PRESENT`・`GPU_DISPLAY_COMMIT` の driver の op が 0 を返した時）の後に、lock 無しで 1 度だけ `kern_text_end(NULL, 0)` を呼ぶ（§3.5）。console は全部の architecture・全部の GPU driver（amdrdna・i915・nvrtx・powervr・bcm2711・venus・GHCI）で framebuffer の backend の使用を終え、serial の backend は続く。GHCI は返す memory があるので、加えて自分の latch の後に自分の BAR の範囲で呼ぶ（§3.3、2 度目の呼び出しは最初の結果を GHCI の範囲で判じ直す）。各 GPU driver の source は変えない（amdrdna も）。彼らの `kern_text_suspend`・`resume` は残り、end の後の resume は何も描かない（§3.5・8.1）。
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
- **TLB（2 度目の review の M1）**: early console の窓の entry は global（`framebuffer-map.c:8` の `FB_FLAGS` = NX・G・PCD・W・P、2 MiB の leaf :60、端の 4 KiB の leaf :66）。固定の窓（`space.c:573-585`）と legacy の窓（:601）も global。動的な view の PTE は global を持たない（`space.c:2926`）。BSP は CR4.PGE を立てる（`space.c:643-645`）。AP は立てないと読んだ（`ap-trampoline.S:28-30` は PAE だけ、`amd64_cpu_init` の `asm.c:391-393` は OSFXSR・OSXMMEXCPT）。`shootdown(HAL_SPACE_SYS, …)` は各 CPU で `flush_request_range`（`space.c:2378-2392`）を通り、system の空間・範囲無し・32 page（`AMD64_INVLPG_MAX_PAGES` :89）を超える要求は `asm_flush_tlb()`（CR3 の読み直し、`asm.c:247-255`）だけで、PGE の CPU の global の entry は消えない。同じ HAL の `amd64_pat_configure`（`asm.c:309-361`）は global の entry を消すために PGE を落として戻す。
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
| x68k（m68k） | `x68k_console_ops`（teletype、suspend・resume は何もしない、`x68k-console.c:43-54`。出力は `hal_putc` だけで、入力は無い、:7-13） | HAL の console が text の VRAM（`X68K_TVRAM_PHYSICAL` 0x00e00000、`src/hal/m68k/bsp-x68k/mmio.h:14`、plane 0x20000 ずつ `console.c:32`・:135）に描く。`hal_putc` は `kernel_putc` が公開されれば委ねる（`src/hal/m68k/bsp-x68k/console.c:94`）。kernel の名前付きの値は無い | 公開しない | 無し |
| VF2（riscv64） | serial の表の写し + `reveal_fatal`（`visionfive2.c:342-348`、`reveal_fatal` :1370-1378 は powervr の画面の取り戻し） | HAL の fbcons（`src/hal/riscv64/fbcons.c`）が loader の画面を direct map か HAL の device window で描く（`bsp-visionfive2/board.c:372-432`）。HAL は描くかに関わらず画面を `riscv64.framebuffer.*` と名付ける（:366-395）。画面が無ければ名付けず描かない（:384-386）。HAL の文字の出力は `hal_putc` だけで、`kernel_putc` が公開されていれば委ね、無ければ SBI の putchar と fbcons の両方に書く（`src/hal/riscv64/lib.c:131-147`）。`hal_printf`・`hal_fatal` も `hal_putc` を通る（:311-320、:340-352）。fbcons の描画は `riscv64_cons_putc` と attach の時の redraw だけ（`cons.c:62-91`、`fbcons.c:113-115`） | 公開しない（`serial-console.c:8-14`） | 無し |
| 他の serial の板（`serial-console.c` の表をそのまま使う物） | `serial_console_ops`（:59-70） | 板により HAL が RAM の framebuffer に描く（例 `src/hal/riscv64/bsp-virt/framebuffer.c:68`・:176、`src/hal/m68k/bsp-virt/framebuffer.c:169`）が、どれも PCIe の GHCI の BAR とは別の RAM | 公開しない | 無し |

- kernel の UART の出力: kernel の `src/drivers/serial/ns16550.c` は FDT から UART を見つけて register を `kern_device_map` する（`drv_ns16550_probe`、:115-、map は :175-181、compatible に `snps,dw-apb-uart` :98-102）が、入力だけで送る関数を持たない（:104-109、:197-）。riscv64 の build に入る（`platform/riscv64/vmunix.mk:60`）。riscv-virt は probe して入力に使う（`src/kern/platform/riscv-virt.c:183`・:298）が、VF2（`visionfive2.c`）は使わない。kernel に SBI を呼ぶ口は無い（`src/kern`・`src/drivers` に ecall が無い）。
- `kernel_putc` は一度公開されると NULL に戻す者が無い（書き手は `backend.c:1136`、`pc98/graphics/backend.c:346`、`sun4u-console.c:230` だけ）。全部の port の `hal_putc` は ACQUIRE で読み、公開されていれば委ねる（例 `src/hal/riscv64/lib.c:136`、`src/hal/amd64/bsp-pcat/cons.c:165`）。hal.h の契約は「kernel が `kernel_putc` を公開した後は全ての文字をそこへ委ね、HAL は画面に触らない」（`include/hal/hal.h:80-86`）。

### 2. 責務と境界

| 層 | file | 責務（使用終了） | しない事 |
| --- | --- | --- | --- |
| GHCI の bus の glue | `src/drivers/gpu/ghci/pci-attach.c` | BAR0・BAR2 の CPU の物理の全体（先頭は mapping の `physical_address`、大きさは `bar.size`）を `struct ghci_device` に残す | 解放の判断をしない |
| GHCI の driver | `src/drivers/gpu/ghci/display-scan.c`・`ghci.h` | 返す boot の範囲がある時、最初の自分の flip が取られた後に 1 度だけ lock 無しで自分の BAR の範囲を付けて `kern_text_end()` を呼び、0 なら範囲を返す | console・/dev/graphics・HAL に直に触らない |
| text-display（/dev/console の顔） | `include/kern/text-display.h`・`src/kern/text-display.c` | 引数を確かめ、表の `end` を呼び、無ければ `ENOTSUP`。0・`EBUSY` の時 snapshot の消費者に知らせる | 板の状態・範囲の判断を持たない |
| 板の console（PC/AT・PC-98・sun4u・rpi4・x68k・VF2） | §3.2・§3.4 | console を戻らない「終わった」状態にし、表示の memory に二度と書かない（resume・reveal・reveal_fatal・splash も）。HAL がまだ画面に描いていれば `kernel_putc` を公開して黙らせる。cell は更新し続ける。/dev/graphics がある板は続けて知らせる。外せない物が ranges に掛かるかで結果を決める | mapping を自分で HAL に頼まない |
| /dev/graphics（PC/AT・PC-98・generic） | `pcat-graphics.c`・`pc98-graphics.c`・`graphics-device.c` | lock の下で（lock が未だ無ければ lock 無しで）ENTER の状態を捨て、以後の open・ioctl を `ENODEV`。backend に終わりを渡す。結果を覚えて 2 度目に返す | hardware に書かない |
| backend（PC/AT・PC-98・sun4u） | `pcat/graphics/backend.c`・`pc98/graphics/backend.c`・`sun4u-firmware-fb.c` | end で kernel の mapping を `kern_device_unmap()` で全部消し、pointer を NULL・ready を 0 に。外せなかった物の範囲を返す | VGA・Cirrus の register を書かない |
| kernel 内 API | `src/kern/pmem.c`（既存の `kern_device_map`・`kern_device_unmap`） | HAL を呼ぶ | 変えない |
| HAL（amd64 の実装） | `src/hal/amd64/space.c`・`bsp-pcat/cons.c` | 既存の `hal_space_map_device` が `kernel_putc` の公開の後に handoff の framebuffer ちょうどの要求へ 1 度だけ early console の窓を返し、既存の `hal_space_unmap_device` がその窓の entry を消して全 CPU の TLB を global の entry も含めて消し、HAL の console の pointer を NULL に（§11） | hal.h を変えない。固定の窓に掛かれば外さない（`HAL_ERR_BUSY`） |

- ユーザーの言う「/dev/console」は `kern_text_*`（/dev/console の `src/drivers/generic/console.c` は `kern_text_*` の上の多重化で、画面を持つのは表）、「/dev/graphics」は PC/AT では text 層を含む `src/drivers/platform/pcat/graphics/` の driver（text 層は `text.h:10-14` の通り /dev/graphics が持つ）。
- 「mapping」の意味（L3、全部の板で 1 つ）: console の層か HAL が**その表示の memory のために作った** kernel の番地の対応（amd64 の HAL の early console の窓、`kern_device_map` の動的な view・固定の窓、PC-98 の text の VRAM の固定の番地、VF2 の HAL の device window）。RAM の direct map（rpi4 の VideoCore の RAM、VF2 で画面が direct map の中の時）は全部の RAM の対応で、表示の memory のための物ではないので含めない。外せる mapping は外し、外せない mapping は「残る物」として結果に数える（§3.1）。

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
	 * Optionally ends the console's use of the display for good.  After it
	 * returns 0 or EBUSY nothing of the console (its text, a quiet boot's
	 * splash, a graphics mode the board's /dev/graphics entered, the
	 * reveal of a fatal stop, the hardware layer's early console)
	 * draws on the display again, and every mapping of the display memory
	 * made for the console or handed over by the hardware layer that can
	 * be removed is removed; the console's characters still reach its
	 * other outputs (a serial line, the log), and its cells keep following
	 * them.  Returns 0 when nothing left (a mapping that could not be
	 * removed, a writer that could not be stopped) lies in one of the count
	 * ranges; EBUSY when something left does; the error of a removal that
	 * failed for a mapping in the ranges; or ENOTSUP, changing nothing, when
	 * the board cannot stop drawing.  A later call returns the first
	 * result, judged again against its own ranges.  Called in thread context
	 * with no lock held; it may sleep.
	 */
	int (*end)(const struct kern_text_range *ranges, unsigned count);
```

```c
/*
 * Ends the console's use of the display for good, for a display driver
 * that is about to give the memory behind ranges (the processor physical
 * ranges the driver's device decodes, count of them) to others.  The
 * console stops drawing whether or not its display lies in the ranges.
 *
 * Returns 0 once nothing of the console draws on the display and nothing
 * left of it can write to or keeps a mapping of memory in the ranges;
 * EBUSY when the console stopped but a mapping or writer it could not
 * remove lies in the ranges; ENOTSUP, changing nothing, when no console is
 * published or the board's console cannot stop; EINVAL, changing nothing,
 * for an oversized list or an empty or wrapping range (count 0, with
 * ranges NULL, is a driver that gives no memory back); or the error of a
 * removal that failed for a mapping in the ranges.  The caller holds no
 * lock; the call may sleep.  There is no way back.
 */
int kern_text_end(const struct kern_text_range *ranges, unsigned count);
```

- 範囲を残す理由（Q6 の後の判断）: 止めるかは範囲で決めない（常に止める）が、止めた後に外せない物（固定の窓・PC-98 の text の VRAM・HAL の direct map や device window）が残り得る板がある。それが呼び手の memory に掛かるかは呼び手が範囲を返して安全かの判断そのもので、範囲が無いと「何か残れば常に `EBUSY`」（VF2 は HAL の device window が常に残るので解放されない）か「残っても 0」（危ない側）のどちらかになる。範囲を付ければ VF2・別の device の上の GOP では 0（解放）、GHCI の BAR の上で外せない時だけ `EBUSY`（保つ）にできる。
- `EBUSY`・errno の後も console は止まったまま（戻さない）。止める前に断るのは `ENOTSUP`・`EINVAL` だけ（L1。前の版の「HAL がまだ描く時の `EBUSY` で何も変えない」は、HAL を `kernel_putc` の公開で黙らせる形にしたので無い）。

`src/kern/text-display.c`: `count` が `KERN_TEXT_END_RANGES` を超える・`count` が 0 でなく `ranges` が NULL・ある範囲の `bytes` が 0 か `physical + bytes` が桁あふれ → `EINVAL`。`ops()` が NULL か `end` が NULL なら `ENOTSUP`。他は `table->end(ranges, count)` を呼び、0 か `EBUSY` の時 `text_changed()`（:546-571）で snapshot の消費者を起こしてから結果を返す。lock は取らない（今の他の関数と同じ）。`count` 0（Q13 の core の呼び出し。`ranges` は NULL でよい）は止めて、残る物を判じる範囲が無いので 0（`ENOTSUP` は今と同じ）。重なりの式は 1 つの inline の関数 `kern_text_range_overlaps(ranges, count, physical, bytes)` として header に置き、全部の板が使う（両方とも空でない、桁あふれは前で断る: `a < b + bn && b < a + an`）。

#### 3.2 PC/AT の内部

- `text.h`: `int drv_pcat_text_end(const struct kern_text_range *ranges, unsigned count);`（表の `.end`）。
- `backend.h`: `int drv_pcat_graphics_end(const struct kern_text_range *ranges, unsigned count);`（/dev/graphics の node の分、`pcat-graphics.c` に置く）と `int drv_pcat_graphics_backend_end(const struct kern_text_range *ranges, unsigned count);`（`backend.c`、`graphics_lock` の下か、lock が未だ無い時は lock 無しで呼ぶ）。backend.h は既に text 層と backend の間の口（`get_framebuffer`・`framebuffer_moved`、:51-73）を持つので同じ所に置く。
- 新しい状態: `text.c` `static int text_ended;`（atomic、`reveal_fatal` が lock 無しで読む）。`pcat-graphics.c` `static int graphics_ended;`（`graphics_lock` の下）。`backend.c` `static int backend_ended;`、BAR の移動で残した古い mapping の表 `linear_retired[PCAT_RETIRED_MAX]`（4 個、物理と大きさも持つ）と数、外せなかった mapping の物理の範囲の表 `backend_left[]`（数個）。結果は毎回 `backend_left[]` と ranges から計算する（「後の呼び出しは最初の結果を、自分の ranges で判じ直して返す」）。
- backend の map を `hal_space_map_device(…, READ|WRITE|NOCACHE)`（`backend.c:1062`、:1222）から `kern_device_map(…, KERN_DEVICE_UNCACHED, …)` に替える（HAL の属性は同じ、1.3）。解除と対にして、ユーザーの決定の「kernel 内 API → HAL」の形にするため。
- HAL の early console の窓を外す（Q1・Q7、M3）: prepare の map は `kernel_putc` の公開（`text_console_start()`、`backend.c:1133-1139`）より前なので、§11 の後も HAL は窓を渡さず、今と同じ動的な view か固定の窓を返す。backend の end は、自分の mapping（`linear_pixels`、`linear_retired[]`）を全部 `kern_device_unmap` した後、handoff が有れば `kern_device_map(handoff の物理, handoff の大きさ, UNCACHED)` で HAL から窓を受け（`kernel_putc` は公開済み、下の 3）、直ちに `kern_device_unmap` で外す（受ける事が委譲の受け取り。linear の map の成否・BAR の移動に関わらず同じ 1 つの道）。HAL が窓を渡さなかった（固定の窓に掛かる・既に外れた）時の `kern_device_map` は今と同じ道で map を返すので、それも直ちに外す（固定の窓なら外れず残る、下の表）。外せなかった物（unmap の失敗、固定の窓、`linear_retired[]` が満ちて残した古い mapping）の物理の範囲を `backend_left[]` に入れる。

`drv_pcat_text_end()` の順:

1. （版 4、review 3 の H1）先に戻らない。text の部分（下の 2）だけを冪等にし、3（公開）と 4（`drv_pcat_graphics_end()`）は毎回通す。`drv_pcat_graphics_end()` は `graphics_lock` の下で、`backend_ended` が 0 なら自分が外し、1 なら `left[]` を判じ直す。後の呼び手は lock で 1 人目の仕事の終わりを待つので、1 人目が外し終える前に 0 を受ける事は無い。
2. `text_lock` の下で終わりにする（§4、§6）。離す。
3. `kernel_putc` が NULL（text の init が描ける surface を得ず公開しなかった、`backend.c:1133-1139`。HAL がまだ early console を描く）→ `kern_text_kernel_putc` を RELEASE で公開する（以後 `hal_putc` は委ね、HAL は画面に触らない、`hal.h:80-86`。text 層の `putc` は終わった状態で serial の mirror と cell だけ）。`kernel_putc` が NULL でも `kern_text_kernel_putc` でもない（PC/AT では起きない）→ `EBUSY`（console は止まっているが HAL の描き手が残り得る）。
4. `drv_pcat_graphics_end(ranges, count)` の結果を返す。VGA の text の surface の時、legacy の窓 `[0xa0000, 0xc0000)`（`text.c:58-60`、固定の窓の中で外せない）は「残る物」として ranges で判じる（GHCI は VGA の class でなく重ならない、1.7）。

#### 3.3 GHCI

- `ghci.h` の `struct ghci_device` の bus の field に `struct ghci_bus_range { uint64_t physical; uint64_t bytes; } bars[2]; unsigned bar_count;` を足す（glue が start の前に埋め、変えない。comment の「bus fields」に足す）。`pci-attach.c` の `map_registers()` と `map_window()` が map に成功した時、`physical_address` と `bar.size`（BAR2 も 1 GiB で切らない全体）を入れる。host bridge の変換は BAR の中で線形なので、BAR の CPU の物理は先頭の `physical_address` から `bar.size` 続く。
- `struct ghci_display`: `boot_console` の意味を「kernel の console がまだ boot の範囲に描き得る（`kern_text_end()` が 0 を返していない）」にし、`int boot_end_due;`（通知が要る）と `int boot_end_tried;`（1 度だけ）を足す。comment（:565-571）を直す。
- `display-scan.c` に static の `boot_end(struct ghci_device *device)`（lock 無しで呼ぶ。§4・§9）と、static の `boot_in_use(const struct ghci_display *display)`（`boot_scanning`、または `flips[0].placed` で `flips[0].image` が boot の image（`taken()` の :1495-1500 と同じ判定、関数に出す））。`#include <kern/text-display.h>`。
- test の stub（`plan/ws290/tests/ghci-stubs.c`・`.h`）: `int kern_text_end(const struct kern_text_range *, unsigned)`（結果を置ける、呼ばれた数、受けた範囲の写し、呼ばれた時に lock を 1 つも持っていない事を `expect`）。`ghci_read` の stub は読んだ register を記録する（L4 の確かめ）。

#### 3.4 他の板（Q2・Q4・Q6）

| 板 | `end` の関数 | 止め方 | 残る物（ranges で判じる） |
| --- | --- | --- | --- |
| PC-98 | `drv_pc98_text_end()`（`pc98/graphics/text.c`）→ `drv_pc98_graphics_end()`（`pc98-graphics.c`）→ `drv_pc98_graphics_backend_end()`（`backend.c`） | text 層は pcat と同じ形で終わる（`text_ended`、cell は更新、`kernel_putc` が NULL なら `drv_pc98_text_putc` を公開）。/dev/graphics は `ENODEV`。backend は 4 plane と Cirrus の窓を `kern_device_unmap`（:416 の失敗の道と同じ呼び方） | text の VRAM `[0xa0000, 0xa4000)` の固定の番地（HAL の低い memory の map、外せない）と unmap に失敗した物 |
| generic の front end（sun4u） | `include/kern/graphics-backend.h` の `struct drv_graphics_backend` の末尾に省略可の `int (*end)(const struct kern_text_range *ranges, unsigned count);` を足し、`drv_graphics_device_end()`（`graphics-device.c`、`graphics_lock` の下、lock 前の道も pcat と同じ）が選ばれた backend の `end` を呼ぶ。sun4u の表に `.end = console_end`（`sun4u-console.c`）、`sun4u-firmware-fb.c` に `drv_sun4u_fb_end()` | console は `screen_on` を 0 に固定し（suspend と違い戻さない）、/dev/graphics を終え、`fb_pixels` を NULL にして `kern_device_unmap`。HAL の early console は handover の後は描かない（`kernel_putc` :230）。HAL は物理の番地で描くので外す mapping は無い | unmap に失敗した物 |
| rpi4 | `console_end()`（`rpi4-console.c`） | 既存の `rpi_cons_suspend_output()` を呼び、以後 `console_resume` は `rpi_cons_resume_output()` を呼ばない（driver の側の印）。HAL の `hal_fatal` も `rpi_cons_output_suspended()` を見て描かない事を段 8 で確かめる（見なければ HAL の描き手が残る → 画面の範囲を「残る物」に数える） | 無し（画面は RAM で direct map の中、L3）。段 8 で hal_fatal が描くと分かれば画面の範囲 |
| x68k | `console_end()`（`x68k-console.c`） | kernel が何も出さない `kernel_putc`（文字を捨てる。klog の ring には klog が自分で残す、`src/kern/klog.c:110-151`）を公開する → `hal_putc` は委ね（`src/hal/m68k/bsp-x68k/console.c:94`）、HAL は text の画面に触らない（`hal.h:80-86`）。x68k に他の出力は無い（`x68k-console.c:7-13`）ので、以後の文字は klog の ring だけ（x68k に PCI は無く GHCI は呼ばない。§17 の Q10） | 無し（text の VRAM は HAL の物理の窓で描き手は止まる）。VRAM の範囲 `[0x00e00000, +0x80000)` の定数は HAL の bsp の header にあり driver から見えないので、残る物の判定には使わない |
| VF2 | `vf2_text_end()`（`visionfive2.c`、`vf2_text_ops.end` に置く） | kernel の UART の出力を `kernel_putc` として公開する → `hal_putc` は委ね（`src/hal/riscv64/lib.c:136-140`）、HAL は SBI の putchar も fbcons も呼ばない。serial は kernel の UART で続く。そのため: (1) `ns16550.c` に送る関数 `drv_ns16550_putc_polled(port, c)`（LSR の THRE を有限回待って THR に書く。IRQ を止めた spinlock の下、`kernel_putc` の約束 `hal.h:1541-1548` に合わせ眠らない。待ちが尽きたら捨てる）を足し、(2) VF2 の platform の init が `soc_tree` で `drv_ns16550_probe` を呼んで port を持つ（入力は始めない）。(3) `reveal_fatal`（:1370-1378）は end の後は何もしない（v1 は panic の表示無し）。UART が見つからなかった時は `ENOTSUP`（何も変えない。serial を失ってまで止めない） | HAL の fbcons の device window（外す口が kernel に無い）か direct map の画面。`riscv64.framebuffer` の物理と大きさが ranges に掛かれば `EBUSY`。名前が無ければ画面は無い |
| serial だけの板 | 足さない（`serial-console.c` の表は `end` が NULL → `ENOTSUP`） | — | — |

- 試験だけで通る重なりの道: PCIe の GHCI が付く板では console の表示の memory は全部 GHCI の BAR の外（PC-98 の legacy・Cirrus の固定の番地、rpi4 の VideoCore の RAM、VF2 の SoC の DRAM）なので、残る物があっても結果は 0。`EBUSY` の道は amd64 で GOP が GHCI の上・固定の窓の時と、host 試験だけ。

#### 3.5 drv_gpu の core（Q13、全 GPU driver）

- 場所（版 4、review 3 の M1。実装 8f560010f）: `src/drivers/gpu/gpu.c` に static の `gpu_console_end(command)` と file-scope の印 `gpu_console_ended`（0 → 1 を compare-and-swap で 1 度だけ）。呼ぶ所は `gpu_ioctl()` の `gpu_ioctl_body()` の後の 1 か所で、`error == 0` かつ command が `GPU_PRESENT`・`GPU_DISPLAY_PRESENT`・`GPU_DISPLAY_PRESENT_SYNC`・`GPU_DISPLAY_COMMIT` の時。ここでは session の admission（`gpu_session_enter`・`gpu_dependency_ioctl` の中の物）も lock も持たない（op の中に置くと admission を持ったまま眠り、同じ fd の他の ioctl と PRESENT_SYNC の fence を遅らせる）。「成功」は ioctl の copyout まで 0 の事（COMMIT_WAIT・ungated の present は adoption の後）。
- 中身: 印が 0 から 1 になった呼び手だけが `kern_text_end(NULL, 0)` を呼ぶ。結果が 0・`ENOTSUP`（serial だけの板など、止める画面が無い）以外なら `kern_logf` を 1 行（`gpu: the text console could not leave the display (%d)`）。結果は flip の成否に混ぜない（flip は成功のまま）。
- 「最初の flip」の意味: user の client が driver に渡した最初の image。driver が自分で点ける boot の image・lease の終わりの点け直し・VRAM の training の一時の停止は flip ではない（core を通らない）。GPU の計算だけで表示を使わない client は呼ばない。
- 既存の `kern_text_suspend`・`resume` の呼び手（1.5 の表: amdrdna の lease の frame と lease の終わりの resume・休みの後の resume、amdrdna の VRAM の training、i915 の bootfb の lease、powervr・bcm2711 の lease、/dev/graphics の ENTER）は変えない。end の前は今と同じに止めて戻す。end の後の suspend は描かない状態のまま、resume は `text_ended` を見て描かない（§4）。よって「lease の終わりに console を戻す」形（amdrdna の lease の終わり・休みの 3 s の後の resume を含む）は、最初の client の flip の後は framebuffer に何も描かない。driver が lease の終わりに console の surface（firmware の framebuffer）を scan-out に戻す所は今のまま（driver の持ち物で、その絵は end の時の最後の console の絵か driver が塗った黒）。scan-out を console の backend に戻すのは Future Work（ユーザーの決定の後者）。
- 2 つの呼び手の順（版 4、review 3 の M2）: GHCI の ungated の present は core の `adoption_wait` の中で latch を見て `ghci_scan_flush()` から `kern_text_end(bars)` を呼ぶので、GHCI の機械では普通は GHCI（範囲付き）が先、core の `(NULL, 0)` が後。別の driver の flip が先なら core が先。どちらの順でも、後の呼び出しは最初に残った物（`left[]`）を自分の範囲で判じ直す（§3.1）。同時の時は H1 の形（graphics_lock で待つ）で 0 を早く返さない。13.2 に両方の順の場面を置く。
- 呼ばない構成: display を持たない GPU（`ops->display` も `ops->present` も無い）は flip の道を通らないので呼ばない。

### 4. 状態遷移

PC/AT の text 層（`text_ended` は終端。PC-98 も同じ形）:

```
(無) --init--> READY --suspend--> SUSPENDED --resume--> READY
  READY・SUSPENDED・HIDDEN(quiet)・NONE（surface 無し）--end--> ENDED（ranges に関わらず）
  ENDED: text_ready = 0、splash 停止、text_splash_ended = 1、
         text_pixels・text_vram・geometry・surface の種類は保つ（pointer は NULL にしない）
         putc・write・clear・set_cursor は（surface が NONE で無ければ）cell と cursor を更新し、描かない
           （draw_cell・vga_cursor は text_ready 0 で何もしない。putc の serial の mirror は今と同じ）
         suspend は今と同じ（text_ready 0）、resume・reveal・reveal_fatal は text_ended を見て live の surface に書かない
         snapshot は RAM に描く（間だけ text_ready 1・text_pixels を RAM に、終わりに戻す。今と同じ）
```

/dev/graphics（PC/AT・PC-98・generic）:

```
(未登録: graphics_lock_ready 0) --end--> ENDED（lock 無しで backend の end）
IDLE --open--> OWNED --ENTER--> ENTERED --close/leave--> IDLE
  どれでも --end--> ENDED: open・ioctl は ENODEV、close は owner を外すだけ（resume しない）、restore_text は何もしない
```

backend（PC/AT）: `PREPARED（linear を map した動的な view か固定の窓、または handoff だけ）--end--> ENDED（ready 0、linear NULL、自分の mapping・retired を解除、HAL の窓を受けて解除、外せなかった物は backend_left[]、framebuffer_moved は何もしない）`。

HAL の early console の窓（amd64）:

```
HELD（HAL が持つ。kernel_putc が NULL の間は描く）
  --map_device(handoff の物理・大きさちょうど, UC) かつ kernel_putc != NULL--> GIVEN（1 つの呼び手）
  GIVEN --unmap_device(その VA・大きさ)、固定の窓に掛からない--> REMOVED（PTE 無し、全 CPU の TLB を global も含め消した、HAL の framebuffer_pixels NULL）
  GIVEN --unmap_device、固定の窓に掛かる--> GIVEN のまま（HAL_ERR_BUSY、何も外さない）
  HELD で kernel_putc が NULL・要求が違う・GIVEN・REMOVED の時の map_device: 今と同じ道（固定の窓か動的な view）
```

GHCI の boot display:

```
boot_take: scanning = kept = console = 1, end_due = end_tried = 0
taken（最初の自分の image）: scanning = 0
   kept && !console          → release = 1（今と同じ）
   kept && console && !tried → end_due = 1
scan_flush（service 以外、device.lock と display_lock の下）:
   release && !boot_in_use                         → kept = 0、全 lock の後で範囲を返す（今の道に boot_in_use を足す）
   release && boot_in_use                          → release = 0（次の自分の image が取られた時に taken が立て直す）
   end_due && present && boot_reserved && bar_count → end = 1, end_due = 0, end_tried = 1
   end_due && !present                             → end_due = 0、end_tried = 1（呼ばない。detach が allocator ごと捨てる）（L7）
   end_due && (!boot_reserved || !bar_count)       → end_due = 0（呼ばない、今と同じ）
   全 lock を離した後 end なら boot_end():
     r = kern_text_end(bars, bar_count)
     r == 0 → device.lock・display_lock を取り直す
         present なら device の register を 1 つ読む（L4）
         console = 0
         kept && present && !boot_in_use → kept = 0、lock を離して範囲を返す
         kept && present && boot_in_use  → 何もしない（boot の image が取られ、次に自分の image が取られた時 release = 1）
         !present                        → 何もしない（detach が allocator ごと捨てる）
     他（ENOTSUP・EBUSY・EINVAL・unmap の errno）→ 範囲は保つ（今と同じ）、log 1 行
```

### 5. 資源の所有と寿命

- firmware の framebuffer の物理: UMA は HAL が永久に予約（1.3）。discrete は GHCI の local memory で、`boot_reserved` の間は allocator の外。`kern_text_end()` が 0 を返し、さらに最初の自分の flip が取られ boot の image が使われていない時にだけ allocator に戻る。
- kernel の mapping（console の層）: backend が prepare から end まで持つ（今の物、`linear_retired[]`）。end で全部 `kern_device_unmap`。BAR の移動で残す古い mapping は、今は永久に残る（`backend.c:1198-1204`）が、表に入れて end で消す（表が満ちたら今と同じく残し、`backend_left[]` に数える）。kernel の pointer のうち backend の `linear_pixels` は NULL にし、text 層の `text_pixels` は値を保つが `text_ready`・`text_ended` で使わない（L2、§6）。
- kernel の mapping（HAL）: early console の窓は end の中で kernel に渡り（`kernel_putc` の公開の後に 1 度だけ）、直ちに外れる。固定の窓に掛かる framebuffer は、その窓が他の device と共有の large page なので HAL が恒久に持ち、外せない（`HAL_ERR_BUSY` → `backend_left[]` → ranges に掛かれば `EBUSY` → GHCI は範囲を保つ）。
- VF2 の UART の port: platform の init が probe して持ち、以後変えない（kernel の mapping は `kern_device_map` の動的な物で、終わりは無い）。
- text の cell・geometry: text 層が永久に持つ。end の後も putc 等で更新する（L1、Future Work の scan-out の切り替えと venus の snapshot が今の文を見るため）。
- /dev/graphics の file: owner は close まで残るが、何もできない。
- GHCI の `bars[]`: glue が start の前に書き、`ghci_device_stop()` まで変えない。`boot_end()` は driver の op（present・close・wait）の中から呼ばれ、drv_gpu の core は op の間 device を消さない（段 5 で `gpu.c` の withdrawal の道を確かめる）。

### 6. 並行性と lock

- 呼ぶ文脈: thread、割り込み可、lock 無し、sleep してよい（`graphics_lock` の mutex と shootdown の ack の待ち）。割り込み・panic・service thread からは呼ばない。
- 結果の判断: backend の `boot_framebuffer`・`linear_physical`・`linear_bytes`・`linear_pixels` は起動の中（prepare と `framebuffer_moved`、user の空間の前）と end の中だけで書かれる。`backend_left[]` は end の中（`graphics_lock` の下）でだけ書き、後の呼び出しは同じ lock の下で読む。他の板の残る物（名前付きの値・固定の番地）は起動の後変わらない。
- 順: `drv_pcat_text_end()` は `text_lock`（spin、137）を取り「終わり」を書いて離す → `kernel_putc` の公開（NULL の時だけ、lock 無しの atomic）→ `drv_pcat_graphics_end()` が `graphics_lock`（mutex、125）を取る → `kern_device_unmap`・`kern_device_map` → HAL の registry の lock と shootdown → `graphics_lock` を離す。text_lock を持ったまま graphics_lock を取らないので、今の順（ENTER の graphics_lock → text_lock、`pcat-graphics.c:239`）と逆にならない。kern_logf は全部の lock を離してから。PC-98・generic も同じ順。
- text の書き手（putc・write・clear・set_cursor・ticker・reveal・progress）は全部 `text_lock` の下で `text_ready` を見る（1.2）。end は lock の下でまず `text_ended` を atomic に 1（release）にしてから `text_ready = 0`、VGA なら CRTC の cursor を今の suspend と同じに消し（:820-824）、`drv_pcat_splash_stop()`・`text_splash_ended = 1`。lock を離した時点で以後の書き手は描かない（cell は更新する）。ticker は次の回で `text_splash_ended` を見て終わる（:1034-1046）。`putc_locked()` の改行の `drv_pcat_splash_step()`（:415-416）は stop の後は何もしない（`splash.c:248-249`、:271）。
- `text_pixels` を NULL にしない（L2）: `reveal_fatal` の lock 無しの道は `draw_cell_locked()` を通して画素ごとに `text_pixels` を読む（:340）。end が同時に NULL にすると panic の CPU が NULL に書く。止めるのは `text_ended` だけにする。
- `reveal_fatal` の lock 無しの道: 最初に `text_ended` を acquire で読み、1 なら `text_ready` を 1 にせず、全画面の消去・redraw・`vga_cursor_locked` をしない（splash の停止と `text_hidden = 0` は今まで通り）。panic の CPU が `text_ended` を 0 と読んで描き始め、同時に別の CPU が end を進める狭い窓では、end の CPU の shootdown が panic の CPU（割り込みを止めている）の ack を待ち続けるので解除も GHCI の解放も起きない（memory の破壊は無い、機械は止まる）。受け入れる残り（§8）。
- HAL の窓の解除（M1・M3）: HAL は `kernel_putc` が NULL の間は窓を渡さず（map_device の条件）、渡した後は HAL の console が窓に書く道は無い（`hal_putc` は委ねる、`hal.h:80-86`、1.3）。HAL は外す前に自分の `console_output_lock()` の下で `framebuffer_pixels = NULL` にする（同じ CPU の再入や将来の呼び手が外れた窓に書かない）。窓の entry は global（`framebuffer-map.c:8` の `FB_FLAGS` の bit 8、2 MiB の leaf は :60、4 KiB の leaf は端の表の :66）で、BSP は CR4.PGE を立てる（`space.c:643-645`）。今の shootdown は `HAL_SPACE_SYS` を CR3 の読み直しで消す（`flush_request_range` :2378-2392、`asm_flush_tlb` は CR3 の読み直し `asm.c:247-255`）だけで、global の entry は残る。よって窓の解除は**新しい種類の shootdown（global を含む全部の消去）**を使う（§11 の 3）。動的な view（`device_window_map` の PTE は global を持たない、`space.c:2926`）の解除は今の shootdown のままでよい。
- /dev/graphics の描画（ioctl）は全部 `graphics_lock` の下で `linear_pixels` を使う（1.2）。end は同じ lock の下で ENTER を捨て `graphics_ended = 1`、backend が pointer を NULL にしてから unmap。lock を離した後の ioctl は `ENODEV`。
- `graphics_lock_ready` が 0 の時（L3）: `drv_graphics_device_restore_text()` の `!graphics_lock_ready` の道（`pcat-graphics.c:96-103`）に倣い、lock を取らずに `graphics_ended = 1` と backend の end をする。この時は /dev/graphics が未だ登録されていない（登録は VFS の init の中、`vfs.c:451-457`）ので owner も ENTER も無く、他の書き手は起動の `framebuffer_moved` だけ（user の空間の前）。後の登録で lock ができた後の open は `graphics_ended` を見て `ENODEV`。GHCI の最初の flip は user の present の後なので、この道は実際には通らない（試験 13.2 の (d) で通す）。PC-98・generic も同じ。
- `framebuffer_moved` は lock を持たない（`backend.c:1205-1234`）。呼ぶのは起動の PCI の attach の中（1.2）で、GHCI の最初の flip（user の present の後）と重ならない。念のため冒頭で `backend_ended` を見て何もしない。重なりを防ぐ lock は足さない（`graphics_lock` はその時まだ初期化されていない事がある、`pcat-graphics.c:118-129`）。
- 2 つの呼び手（2 つの GHCI device、GHCI と core）が同時に呼ぶ: text の end は `text_lock` の下で冪等、/dev/graphics と backend の end は `graphics_lock` の下で冪等で、後の呼び手は lock で 1 人目の外し終わりを待ってから `left[]` を自分の ranges で判じる（H1）。
- rpi4: `rpi_cons_suspend_output()` は HAL の output lock の下で印を書く（`cons.c:184-195`）。driver の「戻さない」の印は atomic で、`console_resume` が読む。
- VF2・x68k: `kernel_putc` の公開は RELEASE の store 1 つ（`hal_putc` は ACQUIRE で読む）。VF2 の UART の送りは spinlock（IRQ を止める）で 1 文字ずつ守る。公開の前に HAL が書いていた文字と公開の後に kernel が書く文字が UART の FIFO で混ざる事は、今の HAL の SBI の出力と同じ程度で受け入れる。公開と同時に別の hart の `hal_putc` が HAL の fbcons に 1 文字描き終える窓は残る（fbcons の画面は SoC の DRAM で GHCI の memory でない）。
- GHCI: `boot_end()` は `scan_flush` が `device.lock`・`display_lock` を離した後（`display-scan.c:2575` の後）で呼ぶ。`end_due` の取り出しと `end_tried` の印は lock の下なので 1 度だけ。結果を戻す時に `device.lock` → `display_lock` を今と同じ順に取り直し、`present`・`boot_in_use` を取り直した後の値で見る（lock を離している間に detach・lease の終わりの点け直しが起き得る、§8）。service（`scan_flush(device, 1)`）は呼ばない（今の解放と同じ。service は眠らない、p012 の H1）。`ghci_scan_flush()` の呼び手は今も lock を持たない約束（`ghci_memory_boot_release` の約束、`memory.c:568-570`）で、drv_gpu の core も op の周りに mutex を持たない（1.5、L5）。段 5 で全部の呼び手を読み直し、stub の lock の数え 0 で確かめる。

### 7. 割り込み・DMA・panic

- 割り込みの文脈の変更は無い。`kernel_putc` の道（割り込み・panic から呼ばれ眠らない、`hal.h:1541-1548`）は今と同じ spinlock だけ。VF2 の新しい UART の `kernel_putc` も眠らない。
- DMA: boot の範囲を返すのは、表示の controller が自分の image を走査し始めた（最初の flip が取られた）後で、boot の image の flip が置かれても取られてもいない時だけ（`boot_in_use` が 0）。
- console の CPU の書き込みの順（L4）: end の shootdown の完了で CPU からの新しい書き込みは止まるが、UC の mapping で出した posted write が PCIe の道にまだ居るかもしれない。`kern_text_end()` が 0 を返した後、範囲を返す前に GHCI は自分の register を 1 つ読む（`ghci_read(device, GHCI_MAGIC)` など副作用の無い物。`present` の時だけ、`device.lock` の下）。PCIe の順序（読みの要求は前の posted write を追い越さない）で、console の最後の書き込みは allocator に返す前に device に届く。console がこの device に書いていなかった時も読みは害が無いので、0 の時は常に読む。
- panic（v1 は表示無し）: end の後の `kern_fatal` → `reveal_fatal` は表示に触らない（§6、VF2 も）→ `hal_fatal` → `kernel_putc` → PC/AT は text 層の `putc` で serial の mirror と cell だけ、VF2 は kernel の UART、x68k は捨てる。ring と debug port にも残る（1.6）。HAL の窓は外れているが、HAL は handover の後その窓を使わない（1.3）。`ghci_display_panic` は呼ぶ物が無いまま（Future Work）。Q6 で console は常に止まるので、別の device の上の GOP の panic の表示（BUG-158）も GHCI の最初の flip の後は無い（8.1 の 1）。

### 8. 失敗と回復

| 場面 | `kern_text_end()` | console | GHCI |
| --- | --- | --- | --- |
| PC/AT、GOP が GHCI の BAR の中、固定の窓の外 | 0（委譲された HAL の窓も外れる） | 終わり（serial・klog・cell だけ） | 範囲を返す、log「boot framebuffer given back」 |
| PC/AT、GOP が別の device（QEMU の stdvga、i915・amdrdna の GOP、legacy の VGA の text、handoff 無し） | 0（外せない物が残っても GHCI の BAR に掛からない） | 終わり（Q6、panic の表示も無い） | 範囲を返す |
| PC/AT、`kernel_putc` が NULL（text が描ける surface を得なかった。HAL が early console を描いていた） | 0（`kern_text_kernel_putc` を公開して HAL を黙らせ、窓を受けて外す） | 終わり（serial の mirror と klog だけ） | 範囲を返す |
| PC/AT、GOP が GHCI の BAR の中、framebuffer が固定の窓に掛かる | `EBUSY`（HAL の窓は渡されたまま外れず、固定の窓も残る） | 終わり | 範囲を保つ |
| PC/AT、GOP が GHCI の BAR の中、unmap の失敗（`pmem.c` の errno）・`linear_retired[]` が満ちていた | その errno・`EBUSY`（2 度目も同じ） | 終わり（描かない、mapping は残り得る） | 範囲を保つ |
| 同上で、外せなかった物が GHCI の BAR の外 | 0 | 終わり | 範囲を返す |
| PC/AT、linear の map が失敗し VGA の text、handoff の GOP が BAR の中（quiet boot の splash は HAL の窓で GOP に描いていた） | 0（text と splash を終え、HAL の窓を受けて外す。legacy の窓は残るが BAR の外） | 終わり | 範囲を返す |
| PC/AT、BAR が動き `framebuffer_moved` が新しい所を map できなかった | 0（古い所の mapping と HAL の窓を外す） | 終わり | 範囲を返す |
| PC-98・sun4u・rpi4・x68k の板（GHCI は付かないか、表示が BAR の外） | 0 | 終わり | （範囲を返す） |
| VF2、UART が見つかった | 0（HAL の fbcons の画面の mapping は SoC の DRAM で BAR の外） | 終わり（serial は kernel の UART、HDMI の画面は最後の絵のまま） | 範囲を返す |
| VF2、UART が見つからなかった | `ENOTSUP`、何も変えない | 今のまま | 範囲を保つ |
| PC-98・VF2 で残る物が ranges に掛かる（host 試験だけ） | `EBUSY` | 終わり | 範囲を保つ |
| 表が無い・`end` の無い表（serial だけの板） | `ENOTSUP` | 今のまま | 範囲を保つ |
| 引数の誤り（GHCI の `bar_count` 0 は呼ばないので起きない） | `EINVAL` | 今のまま | 範囲を保つ |
| GHCI が UMA（`boot_reserved` 0）・boot display を取らなかった | GHCI は呼ばない（core が最初の flip で `(NULL, 0)` で呼ぶ、§3.5） | 終わり（Q12「常に止める」・Q13） | 返す範囲は無い（今のまま） |
| flip が取られる前の detach・LOST の後の detach | 呼ばない（`present` 0、L7） | 今のまま | detach の今の扱い |
| `boot_end` が lock を離している間に lease の終わりが boot の image を点け直した（`flips[0]` に boot、または `boot_scanning` 1） | 0 | 終わり | `console = 0`、`kept = 1` のまま。次に自分の image が取られた時 `boot_release`、次の flush で返す |
| `boot_end` が lock を離している間に detach（`present` 0） | 0 | 終わり | `console = 0`、範囲は返さない（detach が allocator ごと捨てる。phase011 の「解放の前の detach」） |
| 範囲を返した後に lease の終わり・detach | — | — | `kept` は 0 なので点け直さず dark（8.1 の 2） |

- 回復は無い（戻らない）。GHCI は 1 度だけ試し（`end_tried`）、0 以外なら boot の範囲を今と同じに保つ（ユーザーの決定の「通知を断られたら範囲を保つ」）。

#### 8.1 既知の制限（v1）

1. GHCI の最初の自分の flip の後、console は画面に何も出さない（serial と klog だけ）。GOP が別の device の上（QEMU の `config-amd64-ghci.mk` の guest、i915・amdrdna が console を持つ機械、VF2 + KC705）でも止まる（Q6「常に止める」）。panic の表示も無い（ユーザーの決定、Future Work の panic の hook）。
2. 範囲を返した後は `boot_kept` が 0 なので、Keiland（lease の持ち主）が終わると GHCI の画面は dark のまま（`detach_step` の「dark, unless it is」、`display-scan.c:2144-2149`）。Future Work の「/dev/gpu0 の scan-out を console の backend に切り替える」までの制限（L6）。
3. amd64 で framebuffer が固定の窓に掛かり GHCI の BAR の中の機械は、その窓を外せないので範囲を保つ（`EBUSY`）。外すには HAL の実装でその窓の 2 MiB の page を 4 KiB に分ける要がある（Future Work 候補）。
4. VF2 の HAL の fbcons の画面の mapping（device window）は外せない（kernel に口が無い）。SoC の DRAM で GHCI の BAR に掛からないので解放に影響しない。HDMI の画面は最後の絵のまま残る。
5. x68k は end の後の文字が klog の ring だけになる（他の出力が無い）。GHCI は付かないので実際には呼ばれない。
6. 最初の client の flip（どの GPU driver でも、§3.5）の後は、i915・amdrdna・powervr・bcm2711 の `kern_text_resume`（lease の終わり、amdrdna の休みの 3 s の後を含む）は何も描かない。lease の終わりに driver が firmware の framebuffer を scan-out に戻すと、end の時の最後の console の絵（か driver の塗った黒）が見え、新しい文字は serial と klog にだけ出る（Q13「シリアルバックは終了しません」）。
7. serial だけの板（riscv・m68k などの virt）は `end` を持たず `ENOTSUP`（Q11。そこで GPU の flip があっても console はそのまま。HAL が RAM の framebuffer に描く板では画面の文字も続く）。
8. QEMU の venus（virtio-gpu）で Keiland が最初に present した後も console は画面に描かない（login の画面は Keiland が描く）。GPU を使わない回帰の試験（AGENTS.md §9）は flip が無いので変わらない。

### 9. GHCI の側

- `boot_console = 1` の置き換え: `boot_take()` は今まで通り 1 にするが、0 にするのは `kern_text_end()` が 0 を返した時だけ（試験の `boot_console = 0` の直書きは stub の結果に替える）。
- 呼ぶ時: `taken()` で最初の自分の image が取られ、boot の範囲が allocator の外にあり（discrete、`device->boot_reserved`）、BAR の範囲がある（`bar_count` > 0）時に `end_due`。実際の呼び出しは次の service 以外の `ghci_scan_flush()` の終わりで、lock 無し。その後 `ghci_memory_boot_release()`（lock 無し、`memory.c:568-570` の約束通り）。
- 渡す範囲: `bars[0..bar_count)` をそのまま `struct kern_text_range` に写す（BAR0 の全体と BAR2 の全体）。boot framebuffer の CPU の物理を window から計算しない（§15）。
- GHCI の自分の呼び出しは返す範囲がある時だけ（discrete で boot display を取った時）。UMA と、discrete で boot display を取らなかった時（firmware が DISP2 を点けていない）は GHCI は呼ばないが、console は core の最初の flip の呼び出し（§3.5）で止まる（Q12「常に止める」・Q13。Q3 の「UMA は通知しない」は GHCI の範囲の通知だけに残る）。
- 今の解放の道（`boot_release`、`scan_flush` :2557-2561）にも `boot_in_use` の確かめを足す（boot の image の flip が置かれた後に範囲を返さない。§4）。
- detach・停止・panic の `boot_kept` の条件（`display-scan.c:356`・:667・:2136）は変えない。
- log: 成功「ghci: %s: boot framebuffer given back」（今の文、`display-scan.c:2580`）、失敗「ghci: %s: boot framebuffer kept: the console cannot leave it (%d)」。
- 出典の block の comment（`Procedure referenced: amaterrace-gpu d9252e4 docs/ghci.md (section 13.3 …)`、`display-scan.c:2515-2516`）はそのまま。

### 10. platform ごと

| platform | text の表 | end | GHCI | 備考 |
| --- | --- | --- | --- | --- |
| amd64（pcat） | `pcat_text_ops` | 鎖の全部 + HAL の窓の委譲（§11） | 有り（`plan/ws290/tests/config-amd64-ghci.mk`） | 常に止まる。結果は 0（固定の窓が BAR に掛かる時だけ `EBUSY`） |
| i386（pcat） | 同じ file | 同じ code（HAL の窓は無い。`kern_device_map` は今と同じ道） | 無し | build と host 試験 |
| PC-98（i386） | `pc98_text_ops` | 鎖の全部（3.4） | 無し | build と host 試験 |
| sparcv9 sun4u | `sun4u_console_ops` + generic の front end | 鎖の全部（3.4） | 無し | ユーザーの物（main の checkout でユーザーが編集する）。段 7 は最後に、着手の前に Q1 がユーザーに未 commit の変更の有無を確かめる（Q8「最後に、確かめてから (推し)」） |
| rpi4 | `rpi4_console_ops` | 表だけ（HAL の既存の suspend を戻さない形で） | 無し（PCIe はあるが GHCI の config は無い） | build と host 試験 |
| x68k | `x68k_console_ops` | 表だけ（捨てる `kernel_putc` の公開） | 無し | build と host 試験 |
| riscv64 VF2 | serial の写し + `reveal_fatal` + `end` | 表と kernel の UART の出力（ns16550 の送り、VF2 の probe） | 有り（`plan/ws290/tests/config-visionfive2-ghci.mk`、p014 の機材） | p014 で 0 → 解放、HDMI の console が止まり serial が続く事を実機で確かめる（13.7） |
| serial だけの板 | `serial_console_ops` | 無し（`ENOTSUP`） | — | build |

#### 10.1 複数の GPU（H1、Q6 の後）

| 構成 | console の表示の memory | GHCI の `kern_text_end` | 他の driver |
| --- | --- | --- | --- |
| QEMU の guest に KC705 の GHCI を渡す（`config-amd64-ghci.mk`、GOP は QEMU の stdvga） | stdvga の BAR | 0 → console は止まり、範囲を返す | panic の表示（BUG-158）も無くなる（Q6） |
| amd64 で GOP が i915・amdrdna の上、GHCI も居る | i915・amdrdna の BAR | 0 → 止まり、範囲を返す | 最初の flip がどの driver でも core が止める（§3.5）。i915・amdrdna の lease の suspend・resume は呼べるが、resume は何も描かない |
| amd64 で GOP が GHCI の上、i915・amdrdna も居る | GHCI の BAR2 | 0 → 止まり範囲を返す（固定の窓なら `EBUSY`） | 同上 |
| GHCI が 2 枚 | 1 枚の BAR2 か別の device | 最初の 1 枚が止め、両方 0（残る物が自分の BAR に掛からない限り） | — |
| VF2 + KC705（p014） | SoC の DRAM | 0 → HDMI の console は止まり、範囲を返す | powervr の lease は今のまま |

### 11. HAL

- **ユーザーの答え**: Q1「HALはそのマッピングをカーネルに委譲しています。HAL APIは追加せず、カーネルがアンマップしてOKです。」、Q7「実装だけで直す (推し)」 → hal.h に関数を足さず、契約の文も変えず、amd64 の HAL の実装（`src/hal/`、guardrail の「実装の修正・補完」）で kernel が既存の口で HAL の early console の窓を外せるようにする。
- **code の事実**（1.3）: HAL が kernel に委ねているのは描く事（`hal.h:80-86`）で、page table の entry は HAL の system の page table に残り、kernel にそれを渡す記録は無い。既存の HAL の関数ではこの窓を外せない（`hal_space_unmap`・`hal_space_prot` は system の空間を断り、`hal_space_unmap_device` はこの VA を知らず `HAL_ERR_INVALID`）。
- **契約の文**（Q7 の「実装だけ」が今の hal.h で成り立つか）: `hal_space_map_device` は「HAL が device の mapping の kernel の窓を持ち、呼び手は HAL が選んだ番地を受ける」（`include/hal/hal.h:773-779`）で、どの番地を選ぶかを限らない。early console の窓を返すのはこの中。`hal_space_unmap_device` は「`hal_space_map_device()` で作った mapping を外す」（:787-789）で、返した窓はこれに当たる。返す値の一覧は書かれていないので `HAL_ERR_BUSY`（:32 の既存の値）を返しても文に反しない。HAL が handover の後に画面に触らない事は既に `hal_putc` の契約（:80-86）にある。global の TLB の消去は実装の事。よって hal.h の文の変更は要らない、と判断した。変わるのは文に書かれていない HAL の内側の持ち物（窓を持ち続ける事）で、これは Q7 の答えで実装として扱う。念のため、文を足す形の差分を [`plan/ws290/proposed/p017-hal-fb-handover.diff`](../proposed/p017-hal-fb-handover.diff) に置いた（当てない。Q1・ユーザーが文が要ると判じた時だけ承認の項目にする）。
- **実装（`src/hal/amd64/space.c`・`bsp-pcat/cons.c`）**:
  1. 起動の page table を作る時（`space.c:587-594`）、HAL は early console の窓を覚える: 物理 `framebuffer->physical_base`、大きさ `framebuffer->size`（kernel が読む `pcat.framebuffer.size` と同じ値、`bsp-pcat/boot.c:72-95`・:724-725）、VA `ZBL6_FRAMEBUFFER_VIRTUAL_BASE + (physical − 2 MiB に揃えた所)`、entry の範囲（`system_mmio_pd[AMD64_FRAMEBUFFER_PD_FIRST ..]` と端の表 `system_framebuffer_edges`）、状態（HELD・GIVEN・REMOVED、§4）。
  2. `hal_space_map_device(paddr, size, attr)`（M3）: 状態が HELD、`kernel_putc` が NULL でない（ACQUIRE で読む）、`paddr` が handoff の物理と同じ、`size` が handoff の大きさと**同じ**、`attr` が `READ|WRITE|NOCACHE`（窓と同じ UC）の全部の時だけ、固定の窓の判定より先にその VA を返し GIVEN にする（窓は 1 つの呼び手だけに渡す。参照は数えない）。他は今と同じ道（固定の窓か動的な view）。状態の変更は registry の lock（`device_mappings` と同じ lock）の下。
  3. `hal_space_unmap_device(vaddr, size)`: 状態が GIVEN で `vaddr`・`size` が渡した物と同じ時:
     - 窓の物理の範囲 `[paddr, paddr + size)` が固定の窓（`[0xa0000, 0x100000)`、`[0xf0000000, 0xf1000000)`、`[0xfec00000, 0xff000000)`、`space.c:2750-2757`）のどれかと**重なれば**（L2。`device_fixed_range` の「先頭が窓の中で全体が収まる」の判定ではなく重なりで見る。先頭が窓の手前で後ろが窓に掛かる framebuffer も含める）、`HAL_ERR_BUSY` を返し何も外さない（GIVEN のまま、呼び手の物のまま）。
     - 他は、HAL の console の `framebuffer_pixels` を自分の output lock の下で NULL にし、窓の PD の entry と端の表の entry を消し、**global の entry も消える shootdown**（下）で全 CPU の TLB と paging-structure の cache を消して REMOVED にする。
     - shootdown の種類（M1）: `struct amd64_shootdown_request`（`space.c:97-103`）に `unsigned global;` を足し、`flush_request_range`（:2378-）は `global` の要求で、その CPU の CR4.PGE が立っていれば PGE を落として戻す（global の entry を含む全部の TLB と全部の paging-structure の cache が消える）、立っていなければ今の CR3 の読み直し（PGE が無ければ global の bit は効かない）。送り手（:2349-2350）と受け手（`service_shootdowns` :2435）は同じ `flush_request_range` を通るので両方に効く。新しい内部の関数 `shootdown_global()` が `shootdown()` と同じ手順で `global = 1` の要求を出す。他の要求（`global = 0`）の振る舞いは変えない。
     - 選んだ理由（PGE の落として戻す vs invlpg を全部の page に）: (a) 窓は 2 MiB の leaf と端の 4 KiB の leaf の混ぜ物で、端の表を指す PD の entry も消すので、page ごとの invlpg は leaf の大きさに合わせた走査を要求の中に持ち込む。PGE の切り替えは窓の形に依らず 1 度で全部（global を含む TLB と全部の paging-structure の cache）を消す。(b) invlpg を `AMD64_INVLPG_MAX_PAGES`（`space.c:89`）の上限無しで回すと、窓は最大 `AMD64_FRAMEBUFFER_PD_COUNT`（`space.c:67-69`、112 個の 2 MiB = 224 MiB）で、4 KiB ごとなら 1 CPU あたり最大 57344 回を割り込みを止めて回す。(c) 同じ HAL に前例がある（`amd64_pat_configure` が global の page を消すため PGE を落として戻す、`asm.c:329-361`）。(d) 一度だけの出来事なので、他の kernel の global の entry も消える費用は問題にならない。値の出典は Intel SDM Vol. 3A §4.10.4.1（CR4.PGE を変える MOV to CR4 が global を含む全部の TLB の entry と paging-structure の cache を消す）。code の comment には guardrail の形「Procedure referenced: …」で block ごとに書く。
     - 読んだ限り AP は CR4.PGE を立てない（`ap-trampoline.S:28-30` は PAE だけ、`amd64_cpu_init` は `asm.c:391-393` で OSFXSR・OSXMMEXCPT、`amd64_pat_configure` は元の CR4 に戻す）。PGE の有無を CPU ごとに見る上の形はどちらでも正しい。
  4. 窓の受け取りの前に HAL が描かない事: 条件 2 の `kernel_putc` が NULL でない事で、窓を渡す時と外す時（GIVEN の後）に HAL の console は窓を使わない（`kernel_putc` は NULL に戻らない、1.8）。
- 他の port: kernel の番地に early console の窓を持ち handover の後も残す port は amd64 だけ（i386 の HAL は framebuffer を持たない 1.3。sun4u は物理の番地で描く。rpi4 は direct map。VF2 は HAL の device window を持つが、kernel が `kernel_putc` を公開して黙らせるだけで、その窓は外さない、8.1 の 4）。他の port の HAL は変えない。
- **残る物と危険**: 委譲された窓と console の層の mapping は全部外れる。残るのは (1) framebuffer が固定の窓に掛かる時の HAL の恒久の mapping と渡した窓（ranges に掛かれば `EBUSY` で GHCI は範囲を返さない）、(2) loader の page table の同じ窓（HAL が自分の system の page table に CR3 を替えた後は使われない、と読んだ。段 3a で CR3 の切り替えと loader の表の memory の扱いを確かめる。使われていれば HAL の実装で同じく消す）、(3) PC-98 の text の VRAM の固定の番地と VGA の legacy の窓（GHCI と重ならない）、(4) VF2 の fbcons の device window（SoC の DRAM）。よって範囲を返した後に kernel が client の memory を指す書ける mapping を持つ道は、(2) の確かめを除いて無い。
- 前の版の案 B（hal.h に `hal_console_release_display()` を足す差分）はユーザーの答え（「HAL APIは追加せず」）で取り下げ。

### 12. 既存の framework との関係

- drv_gpu の core（`gpu.c`）に、最初の client の flip の後の `kern_text_end(NULL, 0)` を足す（§3.5、Q13）。`GPU_DISPLAY_CLAIM` の通知（1.5）はそのまま。各 GPU driver の source は変えない。
- `kern_text_suspend`・`resume` の意味は end の前は変えない。end を呼ぶのは core（全 GPU driver の最初の flip）と GHCI（返す範囲がある時）。GPU の flip の無い機械・build では何も変わらない。end の後は、どの driver が resume を呼んでも何も描かない（8.1 の 6）。
- cdev: /dev/graphics の node は消さない（file を開いた者がいるかもしれないため）。`ENODEV` で断る。
- `include/kern/graphics-backend.h`（generic の front end）: 末尾に省略可の `end` を足す（3.4、Q2 の答え）。全部の backend の表（今は `sun4u_fb_backend` だけ）は designated initializer か確かめ、そうでなければ段 7 で直す。`:21-28` の「card の driver の乗っ取りは boot の時だけ、firmware の mapping は返さない」の記述に「使用終了の後は返す」を足す。
- serial: `src/drivers/serial/ns16550.c`・`include/drivers/serial/ns16550.h` に送る関数を足す（今の入力の道と riscv-virt の使い方は変えない）。
- PCI: `framebuffer_moved` の古い mapping を end で消すようにするだけ。GHCI の glue は PCI の既存の `drv_pci_device_bar()` と mapping の `physical_address` を読むだけで PCI の API は変えない。
- HAL: hal.h は変えない（Q7）。amd64 の `hal_space_map_device`・`hal_space_unmap_device`・shootdown の実装に委譲の窓と global の消去を足す（§11）。rpi4 は既存の bsp の口（`rpi_cons_suspend_output`、driver が既に使う）だけ。VF2・x68k は既存の `kernel_putc` の公開だけ。

### 13. 試験の計画

#### 13.1 host（GHCI）: `plan/ws290/tests/ghci-driver-host-test.c`・`ghci-stubs.c`・`ghci-stubs.h`

- stub `kern_text_end()`: 結果（既定 `ENOTSUP`）、呼ばれた数、受けた範囲、呼ばれた時の持っている lock の数が 0 を `expect`。試験は `device.bars[]`・`bar_count` を置く。
- 場面（`scene_display_boot` を直し `scene_display_boot_end` を足す）:
  1. discrete、通知 0: flip が置かれただけでは呼ばない、取られた後の flush で 1 回、lock 無し、範囲は置いた BAR と同じ → register の読みが 1 回（L4）、`boot_kept` 0・`boot_reserved` 0・`boot_count` 0・`window_users` 0、log の文。次の flush で 2 回目を呼ばない。detach で dark。
  2. discrete、通知 `ENOTSUP`・`EBUSY`・`EINVAL`・`EIO`: 1 回だけ呼び、register を読まず、範囲を保つ（`boot_reserved`・window 開・`boot_kept`）。次の flush で呼ばない。detach で boot の image を点け直す（今の期待と同じ）。
  3. UMA の boot display・`bar_count` 0・boot display を取らなかった: 呼ばない（数 0）、今の期待のまま。
  4. service の flush（`scan_flush(device, 1)` の道）だけでは呼ばない。
  5. 取られる前の detach、`end_due` が立った後・flush の前の detach（L7）: 呼ばない、`end_due` 0。
  6. 2 つの device: 両方 1 回ずつ、それぞれ自分の BAR。
  7. (a) `end_due` が取られた後、lock を取り直す前に lease の終わりが boot の image を点け直す（stub の `kern_text_end` の中で `light(device, &display->boot)` の道を走らせる、または `flips[0]` に boot を置き、別に `boot_scanning = 1` も試す）: 0 → `boot_console` 0・`boot_kept` 1・`ghci_memory_boot_release` を呼ばない。その後 boot の flip が取られ、次に自分の image が取られると `boot_release` 1、次の flush で 1 回だけ返す。
  8. (b) 同じ窓で detach（stub の中で `present = 0`）: `boot_console` 0、`ghci_memory_boot_release` を呼ばない、register を読まない、二重の解放が無い（ASan）。
  9. 今の解放の道（`boot_release`）で、flush の前に boot の flip が置かれた: 返さず、`boot_release` 0、次の自分の image で立て直し。
- glue（`pci-attach.c`）の `bars[]` の埋め方は、BAR の stub（`drv_pci_device_bar`・`map_bar_region` の `physical_address`）を持つ既存の試験が有れば足し、無ければ段 5 で小さな場面を足す（BAR2 の 1 GiB を超える `bar.size` が切られない事）。
- 既存の `ghci-host-test.sh` の全部（words・ref・model・driver・vk・pipeline・execute・meta・gsi1）が PASS。

#### 13.2 host（kernel の側、PC/AT）: 新しい `plan/ws290/tests/text-end-host-test.c`・`text-end-stubs.c`・`text-end-host-test.sh`

- 本物の `src/kern/text-display.c`・`src/drivers/platform/pcat/graphics/{text.c,pcat-graphics.c,backend.c,font.c,vgafont.c,splash.c}` を host で compile し、stub（spinlock・mutex・cdev_register・copyin/copyout・`kern_device_map`/`unmap`（RAM の配列を返し map・解除を記録、「HAL の窓を返す」要求の条件を真似る、失敗・`EBUSY` を注入）・`kern_boot_framebuffer`（偽の framebuffer、物理の値を置ける）・`drv_pci_*`（Cirrus 無し）・`kern_io_in8/out8`（記録）・`kern_logf`・`kthread_create`・`hal_cpu_current`・`hal_rtc_read_counter`・`drv_pcat_serial_mirror`（記録）・`kern_log_quiet`・`kernel_putc` の変数）。backend.c の依存が重すぎれば backend の stub に替え、`drv_pcat_graphics_backend_end` だけ別の小さな harness で試す（実装の段で判断し記録）。
- 場面:
  - (a) end 無し: putc が偽の framebuffer に描く、suspend・resume が戻せる（今の振る舞い）。
  - (b) end（範囲が handoff と重なる）: 0。`kern_device_unmap` が prepare の map の番地と大きさで 1 回、続いて handoff ちょうどの `kern_device_map` と `kern_device_unmap` が 1 回ずつ（窓の受け取りと解除）。後の putc は serial の記録に出て framebuffer の checksum は変わらず、cell は更新される（snapshot の image に後の文字が出る、L1）。**`!text_ready` の guard を `text_ended` の時は通す 4 か所（`text.c:411` の `putc_locked`、:684 の `write`、:711 の `clear`、:737 の `set_cursor`）の各々で、end の後に cell・cursor が変わり画素は変わらない**（L8）。resume・reveal・reveal_fatal（lock 無しの道も）が framebuffer に書かず VGA の port にも書かない。`text_pixels` は NULL にならない（L2）。2 回目の end は 0 で unmap も map も呼ばない。
  - (c) quiet boot・linear の map の失敗・VGA の text・handoff の GOP あり: end の前は putc の改行と ticker が `splash.pixels`（HAL の窓の代わりの RAM）に描く。end は 0、その後 putc の改行・ticker の 1 回・reveal_fatal で `splash.pixels` の checksum が変わらない、ticker が終わる。backend の end が handoff の範囲で `kern_device_map` と `kern_device_unmap` を 1 回ずつ呼ぶ。legacy の窓を unmap しない。
  - (d) /dev/graphics: open・ENTER の後に end → FILL が `ENODEV` で framebuffer 不変、close は 0 で resume しない、end の後の open は `ENODEV`、`drv_graphics_device_restore_text` は何もしない。登録の前（`graphics_lock_ready` 0）の end: 0、その後の登録と open が `ENODEV`（L3）。
  - (e) unmap の失敗の注入・窓の受け取りの `EBUSY`（固定の窓の代わり）の注入: 範囲が handoff と重なる時は end がその errno・`EBUSY`、2 回目も同じで unmap を再び呼ばない、console は描かない。同じ注入で範囲が重ならない時は 0。
  - (f) 表無し・`end` の無い表 → `ENOTSUP`。引数の誤り（`count` 0 でない時の NULL・5 個・`bytes` 0・桁あふれ）→ `EINVAL`、何も変わらない。`(NULL, 0)` → 0 で console が止まる（core の呼び出し、review 3 の M3）。`(非 NULL, 0)` も範囲無しとして同じ。
  - (j) 2 つの順と同時（review 3 の H1・M2）: `(bars)` の後に `(NULL, 0)`、`(NULL, 0)` の後に `(bars)`、偽の lock で 1 人目が text の end を終え graphics の end の前に 2 人目が入る場面で、2 人目が 1 人目の外し終わりの後に結果を受ける。
  - (g) 重ならない範囲（Q6）→ 0、console は止まり（putc が描かない、suspend・resume・reveal_fatal も描かない）、mapping は (b) と同じに全部外れる。handoff 無しの VGA の text では legacy の窓は残り、範囲がそれと重なる時は `EBUSY`、重ならなければ 0。
  - (h) `kernel_putc` が NULL（text が描ける surface を得なかった）: end は 0、その後 `kernel_putc` が `kern_text_kernel_putc`、putc は serial の mirror にだけ出る、窓の受け取りと解除が 1 回ずつ。`kernel_putc` が別の物 → `EBUSY`、console は止まる。
  - (i) BAR の移動の後: 古い mapping と新しい mapping を両方 unmap、窓の受け取りと解除。移動の map が失敗した後も 0。
- 既存の回帰: `plan/ws014/tests/run-text-notification-test.sh`（`text-display.c` を末尾の field 付きで compile）、`plan/ws275/tests/host-graphics-device.sh`（generic の front end、header の変更の影響が無い事）、i915 の `plan/ws237/tests/run-gen9-*-host-test.sh`（`kern_text_suspend` の stub、不変）。

#### 13.3 host（HAL、amd64）

- `space.c` の委譲の窓の部分を host で試す小さな harness `plan/ws290/tests/hal-fbwindow-host-test.c`（page table を RAM の配列にし、shootdown を記録の stub に。**stub は要求の種類（`global` の有無）・範囲を記録する**）。場面:
  - 覚えた窓の物理・大きさ・VA が handoff と合う。
  - `kernel_putc` が NULL の間の handoff ちょうどの map は窓を返さない（今の道）。公開の後の handoff ちょうど・UC の map は窓の VA を返す。大きさが 1 page 小さい・大きい、属性が違う、2 度目の要求は窓を返さない（M3）。
  - 窓の unmap は PD の entry と端の表の entry を全部 0 にし、HAL の console の pointer を NULL にし、**`global = 1` の shootdown を 1 回だけ出す**（M1。`global = 0` の shootdown で済ませない事を assert）。動的な view の unmap は今と同じ `global = 0` の shootdown（変わらない事を assert）。
  - 固定の窓との重なり（L2）: framebuffer が `[0xf0000000, 0xf1000000)` の中・先頭が 0xefe00000 で末尾が窓に掛かる・末尾が 0xf1000000 をまたぐ、の 3 つで unmap が `HAL_ERR_BUSY`、entry も状態も変わらず shootdown を出さない。
  - 外れた後の同じ map は今の道（動的な view）。
  - `flush_request_range` の `global` の道は host で CR4 を触れないので、CR4 の読み書きを static の関数に出して stub で記録し、PGE が立っていれば落として戻す・立っていなければ CR3 の読み直し、を確かめる。
- `space.c` の依存が重すぎる時は、委譲の窓の判定と entry の消去を `space.c` の中の static でない小さな関数に分けて試す（段 3a で判断し記録）。既存の amd64 の HAL の host 試験が有ればそれも PASS（段 3a で `plan/tools.md` から探す）。

#### 13.4 host（他の板）: `plan/ws290/tests/text-end-boards-host-test.c`・`.sh`

- PC-98（`pc98/graphics/{text.c,pc98-graphics.c,backend.c}` と stub）: 重ならない範囲 → 0、text と /dev/graphics が終わり、4 plane と Cirrus の窓を unmap。範囲が text の VRAM に重なる → `EBUSY`（固定の番地）。
- generic の front end + sun4u（`graphics-device.c`・`sun4u-console.c`・`sun4u-firmware-fb.c` と stub）: 0、`fb_pixels` の unmap が 1 回、`screen_on` が 0 のまま resume で戻らない、`end` の無い backend の表 → /dev/graphics は `ENODEV` にして 0。既存の `plan/ws275/tests/host-graphics-device.sh` も PASS。（段 7 は Q8 どおり最後。）
- rpi4（`rpi4-console.c` と `rpi_cons_*` の stub）: 0、`rpi_cons_suspend_output` が 1 回、その後の `kern_text_resume` が `rpi_cons_resume_output` を呼ばない。
- x68k（`x68k-console.c`）: 0、`kernel_putc` が捨てる関数になり、その後の `hal_putc` の stub が HAL の画面に描かない。
- VF2（`vf2_text_end` と `reveal_fatal`。`visionfive2.c` が host で compile し難ければ、end の判定と UART の putc を `visionfive2.c` の中の小さな関数に分けて試す、段 8 で判断し記録）と `ns16550.c` の送り（register の stub で THRE の待ちと THR の書き、待ちが尽きた時に捨てる）: UART が有る → 0、`kernel_putc` が UART の putc、`reveal_fatal` が powervr の取り戻しを呼ばない。範囲が `riscv64.framebuffer` と重なる → `EBUSY`。UART が無い → `ENOTSUP` で何も変わらない。名前が無い（画面無し）→ 0。
- serial だけの表: `kern_text_end` が `ENOTSUP`。

#### 13.5 build（warning 0）

`plan/ws290/tests/config-amd64-ghci.mk`、`plan/ws290/tests/config-visionfive2-ghci.mk`、`config/ci/config-amd64.mk`（i915・amdrdna 有り）、`config/ci/config-pcat.mk`（i386 が pcat を共有）、`config/ci/config-visionfive2.mk`（powervr）、`config/ci/config-pc98.mk`、`config/ci/config-rpi4.mk`（bcm2711・rpi4 の表）、riscv-virt（ns16550 の変更の影響、段 10 で `config/ci/` から名を確かめる）、x68k の config（段 10 で `config/ci/` から名を確かめる）、sparcv9（sun4u、段 7 の後）。

#### 13.6 QEMU（Q1 が割り当てる。1 本の script `plan/ws290/tests/text-end-qemu.sh`）

- 回帰（end を呼ばない build）: `config/ci/config-amd64.mk` の image を `plan/tools/boot-test.sh`（`uefi-usb`）、i386 を `BOOT_MODE=bios-ide` で login prompt。
- end の道（QEMU に GHCI は無いので試験だけの引き金）: `plan/ws290/tests/config-amd64-text-end.mk`（`config/ci/config-amd64.mk` を include、`ZEDBSD_GRAPHICAL_BOOT := n`、`ZEDBSD_BOOT_KERNEL_MESSAGES := y`、`CONFIG_PCAT_SERIAL_MIRROR := y`、`ZEDBSD_TEST_CPPFLAGS := -DKERN_TEST_TEXT_END_SECONDS=45`（`Makefile:890`））。`text.c` に `#ifdef KERN_TEST_TEXT_END_SECONDS` の kernel thread を置き（Q5 の推しどおり）、text の init から 45 s 後に handoff の範囲で `kern_text_end()` を 1 回呼び、結果を static の変数 `text_end_test_result` と `text_end_test_done` に置く（log の行も出すが判定に使わない）。
- script（AGENTS.md §9: console・serial の log で判定しない。L4）: QMP と gdbstub（`-gdb tcp::<port>`）付きで起動 → `plan/tools/guest/serial.py` で login（対話）→ 20 s の時点で gdb で `linear_pixels`・`linear_physical` と HAL の窓の状態（HELD）を記録し、`linear_physical` が固定の窓に掛かるかを先に見る（掛かれば `EBUSY` を期待し「固定の窓」と出す）→ QMP の HMP `gva2gpa 0xffffffffc2000000+offset`（HAL の窓）が handoff の物理を返す事を記録 → gdb で `text_end_test_done` が 1 になるまで（上限 90 s）見て、`text_end_test_result` が 0（固定の窓なら `EBUSY`）→ 直後に HAL の窓の `gva2gpa` が unmapped（PTE 無し）、gdb で HAL の窓の状態が REMOVED、`device_mappings`（`space.c:141`）に `linear_physical` の entry が無い、`text_ended` が 1 → QMP の screendump A → serial.py で `echo MARKER > /dev/console` を打つ → screendump B → A と B が画素で同じ（console が描かない）→ gdb で `text_cells` に MARKER が入った（cell は更新、L1）→ `plan/ws290/tests/graphics-probe.c`（`--file` で入れる）を serial.py で走らせ、終了の値が `ENODEV` を示す。解除は再利用され得る VA を `x/1wx` で読む方法では判定しない（PTE・HAL の状態で見る）。要約だけを出す。
- (c) core の道（Q13、§3.5）: `config/ci/config-amd64.mk` の graphical boot（Keiland が venus の virtio-gpu に present する）の image を QMP と gdbstub 付きで起動し、gdb で `gpu_console_ended` が 1・`text_ended` が 1 になるまで（上限 120 s）見て、QMP の screendump を 2 枚（serial.py で `echo MARKER > /dev/console` の前後）撮り、Keiland の絵が MARKER で変わらない事を見る（console の log では判定しない）。
- 未実施になる物: panic の後の画面（host の (b) で代える）、GHCI の上の GOP（13.7）、SMP の各 CPU の TLB に古い global の entry が残らない事（QEMU の TCG では観測できない。host の 13.3 の assert で代える）。

#### 13.7 実機

- p014（VF2 + KC705、Q4「VF2 にも実装」）: VF2 の end は 0 → GHCI は範囲を返す。p014 の段 3 に足す確かめ（Q1 が実機を割り当てる。1 本の script で SSH から読む）: (1) attach の時の `boot_take` の log で KC705 の DISP2 が点いていたか（SSH の `dmesg`。点いていなければ boot の範囲は無く、以下は「該当無し」と記録し失敗にしない、L6）、(2) 最初の client の flip の後に `dmesg` に「ghci: …: boot framebuffer given back」が 1 行、(3) その後も serial の console が kernel の UART で働く事（`plan/tools/guest/serial.py` で対話して command を打ち、返事を読む）、(4) VF2 の HDMI の console が新しい文字を描かない事（画面はユーザーが拾える試験の一覧に置く）、(5) local の allocator の空きが boot の範囲の分増えた事（driver に空きを読む口が有れば。無ければ「未確認」と書き、(2) だけで判定）。
- amd64 + discrete の GHCI（GOP が GHCI の上）での 0 の道の確かめは今の機材の計画に無い。未実施として、ユーザーが拾える試験の一覧に置く（Q1 に頼む）。`config-amd64-ghci.mk` の QEMU の guest（GOP は stdvga）を Q1 が割り当てた時は、console が止まり範囲が返る道を確かめられる。

### 14. 実装の段（p017 の中、合計 28 LW、Q9「1 つの Phase で」）

| 段 | 内容 | path | LW | 受け入れ |
| --- | --- | --- | --- | --- |
| 1 | text-display の範囲の型・重なりの関数・op・`kern_text_end`（引数の確かめ） | `include/kern/text-display.h`・`src/kern/text-display.c` | 2 | ws014 の試験 PASS、amd64 の build の warning 0 |
| 2 | PC/AT の text 層の end（`text_ended`、`!text_ready` の guard の 4 か所 `text.c:411`・:684・:711・:737 を「終わった時は cell を更新し描かない」に、resume・reveal・reveal_fatal の条件、splash・ticker の停止、`kernel_putc` が NULL の時の公開）と試験の引き金の `#ifdef` | `src/drivers/platform/pcat/graphics/text.c`・`text.h` | 3 | 13.2 の (b)(c)(f)(g)(h) |
| 3 | PC/AT の /dev/graphics と backend の end（`ENODEV`、lock 前の道、`kern_device_map/unmap` への置き換え、`linear_retired[]`、窓の受け取りと解除、`backend_left[]`、`backend_ended`） | `src/drivers/platform/pcat/graphics/pcat-graphics.c`・`backend.c`・`backend.h` | 3 | 13.2 の (d)(e)(i) |
| 3a | amd64 の HAL の委譲の窓（覚える・条件付きで返す・外す・HAL の console の pointer・固定の窓の重なりで `HAL_ERR_BUSY`）、global の shootdown、loader の page table の確かめ | `src/hal/amd64/space.c`・`bsp-pcat/cons.c`、`plan/ws290/tests/hal-fbwindow-host-test.c` | 2.5 | 13.3 PASS、amd64 の build の warning 0 |
| 4 | kernel の側の host 試験（PC/AT） | `plan/ws290/tests/text-end-host-test.c`・`text-end-stubs.c`・`text-end-host-test.sh` | 2.5 | 13.2 の全場面 PASS（ASan・UBSan も） |
| 5 | GHCI（glue の `bars[]`、`boot_end`、`boot_in_use`、field、comment、L4 の読み）と host 試験。`ghci_scan_flush` の全部の呼び手と `gpu.c` の op の呼び方を読み、lock を持たない事を確かめて記録（L5） | `src/drivers/gpu/ghci/ghci.h`・`display-scan.c`・`pci-attach.c`、`plan/ws290/tests/ghci-driver-host-test.c`・`ghci-stubs.c`・`ghci-stubs.h` | 3.5 | 13.1 の場面 PASS、`ghci-host-test.sh` 全 PASS |
| 6 | PC-98 の text・/dev/graphics・backend の end | `src/drivers/platform/pc98/graphics/text.c`・`text.h`・`pc98-graphics.c`・`backend.c`・`backend.h` | 2 | 13.4 の PC-98、pc98 の build の warning 0 |
| 8 | rpi4・x68k・VF2 の表の `end`、ns16550 の送り、VF2 の UART の probe | `src/drivers/platform/rpi4/rpi4-console.c`・`src/drivers/platform/x68k/x68k-console.c`・`src/kern/platform/visionfive2.c`・`src/drivers/serial/ns16550.c`・`include/drivers/serial/ns16550.h` | 2.5 | 13.4 の各板、riscv64（VF2・virt）・rpi4・x68k の build の warning 0 |
| 9 | 他の板の host 試験 | `plan/ws290/tests/text-end-boards-host-test.c`・`.sh` | 2 | 13.4 の全場面 PASS（段 7 の分は段 7 の後） |
| 10 | build の全部と QEMU の script・config・probe、p014 の確かめの手順（実行は Q1 の割当で） | `plan/ws290/tests/config-amd64-text-end.mk`・`text-end-qemu.sh`・`graphics-probe.c`、p014 の段 3 の script への足し（p014 の持ち主と合わせる） | 2.5 | 13.5 の warning 0、13.6 の要約 PASS（QEMU は Q1 の割当の後）、13.7 は p014 で |
| 11 | drv_gpu の core の `gpu_console_end()`（§3.5）。3 つの flip の道の lock の無い事を読み直して記録。GHCI と分けた commit | `src/drivers/gpu/gpu.c` | 1.5 | amd64（i915・amdrdna・nvrtx・venus 有り）・VF2（powervr）・rpi4（bcm2711）の build の warning 0、13.6 の (c) |
| 7（最後） | generic の front end の `end` と sun4u の console・firmware fb の end。着手の前に Q1 がユーザーに main の checkout の未 commit の変更の有無を確かめる（Q8） | `include/kern/graphics-backend.h`・`src/drivers/generic/graphics-device.c`・`src/drivers/platform/sun4u/sun4u-console.c`・`sun4u-firmware-fb.c`・`.h` | 2.5 | 13.4 の generic・sun4u、ws275 の試験 PASS、sparcv9 の build の warning 0 |

- 合計 29.5 LW（2 + 3 + 3 + 2.5 + 2.5 + 3.5 + 2 + 2.5 + 2 + 2.5 + 1.5 + 2.5。Q13 の全 GPU driver の分は段 11 の 1.5 LW）。それより前の版は 28 LW（2 + 3 + 3 + 2.5 + 2.5 + 3.5 + 2 + 2.5 + 2 + 2.5 + 2.5）。前の版の 27 LW に、VF2 を止めるための kernel の UART の送り（段 8 の +1）を足した。2 度目の review の M4 の通り、前の版の分け方の「段 1〜5 と 10 で 20 LW」は誤りで 19 LW だった。Q9 で分けないので分け方の表は無くした。受け入れの「見積り: 14 LW」と ws.md の表の写しは P3 が直す。
- 段 11 は段 1 の宣言だけに依存し、段 2・3 の後に QEMU の (c) で確かめる。段 1〜3 は順に依存、段 3a は段 3 と並べられる（結合は段 3 の窓の受け取り）。段 5 は段 1 の宣言だけに依存（stub で先に試せる）。段 6・8・9 は段 1 だけに依存。段 7 は最後。他の helper が `src/drivers/gpu/ghci/vk/**` と一部の試験を直しているので、段 5 の `ghci-stubs.c`・`ghci-driver-host-test.c` は P3 が持ち主を確かめてから。

### 15. 選ばなかった案と理由

- **範囲を見ない全体の終わり（この設計の最初の版）**: review の H1 は「関係の無い console を止める」だったが、Q6 でユーザーが「常に止める」を選んだので、止める事そのものは採った。残した違いは結果の判断に範囲を使う事（次）。
- **範囲を引数から外す（Q6 の後に考えた）**: 止めた後に外せない物が残る板（amd64 の固定の窓、PC-98 の text の VRAM、VF2 の fbcons の device window、rpi4 の direct map）で、それが呼び手の memory に掛かるかを判じられなくなる。「何か残れば `EBUSY`」なら VF2 + KC705 で永久に解放されず（Q4 の目的に反する）、「残っても 0」なら amd64 で GOP が GHCI の上・固定の窓の時に client の memory を指す mapping が残る（危ない側）。
- **`ENOENT` で console を止めずに範囲を返す（前の版の推し）**: Q6 の答え（「常に止める」）で取り下げ。
- **GHCI が boot framebuffer の CPU の物理を window から計算して渡す（p012 の「残り」1 の `kern_framebuffer_leave(physical, bytes)` の案）**: `window_physical + (FB_BASE − LOCAL_BASE)` は、firmware が window を driver と違う形にした時に「掛からない」と誤り、残る mapping がある範囲を返す（危ない側に倒れる）。BAR の全体は PCI が知る事実で、CPU が device の memory に届くのは BAR を通してだけ。
- **hal.h に `hal_console_release_display()` を足す（前の版の案 B）**: ユーザーの答え（「HAL APIは追加せず」）で取り下げ。
- **hal.h の契約の文を足す差分を承認の項目にする（2 度目の review の M2）**: Q7 の答え（「実装だけで直す」）と、今の文がこの振る舞いを既に許す事（§11）で、承認の項目にしない。差分は参考として置く。
- **prepare の map で窓を渡す（前の版）**: prepare の map は `kernel_putc` の公開の前で、HAL がまだ窓で描く。早い時期に同じ物理・大きさを map する他の driver に窓が渡り、その unmap で HAL の描く窓が消え得る（M3）。end の中で `kernel_putc` の公開の後に 1 度だけ渡す形にした。
- **invlpg を窓の全部の page に回す（M1 のもう 1 つの案）**: §11 の 3 の (a)(b)。
- **窓の entry から global の bit を落としてから CR3 を読み直す**: 既に TLB に入った global の entry は bit を落としても CR3 の読み直しで消えない。
- **kernel が bootloader の header の `ZBL6_FRAMEBUFFER_VIRTUAL_BASE` を読んで `hal_space_unmap_device` に渡す**: kernel が HAL の内部の番地を知る事になり、今の `hal_space_unmap_device` はその番地を知らない（`HAL_ERR_INVALID`）ので HAL の実装の変更は結局要る。既存の `hal_space_map_device` が番地を返す形の方が「HAL が選んだ番地を返す」の契約に合う。
- **固定の窓の framebuffer も外す（2 MiB を 4 KiB に分ける）**: 他の device と共有の窓の page table を起動の後に作り替える事になり、HAL の変更が大きい。framebuffer がそこに入り GHCI の上の機械は稀で、`EBUSY` で範囲を保てば安全（8.1 の 3、Future Work 候補）。
- **VF2 で捨てる `kernel_putc` を公開する（UART を足さない）**: VF2 の serial は唯一の観察の道（p014 の試験も serial と SSH）で、end の後に serial の出力を失うのは受け入れられない。kernel の ns16550 の driver に送りを足す方が小さい（1 LW）。
- **VF2 で HAL の fbcons を止める口・device window を外す口を HAL に足す**: Q1「HAL APIは追加せず」に反し、`kernel_putc` の公開で HAL は既存の契約（`hal.h:80-86`）どおり画面に触らなくなるので要らない。
- **各 GPU driver の最初の flip の所に呼び出しを足す（Q13 の後に考えた）**: driver ごとに flip の道（amdrdna の present・lease の frame、i915 の bootfb・xd-display、powervr・bcm2711・nvrtx・venus の display）が違い、多くは自分の display の mutex の下で進む（`kern_text_end` は眠り `graphics_lock` を取るので lock の順の確かめが driver ごとに要る）。core の 3 つの flip の道は全 driver が通る ioctl の文脈で lock が無く、1 か所で全部の driver と今後の driver を覆い、作業中の driver（amdrdna）を触らない。GHCI だけは latch と返す範囲が要るので自分でも呼ぶ。
- **drv_gpu の core の `GPU_DISPLAY_CLAIM`（`gpu.c:3841-3847`）で呼ぶ**: claim は flip ではない（ユーザーの言う「最初のフリップ」より前で、claim の後に flip せずに release する client もある）。flip の受付の後にした。
- **`kern_text_suspend` に「戻さない」の印を足す**: suspend は数えず戻せる前提で 8 か所以上が使う（1.5）。意味を混ぜると resume の側の全部を見直す要がある。
- **`kern_text_register()` で serial の表に差し替える**: PC/AT の serial の mirror は text 層の `putc` の中（`text.c:408`）にあり、差し替えると cell も snapshot も失う。
- **end で `text_pixels` を NULL にする**: `reveal_fatal` の lock 無しの道が画素ごとに読む（:340）ので、競合で NULL に書く。`text_ended` で止める（L2）。
- **end の後は cell を変えない（suspend と同じ）**: snapshot（venus）と将来の scan-out の切り替えが古い文を見る（L1）。
- **backend が `hal_space_unmap_device` を直に呼ぶ**: 今の map は直に呼んでいる（`backend.c:1062`）が、ユーザーの決定の「kernel 内 API → HAL」に合わせ `kern_device_map/unmap` に揃える（HAL の属性は同じ）。
- **/dev/graphics の node を消す・file を強制で閉じる**: kernel に file の revoke の仕組みが無く、mmap も無いので `ENODEV` で足りる。
- **UMA でも通知する**: 返す物が無い（Q3、推しどおり）。
- **QEMU の引き金を gdb の `call kern_text_end()` にする**: 止めた vCPU の任意の文脈で眠る関数を呼ぶので不安定（Q5、推しどおり）。

### 16. 出典

- 外部の code・値の引用は無い。zedBSD の既存の code（上の `file:line`）と、GHCI の driver に既にある block の出典（`Procedure referenced: amaterrace-gpu d9252e4 docs/ghci.md (section 13.3 …)`、`display-scan.c:2515-2516`、`memory.c:572-574`）だけ。
- 一般の規則として引く物: PCIe の順序（読みの要求は前の posted write を追い越さない）、Intel SDM Vol. 3A §4.10.4.1（CR4.PGE を変える MOV to CR4 の TLB と paging-structure の cache の消去。同じ HAL の `asm.c:298-308` の comment が既に SDM の手順を引いている）。code の comment に引く時は P3 が `plan/guardrail.md` の出典の規則（block ごとの「Procedure referenced: …」、複数行の comment、file の先頭に書かない）に従う。文を写さない。
- 新しい code の comment に開発の管理情報（WS・Phase の番号など）を書かない（AGENTS.md §10、Guardrail の「出典」）。

### 17. ユーザー・Q1 の判断の項目

答えの出た物（2026-10-28、Q1 経由、原文は上の「設計の判断の項目への答え」）: Q1〜Q13 と hal.h の文。設計に反映した（§3・§10・§11・§13・§14、Q12・Q13 は §3.5・§8・§9）。下の表は問いの記録として残す（Q10・Q11 は推しどおり、Q12 は「常に止める」、Q13 は全 GPU driver に広げる答え）。

問い（答えは上）:

| ID | 問い | 推し | 他の案 |
| --- | --- | --- | --- |
| Q10 | x68k の end: 他の出力が無いので、止めると以後の文字は klog の ring だけ。止めるか（GHCI は付かず実際には呼ばれない） | 止める（捨てる `kernel_putc` を公開。Q6「常に止める」と Q2「全部の backend」に合わせる） | `ENOTSUP`（止めない。x68k の表には `end` を置かない） |
| Q11 | serial だけの板（riscv・m68k などの virt。HAL が RAM の framebuffer に描く板もある）の共通の表に `end` を足すか | 足さない（`ENOTSUP`。GHCI は付かず、止めるには板ごとに kernel の serial の出力が要る） | 板ごとに kernel の UART の送りを公開して止める（riscv-virt は ns16550 が既に有る） |
| Q12 | discrete の GHCI で firmware が boot display を点けていなかった時（返す範囲が無い）も console を止めるか | 止めない（呼ばない。UMA の Q3 と同じく返す物が無い） | 最初の自分の flip で常に止める（「GPU の使用開始の時に通知」の字どおり） |
| Q13 | Q6 の結果を確かめたい: VF2 では end の後 HDMI の console が最後の絵のまま止まり、serial は kernel の UART で続く。QEMU の `config-amd64-ghci.mk` の guest・i915・amdrdna の機械でも console と panic の表示が止まる。これでよいか | よい（Q6 の答えどおり） | — |

### 17.1 3 度目の review（版 3、Opus、2026-10-28）の反映（版 4）

- H1（2 人目が早く 0 を受ける）: §3.2 の 1・§6 を直した。実装 e0bb46bc9 は既にこの形（text の end は毎回 `drv_pcat_graphics_end()` を呼び、それは `graphics_lock` の下で冪等）。
- M1（呼ぶ所）・M2（順）・M3（(f) の矛盾）: §3.5・13.2 を直した。
- M4（VF2 で klog が 2 度）: 段 8 で VF2 の `kern_platform_debug_write()` が end の後は書かない形にし、13.4 の VF2 に「klog の 1 行が UART に 1 度」を足す。
- M5（sun4u）: 段 7 で `screen_on` を保ち `screen_ended` を足し、`screen_enter()` の lock の下で恒久の suspend、suspend・resume・reveal_fatal は ended を見て、`fb_pixels` の NULL と unmap は描き手の止まりを lock で確かめてから。
- M6: (a) 返す memory の無い呼び出し（core の `(NULL, 0)`）でも /dev/graphics（xzed・zterm・zwm の firmware の framebuffer）を `ENODEV` にするかはユーザーの判断（Q14、Q1 経由で問う。推し: 止める。GPU が表示を持った後の firmware の framebuffer への描画は見えないか GPU の memory を壊すため、framebuffer の backend の使用終了に含める）。答えまで実装は止める形。(b) `GPU_DISPLAY_RELEASE` で keyboard は text console に戻るが画面に描かない（8.1 に足す）。(c) venus は lease の終わりに console の worker が `kern_text_snapshot()` で GPU の scan-out に console を描き直す（cell は end の後も更新されるので、venus では console が画面に戻る。framebuffer の backend ではないので Q13 に合う。8.1 の 6・8 の「描かない」は venus を除く）。
- M7（QEMU の (c) が誤りを見つけられない）: 13.6 の (c) は stdvga を GOP、venus の virtio-gpu を別の device にした guest（plan/tools.md の zdesktop-guest の仕組み）で、判定は stdvga の head の screendump か QMP の `pmemsave` で handoff の物理の範囲を MARKER の前後で比べる形にする（gdb の `gpu_console_ended`・`text_ended` は残す）。harness は段 10 で作る。
- M8（gpu.c を compile する既存の host 試験）: `plan/ws014/tests/gpu-test-fd.c`（WS014、closed）の stub に `kern_text_end` が無く、ws113・ws214・ws141（どれも completed、plan/tools.md に無い）の 3 本が link で落ちる見込み。AGENTS.md §9 の整理の規則（参照が無ければ直さず削除）か stub の 1 関数の追加かを Q1 に問う。ws275 の 2 本は段 7 に足す。
- M9（実機）: 13.7 に「i915（5330）・amdrdna の実機の graphical boot で最初の flip の後に例外が無い、dmesg に `gpu: the text console could not leave…` が無い、SSH で login」を Q1 の割当（またはユーザーの拾える一覧）として足す。
- L1（見積りの写し）・L4（Q3 の記述）・L8（i915 の残る絵は最後の client の frame）: 版 4 の写しで直す。L2（`kernel_putc` が別の物の EBUSY）: 大きさの分からない書き手は空でない ranges の全部に掛かるとみなし、`count` 0 では 0。実装は直す（段 2 の残り）。L3（判じ直しの errno）: 実装は `left[]` の entry ごとに errno を持つ（e0bb46bc9）。L5（flip の範囲）: amdrdna の takeover の色の帯・venus の console の worker・GHCI の boot の点け直しは core を通らないので flip に数えない。i915 の capture の build への present も console を止める。COMMIT は plane の image の有無に関わらず成功で flip に数える。L6（VF2 の UART は `/chosen/stdout-path` で選ぶ）: 段 8。L7（rpi5）: §10・13.5 に rpi5 を足す。L9（rpi の HAL の stall watch が HVS の plane に描く）: §3.4 の rpi4 の「残る物」に注として（memory を返さないので害は無い）。

### 18. design.md への反映（P3 へ）

- §13 の版 4.15 の段落の「v1 は boot framebuffer を解放しない（`boot_console` = 1 …）」と「両方の kernel の API は Future Work の候補」を、この設計（GPU の使用開始で console を常に止め、BAR の範囲は残る物の判断だけに使う、`boot_console` は通知の 0 で 0、UMA は呼ばない、VF2 は kernel の UART の `kernel_putc` で止めて解放、HAL の early console の窓は既存の口で kernel に委譲して外す）に直す（版 4.16）。p012 の phase.md の「残り」1 と「既知の制限」の `kern_framebuffer_leave` の行も「p017 で別の形（§15）」と注記。
- Future Work: GPU の使用の終わりに /dev/gpu0 の scan-out を console の backend に切り替える（ユーザーの決定の後者、8.1 の 1・2 を解く）、panic の hook（`ghci_display_panic` の呼び手）、固定の窓の framebuffer の解除（8.1 の 3）、VF2 の fbcons の device window の解除（8.1 の 4）。

## 記録

- 2026-10-28 P3（q1644-i01）: ユーザーの決定（上）で立てた。
- 2026-10-28 P3 の helper（q1644-i01）: 上の「設計」を書いた（code は変えていない、design-reviewer の review は未、Q1〜Q5 は未決）。
- 2026-10-28 P3 の helper（q1644-i01）: design-reviewer（Opus）の review を反映した。H1（`kern_text_end` に呼び手の BAR の範囲を付け、console の表示が重ならなければ `ENOENT` で何も変えない。GHCI は 0・`ENOENT` で範囲を返す。10.1 に複数の GPU）、M1（§11・§3.1 の契約の文を、消える物と残る物に直した）、M2（GHCI の場面 8〜10、kernel の場面 (c)(g)(i)、QEMU の判定を委譲の窓の確かめと PTE・HAL の状態に）、L1〜L6（cell の更新、`text_pixels` を NULL にしない、lock 前の /dev/graphics、register の読み、lock の確かめを段 5 に、8.1 の既知の制限）。code は変えていない。
- 2026-10-28 P3 の helper（q1644-i01）: ユーザーの Q1・Q2・Q4 の答え（P3 経由、原文は上）を反映した。既存の HAL の関数では early console の窓を外せない事を code で確かめ（§1.3）、hal.h を変えずに amd64 の HAL の実装で既存の map・unmap の口から委譲する形にした（§11、Q7）。全部の板の表に `end` を足し（§1.8・§3.4・§10）、VF2 は `ENOENT` で解放、p014 の確かめを §13.7 に置いた。前の版の案 B と Q1〜Q5 の表を取り下げ、Q6〜Q9 を残した。見積りは 27 LW（分けるなら 20 + 8、Q9。受け入れの 14 LW と ws.md の写しは P3 が直す）。code は変えていない。
- 2026-10-28 P3 の helper（q1644-i01）: 2 度目の design-reviewer（Opus、H 0）の M1〜M4・L1〜L8 と、ユーザーの Q6「常に止める」・Q7「実装だけで直す (推し)」・Q8「最後に、確かめてから (推し)」・Q9「1 つの Phase で」（Q1 経由で P3 が伝えた）を反映した。M1: 窓の entry は global で今の shootdown（CR3 の読み直し）では消えない事を code で確かめ（§6）、窓の解除に CR4.PGE を落として戻す新しい種類の shootdown を選び（§11 の 3、invlpg の案を選ばなかった理由も）、13.3 で shootdown の種類を assert する。M2: hal.h の契約の文の差分を `plan/ws290/proposed/p017-hal-fb-handover.diff` に置いたが、Q7 の答えと今の文（`hal.h:773-779`・:787-789・:80-86）で文の変更は要らないと判じ、参考として当てない（§11）。M3: 窓は `kernel_putc` の公開の後に handoff と同じ物理・同じ大きさの 1 つの呼び手にだけ渡し、backend は end の中で受けて外す。M4: LW の和を直し（前の版の 20 は 19）、Q9 で分けないので分け方の表を無くした（28 LW）。L1〜L8: §3.1 の契約の文、固定の窓は重なりで判定、「mapping」の意味を 1 つに（§2）、QEMU の判定を gdb の変数と serial.py の対話に、ユーザーの答えの原文、VF2 の DISP2 が点いていない時の「該当無し」を受け入れに、`!present` の時の `end_due`、guard の 4 か所を段 2 と (b) に。Q6 で `ENOENT` を無くし、範囲は止めた後に残る物の判断だけに残した（§3.1、§15）。VF2 は kernel の ns16550 に送りを足して `kernel_putc` を公開し HAL の fbcons を黙らせる（§3.4）。残りの問いは Q10〜Q13。code は変えていない。
- 2026-10-28 P3（q1656-i01）: helper の設計（0c308bd91 で commit）に、ユーザーの Q10〜Q13 と hal.h の文の答え（原文は上）を反映した。Q13 の全 GPU driver は drv_gpu の core（`gpu.c`）の 3 つの flip の道の成功の後の `kern_text_end(NULL, 0)` 1 度で行い（§3.5、`count` 0 を許す §3.1）、各 GPU driver の source（amdrdna を含む）は変えない。既存の suspend・resume の呼び手との関係（end の後の resume は描かない、amdrdna の lease の終わり・休みの後の resume も）を §3.5・8.1 の 6 に。Q12 で GHCI の自分の呼び出しは返す範囲がある時だけのまま、console は core が止める（§8・§9）。段 11（1.5 LW）を足し 29.5 LW。QEMU の (c) を 13.6 に。design-reviewer の review は未。
- 2026-10-28 P3（q1656-i01）: 3 度目の design-reviewer（Opus、版 3）: H1・M1〜M9・L1〜L9。H1・M1・M2・M3 を §3.2・§3.5・§6・13.2 に反映し、残りは §17.1 に決め方を書いた（版 4）。M6 の (a) は Q14 としてユーザーに問う。
