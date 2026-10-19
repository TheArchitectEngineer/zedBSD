<!-- awesome-plan project=zedbsd record=ws273-p002 -->

# ws273-p002 MIPS の共通の層の変更（RG350 の前の準備、malta は不変）

- 親: [ws273](../ws.md)、Queue: q1327 / q1327-i01（P1、2026-10-19）
- Status: cleared（2026-10-19 Q1 の判定: T1-748 で mips32・mips64 malta の回帰 PASS、`?` の直し（ea3a89dc0）の後の T1-750 で切り替え on の `forced=1` と通常の `alias=` の数字を画面で確認。QEMU のみ、VTAG・L2 の整合は QEMU では見えず実機の p006 で）
- 設計: [design.md](../design.md) 改訂 2.1 の §2.2・§2.3・§3.1 の p002。ユーザーの判断は [ws.md](../ws.md) の「決定」節（D8 A: mips32.h の comment の差分を承認）。

## 範囲と受け入れの条件

design.md §3.1 の p002 の行: Cause の消去と EBase+0x200 の vector、PageCtrl、入口の全体の cache の操作の新しい関数、locore.S から呼ぶ葉の `mips_board_early_mark`（malta は空）、timer の bsp 化と tick の経路・IM の mask、VTAG の I cache の 3 項、L2 の操作、表示の名前の hook、判定の行の分割、console の共通化、試験用の build の切り替え（既定 off）。malta の振る舞いは不変。確認: build（mips32-malta・mips64-malta、切り替え on・off、warning 0）、host 試験、T1 の malta の回帰＋切り替え on の起動 1 回。

## 実装（2026-10-19 P1、commit 8b44436a0）

