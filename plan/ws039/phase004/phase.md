<!-- awesome-plan project=zedbsd record=ws039-p004 -->

# ws039-p004: 段 A3t・A3・A4（Tier S）

Status: cleared（2026-10-16 Q1: 実機 10.0.10.3 で 3 回（warm の後・cold の後を含む）A3t skip・A3 ok・A4 ok mode=S、MSI の到着、Linux の値と照合が一致。main の merge ee5e55768、ci の vmunix warning 0。未確認: VM fault（A5）、training の道（この VBIOS は求めない）、high alert・CTF の fault、ASD の session の値）
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

## 実機（2026-10-16、q1213-i01、10.0.10.3 の RX 5600 XT、USB の zedBSD は DHCP で 10.0.30.5）

手順: Debian で USB の sda2 を mount し `vmunix` を差し替え（vmunix SHA-256 405dc28d…eef、前の物は Debian の `~/ws039/vmunix.a2`）→ `hw-run.sh boot 0001` → `AMDRDNA_ZED=root@10.0.30.5 hw-run.sh collect` → zedBSD の SSH で firmware 11 file を `/lib/firmware/amdgpu/` に scp（cksum が cache と一致）、新しい `amdrdnactl` を `/tmp` へ → `amdrdnactl advance A4` → 40 秒後に `status` と `dmesg`。証拠 `build/tmp/amdrdna-hw-p4-{1,2,3}/`（worktree の中、commit しない）。zedbsd.cfg は p003 のまま（`amdrdna.auto=A1v`）。

**1 回目**（11:36、電源は p003 からの流れ（Debian の起動から 1 時間 20 分）、A0 `sos=0 smu=0x0`）: `advance A4` で A2 → A3t → A3 → A4 が続けて ok。
- `A3t skip: the vbios does not ask for the long training`、`A3t ok train=skip`（VBIOS の caps=0x1）
- `A3 firmware 11 files sos=0x00100d50 kdb=3872B sysdrv=100208B sos=78640B toc=1536B`、`A3 buffers private=0x200000 ring=0x80007eb000 …`、`A3 ih on, fault interrupts on`、`A3 boot kdb/sysdrv/sos … ok`、`A3 sos alive 0x04302b5f`、`A3 ring 0x80007eb000 4096B`、`psp load toc ok`、**`A3 toc tmr=9216KiB at 0x8010000000`**（Linux の dmesg の「reserve 0x900000 … for PSP TMR」と同じ大きさ）、`psp load smu ok`、`psp setup tmr ok`、sdma0・sdma1・ce・pfp・me・mec・mec2・rlc-control・rlc-gpm・rlc-srm・rlc の load ok、`psp autoload rlc ok`、`psp load asd ok`
- **`A3 ok ih=ring0 msi=0 sos=0x00100d50 tmr=9216KiB fw=12 asd=0 warnings=0`**（PSP の応答の status は全部 0）
- `A4 fan duty 50% (limits 0x96091a7d, duty 0x0181333e, mode 0x02c90c00)`（host 試験の期待値と同じ語）、`A4 alerts high=118C low=0C control=0xb8ff0076`（Linux が書いた値と同じ）、**`A4 irq check low=66C: low alerts 0 -> 1, messages 0 -> 1, entries 1, unknown 0`**（IH の enable の後に MSI が届き、IH の thread が THM の H2L の entry を dispatch した）
- **`A4 ok if=0x37 smu=42.65.0 mode=S duty=50% fan=1285rpm driven=50% temp=46C max=46C limit=95C alert=118C irq=thm`**（fan は回り始め、10 秒後の status は 2127〜2129 rpm、温度 44〜45 ℃、監視の thread が 1 秒ごとに更新）。Linux の SMU IF 0x37（`SMU11_DRIVER_IF_VERSION_NV10`）と一致。
- zedBSD の reboot（shutdown の hook: A4 → PSP → A2 の停止）→ Debian が約 40 秒で戻った（停止の印の行は log の file system の後なので disk に無い）。

**2 回目**（11:40、Debian を経た warm reboot の後）: A0 は **`sos=0 smu=0x0`** → この機械は warm reboot で GPU の PSP・SMU が reset される（14 節の 2 の観測）。`advance A4` で 1 回目と同じ行（`A3 sos alive 0x03d0e684`、`A4 ok … fan=1280rpm … temp=46C … irq=thm`）、status は 2146 rpm・45 ℃。

**3 回目**（11:43、cold power cycle の後: Debian を `systemctl poweroff`、ユーザーが電源ボタン）: A0 `sos=0 smu=0x0`、`advance A4` で同じ結果（`A3 ok … tmr=9216KiB fw=12 … warnings=0`、`A4 ok … fan=1282rpm driven=50% temp=46C … irq=thm`、status 2127 rpm・45 ℃）。その後 zedBSD の reboot で Debian に戻した（11:44）。

照合: TMR 9216 KiB と SMU IF 0x37 は Linux（`temp/dump/dmesg-load.txt`、smu_v11_0.h）と一致。THM の alert の語 0xb8ff0076 は Linux が実機に残した値と一致。温度 45〜46 ℃ は p002 の cold の dump（CG_MULT_THERMAL_STATUS 0x5a2d = 45、hwmon の edge 43 ℃）と同程度。fan の回転は tach の値（rpm）で確かめた（目視・音は未確認）。

確かめていない物（この Phase では起こせない・通らない）: memory training の道（この VBIOS は求めない）、VM fault の受け（CP・SDMA が動く A5 で）、THM の high alert と SMUIO の CTF と温度の上限による fault（起こしていない）、PSP・SMU の停止の行の disk への記録、ASD の session 0 が正しいか（Linux の値は見ていない）。

## 状態と次

- [x] 実装、host 試験、build、firmware の package と取得
- [x] 実機（10.0.10.3）: A3t skip・A3 ok・A4 mode=S ok を 3 回（warm reboot と cold power cycle の往復を含む）
- [x] zedBSD-firmware への配置（Q1、commit b6ae44174）と main の取り込み（d00649763）。package は zedBSD-firmware の `amd-navi/` から 13 file を取り照合が通る（2026-10-16 11:4x、upstream への fallback 無し、新しい cache の root で確かめた）
- [x] `config/drivers/firmware.list` の行（a7567b516）と `config/ci/config-amd64.mk` に `amd-navi-firmware`（063d7656e、ユーザーの決定「AMD の firmware を CI・release の amd64 の image に入れます。」）。CI は `yes | make firmware-licenses` で他の firmware と同じく LICENSE.amdgpu を受ける（`make firmware-licenses-pending ZEDBSD_CONFIG=config/ci/config-amd64.mk` に amd-navi-firmware が出る）。menuconfig の check_driver_firmware の amd64 の部分（firmware.list と config-amd64.mk の不足）は 0
- [ ] Q1: merge と clearance の判定
