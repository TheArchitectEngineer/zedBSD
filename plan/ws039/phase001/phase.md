<!-- awesome-plan project=zedbsd record=ws039-p001 -->

# ws039-p001: amdrdna の調べと設計（初期化の順・firmware・display・packet・段の印・license）

Status: in-progress（2026-10-15 P3、q1202 で再開。design.md・amdrdna-license-audit.md を書き、design-reviewer の review（blocker 3・major 10・minor 20）を全部反映した。clearance は Q1 の判定。設計の判断の項目 12 件はユーザー待ち）
Disposition: normal
Parent: [WS039](../ws.md)
実行者: P3（code は書かない Phase。build の行だけ Q1 の依頼で先に入れた）

## 範囲（Q1 の起動の指示から）

初期化の順（PCI・MMIO・GOP の画面の保持・PSP の bootstrap・firmware の load・SMU・GMC（VM の page table、VRAM・GART）・IH・CP（PFP・ME・CE・MEC・KIQ）・SDMA・fence）、Navi10 の firmware の一覧（upstream の path・大きさ・sha256）、display（DCN 2.0）の GOP の引き継ぎ・mode set・flip・cursor、PM4・SDMA の packet、段の印（A0〜A9、nvrtx の N0・P0〜P9 に倣う）と各段の実機の合格の印（kernel log の行）、RDNA1〜3 の全 chip の device id の表、reboot の時の後片付け、試験の道（素の起動、log は kernel.log、SSH）、危険、license の監査の表。

## 受け入れ

- `plan/ws039/design.md`（commit する）に上の全項目と、zedBSD の interface（`struct drv_gpu_ops`・node・runtime・vk-*・present・scanout・shutdown の hook）への対応、compiler（P5、[p007](../phase007/phase.md)）との program の形の口。
- `plan/ws039/amdrdna-license-audit.md`（[nvrtx-license-audit.md](../../ws037/nvrtx-license-audit.md) の形）。
- design-reviewer の review と指摘の反映。

## 出典の扱い（2026-10-15 ユーザー、Guardrail「MIT・GPL の出典のデータの引用」）

- MIT・GPL のどちらの出典からも、使ってよいのは**データ（定数と表: register の offset と bit、packet の形、firmware の表など）だけ**。block ごとに引用元（path・版・表か define の名前・分かれば行）を comment に書く。データには copyright・MIT の表示を付けない。
- **code・comment・構造は MIT でも写さない**（全 WS に適用）。code・comment・構造は自分で書く。
- design.md もこの規則で書く（値は出典付き、手順は自分の言葉）。

## これまでにしたこと（2026-10-15 P3）

1. **build の行**（Q1 の依頼、commit `2699de765`、main に `464c2b843` で merge 済み）: `CONFIG_DRIVER_PCI_AMDRDNA`（Makefile の既定 n と -D、`config/drivers/pci.drivers`（既定 n）・`menu.list` の GPU、`config/ci/config-amd64.mk` は y、`config-rpi4.mk` は n）、`platform/amd64/vmunix.mk` に空の `AMD64_AMDRDNA_SOURCES` の ifeq（driver の file、その後に P5 の compiler の file）。確認: `make -j16 BUILD=build/p3/amd64 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/p3/amd64/vmunix` rc 0・warning 0・include check と vmunix check PASS。`plan/tools/menuconfig-target-host-test.py` は MAC-T001（VCHIQ）で FAIL するが、変更の前の base でも同じ出力（既存）。
2. **正本の取得**（commit しない、`plan/ws039/temp/`、git の ignore の範囲）:

| 正本 | 版 | 置き場 |
| --- | --- | --- |
| Linux（amdgpu） | tag `v6.19`（commit `05f7e89ab9731565d8a62e3b5d1ec206485eeb0b`、nvrtx と同じ） | `temp/linux/`（sparse: `drivers/gpu/drm/amd/amdgpu/`・`include/`・`pm/swsmu/{smu11,inc,*.c}`・`display/dc/`・`display/include/`、`include/uapi/drm/amdgpu_drm.h`、541 MB） |
| Mesa | tag `mesa-25.3.6`（commit `06f9e28304d5d3f109c33535c1c25b9df5769af2`） | `temp/mesa/`（sparse: `src/amd/`・`docs/license.rst`・`include/drm-uapi/amdgpu_drm.h`） |
| linux-firmware | tag `20260410`（commit `dc85ccedc9c973682fbcf4d628ca61174bcc3120`、AX211・i915・nvidia-gsp と同じ） | `temp/linux-firmware/`（sparse: `amdgpu/navi10_*`・`navi12_*`・`navi14_*`・`WHENCE`・`LICENSE.amdgpu`） |

