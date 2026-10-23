<!-- awesome-plan project=zedbsd record=ws289-p007 -->

# ws289-p007 規約の全文の見直し

- 親: [ws289](../ws.md)。対象: WS289 p002〜p006 が変えた C の source（style だけ。動作・API・ABI は変えない）。
- Queue: Q1 の割当（P1、2026-10-23、worktree `/home/awe/zedBSD-worktrees/p1-ws289-p007`、branch `agent/p1-ws289-p007`、基点 main c327d0c73）。
- Status: in-progress（Q1 の判定待ち）。

## 読んだ file（全部を読んだ）

`src/drivers/mmc/sdhci.c`・`include/drivers/mmc/sdhci.h`・`src/drivers/platform/rpi4/rpi4-sdhci.c`(.h)・`src/drivers/ethernet/cadence-gem.c`(.h)・`src/drivers/platform/rpi5/rp1-ethernet.c`・`src/drivers/platform/zybo/zynq-slcr.c`(.h)・`zynq-sdhci.c`(.h)・`zynq-ethernet.c`(.h)・`src/drivers/ethernet/rtl8211e.c`(.h)・`src/kern/platform/zybo.c`・`src/kern/boot.c`・`include/kern/boot.h` の WS289 の hunk・`src/drivers/fs/fat.c` の readdir の hunk・`tests/sdhci-host-test.c`・`tests/cadence-gem-host-test.c`(.h は未変更で通読のみ)。Python・shell は `py_compile` / `sh -n` だけ（問題なし）。

## 機械の検査

`plan/tools/style-check.py` を対象に流した結果、直す前から違反は goto（`cadence-gem.c` 8・`rp1-ethernet.c` 3・`zynq-ethernet.c` 4、いずれも 1 つの `fail:` への前向きの jump で規約が認める形）と `cadence-gem-host-test.c` の blank-after-brace 5 だけ。直した後は goto だけ。

## 直した事（書き方だけ）

- `fat.c` readdir: 2 つ目の `if` に目的の comment、1 つ目の comment に「負の位置」の説明。
- `sdhci.c`: `sd_ioctl` の `(void)x;` を `UNUSED_PARAMETER`（file の先頭に局所の macro、`zynq-*.c` と同じ形）に。
- `rp1-ethernet.c`: `(void)phy/bus` を `UNUSED_PARAMETER` に（宣言群と first 文の規則どおり）。複数の操作が 1 つの paragraph に詰まっていた所（`describe_phy`・`enable_clocks`・`clock_rate`・`reset_phy`）に comment と空行を足して paragraph を分けた。
- `cadence-gem.c`: `choose_mac` の loop に目的の comment（paragraph 分割）、`read_configuration` の CFG6/CFG8、`reset_controller` の 2 つの screener の loop、`wait_register` の ticks 換算に comment。
- `rpi4-sdhci.h`: `#include` と `struct disk;` の間の空行。
- `tests/sdhci-host-test.c`: `return fake_buffer_read();`（関数の戻りを直に return）を変数に受けて return。
- `tests/cadence-gem-host-test.c`: 閉じ括弧の後の空行と comment 5 件（`drv_rp1_map` の 2 つの `if` を含む）。

## 残した事と理由

- `goto fail`（上の 15 件）: 1 つの共有 cleanup の label への前向き jump で規約 §14 が認める。ただし `fail:` が関数の最後で失敗を返すため「最後が成功の return」とは形が違う（`drv_*_init` と `publish_interface`）。直すには失敗の報告を呼び出し側に移す必要があり構造の変更なので残した。
- `sdhci.c` の `transfer_block_recovering` の最後の `return error;`（comment は `Failed:`）: retry を使い切った失敗の経路で、成功の return は loop の中にある。構造を変えずに済ませるため残した。
- `sdhci.c` `controller_init`（約 250 行）・`start_controller` は長いが paragraph 単位の comment は揃っている。分割は動作に触れる恐れがあり行わない。
- `zynq-ethernet.c` の `UNUSED_PARAMETER` 等、局所 macro は他の driver と同じ慣行。
- `drv_cadence_gem_attach` は `result` の NULL を確かめない（欠陥ではなく glue が必ず渡す。動作の変更になるので直さず記録だけ）。
- 他の WS の code の既存違反は記録せず（今回の対象で見つけたものは無い）。`fat.c` の readdir の外の違反（機械の検査で 400 件超）は WS289 の hunk ではないので触れない。

## 確認

- `make -j8 ZEDBSD_CONFIG=config/ci/config-{zybo,armv7-virt}.mk BUILD=build/p007-<c> vmunix` → 両方 rc 0、`warning:` 0。
- `make -j8 -o build/p007-<c>/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-{rpi4,rpi5,amd64}.mk BUILD=build/p007-<c> vmunix` → 3 つとも rc 0、`warning:` 0、`arm64 vmunix check: PASS`・`amd64 vmunix check: PASS`。
- `make … config-rpi5.mk cadence-gem-host-test` → trace が `tests/cadence-gem-rp1.trace` と `cmp` 一致、`cadence-gem-host-test: PASS`。
- `make … config-rpi4.mk sdhci-host-test` → `sdhci-host-test: PASS`。
- `git diff --check` 空。
- build は worktree の `build/llvm`・`build/NoctLang`・`build/llvm-source` を main の checkout への symlink にして行った（読むだけ）。QEMU・実機は未実施（動作は変えていない。必要なら T1 の回帰は Q1 の判断）。
