<!-- awesome-plan project=zedbsd record=ws039-p003 -->

# ws039-p003: 段 A2（BAR0 の窓・GMC・IH の用意）

Status: cleared（2026-10-16 Q1: 実機 10.0.10.3 で 2 回とも A2 ok・underflow=0、Linux の値と照合が一致、host 試験 PASS、build warning 0。main の merge 6e44a158e。IH の ring の enable・MSI の到着・VM fault の受けは A3 で）
Disposition: normal
Parent: [WS039](../ws.md)
実行者: P3（worktree `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3-ws039`、base main `355c8642c`）

## 範囲（[design.md](../design.md) §16 の p003 の行）

BAR0 の 256 MiB の窓の map（2 つの pool）、GMC（FB、allocator（下 32 MiB の予約を含む）、GART、両 hub の VMID 0、context 1〜15、HDP）、IH（ring・MSI の用意、thread、dispatch。enable は A3）、VM fault の受け、host 試験 (d) の前半。**実機: A2 の ok、underflow 0**。

追加（2026-10-16 ユーザー、クリック「video=640x480 を外す」）: 試験の image で GOP を monitor の 1920x1080 のままにする（WS039 の config.mk の中だけで）。

承認: q1212（2026-10-15 夜 ユーザー「AMDに集中しましょう。N=1にします。」と design §15 の回答）。

## 実装（2026-10-16 P3、実機前）

driver（`src/drivers/gpu/amdrdna/`）:
- `gmc.c`（新）: 段 A2。窓の map（BAR0 の先頭 256 MiB のうち GOP の予約の後ろから。console は GOP の framebuffer を自分の map で書き続けるので、別の cache 属性で同じ物理を map しない）、doorbell の先頭 1 page、VRAM の 2 つの pool（CPU の窓・GPU だけ。A1/A1v の layout の予約（top 1 MiB・GOP・VBIOS・下 32 MiB の training）を除く）、GART の置き場（FB の前後の best fit、4 GiB 整列。実機の FB 0x8000000000 では Linux と同じ 0）、dummy page（system memory）、scratch page と GART の表（1 MiB、CPU の pool）、表の全 entry を未使用で書いて HDP flush・invalidate の後に MM_INDEX で 4 個読み戻し、両 hub の設定（Linux の gart_enable の順: 表と範囲・AGP を閉じて FB の aperture・default と fault の page・L1・L2・context 0・identity を閉じる・context 1〜15（4 段、no-retry、default page）・18 本の invalidate engine の範囲）、HDP の MISC_CNTL と flush、fault の既定（default page へ）、doorbell の aperture、両 hub の VMID 0 の invalidate（engine 17、MMHUB は semaphore、GFXHUB は dummy read）、主な register の読み戻し、IH の用意、underflow の前後（HUBP の 3 bit と OPTC の bit、残っていれば A2 の前に clear）。失敗と shutdown では両 hub の context の word を A2 の前の値に戻す。VM fault の受け（`amdrdna_vm_fault`: status を読んで re-arm、最初の 16 個を log）、context の fault 割り込みの on/off（A3 が使う）、`amdrdna_gart_map`、`amdrdna_tlb_flush`、HDP の flush/drop。
- `ih.c`（新）: IH の ring（256 KiB）と wptr の写しの page（system memory、GART に map）、MSI-X か MSI 1 本と handler（thread を起こすだけ）、register（ring off、NBIO の dummy page と INTERRUPT_CNTL、base、RB_CNTL、wptr の写しの address、pointer 0、doorbell 0x2f0 と NBIO の range）、bus master、IH の thread（wptr の写しまで読み、dispatch、doorbell に rptr、overflow の扱い）、停止。
- host でも動く純粋な file: `span-pool.c`（範囲の pool: give・hold・take（整列）・merge）、`hub-registers.c`（両 hub の register 41 種の表、context/engine の instance）、`vm-format.c`（GART の置き場、GART・PTE・PDE の entry、4 段の index の分解、invalidate の word、hub の制御 word、fault の decode）、`interrupt-format.c`（IV の decode、RB_CNTL・doorbell・range・INTERRUPT_CNTL の word、wptr の offset と overflow）。
- `amdrdna-regs.h`: NBIO の seg 2（doorbell aperture・INTERRUPT_CNTL/CNTL2・HDP flush・IH の doorbell range）、HDP、OSSSYS（IH）の register、IH の ring の大きさと doorbell 0x2f0、hub の定数（engine 17、GART 512 MiB、範囲・aperture の値と shift）。全部 block ごとに Values referenced。
- `amdrdna.c`: 最後の段を A2 に、A2 を thread で走らせる、shutdown で bring-up の thread が止まった後に `amdrdna_a2_stop`。`mmio.c`: `amdrdna_block_read/write`。`control.c` と UAPI（`include/uapi/amdrdna.h`、reserved から 36 byte）: gart_base・gart_mib・pool の空き・割り込みの種類と数・VM fault の数・underflow。`userland/tests/amdrdnactl`: `memory …` の行。
- build の行（`platform/amd64/vmunix.mk`）に 6 file。