3. **license の機械判定**（`temp/lic.py`、先頭 6000 byte の SPDX か MIT の permission notice。結果 `temp/linux-lic.tsv`・`temp/mesa-lic.tsv`、path・sha256・license）: Linux の amd の取得範囲 2191 file のうち MIT 2182。MIT でない物は `amdgpu_atpx_handler.c`（GPL-2.0-only）、`amdgpu_vkms.[ch]`（GPL-2.0+）、`amdgpu_preempt_mgr.c`・`jpeg_v5_0_1.c`・`amdgpu_res_cursor.h`・`amdgpu_vm_pt.c`・`amdgpu_vm_tlb_fence.c`（GPL-2.0 OR MIT）、`dcn_4_1_0_sh_mask.h`（表記なし）。Mesa の `src/amd` は MIT 563・表記なし 128（大半が `ci/`・`registers/` の JSON など、Mesa の既定に従う）。今の規則では license に依らずデータだけを使う。
4. **Navi10 の firmware**（tag 20260410、全部 file で link でない。`LICENSE.amdgpu` sha256 `572872598565dc3513470de971a32bf9db301f47afeef3636345eadae33b2eee`、`WHENCE` sha256 `c282239a5a2d849677e9304e6f361e475e1b6e71e7c771c03f8986f71b309527`。license の要点: binary の形の再配布可、notice の再掲が条件、reverse engineering・decompile・disassemble の禁止）:

| file | byte | sha256 |
| --- | --- | --- |
| `amdgpu/navi10_asd.bin` | 205312 | `2d084493e763e60cd90ac25b054f73e4857a24864334cd3c5b092f3273cd12de` |
| `amdgpu/navi10_ce.bin` | 263296 | `074ec5270eb7cad73906a1a591c274a40895b170091bc8068180a4c4d7771165` |
| `amdgpu/navi10_gpu_info.bin` | 772 | `31ce8d3241a5c21b59607c0a456c8396918a47f624343e80a789beac5b4f3395` |
| `amdgpu/navi10_me.bin` | 263424 | `faf2f9ff33580b53f7b90f410447efa1d2b21366f344ff5a383d074bf1ea8c35` |
| `amdgpu/navi10_mec.bin` | 268160 | `936de4938bcfd5e1691b1e544247000f7b4b7430eef317e28f8560bb9e88e758` |
| `amdgpu/navi10_mec2.bin` | 268160 | `936de4938bcfd5e1691b1e544247000f7b4b7430eef317e28f8560bb9e88e758`（mec と同じ byte） |
| `amdgpu/navi10_pfp.bin` | 263424 | `310032fe0698871b80be6eb5877ae7ed3395cb712629698c5c631e0af7c4e04a` |
| `amdgpu/navi10_rlc.bin` | 43952 | `1f52478fb78dee904361b93ad67955fcff151bb7a7772bbfbd2c448c814be1f5` |
| `amdgpu/navi10_sdma.bin` | 33792 | `3fc635b9a17819eaf13913882676f0cd07a6c1a31c87b3ef0dcedebf594c8dd0` |
| `amdgpu/navi10_sdma1.bin` | 33792 | `317b398d0563c8c89b10c31712296843143385f5a1ddcee2031536fe67576c3b` |
| `amdgpu/navi10_smc.bin` | 267970 | `49ed339d0ff71fe64347380ee12b00c3c69a546f454bcd4323c411c72fe6fb2e` |
| `amdgpu/navi10_sos.bin` | 184512 | `3112ea8519545ac89c1623e2e08e841a6a9bc31c2aab2f9b787979e51277e395` |
| `amdgpu/navi10_ta.bin` | 37632 | `5e4614788bb866d40a4f8b8c4a3f1e3474f9f45203df9c7177a356d790b6cca5` |
| `amdgpu/navi10_vcn.bin` | 404544 | `ac5f2182b0ddee7a2886bf1239a47b4027becd0c37a1b0b6cac1f335393302c9`（VCN は範囲の外） |

   navi10 には `_wks`・`dmcu`・`dmcub` の file が無い（DCN 2.0 の Navi10 は display の firmware を使わない見込み、要確かめ）。
5. **調べの作業の文書**（`temp/notes/`、英語、file:line 付き、commit しない）。6 つの調べを helper に並行で頼んだ:

| note | 中身 | 状態（2026-10-15 の区切り） |
| --- | --- | --- |
| `a-chips-pci.md` | RDNA1〜3 の PCI の device id の表、BAR（MMIO は BAR5、VRAM は BAR0、doorbell は BAR2）、Navi10 の IP の base、IP discovery、reset と shutdown、VBIOS の post の確かめ、Mesa の family と gfx_level | **完了**（627 行。要点: RX 5600 XT は 0x731F rev 0xCA、Navi10 は v6.19 で IP discovery だけ（VRAM の上端 −64 KiB、10 KiB）、BAR5 MMIO・BAR2 doorbell・BAR0 VRAM、GRBM_STATUS の byte 0x8010、RDNA3 は class で照合し GC の版で判定、post の確かめは BIOS_SCRATCH_7 の bit 0x200、C2PMSG_81≠0 なら前の driver が PSP を load 済みで reset が要る） |
| `b-psp-fw-smu.md` | firmware の一覧と container の header、PSP 11.0 の bootloader（KDB・sysdrv・SOS）、PSP の ring、TMR、LOAD_IP_FW の順、SMU 11 の mailbox と最小の手順 | 調べの途中で停止を頼んだ（無いか PARTIAL） |
| `c-gmc-vm-ih.md` | FB・VRAM・HDP の flush、GART（VMID0）と 2 つの hub の write の順、4 段の VM と PTE・PDE の bit、TLB の invalidate（Navi1x の SDMA の errata）、fault、IH の ring と IV の 32 byte、割り込みの src id の表、doorbell の index | **完了**（約 785 行。要点: CP EOP は client 0x14 src 181、SDMA trap 0x08/0x09 src 224、DCN の pageflip 0x4F〜0x54・VSTARTUP 0x3C〜0x41、doorbell GFX0 0x8B・SDMA0 0x100・IH 0x178（qword 単位）） |
| `d-cp-sdma.md` | RLC の起動、golden の register、GFX の ring と clear state の preamble、MEC・KIQ・HQD の直接の設定、RELEASE_MEM の fence、PM4 の header と opcode の表、SDMA 5.0 の起動と packet | **PARTIAL**（約 700 行、全 8 問に答えた。残り: Navi10 の PSP の版、PSP の load の後の gfx の engine の状態、opcode の行番号 3 つ、GRBM の remap の数。要点: Navi10 は PSP の autoload で RLC は既に動く、golden 40 項目、Linux は既定で KIQ で gfx の ring を map（直の CP_RB0 は代わりの道）、compute の queue は register だけで立てられる見込み、SET_*_REG の base config 0x2000・SH 0x2C00・context 0xA000・uconfig 0xC000） |
| `e-radv-gfx10.md` | RADV の gfx10 の preamble、VS・PS・compute の register、CB・viewport・blend、image・sampler・buffer の記述子、user SGPR の配置、clear・copy の道、cache の flush | 途中で停止を頼んだ |
| `f-dcn20.md` | DCN 2.0 の GOP の readout（OTG・HUBP・MPC・DIG）、surface の address と形式、flip と完了、vblank・frame counter、cursor、触らない物、blank と release、shutdown、mode set の範囲 | 途中で停止を頼んだ |

   停止を頼んだ 4 つは、PARTIAL の印を付けて書けた所まで残すよう伝えた（届いていない可能性あり。再開の時に note の先頭を見て確かめる）。

## 2026-10-15 夕 P3（q1202、再開）

