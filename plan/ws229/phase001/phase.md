<!-- awesome-plan project=zedbsd record=ws229-p001 -->
# ws229-p001: Gen9・Gen9.5 対応の調べと設計

Status: cleared（2026-10-14 Q1: 設計を受け入れ、D1〜D3 の決定を記録。design-reviewer は p002〜p004 と並行で通す）
Disposition: normal
Parent: [WS229](../ws.md)
Queue: q976
依存: 無し

## 範囲（code は書かない、設計文書だけ）

1. 今の i915（`src/drivers/gpu/i915/`、Gen11・Gen12）の中で graphics の版に依存する所の一覧（device-info・forcewake・MOCS・workaround・context の大きさと layout・execlists（Gen11 以降の ELSQ と Gen9 の ELSP）・GGTT・PPGTT・gt-power・irq・render の state の packet（`intel/genxml.h`）・blit・compute・firmware（GuC・HuC・DMC は要るか））。
2. EU コンパイラ: 前段（`src/drivers/gpu/compiler/`）と後段（`i915/compiler/compile.c`・`eu.c`・`intel/eu-encoding-gen12.h`）のうち、Gen9 で使える物と書き直す物。Gen9 の命令の符号化（native と compact）、SWSB が要らないこと、send の SFID と descriptor（data port・sampler・URB・render target の書き込み）、3 src の形、regioning の制約の違い。ユーザーの問い「命令が 2 つペアになっていた」の答え（hardware の co-issue か、compiler が意識する物があるか）を一次資料で確かめる。
3. GOP の framebuffer のままで GT を起こす方法（display の初期化を飛ばす口、今の takeover の経路との関係）。
4. 一次資料: Linux の i915（tree が派生元にしている v6.8.12）と Mesa の intel の compiler（brw）の source を tarball で取得・検証して読む（取り込まない。license に注意し、写さずに仕様として読む）。Intel の公開の PRM（Skylake・Kaby Lake・Coffee Lake の Volume）。
5. Phase の分け方（例: GT の起動・EU の符号化・3D と compute の state・Vulkan の通し・display controller・規約の見直し）、それぞれの見積もり、試験の方法（QEMU では iGPU を試せないので実機。8 世代の Core の機械の有無は Q1 がユーザーに聞く）。
6. 対応できる Core の世代の一覧（Gen9・9.5 に加え、今の PCI ID に無い Ice Lake（Gen11）・Rocket Lake・Alder Lake-S・Raptor Lake-S の desktop の iGPU を足す手間も一覧に）。

## 確かめ

- 設計文書を design-reviewer に通す（Q1）。HAL の API の変更が要るなら差分を plan に置くだけで適用しない。

---

# 設計文書（2026-10-14 P1）

2026-10-14 Q1（ユーザーの決定「Gen9対応は進めて、今日の夕方のコードフリーズに間に合えば入れます。間に合わなくてもドロップはオーケーです。」）: 網羅より速さ。GuC はベータ3 の WS230 なので Gen9 は execlists。display controller は今日の範囲外（GOP のまま）。§5 は **P1・P2・P3 が別々の file を持って今すぐ並行に入れる単位**で書いた。

## 0. 要約と、ユーザーが待っている 4 つの答え

**(a) EU コンパイラはどこまで流用できるか。**
前段（`src/drivers/gpu/compiler/`、SPIR-V → 共通の scalar IR、20.5k 行）は device に依存せず**そのまま**使える（Intel の名前は spirv.c の説明の中に 4 か所あるだけで、lowering の形は Gen9 でも同じ）。後段のうち **code generator（`compile.c` 5.2k 行＋compute/geometry の .inc 1.6k 行）は 9 割以上そのまま**: payload の形（FS の r0/r1/barycentric、VS の URB handle、CS の cross-thread/per-thread push、pixel centre の add(16) と V immediate、derivative の region、LINTERP の MUL+ADD の形、scratch の header、untyped/A64 の data port、sampler の descriptor、RT write/URB write の descriptor）は Mesa 25.0.7 の source で Gen9 と Gen12.0 が同じ関数（`setup_fs_payload_gfx9()` など）であることを確かめた。書き直すのは **encoder（`eu.c` 2.0k 行）と符号化の表（`intel/eu-encoding-gen12.h` 428 行）**: Gen9 は 128 bit の native 命令の **field の位置が全部違い**、SWSB が無く（hardware の scoreboard）、SEND が 1 descriptor（SFID は bit 27:24、EOT は bit 127）、opcode の値が違う（MOV は 1、Gen12 は 97）。generator 側で直すのは 6 点だけ（§3.4）: SLM の fence の BTI/commit、barrier の後の `sync.bar` → `wait n0`、barrier の ID の mask、compute の終了 message の bit 4、sampler header を使う message の header の作り方、2 payload の SEND（RT の header 付き、scratch の spill）を 1 payload に並べ直す。3 src の MAD は generator が**使っていない**（`drv_i915_eu_mad()` に呼び手が無い）ので、Gen9 の Align16 の 3 src の符号化は**要らない**。

**(b) 「命令が 2 つペアになる」はコンパイラに見えるか。**
見えない。SKL PRM Vol.7（IHD-OS-SKL-Vol 7-05.16、"CoIssue/Dual Issue" の節、p.722）は Gen9 の変更として「EM pipe が FPU pipe と同じ一般の整数・float 演算も実行できるようになった」と書き、thread scheduling の節は「EU は現在の thread の次の命令を発行した後、4 つの unit に空きがあれば**他の thread** の命令で埋める」と書く。つまり Gen9 の co-issue は **EU の thread arbiter が複数の thread の命令を 2 本の演算 pipe（FPU と EM）に同じ clock で発行する hardware の仕組み**で、命令の符号化にも並べ方にも現れない。Mesa も `brw_ir_performance.cpp:38` に「co-issue to FPU1 is currently not modeled」と明記し、brw backend に pairing の規則は無い（古い elk backend の `could_coissue()` は Gen7 だけ）。ユーザーの記憶に近い別の候補は 2 つ: (1) **compact 命令**（64 bit、2 本で native 1 本の 16 byte に収まる。bit 29 CmptCtrl。PRM Vol.7 p.871 "EU Compact Instructions"）— compiler が選ぶ最適化で、**使わなくてよい**（Mesa は `INTEL_DEBUG=nocompact` で全部 native にできる。zedBSD の encoder は Gen12 でも native だけなので同じ方針）。(2) **SIMD16 の「compressed」実行**（1 命令が 2 つの SIMD8 の半分として走る、Mesa の scheduler は issue 4 cycle と数える）— SIMD8 だけの zedBSD には関係ない。他に Gen9 固有で「2 本一組」の物: SENDS（2 payload の 1 命令、使わなくてよい）、barrier の send+wait、ELSE の前の NOP（Wa_220160235、ELSE を使わない zedBSD には無関係）、POW の後の NOP（POW を使わない）。**結論: compiler は何も意識しなくてよい。encoder の field 表の差し替えが仕事の本体。**

**(c) GOP のままで GT を起こせるか。**
起こせる。今の driver は N0（`display/takeover.c` の native-precheck）が STOP と言うと `display->absent = 1` にし、display の段を全部飛ばして GT だけを起こし、node を「display の無い GPU」として公開する経路を**既に持つ**（`device.c:1055-1099`、`display/display.c:1860-1876`）。GT の起動は display の register・GGTT の低い番地・D-state に触れない（`display.c:1858` の log の通り。GDRST の full reset（bit 0）は GT だけ、GGTT は上端の 64 MiB（`I915_GT_GGTT_PAGES` 16384 page、`memory.c:79`）だけを書く。GOP の framebuffer は stolen の低い GGTT 番地にあり、N0 が重なりを検査する）。Gen9 でこの経路を使うには N0 の「pipe の電源が入っているか」の判定（`drv_i915_n0_pipe_powered()`、TGL/XE_LPD の power domain の表）を Gen9 の PWR_WELL_CTL2 の bit で置き換えた **N0-lite** が要る（§4）。段階 (1) の成果は「Vulkan が GT で描け、読み戻しで確かめられる」。画面に出すのは段階 (2) の display controller（今日の範囲外）か、Keiland の compositor が CPU 複写で `/dev/graphics` に出す経路（未決、§4.3 の D3）。

**(d) 対応できる Core の世代。** §6 の表。Gen9（Skylake = 6 世代、Kaby Lake = 7 世代）と Gen9.5（Coffee Lake = 8・9 世代、Whiskey Lake・Amber Lake = 8 世代、Comet Lake = 10 世代）は **GT の中身が同じ**（Linux は全部 `GEN9_FEATURES`、workaround の差は数個、PCI ID は 86 個）。Ice Lake（Gen11、display 11）は EU の符号化が Gen9 形式で GT は Gen11 形式（ELSQ・CSB 12・forcewake の表が別・genxml gen110）なので「Gen9 と Gen12 の中間」の別の仕事。Rocket Lake・Alder Lake-S・Raptor Lake-S は GT が **Tiger Lake と同じ Gen12.0**で、PCI ID と display の差（RKL は display 12 だが Type-C 無し、ADL-S/RPL-S は display 12（XE_D）で ADL-P の 13 ではない）だけ。

