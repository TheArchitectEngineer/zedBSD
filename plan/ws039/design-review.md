<!-- awesome-plan project=zedbsd record=ws039-design-review -->
# amdrdna の設計の review（ws039-p001、design-reviewer）

- 日付: 2026-10-15 16:50 JST（host の `date`）
- 対象の commit: `73a01c38bacfdf4f38b8fce790c6f768157ceea3`（`git -C /home/awe/zedBSD-worktrees/p3 log -1 --format=%H`。対象の file は作業ツリーで変更なし）
- 対象: [design.md](design.md)（全 436 行）、[amdrdna-license-audit.md](amdrdna-license-audit.md)
- 照合に使った物: `plan/ws039/temp/notes/{a〜f}.md`、正本 `plan/ws039/temp/linux`（Linux v6.19 の `drivers/gpu/drm/amd`）・`temp/mesa`（Mesa 25.3.6）、`include/drivers/gpu/gpu.h`・`gpu-display.h`・`gpu-scanout.h`、`src/drivers/gpu/gpu.c`（登録の検査）、`src/drivers/gpu/nvrtx/`（同じ形の実装）、[nvrtx-design.md](../ws037/nvrtx-design.md)、[p007](phase007/phase.md)、[ws.md](ws.md)、`plan/guardrail.md`「MIT・GPL の出典のデータの引用」。
- やらなかった事: source・他の plan の変更、build、試験、実機・QEMU。**すべて source を読んだだけで、hardware での観測は無い**。

以下、design の節の番号は design.md の節。「確認」は正本・note・zedBSD の source の該当行を自分で読んで確かめた事、「未確認」はそうでない事。

---

## blocker（p001 の clearance の前に直すべき物）

### B1. HDP（BAR0 越しの VRAM の一貫性）の flush・invalidate が PSP・ring・IB・読み戻しの道から抜けている。HDP の初期化も PSP より後

- **該当**: 2 節の 5（PSP の buffer を全部 VRAM）、4.3 の 3・5（fw_pri への image の写し、fence の待ち）、5 節（HDP の初期化は A4）、7.2・7.4・7.5（ring・IB）、10.1 の NOTIFICATION・MAPPING、9.2（shader の code は VRAM）。
- **問題**:
  1. 4.3 の 5 は frame を書いた後の HDP の flush だけを書き、**fence の語を待つ間の HDP の read cache の invalidate が無い**。Linux は PSP の fence を待つ前と待つ loop の各回で invalidate する。fence・応答（+864）を BAR0 越しに読むと古い値を読み続け、偽の timeout（A2 の fail）になりうる。
  2. 4.3 の 3（KDB・SYS_DRV・SOS を fw_pri に写して bootloader に渡す）と 7（LOAD_TOC・LOAD_IP_FW・ASD の image を fw_pri に写す）に HDP の flush が無い。Linux の bare metal では fw_pri は GTT（system memory）なので要らないが、design は VRAM に置くので、PSP が書き込み途中の image を読みうる（署名の検査の失敗）。
  3. GFX・SDMA の ring、IB、MQD、CSB、page table、shader の code を VRAM に置く（7.2「ring は VRAM」、9.2）のに、doorbell の前の HDP の flush が書かれていない。Linux は IB の投入ごとに ring の上で HDP を flush し、終わりに invalidate する。
  4. GPU が書いた VRAM を CPU が BAR0 で読む（`resource_read`、user の `resource_map`）の invalidate の手段（register と順）が書かれていない（10.1 は「挟む」とだけ）。
  5. HDP の初期化（HDP_MISC_CNTL の FLUSH_INVALIDATE_CACHE と最初の flush）は A4 にあるが、A2 はその前に HDP を使う。Linux は GMC（GART と HDP の初期化）を PSP より前に hw_init する。A2 を A4 より前に置く順は Linux に無い道で、14 節の危険の一覧にも無い。
- **根拠（確認）**: `temp/linux/.../amdgpu/amdgpu_psp.c:717`・`:730`（fence の待ちの前と loop の中の `amdgpu_device_invalidate_hdp`）、`:508-512`（fw_pri は bare metal で GTT）、`:517-528`（fence・command buffer は VRAM|GTT）。`amdgpu_ib.c:246`（IB の前の HDP の flush）・`:283`（後の invalidate）。`hdp_v5_0.c:30-40`（invalidate = HDP_READ_CACHE_INVALIDATE に 1 を書いて読み戻す）。`gmc_v10_0.c:959-966`（両 hub の後に HDP の初期化と flush）。`amdgpu_device.c:3185-3186`（「gmc hw init early」、PSP より前）、`amdgpu_discovery.c:3166-3172`（GMC の block が PSP より先）。note c の 1.3 節（`c-gmc-vm-ih.md:123-167`）は IB ごとの flush/invalidate を書いているが design に移っていない。
- **直し方の案**: 5 節に「HDP の規則」の小節を作り、(a) CPU が VRAM に書いた後、GPU・PSP に読ませる前は必ず flush（NBIO の flush の register、design の 146 行の形）、(b) CPU が GPU・PSP の書いた VRAM を読む前は必ず invalidate（HDP の read cache の invalidate の register の offset と出典）、(c) ring の上では IB の前に flush の packet（WAIT_REG_MEM、engine の bit）、IB の後に invalidate の register の書き込み、を書く。4.3 の 3・5・7 に flush と invalidate を明記する。A2 の前に HDP の初期化（HDP_MISC_CNTL）を移すか、「A2 を A4 より前に行う」ことを 14 節の危険と A2 の begin の行の値（HDP_MISC_CNTL の読み）に入れ、代わりに fw_pri・fence を A4 の後の GART（system memory、Linux の bare metal と同じ）に置く案と比べて判断の項目にする。

