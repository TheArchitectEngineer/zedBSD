<!-- awesome-plan project=zedbsd record=ws039-p004 -->

# ws039-p004: 段 A3t・A3・A4（Tier S）

Status: in-progress（2026-10-16 P3、q1213-i01。実装・host 試験・build 済み、実機は未）
Disposition: normal
Parent: [WS039](../ws.md)
実行者: P3（worktree `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3-ws039`、base main `6bbfcb15a`）

## 範囲（[design.md](../design.md) §16 の p004 の行）

firmware の package（zedBSD-firmware の `amd-navi/` への file の一覧を Q1 へ）、container の parser、root の mount の後の load、memory training（console の停止・退避・blank・training・戻し）、IH の enable、PSP の bootloader・ring・TOC・TMR・LOAD_IP_FW・AUTOLOAD・ASD、SMU の mailbox と版の読みと Tier S（固定 duty 50% の fan、温度の読み、監視の thread）、停止の PSP・SMU の部分、host 試験 (a)(c)。**実機: A3t・A3 の ok と A4 の `mode=S`（温度と fan の回転が読め fan が回る。cold power cycle の往復を含む）**。p003 の残り（IH の enable・MSI の到着・VM fault の受け、`amdrdna_fault_interrupts` を on）も A3 で。

承認: q1213（2026-10-16 ユーザー「進めてください。コールドブートは言ってくれればやります」と design §15 の回答 #3 blank・#4 CTF は GPU だけ fault・#5 Tier S の fan は 50%）。

## 実装（2026-10-16 P3、実機前）

driver（`src/drivers/gpu/amdrdna/`）:
- host でも動く純粋な file: `firmware-format.c`（共通の header の検査、sos の 4 片（sysdrv・sos・toc・kdb）、mec の program と jump table、rlc の program と 3 つの list、それ以外は ucode 全体。全部の片が file の中か、header の大きさが file と一致するか）、`psp-format.c`（ring の frame、command（LOAD_IP_FW・LOAD_TOC・SETUP_TMR・LOAD_ASD・UNLOAD_TA・AUTOLOAD_RLC・DESTROY_TMR）、reply の読み、wptr の進め、training の c2p・p2c の位置）、`thermal-format.c`（温度の word、fan の duty・mode の word、tach → rpm、駆動中の duty、THM の alert の word）。
- `firmware.c`: `/lib/firmware/amdgpu/` の file を root の mount を待って全部読む（nvrtx の file.c と同じ形、独自の名前）。
- `training.c`: 段 A3t。skip の理由（`amdrdna.train=0`、VBIOS 無し、VBIOS が long training を求めない、SOS が既に居る）→ skip でも ok。走る時は console 停止 → 下 32 MiB を system memory に退避（GOP の部分は MM_INDEX の間接、残りは窓）→ HUBP の blank（`amdrdna.train.blank=0` で無し）→ bootloader に long train（c2p >> 20、100 ms × 30）→ 書き戻し・HDP flush・unblank・console 再開 → p2c の 4 KiB を保存。どちらでも最後に下 32 MiB を pool に返す（他の予約の範囲を除いて）。
- `psp.c`: 段 A3。SOS が居れば fail（cold power cycle 要）→ 11 file を読み header を検査 → buffer（fw_pri 1 MiB の system memory を GART に 1 MiB 整列で、ring・fence・command の 3 page を CPU の VRAM pool、program ごとの system memory を GART に）→ **IH の enable と hub の fault 割り込みの on** → bootloader で KDB → SYS_DRV → SOS（20 ms 後に C2PMSG_81 ≠ 0 を待つ）→ ring の作成 → LOAD_TOC（TMR の大きさ）→ TMR を GPU だけの pool に 1 MiB 整列で → LOAD_IP_FW SMU → SETUP_TMR（MC と物理の両方）→ SDMA0・SDMA1・CE・PFP・ME・MEC1・MEC2・RLC の control・GPM・SRM・RLC_G → AUTOLOAD_RLC → LOAD_ASD。途中の失敗は停止を 1 回流す。停止: UNLOAD_TA（ASD）→ DESTROY_TMR → ring の破棄（SOS は残る）。
- `smu.c`: 段 A4 の Tier S。MP1 の FIRMWARE_FLAGS の bit 0 を待つ → GetDriverIfVersion・GetSmuVersion（mailbox: 最初の message 以外は前の応答を待つ、応答 0 は hung）→ fan を THM の static duty（`amdrdna.fan.duty=`、既定 50%）→ THM の alert（high = VBIOS の shutdown 温度（118 ℃）、low 0）を IH へ、MP1 の sw 割り込みの mask を外す → **IH の確認**（low の閾値を温度 + 20 ℃ に一時的に上げ、H2L の割り込みが IH の thread に数えられるかを 500 ms 待ち、戻す）→ fan の回転を最大 5 s 待つ → 温度（CG_MULT_THERMAL_STATUS の bit 17:9、Linux の vega10 と同じ読み）と最高値（bit 8:0）、rpm、駆動中の duty → 監視の thread（1 秒ごと、`amdrdna.temp.max=`（既定 95 ℃）以上・THM の high alert・SMUIO の CTF で device を fault（印と status の flag、system は止めない））。停止: thread を止め、alert と sw 割り込みを mask、PrepareMp1ForUnload（fan の static は残す）。
- `ih.c`: `amdrdna_ih_enable`、THM・SMUIO・MP1 の entry を `smu.c` へ。`gmc.c`: `amdrdna_vram_cpu`・`amdrdna_vram_take`・`amdrdna_training_range_release`。`mmio.c`: `amdrdna_vram_write`。`stage.c`: `amdrdna_parameter_number`（`<key>=<10 進>`）。`amdrdna.c`: 最後の段 A4、A3 の ok の後は target を A4 に上げる（A3 と A4 は 1 回で続く）、shutdown は A4 → A3 → A2 の逆順。
- UAPI（`include/uapi/amdrdna.h`）: status を 352 byte に（training の結果・理由・ms、TMR、program 数、SOS の版、PSP の警告数、SMU の版・IF、fan の mode・duty・rpm・駆動中の duty、温度・最高値・上限、alert の数、IH の entry 数と未処理の数、flag 4 つ）。`amdrdnactl status` に 3 行。
- build の行（`platform/amd64/vmunix.mk`）に 7 file。

