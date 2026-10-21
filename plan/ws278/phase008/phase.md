<!-- awesome-plan project=zedbsd record=ws278-p008 -->

# ws278-p008 規約の全文の見直し

- 親: [ws278](../ws.md)、Queue: q1543 / q1543-i01（P3、2026-10-21）
- Status: in-progress（第 1 部・第 2 部とも済み。rpi4 の QEMU の起動の回帰と Pi 5 の実機は T1・一括の試験の待ち）

## 目的と受け入れの条件

WS278 が変えた C の全部を [plan/coding-style.md](../../coding-style.md) の全文と照らして書き直す。動作は変えない。build（warning 0）と host の試験、rpi4 の起動の回帰（QEMU raspi4b の boot-test、T1）。

## 第 1 部: bsp-rpi-common の分離（2026-10-21 P3）

出典: 2026-10-21 ユーザー「HALは bsp-rpi-common/ に共通部分を隔離するのがいいでしょうか？」→ Q1「そうする」。続けて「Pi4のbspはコーディング規約が守られていないことを発見してしまったので、この機会にbsp-rpi-common/に分離しつつ整理してコーディング規約を守らせましょう。#ifで分岐するより、多少重複があっても、bspをコピーして書き直すのがいいです。共通で使えるところは-common/に移しましょう。」「無理に共通化しないで rpi4のコピーでrpi5を作って、共通部分をまとめるのが私の好みです。」

### 配置

| directory | file | 中身 |
| --- | --- | --- |
| `src/hal/arm64/bsp-rpi-common/` | boot.c・clock.c/h・cons.c/h・fdt.h・font.c/h・framebuffer.c/h・gic.c/h・led.c/h・mailbox.c/h・uart.h | 両方の board で同じ物（git mv で履歴を保った）。board の名前・番地は board.h の定数（`ARM64_BOARD_TAG`・`ARM64_BOARD_HANDOFF`・`RPI_BOARD_MAILBOX_BUS_ALIAS`）から |
| `src/hal/arm64/bsp-rpi4/` | board.h・board.c・fdt.c・uart.c・activity-led.c・locore-board.h | Pi 4 の固定の番地、`arm64_bsp_map_devices`（3〜4 GiB の 2 MiB の device block）、`arm64_bsp_watch_compositor`（HVS 0xfe40_0000）、`hal_reset`（PM の watchdog）・`hal_poweroff`、FDT の compatible、GPIO 14/15 の PL011、GPIO 42 の LED、locore の device の 1 GiB |
| `src/hal/arm64/bsp-rpi5/` | 同じ並び（bsp-rpi4 を写して Pi 5 向けに書き直した） | 0x10_4000_0000 の device、`hal_space_map_device` の写像、PSCI の reset・電源断、HVS 無し、FDT の compatible と /reserved-memory、debug UART、aon_gpio 9 の LED、locore の自分を写す stub |

- arch 共通の file（locore.S・cmain.c・space.c・watch.c・asm.c・bsp.h・defs.h）から `HAL_BOARD_RPI5` の分岐を無くした。`src/hal/arm64` の中に `HAL_BOARD_` は 0 件。board の違いは bsp.h の関数（`arm64_bsp_map_devices`・`arm64_bsp_watch_compositor`、各 board の `hal_reset`・`hal_poweroff`）、board.h の定数、locore-board.h の assembler の macro（`arm64_board_place_image`・`arm64_board_boot_device_block`）。board の directory は `-Isrc/hal/arm64/bsp-$(ZEDBSD_BOARD)` で include の道に入る（platform/arm64/vmunix.mk）。
- 共通の symbol は `rpi4_*` から `rpi_*` へ改名（`rpi_fdt_info`・`rpi_boot_info`・`rpi_gic_*`・`rpi_led_*`・`rpi_uart_*`・`rpi_cons_*` など）。kernel の driver `src/drivers/platform/rpi4/rpi4-console.c` は include の path と `rpi_cons_suspend_output`・`rpi_cons_resume_output` の名前だけ直した。
- 同じにならない物は共通化しなかった: fdt.c（compatible、/reserved-memory は Pi 5 だけ）、uart.c（番地と初期化が違う。putc 等は同じ文面の写し）、activity-led.c、board.c。
- `hal.h` は不変（HAL の実装の整理）。
- 文書の path だけ直した: `bootloader/README.md`（boot.c）、`docs/reference/evdev.md`（cons.c）。

### 規約の書き直し（動作を変えない根拠）

bsp-rpi-common・bsp-rpi4・bsp-rpi5 の全部の file と、arch 共通の file の触った所に全文を当てた: copyright と file の説明、型・file scope の変数の comment、forward declaration、関数の定義の形、条件の中の関数の呼び出しを外へ、`WORD()` の macro を書き出し（framebuffer の要求の 30 語・8 語は添字を数えて元と同じ）、複合条件を分けた（評価の順と short circuit を保った）、宣言を関数の先頭へ、最後の return を成功に。