1. `temp/notes/` の 6 file は全部あった（b は完了、d・e・f は PARTIAL の印つき）。欠けのうち設計に要る物を調べ直した: DCN の割り込みの経路（amdgpu_dm が sparse の外だったので `plan/ws039/temp/linux` に `display/amdgpu_dm/` を sparse で足した。client DCE 0x04、VSTARTUP 0x3C〜・flip 0x4F〜・HPD 9 を `amdgpu_dm.c` の登録で確認）、cursor の属性（`amdgpu_dm_plane.c`: premultiplied、pitch = buffer の幅、address の整列の検査は無い）、dGPU の `gpu_vm_support`（VRAM だけの scanout）。d の残り（ME/PFP/CE の halt の状態）は driver が自分で unhalt するので設計に影響しない。e の残り（NGG の sizing）は legacy VS を使うので不要。f の残り（DP の link training・watermark・DPP の CM）は mode set の範囲で、範囲の外にした。
2. p007 の 3 節（program の形）を読み、design の 9.2 で driver の側から合わせた（差分なし）。
3. [design.md](../design.md): 段 A0〜A9（3 節）、firmware（4 節）、GMC・IH・CP・SDMA・DCN（5〜8 節）、executor と compiler の口（9 節）、interface の対応表（10 節）、回復（11 節）、試験の道（12 節）、出典の規則の守り方（13 節）、危険（14 節）、判断の項目 11 件（15 節）、Phase の表（16 節、p002〜p010、計 40〜65 LW）。
4. [amdrdna-license-audit.md](../amdrdna-license-audit.md): Linux 106 file・Mesa 44 file・firmware 13 file の path・SHA-256・license・取るデータの表（temp の tsv と 2026-10-15 の再計算）。
5. design-reviewer の review（[design-review.md](../design-review.md)、blocker 3・major 10・minor 20）を全部反映した（扱いは同 file の末尾の「対応」の表）。大きな変更: 段の順を GMC・IH（A2）→ PSP（A3）に（PSP の buffer は GART の system memory、HDP の規則 5.1）、SOS が生きていれば A1 で止まる（cold power cycle）、登録は 1 回で renderer だけの登録を新しい p011（A5s）に、GOP の framebuffer を console の surface のまま残して A6 は flip だけ、compute の queue は作らない、isolate・reset は NULL、lock の表、VRAM の配置、SMU の mailbox と温度の監視、RLC の header の offset の直し、監査の表に 21 行を追加。Phase は p002〜p011（p011 は新規）、計 43〜70 LW。
6. 未: Q1 の clearance の判定。ユーザーの判断の項目（design の 15 節、12 件、既定つき）。

## 再開の点（resume point、2026-10-15 夕の更新）

p001 の残りは Q1 の clearance と判断の項目のユーザーの回答だけ。次の Phase は p002（design の 16 節）。以前の再開の点（参考）:

1. `temp/notes/` の 6 つの note の有無と先頭の PARTIAL の印を確かめ、欠けた topic（b・d・e・f の残り）だけを調べ直す。temp は消えていない前提（worktree `/home/awe/zedBSD-worktrees/p3`、無ければ上の表の版で sparse に取り直す。取得は必ず `plan/ws039/temp` の中の明示の path で行う（2026-10-15 の事故: cwd の取り違えで main の checkout に clone を作り、mv で戻した））。
2. P5 の [p007](../phase007/phase.md)（compiler が出す program の形: user SGPR・記述子・PS の入力・export・driver に渡す header）が main に入っていれば読み、design の口を合わせる。
3. `plan/ws039/design.md` を書く。予定の骨子（まだ文書に無い、調べからの見立て）:
   - **AMD は GSP のような BAR の切り替えが無い**: BAR0 は VRAM を直に見る窓のままなので、GOP の framebuffer の範囲を VRAM の allocator から除けば、text console の切り離し（nvrtx の pcat の口）は要らない見込み。BAR0 は resizable BAR が無ければ 256 MiB。
   - 段の案: A0 発見と ID（PCI・BAR5 の MMIO・chip の判定・VBIOS の post の確かめ）→ A1 GOP の readout（OTG・HUBP の surface の address が BAR0 の中か）→ A2 PSP（SOS が生きているか、bootloader・ring・TMR・LOAD_IP_FW）→ A3 SMU（最小の message、GFXOFF を止める）→ A4 GMC・IH（GART・VM・ring・割り込み）→ A5 CP（RLC・GFX の ring・MEC/KIQ か HQD の直接）と SDMA、最初の copy と fence → A6 display の引き継ぎ（HUBP の surface を自前に、flip・vblank）→ A7 cursor → A8 GPU device の登録（display の ops）→ A9 最初の draw と executor（renderer の node）。各段の実機の合格の印は kernel.log の `amdrdna: Ax ok …` の行。
   - executor: nvrtx の vk-* のうち hardware に依らない部分（object・session・stream・dispatch・device・memory・resource・queue・reply・target・runtime、約 5〜6 千行）を写して amdrdna の名前にするか、共有の層に寄せるか（WS037 の Future Work「executor の共有の層」）を判断の項目にする（既定の案: 写す。WS241 の P1 の作業中の file に触れないため）。投入・fence の待ちは最初から lock の外・spin＋sleep（WS241 の教訓）。
4. `plan/ws039/amdrdna-license-audit.md` を `temp/*-lic.tsv` から、design が参照した file の行で作る（データだけを使う規則なので「扱い」の欄はデータの引用か参照のみ）。
5. Q1 に SHA を送り design-reviewer を回してもらう → 指摘を直す → p002。

## 検証

code は無い（build の行の確認は上の 1）。design-reviewer の review は未実施。実機・QEMU は無い。
