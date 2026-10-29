<!-- awesome-plan project=zedbsd record=ws243-p002 -->

# ws243-p002: NVDEC の channel と engine の起動

Parent: [WS243](../ws.md)、設計 [design.md](../design.md) §1・§2・§7（copy engine）
Status: cleared（2026-10-30 q1677-i02 P5。p004・p006 の RTX 2070 の実機で nvdec-channel ok（c4b0）・copy object 4・refused 0）

## 実装（commit）

- 6560dd0f2: `src/drivers/gpu/nvrtx/nvdec.c`（段 nvdec-channel: devinfo の NVDEC0・falcon の context の大きさ・VRAM の instance/USERD と context・system memory の ring/semaphore/push を work の空間に map・channel・BIND・SCHEDULE・PROMOTE・BSP の object・最初の fence、`nvrtx_nvdec_run()` は 1 枚ずつ submit して待つ、timeout で channel を使わなくする）、`nvdec-records.c`（push・pic・slice の表・work buffer の大きさ・RM の params、host 試験の対象）、`nvrtx-nvdec.h`、`platform/amd64/vmunix.mk`。
- 6aa1ffdfc（channel 側の共有 file の hook を 1 commit）: `objects.c`（devinfo の pick に NVDEC と GR の runlist の copy engine）、`nvrtx-gsp.h`、`nvrtx-gpu.h`（`gpu->nvdec`・`work.copy_made`・`NVRTX_HANDLE_WORK_COPY`・`nvrtx_work_root`）、`work.c`（`nvrtx.video=1` の時だけ copy の object、断られても続ける）、`work-records.c`・`nvrtx-work.h`（subchannel 4 の SET_OBJECT）、`stage.c`・`nvrtx-private.h`（`nvrtx.video=1`）、`gsp.c`（start・stop・release）。
- bb263c464: devinfo の NVDEC0 を RM の内部の番号 0x1d に直した（review I1、Linux v6.19 `r570/nvrm/engine.h` の値。control は NV2080 の 0x13 のまま）、context の大きさ 0 は失敗に。

## 確かめた事

- build: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws243 build/ws243/vmunix` → rc 0、`amd64 vmunix check: PASS`、log に warning・error 無し（2026-10-29 13:3x、HEAD eda161137 の時点）。
- host 試験: `plan/ws243/tests/nvdec-host-test.sh` → `nvdec host test: 88 checks, 0 failed`（push・RM の params・devinfo の pick を含む、p003 と共通）。

## 未実施

- 実機（RTX 2070、`nvrtx.video=1`）での段の通過: devinfo の entry、falcon の context の大きさ、channel の確保・BIND・SCHEDULE・PROMOTE・BSP の object が RM に通るか、最初の fence、GR の channel に copy の object（GRCE）が許されるか（design R1・R2）。p004。
- 既定の boot（`nvrtx.video` 無し）が変わらないことの起動の確かめ（code の上では video=1 の時だけ動く）。