設計からの変更・決めたこと:
- PSP の ring・fence・command buffer は VRAM（Linux が VRAM|GTT で取ると VRAM になるのに合わせた）、fw_pri と program は system memory（Linux の GTT と同じ）。
- program の大きさは piece の大きさそのまま（page の端数は渡さない）。
- Tier S にも THM の alert（high = shutdown 温度）を入れた: 監視の thread の上の hardware の網で、MSI の到着の確認に使える（low の閾値を一時的に上げる）。
- VM fault の受けは、GPU の engine（CP・SDMA）がまだ動かないので A3・A4 では起こせない。実機の確認は A5（p005）に持ち越す（`amdrdna_fault_interrupts(on)` と IH の enable は A3 で入る）。
- 実機の VBIOS は 2STAGE の training の bit が無い（p002 の A1v: caps=0x1）ので、この機械の A3t は `skip` になる見込み（Linux も同じ判定で training しない）。training の道（退避・blank・書き戻し）は実機で通らない。

firmware の package（`userland/firmware/amd-navi/`、package `amd-navi-firmware`、既定 off）: nvidia-gsp と同じ形（zedBSD-firmware の `amd-navi/` → 無ければ linux-firmware tag 20260410 の commit `dc85ccedc9c973682fbcf4d628ca61174bcc3120`、大きさと SHA-256 を照合、license の同意）。`/lib/firmware/amdgpu/navi10_{sos,asd,smc,pfp,me,ce,mec,mec2,rlc,sdma,sdma1}.bin`、`/usr/share/licenses/amd-navi-firmware/{LICENSE.amdgpu,WHENCE}`、manifest。ta・vcn・gpu_info は入れない。試験の image（`plan/ws039/tests/config.mk`）で on。2026-10-16 11:30 に zedBSD-firmware に無いので linux-firmware から取得し照合が通った（`build/sources/firmware/amd-navi/dc85cced…/`）。

zedBSD-firmware の `amd-navi/` に置く file（Q1 経由でユーザーへ）:

| file | byte | SHA-256 |
| --- | --- | --- |
| navi10_sos.bin | 184512 | 3112ea8519545ac89c1623e2e08e841a6a9bc31c2aab2f9b787979e51277e395 |
| navi10_asd.bin | 205312 | 2d084493e763e60cd90ac25b054f73e4857a24864334cd3c5b092f3273cd12de |
| navi10_smc.bin | 267970 | 49ed339d0ff71fe64347380ee12b00c3c69a546f454bcd4323c411c72fe6fb2e |
| navi10_pfp.bin | 263424 | 310032fe0698871b80be6eb5877ae7ed3395cb712629698c5c631e0af7c4e04a |
| navi10_me.bin | 263424 | faf2f9ff33580b53f7b90f410447efa1d2b21366f344ff5a383d074bf1ea8c35 |
| navi10_ce.bin | 263296 | 074ec5270eb7cad73906a1a591c274a40895b170091bc8068180a4c4d7771165 |
| navi10_mec.bin | 268160 | 936de4938bcfd5e1691b1e544247000f7b4b7430eef317e28f8560bb9e88e758 |
| navi10_mec2.bin | 268160 | 936de4938bcfd5e1691b1e544247000f7b4b7430eef317e28f8560bb9e88e758 |
| navi10_rlc.bin | 43952 | 1f52478fb78dee904361b93ad67955fcff151bb7a7772bbfbd2c448c814be1f5 |
| navi10_sdma.bin | 33792 | 3fc635b9a17819eaf13913882676f0cd07a6c1a31c87b3ef0dcedebf594c8dd0 |
| navi10_sdma1.bin | 33792 | 317b398d0563c8c89b10c31712296843143385f5a1ddcee2031536fe67576c3b |
| LICENSE.amdgpu | 2938 | 572872598565dc3513470de971a32bf9db301f47afeef3636345eadae33b2eee |
| WHENCE | 425450 | c282239a5a2d849677e9304e6f361e475e1b6e71e7c771c03f8986f71b309527 |

WHENCE の該当: 「Driver: amdgpu - AMD Radeon」の節の `File: amdgpu/navi10_*.bin` と「Licence: Redistributable. See LICENSE.amdgpu for details.」。

host 試験（実機の証拠ではない）:
- `plan/ws039/tests/amdrdna-psp-test.c`（新、host 試験 (a)(c) と thermal）: 実 file 11 個の片が header の手読みと一致（sos 4 片、mec の jump table 896 byte、rlc の 3 list）、1 byte 短い file の拒否、合成の header（範囲外・32 bit の wrap・header が file より大・sos 1.0・jump table が全体・rlc 2.0・16 byte）、frame の語の位置、wptr の進め（0・1008 の wrap・mod・frame の途中の拒否）、command の語の位置（LOAD_IP_FW・LOAD_TOC・SETUP_TMR・LOAD_ASD・UNLOAD・AUTOLOAD・DESTROY_TMR）、reply の読み、training の位置（6128 MiB で c2p 0x17ef00000・p2c 0x17eff8000）、THM の語（cold の 0x5a2d → 45 ℃、duty 50% → 0x0181333e、mode → 0x02c90c00、tach 0x15e132 → 1046 rpm、alert（118 ℃/0）→ **Linux が実機に書いた 0xb8ff0076 と一致**）。
- `plan/ws039/tests/amdrdna-host-test.sh` に足した（firmware の cache を `AMDRDNA_FIRMWARE` で変えられる）。2026-10-16 11:3x PASS（PASS の行 190、audit-symbols を含む）。監査で Linux の symbol と同じ名前（static の関数 `psp_ring_create`、欄 `fence_value`）を見つけて改名、一般語 30 個を allow list に足した。
- build: `make -j16 ZEDBSD_CONFIG=plan/ws039/tests/config.mk BUILD=build/p3/usb AMDRDNA_RUN=A1v build/p3/usb/vmunix build/p3/usb/bin/amdrdnactl` rc 0、warning 0、kernel include check PASS、amd64 vmunix check PASS。`plan/tools/style-check.py` の違反は新規・変えた file で 0（identify.c・readout.c・vbios.c の既存の条件演算子は範囲外で残る）。`git diff --check` OK。

## 状態と次

- [x] 実装、host 試験、build、firmware の package と取得
- [ ] 実機（10.0.10.3）: A3t（skip の見込み）・A3・A4 mode=S、cold power cycle の往復
- [ ] Q1: merge と clearance の判定、zedBSD-firmware への file の配置（ユーザー）、`config/drivers/firmware.list` に `CONFIG_DRIVER_PCI_AMDRDNA|amd-navi-firmware` の行（所有の外、Q1 に依頼）