| 項 | 変更 |
| --- | --- |
| Cause・0x200（B1） | `locore.S` の先頭で `mtc0 $0, $13`。Status は既存の `MIPS_STATUS_KERNEL_ADDRESSING`（mips32 は 0、mips64 は KX\|UX）を丸ごと書くので BEV・ERL は落ちる（確かめた）。`vectors.S` に `.org MIPS_VECTOR_INTERRUPT`（0x200、`asm.h` に追加）で `j mips_trap_entry`。0x180 の直後・mips64 の `.text` への切り替えと `mips_early_vector` より前（mips32 は `.text.vectors` の中で early_vector より前）。objdump で mips32 の 0x80101200 が `j mips_trap_entry`、`mips_early_vector` は 0x80101208 を確かめた |
| PageCtrl | `space.c` の `mips_space_init` で、PRId の company が 0xd1 の時だけ TLB の初期化の前に CP0 5 sel 4 へ `0xa9000000`（`asm.h` に定数） |
| 入口の全体の cache の操作（M-a） | `cache.c` の `mips_cache_flush_all_early()`: Config1・Config2・PRId をその場で読み、L1 D（Index_Writeback_Inv_D 0x01）→ L2（Index_Writeback_Inv_S 0x03）→ L1 I（Index_Invalidate_I 0x00）を全 way・全 set。global を使わない（stack の局所だけ）。呼ぶ所は `mips_cmain` の先頭（locore.S の bss の消去と stack の後、`mips_board_boot`・`mips_board_init` の前）。L2 が無い時は 0 回で回る |
| `mips_board_early_mark` | `bsp.h` に宣言、`asm.h` に段の番号（START 1・C 2・DISPLAY 3）。locore.S は引数の退避の後に `jal mips_board_early_mark`（a0 = 1）。cmain は flush の後に 2、`mips_board_init` の前に 3。malta は `bsp-malta/early.S` の `jr $ra`（stack・global を使わない） |
| timer の bsp 化（M2） | `bsp.h`: `mips_board_timer_init(hz)`（counter の rate を返す）・`mips_board_counter_read()`・`mips_board_interrupt_mask()`、`mips_board_interrupt(pending)` は served を返す。`clock.c` は tick の数と kernel への受け渡し（`mips_clock_tick()`）と `hal_rtc_*` だけ。malta の Count/Compare と wrap の数えは `bsp-malta/timer.c` へ移し、`mips_board_count_frequency` を消した。`int.c` は pending を bsp に渡すだけ（IP7 の経路は malta の bsp の物）。`wait_for_ticks` の IM は `mips_board_interrupt_mask()`（malta は今と同じ IP7\|IP2）。**`mips_irq_begin_local`・`MIPS_ACK_LOCAL`**: `irq.c:120` は入れ子の検査と ack の記録だけで IP の番号に依らないので、bsp が flag を消してから `mips_clock_tick()` を呼べば IP2 の経路でもそのまま使える（code を読んで確かめた。bsp の ack は不要） |
| VTAG の I cache（M3） | `mips_cache_init` が PRId 0xd1 で `cache_virtual_tags`。`mips_cache_virtual_tags()`・`mips_cache_invalidate_instructions()`（Index_Invalidate_I で全体）。(1) `asid_assign` の世代の切り替えで `tlb_flush_all` の後に I 全体。(2) `hal_space_prot_query` で EXEC が付く時、各 page を kernel の alias で `hal_dcache_clean_range` し、最後に I 全体。(3) `hal_sync_instruction_stream`（`hal_icache_invalidate_range` もここを通る）は D の clean の後に I 全体 |
| L2（M4） | `mips_cache_init` が Config1.M → Config2（SS・SL・SA）を読む。Ingenic では way を 4 に。L2 が在る時は `CACHE L2` の行、VTAG の時は `CACHE I VIRTUAL TAGS forced=…` の行を出す（malta は出ない）。`hal_dcache_clean_range`・`_invalidate_range`・`_clean_invalidate_range` は L1 の後に L2 の Hit（0x1b・0x13・0x17）。`hal_pmem_map_uncached` と kernel の DMA の同期はこれらを通る |
| 表示の名前 | `mips_board_display_name()`（malta は "VGA"）、`cmain.c` の固定の "VGA" を置き換えた |
| 判定の行の分割 | `cons.c` の `mips_cons_print_words(pieces, count)`: 語の境で grid の列の数で折る。`HAL READY` の行はこれで出す（80 列では今と同じ 1 行） |
| console の共通化（Minor 5） | `bsp-malta/cons.[ch]`・`font.[ch]` を `src/hal/mips/` へ `git mv`。`struct mips_cons_display`（列・行・cell・cursor の callback）を `mips_cons_attach()` で渡す。grid は 80×30 の最大の記憶で、大きさは attach の時の値（違う大きさなら空にして cursor を左上へ。小さい display の board は印字の前に attach する）。名前は `malta_cons_*`・`malta_font8x16` → `mips_cons_*`・`mips_font8x16` |
| early exception の hook | `mips_early_exception()` は印字の後・停止の前に `mips_board_early_fault(code)`（malta は空） |
| 試験の切り替え（Minor 12） | `CONFIG_MIPS_FORCE_VTAG_L2_PATHS := y` で `-DMIPS_FORCE_VTAG_L2_PATHS`（両 vmunix.mk、既定 off）。on の時は VTAG の全体の消去と L2 の Hit を L2・VTAG の無い malta でも通す（L2 の Index は 0 個の set で回る）。config は [tests/config-mips32-malta-force.mk](../tests/config-mips32-malta-force.mk)・[tests/config-mips64-malta-force.mk](../tests/config-mips64-malta-force.mk) |
| D8 | `proposed/mips32-h-timer-comment.diff` を `include/hal/arch/mips32.h` に適用（comment だけ、ユーザー承認済み）。他の include/hal は変えていない |

- 規約の注記: coding-style §12 は試験だけの切り替えを production の code に置かない事を求める。この切り替えは承認済みの設計（design.md §3.1 p002、Minor 12）の指定で、環境変数ではなく config の compile 時の define。p007（規約の全文）で扱いを再確認する。
- clang-format: 手元は clang-format-19 だけで、変更前の `src/hal/mips/*.c` も大きく差が出る（例 cache.c で 128 行）ため流していない。書き方は近くの code に合わせた。
- 外部の参照: Linux master `arch/mips/kernel/cpu-probe.c`（PageCtrl の値と company）、`arch/mips/mm/sc-mips.c`（JZ4770 の L2 の way）を「Value(s) referenced:」で comment に書いた（値だけ、code は写していない）。

## 確認（2026-10-19 P1、host のみ。QEMU・実機は未実施）

- build（`make -j16 BUILD=build/ws273-p002/<名> ZEDBSD_CONFIG=<config> vmunix`）: mips32-malta・mips64-malta（`plan/tools/ports/config-mips{32,64}-malta.mk`）と切り替え on（`plan/ws273/tests/config-mips{32,64}-malta-force.mk`）の 4 つとも rc 0、log の `warning:` は 0 件。on の log に `-DMIPS_FORCE_VTAG_L2_PATHS` が入る事を確かめた。
- host 試験: [tests/cons-host-test.c](../tests/cons-host-test.c)（`cc -std=c11 -m32 -Wall -Wextra -Werror -DHAL_ARCH_MIPS32 -Iinclude -Isrc plan/ws273/tests/cons-host-test.c -o <out> && <out>`）PASS: 80 列で `MIPS32 HAL READY DISPLAY LCDC PAGING PASS TIMER TICK` が 1 行、40×15 で `…PAGING` / `PASS TIMER TICK` に折れる、attach で 600 cell を描き範囲外 0、長い語。既存の `plan/tools/ports/mips-pte-host-test.c`・`mips-status-host-test.c`（mips32・mips64）PASS。
- 未実施: QEMU（T1 に依頼）、実機（p003 から）。QEMU は cache を模擬せず IV も立てないので、B1・M3・M4 の誤りは QEMU では見えない（design §3）。