### B2. 判断の項目 6（SOS が生きていれば mode1 reset）は GOP の画面と post を失う。warm reboot の度にこの道に入るので、試験の道（12 節）が成り立たない

- **該当**: 3 節 A0、3.1 の「A2（SOS の後）」の行、4.3 の 3 の最後、11 節の reset と停止の (8)、12 節の実機の手順（`amdrdna.stop=` で段ごとに進める）、14 節の 2、15 節の 6。
- **問題**:
  1. 11 節の停止は SOS を生かしたまま終わる（Linux と同じ）。したがって amdrdna が A2 まで進んだ後の warm reboot では、platform が GPU を reset しない限り、次の起動の A0 で C2PMSG_81 ≠ 0 になる。design はこの時 mode1 reset を試す（15 節の 6）。
  2. mode1 reset は DCN を含む ASIC 全体を reset する見込みで（nvrtx の design も reset は display を落とすとしている）、GOP の timing・link が消える。amdrdna は mode set をしない（2 節の 0、15 節の 7）ので**画面が戻らない**。
  3. Linux は reset の後に post が要るかを BIOS_SCRATCH_7 で判定し、要れば AtomBIOS の asic_init を走らせる。amdrdna は AtomBIOS を持たない（1.2 の VBIOS の行）ので、reset で post の印が消えると GPU を使えない（未確認: mode1 reset の後に BIOS_SCRATCH_7 が消えるか）。
  4. 12 節は「段ごとに `amdrdna.stop=` で進める」が、A2 以降の各試行の後は電源の切断（cold boot）が要る。design・判断の項目 9（host.md）・LW の見積もりのどこにもこの前提が無い。
- **根拠（確認）**: `temp/linux/.../amdgpu/nv.c:542-557`（C2PMSG_81 ≠ 0 なら reset が要る）、`amdgpu_device.c:4804-4841`（reset の後に `amdgpu_device_need_post` → `amdgpu_device_asic_init`）、`:1775-1830`（post の判定は BIOS_SCRATCH_7）、`psp_v11_0.c:403-425`（mode1 は C2PMSG_64 = 0x70000 と 500 ms）、note a の 5.1・5.2 節（`a-chips-pci.md:425-460`）、`plan/ws037/nvrtx-design.md:82`（reset は display も落とす）。
- **直し方の案**: 判断の項目 6 の既定を「C2PMSG_81 ≠ 0 なら A1 まで（読むだけ）で止め、`A0 fail: sos alive, cold power cycle needed` を出して GOP の画面を保つ」に変え、mode1 reset は後の明示の実験（post の要否を A0 の BIOS_SCRATCH_7 で観測してから）に回す。12 節に「A2 以降の試行の間は電源の切断が要る（platform が warm reboot で GPU を reset するかは最初の試行で C2PMSG_81 を見て確かめる）」と書き、判断の項目 9 の host.md に遠隔の電源の切り方（または毎回のユーザーの操作）を必須の項目として入れ、p003〜p006 の LW に反映する。

### B3. GPU device の登録の時点と renderer の結び付けが `drv_gpu_ops` の契約と合わず、Phase の依存（p008 → p006）が抜けている

- **該当**: 3 節 A8・A9、10.1（「display を引き継いだ node は …」）、12 節（`amdrdna.disp.own=1` の時だけ A6〜A8、`amdrdna.vk=1` で A9・renderer の node）、16 節（p005 に RESOURCE〜SHARE・recovery、p008 の依存は p005・p007 だけ）。
- **問題**:
  1. `drv_gpu_register` は ops を借りて登録の時に capability と callback の組を検査し、ops は登録の間ずっと有効で不変である。A8 で display の ops だけで登録した後に、A9 で renderer（CAPSET・COMMAND・NOTIFICATION・JOB・JOB_CAPACITY・recovery）を「足す」ことはできない。nvrtx は renderer の ops を**登録の前に** node へ結び付けている。
  2. 12 節の既定（`disp.own` は p006 の合格まで off）では A8 が走らず、GPU device が登録されない。すると A9（Venus の stream、vkquad）は `/dev/gpu` が無く走らない。12 節の「既定は無し = A8 まで」とも食い違う。
  3. 16 節は p005 で `drv_gpu_ops` の RESOURCE〜SHARE・recovery を作るが、登録は p006（A8）なので、p005 の ops は p006 まで実機で一度も呼ばれない（host 試験 (e) は packet だけ）。p008 は登録に依存するのに、依存の列に p006 が無い。
- **根拠（確認）**: `include/drivers/gpu/gpu.h:103-121`（不変の ops）・`:196-208`（借りた ops は unregister まで有効）、`src/drivers/gpu/gpu.c:860-1140`（登録の時の検査）、`src/drivers/gpu/nvrtx/runtime.c:104-124`（`nvrtx_runtime_bind`: 登録の前に renderer の ops と capability を足す）、`src/drivers/gpu/nvrtx/node.c:50-51`・`:154-156`。
- **直し方の案**: 登録を 1 回にし、その前に「display を持つか（`disp.own`）」と「renderer を持つか（`vk`）」を両方決めて ops を組み立てる形に 3 節と 10.1 を直す（nvrtx の bind と同じ順）。renderer だけの登録（display の ops 無し）を許すかを判断の項目に足す。16 節の p008 の依存に p006（登録）を足すか、登録を p005 の終わりに移して p005 の ops を実機で確かめられるようにする。12 節の「既定 … A8 まで」を実際の既定に合わせる。

---

## major

### M1. display の ops の対応表（10.1）が `drv_gpu_display_ops`・`scanout_ops` の契約と食い違う