**見積もり（±50 %、LW）**: 段階 (1) GOP のまま GT と EU と Vulkan の通し: **8 LW**（p002 2.5、p003 2.0、p004 2.0、p005 1.5）。段階 (2) display controller: **6 LW**（p006、今日の範囲外）。規約の見直し 0.5。合計 **14.5 LW**。Q1 の見積もり（6〜8、10〜15）の範囲の中。**実機なしには段階 (1) の受け入れができない**（QEMU は Intel の GT を模倣しない。§5.3）。

## 1. 一次資料と license

| 資料 | 取得 | 検証 | 読み方 |
| --- | --- | --- | --- |
| Linux v6.8.12 `linux-6.8.12.tar.xz` | cdn.kernel.org → `build/ws229-p001/linux/` | sha256 `19b31956…6039` が `sha256sums.asc` の行と一致（GPG の署名の検証は未実施。sha256sums.asc 自体は署名付き） | `drivers/gpu/drm/i915/` と `include/drm/` だけ展開。i915 の file は **MIT**（`gt/*.c`・`gt/*.h` は `SPDX-License-Identifier: MIT`、`i915_pci.c`・`intel_uncore.c`・`i915_irq.c`・`i915_reg.h`・`include/drm/i915_pciids.h`・`display/intel_ddi.c`・`intel_cdclk.c`・`intel_dpll_mgr.c`・`intel_dmc.c` は "Permission is hereby granted" の MIT の notice。2026-10-14 に先頭 12 行で確認）。今の tree と同じ規則（[i915-rebuild-rules](../../ws031/i915-rebuild-rules.md) §1）: register の番地・bit・表の**値だけ**を `intel/*.h` に出典（file・行・sha256）付きで転記し、logic は Zlib で書き直す |
| Mesa 25.0.7 `mesa-25.0.7.tar.xz` | worktree の `build/mesa-tools/`（`plan/ws101/tests/host/mesa-tools.sh` が取得・SHA-256 で検証して展開した物。sha256 `592272df…`） | 既存 | `src/intel/compiler/`・`genxml/`・`dev/`・`isl/`。**MIT**。今の `intel/eu-encoding-gen12.h`・`intel/genxml.h` と同じ規則（値だけ、出典の sha256 を注記） |
| Intel PRM（Skylake・Kaby Lake・Coffee Lake の公開の Volume、Intel の "Graphics for Linux developer reference" の PDF の mirror kiwitree.net/~lina/intel-gfx-docs/prm/） | `build/ws229-p001/prm/`（15 本、`SHA256SUMS`。SKL vol01・02a・03・04・05・06・07・12・13・16、KBL vol01・04・16、CFL vol01）。`pdftotext -layout` で `prm/txt/` に | mirror の sha256 を記録しただけ（Intel 側に公開の sha は無い。Intel の docs の頁は 1 本の PDF（772631）に統合されていて volume 別の原本は取れない） | 仕様として読む。文章は写さない。値は hardware の事実。引用は Doc Ref（例 IHD-OS-SKL-Vol 7-05.16）と頁で書く |

転記の扱い: 新しい `intel/*-gen9.h`（または既存 header の Gen9 の節）には今の header と同じ形の出典 block（Mesa/Linux の file、sha256、行）を置く。GPL の参照は無い（WS141 の GPL の規則は適用されない）。zedBSD の code は Zlib のまま。

## 2. 今の i915 の版依存の一覧（範囲 1）

今の driver は **Alder Lake-P / Tiger Lake 固定**で、`display_ver` を PCI ID から 12/13 に決め（`device.c:1017-1029`）、`graphics_ver` は 12 の定数で渡す（`device.c:1229, 1641`）。版の分岐の口は `drv_i915_gt_init_mmio(…, graphics_ver, …)`・`drv_i915_gt_init_tables(…, graphics_ver, …)`・`drv_i915_engine_init_workarounds()` などに既にある（`workarounds.c:216-280, 364-370, 609-629`、`device-info.c:481-500`）が、中身は Gen12 だけ。

