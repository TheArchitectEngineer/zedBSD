<!-- awesome-plan project=zedbsd record=ws282-p001 -->

# ws282-p001 camera の枠組みと rpicam の設計

- 親: [ws282](../ws.md)、Queue: Q1 の割当（P3、2026-10-23）
- Status: in-progress（設計の版 1 を書いた。Q1 が review を手配し判定する）
- 成果物: [design.md](../design.md)

## 目的と受け入れの条件

- `/dev/cam0` の枠組みの API（format の列挙、mode、buffer の queue と GPU との共有、start/stop、frame の metadata、control、sensor と receiver の分離、node の権限）、Pi 5 の rpicam（RP1 の CFE の CSI-2 receiver と DMA、connector と firmware の device tree、sensor の I2C bus と電源の GPIO、OV5647 を最初に・IMX219 を後に）、Phase の分け方と見積もり、試験の計画、危険、ユーザーの判断を design.md に書く。実際の code を読み、Linux の file と関数を段ごとに示す。コードは書かない。
- 受け入れ: design.md が上の項目を全部持ち、Q1 の手配する review で致命の欠落が無い。

## 記録（2026-10-23 P3）

- 読んだ物: design.md の冒頭の資料の一覧（zedBSD の rp1・rp1-irq・rp1-gpio・rp1-ethernet・rpi5-fan・i2c・lpss-i2c・dma・fdt・cdev・vm-device・handle・waitq・poll・pmem・hal.h の cache・uapi の gpu/gpu-allocation/audio/ioctl・gpu.c の export/import・bcm2711 の buffer/share/render-device・devfs・sessiond の seat・libvulkan の memory・rpi5.c・vmunix.mk・config-rpi5.mk、WS278 の設計、WS279/283/284/285 の ws.md、plan/coding-style.md の §1〜2 と見出し、Guardrail の license の節）。Raspberry Pi の Linux rpi-6.12.y の rp1_cfe（cfe.c・csi2.c・dphy.c・pisp_fe.c・cfe_fmts.h と header）、ov5647.c、imx219.c（検出と mode の部分）、clk-rp1.c（MIPI CFG の clock）、pinctrl-rp1.c（pin の function）、i2c-designware-core.h、rp1.dtsi・bcm2712-rpi-5-b.dts・bcm2712-rpi.dtsi、ov5647/imx219 の overlay。firmware の D0 の dtb を `dtc` で読んだ（csi の node は disabled、sensor の node は無い、cam0_reg/cam1_reg は rp1_gpio 34/46、i2c4/i2c6 の address）。
- 実行した command: `git worktree add /home/awe/zedBSD-worktrees/p3-cam -b agent/p3-cam main`（b86842b82）、`curl` で Linux の file を scratchpad に取得（commit しない）、`dtc -I dtb -O dts` で dtb を読んだ。build・QEMU・実機は無し（設計の Phase）。
- 結論: design.md の §0（要約）、§1（責務）、§2（kernel の interface）、§3（ABI）、§4（Pi 5 の方式）、§5（失敗・並行性・資源）、§6（判断 D1〜D7）、§7（試験）、§8（出典）、§9（危険 R1〜R10）、§10（選ばなかった案）、§11（Phase p002〜p009、25〜34 LW）。
- HAL の API（hal.h）の変更: 不要（cache の操作 `hal_dcache_invalidate_range` は既にある）。
- 他の WS の source に頼む変更（Q1 へ）: `src/kern/devfs.c`（`cam` の node を 0600 に）、`userland/desktop/sessiond/seat.c`（`/dev/cam%u` を seat に）、`src/drivers/gpu/gpu.c`・`include/drivers/gpu/gpu.h`（`drv_gpu_shared_backing`、p006）、D2 の (a) なら `src/drivers/i2c/lpss-i2c.c` の core の分離（T1 に x86 の i2c-hid の回帰）。
- 未実施: review（Q1 が手配）、ユーザーの判断 D1〜D7、実機の確認（p003 以降）。
- 自己 review（敵対的に読み直した点）: (1) 最初の案は「camera が export して V3D に取り込ませる」だったが、`gpu_import_ioctl` が別 device の handle を EXDEV で拒む事と V3D の import の種類の追加が要る事を読んで、GPU が確保する向き（案 A）に替えた。(2) RP1 の DMA は dtb に `dma-coherent` が無く cache を見ない事を確かめ、user の mapping を uncached にした（R5）。(3) CSI2-DMA は scatter-gather しないので buffer は連続の物理 memory（R9）。(4) 割り込みの FS/FE の同時到着の 3 通りを Linux の扱いの通りにした。(5) spinlock の下で I2C（眠る）を呼ばない（WS278 の GEM の deadlock の教訓）。(6) sensor の register の表の license（R10、Guardrail の規則）。(7) `drv_i2c_bus_find_acpi` しか探す口が無いので、RP1 の I2C は登録の戻りの handle をそのまま rpicam が持つ。