設計からの変更・決めたこと:
- CPU の窓は BAR0 の先頭 256 MiB 全部ではなく、GOP の予約の後ろから map する（GOP の framebuffer は bootloader・HAL の console が自分の map（write-combining の見込み）で書くので、uncached の別の map で同じ物理を重ねない）。CPU の pool はその map の範囲だけ。GOP が窓の途中にある board では GOP より下が使えない（Navi10 の実機は GOP が VRAM の先頭）。A3t（training）で下 32 MiB を退避する時の GOP の部分の読み書きは p004 で決める。
- doorbell の BAR は先頭 1 page（4 KiB）だけ map（driver が鳴らす doorbell は全部その中: IH 0xbc0、KIQ・MEC・GFX・SDMA0/1 も 0x850 まで）。
- GART の reach の先頭 1 MiB は pool から出さない（GPU の address 0 が buffer を指さない）。
- context の fault 割り込みの bit（`amdrdna_fault_interrupts`）は A2 では立てず、ring の enable と一緒に A3 で立てる。
- 割り込みは MSI-X か MSI だけ（INTx の fallback は無し。無ければ A2 fail）。実機は MSI。
- A2 の失敗・shutdown では両 hub の context 0〜15 の word を A2 の前の値に戻す（L1・L2・aperture は戻さない。display は aperture を通るので触らない）。
- underflow: A2 の前に立っていれば clear して 100 ms 後に読み直して log、A2 の後 100 ms 待って読む（ok の行の値）。

試験の image（`plan/ws039/tests/config.mk`）: `ZEDBSD_GRAPHICAL_BOOT := y`（image の build で zedbsd.cfg から `video=` を外す唯一の switch。loader は logo=logo.ppm があると 1920x1080 を望み、GOP は monitor の mode のまま）、`ZEDBSD_BOOT_KERNEL_MESSAGES := y`（kmsg は quiet にならず、console が logo の上に書く）、`ZEDBSD_GRAPHICAL_LOGIN := n`。共通の build の規則は変えていない。

host 試験（実機の証拠ではない）:
- `plan/ws039/tests/amdrdna-memory-test.c`（新）: pool（give・hold（下・中・上）・整列の take・merge・拒否・span の上限）、GART の置き場（実機の FB で 0、FB が 0 なら top、満杯）、GART の entry、PTE・PDE、4 段の分解、invalidate の word、hub の word（実機の dump: MMVM_L2_CNTL の firmware 0x00080602 → Linux 0x00080e01、MMVM_CONTEXT0_CNTL 0x007ffe80 → 0x007ffe01、aperture 0x200000/0x205fbf と一致）、fault の decode、IH の word（RB_CNTL の firmware 0x40610000 → on にすると Linux の 0x403301a1）、IV の decode、hub の register の index。
- `plan/ws039/tests/amdrdna-hub-compare.py`（新）: 両 hub の全 register・全 instance と NBIO・HDP・OSSSYS の 17 個、計 479 個の index が Linux v6.19 の header と一致。
- `plan/ws039/tests/amdrdna-host-test.sh`: 上の 2 つを足した。PASS（PASS の行 132、memory-test・tables-test・dcn-compare 276・hub-compare 479・audit-symbols）。監査の allow list に一般語 38 個（interrupt・context・ring・pointer・mutex_lock など。C の keyword・zedBSD の kernel の口・英語の普通の語）。
- build: `make -j16 ZEDBSD_CONFIG=plan/ws039/tests/config.mk BUILD=build/p3/usb AMDRDNA_RUN=A1v build/p3/usb/vmunix build/p3/usb/bin/amdrdnactl` rc 0、warning 0、kernel include check PASS、amd64 vmunix check PASS。`plan/tools/style-check.py` の違反 0（変えた 9 file）。`git diff --check` OK。

## 実機（2026-10-16、q1212-i01、10.0.10.3 の RX 5600 XT、USB の zedBSD は DHCP で 10.0.30.5）

手順: zedBSD → `hw-run.sh debian` → Debian で USB の sda2（zedBSD-boot）を mount し、`vmunix` と `zedbsd.cfg` を差し替え（前の物は Debian の `~/ws039/vmunix.hw3`（SHA-256 0835d308…）・`~/ws039/zedbsd.cfg.hw3`（00e96298…、video=640x480 入り））→ `hw-run.sh boot 0001` → `AMDRDNA_ZED=root@10.0.30.5 hw-run.sh collect` → 新しい `amdrdnactl` を `/tmp` に送って `advance A2`、`status`、`dmesg`。zedbsd.cfg は config.mk の規則と同じ中身を手で作った（`video=` の行を除き `logo=logo.ppm` と `amdrdna.auto=A1v` を足す。SHA-256 a01b17c1…）。後で make の規則が作った `build/p3/usb/zedbsd-native-uefi-graphical-y-login-n-kmsg-y-amdrdna.auto=A1v.cfg` と byte ごとに一致を確かめた。電源の入れ直しは無し。