- **該当**: 10.1 の DISPLAY・DISPLAY_EVENTS・scanout の行、3 節 A7、8.4。
- **問題**: (1) `display.events` は vblank ではなく、display の構成（topology）の通し番号を IRQ から安全に読む口。vblank は `wait`（と `refresh`）の側。(2) A7 で hardware の cursor を作るのに、`cursor_set`・`cursor_move`・`cursor_hide`（3 つ揃いが必須、query で `GPU_DISPLAY_HAS_CURSOR`）が表に無い。cursor が interface に繋がらない。(3) 10.1 は「最初の集合は nvrtx と同じ」と言うが、nvrtx は `GPU_CAP_DISPLAY_CONTROL`（`power`・`refresh`）を持ち、表には無い。(4) 「pitch = GOP と同じ（固定）・offset 256 byte」は `scanout.constraints` で返す物で、`query_device` だけでは伝わらない（nvrtx は constraints を持つ）。
- **根拠（確認）**: `include/drivers/gpu/gpu-display.h:31-39`（events = IRQ-safe な sequence）・`:41-48`（power・refresh）・`:50-60`（cursor の 3 つ）、`src/drivers/gpu/gpu.c` の display の検査（cursor の 3 つ揃い、DISPLAY_CONTROL の 2 つ揃い）、`src/drivers/gpu/nvrtx/present.c:96-122`（events は「topology sequence」、cursor・power・refresh を持つ）、`nvrtx/share.c:77-81`（query_device と constraints）、`include/drivers/gpu/gpu-scanout.h:50-52`。
- **直し方の案**: 10.1 を nvrtx の `present.c`・`share.c` の表に合わせて書き直す: events = HPD による構成の番号、wait/refresh = VSTARTUP の割り込みの数、cursor の 3 つ（A7）、DISPLAY_CONTROL を持つか（power は blank、refresh は vblank の待ち）を決める、scanout は query_device と constraints（import_image は NULL）。

### M2. recovery の `isolate` と CP の priv/bad opcode の扱いが契約・hardware と合わない。`reset` は NULL と書くべき

- **該当**: 10.1 の recovery の行、11 節の `isolate`・`reset`、10.2。
- **問題**: `isolate` は「止まったと確かめられなかった context を隔離し、**他の context は動き続ける**」ことを約束する口。amdrdna は session をまたぐ GFX の ring が 1 本で、KIQ も preempt も無い（7.3・11 節）ので、stop_poll が失敗する時（＝ fence が終わらない時）は ring が詰まっており、他の context は動き続けられない。11 節も「hang は device の fault」と書いており、isolate が 0 を返せる場面が無い。また CP の priv reg・priv inst・bad opcode を「session の fault」にしているが、Linux はこれを ring の scheduler の fault（ring の reset）として扱う。静的な VMID の session を隔離すると VMID は reset まで返らず、reset は無い（電源の切断まで）ので VMID が減り続ける。
- **根拠（確認）**: `include/drivers/gpu/gpu.h:73-80`、`src/drivers/gpu/nvrtx/runtime.c:96-102`（nvrtx は isolate・reset とも NULL）、`plan/ws037/nvrtx-design.md:194`（isolate は preempt を確かめられた時だけ）、`temp/linux/.../amdgpu/gfx_v10_0.c:9351-9408`（priv/bad opcode → `drm_sched_fault`）、`src/drivers/gpu/gpu.c:7472-7473`（reset は NULL を許す）。
- **直し方の案**: 最初の版は `isolate = NULL`・`reset = NULL`（nvrtx と同じ）と明記し、CP の priv/bad opcode は device の fault にする。VM fault（no-retry で既定の page へ逃がす）だけを session の fault にする。

### M3. 共有の index/data の register と選択の register の排他、割り込みの文脈が設計されていない

- **該当**: 2 節の 6、1.2（PCIE_INDEX2/DATA2）、3 節 A0（MM_INDEX/MM_INDEX_HI/MM_DATA）、15 節の 10（窓の外の VRAM の `resource_read/write` は MM_INDEX の間接）、7.1 の 3・4 と 7.3（GRBM_GFX_INDEX・GRBM_GFX_CNTL の選択）、11 節の停止（HQD の dequeue）、6 節（IH の処理）、5 節の fault（STATUS の読み）。
- **問題**: MM_INDEX/DATA、PCIE_INDEX2/DATA2、GRBM_GFX_INDEX・GRBM_GFX_CNTL（SE/SH や me/pipe/queue の選択）、SMU の mailbox、PSP の ring の投入は、どれも「選んでから読む・書く」2 段の操作で、並行に走ると別の相手の値を読み書きする。gpu.h の契約では別の session の ioctl は並行に来る。design は node の lock 1 つしか書かず、IH の処理を割り込みの handler で行うのか thread で行うのか、どの lock が IRQ-safe かも書いていない。
- **根拠**: `include/drivers/gpu/gpu.h:106-109`（別の session は並行）。Linux はそれぞれ別の lock を持つ（`mmio_idx_lock`・`pcie_idx_lock`・`grbm_idx_mutex`・`srbm_mutex`・SMU の message の lock・PSP の mutex。名前は確認のための参照で、code に写す物ではない。行は未確認）。`src/drivers/gpu/gpu.c` の `drv_gpu_complete`・`drv_gpu_report_session_error` は `spin_lock_irqsave` なので割り込みからも呼べる（確認）。
- **直し方の案**: 2 節に lock の表を足す: (a) 間接の register の組ごとの spinlock（IRQ を止める）、(b) GRBM の選択の lock（選択を戻すまで持つ）、(c) SMU・PSP の command の mutex（sleep 可）、(d) IH は割り込みで writeback の wptr を読んで thread を起こすだけにするか、handler で dispatch するなら handler から呼んでよい関数の一覧。窓の外の VRAM の転送は (a) の lock の中で 1 語ずつになり遅いので、15 節の 10 にその代価を書く。

