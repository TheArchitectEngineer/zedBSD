<!-- awesome-plan project=zedbsd record=ws278-p003 -->

# ws278-p003 Pi 5 の SMP（PSCI CPU_ON）

- 親: [ws278](../ws.md)、Queue: q1543 / q1543-i01（P3、2026-10-21）
- Status: in-progress（実装と host の確認まで。実機の確認は WS278 の最後の一括の試験）

## 目的と受け入れの条件

Pi 5 の 4 つの Cortex-A76 を起こす: device tree の `enable-method = "psci"` の CPU を PSCI CPU_ON（SMC64、smc）で、MPIDR の Aff1 を core 番号に。Pi 4 の spin table の起こし方は変えない。

- build（warning 0、rpi4・rpi5）、`rpi5-fdt-host-test` で 4 CPU が PSCI と読める。
- 実機（一括の試験）: `ARM64 SMP 4 CPUs`、`ARM64 SMP cpu 1..3 online (core 1..3)`、`top` で 4 CPU。

## 設計

- core 番号: Pi 4 の A72 は Aff0、Pi 5 の A76 は MT=1 で Aff1（cpu node の reg 0x000/0x100/0x200/0x300、boot CPU の MPIDR は 0x8100_0000）。board の関数 `arm64_bsp_core_of()`（C）と locore-board.h の macro `arm64_board_core_index`（asm）で同じ計算。core 番号で boot block と GIC の CPU interface を引く（BCM2712 の GIC-400 も core n が interface n と見込む。実機で確かめる、下の R6）。
- 起こし方: `arm64_bsp_cpu_startable()`（Pi 4: spin table と release address、Pi 5: psci）と `arm64_bsp_cpu_release()`（Pi 4: release address に入口を書いて `sev`、Pi 5: CPU_ON 0xc400_0003、x1 = cpu node の reg、x2 = `arm64_secondary_entry` の物理番地、x3 = 0）。CPU_ON が SUCCESS 以外なら起動を止める（kernel は CPU の数を既に聞いている、ws208 D6）。message は `ARM64 SMP cpu N (core C) was not released (E); add cpus=1 …`。
- 入口: TF-A は EL2・MMU off で入れる。`arm64_secondary_entry` の EL2 → EL1 の道は Pi 4 と同じ。
- 出典: PSCI の関数番号と戻り値は Arm DEN0022（board.c の block に Value(s) referenced）。core の Aff1 は Raspberry Pi の dts（bcm2712.dtsi の cpu の reg）。

## 変えた所

- `src/hal/arm64/smp.c`（core 番号と board の起こし方へ。`cpu_mpidr`/`cpu_release_addr` を `cpu_core`/`cpu_entry` に）、`bsp.h`（3 つの関数）、`bsp-rpi-common/fdt.h`（`psci`）、`bsp-rpi5/fdt.c`（enable-method の psci）、`bsp-rpi4/board.c`・`bsp-rpi5/board.c`（実装、Pi 5 の `psci_call` を 3 引数に）、`locore.S`・`bsp-rpi{4,5}/locore-board.h`（`arm64_board_core_index`）、`tests/rpi5-fdt-host-test.c`（psci の確かめ）。
- Pi 4 の振る舞い: spin table の手順は smp.c から bsp-rpi4/board.c へ移しただけ（書く値・順・barrier・`sev` は同じ）。SMP の message の「affinity」は「core」になった（同じ数）。

## 確認（2026-10-21 P3、host）

- `make … config-rpi5.mk … build/rpi5/kernel.elf -j16`: rc=0、warning 0、vmunix check PASS。
- `make … config-rpi4.mk BUILD=build/p3-rpi4 build/p3-rpi4/kernel.elf -j16`: rc=0、warning 0、vmunix check PASS。
- `rpi5-fdt-host-test`: PASS（C1・D0 とも 4 CPU、Aff1、psci、spin table 無し）。
- 未実施: 実機（Pi 5）。rpi4 の SMP の回帰（QEMU raspi4b の 4 CPU、T1、p008 第 1 部の依頼と同じ回で）。

## 危険

- R6: BCM2712 の GIC-400 の CPU interface の番号が core 番号でなければ、SGI（kernel の通知・panic の停止）が別の core に行く。症状は起動後の固まり。実機で `cpus=1` と既定の両方を見る。