- 意図した振る舞いの差は 1 つ: cmain.c の「device tree が起動時の表（RAM の最初の 1 GiB）の外なら止める」確かめを Pi 4 にも当てた（Pi 5 だけだった）。Pi 4 の locore も最初の 1 GiB だけを写すので、外の tree は前は parse の読み取りで落ちていた。止まり方だけが変わる。
- Pi 5 の HAL の message で `RPI4 FRAMEBUFFER`・`RPI4 FDT memory unavailable` が `RPI5 …` になった（`ARM64_BOARD_TAG`）。Pi 4 は同じ文字列。FDT の error 名は Pi 5 の parser で `not-rpi5`。
- 確かめ（機械語）: LTO 無し（`ZEDBSD_KERNEL_LTO=none`）で変更前（2026fac56 を `git archive`）と変更後の rpi4 の kernel.elf を関数ごとに逆 asm で比べた（番地・即値・data の配置の差を除き、`rpi4_` を `rpi_` に読み替え）。違った関数は HAL の触った物だけ: `arm64_boot`（label の分割）、`arm64_cmain`（上の確かめ）、`arm64_space_init`・`arm64_watch_init`（board の関数の呼び出しへ）、`hal_reset`・`hal_poweroff`（asm.c から board.c へ移り、LTO 無しで inline されなくなった）、`begin_node`・`draw_locked`・`rpi_delay_ms`・`rpi_led_stage`・`rpi_boot_set_framebuffer`・`rpi_framebuffer_*`・`rpi_mailbox_property`（書き直しによる命令の並びと inline の違い。各 diff を読み、比べる値・分岐の先・書く値が同じことを確かめた）。新しい関数は `arm64_bsp_map_devices`・`arm64_bsp_watch_compositor`・`arm64_space_map_device_block`・`fill_screen`。kernel の他の関数（driver を含む）は同じ。比べた道具は scratchpad の使い捨て。

### 確認（2026-10-21 P3、host）

- `make ZEDBSD_CONFIG=config/ci/config-rpi5.mk BUILD=build/rpi5 build/rpi5/kernel.elf -j16`: rc=0、warning 0、`arm64 vmunix check: PASS`。
- `make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p3-rpi4 build/p3-rpi4/kernel.elf -j16`: rc=0、warning 0、`arm64 vmunix check: PASS`。
- `make … rpi5-fdt-host-test`: PASS（C1: uart irq 153、D0: irq 152、4 CPU、1 reserved）。
- `build/p3-rpi4/tests/rpi4-fdt-host-test <main の build/sources/firmware/rpi4-boot/3d301dd…/bcm2711-rpi-4-b.dtb>`: PASS（4 CPUs, gic ff841000/ff842000）。worktree の make は rpi4 の firmware の license の確認で止まるので、main の cache の dtb を読むだけで使った。
- `git diff --check`: 問題なし。
- 未実施: rpi4 の起動の回帰（QEMU raspi4b の `plan/tools/boot-test.sh`）。Q1 経由で T1 に依頼する。Pi 5 の実機は WS278 の最後の一括の試験。

## 第 2 部: p003〜p007 の C（2026-10-21 P3、q1543-i02）

対象: `src/drivers/platform/rpi5/` の全部（rp1.c・rp1-gpio.c・rp1-irq.c・rp1-usb.c・rp1-ethernet.c・rpi5-gio.c・rpi5-pinctrl.c・rpi5-wifi.c・rpi5-bt.c と各 header）、`src/kern/platform/rpi5.c`、`tests/rpi5-rp1-host-test.c`、p003 の `src/hal/arm64/smp.c`・`bsp-rpi{4,5}/board.c`・`bsp-rpi5/fdt.c`。

- 書いた時に全文を当て、最後に機械の確かめと読み直しをした: static の関数の forward declaration の有無（script、漏れ 0）、複数行の comment の開き（漏れ 0）、条件の中の関数の呼び出し（rp1-usb.c の 1 件を直した）、閉じ括弧の後の空行（22 か所に段落の comment を足した）、検索の関数の最後を成功の return に（rp1.c の `drv_rp1_pci_to_cpu`・`read_dma_window`、rp1-gpio.c の `find_bank`、rp1-ethernet.c の `read_mac`・`clock_rate`・`stop_transmitter`・`wait_mdio`、rpi5-pinctrl.c の `find_pin` を break と後の判定に書き直した。振る舞いは同じ。`read_mac` は multicast の filter を飛ばして次を見るようにした）、boolean を式で作らない（rpi5-gio.c）。
- 写して書き直した rpi5-wifi.c・rpi5-bt.c は元（rpi4）の形を保ち、Pi 5 の部分を全文で書いた。rpi5-bt.c の comment から開発の管理情報（BUG・試験の回の番号）を落とした（Guardrail の source の中の管理情報の規則）。
- 試験の file（rpi5-rp1-host-test.c）の `failures += check(cond, …)` は既存の host 試験と同じ書き方で残した。
- 確認: rpi5 の kernel build rc=0・warning 0・vmunix check PASS、`rpi5-rp1-host-test` C1・D0 PASS、rpi5 の image の build rc=0（`build/rpi5/hdd-image.img`、root に bcm43455・brcmbt の firmware、config.txt に `pciex4_reset=0`）、rpi4 の `ARM64_KERNEL_SOURCES` は f4cdf706f と同一（291 file、make で展開して diff）。

### 残り

- arch 共通の file の触っていない所（asm.c・page.c・space.c の古い書き方）は WS278 の範囲の外。
- 共有の rpi4 の driver（rpi4-sdio-host.c・rpi4-sdio.c・rpi4-mmc-irq.c・rpi4-bt-link.c・rpi4-sdhci.c・rpi4-console.c）を Pi 5 でも build している。共通の directory（例 `src/drivers/platform/rpi-common/`）へ移すかは、ユーザーの「共通部分を -common/ に」に沿う次の整理（rpi4 の path が変わるので rpi4 の回帰と一緒に、Q1 の判断）。