### M4. RLC の container の header の offset が 4 byte ずれている

- **該当**: 4.2 の表の rlc の行「0x68 から srlc・srlg・srls の各 {版・feature・大きさ・offset}」。
- **問題**: v2_0 の部分は 0x20 + 18 語 = 0x68 で終わり、v2_1 は 0x68 に「direct の reg list の長さ」の 1 語を置いてから srlc を始める。正しくは srlc が 0x6C、srlg が 0x7C、srls が 0x8C（header 全体 156 byte）。design の表のままだと RLC の 3 つの list の image を全部取り違える。
- **根拠（確認）**: `temp/linux/.../amdgpu/amdgpu_ucode.h:226-246`（v2_0、18 語）・`:249-264`（v2_1 の最初の欄）、note b の 2.4 節（`b-psp-fw-smu.md:216-221`、「Total 156 bytes」）と 2.8 の rlc の行（srlc の offset 25344 など）。
- **直し方の案**: 4.2 を「0x68 に 1 語、0x6C から srlc・srlg・srls（各 16 byte）」に直す。host 試験 (a) の期待値は note b の実測（srlc 592 byte @ 25344、srlg 1536 @ 25936、srls 16480 @ 27472）で書く。

### M5. 温度の読み方に出典が無く、Tier 0 の熱の安全の手当てが欠けている

- **該当**: 2 節の 2、3 節 A3（「温度の register を読む（THM）」）、12 節の最後、14 節の 5、15 節の 3、4.2（smc の pptable は使わない）。
- **問題**: (1) Linux は Navi10 の温度を SMU の metrics の表（driver の表を VRAM に置いて SMU に転送させる）から読み、THM の register では読まない。design の「THM の register」は register の名前・offset・出典が無く、note b にも無い。A3 の `temp=` と A9 の前の確かめ（判断の項目 3）の根拠が無い。(2) Tier 0 は fan・thermal・FW_CTF の feature を有効にしないので、温度が上がっても止める仕組みが無い。design は「log に出す」だけで、止める閾値も、誰が止めるかも無い。(3) Tier 0.5 は note b が「pptable の転送が先に要るかもしれない」と書いている。design は VBIOS を読まないが、`navi10_smc.bin` は pptable を 3 つ持っている（4.2 で「使わない」としている）ので、ここが Tier 0.5 の pptable の出所になりうるのに検討が無い。(4) THM の header は監査の表にも無い。
- **根拠（確認）**: `temp/linux/.../pm/swsmu/smu11/navi10_ppt.c:607-611`・`:696-700`（温度は metrics の欄）、`include/asic_reg/thm/thm_11_0_2_offset.h:26`（CG_MULT_THERMAL_STATUS は在るが Linux は Navi10 の温度にこれを使わない）、note b の 7.7 節（`b-psp-fw-smu.md:1067-1106`）・2.8 の smc の行（`pptable_count 3`）。
- **直し方の案**: A3 の温度の源を決めて出典を付ける（SMU の metrics の表の転送（SetDriverDramAddr・TransferTableSmu2Dram）が Tier 0 で動くかを p004 の受け入れにする、または THM の register を使うなら実機で metrics と比べて確かめる段を入れる）。温度の閾値（例: edge が X ℃ を超えたら新しい投入を止めて device の fault）と監視の thread を 11 節に足す。Tier 0.5 の pptable の出所（smc.bin の soft pptable か VBIOS か）を判断の項目 3 に書き足す。

### M6. VRAM の allocator（CPU から見える窓、予約の一覧、整列）が設計されていない

- **該当**: 2 節の 4・5、1.2 の BAR0、4.3 の 7（TMR）、10.1 の MAPPING、15 節の 10。
- **問題**: kernel が CPU で書く物（fw_pri、PSP の ring・command・fence、GART の表、ring、MQD、EOP、CSB、page table、shader の code、cursor、自前の surface）は全部 BAR0 の窓（resizable BAR が無ければ先頭 256 MiB）の中に要るが、design は「窓の中と外」を分ける allocator の規則を持たない。TMR（PSP しか触らない、大きさで整列が理想）は窓の外でよい。予約は GOP の framebuffer・上端 1 MiB・TMR だけで、(a) CSM・legacy の VGA の mode（D1VGA_MODE_ENABLE）で GOP の framebuffer が無い場合、(b) VBIOS が自分用に予約する VRAM（firmwareinfo の `vram_usagebyfirmware`）を知る手段が無い（design は VBIOS を読まない）。
- **根拠（確認）**: `temp/linux/.../amdgpu/gmc_v10_0.c:541-560`（VGA mode の時の予約、そうでなければ HUBP0 の viewport × pitch）、`amdgpu_gmc.c:1030-1047`、`amdgpu.h:288`（9 MiB）、`amdgpu_atomfirmware.c:106-180`（VBIOS の予約の表）、`amdgpu_ttm.c:1713-1770`（上端の予約）、`amdgpu_discovery.h:29-30`、`amdgpu_psp.h:242`・`:287`。上端 1 MiB が discovery・runtime DB・training の p2c を覆うことは確認した。
- **直し方の案**: 5 節の後に「VRAM の配置」の小節を作り、窓の中（kernel が CPU で書く物と user の map）と窓の外（TMR・GPU だけが触る物）の 2 つの領域、予約の一覧（GOP の framebuffer、上端 1 MiB、VGA mode なら A1 で attach しない）、整列（fw_pri 1 MiB、TMR、256 byte など）、A1 の ok の行に配置を出すこと、VBIOS の予約を知らないことの危険（14 節）を書く。