| 領域 | file | 今（Gen12.0） | Gen9 の事実（Linux 6.8.12 の file:行） | 変更の規模 |
| --- | --- | --- | --- | --- |
| PCI ID | `intel/pci-ids.h`・`i915.c:66-70` | TGL/ADL-P/ADL-N/RPL-U/RPL-P | `include/drm/i915_pciids.h:331-542`: SKL 25（GT1 8・GT2 7・GT3 6・GT4 4）、KBL 22（AML_KBL 2 を含む）、CFL 39（CFL/WHL/AML 22 ＋ CML 17）。GT3/GT4 は VCS1（0x1c000）を持つ | 小。ID → platform（SKL/KBL/CFL/CML）と GT の段 → engine mask・stepping の表 |
| platform の識別 | `device.c:1017` | display_ver 12/13 | SKL/KBL/CFL/CML は graphics 9、display 9（`display/intel_display_device.c:447-468`）。stepping（REVID）は workaround の条件に要る（SKL A0–H0、KBL < C0） | 小 |
| forcewake | `mmio.c:26-42, 585-632`、`intel/forcewake-ranges.inc` | RENDER 0xa278/0xd84、GT 0xa188/0x130044、VDBOX/VEBOX 別 domain、`__gen12_fw_ranges` | **RENDER・GT の request/ack の番地と masked の KERNEL bit は同じ**（`intel_uncore.c:2351-2360`）。MEDIA は 1 domain 0xa270/0xd88。範囲の表は `__gen9_fw_ranges`（`intel_uncore.c:1300-1333`、32 行。0x0–0xaff が GT、0x12000–0x13fff・0x1a000–0x1e9ff が MEDIA、0x22000 BCS は GT）。Gen9 は ack の fallback（bit 15 FORCEWAKE_KERNEL_FALLBACK、`fw_domain_wait_ack_with_fallback` 201-240）を使う（Gen12 も同じ関数） | 中。domain の列挙に MEDIA を足し、範囲の表を 1 本足す |
| SSEU・clock・MCR | `device-info.c:425-480, 284-380`、`device.c:1150-1186` | 1 slice × 6 DSS × 16 EU、0x9138/0x913c/0x9134、RPM_CONFIG0 の crystal、MCR 0xfdc = 0x80000000（multicast） | 3 slice × 4 subslice × 8 EU、GEN8_FUSE2 0x9120（slice 27:25、subslice disable 23:20）、GEN9_EU_DISABLE(s) 0x9134+4s（`intel_sseu.c:383-487`）。clock は CTC_MODE 0xa26c と固定の 24 MHz crystal（`intel_gt_clock_utils.c:89-109`）。MCR は GEN8 の layout（slice 27:26、subslice 25:24）で**最初の生きた slice/subslice に steering**（`intel_workarounds.c:1100-1135`、GT の workaround の一部）。L3 bank の表は無い | 中 |
| engine の表 | `intel/engine-table.inc`、`device-info.c:132-220` | RCS 0x2000、BCS 0x22000、VCS0 0x1c0000、VCS2 0x1d0000、VECS0 0x1c8000。media fuse で engine を落とす | VCS0 0x12000、VCS1 0x1c000（GT3/4 だけ）、VECS0 0x1a000（`i915_reg.h:894-916`）。media fuse の読みは MEDIA_VER ≥ 11 だけ（`intel_engine_cs.c:759-760`）。HEVC/SFC の capability は VCS0 だけ | 小 |
| context の大きさ・layout | `device-info.c:481-503`、`context.c:45-63, 236-270, 560-590, 820-870`、`intel/lrc-offsets.h` | render 14 page ＋ wa_bb 2 page、`gen12_rcs/xcs_offsets`、MI_MODE 0x60・BB_OFFSET 0x70・GPR0 0x74・WA_BB_PER_CTX 0x12・CMD_BUF_CCTL 0xb6、indirect ctx offset 0xD、per-context の indirect/per-ctx batch、相対 MMIO | render **22 page**（`intel_engine_cs.c:47`）、`gen9_rcs_offsets`（`intel_lrc.c:328`）と `gen9_xcs_offsets`（141、絶対番地の長い形。相対 MMIO は Gen11+）。MI_MODE 0x54・BB_OFFSET 0x64・GPR0 0x68・WA_BB_PER_CTX 0x18、CMD_BUF_CCTL 無し、indirect ctx offset 0x26（720-813）。CTX_CONTEXT_CONTROL は Gen9 で SAVE_INHIBIT（bit 2）と RS_CTX_ENABLE（bit 1）も masked で落とす（881-905）。**engine 共通の indirect ctx の workaround batch が必要**（`gen9_init_indirectctx_bb` 1770-1838: MI_ARB_DISABLE、L3SQCREG4 の flush（0xb118、SRM/LRI/PIPE_CONTROL/LRM）、SLM の clear の PIPE_CONTROL、0x7014/0x2088/0x2090 の LRI、MI_ARB_ENABLE）。per-ctx batch は無し。descriptor は同じ bit（VALID・64B addressing・PRIVILEGE）だが上位 dword の context id は Gen8 の 21 bit の field（engine class/instance の bit は無い） | 大。offsets の表 2 本、wa batch 1 本、slot の番号を版で引く |
| execlists | `submit.c:84-150` | ELSQ 0x510 ＋ CONTROL 0x550、CSB 12、HWSP の write index 0x2f、`gen12_csb_parse`、RING_MODE の GEN11_GFX_DISABLE_LEGACY_MODE | **ELSP 0x230 に 4 dword**（port 1 の上・下、port 0 の上・下。`execlists_submit_ports` 908-942、`write_desc` 726-735）、control 無し。CSB **6**（mask 0x7）、MMIO の buffer 0x370、pointer 0x3a0、HWSP の write index **0x1f**。`gen8_csb_parse`: IDLE_ACTIVE(bit 0) \| PREEMPTED(bit 1) で pending → active（1746-1749）。RING_MODE は GFX_RUN_LIST_ENABLE（bit 15）。WaIdleLiteRestore: 毎 request の尾に MI_ARB_CHECK＋NOOP、tail が進まない再投入は FORCE_RESTORE（686-722）。RING_IMR（base+0xa8）で engine 毎の割込みを mask | 中。submit の write と parse を関数 pointer で版分け |
| reset | `reset.c`、`device-info.c:586-608` | GDRST 0x941c、GEN11 の domain bit、SFC lock、RESET_CTL の handshake | handshake は同じ（`intel_reset.c:563-609`）。domain bit が違う: RENDER 1、VCS0 2、BLT 3、VECS 4、VCS1 7、FULL 0（`intel_gt_regs.h:661-700`）。SFC lock 無し。write＋wait を 2 回（IP < 12.70 は Gen12 も同じ） | 小 |
| workaround | `workarounds.c`、`intel/workarounds.h` | ADL-P の GT 4 個、RCS 7 個、ctx 5 個、whitelist | GT: MCR steering、GAM_ECOCHK 0x4090 bit 8/25、MMCD_MISC_CTRL 0x4ddc bit 31/27、UCGCTL4 0x940c bit 14、GAMT_ECO 0x4ab0 bit 18（stepping 条件）、KBL < C0 の GAMT_CHKN 0x4ab8 bit 28（`intel_workarounds.c:1138-1227`）。ctx（RCS）: 9 register（0x7014、0xe194、0xe4f0、0x7004、0xe188、0x7300、0xe184、0xe180、0x2580 の preempt level = command）＋ SKL の IZ hashing・KBL/CFL の 0x7014 bit 8・0xe100 bit 4（441-637）。RCS engine: 0x20e0 bit 14、0xb004 bit 7、0x20d4 bit 2、0xb11c、0xb118 bit 21/22、0xb008 bit 0（2546-2600）。whitelist: 0x2248・0x2580・0x7304・0x7014（＋SKL/KBL 0xb118、CFL 0x2348 RO range 4、CML CTX_TIMESTAMP）（1973-2073）。**0x7300・0x7304 は Gen9 と Gen12 で意味が違う** | 中。表を platform で持ち、既存の `drv_i915_wa_*` の helper はそのまま |
| MOCS・PAT | `workarounds.c:607-780`、`intel/mocs.h`・`mocs-table.inc` | global MOCS 0x4000（64）、L3CC 0xb020、uc index 3、`GEN12_PAT_INDEX` 0x4800+4i | **engine 毎の MOCS**（RCS 0xc800、VCS0 0xc900、VCS1 0xca00、VECS 0xcb00、BCS 0xcc00、各 64 dword。`intel_mocs.c:682-697`、engine resume 毎）、表は `skl_mocs_table`（0 UC=0x09/L3 0x10、1 PTE=0x30/0x30、2 cached=0x3b/0x30、63、未使用は index 1）。L3CC は同じ番地・形。PAT は **GEN8_PRIVATE_PAT_LO/HI 0x40e0/0x40e4 の 64 bit**（`intel_gtt.c:609-630`、8 entry: WB\|LLC、WC、WB\|ELLC_OVERRIDE、UC、WB\|LLCeLLC\|AGE 0..3）。PPGTT の PTE の cache は PWT/PCD/PAT bit（`gen8_ppgtt.c:32-59`、Gen12 の PAT index の 3 bit と位置は同じ bit 3/4/7 だが意味は PPAT index）。`gtt_write_workarounds`（`intel_gtt.c:434-499`）: 0x4dd4 = 0x67F1427F、0x4080 \|= 0xF、0x4024 = 0xF0007FFF | 中 |
| GGTT・TLB | `ggtt.c`、`device.c:1344-1443`、`tlb.c` | PTE = addr \| PRESENT、GGMS、table は uncached、invalidate 無し、TLB は 0xced8 系 | GGMS・PTE・table の大きさは同じ（`intel_ggtt.c:1110-1150, 280-290`）。Gen9 は GSM を WC で map し **GFX_FLSH_CNTL_GEN6 0x101008 = 1 で invalidate**（198-226）。TLB invalidate は engine 毎の register RTCR 0x4260・M1TCR 0x4264・M2TCR 0x4268・VTCR 0x4270・BTCR 0x426c（`intel_engine_cs.c:1158-1262`）。`has_coherent_ggtt` は真（chipset flush は要らない） | 小 |
| gt-power | `gt-power.c:86-212, 213-317` | `gen11_rc6_enable`、GEN9_PG_ENABLE の media sampler bit、RP1 を GEN10_FREQ_INFO_REC、CAGF を GEN12_RPSTAT1 | `gen9_rc6_enable`（`intel_rc6.c:146-214`）: WAKE_RATE_LIMIT 54<<16（SKL は 108<<16）、PG hysteresis 250/250、RC6_THRESHOLD 37500、PG_ENABLE は RENDER\|MEDIA（**SKL GT3/GT4 は PG を使わない**）。**Gen9 は RC6 の context 破壊の workaround の対象**（NEEDS_RC6_CTX_CORRUPTION_WA、`i915_drv.h:707`）: RC_CONTROL の enable は 0 で、park の時だけ手で RC6 に入れ、0x8504 を検査（550-563, 668-718）。RPS: `gen9_rps_enable` は同じ、GEN6_RC_VIDEO_FREQ 0xa00c = RP1<<23 を足す。RP1 は GEN6_RP_STATE_CAP 0x145998 の bit 15:8、CAGF は RPSTAT1 0xa01c bit 31:23。PCODE は同じ番地。PM 割込みは GEN8_GT_IIR(2) | 中。**段階 (1) の第 1 版は RC6 off・RPS を固定（RPe の要求だけ）**にして危険を避ける（§7） |
| 割込み | `irq.c` | GEN11_GFX_MSTR_IRQ 0x190010、INTR_DW、identity、GU_MISC | **GEN8_MASTER_IRQ 0x44200**（bit 31 control、bit 0 RCS・1 BCS・2 VCS0・3 VCS1・4 PM・6 VECS）、GT の bank 0..3 の ISR/IMR/IIR/IER 0x44300+0x10n（bank 0 = RCS 15:0 / BCS 31:16、1 = VCS0/VCS1、2 = PM/GuC、3 = VECS。`i915_reg.h:4299-4302`、`intel_gt_irq.c:405-487`）。engine の bit は USER_INTERRUPT 0、CS_MASTER_ERROR 3、CONTEXT_SWITCH 8、WAIT_SEMAPHORE 11。display は GEN8_DE_*（段階 (1) は mask したまま） | 中。`irq.c` の GT 半分を版で分ける（`i915_irq_dev` の ops の形は既にある） |
| firmware | `firmware.c`、`display/dmc.c` | DMC（display） | **GuC/HuC は Gen9 では要らない**（Linux の既定 enable_guc=0 for Gen < 12、`intel_uc.c:26-59`。GuC submission は Gen11+ だけ、HuC は GuC 経由でしか load できない → Vulkan Video を Gen9 でやらない限り不要）。DMC は display（skl_dmc_ver1_27、kbl_dmc_ver1_04）で段階 (2) | 無し（段階 (1)） |
| 3D state | `render/state.c`（297 か所）、`intel/genxml.h`（357 define） | gen120.xml | gen90.xml との差（Mesa 25.0.7 の genxml、§2.1） | 中 |
| compute | `render/compute.c` | PIPELINE_SELECT(GPGPU)、MEDIA_VFE_STATE、IDD、CURBE、GPGPU_WALKER、`PIPE_CONTROL0_HDC_PIPELINE_FLUSH`、L3ALLOC 0xb134 | 同じ packet で長さも同じ（VFE の dw3 bit 7 Reset Gateway Timer・dw4 1:0 Slice Disable、MEDIA_STATE_FLUSH の dw1 bit 6 は Gen9 固有、IDD の dw2 bit 20 は Gen12 固有）。HDC pipeline flush の bit は Gen12 だけ（gen90.xml の PIPE_CONTROL に無い）。**L3 の分割は L3ALLOC ではなく GEN8_L3CNTLREG 0x7034**（Mesa の `intel_l3_config.c` の Gen9 の表。SLM を使うなら SLM enable の bit が要る） | 小〜中 |
| blit・migrate | `render/blit.c`、`migrate.c` | blit は 3D pipeline の shader で描く（XY_* は使っていない）。migrate は BCS の context | Gen9 の genxml に XY_BLOCK_COPY は無いが使っていないので影響無し。**Gen9 の BCS は Linux では command parser 無しに user の batch を流せない**（`i915_cmd_parser.c:961-999` "BCS Engine unsafe without parser"）が、zedBSD は user の batch を受けず kernel が組む（`render/command.c`）ので parser は要らない | 無し |
| video | `render/video*.c`、`vulkan-codec.inc` | Gen12 の MFX/HCP | Gen9 の MFX は別物（HuC も要る）。**Gen9 では Vulkan Video を提供しない**（`uabi_capabilities` に HEVC/SFC を立てない） | 無し（非対応と明記） |
| display | `display/*`（約 70k 行） | TGL/XE_LPD | 段階 (2)、今日の範囲外 | 大 |

