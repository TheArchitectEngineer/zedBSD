<!-- awesome-plan project=zedbsd record=ws279-p002 -->

# ws279-p002 V3D 7.1 の骨格（電源・clock・SMS・識別・MMU・IRQ 2 本、段 V0〜V4）

- 親: [ws279](../ws.md)、担当: P2（2026-10-22、worktree `agent/p2-pi5gpu`）
- Status: in-progress（実装・host・実機の確認済み、Q1 の判定待ち）
- 依頼の元: 2026-10-22 ユーザー「整理の後、GPUのコードを実装していく、です」（Q1 経由、GPU の directory の整理 main 8ed5a230a の後）。design.md §1・§5・§6 に従う。

## 目的と受け入れの条件

1. `CONFIG_DRIVER_BCM2712_GPU`（既定 n）で Pi 5 だけが 7.1 の driver を build する。Pi 4 の kernel.elf と、switch が n の Pi 5 の kernel.elf は変わらない。
2. 起動を止めない: 起動時は V0（dtb の読み・窓の map・IRQ の登録は mask のまま・PM と clock を読むだけ）。電源を入れる V1 以降は root の `sysctl hw.gpu.start=1` で一度だけ走る。どの段の失敗・時間切れも 1 行を log に出して戻る。
3. 段: V0 発見、V1 PM 0x304 の reset 解除・firmware clock 5 を最大で on・SMS の power-off 解除と reset、V2 識別（版 71・rev・QPU 数・VPM・PA/VA 幅）、V3 page table と MMU、V4 IRQ 2 本（hub SPI 250、core SPI 249）。
4. build の warning 0（rpi5 の GPU 入り・既定、rpi4）。

## 作った物

- `src/drivers/gpu/bcm2711/`（design D1: Pi 4 の directory に 7.1 の file を足し、board の source の一覧で選ぶ）:
  - `attach71.c`・`bcm2712-gpu.h`: `drv_bcm2712_gpu_attach()`。V0 の後 `kern_gpu_start_ops` で engine を保持し、`hw.gpu.start=1` で V1〜V9 を走らせる。`rpi4gpu.off=1` も効く。
  - `v3d71.c`: V0・V1、SMS の reset、PM の reset。
  - `v3d71-firmware.c`: Pi 5 の mailbox（bus alias 無し）の property の口。Pi 4 の `rpi4-firmware.c` は alias 0xc0000000 を使い Pi 5 では使えないので、GPU に要る分だけを持つ。
  - `v3d71-hardware.c`: Pi 4 の `v3d-hardware.c` と同じ関数名・契約の 7.1 版（識別、MMU、cache、IRQ 2 本、reset = SMS → PM）。
  - `v3d71-private.h`: `struct bcm2712_v3d`（先頭が Pi 4 の `struct bcm2711_v3d`、SMS の窓・hub の IRQ・版を足す）。
- 共有のまま使う: `stage.c`・`fdt-util.c`・`mmu.c`・`buffer.c`。
- `src/kern/platform/rpi5.c`: `kern_platform_refresh_devices()` の最後（Wi-Fi の後）で attach。
- `Makefile`・`platform/arm64/vmunix.mk`: switch と source の一覧（p004 の 7.1 compiler の block と一緒）。
- `v3d-job.h`: 7.1 の TFU の field の意味を comment で足しただけ（object は不変）。
- 試験の image: `plan/ws279/tests/config-rpi5-gpu.mk`（config-rpi5.mk ＋ switch y）。

## 確認（host、2026-10-22 P2）

- `make ZEDBSD_CONFIG=plan/ws279/tests/config-rpi5-gpu.mk BUILD=build/p2-rpi5-gpu vmunix`: warning 0。
- Pi 4（config/ci/config-rpi4.mk）と Pi 5 既定（config/ci/config-rpi5.mk）の kernel.elf が e7b4e07ad の build と `cmp` で一致。
- 実機: 下の記録。

## 記録（実機、2026-10-22 P2、kei@10.0.30.5、Pi 5 2 GB D0、kernel = bfea9f4a2 の config-rpi5-gpu.mk の build）

- 手順: 起動していた kernel を `/boot/boot0/vmunix.good` に写して cmp、新しい vmunix を写して sync・cmp、reboot（約 30 秒で SSH が戻った）。起動後 `sudo sysctl hw.gpu.start=1`、`dmesg | grep v3d:`。
- 起動時（V0 だけ）:
  - `v3d: V0 hub 1002000000 core 1002008000 sms 1002030800 pm 107d200000`
  - `v3d: V0 irq hub 282 core 281 pm 10c=0000706d 304=00001040`（firmware が既に 0x304 の bit 6 を立てていた。design §8 U1 の答え）
  - `v3d: V0 clock v3d (5) state 1 rate 500000000 max 1150000000`
- start 後:
  - `v3d: V1 SMS TEE 00000050 idle after 0 us, REE 00000200 after 0 us` / `v3d: V1 ok PM 304=00001040 clock 1150000000 Hz`
  - `v3d: V2 hub 42554856 00081117 00001900 00020a10` / `v3d: V2 core 07443356 81001431 c0078101`
  - `v3d: V2 version 7.1 rev 10 compat 2 cores 1 QPUs 12 VPM 64 KiB PA 36 VA 36`（U4・U8 の答え: D0 は 7.1.10、QPU 12、VPM 64 KiB）
  - `v3d: V3 MMU ctl 060d0c01 table 2676000 scratch 238d000`
  - `v3d: V4 IRQ hub 282 core 281 open, pending hub 0 core 0`
- 起動・LAN・SSH は start の前後とも使えた。新しい SError は無い（`rpi5-wifi: routing the pins` の 1 件は前からの物）。
- 残り: 段の印の写真（framebuffer）は撮っていない（dmesg で読んだ）。C1 の基板は未確認。