### M7. A8 の「text console を自前の surface に付け替え」は zedBSD に無い口に依存する（計画に無い依存）。nvrtx の形にすれば A6 の SDMA の copy も要らない

- **該当**: 2 節の 4、3 節 A6・A8、12 節の 1 行目、16 節（p006 の依存は p005）。
- **問題**: pcat の text console には surface を外す・付け替える口が無い（init・ready・putc・write・clear・suspend・resume・snapshot だけ）。nvrtx の design もこれを「計画に無い依存」として挙げ、実装では付け替えをせず、firmware の console を VRAM の元の位置に置いたまま、そこへ flip で戻す形にした。また A6 は GOP の画面を SDMA（A5）で自前の surface に写すので、display（DCN は firmware も SMU も要らない）が PSP・CP の成功に依存してしまう。A6 から A8 までの間は console が GOP の framebuffer に書き続けるのに画面はそれを出さず、段の印が見えない。
- **根拠（確認）**: `src/drivers/platform/pcat/graphics/text.c`（関数の一覧、`:483`〜`:834`）、`plan/ws037/nvrtx-design.md:109`、`src/drivers/gpu/nvrtx/scanout.c:8-31`（console は VRAM の先頭のまま、GPU device はそこから flip で離れて戻る）。
- **直し方の案**: GOP の framebuffer をそのまま「console の surface」として持ち（allocator から除いたまま）、A6 は client の surface と GOP の framebuffer の間の flip だけにする。copy と付け替えを消す。これで p006（A6〜A8 の display の部分）は p002 だけに依存でき、PSP が実機で止まっても display と cursor を先に確かめられる（16 節の依存を直す）。付け替えを残すなら、pcat の口を作る作業を WS039 の外の依存として Q1 に出す。

### M8. SMU の mailbox の手順が design に無い（値が commit しない note にだけある）

- **該当**: 3 節 A3、11 節の停止の (6)、4.3 の対になる SMU の節が無い。
- **問題**: A3 と停止は SMU に message を送るが、design には mailbox の register（MP1 の C2PMSG_66・82・90）、送る前の待ち、応答の値の意味、timeout が無い。これらは note b にだけあり、note は commit しない（design 8 行目）。worktree を作り直すと p004 の実装の根拠が消える。
- **根拠（確認）**: `b-psp-fw-smu.md:885-900`（register と手順）・`:940-948`（message の番号、PrepareMp1ForUnload 0x32）。
- **直し方の案**: 4.4「SMU の mailbox（A3・停止、Procedure referenced の対象）」を足し、register・順・応答の値・timeout・送る message（0x03・0x02・0x32）を出典付きで書く。同じ理由で、design が「note を見よ」で済ませている値（A0 の register の dword、DCN の register の offset など、p002 で使う物）を design か、commit する `plan/ws039/` の文書に移す。

### M9. license の監査の表に、design と note が参照した file が欠けている。p007 の行の版が食い違う

- **該当**: amdrdna-license-audit.md の「file ごとの表」（「作業の文書と design.md が参照した file」と書いている）。
- **問題**:
  1. note と design が参照しているのに表に無い Linux の file（機械的に照合した。design・note の引用の file 名と表の file 名の差）: `amdgpu/amdgpu_discovery.h`（discovery の位置と大きさ、design 2 節の 4・A0 の値）、`amdgpu/amdgpu_ih.h`（IH の ring の 256 KiB、6 節）、`amdgpu/amdgpu_ttm.c`（VRAM の予約、BAR0 = VRAM の offset の根拠）、`amdgpu/amdgpu.h`、`amdgpu/atom.c`、`amdgpu/amdgpu_bios.c`、`amdgpu/amdgpu_object.c`、`amdgpu/amdgpu_dpm.c`、`amdgpu/amdgpu_ids.h`、`include/amd_asic_type.h`、`amdgpu/gmc_v9_0.c`、`amdgpu/amdgpu_cs.c`、`amdgpu/amdgpu_vcn.c`、`amdgpu/vcn_v2_0.c`、`amdgpu/{cik,si,vi,soc21}.c`、`include/uapi/linux/kfd_ioctl.h`（読むだけの物を含む）。M5 で THM を使うなら `include/asic_reg/thm/thm_11_0_2_*.h` も。
  2. 表は Mesa 25.3.6 の `aco_assembler.cpp`・`aco_opcodes.py`・`ac_nir_prerast_utils.c` などに「p007」と書いて 25.3.6 の SHA-256 を付けているが、p007 は Mesa **25.0.7** から取り、block の comment にも 25.0.7 と書く（p007 の出典の表は別の SHA-256）。13.3 の監査で版が合わなくなる。
- **根拠（確認）**: `amdrdna-license-audit.md:25-189`、`phase007/phase.md:18-33`（25.0.7 と SHA-256）、note c の `:112-114`（`amdgpu_ttm.c`）・`:549`（`amdgpu_ih.h:30-31`）、note a の `:351`。
- **直し方の案**: 欠けた file を同じ形（path・SHA-256・license・取るデータ）で足す。p007 の行は「p007 は 25.0.7、別の監査の表（`amdrdna-compiler-license-audit.md`）」と書いて 25.3.6 の表から「p007」の印を外すか、両方の版の行を持つ。

### M10. p005 の範囲が膨らんだのに LW が元のまま。10.1 の段の欄と 16 節が食い違う