### 2.1 3D state の packet の差（gen90.xml vs gen120.xml、Mesa 25.0.7 `src/intel/genxml/`）

今の generator が出す packet（`render/*.c` の `GEN12_CMD_*` 69 種）のうち**長さも field も同じ**: PIPELINE_SELECT、VF_SGVS、VF_INSTANCING、VF_TOPOLOGY、URB_*、PUSH_CONSTANT_ALLOC_*、CONSTANT_VS（11）、GS、TE、STREAMOUT、CLIP、SBE、SBE_SWIZ、RASTER、WM、PS_BLEND、*_POINTERS、CLEAR_PARAMS、MULTISAMPLE、SAMPLE_MASK、SAMPLE_PATTERN、DRAWING_RECTANGLE、POLY_STIPPLE_OFFSET、LINE_STIPPLE、INDEX_BUFFER、GPGPU_WALKER、MEDIA_INTERFACE_DESCRIPTOR_LOAD、MEDIA_CURBE_LOAD、MI_FLUSH_DW、COLOR_CALC_STATE、BLEND_STATE、SF_CLIP_VIEWPORT、CC_VIEWPORT、SCISSOR_RECT、VERTEX_ELEMENT_STATE、BINDING_TABLE_STATE。

違う物:

| packet | Gen9 | Gen12 |
| --- | --- | --- |
| STATE_BASE_ADDRESS | 19 dword（length 17） | 22（20）: bindless sampler の base が dw19–21 |
| 3DSTATE_DEPTH_BUFFER（8） | format dw1 20:18、stencil write enable dw1 27、width dw4 17:4、height 31:18、LOD dw4 3:0、min array dw5 20:10、depth 31:21 | format dw1 26:24、width dw4 14:1、height 30:17、LOD dw7 19:16、min array dw5 18:8、depth 30:20、CCS/compression の bit |
| 3DSTATE_STENCIL_BUFFER | **5** dword（pitch dw1 16:0、MOCS dw1 28:22、enable dw1 31、qpitch dw4） | 8（MOCS dw5 6:0、write enable dw1 28、dw4–7 に width/height/depth/LOD） |
| 3DSTATE_HIER_DEPTH_BUFFER（5） | — | dw1 bit 20・23:22 を足す |
| 3DSTATE_PS（12） | — | dw6 bit 5（Dual SIMD8）・bit 9 |
| 3DSTATE_PS_EXTRA（2） | — | bit 4・9・18–22（bary の係数の要求）は Gen11+ |
| 3DSTATE_VS（9） | dw3 bit 31 Single Vertex Dispatch、max threads dw7 31:23 | dw7 bit 9、max threads 31:22 |
| 3DSTATE_HS/DS、SF、WM_DEPTH_STENCIL、WM_HZ_OP、VF、CONSTANT_PS、3DPRIMITIVE | — | 1〜数 bit の追加（使っていなければ 0 のまま） |
| 3DSTATE_CPS_POINTERS、PRIMITIVE_REPLICATION、CONSTANT_ALL | **無い** | Gen12 だけ → Gen9 では出さない |
| 3DSTATE_BINDING_TABLE_POOL_ALLOC | 有る（Gen8 の形） | 有る（長さと field は p004 で確認） |
| PIPE_CONTROL（6） | — | dw0 bit 9 HDC pipeline flush・bit 10、dw1 bit 17・22・27・28 tile cache・29 command cache・30 L3 fabric は Gen12 だけ。**今の compute が使う `PIPE_CONTROL0_HDC_PIPELINE_FLUSH` は Gen9 で出せない** |
| MI_BATCH_BUFFER_START（3） | dw0 bit 16 Add Offset Enable | — |
| MEDIA_VFE_STATE（9）・MEDIA_STATE_FLUSH（2）・INTERFACE_DESCRIPTOR_DATA（8） | 上の表 | 上の表 |
| VERTEX_BUFFER_STATE（4） | — | dw0 bit 25（L3 bypass disable、今の `GEN12_VERTEX_BUFFER_L3_BYPASS_DISABLE`） |
| SAMPLER_STATE（4） | border pointer dw2 23:6 | 31:6、dw0 bit 30、dw2 bit 1/2/3/5、dw3 bit 24/26 |
| RENDER_SURFACE_STATE（16） | MOCS dw1 30:24（同じ）。Gen9 だけ: inline clear colour dw12–15、Quilt dw10、V plane offset dw11 | Gen12 だけ: dw1 bit 15/17/18/31、dw2 bit 31、dw3 bit 18/19/20、dw4 bit 31、dw6 bit 15/30、dw10 bit 10/11、clear value address dw12–13 |
| MOCS の値（isl） | index << 1: UC 0、external 2、internal 4（`isl.c:199-205`） | internal 2<<1、external 61<<1、UC 3<<1、bit 0 = protected |
| blitter | XY_SRC_COPY/COLOR は gen60 で exclude、XY_BLOCK_COPY は無い | XY_BLOCK_COPY（12）・XY_FAST_COLOR | 使っていない |

## 3. EU コンパイラ（範囲 2）

### 3.1 流用できる物（確かめた根拠）

- 前段 `src/drivers/gpu/compiler/`（spirv.c 15.7k、optimize.c 1.4k、ir.h）: Intel 固有の決め事は「Mesa が Gen12 でこう lower する」という説明の comment だけ（spirv.c:6281, 8251, 8541, 11204）。lower の形（imod、pow → exp/log、fclamp）は Gen9 でも正しい（Gen9 には native の POW・LRP があるが使わなくてよい）。**変更無し**。
- generator `compile.c`＋`.inc`（Mesa 25.0.7 で Gen9 と Gen12.0 が同じ関数を通る物）:
  - FS の payload: `setup_fs_payload_gfx9()`（`brw_fs_thread_payload.cpp:247-335`）が Gen9〜12 共通。r0、r1（subspan の座標 r1.2–r1.5）、barycentric（SIMD8 で mode 毎 2 register）、source depth、source W、sample mask。
  - pixel centre: `add(16) tmp:UW, r1.4<1,4,0>:UW, 0x11001010:V` → `mov(8) <8,4,1>` は verx10 < 125 で共通（`brw_compile_fs.cpp:355-416`）。
  - LINTERP: Gen9 は PLN を使うが、**係数の layout（.3 = 定数、.1/.0 = 2 つの delta）は ver < 20 で同じ**（`brw_fs_nir.cpp:4422-4456`）ので、今の MUL+ADD の形（`compile.c:2185-2198`）は Gen9 でもそのまま正しい。
  - VS の payload（r0、r1 = URB handle）、CS の payload（r0 だけ、local id は per-thread の push）、push constant の上限 64 register: 同じ。
  - scratch の spill/fill: header の作り方（g0.3[3:0]、g0.5[31:10]、header.2 = offset/16）、DC0・OWord block read/write・BTI 253: 同じ（`brw_reg_allocate.cpp:780-978`）。
  - sampler の descriptor（BTI 7:0、sampler 11:8、type 16:12、SIMD 18:17）: 同じ。LOD 0 の SAMPLE_LZ も同じ。
  - RT write の descriptor（type 12、control 13:8、last RT bit 12）、URB write（opcode 7、per-slot bit 17、channel mask bit 15、header 必須）、untyped/A64 の DC1 の message（type 2・0x12・1・9）: 同じ。
  - integer の除算は SIMD8 だけ・source modifier 無し（Gen9 でも同じ制約）。HF の math は Gen9 でも可（SIMD8 まで）。
- host 試験の道具: `build/mesa-tools/build-asm/.../brw_asm`・`brw_disasm` は `--gen=skl` を受け、native の 16 byte 命令を出す（2026-10-14 に 2 命令で確認）。

### 3.2 書き直す物: encoder の表（Gen9 の 128 bit native、Mesa `brw_eu_inst.h:319-388` の 9+ の列）