## T1 への依頼（malta の回帰と切り替え on の起動）

tree は p002 の commit 8b44436a0 を含む main。QEMU のみ。

1. mips32-malta の回帰: `make -j16 BUILD=build/t1-ws273p002/m32 ZEDBSD_CONFIG=plan/tools/ports/config-mips32-malta.mk vmunix` → `BOOT_MODE=mips32-malta BOOT_MEMORY=256M plan/tools/boot-test.sh build/t1-ws273p002/m32/vmunix`。画面に `PAGING PASS`・`MIPS32 HAL READY DISPLAY VGA PAGING PASS TIMER TICK`（1 行）・`MIPS32 CPU 1 ONLINE`・`VFS initialization failed`（Master の Tools の MIPS の行と同じ）。
2. mips64-malta の回帰: 1 と同じで `config-mips64-malta.mk`・`BOOT_MODE=mips64-malta`、`MIPS64 HAL READY DISPLAY VGA PAGING PASS TIMER TICK`。
3. 切り替え on の起動 1 回（mips32）: `ZEDBSD_CONFIG=plan/ws273/tests/config-mips32-malta-force.mk`、`BUILD=build/t1-ws273p002/m32-force`、boot-test は 1 と同じ判定に加えて画面に `MIPS32 CACHE I VIRTUAL TAGS forced=1` が出る事。hang・例外（`EARLY EXCEPTION`・`FATAL`）が無い事だけを見る（整合の誤りは QEMU では見つからない）。
4. 余裕があれば 3 を mips64（`config-mips64-malta-force.mk`）でも 1 回。

## T1-748 の結果と直し（2026-10-19、q1331-i01）

- 観測（T1-748、QEMU）: 切り替え on の mips32 malta で `MIPS32 CACHE I VIRTUAL TAGS forced=?`、通常の malta で `CACHE I … alias=?`。
- 原因: `mips_cons_print_words` ではない。MIPS の `hal_printf`（src/hal/mips/lib.c）は `%c %s %u %x %p %%` だけを持ち、それ以外は `?` を出す。src/hal/mips/cache.c の 2 行が `%d` を使っていた（`alias=%d`・`forced=%d`）。
- 直し: cache.c の 2 行を `%u` と `(unsigned)` の cast に（値は 0 か 1）。hal_printf は変えない。
- host 試験: cons-host-test.c に、cache.c の `hal_printf(` の行の変換が hal_printf の持つ物だけかを見る試験を足した（`CACHE_C=<file>` で別の file を指せる）。直した cache.c で `PASS cons`、直す前の cache.c（`git show HEAD~:src/hal/mips/cache.c`）では 2 行を FAIL と出す事を確かめた。
- build: mips32・mips64 malta の切り替え off・on の 4 つ（`BUILD=build/ws273-p002b/{m32,m64,m32f,m64f}`）rc 0、warning 0。QEMU は未実施。

### T1 への再試験の依頼（T1-748 の後）

tree は本 commit を含む main。QEMU のみ。上の「T1 への依頼」の手順と同じ。

1. (3) 切り替え on の mips32 malta（`plan/ws273/tests/config-mips32-malta-force.mk`）: 画面に `MIPS32 CACHE I VIRTUAL TAGS forced=1` と `CACHE I … alias=0` または `alias=1`（`?` でない数字）。
2. (4) 切り替え on の mips64 malta（`config-mips64-malta-force.mk`）: `MIPS64 CACHE I VIRTUAL TAGS forced=1` と数字の `alias=`。
3. (1) 通常の mips32 malta（`plan/tools/ports/config-mips32-malta.mk`）: `alias=` が数字で、`VIRTUAL TAGS` の行は出ない。ほかの判定は上の 1 と同じ。

## 残り

- T1 の結果を Q1 が判定するまで cleared にしない。
- p003（bsp-jz4770 の最小）は D1・D5・D7 の決定済み（ws.md）なので依存は p002 だけ。