- **該当**: 16 節の p005（3〜5 LW）、10.1 の「段」の欄、ws.md の Phase の表。
- **問題**: ws.md の案の p005（CP・SDMA・最初の copy と fence、3〜5 LW）に対し、design の p005 は golden・RLC・CSB・GFX の ring・HQD・SDMA・fence と割り込み・最初の copy に加えて、session の VMID・4 段の page table・BO・`drv_gpu_ops` の RESOURCE〜SHARE・recovery・停止の hook・Tier 0.5・host 試験 (e) を含むが、見積もりは同じ 3〜5 LW。一方 10.1 は open/close・RESOURCE・TRANSFER・MAPPING を A4（p004）に置き、16 節（p005）と食い違う。
- **直し方の案**: p005 を「engine の起動（A5）」と「session・BO・ops・recovery」の 2 つに分けて見積もり直し、10.1 の段の欄を 16 節に合わせる（B3 の登録の位置と一緒に決める）。

---

## minor

### m1. design.md の冒頭「symbol の名前を写さない」と本文が合わない（6 行目）
本文は出典の識別子を地の文で使っている: `cp_fw_write_wait`（7.5、196 行）、`tcc_rb_non_coherent`（9.3、265 行）、`dc_disable_accelerated_mode`（8.5、237 行）、`pciidlist`（1.1）、`common_firmware_header`（4.2）、`ac_align_shader_binary_for_prefetch`（p007）と、register・field の mnemonic 全般。Guardrail の規則は code についての物で、出典の指示としての名前は許されるが、冒頭の宣言は不正確。**案**: 「出典の識別子は出典の指示と hardware の語として現れる。code には 13.1 の規則で写さない」と直す。実装の担当が design の語をそのまま macro 名にしないよう、13.1 の例の表を A0〜A2 の register にも広げる。

### m2. 13.2 の `derived` の例が 1 行の comment の形（377 行）
Guardrail は出典の comment を複数行の形にすることを求める。例の `/* derived: … */` は「値の出典の comment の中に」と書いてあるが、1 行の comment と読める。**案**: 複数行の comment の中の 1 行として例を書き直す。

### m3. 13.3 の監査の script が tree に無い `amdgfxregs.h` を読む前提（382 行）
Mesa 25.3.6 の `amdgfxregs.h` は build の時に JSON から作られる（`temp/mesa/src/amd/common/meson.build:34-37`、tree に file は無い。確認）。**案**: `src/amd/registers/gfx10*.json` の名前から直接集める。また「上 30 行の中に出典」は 1 つの comment が多くの block を覆えてしまうので、block の直前の comment に限る。

### m4. libdrm の `amdgpu.ids` と `pci.ids` の license が監査の表に無い（監査 13 行）
値（device id・revision）を取るので、license（`pci.ids` は GPL-2.0-or-later か BSD-3-Clause の選択、`amdgpu.ids` は libdrm の MIT。未確認、file の先頭で確かめる）と版・SHA-256 を記録する。

### m5. firmware の file の数の不一致
15 節の 1 は「navi10 の 12 file（sos〜sdma1 と LICENSE.amdgpu・WHENCE）」（数えると 11 + 2 = 13）、12 節の (a) は「12 file」、監査は「11 file」。**案**: 「BLOB 11 file（mec2 は mec と同じ byte）＋ LICENSE.amdgpu・WHENCE」に揃える。

### m6. LOAD_TOC の応答が 0 の時の 4 MiB は Linux と違う（4.3 の 7）
Linux は 4 MiB を既定にした後、LOAD_TOC が成功すれば応答の値で上書きする（0 でも）（`amdgpu_psp.c:864-887`、確認）。0 は異常として A2 の fail にするか、独自の判断であることを書く。

### m7. 14 節の 13（64 bit の doorbell）は既に解ける
`kern_mmio_write64`（`include/kern/device-io.h:49`）と amd64 の `hal_mmio_write64`（`src/hal/amd64/asm.c:519-529`、volatile の 64 bit の 1 回の store と write barrier）がある（確認）。HAL の変更は要らない。p002 の確かめを「disassemble で 1 命令の store を見る」に絞れる。

### m8. executor の写す量の見積もり（9.4、15 節の 5）
9.4 が挙げる 13 file は `wc -l` で計 5,993 行（確認）で、「約 8〜9 千行」と合わない（p001 の phase.md は 5〜6 千行）。また `vk-resource.c` は nvrtx の hardware の語を含む（grep で確認）。数を直し、hardware に依る部分の切り分けを p008 の最初の作業に書く。

### m9. 8.4 の DST_OFFSET が判断の項目 7 を指すが、項目 7 にその内容が無い
HDMI の時に pixel clock を読めず DST_OFFSET を 0 で試す件を、判断の項目 7 に足すか 14 節の 9 だけを指す。

### m10. 16 節の p001 が「済」だが phase.md は in-progress
review の反映と clearance の前なので「review 待ち」にする。

### m11. IH の処理の loop で wptr を読み直さない（6 節）
Linux は rptr を書いた後に wptr を読み直し、動いていればもう一度回す（`amdgpu_ih.c:209-250`、note c の 6.3）。RPTR_REARM に頼るだけだと取り逃しの可能性を design が説明していない。

### m12. A1 で legacy VGA の mode（CSM）の時の扱いが無い
D1VGA_MODE_ENABLE が立っていると HUBP の surface は使われていない（`gmc_v10_0.c:545`）。8.2 の 7 の「見込み」を「立っていれば attach しない」に変える。

### m13. 非同期の bring-up（A2〜A8、root の mount の後）と shutdown の hook の競合
4.1 の最後で A2 を非同期にするが、bring-up の途中で reboot・shutdown が来た時に、停止の手順（11 節）と bring-up の thread のどちらが何を持つかが無い。段の進行を状態として持ち、停止は今の段までの逆順だけを流す、を書く。