| field | Gen9 | Gen12（今の `eu-encoding-gen12.h`） |
| --- | --- | --- |
| opcode | 6:0 | 6:0 |
| access mode | 8（Align1 = 0） | 無し（常に Align1） |
| NoDDClr / NoDDChk | 9 / 10 | 無し |
| nib / qtr ctrl | 11 / 13:12 | 19 / 21:20 |
| thread ctrl（switch/atomic） | 15:14 | atomic は bit 32 |
| pred ctrl / pred inv | 19:16 / 20 | 27:24 / 28 |
| exec size | 23:21 | 18:16 |
| cond modifier = math function = **SFID** | **27:24** | 95:92 |
| acc_wr / branch ctrl | 28 | 33 |
| cmpt / debug | 29 / 30 | 29 / 30 |
| saturate | 31 | 34 |
| flag subreg / reg | 32 / 33 | 22 / 23 |
| NoMask | 34 | 31 |
| dst file / type / subreg / reg / hstride / addr mode | 36:35 / 40:37 / 52:48 / 60:53 / 62:61 / 63 | 50 / 39:36 / 55:51 / 63:56 / 49:48 |
| src0 file / type / subreg / reg / abs / neg / addr / hstride / width / vstride | 42:41 / 46:43 / 68:64 / 76:69 / 77 / 78 / 79 / 81:80 / 84:82 / 88:85 | 66 / 43:40 / 71:67 / 79:72 / 44 / 45 / — / 65:64 / 83:81 / 87:84（＋imm bit 46） |
| src1（同じ順） | 90:89 / 94:91 / 100:96 / 108:101 / 109 / 110 / 111 / 113:112 / 116:114 / 120:117 | 98 / 91:88 / 103:99 / 111:104 / 120 / 121 / — / 97:96 / 115:113 / 119:116（＋imm bit 47） |
| imm32 | 127:96 | 127:96 |
| JIP / UIP | 127:96 / 95:64（byte 単位、同じ）。IF/ENDIF/WHILE は **src0 = imm D 0** を置く（`brw_eu_emit.c:1113-1328`） | 同じ位置、is_imm の bit を立てる |
| SWSB | **無い**（hardware が依存を解く） | 15:8 |
| SEND: SFID / EOT / desc | SFID 27:24、EOT **127**、desc は src1 の imm UD（bit 126:96 = desc 30:0、bit 31 は 0）。mlen 28:25・rlen 24:20・header 19 は desc の中で同じ | SFID 95:92、EOT 34、desc と ex_desc は散らばる |
| SENDS（2 payload、opcode 51/52） | 有る（src1 reg 51:44、ex_desc 95:80 = [31:16]、67:64 = ex_mlen） | 無い（SEND が 2 source） |
| 3 src（MAD） | **Align16 だけ**（Align1 は Gen10 から。`brw_eu_validate.c:2478`） | Align1 |
| register file | ARF 0、GRF 1、IMM 3（同じ） | 同じ |
| type（2 src、register） | UD 0・D 1・UW 2・W 3・UB 4・B 5・DF 6・F 7・UQ 8・Q 9・HF 10（`brw_reg_type.c:72-101`）。immediate: UV 4・VF 5・V 6・HF 11・DF 10 | `brw_reg_type` の enum の値そのまま（今の表: UW 1・UD 2・V 4・D 6・HF 9・F 10） |
| hstride / width / vstride | 0,1,2,4 → 0..3 / 1..16 → 0..4 / 0,1,2,4,8,16,32 → 0..6（同じ） | 同じ |
| opcode の値 | MOV 1、SEL 2、NOT 4、AND 5、OR 6、XOR 7、SHR 8、SHL 9、ASR 12、CMP 16、IF 34、ENDIF 37、WHILE 39、SEND 49、SENDC 50、MATH 56、ADD 64、MUL 65、FRC 67、RNDD 69、RNDE 70、RNDZ 71、MAD 91、NOP 126、WAIT 48 | ALU の < 64 に 96 を足した値（MOV 97 …）、NOP 96、SYNC 1 |
| math function | 27:24（INV 1、LOG 2、EXP 3、SQRT 4、RSQ 5、SIN 6、COS 7、QUOT 12、REM 13: 値は同じ） | 95:92 |
| Gen9 だけの規則 | src0 が 64 bit 未満の immediate なら src1 file = ARF・type = src0 の type を置く（`brw_eu_emit.c:198-203`）。dst の stride は exec type の大きさ / dst type の大きさ、subreg は exec type に揃える。2 register 跨ぎの MATH dst は半分ずつ。packed byte の dst は raw MOV だけ | — |

**設計**: `eu.c` の「意味」の層（`drv_i915_eu_mov/alu2/cmp/select/send/...` の API と region の helper、`i915_eu_buf`）は残し、「bit に置く」層（`i915_eu_common`・`i915_eu_dst/src0/src1`・`i915_eu_send_common`・`i915_eu_set`）を **`struct i915_eu_format`（field の HI/LO・opcode の表・type の表・SWSB の有無・SEND の形）で引く**形にする。Gen12 の表は今の header、Gen9 の表は新しい `intel/eu-encoding-gen9.h`（Mesa `brw_eu_inst.h`・`brw_eu.c`・`brw_reg_type.c`・`brw_eu_defines.h` の 9+ の列を出典に）。compaction は Gen12 と同じく使わない。SWSB は format に無ければ byte を置かず `in_order` も数えない（`sync.nop` の呼び出しは no-op に）。

選ばなかった案: (i) `eu.c` を丸ごと複製して `eu-gen9.c` にする — 2 本の encoder を同じ API で保つ手間が毎回倍になる。(ii) Mesa の encoder の構成（`brw_eu_inst.h` の accessor）を写す — license は MIT で可能だが規約（Zlib の書き直し、値だけ転記）に反する。

### 3.3 scoreboard

Gen9 には SWSB が無く、hardware の scoreboard が register の依存を解く（Mesa は `ver >= 12` だけ `brw_lower_scoreboard` を走らせる、`brw_lower_scoreboard.cpp:1367`）。今の encoder の「常に 1 token、何も飛ばさない」保守的な SWSB（`eu.c:18-73`）に相当する物は Gen9 では**不要**。NoDDClr/NoDDChk は最適化で、使わない。残る Gen9 の hazard（Mesa `brw_eu_validate.c`、`brw_generator.cpp:786-808`）で今の generator に当たる物: 無し（POW・CSEL・mixed float・64 bit・byte の src1 は使わない）。

### 3.4 message と generator で直す 6 点

| 何 | Gen12（今） | Gen9 | 出典 |
| --- | --- | --- | --- |
| compute の終了 | thread spawner（SFID 7）に r0 の 1 register、desc 0、EOT | 同じ ＋ **desc の bit 4**（"do not dereference URB"、ver < 11） | `brw_fs_visitor.cpp:352-392` |
| barrier | gateway（SFID 3、subfunction 4）の send → `sync.bar` | gateway の send → **`wait n0`**（opcode 48、ARF 0x90、exec 1、NoMask）。payload.2 の mask は **0x8f000000**（Gen11+ は 0x7f000000） | `brw_generator.cpp:528-538`、`brw_fs_nir.cpp:3061-3069` |
| memory fence | global と SLM（BTI 254）の 2 本、commit、stall、acquire に `sync.allwr` | **DC0 の fence 1 本、BTI 0、commit 有り（ver == 9 は simulation の都合で commit が要る）**、SLM 専用の fence 無し、`sync` 無し | `brw_fs_nir.cpp:6043-6054`、`brw_eu_emit.c:1691-1721` |
| sampler の header（offset・bias・lod を使う message） | header を 0 で作り dword 3 に `r0.3 & ~0x1f`（Gen11+） | **header = r0 の複写**、dword 2 に offset（ver < 11） | `brw_lower_logical_sends.cpp:813-882` |
| RT write の header（RT 1 つ以上、dual source） | RT index・src0 alpha は ex_desc | **2 register の header（g0, g1）**: g0.0 bit 11 = src0 alpha、header.2 = RT index。RT 1 つ・単一 source は header 無しの plain SENDC で同じ | `brw_lower_logical_sends.cpp:311-374, 484-511` |
| 2 payload の SEND（今の RT write・scratch の spill は ex_desc の 2 本目の payload を使う） | SEND の src0 と src1 | **payload を GRF で連続に並べて 1 本の SEND（mlen = header + data）**にする。SENDS（opcode 51）は出さない（命令を 1 つ増やさずに済むが、符号化と検査の表が 1 本増える） | `brw_eu_emit.c:1472-1531`（Mesa は SENDS を出す） |

今の generator に無い物で Gen9 で**新しく要らない**物: PLN（MUL+ADD で足りる）、LRP、POW、Align16 の MAD（呼び手無し）。SIMD16 は pixel centre の `add(16)` UW だけで、Gen9 でも同じ形で可。

### 3.5 検査の方法（host）

