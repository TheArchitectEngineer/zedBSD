<!-- awesome-plan project=zedbsd record=ws268-p001 -->

# ws268-p001 x68k の壊れの洗い出しと直し、build

Status: in-progress（2026-10-19 q1258-i01、P2。build・検査は済み、Q1 の判定待ち。MAME の起動は未実施）

- 親: [WS268](../ws.md)。Queue: q1258-i01。
- 起点: 2026-10-18 ユーザー「x68kはエミュレータのセットアップ、もうわからないですよね？わからなければ、ビルドだけでいいです。virt 68kが動けばいいです。」

## 壊れの洗い出し（2026-10-19）

sun4u（WS267）と同じ型: 2026-10-04 以降の HAL・kernel の変更に x68k（MC68030 の HAL）が追従していなかった。

1. `struct x68k_boot_handoff` の定義が tree に無い（`include/kern/boot.h` に「XXX: To be removed.」の定数だけが残っていた）。
2. kernel の compile が libc の header を読み `KERN_UAPI_NATIVE` 無しで #error、kernel に libc の object を link。
3. HAL の旧 API: `struct hal_pmem`・`hal_pmem_request`・`hal_pmem_stats`、`hal_cons_*`（console と keyboard の入力の API）、`hal_set_allocator`・`hal_malloc`、`hal_task_init`・`hal_task_user_context`・`hal_task_user_stack`、`hal_irq_set_handler`・`hal_irq_service_wait`、`hal_irq_get_affinity` の旧形、`src/hal/pmem-constraints.c`（無い）、`cons-wait.h`（無い）。
4. kernel の platform（`src/kern/platform/x68k.c`）が HAL の内部の header（`hal/m68k/bsp-x68k/bsp.h`・`scsi.h`）を include し、HAL の関数を直に呼んでいた（Guardrail の受け渡しの規約に反する）。
5. link script に `.vfs_bss`・`.net_bss`・`.hightext` が無く、vfs の表が PROGBITS の data（4.2 MB）になり image の raw 領域に収まらない。
6. GCC だけが出す警告: `devfs.c` の maybe-uninitialized、boot stage の RWX segment（平らな block として意図した物）。
7. 存在しない試験の参照（`tests/x68k-image-host-test.py`・`tests/x68k-emulator-rom-host-test.py`）。

## 直したこと

- 受け渡し（Guardrail）: boot loader → HAL の構造体を `bootloader/x68k/handoff.h` に定義（`struct x68k_boot_handoff`・`struct x68k_memory_region`、拡張の magic・版、ram_bytes の offset 32 を _Static_assert）。include するのは loader（stage2.c）と HAL（bsp-x68k/bsp.h）だけ。boot.h の「XXX」の 3 定数を外した。HAL → kernel は共通の record だけ（版は `KERN_HANDOFF_VERSION_X68K` のまま、size は共通の record の大きさ）と名前ごとの値 `x68k.scsi.initiator`（host の SCSI ID）・`x68k.spc.registers`（SCSI controller の最初の register の kernel address）。kernel の x68k.c は HAL の header を読まず、この 2 つから MB89352 の bus を組む。
- HAL（MC68030 と bsp-x68k）: pmem（`bsp-x68k/pmem.c` を今の API の page の bitmap に書き直し、`hal_get_memstat`・`hal_pmem_to_kernel`・`alloc_limited`）、space の table の page の確保と `hal_space_map_device`（装置の窓は boot 時から直接の map にある）、task（create_for_init_context・get_user_context・get_user_stack・kernel_alloc）、irq（register・unregister・新しい get_affinity）、timer（hal_irq_register）、runtime.c（規約の形、allocator の配管を外す）、console.c（hal_putc だけの出力の console、96x32、文字は機械の CGROM）、boot.c・handoff.c・bsp.h・cmain.c（規約の形）。
- 入力: 旧 HAL の keyboard（`hal_cons_*` の入力 API、既に `cons-wait.h` が無く壊れていた）と `scsi.c`（kernel 側へ移した bus）は build から外した。file は残っている（下の残件）。
- kernel: `src/drivers/platform/x68k/x68k-console.c`（新規、/dev/console を teletype として登録、出力だけ）。
- build: kernel と userland の compile を sun4u・m68k-virt と同じ形に（GCC の freestanding header、`-D__ZEDBSD__=1`、libc の header は userland だけ、userland の libc は sysroot と同じ source、libgcc で 64 bit の除算などの helper）、link script の bss、boot stage の `--no-warn-rwx-segments`（理由を comment）、存在しない試験の参照を削除。

## 結果（2026-10-19）

- `make ZEDBSD_CONFIG=plan/ws268/tests/config-x68k.mk BUILD=build/ws268 build/ws268/zedbsd-x68k.hd`: rc=0、warning・error 0。`m68k kernel check: PASS (entry=0x10000, physical_end=0x6f2000)`、`X68k image check: PASS`。
- `check-disk-image`・`x68k-user-abi-check`・`x68k-target-audit`: rc=0。
- 影響の確かめ: amd64 の vmunix（devfs.c・boot.h）rc=0、warning 0、amd64 vmunix check: PASS。m68k-virt の vmunix（`config/ci/config-m68k-virt.mk`）rc=0、warning 0。sun4u の image も rc=0。
- MAME（任意）: `mame x68030 -rompath ~ -hard <image> -video none -nothrottle -seconds_to_run 240`（Lua の停止時の snapshot）を 2 回試したが、画面は status の表示だけで内容が出ず、IPL が SCSI の disk から起動したかも分からない（SRAM の起動の設定など未調査）。→ **未実施**（MAME での起動は確かめていない）。

## 残件

- 使われなくなった file（削除の候補、Q1 に path を送る）: `src/hal/m68k/bsp-x68k/keyboard.c`・`keyboard-map.c`・`keyboard-map.h`・`keyboard.h`・`scsi.c`・`scsi.h`。X68000 の keyboard の入力を戻すなら kernel 側の driver（MFP の USART を読み tty に入れる）が要る（範囲外、WS268 は build だけ）。
- p002 規約の全文の見直し。