### m14. compute の HQD（14 節の 7）を誰も使わない
10.1 の NOTIFICATION は GFX の ring だけに投入し、A9 の dispatch も GFX の ring で出せる。KIQ 無しの HQD は未知の危険（14 節の 7）なので、使い道が決まるまで A5 から外すと危険が減る。残すなら用途（例: 将来の async compute）を書く。

### m15. 9.3 の SDMA の copy と GFX の ring の順序
clear・copy の一部を SDMA に出す案（9.3 の最後）は、同じ Vulkan の queue の中の順序を engine をまたいで守る仕組み（fence の待ち・WAIT_REG_MEM）が要るが書かれていない。最初は queue の中の copy を GFX の ring（DMA_DATA・draw）だけで行い、SDMA は kernel の copy（A6 の案を残すなら）に限るのが単純。

### m16. firmware の SHA-256 の検査の場所が不明（4.2）
「package の manifest で SHA-256 を検査するので crc は見ない」とあるが、kernel が load の時に検査するのか、install の時だけかが無い。kernel で検査しないなら、header の大きさの整合（4.2 の最後）を load の時の必須の検査として書く。

### m17. A4 は GOP が scan out 中に MMHUB の system aperture を書き直す
5 節の (b)（MMHUB の aperture・default の address）は DCN の読み出しの経路に当たりうる（未確認）。Linux も DC の初期化の前に同じ書き込みをしているので壊れない見込みだが、A4 の ok の行に HUBP の underflow の状態（8.3 の最後の register）を出して確かめる。

### m18. 実機で固まった時の証拠の残し方が無い（12 節）
合格の印は SSH で取る kernel.log だが、PSP・MMIO で host が固まると log は disk に残らない見込み。A6 以降は画面にも印が出ない（M7）。nvrtx の design（判断の項目 6）のように、危ない段の前の log の同期（`amdrdna.pause` の間に sync）、または次の起動で読める印（scratch の register、未確認）を決める。

### m19. HPD の割り込みの enable・ack の register が無い（6 節、10.1 の DISPLAY_EVENTS）
src id 9（ctx 0〜5）だけが書かれ、DCN の HPD の割り込みの enable・ack・sense の register と出典が無い。M1 で events を HPD の構成の番号にするなら必要。

### m20. 10.1 の recovery の `reset` は「NULL」と書く
11 節は自動の reset をしないと決めているが、10.1 の表は `reset` を callback として挙げる。gpu.c は NULL を許す（`gpu.c:7472-7473`、確認）。M2 と合わせて NULL と明記する。

---

## 確かめて問題が無かった物（抜き取り）

- PSP の bootloader の待ち（20 回、mask 0x8000FFFF）、SOS の 20 ms と C2PMSG_81 の変化の待ち: `psp_v11_0.c:166-181`・`:270-280`。
- SMU の LOAD_IP_FW を SETUP_TMR の前に送る順（MP0 11.0.0 は pmfw centralized）: `amdgpu_psp.c:95-121`。
- doorbell の index は全部 64 bit の slot の番号で、design の「<< 1 して dword」と矛盾しない: `amdgpu_doorbell.h:186-215`、`gfx_v10_0.c:4666`・`:4695`、`sdma_v5_0.c:1411-1413`、`navi10_ih.c:578`。
- CP_HQD_PQ_CONTROL の RPTR_BLOCK_SIZE 9、CP_RB_ACTIVE・CP_MAX_CONTEXT・CP_DEVICE_ID: `gfx_v10_0.c:6372-6374`・`:6550`・`:6973-6988`。
- DCN の FLIP_TYPE bit 1・FLIP_PENDING bit 8、cursor の MODE 10:8・PITCH 17:16・LINES_PER_CHUNK 28:24・SIZE（HEIGHT 0、WIDTH 16）: `dcn_2_0_0_sh_mask.h:9514-9517`・`:9993-10021`。
- DCN の割り込みの src id（VSTARTUP 0x3C〜0x41、HUBP の flip 0x4F〜0x54、HPD 9）: `irqsrcs_dcn_1_0.h:156`・`:1039-1044`・`:1108-1113`。
- container の header の共通の 32 byte、sos・smc・gfx・sdma の欄（rlc 以外）: `amdgpu_ucode.h`、note b の 2 節。
- IH の RB_CNTL の field と MC_SPACE 4（GART）の選択: note c の 6.1・6.2（`c-gmc-vm-ih.md:548-615`）。

## 確かめていない物

- mode1 reset の後に BIOS_SCRATCH_7（post の印）と DCN の状態が消えるか（B2 の 2・3 は Linux の code の道からの推論）。
- PSP の buffer を GMC・HDP の初期化の前に VRAM に置いて動くか（B1 の 5、Linux の bare metal に例が無い。`adev->debug_use_vram_fw_buf` の debug の道はある: `amdgpu_psp.c:509`）。
- M3 の Linux の lock の行番号。
- `pci.ids`・`amdgpu.ids` の license の文面（m4）。
- A4 の MMHUB の書き換えが GOP の scanout に影響するか（m17）。
- note の値のうち、上の抜き取り以外（GMC の L2 の既定値、golden の表、DCN の stride、PM4 の各 opcode など）は正本と全部は照らしていない。

## 件数

| 区分 | 件数 |
| --- | --- |
| blocker | 3（B1〜B3） |
| major | 10（M1〜M10） |
| minor | 20（m1〜m20） |
| 計 | 33 |

---

## 対応（2026-10-15 P3、design.md の第 2 版に反映。指摘ごとの扱い）