- `plan/ws101/tests/host/run.sh`・`plan/ws075/tests/guard/run.sh`・`plan/ws031/tests/run-vk-gentool-test.sh` は `brw_disasm --gen=adl` で byte の往復を見る。Gen9 の format は `--gen=skl` で同じ往復をする。disassembler が compact 命令を出す場合に備え、往復の比較は native だけにする（brw_asm は既定で native を出すことを 2026-10-14 に確認）。
- `src/drivers/gpu/i915/tests/execution/eu-test.c`（GT で 1 thread の A64 store）は Gen9 の kernel の byte を Gen9 の encoder で作る形に広げる（実機で走らせるのは p005）。

## 4. GOP の framebuffer のままで GT を起こす（範囲 3）

### 4.1 今の経路

1. `i915_start_prepare` → `i915_start_registers`（BAR0、forcewake の表、fuse、MCR、`drv_i915_gt_init_mmio`、**GDRST full reset**）→ `i915_start_memory`（`drv_i915_display_create`: display の世界の確保だけ、hardware には触れない）→ `i915_start_ggtt`（GGMS、table の map、scratch。**table は埋めない**）→ `i915_start_aperture` → `i915_start_interrupts` → `i915_start_display_noirq`（N0 の precheck → STOP なら `absent`）→ `i915_start_gt`（表、GT memory（GGTT の**上端 64 MiB**だけ）、engines、resume、defaults、verify、migrate）→ `i915_start_display_nogem`/`_register`（absent なら何もしない）→ `i915_start_publish`。
2. N0（`takeover.c:2725-2822`）: pipe の電源が入っている pipe の TRANSCONF・PLANE_CTL・PLANE_SURF を読み、**firmware の scanout が driver の GGTT の窓（`driver_ggtt_first` 以上）と重なれば STOP**、VT-d の状態で STOP、lit の pipe があれば takeover（段階 (2) の仕事）。STOP の時 `display->absent = 1` で、**display の register も GGTT の PTE も D-state も触らない**（`display.c:1858` の log に明記）。
3. docs の規則（`docs/architecture/kernel-and-hal.md:75-92`）: GPU driver は load の時 firmware の絵をそのままにし、display は client が lease を取った時だけ取る。`/dev/graphics` の console は firmware の framebuffer に描き続ける。

### 4.2 Gen9 の段階 (1) の口

- `display_ver == 9` の device は display の世界を作った直後に **N0-lite** を走らせ、結果に関わらず `absent = 1` にする（takeover も modeset もしない）。N0-lite は Gen9 の電源の事実で pipe の可読性を決める: HSW_PWR_WELL_CTL2 0x45404 の STATE bit（SKL: PG1 bit 28 → pipe A と eDP の transcoder、PG2 bit 30 → pipe B・C。p002 の最初に Linux `intel_display_power_map.c` の skl の表と SKL PRM Vol.12 で確かめる）。読むのは TRANSCONF・PLANE_CTL_1・PLANE_SURF_1・PLANE_STRIDE/SIZE（番地は Gen9 でも同じ: 0x70008・0x70180・0x7019c）。目的は**GGTT の重なりの検査と log**だけ。
- GT の起動は display に触れない事を Gen9 でも保つ: GDRST の bit 0（GEN6_GRDOM_FULL）は GT の domain だけ（Linux は display が動いたままの hang recovery で使う）。RC6/RPS は GT。GGTT の PTE は上端の窓だけ。display の割込み（GEN8_DE_*、SDE）は mask のまま（GEN8_MASTER_IRQ の display の bit を enable しない）。DMC は読まない。
- `i915.display=off`（仮の語、boot の parser の key は p002 で決める。`include/kern/boot.h` に I915 の key は START・DEBUG の 2 つ）で N0-lite も飛ばして常に absent にする口を足す（実機の初回の bring-up で display の register の読みすら避けるため）。

### 4.3 未決（Q1 → ユーザー）

- **D1** 段階 (1) の受け入れを「Vulkan の test（vkx・vke1・vke2・vkc・vkcs）が実機の GT で PASS（読み戻し）」にしてよいか。画面に出すのは段階 (2) か D3。
- **D2** 試験の機械（8 世代の Core の機械の有無。VT-d で iGPU を QEMU に passthrough する 5330 と同じ形が取れるか、それとも USB boot の実機か）。
- **D3** display の無い GPU node で Keiland を動かすか（compositor が GPU で描いて CPU で `/dev/graphics` の firmware framebuffer に複写する経路。`I915_TEST_CAPTURE` の capture display（`display/capture.c`、present を guest RAM に落とす）が近いが試験用）。段階 (2) を先にするなら不要。

## 5. Phase の分け方（3 人が別々の file を持つ単位）と受け入れ

**版の選択の口（p002〜p004 が共有する最小の契約、最初に Q1 が main に入れる）**: `struct i915_gt` に `unsigned graphics_ver`（9 か 12）を足し、`device.c` の `12` の定数（`device.c:1229, 1641`）をそれに置き換える。`drv_i915_shader_compile*()` には `struct drv_gpu_shader_ir` を変えずに **`int graphics_ver` の引数**を足す（呼び手は `render/pipeline*.c`）。この 2 つの差分は小さいので **p002 の最初の commit** にし、Q1 が merge してから p003・p004 が base にする（それまで p003・p004 は自分の file だけを変え、引数は仮に 12 で呼ぶ）。

| Phase | 担当 | 所有する file（他の担当は触らない） | 内容 | 受け入れ（host で確かめられる範囲） | LW |
| --- | --- | --- | --- | --- | --- |
| **p002 GT の起動（Gen9）** | P2 | `device.c`・`device-info.[ch]`・`gt.h`・`mmio.[ch]`・`intel/forcewake-ranges-gen9.inc`（新）・`intel/engine-table-gen9.inc`（新）・`intel/pci-ids.h`・`i915.c`・`context.c`・`intel/lrc-offsets.h`（Gen9 の表を足す）・`submit.[ch]`・`reset.c`・`workarounds.[ch]`・`intel/workarounds.h`・`intel/mocs.h`・`intel/mocs-table-gen9.inc`（新）・`ppgtt.c`・`ggtt.c`・`tlb.c`・`irq.[ch]`・`gt-power.c`・`engine.c`・`intel/gt-regs.h`（Gen9 の register を足す）・`display/takeover.c` の N0-lite（1 関数）・`tests/contracts/*`・`tests/execution/ktest-gt.c` | §2 の表の「GT」の行を全部: PCI ID 86 個と platform/stepping、`graphics_ver`/`display_ver` 9、forcewake の MEDIA domain と `__gen9_fw_ranges`、SSEU/clock/MCR、engine の表、context（22 page、`gen9_rcs/xcs_offsets`、slot、CONTEXT_CONTROL、indirect ctx の wa batch）、ELSP と CSB 6、reset の domain、workaround（GT/ctx/engine/whitelist）、engine 毎の MOCS、PAT 0x40e0、gtt write workaround、GGTT invalidate、TLB の register、割込み（GEN8_MASTER_IRQ と bank）、gt-power（第 1 版: RC6 off・PG 0・RPS は RPe の固定要求）、N0-lite と `display->absent`、HEVC/SFC の capability を落とす | `sh src/drivers/gpu/i915/tests/contracts/run.sh` PASS、ktest-gt の register model に Gen9 の表（ELSP の 4 dword の順、CSB 6 の wrap、context image の slot、MOCS の番地、forcewake の domain の引き）を足して PASS、Gen12 の model は変化無し、`make $(BUILD)/vmunix` warning 0。実機は p005 | 2.5 |
| **p003 EU の符号化（Gen9）** | P1 | `compiler/eu.[ch]`・`intel/eu-encoding-gen9.h`（新）・`intel/eu-encoding-gen12.h`（format の表に組み替える分だけ）・`compiler/compile.c`・`compiler/compile-compute.inc`・`compiler/compile-geometry.inc`・`compiler/compiler.h`・`tests/execution/eu-test.c`・`tests/render/generality-shaders/`・`plan/ws101/tests/host/`・`plan/ws075/tests/guard/`・`plan/ws031/tests/run-vk-gentool-test.sh` | §3.2 の `struct i915_eu_format` と Gen9 の表、§3.4 の 6 点、`drv_i915_shader_compile*()` の `graphics_ver` | 既存の host の shader 試験（`plan/ws101/tests/host/run.sh`・`glsl/run.sh`、`plan/ws075/tests/guard/run.sh`、`plan/ws031/tests/run-vk-gentool-test.sh`、`tests/render/generality-shaders` の .spv）を **`--gen=skl` でも**走らせ、Gen9 の byte が Mesa の `brw_disasm --gen=skl` → `brw_asm --gen=skl` の往復で一致、拒否すべき shader は同じく拒否。Gen12 の往復は変化無し。IR の interpreter の結果（ws101 host）は不変。build warning 0 | 2.0 |
| **p004 3D と compute の state（Gen9）** | P3 | `intel/genxml.h`（Gen9 の節）または `intel/genxml-gen9.h`（新）・`render/state.[ch]`・`render/compute.c`・`render/draw.c`・`render/command.c`・`render/pipeline*.c`・`render/descriptor.c`・`render/image.c`・`render/blit.c`・`render/gfx.h`・`intel/commands.h`・`plan/ws083/tests/genxml-decode.py`（gen90 の継承）・`plan/ws101/tests/host/genxml-check.py` | §2.1 の差: STATE_BASE_ADDRESS 19、DEPTH/STENCIL/HIER の field、SAMPLER_STATE の border pointer、MOCS の値（isl の Gen9: UC 0、cached 2<<1）、PIPE_CONTROL の bit の選別（HDC flush・tile cache・L3 fabric を出さない）、CPS_POINTERS/PRIMITIVE_REPLICATION/CONSTANT_ALL を出さない、VFE/MEDIA_STATE_FLUSH の Gen9 の bit、**L3CNTLREG 0x7034 の分割（SLM 有り）**、URB の上限（VS max 1856 など、GT1 の例外）、thread 数（PS 64/slice、CS 56）、surface state の Gen12 だけの bit を立てない。`render/*.c` の `GEN12_` の直書きを `graphics_ver` で引く表に | ws083 の genxml の decoder を gen90.xml（→ gen80 → gen75 の継承）で解決し、p004 が出す batch と dynamic state（state の builder を host で呼ぶ既存の `plan/ws101/tests/host/genxml-check.py` の形）が gen90.xml の field に decode できる。Gen12 の decode は変化無し。build warning 0 | 2.0 |
| **p005 Vulkan の通しと実機の bring-up** | 空いた担当 | 統合。`tests/execution/*`、`plan/ws229/tests/` | 実機（D2）で eu-test → vkcs → vkx・vke1・vke2・vkc → generality。hang の解析と修正の buffer。RC6/RPS を Linux の Gen9 の形に進めるかの判断 | 実機で上が PASS（読み戻し）。回帰: 5330（Gen12）で同じ試験が PASS のまま（T1）。**実機が無ければ host の受け入れ（p002〜p004）までで WS を区切り、p005 は uncleared のまま残す** | 1.5 |
| p006 display controller（Gen9） | — | `display/*` | 今日の範囲外（SKL の電源・CDCLK・DPLL・DDI の表・watermark・PCH・DMC・plane） | 実機で modeset | 6.0 |
| p007 規約の全文の見直し | — | WS が変えた C | conformance-reviewer | — | 0.5 |