**1 回目**（10:07〜10:08、vmunix d00dd942…、証拠 `build/tmp/amdrdna-hw-a2-1/`、worktree の中、commit しない）:
- A1（1920x1080 の GOP）: `A1 scaler dpp=0 mode=1 auto=0 out=0,0 1920x1080 ratio=0x1000000/0x1000000 viewport=0,0 1920x1080 active=1920x1080`、`A1 ok otg=0 opp=0 mpcc=0 hubp=0 dig=4 mode=DVI 1920x1080 on 1920x1080 whole fb=+0x0 pitch=0x780 stride=1920 fmt=8 lit=1`、**`A1 clk … underflow=0`**（p002 の拡大の GOP では 3 回とも 1 だった）、`vram kept gop 0x0-0x7eb000`。scratch7 = 0x0070020a（Debian の dump と同じ値に戻った）。
- `amdrdnactl advance A2`（10:08:30）:
  - `A2 window +0x7eb000-0x10000000 at 0x40007eb000, doorbells 4KiB at 0x4010000000`
  - `A2 pools cpu=224MiB in 1 runs, gpu=5871MiB in 1 runs`
  - `A2 table vram=+0x2001000 entries=131072 gart=0x0-0x1fffffff scratch=+0x2000000 dummy=0x75408000`
  - `A2 gfx hub before: l1=0x00000501 l2=0x00080602 ctx0=0x007ffe80 ctx1=0x007ffe80 aperture=0x200000-0x205fff bypass=0x0/0x0 fault=0x3ffffffc`（mm hub も同じ値）
  - `A2 gfx hub: l1=0x00001d59 l2=0x00080e01 ctx0=0x007ffe01 ctx1=0x007ffe07 base=0x0000000002001001`（mm hub も同じ値）
  - `A2 ih ring=0x0 bus=0x75414000 pointer=0x40000 256KiB irq=msi vector=0 doorbell=0x2f0 control=0x403101a0`
  - **`A2 ok bar0=256MiB pool=2 cpu=222MiB gpu=5871MiB fb=0x8000000000-0x817effffff gart=0x0+512MiB table=+0x2001000 ih=ready underflow=0`**
  - `amdrdnactl status`: `stage=A2 failed=none`、`memory gart=0x0+512MiB cpu_free=222MiB gpu_free=5871MiB irq=1 signals=0 faults=0 underflow=0`
- zedBSD の reboot（shutdown の hook が A2 を戻す道）→ Debian が約 30 秒で戻った（固まらない）。

直し（1 回目の後）: GART の reach の先頭 1 MiB を pool から出さない（GPU の address 0 が buffer を指さないように。1 回目は IH の ring が GART 0 にあった）。shutdown の戻しに印の行を足した。

**2 回目**（10:11、vmunix 2839ff6d…、証拠 `build/tmp/amdrdna-hw-a2-2/`）: A1 は 1 回目と同じ（underflow=0）。A2 の行は 1 回目と同じで、`A2 ih ring=0x100000 bus=0x75431000 pointer=0x140000 …`、**`A2 ok … underflow=0`**、status `stage=A2 failed=none`。hub の before の値が firmware の値（l1=0x501、ctx0=0x7ffe80）に戻っていた → この機械では Debian を経た warm reboot（か VBIOS の post）で GMC の register が初期化される（14 節の 2 の一部の観測。SOS の有無は A3 で）。

照合（Debian の dump、`temp/dump/loaded` = Linux の amdgpu の後）: hub の L2 の word 0x00080e01 が Linux の MMVM_L2_CNTL と一致、system aperture 0x200000-0x205fbf の計算（host 試験）と Linux の値が一致、GART の範囲 0x0-0x1fffffff は Linux の dmesg の「GART: 512M 0x0000000000000000 - 0x000000001FFFFFFF」と一致。L1 の word は Linux の dump に無い（読んだ値: firmware 0x501 → 0x1d59）。context 0 の word は Linux の 0x007ffe01 と一致（Linux は割り込みの bit も立てるが、firmware の値に既に立っている）。

## 状態と次

- [x] 実装、host 試験、build
- [x] 実機（10.0.10.3）: 1920x1080 の GOP で A1 の underflow 0、`advance A2` で **A2 ok、underflow 0**（2 回）、shutdown の戻しの後 Debian に戻る（2 回とも約 30 秒。2 回目の reboot の後に USB の `log/boot-000005.txt` を読んだが shutdown の印の行は無い: driver の shutdown は log の file system の後なので disk に残らない。戻しの道の証拠は reboot が完了したことだけ）
- [ ] Q1: merge と clearance の判定
- 残り（この Phase では確かめられない・しない）: IH の ring は A3 まで off なので、MSI の到着・IH の thread の dispatch・VM fault の受けは実機で通っていない（host 試験の decode まで）。A1 の失敗の時の control の node の道は今回も通っていない（A1 は ok）。context の fault 割り込みの on は A3 で呼ぶ。