| 指摘 | 扱い | design.md の場所 |
| --- | --- | --- |
| B1 | 直した: PSP の buffer・ring・writeback・IH の ring は GART の system memory（Linux の bare metal と同じ）、VRAM に置く物の HDP の規則（5.1）、段の順を GMC・IH（A2）→ PSP（A3）に変更 | 2 節の 5、3 節、4.3、5.1 |
| B2 | 直した: C2PMSG_81 ≠ 0 なら A1 まで読むだけで止まり cold power cycle を求める（mode1 reset は試さない）。12 節に「A3 以降の試行の間は cold power cycle」、host.md の項目、LW に反映 | A0、3.1、11 節、12 節、14 節の 2、15 節の 6・9 |
| B3 | 直した: 登録は 1 回、ops は登録の前に組み立てる。renderer だけの登録を A5s（新しい p011）で行い、display 付きは A8。p008 は p011 に依存 | 3 節の登録の規則、10.1、15 節の 12、16 節 |
| M1 | 直した: events = topology の sequence、wait/refresh = VSTARTUP、cursor の 3 つ、DISPLAY_CONTROL（power = HUBP の blank、refresh）、scanout は query_device＋constraints、import_image = NULL | 10.1 |
| M2 | 直した: isolate = NULL、reset = NULL、CP の priv/bad opcode は device の fault、VM fault だけ session の fault | 10.1、11 節 |
| M3 | 直した: lock の表（間接の register の spinlock、GRBM の選択の mutex、PSP・SMU の mutex、ring・display・node）と IH は handler で thread を起こすだけ | 2 節の 8 |
| M4 | 直した: 0x68 に 1 語、srlc 0x6C・srlg 0x7C・srls 0x8C、header 156 byte。host 試験 (a) の期待値は実測 | 4.2 |
| M5 | 直した: 温度は THM の register と SMU の metrics の表の 2 つの源（出典付き）、監視の thread と閾値（95 ℃）で fault、p004 の受け入れ、Tier 0.5 の pptable の出所を判断の項目 3 に | 2 節の 2、4.4、11 節、15 節の 3 |
| M6 | 直した: VRAM の配置の小節（予約、窓の中・外の pool、VBIOS の予約が未知である余裕、整列） | 5.2 |
| M7 | 直した: GOP の framebuffer を console の surface のまま残し、A6 は flip だけ（copy と付け替えを消した）。p006 の display は p003 だけに依存（登録は p011） | 2 節の 4、A6、8.3、8.5、16 節 |
| M8 | 直した: SMU の mailbox の小節（register・手順・応答の code・timeout・message）。note の値を p002 で design か phase.md へ写す規則 | 4.4、冒頭 |
| M9 | 直した: 欠けた Linux の file 19 件と host の表（pci.ids・amdgpu.ids）を監査の表に追加。p007 の Mesa 25.0.7 の扱いを判定の要点の 5 に | amdrdna-license-audit.md |
| M10 | 直した: p005 を A5（engine）と p011（session・ops・登録）に分け、LW を見直した（計 43〜70 LW）。10.1 の段の欄を Phase に | 16 節、10.1 |
| m1 | 直した: 冒頭の宣言を「出典の識別子は出典の指示と部品の語として現れ、code には写さない」に。13.1 の例を A0〜A2 の register に広げた | 冒頭、13.1 |
| m2 | 直した: derived は複数行の comment の中の行 | 13.2 |
| m3 | 直した: JSON の名前から集める、block の直前の comment に限る | 13.3 |
| m4 | 直した: pci.ids・amdgpu.ids の行 | 監査の表 |
| m5 | 直した: BLOB 11 file ＋ LICENSE.amdgpu・WHENCE・manifest に揃えた | 4.1、12 節、15 節の 1 |
| m6 | 直した: tmr_size 0 は A3 の fail | 4.3 の 7 |
| m7 | 直した: `kern_mmio_write64` で足りる、HAL の変更は要らない。14 節から外した | 1.2 |
| m8 | 直した: 5,993 行、vk-resource.c の切り分けを p008 の最初に | 9.4、15 節の 5 |
| m9 | 直した: 判断の項目 7 に DST_OFFSET を足した | 15 節の 7 |
| m10 | 直した: p001 は review 待ち | 16 節 |
| m11 | 直した: rptr の後に wptr を読み直す | 6 節 |
| m12 | 直した: VGA の mode なら attach しない | A1、8.2 の 7 |
| m13 | 直した: `stage` を持ち、停止は逆順。bring-up の thread との競合の規則 | 4.1、11 節 |
| m14 | 直した: compute の queue は作らない（dispatch は GFX の ring）。KIQ は Future Work | 7.3、15 節の 4 |
| m15 | 直した: queue の中の copy・clear は全部 GFX の ring、SDMA は kernel の copy だけ | 7.4、9.3 |
| m16 | 直した: install で SHA-256、load で header の整合を必須に | 4.1 |
| m17 | 直した: A2 の ok の行に underflow、14 節に危険 | A2、5.3、14 節の 11 |
| m18 | 直した: 危ない段の前に log の同期、次の起動で読める印は p002 で決める | 12 節 |
| m19 | 直した: HPD の enable・ack・sense の register と bit | 6 節 |
| m20 | 直した: reset = NULL | 10.1 |

残る物（design の外、Phase で確かめる）: mode1 reset の後の post の印（別の Phase の候補）、PSP の buffer の GART の配置が実機で通るか（Linux と同じ道なので見込みは高い）、THM の register の単位、SMU の metrics の表が feature 無しで転送されるか、MMHUB の aperture の書き換えと GOP の scanout。