衝突の回避: p002 と p003 は file が重ならない。p004 と p002 は `intel/commands.h`（p004）と `intel/gt-regs.h`（p002）で分かれる。`device.c` は p002 だけ。`render/pipeline*.c` の compile の呼び手（`graphics_ver` の引数）は p004 の所有なので、p003 は `compiler.h` の宣言を変えたら Q1 経由で p004 に 1 行の差分を頼む（または p003 が `drv_i915_shader_compile_gen()` を新しい名前で足し、古い名前を Gen12 の wrapper として残して呼び手を触らない。**後者を取る**）。

### 5.3 試験の方法

- **host**: contracts（mock の mmio/pci/dma）、ktest-gt の register model に Gen9 の表、EU の byte の往復（brw_asm/disasm `--gen=skl`）、genxml の decode（gen90.xml）。全部 QEMU 無し。
- **QEMU**: Intel の GT を模倣する device は無い（標準 VGA の framebuffer の boot-test だけ）。**回帰（Gen12 の経路を壊さない）**は host の試験と、5330 の実機（T1 の `plan/ws101/tests/hw/run-hw.sh`・`plan/ws075/tests/test-hw.sh`）。
- **実機（Gen9）**: 8 世代（または 6・7・9・10 世代）の Core の機械が要る。5330 と同じ VT-d の passthrough（`plan/tools/hw5330/`）が使えれば gdbstub・QMP の道具がそのまま使える。QEMU の IGD passthrough は Gen9 で legacy mode（OpRegion・stolen）が要る可能性があり、USB boot の native の方が確実（serial の log は判定に使わず、画面の PNG と SSH。display 無しの段階 (1) では読み戻しの結果を SSH で取る）。Q1 がユーザーに機械の有無を聞く（D2）。

## 6. 対応できる Core の世代（範囲 6）

| 世代（Core） | code name | graphics / display | Linux の platform | PCI ID の数（6.8.12） | GT の仕事 | display の仕事 |
| --- | --- | --- | --- | --- | --- | --- |
| 6 | Skylake | 9 / 9 | SKL | 25（GT1 8・GT2 7・GT3 6・GT4 4） | この WS | p006 |
| 7 | Kaby Lake（Amber Lake-Y も） | 9 / 9 | KBL | 22 | 同上（workaround の差: KBL < C0 の 0x4ab8、ctx の 0x7014 bit 8・0xe100、A0–F0 の semaphore poll） | p006（PCH KBP） |
| 8・9 | Coffee Lake、Whiskey Lake、Amber Lake | 9.5 / 9 | CFL（WHL/AML は CFL の info） | 22 | 同上（ctx の 2 個は無条件、whitelist に 0x2348） | p006（PCH CNP） |
| 10 | Comet Lake | 9.5 / 9 | CML | 17 | 同上（CTX_TIMESTAMP の whitelist） | p006（PCH CMP） |
| 10（Ice Lake） | Ice Lake | 11 / 11 | ICL | 15 | **別の仕事（2〜3 LW）**: GT は Gen11（ELSQ・CSB 12・`__gen11_fw_ranges`・GEN11 の割込み・global MOCS 無し（engine MOCS）・icl の workaround）、EU は Gen9 の符号化（SWSB 無し）だが message は Gen11 の形（今の sampler header・RT write の ex_desc）。genxml は gen110（Gen12 と近い） | display 11（Combo PHY・TC の初代、別物） |
| 11（desktop） | Rocket Lake | 12 / 12 | RKL | 6 | **小（0.3 LW）**: ID と「VCS2 無し」。GT は TGL と同じ | display 12 だが Type-C 無し・DDI の割付が違う（0.5〜1 LW） |
| 12（desktop） | Alder Lake-S | 12 / 12（XE_D） | ADL-S | 8 | **小（0.3 LW）**: ID。GT は ADL-P と同じ（MOCS は gen12 の表） | display は TGL 系の 12（ADL-P の 13 ではない）: 今の `display_ver >= 13` の分岐が TGL 側に倒れる。PCH ADP-S（0.5〜1 LW） |
| 13・14（desktop） | Raptor Lake-S | 12 / 12 | RPL-S（adl_s_info） | 8 | 同上 | 同上 |

Gen9 の **PCI ID の合計 86**（SKL 25 ＋ KBL 22 ＋ CFL 39）。Gen9 の GT の段（GT1 1×2×6 EU、GT2 1×3×8、GT3 2×3×8 ＋ VCS1、GT4 3×3×8 ＋ VCS1）は fuse から読める（SSEU）ので、表は engine mask（VCS1 の有無）と stepping だけでよい。SKL PRM Vol.4 の SKU 表（IHD-OS-SKL-Vol 4-05.16 p.223 以降、ID と GT の対応）と Linux の表は一致する（2026-10-14、SKL の 0x1902/1906/190B/1912/1916/191E/1923/1926/1927/192D/193B/193D を照合）。

## 7. 選ばなかった案と理由

| 案 | 理由 |
| --- | --- |
| Gen9 に GuC を使う | Linux も Gen9 は既定で GuC off。submission は Gen11+ だけ。GuC はベータ3 の WS230（Q1、2026-10-14） |
| Gen9 の第 1 版から RC6・RPS を Linux と同じにする | Gen9 の RC6 には context 破壊の workaround（park の時だけ手で入れる）が要り、GT3/4 は PG を使えない。段階 (1) は「動く」が先で、電力は p005 の判断に回す |
| compact 命令・SENDS・PLN・Align16 MAD を実装する | 今の generator が使わない、または MUL+ADD で足りる。encoder の表が増えるだけ |
| display を Gen9 で最初から（段階 (2) を先に） | ユーザーの指示（まず GOP のまま、最後に display）。display は 70k 行の中の版分岐で、GT より大きく、GT が動かないと確かめにくい |
| Gen9 の display を今の TGL/XE_LPD の code と別の driver にする | watermark・plane・DDI・DP・hotplug は Linux でも同じ file（`skl_*`、`intel_ddi.c`）の版分岐で、今の code がそれを移植した物。別 driver は重複が大きい |
| Ice Lake をこの WS に含める | GT が Gen11 で Gen9 とも Gen12 とも違う第 3 の形。EU は Gen9 の encoder が使えるので、p003 の後に別 WS で安い |
| `eu.c` を複製して Gen9 専用の encoder にする | API を 2 本で保つ手間。format の表で引く方が差分が小さい（§3.2） |

## 8. 敵対的レビュー（P1 の自己レビュー）

1. **「generator はそのまま」の根拠は Mesa の同じ関数を通ることだが、zedBSD の generator は Mesa の写しではない。** → Gen12 で実機の試験が通っている form（payload・descriptor）と Mesa の Gen9/12 共通性を合わせた判断。p003 の受け入れは Mesa の disassembler の往復だけで、**message の意味は実機でしか確かめられない**（p005 に残る）。
2. **sampler の header の差**: 今の generator は offset/bias/lod の message で Gen11+ の header（0 で作り dword 3）を使う。Gen9 で r0 の複写にし忘れると sampler state pointer が壊れて絵が出ない。§3.4 に入れた。
3. **2 payload の SEND を 1 payload にする案**は register の割付（header と data が連続する GRF）を generator に要求する。scratch の spill は header を毎回作っているので隣に data を置ける。RT write は data が 4 register（SIMD8 の RGBA）なので header 2 register を前に置く形に変える。register の圧迫は小さい（6 register）。
4. **context の wa batch（gen9_init_indirectctx_bb）**は Gen12 の per-context batch と違い engine 共通の 1 page。今の `context.c` は `wa_bb_page` を context 毎に持つので、Gen9 では「全 context が同じ内容の page を持つ」形でも動く（Linux は engine の 1 page を共有）。簡単な方（context 毎）を取る。
5. **GGTT の invalidate**: Gen9 は GSM を WC で map し `GFX_FLSH_CNTL_GEN6` を書く。今の driver は uncached で map する。uncached なら write は順序通りに出るが、GT 側の TLB に古い PTE が残る可能性は残る。p002 で `GFX_FLSH_CNTL_GEN6` の書きを**入れる**（害が無い）。
6. **N0-lite の電源の bit** は Linux の `intel_display_power_map.c`（skl の well の表）で確かめる前に書いた推定（PG1 bit 28、PG2 bit 30）。p002 の最初に確かめる。
7. **Vulkan の userland**は device の世代を見ない（`include/uapi/gpu.h` の capability は device_id と opcode の集合）。Gen9 で落ちる機能（video）は capability の bit で消せる。16 bit の整数・HF は Gen9 でも可。**64 bit float/int は Gen9 に有る**が今の generator が出さないので影響無し。
8. **Gen9 の BCS と command parser**: zedBSD は user の batch を受けないので無関係だが、将来 user の batch を受ける設計に変える時は Gen9 の BCS に Linux と同じ制限が要る（記録）。
9. **見積もりの不確かさ**: 実機が無いと段階 (1) は閉じない。実機が 5330 の様に passthrough できない場合、boot と log の取り方（SSH、PNG）の道具作りが 0.5〜1 LW 増える。
10. **3 人の並行**: p002〜p004 の host の受け入れは互いに独立だが、**本当に動くかは 3 つが揃って実機で初めて分かる**。code freeze に「Gen9 の経路が入っているが未検証」で入れるなら、Gen12 の経路の回帰（host の試験と 5330）だけが入れる条件。

## 9. HAL の API の変更

要らない（i915 は `drv_pci_*`・`drv_dma_*` の既存の口だけを使う。`include/hal/hal.h` に触れない）。

## 10. 実行の記録

- `git -C /home/awe/zedBSD-worktrees/p1 merge --ff-only main` → b05b2ccfe。
- 取得: `curl` で linux-6.8.12.tar.xz（142.6 MB、sha256 一致）、sha256sums.asc、PRM の PDF 15 本（kiwitree、`prm/SHA256SUMS`）。`tar -xJf … linux-6.8.12/drivers/gpu/drm/i915 include/drm COPYING LICENSES`。`pdftotext -layout` で `prm/txt/`（130k 行）。
- Mesa: worktree の既存の `build/mesa-tools/mesa-25.0.7/`（tarball の sha256 `592272df…`）。`brw_asm --gen=skl` / `brw_disasm --gen=skl` が動くことを 2 命令で確認。
- 読んだ zedBSD の source: `device.c`・`device-info.[ch]`・`gt.h`・`mmio.c`・`intel/forcewake-ranges.inc`・`submit.c`・`context.c`・`intel/lrc-offsets.h`・`engine.c`・`workarounds.c`・`intel/mocs.h`・`ppgtt.c`・`ggtt.c`・`memory.h`・`tlb.c`・`irq.c`・`reset.c`・`gt-power.c`・`firmware.c`・`pci.c`・`i915.c`・`intel/pci-ids.h`・`compiler/eu.[ch]`・`compiler/compiler.h`・`compiler/compile.c`（抜粋）・`intel/eu-encoding-gen12.h`・`intel/commands.h`・`intel/genxml.h`（抜粋）・`render/state.c`（抜粋）・`render/compute.c`・`render/blit.c`・`display/display.c`（抜粋）・`display/takeover.c`（N0 の節）・`tests/`（一覧）・`docs/architecture/kernel-and-hal.md`・`plan/ws031/i915-rebuild-rules.md`。
- Linux と Mesa の差分の調べは Explore の subagent 2 本（read-only）に並行させ、要点（RING_ELSP 0x230、CSB 6、render context 22 page、`gen8_csb_parse`、engine MOCS、Align1 3-src の禁止、co-issue の注記、barrier の WAIT、EOT bit 127、gen60.xml の XY_* の exclude、`nir_lower_interpolation` の ver >= 11）を自分で grep して確かめた。
- 作った物: この文書だけ。build・QEMU・実機は無し（設計の Phase）。削除した物は無し。`build/ws229-p001/`（約 1.1 GB: tarball 143 MB、展開 ~200 MB、PDF 52 MB、txt）は Q1 が消してよい使い捨て（Linux の tarball は再取得 1 分）。p002〜p004 の担当は同じ資料を使うので、Q1 の判断で残してもよい（読み取り専用の複写か symlink で）。

## 11. 残課題（Q1 へ）

- design-reviewer を通す（Q1）。
- D1〜D3（§4.3）をユーザーに。特に D2（Gen9 の機械）が無いと p005 は始められない。
- p002〜p007 を WS229 の Phase の表に足す（Q1。§5 の形）。§5 の「版の選択の口」（`gt->graphics_ver` と compile の `graphics_ver`）を p002 の最初の commit として先に main に入れる段取り。

## ユーザーの決定（2026-10-14 クリック）

- D1: 段階 (1) の受け入れは「実機の GT で Vulkan の試験の読み戻しが PASS」（推しどおり）。画面は GOP の console のまま。
- D2: 実機は 6・7・8 世代の Core i5 の laptop（ユーザー「全部Laptopです。」）を USB で直に起動（推しどおり）。結果は SSH か画面の写真。
- D3: display の無い node で Keiland を動かす経路は作らない（D1 の決定で不要）。

- 2026-10-14 Q1: §5 の所有の追記（P2 の報告）: `request.c`（PIPE_CONTROL・MI_ARB_CHECK・AUX の invalidate・MI_FLUSH_DW_CCS の Gen12 だけの命令）・`verify-workarounds.c`（MCR の range の表）・`defaults.c`（Gen9 の null render state、第 1 版は流さず残件）は p002 の所有。`intel/commands.h` は p004 の所有のまま。

## review-1 と Q1 の判断（2026-10-14）

[review-1](review-1.md)（致命的 F1・F2、重大 M1〜M10、中 1〜8、軽微）。Q1 の範囲の判断:
- F1（request.c）は p002 の所有（既に P2 に了承済み）。F2: Gen9 では SENDS・SENDSC を出す（§3.4 の「1 payload に並べ直す」と §7 の SENDS を退けた理由は撤回）。見積もりの p003 は見直す。
- 所有の追記: p002 に `display/display.c`（native check の配線・noirq_front・display irq の bind の Gen9 の分岐だけ）・`include/kern/boot.h`（Gen9 の boot の key）・`worker.c`。p004 に `render/batch.c`・`render/heap.h`・`render/fence.c`・`render/vulkan.c`・`render/instance.c`・`tests/fixtures/draw-fixture.c`。`plan/ws101/tests/host/genxml-check.py` は p004、それ以外の `plan/ws101/tests/host/` は p003。
- 他の WS の plan/tests（ws101・ws075・ws031・ws083）は、Gen9 の選択肢を足すだけ（Gen12 の振る舞いと期待を変えない）の編集を Q1 が許可する。
- 受け入れの直し（M6）: p003 は Gen9 の期待の text を brw_asm で byte にして zedBSD の byte と比べる試験と、Gen12 の byte の前後の同一。p004 は Gen12 の batch の byte の前後の同一。p002 は Gen12 の起動の mmio の書き込み列の golden（mock）か、5330 の T1 の回帰。ktest-gt は host で走らないので p005 へ。
- U1〜U3 はユーザーに聞く。

## ユーザーの決定（2026-10-14 クリック、review-1 の U1〜U3）

- U1:「実機で PASS したら既定で有効」→ 実装は boot の指定（例 `i915.gen9=on`）でも有効にできる口を持ち、p005 の実機の試験が PASS したら既定で Gen9 に attach する（PASS までは既定 off）。
- U2:「Gen9 の間は suspend を拒む」（推しどおり）→ display が absent の Gen9 では suspend を断るか D0 を保つ。display controller の段階（p006）で外す。
- U3:「5330 の回帰を必須」（推しどおり）→ ベータ2 に入れる条件は、5330（Gen12）の実機で T1 の Vulkan の回帰が PASS。通らなければ Gen9 はドロップ。
