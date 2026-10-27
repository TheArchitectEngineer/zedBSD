# WS290 の設計: zedBSD の GHCI の driver を GHCI 2.0 に書き直す

- Phase: [ws290-p001](phase001/phase.md)（Queue q1594 / q1594-i01、担当 P1）。版 1: 2026-10-26（2721c49fc）。版 2: 2026-10-26、design-reviewer の 28 件（[design-review.md](design-review.md)）を反映（§23）。**版 3: 2026-10-27（q1621-i01、担当 P3）、ghci.md ce9725a の更新とユーザーの補足を反映（§0.1、§24、§25）**。
- 親: [WS290](ws.md)。前身の設計: [WS288 design](../ws288/design.md) §6（1.1 の driver）。
- この文書は設計だけ。source は変えていない。

## 0.1 版 3 の改訂（2026-10-27）

> **中断（2026-10-27、版 3 は書きかけ）**: ユーザーの G01 の回答で版の前提が変わり、ユーザーは「そのほかは仕様を調整中です。いったん作業を終了していいです。」と伝えた。版 3 の本文（§0.1・§24・G40〜G49 など）は ghci.md ce9725a を読んだ**書きかけ**で、G01 の回答と design-reviewer の指摘 V01〜V25（[design-review-v3.md](design-review-v3.md)）は**まだ本文に反映していない**。再開の条件と手順は [phase001/phase.md](phase001/phase.md) の「再開」。

**G01 の回答（2026-10-27 ユーザー、原文）**: 「G01: GHCIはまだ策定中のドラフトで、誤ってAIハルシネーションによって2.0にされてしまいました。1.0が正しく、それ以外のバージョンは存在しません。1.0以外は予定すらされていません。2.0は誤りなので、ドライバでは2.0を考慮してはいけません。すべて単一のGHCI 1.0です。GHCIは1.0のあとはcapabilityが追加されていく予定で、決められた1.1の必須機能とかはない予定です。1.1で追加されたCAPがあって、そのCAPに対応するかはハードウェア実装次第、という形になっていきます。」「そのほかは仕様を調整中です。いったん作業を終了していいです。」

**解釈（Q1、2026-10-27）**: driver は単一の GHCI 1.0 だけを扱う。「2.0」「1.1 legacy」の区別・版による分岐を設計から外す（版の値は 1.0 だけを受け、機能の有無は版でなく capability の flag だけで判断。将来の minor で足される CAP も flag で見る）。

**この回答で版 3 の書きかけから直す事（再開の時）**: (1) 文書の「2.0」「TO-BE（旧称 2.0）」の語を「GHCI 1.0」に、WS の名前の扱いは Q1 と決める。(2) §0.1・§5.1 の 2・§2 の §16 の行の「minor ≥ 1 を拒む」を捨て、major 1 を受けて機能は flag だけで見る形に（将来の minor の CAP も flag）。(3) G40 は G01 の回答で閉じる（版の矛盾ではなく、1.1 の register の窓 §3.1.1 は仕様の誤りか調整の対象として ghci.md の修正を待つ）。(4) §3.1.1・§7.5 の「1.1」の記述を「legacy」と呼ぶ前提を、修正後の ghci.md で読み直す。

**読んだ物**: amaterrace-gpu の `git diff 3af62ca ce9725a -- docs/ include/`（docs/ghci.md +178 行、include/ghci_regs.h・ghci2_regs.h・ghci_cmd.h・ghci_frame.h）。読んだ時点の main は **ce9725a**（2026-10-27 13:56）。docs/・include/ の最後の差は **f718918**（13:15）で、f718918..ce9725a は `config/minimal.mk`・`plan/ws003/minimal_fit_report.md`・`tools/vivado-synth-l2.tcl` の 3 file だけ（docs/・include/ の差は無い）。参考に `plan/ws003/`（am-ws003: capability と config.mk）、`plan/ws004/`（am-ws004: task・mesh shader）、`config.mk.template`、`rtl/gpu/cp/atg_cp_regs.v`（capability の bit の組み方）を `git show` で読んだ。amaterrace-gpu には書いていない。

**ユーザーの補足**（2026-10-27、Q1 経由の原文）: 「コマンドベースの通信に変化して、レジスタ群がばっさりなくなりました。また、capabilityが導入され、capフラグがあるときだけ使える機能がたくさんできました。Vulkanのextなどとして見せるかどうかにマッピングが必要です。」

**版の名前**: ghci.md ce9725a は TO-BE を「**GHCI 1.0**」（GHCI_VERSION = 0x00010000）と呼び直し、§3.1.1 の legacy を「1.1」（0x00010001）と呼ぶ。この文書の「2.0」は「**TO-BE（ghci.md の GHCI 1.0、旧称 2.0）**」と読む（WS の名前とユーザーの決定「2.0 だけ」はこの TO-BE を指す。1.1 legacy は捨てる対象のまま）。行の番号は版 2 までの節では 4d12da1、§0.1・§24・G40 以降では ce9725a の物。

**変わった事と設計の扱い**:

| 変わった事（ce9725a） | 設計の扱い（版 3） |
| --- | --- |
| VERSION が 0x00010000（§3.1 181 行、§14 1986 行）。legacy 1.1 は 0x00010001（207 行）で major が同じ。§16 は「minor は register を足すだけ」 | identify は **0x00010000 だけ**を受け、minor ≥ 1 は「1.1 legacy は対象外」で拒む（§5.1）。版の付け方の矛盾は G40 |
| DEVICE_CAPS に [15:8] PHYS_LANES（4・8・16・32、旧 SIMD_LANES 32 固定）、[24] PROT、[25] TEX_FFILT、[26] ROP_PACK、[30] TASK_SHADER、[31] MESH_SHADER（§3.2.1 249〜270 行） | **capability を driver の一つの型 `struct ghci_caps` に読み、Vulkan への対応を一つの表で決める**（§24）。flag の有る・無いの両方を host 試験する |
| CAPS（0x020）と CAPS2（0x024）の表（§3.2.2・§3.2.3 272〜313 行）。ただし 19 register の表（§3.1）で 0x020・0x024 は RING_BASE_LO/HI | register としては読めない（G41）。推し: **capability を command で読む**（`CMD_QUERY_CAPS` が capability の block を memory に書く、§24.2）。block が無い GPU は DEVICE_CAPS だけで動く |
| include と RTL の slim（`SLIM_MMIO`）は EXT_CAP_FMT0（0x080、BC1〜BC7）と EXT_CAP_FEATURE（0x084）を持つが ghci.md に無い。RTL の slim は EXTCAP_PTR に 0 を返す | FMT0・FEATURE は G42、slim で EXTCAP（MEM・FMT・DISP2）が無い事は G43。**driver は EXTCAP に頼らない**: format は capability の flag と driver の表から（§24.4）、MEM は block から、DISP2 は見つかった時だけ（§13） |
| FMT の注に TEX_FFILT・ROP_PACK の時の format feature の規則（§4 407〜418 行）、§12.9・§12.12 に同じ規則 | §24.4 の format の表に入れる |
| PROT が build で外せる（`CONFIG_MC_ENABLE_PROT`、DEVICE_CAPS[24]、§11.6 1092〜1100 行） | WS292 の範囲（U5）。WS290 は DEVICE_CAPS[24] を記録するだけ |
| §11.5.1 cache と内部 buffer の大きさ（CAPS2 で報告） | Vulkan には見せない（log だけ、§24.3） |
| include だけに `CMD_DRAW_MESH_TASKS`（0x2A、4 dw）と `GHCI_VK_CMD_DRAW_MESH_TASKS` | G44。Vulkan 1.0 の header に無いので WS226 まで使わない |
| ring・fence・IRQ・fault・VM_MAP・PSO・descriptor・§7.2 の command の表 | **差なし**。G03〜G08・G10〜G16・G20〜G27・G30〜G39 は未回答のまま |
| §13.2 DISP2 | 差なし。G28 はユーザーが決めた（「G28は決まったのでは」、§13.2 に従う）。残る事実の欠け（DISP_SET・FLIP の TO-BE の opcode、IRQ の bit）は G49 |

**版 2 から捨てた register の前提**（ユーザーの補足「レジスタ群がばっさりなくなりました」に合わせる。版 2 の ring・doorbell・fence・VM_MAP・PSO・descriptor は既に 19 register と ring の command の上に組んであり、そのまま保つ）:

1. attach で EXTCAP を必ず走査し MEM・FMT・DISP2 の register を読む事（版 2 §5.1 の 4・5）→ EXTCAP は任意。無ければ（EXTCAP_PTR = 0）それで正しい device として動く。
2. `vk-format.c` が FMT の register（FMT_INDEX → FMT_OPTIMAL・LINEAR・BUFFER・LIMITS）を format ごとに読む事 → driver の format の表を capability の flag で絞る（§24.4）。FMT が見つかる GPU でも register は読まない（G43 の推し）。
3. `vk-device.c` の limit を CAPS・CAPS2・RESIDENT_WARPS の register から読む事 → `struct ghci_caps`（DEVICE_CAPS と capability の block）から（§24.3）。
4. discrete の local memory の範囲を MEM の register から読む事 → capability の block の MEM の語から（G43）。block が無ければ local memory を使わない（host memory だけで動く）。
5. 表示を DISP2 の register の窓が有る前提で組む事 → DISP2 が見つからなければ表示を出さない（firmware の framebuffer のまま、Guardrail の scanout の規則）。見つかれば §13.2 に従う（G28 の決定、残りは G49）。

## 0. 出典・読んだ物・確認の水準

| 読んだ物 | 版 | 使い方 |
| --- | --- | --- |
| amaterrace-gpu `docs/ghci.md`（1961 行） | **main 4d12da1**（2026-10-26 22:48、`git show 4d12da1:docs/ghci.md`）。読んだ時点の main は be3cf15 で、4d12da1..be3cf15 に `docs/`・`include/` の差は無い（`git diff --stat` が空） | driver の要件の正本。下の行番号は 4d12da1 の行 |
| 同 `include/ghci2_regs.h`・`ghci_cmd.h`・`ghci_layout.h`・`gsi1_isa.h` | 4d12da1 | ghci.md との照合だけ。値の正本は ghci.md（§17） |
| 同 `plan/ws001/ws.md`・`phase004`・`phase005`・`phase007` | 4d12da1 | 2.0 の hardware の計画（am-ws001-p004 MMU、am-ws001-p005 複数の ring、am-ws001-p007 2.0 化） |
| 同 `tools/gsi1asm.py`・`gsi1dis.py`・`gsi1model.py`・`gsi1fp.py` | 4d12da1 | 先頭の SPDX（Zlib）を確かめただけ（§16 の U2） |
| zedBSD `src/drivers/gpu/ghci/`（49 file、33136 行、`find`・`wc -l`） | main 660281830 | file ごとの行き先（§3） |
| `include/uapi/gpu-op.h` 2016〜2077 行、`include/drivers/gpu/gpu.h`、`include/drivers/generic/dma.h`、`include/drivers/pci/pci-ghci.h` | 同 | 境界と UAPI |
| `platform/amd64/vmunix.mk` 271〜298 行、`platform/riscv64/vmunix.mk` 244〜270 行、`Makefile` 309〜319・822〜825・1085 行、`src/kern/platform/pcat.c` 58・226・356 行、`src/drivers/platform/visionfive2/vf2-pcie.c` 25・204〜260 行 | 同 | 結線 |
| `userland/base/ghcictl`、`userland/tests/ghci-blit`・`ghci-display` | 同 | userland の行き先 |
| `plan/ws288/tests/ghci-*`・`gsi1-*`・`config-*-ghci.mk` | 同 | 試験の行き先 |
| `src/drivers/gpu/amdrdna/vk/`（12633 行） | 同 | 新しい executor の型（Venus の wire → hardware の packet） |
| `plan/coding-style.md`、`plan/guardrail.md`、AGENTS.md | 同 | 規約・境界 |

確認の水準: 上の物は全部 cat・grep・`git show` で読んだ。amaterrace-gpu には書き込み・build・git の状態を変える操作をしていない。2.0 の hardware は無く（am-ws001-p007）、**QEMU・VFIO・実機の確認はどれも未実施**（この WS では行わない、ws.md の決定）。

用語: 「2.0」= ghci.md の TO-BE（ce9725a では「GHCI 1.0」、§0.1。§1.2 の Key Architecture Tenets、§3.1〜3.5、§6、§7.1〜7.3、§8、§9、§10、§11、§12、§13.2・13.3、§14、§15）。「1.1」= §3.1.1・§7.5 と、2.0 の節に残る 1.1 の記述。**Gnn** は §19 の ghci.md の疑問、**Unn** は §20 の ghci.md 以外の判断。

## 1. 責務と境界

### 1.1 この driver がする事

1. TO-BE の PCIe の function を見つけ（BAR0 の GHCI_MAGIC と GHCI_VERSION = 0x00010000、版 3。版 2 は major 2）、DEVICE_CAPS を読み、capability の block を command で取って `struct ghci_caps` にする（§24）。EXTCAP は任意（有れば走査するが、無くても正しい device）。
2. ring を host memory に置き、doorbell（RING_WPTR）、fence、割り込み（MSI・MSI-X・INTx・poll）、fault・watchdog・reset を扱う。
3. GPU の memory: host memory（UMA・discrete の両方）と local memory（discrete）の物理の確保、session ごとの GPU の仮想 address 空間（VM_MAP・VM_UNMAP を ring に置く）、寿命（fence を待ってから返す）。
4. Vulkan の executor（`vk/`）: libvulkan の Kei GPU command protocol（Venus の番号、`uapi/gpu-op.h`）を受け、Vulkan の object を driver が持ち、PSO（`GhciPipelineState`）・descriptor table を GPU memory に焼き、command buffer の記録を submit の時に `CMD_*` の packet に直して ring に置く。
5. shader: SPIR-V → 共有の IR（`src/drivers/gpu/compiler/`）→ GSI1（`compiler/`、kernel の中。Guardrail「SPIR-V の compile は kernel 空間の driver が行う」）。
6. 表示（DISP2）: mode、lease、present、cursor、hotplug。G28 はユーザーの決定で §13.2 に従う（版 3）。DISP2 が見つからない device（EXTCAP_PTR = 0 など）では表示を出さない。DISP_SET・FLIP の TO-BE の opcode は G49 で回答待ち。
7. boot の framebuffer の扱い（§13.3）と Guardrail の scanout の規則（firmware の出力先以外に自分で scanout を始めない）。
8. capability（版 3）: DEVICE_CAPS と capability の block の flag を Vulkan の feature・limit・format・queue の性質に写し（§24）、flag の無い機能は隠すか、executor で代わりに行うか、断る。

### 1.2 しない事（境界）

- **ring を書くのは kernel だけ**。ghci.md §6.0（398〜403 行）・§8.1（651〜653 行）は「UMD が PSO を焼き ring に packet を書く」と書くが、2.0 の ring には VM_MAP（任意の bus address を GPU の VA に写す）が載るので、client が ring や secondary を直接書くと他の process の memory を読める。zedBSD では libvulkan は汎用の Venus の client のまま変えず（Guardrail）、ghci.md の「UMD」の役は kernel の `vk/` が担う（D1、§21）。
- libvulkan・共有の compiler の frontend（`spirv.c`・`optimize.c`・`ir.h`）・GPU core（`gpu.c`）・HAL は変えない。HAL の API の差分は無い（kern_* と drv_pci・drv_dma だけを使う）。
- amaterrace-gpu の sim・firmware・Verilator は使わない（ws.md の決定）。
- session の保護（§11.6、PROT）は実装しない（G29、U5）。2.0 の session ごとの VA 空間が隔離の手段。
- platform binding（§15.1、device tree の窓）の attach は書かない。2.0 の platform の board が無い（Future Work の候補、U4）。
- 複数の ring（am-ws001-p005 の `RING[i]_*`）は 2.0 の register 表に無いので扱わない。ring は `struct ghci_ring` に閉じ、後で増やせる形にする（§4.2）。

## 2. ghci.md 2.0 の要件の一覧と driver の対応

「対応」の列: **実装**（2.0 の記述で書ける）、**推し**（記述が食い違う・欠けるので推しの案で書き、G の回答で確かめる）、**待ち**（回答が無いと書けない）。

| 節（行） | 要件 | driver の対応 | 対応 | Phase |
| --- | --- | --- | --- | --- |
| §1.1（40〜53） | 一つの driver で全部の GHCI GPU、host は register だけ、UMA と discrete は確保場所だけ違う | 構成の前提（§4） | 実装 | 全部 |
| §1.2 tenets（86〜103） | 19 register、in-memory PSO、自律 MMU、LLC、DC の QoS | 19 register は ghci-regs.h。PSO の大きさは G24、MMU の page table の在りかは G08 | 推し | p002・p005 |
| §2（149〜164） | 32 bit LE、aligned access、読みに副作用無し、FourCC | `ghci_read`・`ghci_write` は今のまま | 実装 | p004 |
| §3・§3.1（169〜198） | 0x000〜0x048 の 19 register | ghci-regs.h に名前と offset。窓の大きさは G02 | 推し | p002・p004 |
| §3.2（245〜259） | DEVICE_CAPS（UMA、IOMMU、HW_WALKER、INDIRECT_DRAW、SECONDARY_RING、DUAL_SOURCE、FP16、GSI1、SIMD_LANES、NUM_SU） | identify で読む。HW_WALKER = 0 と IOMMU の意味は G09、GSI1 は G19。**版 3**: ce9725a の §3.2.1〜§3.2.3（DEVICE_CAPS の新しい bit、CAPS、CAPS2）は `struct ghci_caps` と §24 の表。CAPS・CAPS2 の場所は G41 | 推し | p002・p004・p007 |
| §3.3（261〜278） | CTRL（RUN、HALT、RESET、IRQ_GLOBAL、TLB_FLUSH）、STATUS（READY、BUSY、HALTED、FAULT、RESETTING） | start・stop・reset。状態の field は G13、TLB_FLUSH は G10 | 推し | p004 |
| §3.4（280〜292） | IRQ_STATUS（W1C）・IRQ_ENABLE: FENCE、FAULT、WATCHDOG、DISPLAY、`CTRL.IRQ_GLOBAL` と AND | handler（§9）。mask の仕方は G12 | 推し | p004 |
| §3.5（294〜317） | FAULT_INFO（kind・unit・syndrome）、FAULT_ADDR_LO/HI、DIAG_CP_PC/STATUS | fault の記録と log。kind の番号は G14、session は G14 | 推し | p004 |
| §4（322〜360） | EXTCAP の list、MEM・FMT・DISP2 など | 今の `ghci_extcap_find` を残す。2.0 で有効な ID は G29。**版 3**: EXTCAP は任意（slim は EXTCAP_PTR = 0、G43）。FMT の register は読まない（§24.4） | 推し | p002・p004 |
| §5（362〜377） | 状態 IDLE〜RESETTING、reset が保つ・消す register | start と回復の順（§10）。STATUS の状態の field は G13 | 推し | p004 |
| §6.1（420〜426） | ring の形、空と満 | `ghci_ring_plan`。pointer の単位は G03 | 推し | p002・p004 |
| §6.2（428〜437） | packet は折り返さない、NOP で埋める、全部書いてから WPTR、満なら最古の fence を待つ | 今の ring_room・pad を 2.0 の NOP で | 実装 | p004 |
| §6.3（439〜444） | DMA の同期 → store barrier → RING_WPTR | `ring_write` の順（§9.3） | 実装 | p004 |
| §6.4（446〜453） | RPTR は再利用の境界、memory は fence で守る | 寿命の規則（§7） | 実装 | p004・p005 |
| §7.1（462〜474） | header: opcode 8 bit、dwords 8 bit（1〜255）、flags 16 bit | cmd.c の encoder | 実装 | p002 |
| §7.2・§7.3（476〜613） | `CMD_NOP` 0x00、`CMD_BIND_*` 0x20〜0x23、`CMD_SET_*` 0x24〜0x26、`CMD_DRAW*` 0x27〜0x29、`CMD_DISPATCH` 0x30、`CMD_BARRIER` 0x31、`CMD_EXECUTE_COMMANDS` 0x32、`CMD_FENCE` 0x33 | cmd.c に 1 opcode 1 関数。足りない command は G21・G22・G23・G25、FENCE の幅は G11 | 推し・待ち | p002・p009 |
| §8.1・§8.2（643〜706） | `GhciPipelineState`（128 byte の表）を GPU memory に、`CMD_BIND_PIPELINE` で結ぶ | `ghci_pso_encode`（byte ごとに書く）。欠けた field は G24 | 待ち | p008 |
| §8.3（708〜744） | texture descriptor 32 byte、sampler descriptor 16 byte | `ghci_texture_descriptor_encode`・`ghci_sampler_descriptor_encode` | 推し | p008 |
| §8.4（746〜755） | descriptor table を set 0〜3 に結ぶ | table の形・buffer の descriptor・push constant は G20 | 待ち | p008 |
| §9.1（760〜776） | fence は 32 bit・回り込みの比較、完全、host memory、reset の後に最後の値を書く | 今の `ghci_fence_passed` と回復。CMD_FENCE の 48 bit との差は G11、address の空間は G04 | 推し | p004 |
| §9.2（778〜783） | level、mask → 読む → W1C → unmask | 今の handler の形。SOFT は 2.0 に無い（G12） | 推し | p004 |
| §10（785〜810） | fault → FAULTED、装置全体の reset、watchdog、quiesce、reset の順 | 回復（§10）。kind・STEP・WDT・AXI_STUCK は G14〜G16 | 推し | p004 |
| §11.1（815〜837） | host は register・doorbell・local window だけ、upload/readback は copy の command | UMA は CPU で書く。discrete の copy は G22 | 待ち（discrete） | p005・p010 |
| §11.2（839〜889） | VM_MAP・VM_UNMAP を ring に、2 段の page table、unmap の drain と TLB の無効化は CP | vm.c（§5.5）。opcode・session・幅・page table の在りか・失敗の報告は G04〜G08 | 待ち | p005 |
| §11.3（891〜954） | UMA と discrete、MEM capability、local window | memory.c。LOCAL_BASE・HOST_LIMIT の VM での意味は G31 | 推し | p005・p010 |
| §11.4（956〜971） | coherence は platform の性質、OS の DMA 同期 | 今の host_uncached の仕組みを残す | 実装 | p005 |
| §11.5（973〜984） | GPU の cache の無効化、fence の順序 | 1.1 の command を指す（G27）。submit の頭に `CMD_BARRIER` の無効化を置く | 推し | p009 |
| §11.6（986〜1118） | PROT | 1.1 の仕組み（G29）。実装しない | — | — |
| §12（1120〜1668） | GSI1、program の binary（§12.10）、interface 表、MSAA、色の出力 | compiler/ は残す。CAPS の bit・limit の出所は G18、program の渡し方は G19 | 推し | p006 |
| §13.2（1721〜1848） | DISP2 | 2.0 の command が無い（G28） | 待ち | p011 |
| §13.3（1850〜1862） | boot framebuffer の引き継ぎ | attach で ENABLE を見て窓と範囲を保つ | 実装 | p005（窓）・p011 |
| §14（1864〜1887） | MAGIC・VERSION・SCRATCH の往復、最小の ring（FENCE 1 つ）で DMA と IRQ を確かめる | attach の確かめ（§5.1） | 実装 | p004 |
| §15.2（1913〜1926） | class 03h/02h か 80h、BAR0、BAR2（任意）、MSI/MSI-X 1 本か INTx、Bus Master、No Snoop 無し | pci-attach.c を残す。BAR0 の大きさは G02、DMA の門は G30 | 推し | p004 |
| §16（1928〜1939） | major で互換、知らない major は拒む | **版 3**: VERSION = 0x00010000（TO-BE、ghci.md の GHCI 1.0）だけを受ける。minor ≥ 1（1.1 legacy）と他の major は log を出して attach しない（G40） | 推し | p004 |
| Appendix A（1941〜1961） | 初期化の順（参考） | 2.0 の順は §5.1。MEM_READY・INIT は G30 | 推し | p004 |

## 3. 今の driver の file ごとの行き先

「残す」= 中身はほぼそのまま（参照する名前の変更程度）、「直す」= 構造を保って 1.1 の部分を 2.0 に替える、「捨てる」= 消す（rm は Q1）、「新」= 新しく書く。

| file | 行 | 今の役 | 行き先 | Phase | 理由 |
| --- | --- | --- | --- | --- | --- |
| `ghci.h` | 835 | device・session・allocation・display の型、file 間の宣言 | 直す | p004〜 | vendor/device/isa/caps2/gclk/aperture/fw_*・diag_*・prot_* を外し、DEVICE_CAPS・VM・ring を足す（§4.3） |
| `ghci-words.h` | 287 | host で動く語の演算の宣言 | 直す | p002 | native・blit・DISP_SET の宣言を外し、cmd.c・VA の宣言を足す |
| `hcr.h` | 485 | 1.1 の register（ghci_regs.h の写し） | 捨てる → `ghci-regs.h` 新 | p002 | 1.1 の値 |
| `packets.h` | 66 | 1.1 の封筒と class N | 捨てる → `ghci-cmd.h` 新 | p002 | 1.1 の封筒 |
| `frame.h` | 221 | class V（GHCI_FRAME・result） | 捨てる | p004 | firmware の中継が無い |
| `layout.h` | 167 | image の layout（firmware と共有） | 直す | p008 | 2.0 の texture descriptor は pitch を持つので driver だけの規則にする。「firmware と同じ計算」の注を外す |
| `check.c` | 584 | fence の比較、ring の余地、EXTCAP、native の検査 | 直す | p002 | fence・ring・EXTCAP は残し、native の検査（約 400 行）を捨てる |
| `state.c` | 206 | 1.1 の packet（FENCE・WAIT_IDLE・INVALIDATE・DISP_SET・FLIP・BLIT） | 捨てる → `cmd.c` 新 | p002 | 1.1 の packet |
| `range.c` | 194 | first fit の address の allocator | 残す | p002 | local memory と VA の両方に使う |
| `capset.c` | 88 | libvulkan が読む capset | 残す | — | 形は libvulkan が決める |
| `jobs.c` | 278 | job marker | 残す | — | fence に結ぶ仕組みは 2.0 でも同じ |
| `ghci.c` | 1436 | attach の共通、session、resource、command | 直す | p004・p005 | identify を 2.0 に、session に VM、`gpu_command` から NATIVE・DIAG・FIRMWARE を外す |
| `pci-attach.c` | 479 | PCIe の結び（BAR0・BAR2・割り込み・host の view） | 直す（小） | p004 | BAR0 の大きさの検査（G02）、`ghci_device_start` の呼び方は同じ |
| `cp.c` | 1262 | ring・fence・IRQ・回復・service | 直す（大） | p004 | 2.0 の register・IRQ の bit・reset の規則、firmware の start と INIT を外す |
| `service.c` | 74 | service・poll の thread の開始 | 残す | — | |
| `memory.c` | 688 | local・host の物理、aperture、window、遅延の解放 | 直す | p005 | aperture が無い。物理の確保と VA を分ける |
| `blit.c` | 195 | BLT での clear と bounce の転送 | 捨てる | p004 | 2.0 に BLT が無い。UMA は CPU、discrete は p010（G22） |
| `native.c` | 96 | `GPU_OP_GHCI_NATIVE`（試験用） | 捨てる | p004 | class 0 の µop が無い |
| `firmware.c` | 2071 | INIT、DIAG、CPFW の reload、paint | 捨てる | p004 | 2.0 に INIT・DIAG・DBG・CPFW が無い。liveness は SCRATCH と FENCE（§14） |
| `share.c` | 275 | session 間の export/import | 直す | p005 | import を輸入先の VA に VM_MAP する |
| `isa.c` | 48 | ISA の backend の選択 | 直す（小） | p006 | ISA_ID が無い。DEVICE_CAPS（G19）から |
| `display.c` | 1274 | display の ops（lease、present、cursor） | 直す | p011 | ops の形は残す。下の scan の送り方が変わる |
| `display-scan.c`・`.h` | 1919 | DISP_SET・FLIP を ring に置く側 | 直す | p011 | lease・queue・record・回復の数え直しの論理は残し、packet を 2.0 の物（G28）に |
| `adv7511.c` | 320 | DC の I2C の先の HDMI 送信器 | 残す | p011 | DC_CAPS[2] の仕組みは 2.0 の DISP2 にもある（1767 行） |
| `compiler/*`（gsi1-*.c・.h、`gsi1_isa.h`） | 10588 | GSI1 の compiler | 残す | p006 | §12 の ISA と program の形は 2.0 でも同じ。`gsi1_isa.h` は amaterrace の同名と header の 1 行（2 行目の名前）だけ違う（`diff` で確認） |
| `vk/vk.h` | 473 | executor の型 | 書き直す | p007 | relay の型を捨て、driver が持つ object の型に |
| `vk/vk-engine.c`・`.h` | 880 | firmware の heap、staging、frame の結果 | 捨てる | p007 | firmware の中継が無い |
| `vk/vk-relay.c` | 1280 | R（firmware へそのまま） | 捨てる | p007 | 同上。decode の部分は vk-record.c・vk-layout.c が新しく持つ |
| `vk/vk-stream.c` | 1115 | frame の decode、transport、staging | 直す | p007 | transport（SET_REPLY_STREAM・SEEK・EXECUTE_STREAMS・trailer）を残し、staging と GHCI_FRAME を捨てる |
| `vk/vk-node.c`・`.h` | 543 | capset・submit・drain・jobs・stop・fault | 直す（小） | p007 | |
| `vk/vk-session.c` | 487 | SESSION_OPEN、object の表、PROT | 書き直す | p007 | SESSION_OPEN と PROT を捨て、object の表は vk-object.c へ |
| `vk/vk-device.c` | 1336 | D の答え（instance・physical device・limit・feature・format） | 直す | p007 | limit・feature・queue・memory の型を `vk-caps.c` の写し（§24）から。CAPS・CAPS2・RESIDENT_WARPS の register は読まない（版 3） |
| `vk/vk-format.c` | 282 | VkFormat の大きさと FMT capability | 直す | p007 | 版 3: FMT の register を読む `ghci_vk_format_load`（150〜190 行）を捨て、driver の format の表（今の 253〜282 行の `fallback_features`「FMT の無い GPU の既定」を元に広げる）を capability の flag で絞る（§24.4） |
| `vk/vk-memory.c` | 1415 | GHCI_MEMORY、buffer・image（firmware の object）、fence・event・query | 書き直す | p007 | buffer・image を driver の object に |
| `vk/vk-program.c` | 1189 | module、compile、GHCI_PROGRAM | 書き直す → `vk-pipeline.c` | p008 | compile と specialization（約 500 行）は残し、PSO を焼く形に |
| （新）`ghci-regs.h`・`ghci-cmd.h`・`cmd.c` | — | 2.0 の register・packet・PSO・descriptor の値と encoder | 新 | p002 | §4.4 |
| （新）`caps.c` | — | 版 3: DEVICE_CAPS と capability の block の語 → `struct ghci_caps`（decode・重なる bit の照合・既定の値）。kernel を触らない（host 試験がそのまま呼ぶ） | 新 | p002 | §24.1〜§24.2 |
| （新）`vk/vk-caps.c` | — | 版 3: `struct ghci_caps` → VkPhysicalDeviceFeatures・limits・queue family・memory の型・format の feature（§24 の表を code の表に） | 新 | p007 | §24.3〜§24.5 |
| （新）`vm.c` | — | session の VA 空間、VM_MAP・VM_UNMAP、再生 | 新 | p005 | §5.5 |
| （新）`vk/vk-object.c`・`vk-layout.c`・`vk-target.c`・`vk-command.c`・`vk-record.c`・`vk-execute.c`・`vk-queue.c`・`vk-reply.c` | — | driver が持つ Vulkan の object と、記録 → `CMD_*` | 新 | p007〜p009 | amdrdna の `vk/` と同じ分け方（§11） |

外の file:

| file | 行き先 | Phase |
| --- | --- | --- |
| `platform/amd64/vmunix.mk`・`platform/riscv64/vmunix.mk` の GHCI の list | 直す（file の増減。`native.c` の `CONFIG_GHCI_NATIVE` の block を外す） | 各 Phase |
| `Makefile` 309〜319・823 行の `CONFIG_GHCI_NATIVE` | 外す（U1、Q1 の承認） | p012 |
| `include/uapi/gpu-op.h` 2016〜2077 行の `GPU_OP_GHCI_*` | 外す（U1） | p012 |
| `include/drivers/pci/pci-ghci.h`、`pcat.c`、`vf2-pcie.c` | 残す（`drv_pci_ghci_driver_register` の形は同じ） | — |
| `src/drivers/gpu/compiler/`（共有） | 残す（GHCI の feature の bit は今のまま使う） | — |
| `src/drivers/gpu/i915/vk/vk-codec.c` | 残す（wire の codec を共有） | — |
| `userland/base/ghcictl` | 直す: `list`・`display` を残し `diag`・`paint`・`fwload` を外す | p012 |
| `userland/tests/ghci-blit` | 捨てる（`GPU_OP_GHCI_NATIVE` の試験） | p012 |
| `userland/tests/ghci-display` | 残す（汎用の display の ioctl だけ。実機でだけ動く） | — |
| `plan/ws288/tests/ghci-host-test.*`・`ghci-driver-host-test.c`・`ghci-vk-host-test.c`・`ghci-e2e*`・`ghci-su-board.py`・`bringup-tb.*`・`config-*-ghci.mk` | 1.1 の試験。p004 で build できなくなる。master.md の Tools・試験の一覧に無い（`grep` で確認）ので試験の整理の基準に従い削除を Q1 に頼む。model と kernel の stub の部分は `plan/ws290/tests/` に移して直す | p003・p004 |
| `plan/ws288/tests/gsi1-*`・`gsi1/` | compiler の回帰の試験。`plan/ws290/tests/` に移し（U2）、WS の終わりに master の Tools に登録を Q1 に頼む | p006 |

## 4. 新しい構成

### 4.1 file と責務

```
src/drivers/gpu/ghci/
  ghci-regs.h      2.0 の register・DEVICE_CAPS・CTRL・STATUS・IRQ・FAULT_INFO・EXTCAP の ID と MEM・FMT・DISP2 の offset（ghci.md から起こす）
  ghci-cmd.h       opcode・packet の dwords・PSO と descriptor の byte の offset と符号の値（ghci.md から）
  ghci-words.h     host で動く関数の宣言（fence、ring、EXTCAP、range、cmd、PSO・descriptor、jobs、capset）
  cmd.c            packet・PSO・descriptor の encoder（kernel を触らない。host 試験がそのまま呼ぶ）
  check.c          fence の比較、ring の余地、EXTCAP の走査（任意）
  caps.c           DEVICE_CAPS と capability の block → struct ghci_caps（版 3、§24）
  range.c jobs.c capset.c
  ghci.h           device・session・allocation・mapping・display の型
  ghci.c           attach の共通、identify、drv_gpu の登録、session、resource・blob・map・read・write
  pci-attach.c     PCIe の結び
  cp.c             ring、doorbell、fence、IRQ、service の仕事（回復・hang・reclaim）
  service.c        thread の開始
  memory.c         物理の確保（host・local）、window、遅延の解放
  vm.c             session の VA 空間、VM_MAP・VM_UNMAP、reset の後の再生
  share.c          export・import
  isa.c            ISA の backend の選択
  display.c display-scan.c display-scan.h adv7511.c   表示（p011）
  compiler/        GSI1 の compiler（今のまま）
  vk/              executor（§11）
```

### 4.2 主な型（ghci.h、版 1 の案。field の名前は実装で整える）

```c
/* One ring of the device: its memory, its size and the driver's write pointer. */
struct ghci_ring {
	struct ghci_allocation *memory;	/* host memory, bus address in RING_BASE */
	uint32_t bytes;			/* RING_SIZE */
	uint32_t wptr;			/* the next byte the driver writes (G03) */
};

/* One mapping of an allocation into a session's GPU address space. */
struct ghci_mapping {
	struct ghci_mapping *next;	/* the VA space's list, address order not needed */
	struct ghci_allocation *allocation;
	uint32_t address;		/* GPU virtual address (G07: 32 bits) */
	uint32_t bytes;
	uint32_t flags;			/* GHCI_VM_READ | WRITE | EXEC | ... */
	enum ghci_mapping_state state;	/* LIVE, UNMAP_WANTED, UNMAPPING (§6.3) */
	struct ghci_ticket ticket;	/* the fence after VM_UNMAP (UNMAPPING) */
	uint64_t replayed;		/* the reset generation it was last mapped in */
};

/* The mappings of one session, in the device's one VA space (G06, G32). */
struct ghci_vm {
	uint32_t session_id;		/* 1..sessions_max; 0 is the driver's */
	struct ghci_mapping *mappings;
	unsigned mapping_count;
	unsigned fault_count;		/* faults blamed on it in the window (R23) */
	uint64_t fault_window_tick;
};
```

**VA の allocator は device に 1 つ**（`struct ghci_device` の `struct ghci_range_map addresses`、`[GHCI_VA_BASE, GHCI_VA_LIMIT)`）。page table が session ごとか、全体で 1 つで PTE の session の tag で検査するかが ghci.md で決まらない（G32）ので、どちらでも正しい「全 session で VA が重ならない」形を取る。代わりに全 session の合計が 4 GiB 未満に制限される（G07）。G32 で session ごとと答えがあれば allocator を session に移すだけで済む。

`struct ghci_allocation` は物理の側だけにする: `kind`（HOST・LOCAL）、`bus`（host は bus address、local は MEM の LOCAL_BASE からの GPU の物理の address。G31）、`bytes`、`view`（CPU の view、local は NULL）、`run`、`holds`・`released`・`backing`（今のまま）。今の `gpu_address`（32 bit、aperture の中の address）は無くし、GPU が使う address は `ghci_mapping.address` だけにする。

`struct ghci_session` に `struct ghci_vm vm` を足し、`tag` を `vm.session_id` に替える。`protected` を外す。session の状態に `OPEN`・`CLOSING`・`RETIRED`（§6.2）。

`struct ghci_device` から外す: `vendor_id`・`device_id`・`isa_id`・`device_version`・`caps2`・`gclk_hz`・`line_bytes`・`aperture_*`・`prot_*`・`fw_*`・`diag_*`・`bounce`。足す: `struct ghci_caps caps`（版 3、§24.1。DEVICE_CAPS と capability の block を decode した物。attach の後は変えない）、`struct ghci_ring ring`、`fence_page`（host memory、bus address）、`sessions_max`（G06）、VA の allocator（上）、`replay_pending`（reset の後の再生の途中、§5.4）、`quarantined`（LOST で返せない物理の数と bytes、§10）。`ring_dwords`・`wptr` は `ring` へ。

### 4.3 公開と内部の interface

外（kernel の他の部分）に出す物は今と同じ: `drv_pci_ghci_driver_register()`（`include/drivers/pci/pci-ghci.h`）だけ。GPU core へは `struct drv_gpu_ops`（`gpu.h` の版 9）。

capability（`drv_gpu_ops.capabilities`）:

| Phase の後 | capabilities | 理由 |
| --- | --- | --- |
| p004 | `RESOURCE | BLOB | TRANSFER | MAPPING`（今の `ghci.c` 55〜56 行の基本の組から SHARE 系を除いた物） | Vulkan の executor がまだ無い。`NOTIFICATION` は `ops->commands`（submit・drain）が要り、無ければ `drv_gpu_register` が `EINVAL`（`gpu.c` 1148〜1157 行）。`COMMAND` を出すと libvulkan が native の device を作りに来る |
| p005 | + `SHARE | ALLOCATION_SHARE` | |
| p007 | + `CAPSET | COMMAND | NOTIFICATION | FENCE | JOB | JOB_CAPACITY` と `recovery` の ops を同時に | executor が答える（今の `vk-node.c` 90 行の bind と同じ組） |
| p011 | + `DISPLAY | DISPLAY_EVENTS`（DISP2 が有る時） | |

内部の主な関数（ghci.h、今の名前を保てる物は保つ）:

```c
int  ghci_device_start(struct ghci_device *device);
int  ghci_device_stop(struct ghci_device *device);
int  ghci_cp_start(struct ghci_device *device);
void ghci_cp_stop(struct ghci_device *device);
int  ghci_cp_submit(struct ghci_device *device, struct ghci_session *session,
                    const uint32_t *packets, uint32_t dwords, struct ghci_ticket *ticket);
int  ghci_cp_wait(struct ghci_device *device, const struct ghci_ticket *ticket, unsigned milliseconds);
int  ghci_cp_interrupt(void *argument);
int  ghci_memory_alloc(struct ghci_device *device, enum ghci_memory_use use, uint64_t bytes,
                       struct ghci_allocation **result);
void ghci_memory_free(struct ghci_device *device, struct ghci_allocation *allocation);
int  ghci_vm_open(struct ghci_device *device, struct ghci_session *session);
void ghci_vm_close(struct ghci_device *device, struct ghci_session *session);
int  ghci_vm_map(struct ghci_device *device, struct ghci_session *session,
                 struct ghci_allocation *allocation, uint32_t flags, struct ghci_mapping **result);
void ghci_vm_unmap(struct ghci_device *device, struct ghci_session *session, struct ghci_mapping *mapping);
int  ghci_vm_replay(struct ghci_device *device);	/* after a reset, G08 */
```

`ghci_cp_submit` の `session` は packet の header に session の番号を入れるため（G04 の推し）。NULL は driver の仕事（session 0）。`ghci_cp_submit` は 1 つの submit を cp_lock を持ったまま全部置く（§5.2）。

**host 試験の stub の `drv_gpu_register`** は `gpu.c` の検査（capability と ops の組、1000〜1240 行）と同じ検査を持つ（p003）。stub と core が離れる危険は p013 で `gpu.c` の検査と照らし直す。

### 4.4 2.0 の layout（cmd.c の encoder）

全部の encoder は `uint32_t *out`（dword の列）か `uint8_t *out`（PSO・descriptor）に **byte・dword を 1 つずつ書く**（struct を memory に重ねない: 詰め物・endian・`float` の型を避ける。kernel は float を使わない）。値は ghci.md の表から、block ごとに `Values referenced: amaterrace-gpu docs/ghci.md (4d12da1) section N` を付ける（§17）。

| encoder | dwords / bytes | ghci.md | 注 |
| --- | --- | --- | --- |
| `ghci_cmd_header(op, dwords, flags)` | 1 dw | §7.1 466〜470 | `dwords` 1〜255 を assert。flags[15:0] |
| `ghci_cmd_nop(dwords, out)` | 1〜255 | §7.2 480 | ring の端の埋め。255 を超える残りは NOP を続ける |
| `ghci_cmd_bind_pipeline(va, out)` | 3 | §7.3 498〜505 | va は 64 byte 揃え |
| `ghci_cmd_bind_vertex_buffers(first, count, bindings, out)` | 2 + 4n | 507〜516 | n ≤ 63（2 + 4 × 63 = 254） |
| `ghci_cmd_bind_index_buffer(va, type, out)` | 4 | 518〜524 | size は無い（G25） |
| `ghci_cmd_bind_descriptor_table(va, set, count, out)` | 4 | 526〜532 | set 0〜3 |
| `ghci_cmd_set_viewport(bits[6], out)` | 7 | 534〜538 | float は wire の bit をそのまま |
| `ghci_cmd_set_scissor(x, y, w, h, out)` | 5 | 539〜540 | |
| `ghci_cmd_set_dynamic_state(mask, bias, slope, blend, ref, out)` | 6 | 542〜550 | blend 定数の UNORM8 への変換は整数で（G24） |
| `ghci_cmd_draw`・`draw_indexed`・`draw_indirect`・`dispatch` | 5・6・6・4 | 552〜586 | |
| `ghci_cmd_barrier(src, dst, flags, out)` | 4 | 588〜594 | stage の bit の値は include の `GHCI_STAGE_*`（ghci.md に無い、G27） |
| `ghci_cmd_execute_commands(va, dwords, out)` | 4 | 596〜604 | v1 の executor は使わない（§11.5） |
| `ghci_cmd_fence(address, sequence, irq, out)` | 5 | 606〜613 | seq の hi は 0（G11） |
| `ghci_cmd_vm_map`・`vm_unmap` | 10・8 | §11.2 852〜862 | opcode は G05 の回答まで書かない |
| `ghci_pso_encode(fields, out[128])` | 128 byte | §8.2 662〜706 | 大きさは G24 |
| `ghci_texture_descriptor_encode`・`ghci_sampler_descriptor_encode` | 32・16 byte | §8.3 710〜744 | format の符号は G24 |

## 5. 流れ

### 5.1 attach（p004）

版 2 で順を直した（R07・R08）: **Bus Master は reset が済むまで立てない**。2.0 には aperture が無く、reset は `RING_BASE`・`RING_SIZE`・`IRQ_ENABLE` を保つ（§5 374 行）ので、前の kernel（warm reboot、kexec、FLR の無い function）が RUNNING のまま残した GPU は、Bus Master が立った瞬間に古い ring・fence の address（今は別の持ち主の memory）へ DMA し得る。今の `pci-attach.c` は割り込みを取って Bus Master を立ててから `ghci_device_start` を呼ぶ（165〜178 行、注「the aperture still keeps it off」）ので、この順を変える。

1. `pci-attach.c`: class 03h（subclass 02h か 80h、§15.2 1920 行）の function を受け、BAR0 を map。BAR0 が 4 KiB より小さければ拒む（G02 の推し）。function が FLR を持てば先に FLR。BAR2 が有れば window として map（使わない、§11.1）。host の view を決め（今の `choose_host_view`）、割り込みを取る（MSI-X・MSI・INTx、取れなければ poll）。**Bus Master はまだ切ったまま**。
2. `ghci.c` identify（読みだけ）: `GHCI_MAGIC` = 0x47484349、`GHCI_VERSION` = 0x00010000（版 3、TO-BE。minor ≥ 1 の 1.1 legacy と他の major は log を出して `ENODEV`、G40）。DEVICE_CAPS を読み `ghci_caps_decode_device`（§24.1）。`CAP_HW_WALKER` = 0 なら拒む（G09 の推し）。`CAP_GSI1` = 0 か `NUM_SU` = 0 の device は attach するが executor を出さない（§24.3、display だけの device）。
3. reset: 状態に依らず `CTRL.RESET` を書き、`CTRL.RESET` = 0・`STATUS.RESETTING` = 0・`STATUS.READY` = 1 を `GHCI_RESET_WAIT_MS` 待つ（待つ条件は §5.4 と同じ、R11）。FAULTED・HALTING で残った device もここで既知の状態になる（FAULTED は SCRATCH の書き込みを受けない、§5 371 行。R08）。reset は MMIO だけで済み DMA は要らない。
4. `SCRATCH` に 0x5a5a5a5a・0xa5a5a5a5 を書いて読み戻す（§14 1872〜1873 行）。EXTCAP_PTR が 0 でなければ EXTCAP を走査し DISP2（と DISP）を探す（各々の offset + 大きさが BAR0 の中か確かめる）。**版 3**: MEM・FMT の register は読まない（MEM は capability の block、FMT は §24.4 の表）。EXTCAP_PTR = 0 は正常（G43）。
5. DISP2 が有り、その `CTRL.ENABLE` が 1 なら boot framebuffer の範囲（`FB_BASE`・`FB_STRIDE`・`FB_SIZE`）を確保から外し window を開けたまま、0 なら window を閉じる（§13.3。FB_BASE の address の空間は G28）。
6. `cp.c` start: ring（host memory、既定 1 MiB、§6 の RING_SIZE の範囲）と fence page（host memory 4 KiB）を取り、fence の語に 0、`RING_BASE_LO/HI`・`RING_SIZE` を書く。IRQ_STATUS を全部 W1C、`IRQ_ENABLE` = FENCE | FAULT | WATCHDOG（DISP2 が有れば | DISPLAY、p011）。**ここで Bus Master を立てる**（`drv_pci_device_set_bus_master(pci, true)`）。`CTRL` = IRQ_GLOBAL | RUN（poll の device は IRQ_ENABLE = 0、G34）。
7. liveness: driver の `CMD_FENCE`（IRQ 付き）を 1 つ置き、fence の語が 1 になり割り込みが来る（poll の device は fence の語だけ）ことを待つ（§14 1883〜1887 行）。来なければ `EIO` で attach を失敗にし（Bus Master を切ってから memory を返す）、`DIAG_CP_PC`・`DIAG_CP_STATUS`・`STATUS` を log する。
7a. **capability の block（版 3、§24.2、G41 の推し）**: driver の `CMD_QUERY_CAPS`（host memory の 4 KiB の block の bus address）と `CMD_FENCE` を置き、fence を待って block を `ghci_caps_decode_block` で読む。CP が未知の opcode で fault（kind 1）した時・block の版が 0 か知らない時は、装置を reset して（§5.4 の段 A と同じ待ち）block 無しで続ける（`caps.sources` に記録し log）。block の DEVICE_CAPS と register の DEVICE_CAPS が違えば register を正にし log（G45）。discrete（`CAP_UMA` = 0）で block の MEM の語が無ければ local memory を使わない（§5.6）。
8. service thread・poll thread を起こし、`drv_gpu_register`。名前は `ghci:<PCI vendor>:<PCI device>`（G17 の推し: 2.0 には vendor/device の FourCC が無い。block に vendor・device が有ればそれを log に足す）。

### 5.2 submit（p004、vk/ は p009）

`ghci_cp_submit`: cp_lock を取り、`replay_pending` なら再生が終わるまで待つ（§5.4）。要る byte（packet + CMD_FENCE 5 dw）を `ghci_ring_plan` で量る。**1 つの submit は cp_lock を持ったまま全部置く**（R05）: ring の余地が足りなければ cp_lock を持ったまま RPTR の進みを有界に待つ（今の `ring_room` が RPTR を 1 ms ごとに読む形、cp.c 789〜815 行。§6.4 450〜451 行: RPTR より前は再利用できる）。ring より大きい submit は packet の境で分け、分けた所ごとに doorbell を鳴らして RPTR を待つ（cp_lock は離さないので、別の session の packet も reset も間に入らない）。待ちが `GHCI_RING_WAIT_MS` を超えたら、前半が置かれていない時は `ETIMEDOUT` で返し、前半が置かれた後なら hang として扱い（fence の無い半分の submit が ring に有る）回復を起こして `EIO`。packet は session の番号を header に（G04）、最後に device の fence の値 +1 を書く `CMD_FENCE`（fence page の bus address、IRQ 付き）を置く。§9.3 の順で doorbell。ticket = {fence, generation}。

fence の窓（今の `fence_window_wait`、cp.c 974〜997 行）も cp_lock を持ったまま `ghci_cp_wait` で有界に待つ今の形を保つ（版 1 の「cp_lock を離す」は今の code の読み違い、R12）。

### 5.3 割り込み（p004）

handler（`ghci_cp_interrupt`、irq_lock の spin）: `IRQ_ENABLE` = 0 → `IRQ_STATUS` を読む → 扱う bit を W1C → `IRQ_ENABLE` = irq_mask。FENCE: fence page を読み `completed` を進め `fence_wake` を起こし service に RECLAIM。FAULT: FAULT_INFO・FAULT_ADDR を記録し service に RECOVER。WATCHDOG: 同じく RECOVER。DISPLAY: service に DISPLAY（p011）。`drv_gpu_complete` は handler で呼ばない（今と同じ、service thread が呼ぶ）。

### 5.4 fault・watchdog・reset（p004・p005、§10）

版 2 で reset と VA の再生を分けた（R01）。今の recover は cp_lock の中で device.lock を取らない（cp.c 1068〜1161 行、`fail_sessions` は cp_lock を離してから device.lock、1203〜1227 行）ので、その形を保つ。

**段 A（service thread、cp_lock だけ）**:
1. cp_lock を取る。`CTRL.RESET` を書き、`CTRL.RESET` = 0・`STATUS.RESETTING` = 0・`STATUS.READY` = 1 を待つ（`GHCI_RESET_WAIT_MS`）。待ちが切れたら **即 LOST**（GPU がまだ DMA し得る。下の「LOST」）。
2. reset は ring の register・IRQ_ENABLE を保ち WPTR・RPTR・IRQ_STATUS・FAULT_*・CTRL.RUN を消す（§5 374〜377 行。IRQ_GLOBAL の扱いは G34）。driver は `ring.wptr` = 0、fence の語に最後に完了した値を書く（§9.1 775〜776 行）。
3. generation を進める。古い generation の ticket と job marker を ERROR で完了する。reset が成功した（hardware が quiesce した、§10 805〜807 行）ので、古い ticket を待っていた物理は返してよい。
4. 表示（p011 の後）: CP は IDLE のまま、DISP2 の `STATUS[31]` が落ちるのを待ち `TAKEN0`・`TAKEN1` を読んで flip を数え直す（§13.2 1827〜1830 行。今の recover も RUN の前、cp.c 1155〜1158 行。R09）。
5. `replay_pending` = 1（G08 の推し: reset で CP の page table が消える。消えないと答えがあれば再生は要らず、0 のまま）。`CTRL` = IRQ_GLOBAL | RUN（DMA を伴う再生は RUN の後。IDLE が doorbell を受けるかは未定、G33）。
6. cp_lock を離す。未完の仕事を持っていた session に `drv_gpu_report_session_error`（§10、R13）。

**段 B（service thread、device.lock → cp_lock の順）**: device.lock を取り、生きている mapping（`state` が LIVE と UNMAP_WANTED の物）を全部、cp_lock を取って VM_MAP で ring に置く。ring の余地は RPTR を待って作る（RUN の後なので CP は読み進む）。mapping の `replayed` を今の generation に。終われば `replay_pending` = 0 にして submit の待ちを起こす。再生の途中で fault が来れば段 A からやり直す（`replayed` が古い mapping だけを次に置く）。表示の再開（queue の頭の flip を置き直す）もここ（p011）。

**reset の失敗と LOST**（R04・R11）:
- 「`CTRL.RESET` が落ちない・READY が立たない」= 即 LOST。
- 「reset は成功したが `GHCI_FAULT_STORM_MS`（推し 10 s）の中で fault・hang が 3 回」= LOST。
- LOST: 以後の submit は `EIO`、`drv_gpu_report_error`。**GPU がまだ DMA し得るので、物理を一切返さない**（ring、fence page、未完の ticket を待つ物、mapping の物理。`quarantined` に数と bytes を記録して log）。`drv_gpu_report_error` の前に quarantine を立てる（`gpu.h` 180〜184 行の約束）。detach は Bus Master を切った後にだけ返す（§5.7）。

hang（fence が `GHCI_HANG_MS` 動かず未完がある）も同じ回復（hardware の watchdog の有無に依らない、G16）。

**fault の session**（R23）: fault の時に責めを負う session（G14 の欄。無ければ hang・fault の時に未完の最古の ticket の session）の `fault_count` を数え、`GHCI_FAULT_WINDOW_MS`（推し 60 s）の中で 3 回なら、その session を止める（`drv_gpu_report_session_error` は sticky で以後の submit を止める、`gpu.c` 858〜861 行）。1 つの client が装置全体の reset を何度も起こせない。

### 5.5 VM（p005、§11.2）

- session の open で `ghci_vm_open`: 番号を取る（1〜`sessions_max`、G06）。VA は device の 1 つの allocator（§4.2、`[GHCI_VA_BASE, GHCI_VA_LIMIT)`、推し: 0x00010000〜0xFFFF0000、page 0 を null にし上端を空ける）から取る（G32）。G06 の回答に session の生成の command が有れば ring に置く。
- 確保 → map: `ghci_memory_alloc` で物理（host は連続の run、local は range）→ `ghci_vm_map` で VA を取り `VM_MAP(session, flags, va, bus, bytes)` を ring に置く（device.lock → cp_lock の順）。map の完了は待たない: 後の packet は ring の順で後に来るので CP は map の後に読む（§7.1 474 行の in-order。VM_MAP が後の packet より先に効く事は G08 の推しに含めて確かめる）。flags: host memory は `host_uncached` に依らず常に READ | WRITE | COHERENT、WC は付けない（§11.4 968〜971 行「GPU は coherence を外さない」と合わせる、G38）。program は | EXEC。
- **VM_MAP・VM_UNMAP の回数を減らす**（R14）: VM_UNMAP は GPU の全 unit の drain を待つ（§11.2 885〜889 行）ので、object ごと・submit ごとの unmap は全 session を止める。map・unmap は VkDeviceMemory・blob・import と、session の **executor の heap**（§11.2 の `vk-memory.c`: 1 MiB から始め足りなければ倍に、一度だけ VM_MAP）の単位だけにする。PSO・descriptor table・program・submit の一時の memory は heap の中を sub-allocate し、返す時は fence を待つだけで unmap しない。
- unmap: `ghci_vm_unmap` は mapping を UNMAP_WANTED にし、`VM_UNMAP` を ring に置けたら UNMAPPING にして後の fence の ticket を記す。置けなかった時（ring の満の timeout、再生の途中）は UNMAP_WANTED のまま物理を hold し、service thread が再び置く（R04）。UNMAPPING の ticket が過ぎたら VA を allocator に返し、物理は他の mapping と hold が無ければ返す（§11.2 885〜889 行の drain は CP の物、driver は fence で待つ）。
- import（share.c）: 輸入先の session の番号で同じ物理を `VM_MAP`（READ、必要なら WRITE）。VA は device の allocator から別に取る。export の record は物理を hold する（今の share.c の hold を物理に）。
- close（R24）: `close` は失敗できない（`gpu.h` 110〜111 行）。全 mapping を UNMAP_WANTED にして置ける物は置き、session を `CLOSING` にして返る（待たない）。全部の mapping が消え、最後の ticket が過ぎた時（または成功した reset の後で page table が消えると答えが有る時）に番号を返して `RETIRED`。その前に番号を次の open に渡さない（GPU の page table に古い mapping が残る番号を使い回さない）。
- 32 bit の制限: G07 の推しで VA は 32 bit、PTE の物理は [31:12] なので host memory は bus address が 4 GiB 未満の物だけ（今の `host_take` の `kern_pmem_alloc_limited` に上限 4 GiB − 1）。

### 5.6 unified と discrete（p005・p010、§11.3）

| | unified（`CAP_UMA` = 1、版 3） | discrete（`CAP_UMA` = 0 で、capability の block が MEM の語（LOCAL_BASE・LOCAL_SIZE > 0）を持つ、版 3。block が無い discrete は local memory を使わず unified と同じ扱い） |
| --- | --- | --- |
| ring・fence・PSO・descriptor・program・command の写し | host | host（CPU が書き GPU が読む物は常に host。§11.3 の表 934 行と同じ考え） |
| `GHCI_USE_MAPPABLE`（Vulkan の host visible） | host | host |
| `GHCI_USE_DEVICE`（device local だけ） | host | local（p010） |
| 確保の時の 0 埋め | CPU | local は GPU の fill（G22 の command、無ければ executor の compute の fill、§11.4）。それまでは local の確保を `ENOTSUP`（p005） |
| Vulkan の heap | 1 つ | 2 つ（p010 まで 1 つ: host だけ） |

### 5.7 detach（p004、R10）

`ghci_device_stop`: session が残れば `EBUSY`（今と同じ）。
1. （p011 の後）表示が driver の framebuffer を scan していれば CTRL.ENABLE = 0 にして `STATUS.idle` を待つ（§13.1 1714 行、§13.2 1843〜1845 行。表示は GPU の reset で止まらない、1734〜1735 行）。boot framebuffer を scan したままなら触らない。
2. RUNNING なら `CTRL.HALT` → `STATUS.HALTED` を有界に待つ（FAULTED・LOST は HALT を受けないので飛ばす、§5 371 行）。
3. 状態に依らず `CTRL.RESET`（RING_BASE が返す ring を指したまま IDLE に残らないよう、IRQ_ENABLE = 0、CTRL = 0）。
4. **Bus Master を切る**（`drv_pci_device_set_bus_master(pci, false)`。今は memory を返した後の `release_bus`、pci-attach.c 214〜221・449〜454 行、を前に出す）。
5. thread を止める → ring・fence page・quarantine の物理を返す（Bus Master を切った後なので LOST でも返せる）→ window を開ける（§13.3）。

## 6. 状態遷移

### 6.1 device（driver の側）

```
ATTACHING --identify・reset・liveness OK--> RUNNING
ATTACHING --失敗--> (Bus Master を切り、memory と bus を返す)
RUNNING --FAULT/WATCHDOG/hang--> RECOVERING(段 A) --reset OK--> REPLAYING(段 B) --> RUNNING
RECOVERING --RESET が落ちない・READY が立たない--> LOST
RUNNING --reset は成功したが短い間に fault・hang 3 回--> LOST
LOST: submit は EIO、物理を返さない（quarantine）、detach だけ受ける
RUNNING|LOST --stop（session 0 個）--> STOPPING --> DETACHED
```

CP の状態（§5）と driver の見方: driver は `STATUS` の READY・BUSY・HALTED・FAULT・RESETTING の bit と自分の `CTRL.RUN` だけで判断する（STATUS[19:16] は §3.3 に無い、G13）。IDLE と RUNNING は driver が `CTRL.RUN` を書いたかで区別する。

### 6.2 session

`OPEN`（番号あり）→ `CLOSING`（全 mapping を UNMAP_WANTED、置けた物は UNMAPPING、番号は予約のまま）→ `RETIRED`（全 mapping が消え最後の ticket が過ぎた。番号を返す）。reset は session を消さない（mapping は再生）。未完の仕事を reset で失った session は `drv_gpu_report_session_error` で止まる（§10）。

### 6.3 allocation と mapping

```
allocation: ALLOCATED --map--> MAPPED(1..n mappings) --mapping も hold も無い--> FREED
mapping:    LIVE(VM_MAP placed) --unmap--> UNMAP_WANTED --VM_UNMAP placed--> UNMAPPING(ticket) --ticket passed--> gone
            UNMAP_WANTED は ring の満・再生の途中で置けなかった間。物理は hold したまま、service が再び置く
LOST の device: どの状態からも FREED にしない（quarantine）
```

hold（frame の返事を書く blob、export、表示の scan）が有る間は物理を返さない（今の `holds`・`released` の規則）。

## 7. 資源の所有権と寿命

| 資源 | 持ち主 | 確保 | 返す時 |
| --- | --- | --- | --- |
| ring、fence page | device | start | stop の Bus Master を切った後（§5.7） |
| session の番号 | session | open | `RETIRED`（§6.2） |
| VA | device の allocator（mapping ごと） | map | UNMAPPING の ticket の後 |
| resource・blob の物理 | session（export の record も hold） | `resource_create`・`blob_create` | `resource_destroy` → 全 mapping の unmap の ticket の後 |
| executor の heap（session ごと、§5.5） | vk の session | 最初の object | session の close（heap の mapping の unmap の ticket の後） |
| PSO・descriptor table・program・submit の一時の memory | heap の中の区画 | create・submit | destroy・submit の後、その時の session の最後の fence の後（submit 中の command が読むため。§6.4 453 行）。unmap はしない |
| command buffer の記録 | vk の command buffer（kernel の heap） | record | reset・free。submit した写しは ring の中だけ |
| job marker | session の表（64） | reserve | complete・cancel |
| boot framebuffer の範囲 | firmware（driver は確保から外すだけ） | — | 最初の自分の flip の後（p011）。p011 までは detach まで外したまま |

規則: GPU が参照し得る物は「参照を含む submit の fence が過ぎるまで」返さない。ticket が「reset で終わった」時は、その reset が成功した時だけ返してよい（LOST は返さない）。回収は IRQ の handler ではなく service thread（今の L7 の規則）。**executor の heap・PSO・descriptor・program は `resource_map` の対象にしない**（client が kernel の検査した program や PSO を書き換えられないように、R22）。

## 8. 並行性とロック

今の順を保つ: `device.lock`（mutex: session、allocator、VA の allocator、mapping の list、寿命）→ `display.lock`（mutex）→ `cp_lock`（mutex: ring、doorbell、reset、`replay_pending`）。`irq_lock`（spin: completed、generation、fault の記録、service の仕事）は handler が取る最も内側の lock で、他の lock を持ったまま待たない。

- VM_MAP・VM_UNMAP・再生（段 B）: VA と mapping の list は device.lock の中、ring に置くのは cp_lock の中（device.lock → cp_lock）。
- reset（段 A）は cp_lock だけを取り、device.lock を取らない（今の形）。段 A の後の session の報告と段 B は cp_lock を離してから device.lock を取る。
- ring の満の待ちは cp_lock を持ったまま有界に（今の `ring_room`・`fence_window_wait`、§5.2）。cp_lock を持つ間に device.lock・display.lock を取らない。
- GPU core の約束（`gpu.h`）: 1 session は 1 ioctl ずつ、別の session は並行。executor の session の状態は session の admission で守られ、device 全体の物（ring、VA の allocator、物理）は上の lock で守る。
- reset の間（cp_lock を持つ）に来た submit は cp_lock で待ち、`replay_pending` が落ちてから新しい generation で置かれる。
- poll の device: poll thread が handler の仕事を `GHCI_POLL_MS` ごとに行う（今のまま）。
- host 試験の stub は lock の取得の順を記録し、上の順に反すれば FAIL にする（§15.2、R19）。

## 9. 割り込みと DMA

### 9.1 割り込み

- MSI・MSI-X は 1 本、INTx は予備、どれも無ければ poll（今の `take_interrupt`。§15.2 1923 行）。MSI は `OR(IRQ_STATUS & IRQ_ENABLE)` が 0 → 1 になる時と、IRQ_STATUS・IRQ_ENABLE を書いた後にまだ 1 の時に来るので、handler の「mask → 読む → W1C → unmask」は取りこぼさない。
- `CTRL.IRQ_GLOBAL` は start で 1 にし、handler では触らない（CTRL は RUN・HALT・RESET と同じ register なので、handler で読み書きすると cp_lock の側の書き込みと競る。G12）。reset の後にも書き直す（保たれるか未定、G34）。
- poll の device は IRQ_ENABLE = 0 で IRQ_STATUS を読む。IRQ_ENABLE が 0 の bit が IRQ_STATUS に立つかは未定（§3.4 283〜284 行、G34）。poll は fence の語を主に見るので fence は困らないが、fault の検出は G34 に依る（推し: 立つ）。

### 9.2 DMA と coherence（§11.4）

- host memory は `kern_pmem_alloc_limited`（上限 4 GiB − 1、G07）の連続の run。CPU の view は直の map か uncached の alias（今の `host_view`・`host_uncached`、`CONFIG_GHCI_HOST_UNCACHED`）。bus address は物理の address と同じ（今の driver と同じ。bus の offset が有る platform は未検証: VF2 の WS288 p043 は canceled）。
- 連続の run にする理由: `drv_gpu_mapping` は 1 つの物理の範囲（`gpu.h`）なので user の map が連続を要る。VM で不連続でも GPU は困らないので、将来 `drv_dma_vector` の segment ごとに VM_MAP する形に替えられる（§21 の D8）。

### 9.3 doorbell の順（§6.3）

ring へ書く → store barrier → `RING_WPTR`。coherent な bus では DMA の同期は要らず、非 coherent な bus では ring と fence page を uncached の view で触るので cache の clean・invalidate も要らない（今の形: cp.c 913〜920 行、pci-attach.c 372〜395 行、memory.c 617〜645 行。版 1 の「cache_clean の相当」は誤り、R12）。dword は little endian で書く（§16）。

## 10. 失敗と回復（まとめ）

| 失敗 | 検出 | 回復 | client に見える物 |
| --- | --- | --- | --- |
| attach の識別の失敗（MAGIC・major・HW_WALKER・reset・SCRATCH・liveness） | identify | attach しない（Bus Master を切り memory と bus を返す） | /dev/gpuN が無い。log |
| fault（IRQ_FAULT） | handler | §5.4 の装置全体の reset と再生 | reset で未完の仕事を失った session は全部 `drv_gpu_report_session_error`（sticky で以後の submit を止める、今の `fail_sessions` と同じ、R13）。仕事の無かった session は続く。fault の session の特定（G14）は log と §5.4 の数えにだけ使う |
| watchdog（IRQ_WATCHDOG）・hang | handler・service | 同上 | 同上 |
| 同じ session の fault が短い間に 3 回 | §5.4 | その session を止める | その session の以後の submit が失敗 |
| reset の失敗（RESET が落ちない・READY が立たない）、reset の後の fault の嵐 | recover | LOST（物理を返さない） | 以後 EIO、`drv_gpu_report_error` |
| ring の満 | ring_plan | cp_lock を持ったまま RPTR を有界に待つ。前半を置く前なら `ETIMEDOUT`、後なら hang として回復 | submit の失敗 |
| VM_UNMAP を置けない | vm.c | UNMAP_WANTED で物理を hold、service が再び置く | 無し（memory が遅れて返る） |
| VA・物理の不足 | allocator | 遅延の解放を回収して一度だけやり直す | `ENOMEM`（Vulkan は OUT_OF_DEVICE_MEMORY） |
| VM_MAP の失敗（CP の page table の不足） | G08（2.0 に返事の仕組みが無い） | 推し: fault として装置の reset | 上の fault と同じ |
| compile の失敗 | compiler | pipeline を作らない | Vulkan の結果と理由の log |
| bind の無い draw（記録の検査） | vk-execute.c（§11.3） | submit を拒む | VK_ERROR（submit の返事） |
| 割り込みが来ない | liveness・hang | poll に落とさない（hang として reset。poll は bus が割り込みを持たない時だけ） | 上の fault と同じ |

## 11. Vulkan の executor（`vk/`）

### 11.1 方針

今の `vk/` は「検証する中継」（Vulkan の object の意味は firmware の物、WS288 design §6.7）。2.0 には firmware の gpu-op が無いので、**Vulkan の object を driver が持ち、記録を submit で `CMD_*` に直す**（amdrdna の `vk/` と同じ型、`vk-execute.c` 2083 行の先頭の注「The records are turned into packets」）。wire の decode は i915 の codec（`src/drivers/gpu/i915/vk/vk-codec.c`）を今のまま共有する。

### 11.2 file

| file | 役 | 元 |
| --- | --- | --- |
| `vk.h` | 型（object の種類、session、記録、submit の build） | 書き直し |
| `vk-stream.c` | transport（reply の stream、seek、EXECUTE_STREAMS、trailer）、dispatch | 今の物から staging・GHCI_FRAME を外す |
| `vk-node.c`・`.h` | capset、submit、drain、jobs、stop、fault | 今の物 |
| `vk-object.c` | wire の識別子 → object、payload の寿命（hold） | 新（amdrdna の vk-object.c の形） |
| `vk-reply.c` | create・destroy の返事 | 新 |
| `vk-device.c` | instance・physical device・device・queue の答え、limit・feature | 今の物（答えの中身は `vk-caps.c` から、版 3） |
| `vk-caps.c` | capability → Vulkan の feature・limit・queue・memory の型・format の feature の写し（§24） | 新（版 3） |
| `vk-format.c` | format の大きさ。feature は `vk-caps.c` の format の表から（FMT の register は読まない、版 3） | 今の物を直す |
| `vk-memory.c` | VkDeviceMemory（allocation + session の VA への map）、buffer・image（layout.h）、requirements、session の executor の heap（§5.5、PSO・table・program・一時の memory の sub-allocate、fence で返す） | 書き直し |
| `vk-layout.c` | descriptor set layout・pipeline layout・pool・set・update → descriptor table（G20） | 新 |
| `vk-pipeline.c` | shader module、cache、graphics・compute pipeline: stage ごとに compile → program を session の VA に置く → PSO を焼く（G19・G24） | `vk-program.c` の compile と specialization を移す |
| `vk-target.c` | render pass・framebuffer → render target の束（G21） | 新 |
| `vk-copy.c` | executor の compute program（copy・fill・clear・resolve、G22 の command が来るまで）を IR で作り compile して heap に置く | 新（amdrdna の `vk-quad.c` の考え） |
| `vk-command.c` | command pool・buffer、begin・end・reset | 新 |
| `vk-record.c` | `vkCmd*` を記録に（引数を全部読み、名を持つ object を hold） | 新（decode の多くは今の `vk-relay.c` の `record`・`decode_*` から） |
| `vk-execute.c` | 記録 → `CMD_*` の列（submit ごとに build の buffer へ） | 新 |
| `vk-queue.c` | `vkQueueSubmit`、fence・semaphore・event・query | 新（fence の部分は今の `vk-memory.c` から） |

### 11.3 submit の流れ

1. `vkQueueSubmit` の frame で、各 command buffer の記録を順に `vk-execute.c` が `CMD_*` に直し、kernel の build の buffer に貯める。最初に `CMD_BARRIER`（`INVAL_L1`、G27 の推し: 暗黙の無効化を当てにしない）。
2. **bind の検査と出し直し**（R22）: CP の bind の状態（PSO・vertex buffer・descriptor table・dynamic state）が session ごとか ring 全体か、reset で消えるかが未定（G37）なので、executor は各 command buffer の頭で「その時点の bind を全部」出し直さない代わりに、draw・dispatch の前に「同じ command buffer の中で pipeline、その pipeline が使う set、要る dynamic state が bind 済み」を記録の時に検査し、欠ければ submit を拒む（VK_ERROR）。各 submit の列の頭では前の submit の bind を当てにしない（command buffer は自分の bind を必ず含む、Vulkan の規則と同じ）。これで別の session の bind を自分の VA で解く draw は出ない。
3. push constant・dynamic な値で GPU memory が要る物は submit の一時の memory（session の executor の heap の区画）に置く。
4. `ghci_cp_submit(session, packets)`。1 つの submit は cp_lock を持ったまま全部置く（§5.2、R05）。
5. 最後の fence に job marker と VkFence を結ぶ（今の `jobs.c`・vk-node の commit の形）。
6. semaphore: device の ring は 1 本なので、submit の順が全部の timeline の順（今の vk-node の注と同じ）。wait は ring の順で満たされる。

### 11.4 draw と dispatch（G の回答待ちの部分）

| Vulkan | 2.0 の command | 欠け |
| --- | --- | --- |
| `vkCmdBindPipeline` | `CMD_BIND_PIPELINE`（PSO の VA） | PSO の field（G24） |
| `vkCmdBindVertexBuffers`・`IndexBuffer` | `CMD_BIND_VERTEX_BUFFERS`・`INDEX_BUFFER` | 属性 → binding の対応（G24） |
| `vkCmdBindDescriptorSets`・`PushConstants` | `CMD_BIND_DESCRIPTOR_TABLE` | buffer の descriptor、push constant の command、slot の対応（G20） |
| `vkCmdSetViewport`・`Scissor`・`DepthBias`・`BlendConstants`・`StencilReference` | `CMD_SET_*` | |
| `vkCmdBeginRenderPass`・`EndRenderPass`・`NextSubpass` | 無し | render target の command（G21） |
| `vkCmdDraw*`・`Dispatch*` | `CMD_DRAW*`・`CMD_DISPATCH` | indexed の indirect（G25） |
| `vkCmdPipelineBarrier` | `CMD_BARRIER` | stage の bit の値（G27） |
| `vkCmdCopy*`・`FillBuffer`・`UpdateBuffer`・`Clear*`・`ResolveImage`・`BlitImage` | 無し | G22。専用の command が来るまで **executor の compute program**（kernel の GSI1 の compiler で作る、amdrdna の `vk-execute.c` 22〜29 行が自分の program で copy・clear する形）で行う。buffer の copy・fill は storage buffer（G20）が要り、image の clear・copy・blit・resolve は storage image（§12.10 kind 10）か render target（G21）が要る |
| `vkCmdBeginQuery`・`EndQuery`・`WriteTimestamp`・`CopyQueryPoolResults` | 無し | G23 |
| `vkCmdSetEvent`・`ResetEvent` | executor の compute の 1 語の書き込み（CMD_FENCE を device の fence 以外の値に使ってよいかは G39） | `vkCmdWaitEvents`: 同じ queue の GPU が立てた event は ring の順と `CMD_BARRIER` で満たせる。host の `vkSetEvent` を待つ時だけ待ちの command が要る（G23） |
| `vkCmdExecuteCommands` | 記録を展開して同じ列に（`CMD_EXECUTE_COMMANDS` は使わない、§11.5） | |

### 11.5 `CMD_EXECUTE_COMMANDS` を v1 で使わない理由

secondary の VA 空間・入れ子・中で許される opcode が ghci.md に無く（G26）、client が書いた secondary を GPU に読ませると ring と同じ問題（VM_MAP）がある。v1 は記録を kernel が全部 ring に展開する。G26 が決まり `CAP_SECONDARY_RING` が有れば、kernel が書いた secondary（session の VA の中の command の写し）で ring の使用を減らす最適化を後の Phase（Future Work 候補）にできる。

### 11.6 limit と feature

**版 3 で書き直した**: 答えは全部 `vk-caps.c` が `struct ghci_caps` から作る（§24.3〜§24.5 の表）。CAPS・CAPS2・RESIDENT_WARPS・FMT の register は読まない。capability の block が無い device は DEVICE_CAPS だけで答え、block の語に依る項目は「Vulkan 1.0 が許す最小の値」か「無し」で答える（§24.3 の「block 無し」の列）。vendorID・deviceID は PCI の ID（G17、block に vendor・device が有ればそれ）。`timestampPeriod` は 0 でなく、`timestampValidBits` = 0（G23・G17、query を出さない）。

## 12. compiler（`compiler/`）

- 残す。§12 の GSI1（命令の形・opcode・program の binary・interface 表）は 2.0 の文書でも同じ節のまま。`gsi1_isa.h` は amaterrace-gpu 4d12da1 の `include/gsi1_isa.h` と header の 1 行（"Amaterrace GPU" → "zedBSD"）以外同じ（`diff` で確かめた）。
- 変わる所は `struct ghci_gsi1_options` を埋める側だけ（`vk-pipeline.c`）: `registers`（1.1 は RESIDENT_WARPS[23:16]）、`bank_words`（CAPS2）、`varying_words`（CAPS[30]）、`sample_mask_input`（CAPS[29]）の出所が 2.0 に無い（G18）。回答まで: registers 32、bank_words 256、varying_words 16、sample_mask_input 0（register・bank・varying は今の compiler が「0 なら」と既定に読む値、`gsi1-compile.h` 46〜72 行の注。varying は 0 だと 64 と読むので driver は 16 を明示する）。
- **版 3**: options は `struct ghci_caps` から埋める（§24.3 の compiler の行）: `registers` は block の LIMITS[7:0]（G48。無ければ 32）、`samples` の上限は `CAP_MSAA`（CAPS[29]）と LIMITS の最大の sample 数（G48。block が無ければ 1）、`sample_mask_input` は CAPS[29]、`varying_words` は CAPS[30]（有れば 64、無ければ 16）。今の compiler は subgroup（`shfl`・`vote`・`ballot`）を出さず（`grep` で 0 件）、SPIR-V の受け入れは `DRV_GPU_SPIRV_FEATURES_GHCI`（共有の `spirv.h` 75 行）で決まる。`CAP_SUBGROUP`・`CAP_FP16` が有っても WS290 では compiler を広げない（Vulkan 1.0 の SPIR-V 1.0 では使えない。WS226 で Vulkan 1.1 以降と一緒に）。SFU の無い GPU（`CONFIG_SU_ENABLE_SFU = 0`）を示す flag が無い（G48）ので、回答まで SFU は有ると読む。block の LIMITS[16] が 0 と答えた device では、`rcp`・`rsq`・`sin`・`cos` を使う program の compile を失敗にする（pipeline の作成の失敗。lowering は Future Work 候補）。
- program の渡し方: 1.1 は PROGRAM gpu-op。2.0 は PSO の `vs_code_va`・`fs_code_va`・`cs_code_va`（§8.2 667〜669 行）。推し: program の binary（header の word 0 = "GSI1"）の先頭の VA（G19）。program は host memory、session の VA に EXEC で map。
- 試験: `plan/ws288/tests/gsi1-host-test.*`・`gsi1-reference.*`・`gsi1/*.{comp,vert,frag}` を `plan/ws290/tests/` へ移す。今の script は `~/amaterrace-gpu/tools` の python を import する（`gsi1-host-test.sh` 15 行、`.py` 25〜27 行）。python は import で `tools/__pycache__` を書くので amaterrace-gpu を読み取り専用に保てない。U2 の推し: 4d12da1 の `tools/gsi1asm.py`・`gsi1dis.py`・`gsi1model.py`・`gsi1fp.py`（Zlib、同じ著作者、§17）を `plan/ws290/tests/gsi1-tools/` に写し、出典の SHA を README に書く。

## 13. 表示（DISP2）

- 今の `display.c`・`display-scan.c` の論理（lease、present の shared と copied、queue の深さ 2、mailbox、record と TAKEN0/1 による数え直し、cursor の plane 1、hotplug の poll、DISP_ERROR の数と mask、idle frame）は DISP2 の register の意味（§13.2 1742〜1834 行）に結びついていて、その部分は 2.0 でも同じ。
- 変わる所は「ring で register を書く・flip を置く」手段で、1.1 の DISP_SET・FLIP（`state.c` の `ghci_packet_disp_set`・`ghci_packet_flip`）が 2.0 の command 表（§7.2）に無い。§13.2 は今も DISP_SET・FLIP（1726〜1728・1753・1838 行）と IRQ_STATUS の bit 9〜12（1728 行）を書くが、2.0 の IRQ_STATUS は bit 3 の DISPLAY だけ（291 行）。→ 版 2 は「G28 が決まるまで p011 は始めない」。**版 3**: ユーザーが G28 を決めた（「G28は決まったのでは」= ghci.md §13.2 に従う）。p011 は §13.2 の register の意味・DISP_SET と FLIP の意味・事象（VBLANK・FLIP・DISP_ERROR・HOTPLUG）どおりに書く。ce9725a でも欠ける事実（DISP_SET・FLIP の TO-BE の opcode と dword の形、§3.4 の IRQ_DISPLAY 1 bit と 4 つの事象の区別、slim で DISP2 を見つける場所）は G49 で問い、p011 は推し（opcode を 1 つの定数に置く）で進め、回答で定数を直す。
- **転送が要る**（版 1 の「packet だけが替わる」は誤り、R06）: copied の present と cursor の staging は 1.1 の BLT で写していた（`display.c` 22 行、562〜615 行、1075〜1145 行）。2.0 では executor の copy（§11.4、G22 か compute）を使い、UMA で CPU が届く memory は CPU で写す。discrete では scan の image は local memory（§13.2 1798〜1801 行）なので p010 が要る。`FB_BASE`・`FB_NEXT`・`CURSOR_BASE`・flip の record の address の空間（bus か VA か）は G28 に含める。今の `struct ghci_display_image.base`（aperture の 32 bit の address）は G28 の答えの空間の address に替える。
- **DISP2 が無い device（版 3）**: EXTCAP_PTR = 0 の slim など、DISP2 が見つからなければ display の capability を出さず、scanout に触らない（boot framebuffer は firmware のまま）。capability の block の CAPS[3] `CAP_DISP` が 1 なのに DISP2 が見つからなければ log する（G49）。cursor の plane は EXT_CAP_FEATURE[2] HW_CURSOR（G42）が 0 と分かる時は出さない（今の `display.c` 184〜185・940 行の DC_CAPS[8] による `GPU_DISPLAY_HAS_CURSOR` の経路と同じ。DC_CAPS[8] と HW_CURSOR の両方を見る）。
- それまでの driver（p004〜p010）: DISP2 を見つけ、mode の表を読み、boot framebuffer の範囲と window を保つだけ。scanout を始めない・止めない（Guardrail の scanout の規則）。display の capability は出さない。
- adv7511.c: DC_CAPS[2] の外付けの送信器（1767 行）。2.0 の DISP2 にも同じ register（I2C_*）が有るので残す。HPD の poll も今の形。

## 14. 既存の framework との関係

| framework | 関係 |
| --- | --- |
| drv_gpu（`gpu.h` 版 9） | ops の表を出す（§4.3 の capability の段）。recovery_ops（stop_begin・stop_poll・fault・reset）は今の vk-node の形を保つ（p007）。`isolate` は出さない（2.0 の session ごとの回復は G15 の答え次第） |
| cdev（/dev/gpuN） | GPU core が作る。driver の名前で選ぶ（`ghci:` の prefix、ghcictl） |
| PCI | `drv_pci_ghci_driver_register` と match・attach・detach・shutdown（今の pci-attach.c の形）。pcat.c・vf2-pcie.c は変えない |
| DMA | §9.2。`drv_pci_device_dma` は coherence の判断だけに使う（今と同じ） |
| HAL | 変更なし（承認の要る差分は無い） |
| UAPI | ioctl の追加は無い。`GPU_OP_GHCI_*`（gpu-op.h 2016〜2065 行の注と 2066〜2076 行の enum）は削除（U1） |
| libvulkan | 変えない。capset（id 4、176 byte、`OPAQUE | STRICT_QUEUE | QUIESCE`）も今の `capset.c` のまま |
| 共有の compiler | 変えない（`DRV_GPU_SPIRV_FEATURES_GHCI` を今のまま使う） |

## 15. 試験の設計

### 15.1 方針

- ws.md の決定: **zedBSD の host 試験だけ**。amaterrace-gpu の sim・firmware・Verilator は使わない。2.0 の hardware は無いので QEMU・VFIO・実機は**未実施**（この WS の受け入れに含めない。CONFIG_DRIVER_PCI_GHCI は既定で n なので既定の image の boot の回帰も要らない: driver の file は既定の build に入らない）。
- build: Phase ごとに amd64 の vmunix を `plan/ws290/tests/config-amd64-ghci.mk`（`config/ci/config-amd64.mk` + `CONFIG_DRIVER_PCI_GHCI := y`）で build し warning 0。riscv64（VisionFive 2、`plan/ws290/tests/config-visionfive2-ghci.mk`、`CONFIG_GHCI_POLL := y`）は p004・p009・p013 で build し warning 0（U4）。出力は `BUILD=build/ws290-<phase>/`。
- AGENTS.md の方針: 細かい修正ごとの回帰はしない。Phase ごとに変えた所の host 試験と build。

### 15.2 部品（`plan/ws290/tests/`）

| file | 中身 |
| --- | --- |
| `ghci2-words-host-test.c` | cmd.c・check.c・range.c・jobs.c・capset.c を直に呼ぶ: 各 encoder の dword・byte を ghci.md の表から手で書いた期待値と比べる、ring の余地と NOP の埋め（端・満・255 dw を超える残り）、fence の回り込み（0xFFFFFFF0 → 0x10）、EXTCAP の走査（正常、次の距離 0、環、窓の外、32 個の上限）、VA の allocator（断片、上限、page 0 を出さない）、PSO・descriptor の offset |
| `ghci2-model.c`・`.h` | **GHCI 2.0 の device の model**（zedBSD の tree の中、この WS が書く。p003 で core を書き、p009 で転送・query の command、p010 で MEM の local memory と window、p011 で DISP2 を足す）: 19 register と EXTCAP の窓（MEM・FMT・DISP2 を任意に）、host RAM と local memory の配列、doorbell で ring を読む CP（header の検査、dwords の範囲、折り返しの禁止、未知の opcode → fault kind 1、ring の範囲外 → kind 2）、`CMD_FENCE` の書き込み（完全: 先の command は model の中で同期に終わる）と IRQ、`CMD_*` の受け取りの記録（試験が列を照合）、VM（session ごとの VA → bus の表、VM_MAP・UNMAP、未 map の VA を読む・書く command → kind 3・4）、reset の §5 の保つ・消す register、HALT、fault・watchdog の注入、IRQ の level を driver の handler の呼び出しで、poll の device。G の推しを compile の定数（`MODEL_RING_UNITS_BYTES` など）にして、回答が違えば model と driver の 1 か所ずつを直す。**版 3**: capability の profile（§24.6: DEVICE_CAPS、`CMD_QUERY_CAPS` に block を答えるか未知の opcode として fault するか、block の 16 語、EXTCAP の有無と DISP2）を試験ごとに組む。既定の profile は EXTCAP 無し（slim）。flag の無い機能に当たる command（例: CAP_INDIRECT_DRAW の無い時の `CMD_DRAW_INDIRECT`、CAP_MESH_SHADER の無い時の 0x2A）を受けたら fault kind 1 にし、driver がそれを出さない事を試験が確かめる |
| `ghci2-stubs.c` | driver が使う kernel の service（lock、wait queue、時計、MMIO、`kern_pmem_*`、GPU core の `drv_gpu_*`、PCI の Bus Master）の stub。1 thread で、sleep は偽の時計を進め model を走らせる。今の `plan/ws288/tests/ghci-driver-host-test.c` の stub の部分（Zlib、同じ著作者）を移して直す。版 2 で足す物（R19・R02）: (1) **lock の順の記録**: 取った mutex を thread ごとの stack に積み、§8 の順（device.lock → display.lock → cp_lock）に反する取得と、cp_lock を持ったままの device.lock を FAIL にする、(2) **待ちの途中の注入**: sleep・wait の hook で、試験が「割り込み・fault・reset・別の session の submit」を差し込める（1 thread のまま競りの順を作る）、(3) `drv_gpu_register` は `gpu.c` の capability と ops の組の検査（1000〜1240 行）と同じ検査をする、(4) Bus Master の状態を model に伝え、Bus Master が切れている間の model の DMA を FAIL にする |
| `ghci2-driver-host-test.c` | driver の file（vk/ を除く）+ model + stub。attach から detach までの場面（§15.3 の p004・p005・p010・p011） |
| `ghci2-vk-host-test.c` | 上 + vk/ + codec。libvulkan と同じ wire の frame を codec で作って `command` に渡し、返事と model が受けた `CMD_*` の列・PSO・descriptor の byte を確かめる（今の `ghci-vk-host-test.c` の frame の組み立て方を移す） |
| `ghci2-host-test.sh` | 上の 3 つを `build/tmp/` の新しい directory で build（`-std=gnu11 -Wall -Wextra -Werror -Wdeclaration-after-statement -fsanitize=address,undefined`、今の script と同じ）して走らせ PASS/FAIL。rm はしない |
| `gsi1-host-test.*`・`gsi1-reference.*`・`gsi1/`・`gsi1-tools/` | compiler の回帰（§12、U2） |
| `config-amd64-ghci.mk`・`config-visionfive2-ghci.mk` | build の確認用 |

model は「GHCI 2.0 を driver の推しの読み方で動かす物」であり、2.0 の hardware の振る舞いの証拠ではない。試験の結果は「driver が ghci.md（と G の推し）に合う packet と register の順を作る」ことの証拠として書く。

### 15.3 Phase ごとの場面

| Phase | host 試験の場面（すべて PASS が受け入れ） |
| --- | --- |
| p002 | words: 全 encoder の期待値（`CMD_QUERY_CAPS` を含む）、ring・fence・EXTCAP・VA の境界。**caps.c**: DEVICE_CAPS の各 bit と PHYS_LANES・NUM_SU の decode、block の decode（版 0・短い block・知らない版）、重なる bit の AND と `mismatches`、lanes の不正な値、block 無しの既定 |
| p003 | model の自己試験: capability の profile（block を答える・fault する、EXTCAP 無し・有り）、手で組んだ ring の列（NOP・FENCE・未知の opcode・折り返し・範囲外）、reset の register の保つ・消す（§5 の 374〜377 行の表）、IRQ の level と W1C、VM の表（推しの G05・G06・G08 の形）、Bus Master が切れている間の DMA の検出、stub の lock の順の検出（わざと逆の順を取って FAIL を見る） |
| p004 | attach（版 3: VERSION 0x00010000 を受け、0x00010001（1.1 legacy）・0x00020000・0x00000000 を拒む、MAGIC 違い、HW_WALKER = 0 を拒む、NUM_SU = 0・GSI1 = 0 は attach して executor の capability を出さない、`CMD_QUERY_CAPS` の block 有り・無し（fault → reset → block 無しで続き submit が動く）、block と register の DEVICE_CAPS の食い違いの log、EXTCAP_PTR = 0 で attach が済む、SCRATCH の不一致、liveness の fence が来ない → `EIO`）、**FAULTED・RUNNING で残った device**（古い RING_BASE を持つ）を reset してから Bus Master を立てる（Bus Master の前の DMA が 0）、start の register の順（model の記録と照合）、`drv_gpu_register` の検査を通る capability、submit と fence と IRQ、poll の device、4 KiB の ring で 1000 回の submit（折り返し・満の待ち）、ring より大きい submit（cp_lock を離さない、別の session の submit を待ちの途中に注入しても間に入らない）、fault の注入 → reset → 古い ticket が ERROR → 次の submit が動く、watchdog、hang（fence が止まる）、reset の失敗 → LOST で物理を返さない、fault の嵐 → LOST、同じ session の fault 3 回 → その session だけ止まる、detach（session が有れば EBUSY、LOST でも Bus Master を切ってから返す）、lock の順の違反 0 |
| p005 | resource・blob の確保と VM_MAP（VA と bus が model の表に一致、COHERENT の flag）、map・read・write（UMA）、destroy → VM_UNMAP → fence の後に物理が返る（前には返らない）、**VM_UNMAP の置き損じ**（ring の満を注入）→ 物理を hold → service が置き直す、import（輸入先の番号で別の VA に map）、**2 つの session に同じ VA を出さない**、session の close が待たずに返り番号は `RETIRED` まで再利用されない、64 番目の open（session の上限）、VA の不足、4 GiB 以上の host memory を出さない、**ring より大きい再生**（多数の mapping で reset → RUN の後に RPTR を待ちながら再生、submit は再生の後）、再生の途中の fault、local memory の確保が ENOTSUP、lock の順の違反 0 |
| p006 | gsi1 の compiler の回帰（移した試験、amaterrace を読まない）、options が `struct ghci_caps` から埋まる（block 有り: LIMITS の registers 64・CAP_MSAA の samples、block 無し: 32・1）、LIMITS[16] SFU = 0 の profile で sin を使う program の compile が失敗し、1 で通る |
| p007 | vk: **flag ごとの差の試験**（§24.6: §24.3 の各行で `full` とその flag だけ落とした profile の答えの差が表の「有る時」の項目とちょうど一致）、profile `minimal`・`balanced`・`full`・`no-su`・`discrete`・`mismatch` の features・limits・format・queue・memory の型の答え、vkCreateDevice が答えより多い feature を求めると `VK_ERROR_FEATURE_NOT_PRESENT`、instance・physical device・device・queue の答え、memory・buffer・image の create と requirements、shader module、fence の signal と wait、capset、job marker、session の close の drain |
| p008 | 代表の pipeline（三角形・blend・depth・stencil・MSAA・compute）の PSO の 128 byte、descriptor table の byte、compile の失敗の返事。版 3: flag の無い profile で dual source・logic op・stencil・MSAA・compare の sampler・graphics（CAP_GRAPHICS = 0）の pipeline の作成が断られ、有る profile で通る |
| p009 | 版 3: CAP_INDIRECT_DRAW の無い profile で indirect の記録の submit が断られ、model に `CMD_DRAW_INDIRECT` が届かない。TRILINEAR の無い profile で mipmap の linear が nearest に落ちる（descriptor の byte）。vkdemo の 1 frame に当たる記録 → model の `CMD_*` の列と照合、compute の dispatch、indirect、barrier、copy・fill・clear（G22 の command か executor の compute）、query（G23）、bind の無い draw を拒む、大きな submit の分割、submit の後の fence と job、executor の heap が `resource_map` で map できない |
| p010 | discrete: local memory の確保が fill で 0 になる、upload・readback が copy と fence、heap 2 つ、boot framebuffer の範囲を出さない・window の開閉 |
| p011 | DISP2（G28 の回答の形）: mode、claim・present（shared・copied の copy）・queue・cursor（staging の copy）・hotplug・error・reset の後の数え直し（RUN の前、IDLE で TAKEN を読む）、detach で表示を消してから memory を返す |
| p012 | ghcictl の build（userland の対象 build）。試験の追加は無い |
| p013 | 全部の ws290 の host 試験、amd64・riscv64 の build の warning 0、`git diff --check` |

## 16. 他の platform と userland

- amd64（PCIe）: 受け入れの対象。
- riscv64 の VisionFive 2（M.2 の PCIe、WS288 p043 で build を通した）: build の list を同じに直し、build の warning 0 だけを受け入れに入れる（実行の確かめは無い、U4）。`CONFIG_GHCI_POLL` の poll の経路は host 試験で覆う。
- ZYBO・armv7: 今の driver に attach の code が無い（`zybo-attach.c` は WS288 design §6.1 の案のまま作られていない）。platform binding（§15.1）は書かない（U4、Future Work の候補 fw-ghci-platform-binding）。
- endian（R26）: 対象は little endian の platform（amd64・riscv64）だけ。ring の dword は native の `uint32_t` で書く今の `ring_put` を、little endian で書く小さな関数に替える（big endian の platform で build されても正しい値になる。build と試験は little endian だけ）。PSO・descriptor は byte で書く（§4.4）。
- userland: `ghcictl` は `list`（driver の名前・capability・display の mode）と `display` を残す（`display` は p011 まで「表示は未対応」）。`ghci-blit` は削除、`ghci-display` は残す（U6）。

## 17. ライセンスと転記

| 出典 | SPDX | 扱い |
| --- | --- | --- |
| amaterrace-gpu `docs/ghci.md`（4d12da1） | Zlib（文書の 5 行目、`LICENSE`） | register・opcode・byte の offset・符号の値を driver の header に使う。値の block ごとに `Values referenced: amaterrace-gpu docs/ghci.md (4d12da1) section N`。文章は写さず driver の言葉で書く |
| 同 `include/ghci2_regs.h`・`ghci_cmd.h` | Zlib（各 file の先頭）、著作者は zedBSD と同じ Awe Morris | **写さない**: `ghci_cmd.h` は `float` の field と struct の重ね（kernel は float を使わない、詰め物と endian）、`GHCI_MAKE_HEADER` が opcode を 8 bit に切るので VM_MAP（0x10110）が 0x10 になる。値を ghci.md と照合する時の参照にだけ使う |
| 同 `include/gsi1_isa.h` | Zlib | 今の `compiler/gsi1_isa.h` は 4d12da1 と header の 1 行以外同じ写し。残す。同期の時は SHA を phase.md に書く |
| 同 `tools/gsi1*.py` | Zlib | 試験の参照として写す案（U2）。写した file の先頭の SPDX と著作権の行を保ち、README に出典の SHA |
| 今の zedBSD の driver・試験 | Zlib | 移す・直す。WS288 の `ghci-driver-host-test.c` の stub の部分を `ghci2-stubs.c` へ移す |
| Vulkan の仕様の値（VkFormat・VkBlendFactor などの番号） | 仕様の事実 | 今の `vk-format.c` と同じく `Values referenced:` で出典を書く |

GPL の参照は無い。他の GPU の driver（amdrdna など）からは「分け方」だけを参考にし、code は写さない。

## 18. 実装の Phase の分割

8 LW ≈ 2〜3 時間。依存の「G」は §19 の回答（ユーザーの判断）。「推しで」と書いた G は、回答の前に推しで書き、回答が違えば決めた 1 か所（定数か 1 つの関数）を直す物。p002・p003 は回答なしで始められる（推しで書く）。

| Phase | 内容 | 依存 | 要る回答 | 受け入れ | host 試験 | LW |
| --- | --- | --- | --- | --- | --- | --- |
| p002 | 2.0 の語の層: `ghci-regs.h`・`ghci-cmd.h`・`cmd.c`（VM 以外の全 command と `CMD_QUERY_CAPS`、PSO・descriptor の encoder、2.0 の ring・fence・EXTCAP の関数）、**`caps.c`（版 3、§24.1〜§24.2）**。**build に入っている file（`check.c`・`ghci-words.h`・`cp.c` など）は変えない**: 2.0 の関数は `cmd.c` に別名（`ghci2_ring_plan` など）で足し、古い物は p004 で消して名前を整える（R18） | p001 | 推しで: G03・G11・G24 の大きさ、G41・G42・G45・G47・G48（block の形と decode） | words の試験 PASS。`CONFIG_DRIVER_PCI_GHCI=y` の amd64 の vmunix が今のまま build できる（新しい file は build に入れない） | `ghci2-words-host-test` | 12 |
| p003 | GHCI 2.0 の model と host の枠: `ghci2-model.*`（core: register・ring・FENCE・VM・reset・IRQ・fault の注入・Bus Master）、`ghci2-stubs.c`（lock の順の記録、待ちの途中の注入、`drv_gpu_register` の検査）、`ghci2-host-test.sh`。WS288 の 1.1 の試験の削除の list を Q1 へ | p002 | 推しで: G03・G05・G06・G08・G11（model の VM と ring の形）、G41・G43（capability の profile） | model の自己試験 PASS | p003 の場面 | 16 |
| p004 | core: identify（2.0）、pci-attach（Bus Master の順、FLR）、cp（ring・doorbell・fence・IRQ・poll・hang・watchdog・fault・reset の段 A・LOST・quarantine）、detach（Bus Master を切ってから返す）、fault の session の数え。1.1 の file（`firmware.c`・`native.c`・`state.c`・`blit.c`・`frame.h`・`packets.h`・`hcr.h`）と `vk/`・display・compiler を build から外し、**それらを呼ぶ所を消す**（`ghci.c` の `ghci_vk_node_bind`・display の ops、`cp.c` の `ghci_display_recover_*`・`ghci_vk_reset`・`ghci_vk_service`・`ghci_display_service`、`isa.c`。p006・p007・p011 で戻す）。`check.c` の 1.1 の関数を消し p002 の別名を戻す。capability は §4.3 の p004 の段 | p003 | G04。推しで: G02・G09・G11・G12・G13・G14・G15・G16・G30・G33・G34、G40（版）・G41（block の取得と fallback）・G43（EXTCAP 無し） | amd64・riscv64 の vmunix の build の warning 0、p004 の場面 PASS、**build に入る file** に 1.1 の register 名・command・STATE の code が無い（`grep`。vk/・display の 1.1 の code は p007・p011 で消すまで tree に残るが build に入らない） | p004 の場面 | 22 |
| p005 | memory と VM: 物理（host）、`vm.c`（device の 1 つの VA の allocator、mapping の状態、UNMAP_WANTED の置き直し、段 B の再生、`replay_pending`）、resource・blob・map・read・write（UMA）、share・import、遅延の解放、session の `CLOSING`・`RETIRED`。local は ENOTSUP | p004 | G04・G05・G06・G07・G08・G32・G36。推しで: G31（local は p010）・G38 | p005 の場面 PASS、build の warning 0 | p005 の場面 | 22 |
| p006 | compiler の接続: `isa.c`、options の出所、compiler を build に戻す、gsi1 の試験を移す（U2） | p002・p004（`struct ghci_caps`、compiler を外したのは p004） | G18・G19。推しで: G48（LIMITS） | gsi1 の試験 PASS（amaterrace-gpu を読まない）、build の warning 0 | p006 の場面 | 8 |
| p007 | executor の土台: `vk.h`・stream・node・object・reply・**`vk-caps.c`（版 3、§24.3〜§24.5）**・device・format（FMT の register を読まず表で）・memory（executor の heap を含む）・command（pool・begin・end）・queue（submit の空の command buffer、fence・semaphore）。`vk-engine`・`vk-relay`・`vk-session` の 1.1 の部分を削除の list に。capability に COMMAND・NOTIFICATION など（§4.3） | p005・p006 | 推しで: G17・G18、G41〜G48（§24 の表）。U8 は推しで | p007 の場面 PASS（flag ごとの差の試験を含む）、build の warning 0 | p007 の場面 | 30 |
| p008 | pipeline と descriptor: `vk-pipeline.c`（compile・program の配置・PSO）、`vk-layout.c`（set layout・pipeline layout・pool・set・update → table）、`layout.h` の直し | p007 | G19・G20・G24・G35 | p008 の場面 PASS | p008 の場面 | 18 |
| p009 | 記録と実行: `vk-target.c`・`vk-record.c`・`vk-execute.c`（bind の検査）・`vk-copy.c`（G22 の command が無い間の compute の copy・fill・clear）、draw・dispatch・indirect・barrier・copy・clear・query・event。model に転送・query の command を足す | p008 | G20・G21・G23・G25・G37。G22 は「専用の command か compute か」を決める（無ければ compute）。推しで: G27・G39 | p009 の場面 PASS、amd64・riscv64 の build の warning 0 | p009 の場面 | 36 |
| p010 | discrete: local memory（fill で 0、upload・readback の copy）、heap 2 つ、window と boot framebuffer。model に MEM の local memory と window を足す | p005・p009（copy・fill） | G31。G22 は p009 と同じ | p010 の場面 PASS | p010 の場面 | 12 |
| p011 | 表示: display・display-scan を 2.0 の表示の command に、IRQ の振り分け、copied の present と cursor の copy（executor の copy か CPU）、reset の段 A の数え直し・段 B の再開、detach の表示の停止、adv7511、display の capability。model に DISP2 を足す | p007・p009（copy）・p010（discrete の scan の image は local） | G28 は決定（§13.2 に従う）。推しで: G49（DISP_SET・FLIP の opcode、IRQ、DISP2 の在りか）・G13 | p011 の場面 PASS（DISP2 有り・無しの両方） | p011 の場面 | 22 |
| p012 | userland と UAPI: `GPU_OP_GHCI_*` と `CONFIG_GHCI_NATIVE` を外す、ghcictl を直す、ghci-blit を削除の list に、platform の vmunix.mk の最終の list | p004（ghcictl の display は p011 の後に文言を直す） | U1・U6 | userland の build（ghcictl）、amd64 の vmunix の build の warning 0 | — | 4 |
| p013 | 規約の全文の見直し: WS が変えた C の全部（driver・試験）を `plan/coding-style.md` の全文と照らす、`git diff --check`、全部の host 試験、amd64・riscv64 の build の warning 0、tree 全体（vk/・display を含む）に 1.1 の code が無い事の `grep`、stub の `drv_gpu_register` の検査を `gpu.c` と照らし直す、gsi1 の試験と `ghci2-host-test.sh` を master の Tools に登録を Q1 に依頼 | p002〜p012 | — | 規約の違反 0（例外は承認済みの物だけ）、全 PASS、warning 0 | 全部 | 10 |

合計 **212 LW**（12+16+22+22+8+30+18+36+12+22+4+10。版 1 は 176、版 2 は 196。版 3 で capability の decode・profile・Vulkan への写しと flag ごとの差の試験の分を p002・p003・p004・p006・p007・p009 に足した）。

依存の図（prerequisite → dependent）:

```
p001 → p002 → p003 → p004 → p005 → p007 → p008 → p009 → p010 → p011
                     p004 → p006 → p007
             p002 → p006
                     p007 → p011,  p009 → p011
                     p004 → p012
            p002..p012 → p013
外部（文脈、ここで実行しない）: G の回答（ユーザー）→ p004 以降（Phase ごとの「要る回答」）
```

WS の受け入れ（ws.md）との対応: 「1.1 の code が残っていない」= p004（build に入る core）・p007（vk/）・p011（display）・p012（UAPI）の後に tree 全体を `grep` で確かめる（p013）。「amd64 の vmunix の warning 0」= 各 Phase、最後は p013。「他の platform の扱い」= §16（riscv64 は build だけ）。「host 試験が到達目標の各項目を覆う」= §15.3。「規約の全文」= p013。

**G28（表示）**: 版 2 は「決まらない場合 p011 は uncleared」。版 3: ユーザーが決めた（§13.2 に従う）。p011 は G49 の推し（opcode を定数 1 つ）で進められる。

**回答を聞く順の案**（版 3 で G41 を先頭に足した）: (0) G41（capability を command で読むか。p002〜p007 の形を決める。推しで進むが違えば 3 か所を直す）→ (1) G04（fence の address、p004 が止まる）→ (2) G05・G06・G07・G08・G32・G36（p005）→ (3) G18・G19（p006）→ (4) G20・G21・G22・G23・G24・G25・G35・G37（p008・p009）→ (5) G28（p011）・G31（p010）。他（推しで進む物）はまとめて確かめる。

## 19. ghci.md の疑問（ユーザーへ）

事実はすべて amaterrace-gpu `docs/ghci.md` 4d12da1 の行。推しは driver の側の案で、ghci.md を直すかどうかはユーザー（と amaterrace-gpu の担当）の判断。「止まる Phase」はその回答が無いと受け入れられない Phase。重さ: **C**（draw・表示・VM が書けない）、**H**（driver の挙動が決まらない）、**M**（推しで進められるが確かめが要る）、**L**（文の直し）。

| ID | 重さ | 事実（節・行） | driver の推し | 止まる Phase |
| --- | --- | --- | --- | --- |
| G01 | L | 版の表記: 4 行「Version 1.0 — Draft」、181 行 GHCI_VERSION の major 2、§16（1928〜1939 行）は 1.1 の履歴だけ、§1.4（124〜131 行）は class 2・state register file を書く、§15.1 1906 行 `compatible = "ghci,ghci-1"` | 表題を 2.0、§16 に 2.0 の項、§1.4 を 2.0 に、compatible を `ghci,ghci-2` | なし |
| G02 | M | register の窓の大きさ: §2 149〜154 行「17 address bits（128 KiB）…最初の 64 KiB だけでもよい」、§3 169〜170 行「76 bytes…4 KiB の page」、§11.3 921 行「BAR0 は exactly 76 bytes」、§15.1 1896 行 128 KiB か 64 KiB、§15.2 1921 行 BAR0 128 KiB。EXTCAP（MEM・FMT・DISP2 の 256 byte）の register がどこに来るか（4 KiB に入るか）も書かれない。am-ws001-p007 は「BAR0 は 4KB/1ページ」 | BAR0 は 4 KiB 以上、EXTCAP の register はその中。driver は 4 KiB 未満を拒み、各 EXTCAP の offset + 大きさが BAR0 の中かを確かめる | p004（推しで進め、回答で確かめる） |
| G03 | H | ring の pointer の単位: §3.1 191〜192 行 RING_RPTR・RING_WPTR は「byte offset」、§6.1 422〜425 行は「dword index、n = size / 4」、`ghci2_regs.h` は byte | byte の offset（4 の倍数）。§6.1 を byte に直す | p002・p004 |
| G04 | C | packet の address の空間と session: RING_BASE（188 行）は「64-bit base address」で bus か GPU VA か書かれない。CMD_FENCE の fence_addr（610 行）、§9.1 771 行「fence memory is in host memory」。§11.2（841〜847 行）は VA を VM_MAP で作るが、ring の `CMD_DRAW` などがどの session の VA で解かれるかを決める欄が無い（header の flags は「Command-specific flags or sequence tag」470 行、VM_MAP の session_id 854 行、PTE の session tag 882 行）。§1.3 の用語は今も「Session: … named by the job tag of the client's packets」（120 行）・「GPU address: … 32-bit address space」（114 行）だが、2.0 の header（466〜470 行）に job tag の欄は無い。§15.2 1924 行は「GPU host addresses (section 11.2) are PCIe bus addresses」 | RING_BASE・ring・session 0（driver）の packet の address は bus address（IOVA か物理、翻訳しない）。各 packet の header の flags[7:0] を session の番号（0 = driver）にし、`CMD_*` の VA はその session の空間で解く。（代わりの案: `CMD_SET_SESSION` を足す） | p004（fence）・p005・p007〜p011 |
| G05 | C | VM_MAP・VM_UNMAP の符号: opcode 0x10110・0x10111（854〜855 行）は §7.1 の 8 bit の opcode（468 行）に入らない。§7.2 の表（478〜494 行）に無い。dwords（10・8）は `ghci_cmd.h` にだけ有り、`GHCI_MAKE_HEADER` は opcode を `& 0xFF` で切る（0x10・0x11 になる） | §7.2 に 8 bit の opcode を割り当てる（例 0x40 VM_MAP 10 dw、0x41 VM_UNMAP 8 dw、`ghci_cmd.h` の field の順） | p005 |
| G06 | C | session の生成と破棄: SESSION_OPEN・CLOSE は 1.1 の class 2（§7.5 620 行、§11.6）。2.0 の VM_MAP は session_id を取る（854 行）が session を作る・壊す・page table を返す command が無い（am-ws001-p004 の範囲 3 は「session の終了時に CP が自律的に解放」と書くが、終了を CP がどう知るか無い）。PTE の session tag は 6 bit（882 行）なので session は最大 64 か、数は capability で示されない。**方針は決まっている**: 2026-10-24 ユーザー「プロセスIDの使用開始、使用終了のオプションコマンドを追加し、未対応なら未対応と応答させる。…これらはCapabilityとして見える。」（`plan/master.md` 705 行、1.1 の時の決定）。問うのは 2.0 の command の形と、返事の無い 2.0 で「未対応」をどう知らせるか（G08） | 方針どおり `CMD_VM_SESSION_OPEN(session)`・`CMD_VM_SESSION_CLOSE(session)`（close は全 unmap と page table の解放、drain の後）を足す。対応の有無と session の数は DEVICE_CAPS か EXTCAP で示す（返事は要らない。driver は既定 63） | p005 |
| G07 | H | address の幅: §11.2 867〜870 行 GPU VA は 32 bit（10 + 10 + 12）、全部の command の VA は 64 bit（504〜505 行など）、VM_MAP の iova は u64（854 行）、PTE の物理は [31:12]（875 行、4 GiB まで）、§12.8 1338 行 shader の address は 32 bit | v2.0 は VA 32 bit（上位の dword は 0、0 でなければ fault）、VM_MAP の bus address は 4 GiB 未満。driver は host memory を 4 GiB 未満から取る。広げるのは次の版 | p005 |
| G08 | C | page table の在りか・容量・失敗・reset: §11.2 865 行「CP の internal firmware heap に」、しかし HEAP gpu-op（621 行）は 1.1。§11.3 909〜910 行・§1.2 95〜96 行は「OS の page table を直に辿る」と書き食い違う。VM_MAP が page table の不足で失敗した時の知らせ方が無い（2.0 に返事の仕組みが無い）。reset（§5 374〜377 行）で page table が残るか書かれない | page table は CP が持つ（OS の page table は辿らない）。容量の memory は driver が貸す command（例 `CMD_VM_POOL(bus, bytes)`、start と reset の後）、不足は fault kind 5 と syndrome。reset は page table を消し、driver が再生する。VM_MAP は ring の順で後の packet より先に効く（driver は map の完了を待たない）と明記する | p005 |
| G09 | M | `CAP_HW_WALKER` = 0（251 行）の GPU が VA をどう扱うか、`CAP_IOMMU`（250 行「platform の IOMMU が有効」）は host の性質で device が知り得ない | 2.0 は HW_WALKER = 1 を必須にし、driver は 0 を拒む。CAP_IOMMU は情報だけで driver は読まない（OS の DMA の API に従う） | なし（推しで進む） |
| G10 | M | TLB の無効化: §11.2 841 行「host に TLB の無効化を見せない」、しかし CTRL.TLB_FLUSH（269 行）と CMD_BARRIER flags[2]（594 行）が有る。いつ driver が使うのか無い | driver は使わない（VM_UNMAP の drain と無効化は CP、885〜889 行）。debug 用と明記 | なし |
| G11 | H | fence の幅: §9.1 762 行「32 bit、回り込みで比較」、607 行「Emits a 64-bit sequence counter」、CMD_FENCE は seq を lo 32 + hi 16 bit で運ぶ（612〜613 行、48 bit）。memory に何 byte 書くか（4 か 8）、揃えが無い | GPU は 8 byte（lo、hi を 0 で広げた物）を 8 byte 揃えの address に書く。driver は lo の 32 bit で比較し、hi に 0 を置く | p002・p004 |
| G12 | M | IRQ の mask の仕方: §9.2 781 行「handler は mask し…unmask」が IRQ_ENABLE か CTRL.IRQ_GLOBAL（268 行）か無い。§9.2 783 行 IRQ_SET.SOFT は 2.0 に無い | mask は IRQ_ENABLE。IRQ_GLOBAL は start で 1 のまま。SOFT の行を消す | p004（推しで進む） |
| G13 | H | CP の状態の field: §5 365 行「State (STATUS[19:16])」、§3.3 278 行 STATUS[31:5] は reserved。IDLE と RUNNING を host が区別できない。DISP2 の書き込みの規則（1748 行）は状態に依る | STATUS[19:16] に §5 の状態を置くと §3.3 に書く | p004（推しで進む）・p011 |
| G14 | H | fault の kind の番号と session: §3.5 300〜308 行（1〜7、kind 3 = MMU の read）、§6.2 434 行「ring error（kind 3）」、§10 788 行「kinds 1-8」・794 行「parse の kind 2, 3, 7」・797 行「watchdog は kind 4」、§11.6 1095 行「kind 1 と FAULT_INFO[9]、[31:24] は job tag」、§12.6 1283 行「ISA fault は kind 8」（§3.5 は 6）。§11.2 847 行「faulting session を FAULT_INFO に」とあるが §3.5 の layout に session の欄が無い | §3.5 の番号を正にし、他の節を合わせる。watchdog は IRQ_WATCHDOG だけ（kind を使わない）。SYNDROME の下位 8 bit を session の番号に（または欄を足す） | p004（番号は推しで）・p005（session の欄） |
| G15 | M | 回復の範囲: §11.2 847 行「offending client context だけを止める」、§10 791 行「recovery は装置全体」、§5 371 行 FAULTED を出るのは CTRL.RESET だけ、§10 795 行の STEP は 2.0 の CTRL に無い | v2.0 は装置全体の reset。session だけの回復は次の版。STEP の行を消す | なし（推しで進む） |
| G16 | M | watchdog と quiesce: §10 797 行 CTRL.WDT_EN・WDT_CYCLES、802 行 STATUS.AXI_STUCK、805 行 AXI_IDLE は 2.0 の register に無い。IRQ_WATCHDOG（290 行）は有る | watchdog は常に有効で上限は実装の値、AXI_STUCK を STATUS[5] に。driver は自分の hang の検出も持つ | p004（推しで進む） |
| G17 | L | 識別: 1.1 の VENDOR_ID・DEVICE_ID・ISA_ID・DEVICE_VERSION・GCLK_HZ（208〜213 行）に当たる物が 2.0 に無い。driver の名前・Vulkan の vendorID/deviceID・timestampPeriod に要る | IDENT の EXTCAP（vendor、device、版、clock）を足す。それまで driver は PCI の ID を使い timestamp を出さない | なし |
| G18 | H | limit と optional な ISA の bit: §12 は 1.1 の CAPS[19]・[24]・[25]・[26]・[28]・[29]・[30]（1130・1139〜1141・1276〜1279・1297・1394〜1397・1594・1645 行）と RESIDENT_WARPS[23:16]（1154 行）を指す。2.0 の DEVICE_CAPS（247〜259 行）は FP16 だけ。register 数・constant bank・texture slot・varying・LDS・warp の数（scratch、1531〜1536 行）・attachment の数の出所が無い | LIMITS の EXTCAP を足し、optional の命令の bit を DEVICE_CAPS[31:24] に。それまで driver は Vulkan 1.0 の最小の値と compiler の既定（32 register、bank 256、varying 16） | p006・p007 |
| G19 | C | program の渡し方: 1.1 の PROGRAM gpu-op（§12.10 1457 行）が無い。PSO の `*_code_va`（667〜669 行）が GSI1 の header を指すか code を指すか、program の検査（1551〜1556 行）の失敗の知らせ方、`CAP_GSI1`（256 行「native」）と §12.1 1126〜1128 行「shader unit が有れば必ず GSI1 を受ける」の関係、shader unit が無い GPU の見分け方（NUM_SU = 0 か） | `*_code_va` は program の binary の先頭（header）、8 byte 揃え、検査の失敗は fault（kind 6 と syndrome）。shader unit の有無は NUM_SU ≠ 0、GSI1 は常に受ける（CAP_GSI1 は native かどうかだけ） | p006・p008 |
| G20 | C | resource の結び方: §8.3（708〜744 行）は texture と sampler だけ。§8.4 748 行は「textures, samplers, and buffers」。buffer（uniform・storage・texel buffer）の descriptor、push constant の command、descriptor table の形（型の混在、1 要素の大きさ）、interface 表（§12.10 1493〜1508 行の set・binding）と table の index の対応、GSI1 の texture の slot（sub[3:0]、1254 行）と set 0〜3 × count の対応、CP が bank の語を書く元（1524〜1530 行「bound descriptor sets と push constants」）が書かれない | table は 32 byte の slot の配列（texture 32 B、sampler 16 B + 詰め物、buffer {VA 8 B、bytes 4 B}）、index = binding（driver が set layout から配置）。`CMD_SET_PUSH_CONSTANTS`（offset、bytes、データ）を足す。CP は interface の (set, binding) で slot を引く。texture slot k は「pipeline の k 番目の sampled image の entry」 | p008・p009 |
| G21 | C | render target: color・depth の attachment（address・pitch・format・samples）を結ぶ command も PSO の field も無い。load・store・clear の扱いも無い（PSO 662〜706 行、§7.2 の表） | `CMD_BIND_RENDER_TARGETS`（color 0〜3 と depth/stencil の {VA、pitch、format、samples}、render area）を足す。clear は G22 の fill か `CMD_CLEAR_ATTACHMENTS` | p009 |
| G22 | H | 転送の command: copy・fill・blit・clear・resolve・update が無い。しかし §11.1 821〜822 行・§11.3 935 行は「upload と readback は copy の command」、§12.11 1635 行は resolve を 1.1 の BLT に。driver は local memory の 0 埋め（他の session の data を見せない）と vkCmdCopy*・Fill・Update・Clear・Resolve に要る | `CMD_COPY_BUFFER`、`CMD_FILL_BUFFER`、`CMD_COPY_IMAGE`（linear の矩形）、`CMD_RESOLVE` を足す（VA で、session の空間で）。**来るまで driver は executor の compute program で copy・fill・clear・resolve を行う**（kernel の GSI1 の compiler で作る。amdrdna の executor が自分の program で copy・clear する形、`src/drivers/gpu/amdrdna/vk/vk-execute.c` 22〜29 行）。compute には G20（storage buffer の descriptor）と G21 が要るので、専用の command より遅いが止まらない（R16 で C から H に） | p009・p010（G20・G21 が答えられていれば compute で進む） |
| G23 | H | query・timestamp・event の待ち: occlusion query（Vulkan 1.0 core で必須）、vkCmdWriteTimestamp、vkCmdWaitEvents の command が無い | `CMD_QUERY_BEGIN/END`（occlusion の数を VA に）、`CMD_WAIT_MEM`（VA の語が値になるまで待つ）。timestamp は後（`timestampValidBits` = 0 で出さない）。SetEvent は executor の compute の書き込み（CMD_FENCE を使ってよいかは G39）。GPU が立てた event の WaitEvents は ring の順と CMD_BARRIER で満たせ、待ちの command は host の vkSetEvent を待つ時だけ要る | p009 |
| G24 | C | PSO: 大きさが 48 byte（92・399 行）と 128 byte（500・656・662 行、`ghci_cmd.h` も 128）で食い違う。欠け・食い違い: PSO の `vertex_stride`（670 行）と `CMD_BIND_VERTEX_BUFFERS` の binding ごとの `stride_bytes`（516 行）のどちらが勝つか、属性 → binding の対応が無い、属性の offset は 8 bit（676〜677 行）で 0〜7 の属性だけ（mask は 0〜15、673 行）。Vulkan 1.0 の必須の `maxVertexInputAttributeOffset` は 2047 以上（Vulkan 1.0 の Required Limits、repo の外の事実で未確認）なので 8 bit では足りない、属性の format は属性ごとに 4 bit（674〜675 行）で符号の表が無い、texture の format の符号（719 行）が無い、blend factor の表は「etc.」（694 行）で、`ghci_cmd.h` 94〜105 行は 0〜11（10 = SRC1_COLOR、11 = SRC1_ALPHA）と定め VkBlendFactor（10 = CONSTANT_COLOR、15 = SRC1_COLOR、全 19 個）と番号が違い CONSTANT_* が無い、stencil op の符号（689 行）、depth bias の形（547 行「float / 24.8 fixed」）、sample mask と alpha to coverage（§12.11 1570〜1578 行は 1.1 の STATE の語）、varying の数（VARYING_COUNT）、triangle fan、attachment ごとの blend | 128 byte。予約の 0x60〜0x7F（706 行）に属性の binding・offset 8〜15・sample mask・alpha to coverage・varying の数を入れる。属性の offset は 16 bit に広げる（2047 を入れるため）。stride は binding ごとの `stride_bytes` が勝ち PSO の `vertex_stride` は消す。属性の format は 4 bit では VkFormat の番号が入らないので、GHCI の属性の format の表（例 8 bit: R32_SFLOAT・R32G32_SFLOAT・…・R8G8B8A8_UNORM）を ghci.md に書く。texture の format・blend factor・stencil op・compare op は 8 bit に VkFormat（texture は 0〜184 で 8 bit に入る）・VkBlendFactor・VkStencilOp・VkCompareOp の番号をそのまま使う（`ghci_cmd.h` の blend factor の 0〜11 の表は VkBlendFactor に合わせて直す）。depth bias は IEEE の float の bit | p002（大きさ）・p008 |
| G25 | H | draw の indirect: 1 つの opcode（570 行）で indexed と非 indexed の区別が無い。flags[0]「count を buffer から」（578 行）なのに count の buffer の address が無い。BIND_INDEX_BUFFER の表（484 行）は「size」と書くが binary（518〜524 行）に無い | flags[1] = indexed。count の buffer は v2.0 では無し（flags[0] を予約に）。index buffer の size の dword を足すか表から消す | p009 |
| G26 | M | secondary と権限: `CMD_EXECUTE_COMMANDS`（596〜604 行）がどの VA 空間で、入れ子を許すか、中で VM_*・FENCE を許すか無い。§6.0 401〜403 行・§8.1 651 行は UMD が ring を書く前提 | ring は kernel だけが書く。secondary は呼んだ packet の session の VA で、VM_* と session の command は secondary の中で fault | なし（v1 は使わない） |
| G27 | M | cache の無効化: §11.5 975〜979 行は 1.1 の DRAW・LOAD_PROGRAM・INVALIDATE を指す。2.0 の CMD_DRAW・BIND_PIPELINE が暗黙に無効化するか無い。CMD_BARRIER の stage の bit の値（592 行「VF, VS, FS, CS, ROP, BLT」）は include にだけ | 暗黙の無効化は無し、CMD_BARRIER の flags で明示。stage の bit を §7.3 に書く（`ghci_cmd.h` の `GHCI_STAGE_*` の値） | p009（推しで進む） |
| G28 | C | 2.0 の表示: DISP2（§13.2）は DISP_SET・FLIP（1726〜1728・1753・1838 行）と IRQ_STATUS の bit 9〜12（1728 行）を使うが、2.0 の command 表（§7.2）に DISP_SET・FLIP は無く、IRQ は bit 3 の DISPLAY だけ（291 行）。VBLANK・FLIP・DISP_ERROR・HOTPLUG の区別も無い。`FB_BASE`・`FB_NEXT`・`CURSOR_BASE`（1770〜1790 行）と flip の record の address が bus か GPU VA（どの session の）かも無い（G04）。copied の present と cursor の staging は 1.1 では BLT で写した（転送は G22） | `CMD_DISP_SET`（{offset, value} の対、flags[0] で idle を待つ）と `CMD_FLIP`（plane、image、record、value、complete）を 1.1 の中身で 2.0 の opcode に足す。DISP2 の窓に IRQ_STATUS（W1C、bit 0 VBLANK・1 FLIP・2 DISP_ERROR・3 HOTPLUG）を置き、GHCI の IRQ_DISPLAY はその OR。record・`FB_*`・`CURSOR_BASE` の address は bus（session に属さない。driver の framebuffer も client の image も bus address で渡す） | p011 |
| G29 | M | 2.0 で有効な EXTCAP: §4（328〜340 行）は PERF（PERF0〜3 は 2.0 に無い）、DBG、CPFW、MMU（「後の版で予約」、2.0 は MMU を持つ）、PROT を挙げる。§11.6（986〜1118 行）の PROT は APERTURE・SESSION_OPEN・MEMORY・HEAP・KICK・FAULT_INFO[9] の 1.1 の仕組み | 2.0 で有効なのは MEM・FMT・DISP・DISP2（と G17・G18 で足す物）。PROT は 2.0 では廃止（session の VA が隔離）、PERF・DBG・CPFW は vendor の物と書く | なし |
| G30 | M | 初期化の順: Appendix A 1950 行 MEM_READY（2.0 に無い）、1954〜1955 行「INIT は GHCI 1.1+ で必須」（2.0 も含むと読める）、§15.2 1924 行「Bus Master と aperture が設定されるまで DMA しない」（aperture は 2.0 に無い） | 2.0 の初期化は READY → ring の register → IRQ → IRQ_GLOBAL | RUN、liveness は SCRATCH と CMD_FENCE（§14 1883〜1887 行）。INIT は無し。DMA の門は「Bus Master と CTRL.RUN」 | p004（推しで進む） |
| G31 | H | MEM capability と VM: LOCAL_BASE（948 行「GPU address」）と HOST_LIMIT（951 行「GPU addresses below HOST_LIMIT…reach the host」）は VA が有る 2.0 で何の address か。local memory を VM_MAP する時の `iova_or_phys` は LOCAL_BASE + offset か | LOCAL_BASE は VM_MAP の bus の側で local memory を指す address、HOST_LIMIT は VM_MAP で host を指せる bus address の上限 | p005（discrete）・p010 |
| G32 | C | page table は session ごとか、全体で 1 つか: §11.2 854 行は「the session's GPU virtual address space」、しかし PTE は session の tag（882 行）を持ち、PDI・PTI は 1 組（868〜869 行）、am-ws001-p004 の内部 register `MMU_PT_ROOT` は単数。session ごとの page table なら tag は要らない。全体で 1 つなら 2 つの session が同じ VA を使えない | どちらでも正しい形で driver を書く（device に 1 つの VA の allocator、§4.2）。ghci.md にどちらかを書く（推し: session ごと。tag は検査の二重化） | p005 |
| G33 | M | CP の状態ごとに受ける書き込み: §5 367 行 IDLE は「Slim MMIO register writes (RING_BASE, RING_SIZE, SCRATCH, etc.)」、doorbell（RING_WPTR）と IRQ_STATUS の W1C を受けるかが「etc.」で分からない。reset の後の再生と attach の順に要る | IDLE は RING_WPTR を受けて保つが CP は読まない（RUN で読み始める）、IRQ_STATUS の W1C と IRQ_ENABLE は全部の状態で受ける | p004（推しで進む、driver は RUN の後にだけ doorbell） |
| G34 | M | 割り込みの細部: `CTRL.IRQ_GLOBAL` が reset で保たれるか消えるか（§5 374〜376 行の両方の list に無い）。IRQ_ENABLE が 0 の bit の事象が IRQ_STATUS に立つか（§3.4 282〜284 行は「IRQ_ENABLE が 1 で…assert」と line の条件だけを書く） | IRQ_GLOBAL は reset で 0 になる（driver は reset の後に書き直す）。IRQ_STATUS は IRQ_ENABLE に依らず立つ（poll の device が fault を見られる） | p004（推しで進む） |
| G35 | H | CP が自分で取る memory: scratch（§12.10 1531〜1536 行「the command processor allocates … bytes of GPU memory」）を 2.0 でどこから取るか（1.1 の HEAP は 621 行で 1.1 の側）。G08 の page table の pool と同じ問い | G08 の pool の command で driver が貸す memory から取る。足りなければ fault kind 5 | p008（scratch の要る program） |
| G36 | H | VM_MAP・VM_UNMAP の約束: gpu_va・size・iova の揃え（ghci.md に無く、`ghci_cmd.h` の注だけ「4KB aligned」）、既に map された VA への VM_MAP（上書きか fault か）、一部だけ map された範囲の VM_UNMAP、size 0 | 揃えは 4 KiB、既に map された VA への VM_MAP は fault（driver は出さない）、VM_UNMAP は map された範囲と完全に一致する物だけ | p005 |
| G37 | H | bind の状態の範囲: `CMD_BIND_*`・`CMD_SET_*` の状態（PSO・vertex buffer・descriptor table・dynamic state）が session ごとか ring 全体か、別の session の packet で消えるか、reset で消えるか（§5 376 行「GPU pipeline states」を消す） | ring 全体で 1 つ、reset で消える。driver は command buffer ごとに bind を検査して必ず出す（§11.3）ので、どちらでも動く | p009 |
| G38 | M | VM_MAP の COHERENT・WC（861〜862 行、PTE 880〜881 行）と §11.4（968〜971 行「GPU は coherence を外さない、No Snoop を付けない」）の関係。COHERENT の無い mapping は snoop しないのか | host memory の mapping は常に COHERENT、WC は local memory の CPU の書き込みの最適化の印で GPU の振る舞いを変えない、と書く。driver は host を COHERENT で map | p005（推しで進む） |
| G39 | L | `CMD_FENCE` を device の fence 以外の値（event の 1・0、query の結果）に使ってよいか: §9.1 762〜765 行「ring ごとに単調に増える」 | 任意の address・値に使ってよく、単調の規則は driver が自分の fence の語に守る物と書く。答えまで driver は event に compute の書き込みを使う | p009（推しで進む） |

（注: ws.md の例「§3.1 の 19 register と §3.2 以降の説明の整合」は G02・G13・G16・G33・G34、「ring の pointer の単位」は G03、「版の表記」は G01、「PSO の 48 byte の配置」は G24、「VM の操作の command の形」は G05・G06・G36 に入れた。番号は G01〜G39 の 39 件（版 2 で review の R03・R20・R21・R25 から G32〜G39 を足した）。重さの内訳: C 10（G04・G05・G06・G08・G19・G20・G21・G24・G28・G32）、H 13、M 13、L 3。）

### 19.1 版 3: ce9725a での G01〜G39 の状態

ghci.md 4d12da1 → ce9725a（と 3af62ca の §6.0）で、上の各行の事実がどうなったか。**回答** = 推しか別の答えで決まった、**一部** = 一部が決まり残りがある、**変更** = 事実が変わり新しい G に移した、**未回答** = 事実が同じ。

| ID | 状態 | ce9725a で見た事 |
| --- | --- | --- |
| G01 | **回答（ユーザー、2026-10-27）** | 単一の GHCI 1.0 だけ。2.0 は誤り、1.1 の必須は無く CAP が足されていく（上の「G01 の回答」）。ce9725a の事実: 表題「Version 1.0 — Draft 0.2」、VERSION 0x00010000（181 行）、`ghci,ghci-1` は 1.0 と合う。新しい矛盾は G40 |
| G02 | 未回答 | §11.3 は今も「BAR0 MMIO Window: Exactly 76 bytes」（996 行）。EXTCAP の置き場所は G43 に含めた |
| G03〜G08 | 未回答 | §6.1（497〜498 行）は今も dword index、§3.1 は byte offset。VM_MAP・session・page table の記述は差なし |
| G09 | 一部 | DEVICE_CAPS[2] の config は `CONFIG_CP_AUTONOMOUS_MMU`（257 行）。`config.mk.template` は 0 を「Static address mapping or software walk」と書くが、ghci.md に driver の扱いは無い。推しのまま（0 は拒む） |
| G10〜G16 | 未回答 | 差なし |
| G17 | 未回答 | TO-BE に vendor・device・clock が無いまま。G41 の block の word 13〜15 を推す |
| G18 | 一部 | DEVICE_CAPS・CAPS・CAPS2 に MSAA・SUBGROUP・LDS_KB などの表ができた（§3.2）。ただし CAPS・CAPS2 は読めない（G41）、registers per lane などは flag が無い（G48） |
| G19 | 一部 | CAP_GSI1 は「Fixed (1)」（262 行）、CAPS[2] CAP_SU が有る（280 行）。program の渡し方（`*_code_va` が何を指すか）は未回答 |
| G20〜G27 | 未回答 | 差なし（PSO の表、§7.2 の command の表も同じ） |
| G28 | 回答（ユーザー） | ユーザー「G28は決まったのでは」= §13.2 に従う。§13.2 は差なし。残る欠けは G49 |
| G29 | 一部 | PROT は build で外せ、無い時は EXTCAP から外れ DEVICE_CAPS[24] = 0（§11.6 1092〜1100 行、§4 420〜425 行）。PERF・DBG・CPFW の扱いと TO-BE の EXTCAP（G43）は未回答。PROT は WS292 |
| G30〜G39 | 未回答 | 差なし |

### 19.2 版 3 で足した疑問（G40〜G49）

事実は ce9725a の行（include・RTL と書いた物はその file）。

| ID | 重さ | 事実（節・行） | driver の推し | 止まる Phase |
| --- | --- | --- | --- | --- |
| G40 | 閉じる（G01 の回答） | 版の付け方: TO-BE は VERSION 0x00010000（181 行）、legacy は 0x00010001（207 行）で同じ major 1。§16（2046〜2048 行）は「minor は使っていない offset に register・capability・command を足すだけ」と書くが、1.1 の窓（§3.1.1）は 1.0 の窓と offset の意味が全く違う。§16 の版の list は 1.1 だけ（2051〜2054 行）。Appendix A 1 は「major を確かめよ」（2061 行）なので major だけを見る driver は 1.1 legacy に attach する | TO-BE の major を 2 にする（または legacy を major 0 か別の MAGIC に）。§16 に TO-BE の項を足す。driver は回答まで 0x00010000 だけを受け、minor ≥ 1 を拒む（TO-BE の将来の 1.1 も拒んでしまう） | なし（推しで進む） |
| G41 | C | CAPS・CAPS2 の場所: §3.2（247 行）は「DEVICE_CAPS（0x008）、CAPS（0x020）、CAPS2（0x024）」と書くが、§3.1（188〜189 行）の 19 register では 0x020・0x024 は RING_BASE_LO・RING_BASE_HI（RTL の slim も同じ）。§3.2.2・§3.2.3 の flag（GRAPHICS・STENCIL・MSAA・SUBGROUP・LDS_KB など）を TO-BE で読む方法が無い | **capability を command で読む**: `CMD_QUERY_CAPS`（例 opcode 0x34、3 dw: header、block の bus address の lo・hi）で CP が 16 語の capability の block（§24.2 の表: DEVICE_CAPS・CAPS・CAPS2・FMT0・FEATURE・LIMITS・session の数・MEM・vendor・device・clock）を書き、後の CMD_FENCE で完了を知る。この command を知らない GPU は kind 1 の fault（driver は reset して DEVICE_CAPS だけで動く）。register を足す案（0x04C〜）は §3.1 の「Exactly 19」に反する | p002・p004・p007（推しで進む。違えば 3 か所を直す） |
| G42 | H | `include/ghci_regs.h`（ce9725a）と RTL（`rtl/gpu/cp/atg_cp_regs.v` の slim）は EXT_CAP_FMT0（0x080: BC1〜BC7）と EXT_CAP_FEATURE（0x084: LOGIC_OP・EARLY_Z・HW_CURSOR・COLOR_LUT・MAX_MRT[7:4]・DUAL_SOURCE・COARSE_SCAN・FAST_CLEAR）を持つが、ghci.md に無い（`grep` で 0 件）。76 byte の窓の外 | G41 の block の word 4・5 に置き、ghci.md §3.2 に bit の表を書く | p007（推しで進む） |
| G43 | H | slim の EXTCAP: §3.1 に EXTCAP_PTR（183 行）が有るが、窓は 76 byte（169 行）で EXTCAP の list と MEM・FMT・DISP2 の register の置き場所が無い。RTL の slim は EXTCAP_PTR に 0 を返す。§4 の MEM（local memory、§11.3）・FMT・DISP2（§13.2）を TO-BE で使えるかが分からない（G02・G29 を含む） | TO-BE の EXTCAP は任意。driver は EXTCAP に頼らない: FMT は flag と driver の表（§24.4）、MEM は G41 の block の word 8〜12、DISP2 は G49。EXTCAP が有れば DISP2 だけを探す | p005（discrete）・p010・p011（推しで進む） |
| G44 | M | `CMD_DRAW_MESH_TASKS`（0x2A、4 dw: group count x・y・z）は `include/ghci_cmd.h` と `ghci_frame.h` の `GHCI_VK_CMD_DRAW_MESH_TASKS`（137）にだけ有り、ghci.md §7.2（551〜569 行）に無い。task・mesh の program を PSO のどの field が指すか（§8.2 は vs・fs・cs の code_va）も無い。DEVICE_CAPS[30]・[31] はどちらも `CONFIG_SU_ENABLE_MESH`（269〜270 行） | §7.2・§7.3 に 0x2A を、§8.2 に task・mesh の code_va を足す。driver は WS226（header と libvulkan が VK_EXT_mesh_shader を持つ）まで使わない | なし（WS226） |
| G45 | M | 重なり・食い違う flag: DEVICE_CAPS[6] FP16 と CAPS[28] PACKED_FP16（どちらも `CONFIG_SU_ENABLE_FP16`）、DEVICE_CAPS[2] と CAPS[16] MMU、PHYS_LANES（8 bit の数）と CAPS[7:4] LANES_LOG2、NUM_SU と CAPS[2] CAP_SU、DEVICE_CAPS[25] TEX_FFILT と CAPS[26] TEX_EXT（どちらも `CONFIG_TU_ENABLE_FFILT` と書くが TEX_EXT は 3D・cube・depth compare・gather も含み、`config.mk.template` の `CONFIG_TU_ENABLE_3D_CUBE` は別。RTL は CAPS[26] に TRILINEAR を入れる）、DEVICE_CAPS[26] ROP_PACK と CAPS[30] CORE_RENDERING（どちらも `CONFIG_ROP_ENABLE_PACK`）、DEVICE_CAPS[5] と FEATURE[8] DUAL_SOURCE | 1 つの機能に 1 bit。driver は重なる bit の AND（弱い方）を使い食い違いを log（§24.1）。CAPS[26] は 3D・cube・compare・gather だけにし FP の filter は DEVICE_CAPS[25]、CAPS[30] は「Vulkan 1.0 core の rendering の state」（独立 blend・depth clamp など）だけにし format は DEVICE_CAPS[26] | p007（推しで進む） |
| G46 | L | TO-BE で意味の無い flag: CAPS[15:12] STATE_VERSION（state register file は TO-BE に無い）、CAPS[17] CP_FW・[18] CP_RELOAD（CPFW）、CAPS[1] CAP_BLT（TO-BE に BLT の command が無い、G22）、CAPS[10] CAP_WAIT_VSYNC（vsync を待つ command が無い、G49） | TO-BE では予約にするか、対の command（G22・G49）と一緒に定める | なし |
| G47 | M | 値の幅: CAPS2[31:28] LDS_KB は 4 bit なのに 16 KiB を挙げる（313 行、§11.5.1 1077 行）、[19:16] L2_WAYS の 16（310 行）と [23:20] TU_L1_KB の 16（311 行）も 4 bit に入らない。CAPS2[7:0] NUM_QUEUES（1〜3、308 行）に対し 19 register の ring は 1 本（am-ws001-p005 の `RING[i]_*` と同じ問題） | 4 bit の field は log2 の符号（0 = 無し、n = 2^(n−1) KiB など）にする。queue の数は ring が 1 本の間は 1 と書く | p007（推しで: LDS は 16 を超える値を 15 KiB で切らずに log2 と読むかを回答で直す） |
| G48 | H | flag の無い build の option（`config.mk.template`）: `CONFIG_SU_ENABLE_SFU`（0 = 「Software emulation / trap」、compiler が rcp・rsq・sin・cos を出せるか）、`CONFIG_SU_NREGS`（32・64、compiler の registers）、`CONFIG_SU_NWARPS`、`CONFIG_TU_ENABLE_TRILINEAR`、`CONFIG_TU_ENABLE_3D_CUBE`、`CONFIG_RX_MSAA_SAMPLES`（2 か 4。CAPS[29] は有無だけ）、`CONFIG_ROP_MRT_COUNT`（FEATURE[7:4] は include だけ、G42）。ghci.md に対応する bit が無い | G41 の block の LIMITS の語（[7:0] registers per lane、[15:8] 一度に動く warp、[16] SFU、[17] TRILINEAR、[18] 3D・cube、[21:19] 最大の sample 数の log2）。回答まで driver は registers 32、SFU・TRILINEAR 有り、3D・cube 無し、sample 1（CAP_MSAA が有っても数が分からないので 1） | p006・p007（推しで進む） |
| G49 | H | G28（ユーザーの決定で §13.2 に従う）の残り: §13.2（1841〜1843 行）は DISP_SET・FLIP を「§7.3 の micro-op」と書くが §7.3（TO-BE、571〜688 行）に無く、opcode・dword の形が無い。事象は「IRQ_STATUS の bit 9〜12」（1843 行）と書くが §3.4（340〜346 行）は bit 3 の IRQ_DISPLAY だけ。DISP2 の発見は EXTCAP（G43）。CAPS[3] CAP_DISP は「DISP か DISP2 の capability が窓に有る」（281 行） | `CMD_DISP_SET`（例 0x38: {offset, value} の対を n 組、flags[0] で idle を待つ）と `CMD_FLIP`（例 0x39: plane、image、record、value、complete。1.1 の FLIP の中身）を §7.2 に足す。IRQ_DISPLAY は DISP2 の窓の事象の register（W1C: VBLANK・FLIP・DISP_ERROR・HOTPLUG）の OR と書く。DISP2 の窓は EXTCAP（4 KiB の page の中）か G41 の block で offset を示す | p011（推しで進む。opcode は定数 1 つ） |

重さの内訳（版 3 で足した 10 件）: C 1（G41）、H 4（G42・G43・G48・G49）、M 4（G40・G44・G45・G47）、L 1（G46）。

参考（疑問ではない事実）: RTL（`atg_cp_regs.v`、ce9725a）の slim は DEVICE_CAPS[29:24] を 0（PROT・TEX_FFILT・ROP_PACK は常に 0）、NUM_SU を 1、CAPS[26] を TRILINEAR、CAPS[30] を 0 で組み、ghci.md の表と違う。driver は ghci.md を正にする（§17）。am-ws002・am-ws003・am-ws004 は ws.md で completed（ce9725a）。

情報（疑問ではない事実、ユーザーに伝える）:

- am-ws001 の ws.md の表は p004・p007 を completed と書くが、各 phase.md の状態は `in-progress`（`git show 4d12da1:plan/ws001/phase004/phase.md` と `phase007/phase.md` の 6 行）。2.0 の hardware がいつ有るかはこの driver の実機の確かめの時期に関わる。
- am-ws001-p005（複数の ring）は `RING[i]_*` を足す計画で、2.0 の 19 register の表と両立しない。driver は ring を 1 つの型に閉じておく（§1.2）。

## 20. ghci.md 以外の判断（Q1・ユーザーへ）

| ID | 判断 | 推し | 理由 |
| --- | --- | --- | --- |
| U1 | `include/uapi/gpu-op.h` の `GPU_OP_GHCI_*`（2016〜2065 行の注と 2066〜2076 行の enum）と `Makefile` の `CONFIG_GHCI_NATIVE`（310〜311 行の注、317・823 行）を消す。WS の driver の外の共有の file | 消す（p012、Q1 の承認で） | 使うのは 1.1 の driver・ghcictl・ghci-blit・WS288 の試験だけ（`grep` で確認）。libvulkan は使わない |
| U2 | gsi1 の試験の参照の python を amaterrace-gpu から写すか | `plan/ws290/tests/gsi1-tools/` に 4d12da1 の 4 file を写す | 読み取り専用を保つ（import が `__pycache__` を書く）、ws.md の「zedBSD の host 試験だけ」、同じ著作者の Zlib |
| U3 | p004〜p010 の間、driver は Vulkan も表示も出さない（capability を段で足す）。G28 が決まらない時に表示を WS の目標から外すか | 段で出す。表示は回答を待つ | CONFIG は既定 n、2.0 の hardware は無いので利用者への影響は無い |
| U4 | riscv64（VF2）は build だけ保つ。ZYBO・platform binding（§15.1）は書かない | 推しのとおり、fw-ghci-platform-binding を Future Work に | 2.0 の platform の board が無い |
| U5 | PROT（§11.6）を実装しない | 実装しない | 2.0 は session ごとの VA。PROT は 1.1 の aperture の上の仕組み（G29） |
| U6 | userland: ghci-blit を消す、ghcictl の diag・paint・fwload を消す | 消す | 1.1 の native・firmware の command |
| U7 | ring を kernel だけが書く（ghci.md §6.0 の UMD の記述と違う） | kernel だけ | §1.2・D1。安全（VM_MAP）と libvulkan を変えない Guardrail |
| U8（版 3） | capability の flag が無い時、Vulkan 1.0 の API で隠せない不足（`vkCmdDraw*Indirect` に CAP_INDIRECT_DRAW が要る、D24S8 か D32S8 の depth/stencil の必須の format に CAP_STENCIL が要る、`maxColorAttachments` の必須の最小 4 に MAX_MRT が足りない、`maxComputeSharedMemorySize` の最小 16384 に LDS_KB が足りない、など）をどう見せるか | Vulkan 1.0 を名乗ったまま、隠せる物は隠し、隠せない物は記録の検査・pipeline の作成で断って log する（§24.3）。不足を「Vulkan 1.0 不適合」として ghcictl の `list` に出す。名乗る版（1.0 か、WS226 の後の 1.x）と portability の扱いは WS226 で決める | ユーザーは amaterrace 側で「Vulkan 1.0は適合しなくてもよく、MSAAなどをcapabilityビットでオプションにしていきたい」（am-ws003 の出典、2026-10-27）と決めたが、zedBSD の driver が不足をどう名乗るかは決まっていない。WS290 の実装は推しで進められる（断る場所が 1 つの関数に集まる） |

## 21. 選ばなかった案と理由

| ID | 選んだ案 | 選ばなかった案 | 理由 |
| --- | --- | --- | --- |
| D1 | ring・PSO・descriptor は kernel が書く | ghci.md §6.0 どおり UMD（libvulkan の側の GHCI 専用の部分）が書く | libvulkan は汎用の Venus の client で変えない（Guardrail）。ring に VM_MAP が載るので client に書かせると隔離が壊れる。SPIR-V の compile は kernel（Guardrail） |
| D2 | 書き直し（core・vk を作り直し、compiler・jobs・capset・range・display の論理を残す） | 今の 1.1 の driver を「1.1 と 2.0 の両方」に広げる | ユーザーの決定「2.0 だけ」。1.1 の code（firmware.c 2071 行、vk-relay・engine）は 2.0 で意味が無い |
| D3 | amdrdna の executor の型（記録 → packet） | 今の relay の形を残し、firmware の代わりに driver の中に「仮想の firmware」を置く | 2.0 の command は Vulkan の object を前提にしない（PSO と table の VA）。二段の変換は無駄。amdrdna の形は zedBSD で実績がある |
| D4 | driver の header を ghci.md から起こす | amaterrace の `ghci2_regs.h`・`ghci_cmd.h` を写す | `float` の型と struct の重ね（kernel は float を使わない、詰め物・endian）、`GHCI_MAKE_HEADER` の 8 bit の切り捨て（G05）。値の正本は ghci.md |
| D5 | encoder は byte・dword を 1 つずつ書く関数 | `GhciPipelineState` のような struct を GPU memory に重ねる | coding-style（1 field 1 行、意味の comment）、詰め物と endian の不確かさ、host 試験で byte を比べやすい |
| D6 | v1 は記録を ring に全部展開 | `CMD_EXECUTE_COMMANDS` で kernel の secondary | G26 が未定。ring の大きさ（既定 1 MiB、16 MiB まで）で足りる。後で最適化にできる |
| D7 | CPU が書き GPU が読む物（ring・fence・PSO・table・program）は discrete でも host memory | discrete では local に置き copy | copy の command が無い（G22。compute の copy は使えるが、毎回の upload は無駄）。ghci.md の表（934 行）も ring・command は host。PSO・table・program も同じ扱いにすると discrete と UMA の差が確保の場所だけになる（§1.1 の目標） |
| D8 | host memory は連続の run（`kern_pmem_alloc_limited`） | `drv_dma_vector` の segment ごとに VM_MAP | user の map（`drv_gpu_mapping`）は 1 つの物理の範囲。今の確保は WS288 p006・p040 で動いた。不連続は将来の改善 |
| D9 | 試験の model を zedBSD の tree に書く（`plan/ws290/tests/ghci2-model.c`） | amaterrace の Verilator・firmware | ユーザーの決定。2.0 の RTL も無い |
| D10 | reset の後に VA を再生（RUN の後、段 B） | session を全部失敗にして client に作り直させる | reset で未完の仕事を持たなかった session は続けられる（未完の仕事を失った session は `report_session_error` で止まる、R13）。driver は mapping の list を寿命の管理のために既に持つ。G08 で「残る」と答えがあれば再生を外すだけ |
| D11 | 段で capability を足す（p004 は COMMAND 無し） | p004 から vk/ を stub で残す | stub の executor が COMMAND を出すと libvulkan が native の device を作りに来て失敗する。段の方が受け入れが明確 |
| D12 | display の論理（lease・queue・record・数え直し）を残し、送り方（G28 の command）と転送（copied・cursor の copy）を替える | display を 2.0 で書き直す | lease・queue・record の論理は DISP2 の register の意味に結びつき、2.0 でも同じ（§13.2）。転送は 1.1 の BLT が無いので executor の copy か CPU（R06） |
| D13 | VA の allocator は device に 1 つ | session ごとの VA 空間 | page table が session ごとか全体で 1 つかが未定（G32）。全体で 1 つでも session ごとでも正しい（R03） |
| D14 | executor の heap を session に 1 つ map し、PSO・table・program・一時の memory は sub-allocate | object ごとに VM_MAP・VM_UNMAP | VM_UNMAP は GPU 全体の drain を待つ（885〜889 行）。object ごと・submit ごとの unmap は全 session を止める（R14） |
| D15 | 1 つの submit は cp_lock を持ったまま全部置く | 分割の間に cp_lock を離す | 別の session の packet・reset が間に入ると bind の状態を取り違える、fence の無い半分の submit を待てない（R05） |
| D16 | Bus Master は reset の後に立てる | 今の順（割り込みの後すぐ） | 2.0 に aperture が無く、reset 前の古い RING_BASE へ DMA し得る（R07） |

## 22. 敵対的な自己点検（版 1。版 2 で 1・5 を直した）

1. **G の回答を待つ Phase が多すぎて WS が進まないのでは**: p002・p003 は回答なしで進む（推しで書く）。p004 は推しで進められる疑問（G02・G03・G09・G11〜G16・G30・G33・G34）と、fence の address（G04）が要る。G04 だけ先に聞けば p004 まで進む。§18 の「回答を聞く順の案」を使う。
2. **model が driver と同じ推しで書かれるので、試験が推しの誤りを見つけない**: そのとおり。model の役は「driver が自分の読み方で一貫しているか」で、ghci.md の解釈の正しさの証拠ではない（§15.2 の末尾に書いた）。解釈の正しさは G の回答と、将来の実機（am-ws001-p007 の後）の確かめ。
3. **1 session = 1 open で、session の数が 63（6 bit）に減る**: 今は 255。Keiland の client の数で足りるか。足りなければ G06 で数を capability にしてもらう。driver は `sessions_max` を数で持つ。
4. **kernel が全部の記録を ring に写すので CPU の負荷が大きい**: 1.1 も frame を staging に写していた。PSO・table は create の時に一度だけ焼く（submit の写しは command の dword だけ）。
5. **VM_MAP の完了を待たない**: in-order（474 行）に依る。CP が VM_MAP を後の packet と並べて処理しない事は ghci.md に書かれていない（§11.2 の drain は unmap だけ）。G08 の推しの文に「VM_MAP は ring の順で後の packet より先に効く」を入れた。
6. **reset で display を止めない**: DISP2 は GPU の reset で止まらない（1734〜1735 行）。p011 まで driver は scan の register を触らないので、reset の回復で表示は変わらない。
7. **discrete で local memory を出さない間、Vulkan の heap は host だけ**: 性能は出ないが正しい。p010 で足す。
8. **`kern_pmem_alloc_limited` の 4 GiB の上限で host memory が足りない**: PC の guest では 4 GiB 未満の memory は限られる。WS288 も同じ制約（aperture 32 bit）で動いた。G07 の回答で広げる。
9. **規約**: encoder を 1 command 1 関数にし、header の組み立ての macro（`GHCI_MAKE_HEADER` の類）は「domain の操作の名」を持つ関数に（coding-style §2 の macro の規則）。float は使わない（blend 定数の UNORM 化は整数で）。

## 23. design-reviewer の指摘と対応

design-reviewer（2026-10-26、版 1 の 2721c49fc に対して）: 28 件（Critical 1・High 6・Medium 17・Low 4）。本文は [design-review.md](design-review.md)。P1 は R02（`gpu.c` 1148〜1157 行）・R07（`pci-attach.c` 165〜178 行）・R12（`cp.c` 974〜997 行の注「cp_lock is held」）・R27（49 file、`compiler/` 10588 行）を自分で確かめた。全部を反映した（版 2）。

| 指摘 | 重さ | 対応 | 反映した所 |
| --- | --- | --- | --- |
| R01 再生の lock の順と ring の容量 | Critical | reset（段 A、cp_lock だけ、RUN まで）と再生（段 B、device.lock → cp_lock、RUN の後に RPTR を待つ）を分け、`replay_pending` で submit を待たせる | §5.4・§5.2・§8・§6.1、G33 |
| R02 p004 の capability が `drv_gpu_register` で拒まれる | High | p004・p005 は NOTIFICATION・FENCE を出さない。p007 で COMMAND・NOTIFICATION・FENCE・JOB と recovery を同時に。stub に `gpu.c` の検査 | §4.3・§15.2・p013 |
| R03 session ごとの VA の前提 | High | VA の allocator を device に 1 つ。G32 を足した | §4.2・§5.5・G32・D13 |
| R04 GPU がまだ触れる物理を返す経路 | High | mapping に UNMAP_WANTED、置き直し。LOST は物理を返さず quarantine。reset の成功の時だけ古い ticket の物理を返す。detach は Bus Master を切ってから | §5.4・§5.5・§5.7・§6.3・§7・§10 |
| R05 大きな submit の分割の間に他が入る | High | 1 つの submit は cp_lock を持ったまま RPTR を待って全部置く。前半を置いた後の timeout は hang | §5.2・§11.3・D15 |
| R06 表示は BLT に依る | High | copied・cursor は executor の copy か CPU。p011 の依存に p007・p009・p010、LW 16 → 22。G28 に address の空間 | §13・§18・G28・D12 |
| R07 Bus Master を reset の前に立てる | High | reset の後に Bus Master。FLR が有れば先に | §5.1・D16 |
| R08 FAULTED の device の SCRATCH | Medium | 読みだけの identify → reset → SCRATCH の順 | §5.1・§15.3 |
| R09 表示の数え直しが RUN の後 | Medium | 段 A の中、RUN の前の IDLE で | §5.4 |
| R10 detach が FAULTED・LOST・表示を扱わない | Medium | 表示を消す → HALT（RUNNING の時だけ）→ RESET → Bus Master を切る → 返す | §5.7 |
| R11 reset の失敗の数え方の食い違い | Medium | 「RESET が落ちない・READY が立たない」= 即 LOST、「成功後の嵐」= 3 回で LOST。待つ条件を統一 | §5.1・§5.4・§6.1・§10 |
| R12 今の code の説明の誤り | Medium | `fence_window_wait`・`ring_room` は cp_lock を持ったまま。cache clean は無く uncached の view | §5.2・§8・§9.3 |
| R13 session の報告の意味 | Medium | 未完の仕事を失った session は全部報告（sticky）。fault の session の特定は log と数えにだけ | §5.4・§10・D10 |
| R14 VM_UNMAP は GPU 全体の drain | Medium | session の executor の heap を一度 map して sub-allocate | §5.5・§7・§11.2・D14 |
| R15 G24 の推しの実現性と読み違い | Medium | 属性の format は GHCI の表、offset は 16 bit、stride は binding、blend factor の include との差を書いた | G24 |
| R16 copy・clear・event を待ちにしすぎ | Medium | G22 を H に、compute の copy・fill・clear（`vk-copy.c`）。WaitEvents は GPU の event なら barrier | §11.4・G22・§18 |
| R17 依存の抜け | Medium | p004 → p006、p007・p009・p010 → p011。p003 の推しの G を書いた | §18 |
| R18 受け入れの食い違い | Medium | p002 は別名で足し build の file を変えない。p004 の grep は build に入る file、vk/・display を呼ぶ所を消すと明記。tree 全体の grep は p013 | §18 |
| R19 試験が危ない所を覆わない | Medium | stub に lock の順の記録と待ちの途中の注入、model の拡張を各 Phase と LW に、場面を足した | §15.2・§15.3・§18 |
| R20 疑問の一覧の抜け | Medium | G33（IDLE の書き込み）・G34（IRQ_GLOBAL と IRQ_STATUS）・G35（scratch）・G36（VM_MAP の約束）・G37（bind の範囲）を足し、DISP2 の address は G28 に | §19 |
| R21 COHERENT・WC | Medium | host は常に COHERENT、WC 無し。G38 | §5.5・G38 |
| R22 bind の検査、executor の memory を見せない | Medium | 記録の時に bind を検査し欠ければ拒む。executor の heap は `resource_map` の外 | §11.3・§7・§10 |
| R23 1 つの session の fault が何度も reset させる | Medium | session ごとに fault を数え、3 回で止める | §5.4・§10 |
| R24 close の drain と番号の再利用 | Medium | close は待たず `CLOSING`、`RETIRED` まで番号を再利用しない | §5.5・§6.2・§7 |
| R25 CMD_FENCE を event に使う | Low | event は compute の書き込み。G39 | §11.4・G39 |
| R26 endian | Low | 対象は little endian。ring の dword は LE で書く関数 | §16・§9.3 |
| R27 出典・数の誤り | Low | 49 file、10588 行、header の差 1 行、278 行、607 行、gpu-op.h の範囲、合計の式 | §0・§3・§12・§17・G11・G13・§14・§20・§18 |
| R28 G04・G06 の根拠と既存の決定 | Low | 114・120・1924 行と 2026-10-24 のユーザーの決定（master.md 705 行）を引き、G06 は「形と未対応の知らせ方」だけを問う | G04・G06 |

版 2 で確かめていない事: R02 の拒否は `gpu.c` を読んだだけで実際に呼んでいない（p004 の host 試験の stub の検査で確かめる）。Vulkan の `maxVertexInputAttributeOffset` の 2047 は repo の外の知識（G24 に「未確認」と書いた）。

## 24. capability と Vulkan への対応（版 3）

出典: amaterrace-gpu `docs/ghci.md` ce9725a の §3.2.1（DEVICE_CAPS、249〜270 行）、§3.2.2（CAPS、272〜300 行）、§3.2.3（CAPS2、302〜313 行）、§4 の FMT の注（407〜418 行）、§11.5.1（1061〜1077 行）、§11.6（1092〜1100 行）、§12.9（1557〜1562 行）、§12.12（1778〜1783 行）。`include/ghci_regs.h`（ce9725a）の EXT_CAP_FMT0・EXT_CAP_FEATURE（ghci.md に無い、G42）。am-ws003（capability を build の option にした計画、`config.mk.template`）は flag の意味の参考にだけ使い、値の正本は ghci.md（§17）。

### 24.1 driver の型

```c
/* Where each word of the capabilities came from. */
#define GHCI_CAPS_FROM_REGISTER	0x1U	/* DEVICE_CAPS read at identify */
#define GHCI_CAPS_FROM_BLOCK	0x2U	/* the block of CMD_QUERY_CAPS (G41) */

/* The capabilities of one device, decoded once at attach and not changed after. */
struct ghci_caps {
	uint32_t sources;		/* GHCI_CAPS_FROM_* */
	uint32_t device_caps;		/* DEVICE_CAPS (section 3.2.1) */
	uint32_t caps;			/* CAPS (3.2.2), 0 without the block */
	uint32_t caps2;			/* CAPS2 (3.2.3), 0 without the block */
	uint32_t fmt0;			/* EXT_CAP_FMT0 (G42), 0 without the block */
	uint32_t feature;		/* EXT_CAP_FEATURE (G42), 0 without the block */
	uint32_t limits;		/* LIMITS (G48), 0 without the block */
	uint32_t lanes;			/* physical lanes per shader unit */
	uint32_t shader_units;		/* NUM_SU */
	uint32_t sessions;		/* sessions the CP takes (G06), 63 without the block */
	uint64_t local_base;		/* local memory (G43), 0 without */
	uint64_t local_bytes;
	uint32_t mismatches;		/* bits that disagree between the overlapping fields (G45) */
};
```

- `ghci_caps_decode_device(device_caps, caps)`（identify）と `ghci_caps_decode_block(words, dwords, caps)`（§5.1 の 7a）は `caps.c` の pure な関数（host 試験が直に呼ぶ）。
- 重なる bit（G45: DEVICE_CAPS[6] と CAPS[28]、[2] と CAPS[16]、PHYS_LANES と CAPS[7:4]、NUM_SU ≠ 0 と CAPS[2]、DEVICE_CAPS[5] と FEATURE[8]）は AND（弱い方）を使い、食い違った bit を `mismatches` に入れて attach の時に 1 回 log する。PHYS_LANES と LANES_LOG2 は 4・8・16・32 だけを受け、他の値は 8（ghci.md の既定）と読んで log。
- 判定はすべて `ghci_caps_*` の小さな関数（`ghci_caps_has_graphics(caps)` など）で行い、driver の他の file は `caps` の語を直に bit で調べない（一か所で変えられるように）。

### 24.2 capability の block（G41 の推し）

ce9725a で CAPS・CAPS2 は 0x020・0x024 と書かれるが、その offset は 19 register の表では RING_BASE_LO/HI（G41）。ユーザーの補足（「コマンドベースの通信に変化して、レジスタ群がばっさりなくなりました」）に合わせ、**register を足さず command で読む**案を推す:

- `CMD_QUERY_CAPS`（推し: opcode 0x34、3 dw）: dword 1・2 = block の bus address（64 byte 揃え、host memory）。CP は先の command の完了を待たずに block を書き、後の `CMD_FENCE` の前に書き終える（fence が block の完了を示す）。
- block（推し: 16 dword、little endian）:

| word | 中身 | 関係する G |
| --- | --- | --- |
| 0 | [15:0] block の版（1）、[31:16] block の dwords（16） | G41 |
| 1 | DEVICE_CAPS（register と同じ値） | G45 |
| 2 | CAPS（§3.2.2） | G41・G45・G46 |
| 3 | CAPS2（§3.2.3） | G41・G47 |
| 4 | EXT_CAP_FMT0（BC1〜BC7） | G42 |
| 5 | EXT_CAP_FEATURE | G42 |
| 6 | LIMITS: [7:0] registers per lane、[15:8] 一度に動く warp、[16] SFU、[17] TRILINEAR、[18] 3D・cube、[21:19] 最大の sample 数の log2 | G18・G48 |
| 7 | session の数（0 = 63 と読む） | G06 |
| 8・9 | local memory の base（discrete、G31 の address） | G31・G43 |
| 10・11 | local memory の bytes | G43 |
| 12 | local window の bytes | G43 |
| 13・14 | vendor・device の FourCC | G17 |
| 15 | core の clock（Hz） | G17・G23 |

- block が無い GPU（`CMD_QUERY_CAPS` を未知の opcode として fault kind 1 にする、または block の版が 0）では `sources` = REGISTER だけ。driver はその device を DEVICE_CAPS だけで動かす（§24.3 の「block 無し」の列）。
- 代わりの案（選ばない）: CAPS・CAPS2・FMT0・FEATURE を 0x04C 以降の register に置く。§3.1 の「Exactly 19 registers」とユーザーの補足に反し、増えるたびに窓が広がる。
- 推しが違う答えになった時に直す所: `cmd.c` の `ghci_cmd_query_caps`、`caps.c` の `ghci_caps_decode_block`、model の block の作り方（§15.2）の 3 か所。

### 24.3 flag → zedBSD での見せ方

「見せ方」の分類: **V10** = Vulkan 1.0 の feature・limit・format・queue・memory の型として kernel の executor（`vk/vk-caps.c`）が答える（WS290）。**V1x** = Vulkan 1.1 以降の core か extension で、zedBSD の header（Vulkan 1.0 の 137 command、WS224）と libvulkan に無いので WS290 では見せない（WS226 で見せる）。**DRV** = driver の内部だけ（Vulkan に見せない）。「無い時」: **隠す**（feature を false、limit を最小、format の bit を落とす）、**代替**（executor が別の手段で行う）、**断る**（vkCreateDevice の `VK_ERROR_FEATURE_NOT_PRESENT`、pipeline の作成や submit の失敗と log）、**拒む**（attach しない）。

DEVICE_CAPS（register、block が無くても読める）:

| bit | flag | 有る時 | 見せ方 | 無い時 |
| --- | --- | --- | --- | --- |
| [0] | CAP_UMA | 1 つの heap（DEVICE_LOCAL・HOST_VISIBLE・HOST_COHERENT） | V10（memory の型） | discrete: block の MEM が有れば heap 2 つ（p010）、無ければ host の heap 1 つ（HOST_VISIBLE・HOST_COHERENT、DEVICE_LOCAL は付けない） |
| [1] | CAP_IOMMU | 情報だけ（G09） | DRV（log） | 同じ |
| [2] | CAP_HW_WALKER | VA と VM_MAP | DRV | 拒む（G09） |
| [3] | CAP_INDIRECT_DRAW | `multiDrawIndirect`・`drawIndirectFirstInstance`、`maxDrawIndirectCount` = 2^32 − 1。`CMD_DRAW_INDIRECT` を使う | V10 | feature を false、`maxDrawIndirectCount` = 1。`vkCmdDraw*Indirect` は Vulkan 1.0 で隠せないので、記録の検査で submit を断る（log。Vulkan 1.0 で隠せない不足の扱いは U8） |
| [4] | CAP_SECONDARY_RING | `CMD_EXECUTE_COMMANDS` | DRV（v1 は使わない、D6） | 同じ（secondary は展開） |
| [5] | CAP_DUAL_SOURCE | `dualSrcBlend`、`maxFragmentDualSrcAttachments` = 1 | V10 | 隠す（0）。SRC1 の blend factor を持つ pipeline の作成を断る |
| [6] | CAP_FP16 | packed fp16 | V1x（`shaderFloat16`、VK_KHR_shader_float16_int8）。compiler は広げない（§12） | 隠す |
| [7] | CAP_GSI1 | executor（compiler は GSI1 だけ） | DRV | executor を出さない（COMMAND の capability を登録しない）。display だけの device |
| [15:8] | PHYS_LANES | 性能だけ（論理の warp は 32） | DRV（log）。subgroup の大きさ 32 は V1x | — |
| [23:16] | NUM_SU | 0 でなければ executor | DRV | 0: executor を出さない |
| [24] | CAP_PROT | WS292 | DRV（記録だけ） | WS292 で扱う |
| [25] | CAP_TEX_FFILT | 16・32 bit float の format に `SAMPLED_IMAGE_FILTER_LINEAR` | V10（format） | 隠す（その bit を落とす。nearest は残す） |
| [26] | CAP_ROP_PACK | 整数と 32 bit float の format に `COLOR_ATTACHMENT`（blend 無し） | V10（format） | 隠す（その bit を落とす） |
| [30] | CAP_TASK_SHADER | task shader | V1x（VK_EXT_mesh_shader の `taskShader`） | 隠す |
| [31] | CAP_MESH_SHADER | mesh shader、`CMD_DRAW_MESH_TASKS`（G44） | V1x（VK_EXT_mesh_shader の `meshShader`） | 隠す |

CAPS（block の word 2。block が無ければ全部 0 と読む = 「block 無し」）:

| bit | flag | 有る時 | 見せ方 | 無い時（block 有りで 0） | block 無し |
| --- | --- | --- | --- | --- | --- |
| [0] | CAP_CP | ring | DRV | 拒む | ring は有ると読む（liveness の fence が通った） |
| [1] | CAP_BLT | TO-BE に BLT の command が無い（G46） | DRV（使わない） | — | — |
| [2] | CAP_SU | NUM_SU と照合（G45） | DRV | executor を出さない | NUM_SU だけで決める |
| [3] | CAP_DISP | display を探す | DRV（§13） | display を出さない | DISP2 が見つかった時だけ |
| [7:4] | LANES_LOG2 | PHYS_LANES と照合（G45） | DRV | — | — |
| [8] | CAP_FILTER_BILINEAR | UNORM8 の format に `SAMPLED_IMAGE_FILTER_LINEAR` | V10（format） | 隠す（全部の format から落とす） | 有ると読む（版 2 までの既定と同じ。G41 の回答で確かめる） |
| [9] | CAP_STENCIL | D24S8 の `DEPTH_STENCIL_ATTACHMENT`、stencil の state | V10（format・PSO） | 隠す（D24S8 を出さず D32_SFLOAT だけ）。stencil test の有る pipeline の作成を断る | 有ると読む（同上） |
| [10] | CAP_WAIT_VSYNC | 表示の待ち（G46・G49） | DRV（p011） | 待たない（driver の vsync の事象で） | 同じ |
| [11] | CAP_TILED_TEXTURE | optimal の layout を tiled にできる | DRV（layout.h の選択。v1 は linear だけ） | linear | linear |
| [15:12] | STATE_VERSION | 1.1 の state file（G46） | 読まない | — | — |
| [16] | CAP_MMU | HW_WALKER と照合（G45） | DRV | 拒む | HW_WALKER だけ |
| [17]・[18] | CAP_CP_FW・CAP_CP_RELOAD | 1.1 の firmware（G46） | 読まない | — | — |
| [19] | CAP_NATIVE_ISA | native の ISA が GSI1 と違う | DRV（log。driver は GSI1 だけ使う） | — | — |
| [23:20] | LINE_SHIFT | `nonCoherentAtomSize`・`optimalBufferCopyOffsetAlignment`・`optimalBufferCopyRowPitchAlignment` と layout の行の揃えを line の bytes に | V10（limit） | — | 64 byte と読む（今の `GHCI_LAYOUT_ALIGN`） |
| [24] | CAP_SUBGROUP | `shfl`・`vote`・`ballot` | V1x（Vulkan 1.1 の subgroup の properties） | 隠す | 隠す |
| [25] | CAP_SA | systolic array | V1x（VK_KHR_cooperative_matrix、WS226 の後） | 隠す | 隠す |
| [26] | CAP_TEX_EXT | 3D・cube の image、`imageCubeArray`、`shaderImageGatherExtended`、sampler の `compareEnable` | V10（feature・image の format の答え） | 隠す。3D・cube の image は `vkGetPhysicalDeviceImageFormatProperties` で `VK_ERROR_FORMAT_NOT_SUPPORTED`、compare の sampler の作成を断る | 隠す |
| [27] | CAP_GRAPHICS | queue family に GRAPHICS | V10（queue） | queue は COMPUTE・TRANSFER だけ。graphics pipeline の作成を断る | **有ると読む**（版 2 までの driver の前提。G41 の回答で確かめる） |
| [28] | CAP_PACKED_FP16 | DEVICE_CAPS[6] と照合（G45） | V1x | 隠す | DEVICE_CAPS[6] |
| [29] | CAP_MSAA | `framebuffer*SampleCounts`・`sampledImage*SampleCounts` に 4（と LIMITS の数、G48）、`sampleRateShading`、`alphaToOne`、`standardSampleLocations` | V10（limit・feature） | 1 だけ。feature を false | 1 だけ |
| [30] | CAP_CORE_RENDERING | `independentBlend`・`depthClamp`・`depthBiasClamp`・`depthBounds`・`occlusionQueryPrecise`、varying 64 語（§12） | V10（feature） | 隠す。varying 16 語 | 隠す |

CAPS2（block の word 3）:

| field | 有る時 | 見せ方 | block 無し |
| --- | --- | --- | --- |
| [7:0] NUM_QUEUES | ring は 1 本（§3.1）なので queue family の `queueCount` は常に 1（G47） | V10 は 1 | 1 |
| [15:8] L2_CACHE_KB・[19:16] L2_WAYS・[23:20] TU_L1_KB・[27:24] VF_LINES | §11.5.1 | DRV（attach の log だけ） | — |
| [31:28] LDS_KB | `maxComputeSharedMemorySize` = KiB × 1024（符号は G47） | V10（limit） | 0（compute は shared memory 無し。Workgroup の変数を持つ program の compile を断る） |

EXT_CAP_FMT0・EXT_CAP_FEATURE（block の word 4・5、G42）:

| bit | flag | 有る時 | 見せ方 | 無い時・block 無し |
| --- | --- | --- | --- | --- |
| FMT0 [6:0] | BC1〜BC7 | 各 BC の format に sampled の feature。全部が有れば `textureCompressionBC` | V10（format・feature） | 隠す |
| FEATURE [0] | LOGIC_OP | `logicOp` | V10 | 隠す。logic op の有る pipeline を断る |
| FEATURE [1]・[9]・[10] | EARLY_Z・COARSE_SCAN・FAST_CLEAR | 性能だけ | DRV | — |
| FEATURE [2] | HW_CURSOR | display の cursor の plane（§13） | DRV（display の flag） | cursor を出さない |
| FEATURE [3] | COLOR_LUT | gamma の表（zedBSD の display の API に無い） | DRV（使わない） | — |
| FEATURE [7:4] | MAX_MRT | `maxColorAttachments`・`maxFragmentOutputAttachments`・`maxFragmentCombinedOutputResources` の色の部分 | V10（limit） | 1（Vulkan 1.0 の必須の最小 4 に足りない。U8） |
| FEATURE [8] | DUAL_SOURCE | DEVICE_CAPS[5] と照合（G45） | V10 | DEVICE_CAPS[5] |

LIMITS（block の word 6、G48）: registers per lane → compiler の `registers`、一度に動く warp → `maxComputeWorkGroupInvocations` の上限の計算（今の `vk-device.c` 499 行の「workgroup は shader unit の warp の大きさ」）、SFU → §12、TRILINEAR → 無い時は sampler の `mipmapMode` = LINEAR を NEAREST に落とす（Vulkan 1.0 に隠す feature が無いので代替。log）、3D・cube → CAPS[26] と AND、最大の sample 数 → CAP_MSAA の時の sample の数。block 無しは「registers 32、SFU・TRILINEAR 有り、3D・cube 無し、sample 1」。

### 24.4 format の feature

`vk-caps.c` の format の表は 1 行 1 format（VkFormat、optimal・linear・buffer の feature、要る flag）。答えは「表の feature のうち、要る flag が有る物」。表の元は今の `vk-format.c` の `fallback_features`（253〜282 行: RGBA8・BGRA8 の色、D24S8・D32_SFLOAT、32 bit float の vertex）に、ghci.md の format の規則（§4 407〜418 行、§12.9 1557〜1562 行、§12.12 1778〜1783 行）で足す:

| format の群 | 常に（flag 無しで） | 要る flag |
| --- | --- | --- |
| RGBA8・BGRA8・RGB565 の UNORM（ROP の Path 0） | SAMPLED_IMAGE、COLOR_ATTACHMENT、COLOR_ATTACHMENT_BLEND、TRANSFER（copy は executor） | FILTER_LINEAR に CAP_FILTER_BILINEAR |
| sRGB（Path 1）、16 bit float の色 | SAMPLED_IMAGE | COLOR_ATTACHMENT・BLEND に CAP_CORE_RENDERING（G45 の推しで）、FILTER_LINEAR に CAP_TEX_FFILT |
| 整数（UINT・SINT）、32 bit float の色（Path 2） | SAMPLED_IMAGE（nearest） | COLOR_ATTACHMENT に CAP_ROP_PACK、32 bit float の FILTER_LINEAR に CAP_TEX_FFILT |
| D32_SFLOAT | DEPTH_STENCIL_ATTACHMENT、SAMPLED_IMAGE | — |
| D24_UNORM_S8_UINT | — | DEPTH_STENCIL_ATTACHMENT に CAP_STENCIL |
| BC1〜BC7 | — | SAMPLED_IMAGE（と FILTER_LINEAR に CAP_FILTER_BILINEAR）に FMT0 の各 bit |
| vertex の format（R32〜R32G32B32A32_SFLOAT、R8G8B8A8_UNORM など） | VERTEX_BUFFER | — （属性の format の符号は G24） |
| storage image・storage buffer の format | 版 2 と同じ（shader の program で行う、FMT の注「driver が program で行う物は報告しない」） | — |

「flag の無い時」の format の bit を落とす規則は、ghci.md が FMT_OPTIMAL について書く規則（407〜418 行）と同じ結果になる（FMT の register を読む代わりに、規則を driver の表で行う）。

### 24.5 どこで行うか

- **kernel の executor（`vk/vk-caps.c`）が全部を行う**。libvulkan は汎用の Venus の client で、VkPhysicalDeviceFeatures・Properties（limits）・FormatProperties・ImageFormatProperties・QueueFamilyProperties・MemoryProperties を wire でそのまま運ぶ（今の `vk-device.c` の 128 行などの op）。libvulkan は変えない（Guardrail、§1.2）。
- device の extension の一覧は libvulkan が自分で作る（`userland/desktop/libvulkan/instance.c` 1139〜1169・1436〜1437 行の `supported_extensions`、`vkEnumerateDeviceExtensionProperties` 468 行）。WS290 は extension を足さない（V1x の物は header にも libvulkan にも無い）。
- vkCreateDevice が求めた feature が答えた物を超えれば `VK_ERROR_FEATURE_NOT_PRESENT`（今の `features_within`、`vk-device.c` 95 行）。
- **WS226 との境界**: WS226（ユーザー「すべてのGPUドライバについて、capabilityフラグとともに実装します」）は header を Vulkan 1.3 にし、libvulkan が 1.1〜1.3 の command と feature の構造体（`VkPhysicalDeviceFeatures2` の pNext など）と extension（VK_EXT_mesh_shader など）を device に問う形を作る。WS290 は (1) `struct ghci_caps` と §24.3 の表（V1x の行を含む）を driver の中に持ち、(2) V10 の行だけを答える。V1x の行を wire で答える op は WS226 の設計で決める（driver の表に「V1x」と書いた行が入力）。WS290 は V1x の行のために使われない code を書かない。

### 24.6 host 試験（§15.2・§15.3 に入れた）

- model は capability の **profile**（DEVICE_CAPS、block を答えるか、block の 16 語、EXTCAP の有無と DISP2）を試験ごとに組める。profile の例: `minimal`（DEVICE_CAPS = HW_WALKER | GSI1、NUM_SU 1、lanes 4、block 無し、EXTCAP 無し）、`balanced`（am-ws003 の KC705 の既定に当たる値の block）、`full`（全部の flag と block、DISP2 有り）、`no-su`（NUM_SU 0）、`discrete`（UMA 0 と MEM の語）、`mismatch`（G45 の重なる bit を食い違わせる）。
- **flag ごとの差の試験**（p007）: §24.3 の表の各行について、`full` と「`full` からその flag だけ落とした profile」の 2 回で vk の答え（feature・limit・format・queue・memory の型）を取り、差が表の「有る時」の項目と**ちょうど一致**する（他の項目は変わらない）ことを確かめる。表と試験は同じ data（`vk-caps.c` の表）から作らず、試験の側に期待の差を手で書く（表の誤りを表で確かめない）。
- 「断る」の行は、flag の無い profile で該当の pipeline・submit・vkCreateDevice が失敗し、有る profile で通ることを確かめる。
- `CMD_QUERY_CAPS` の両方: block を答える model と、未知の opcode として fault する model（reset の後に block 無しで attach が済む、submit が動く）。

## 25. 版 3 の自己点検と design-reviewer

自己点検（版 3）:

1. **block の推し（G41）が外れたら**: 直す所は `cmd.c`・`caps.c`・model の 3 か所（§24.2）。driver の他の部分は `struct ghci_caps` だけを見る（§24.1）ので、register で読む答えになっても同じ型に入れるだけで済む。
2. **block の無い device を「graphics・stencil・bilinear 有り」と読む**（§24.3 の CAPS の「block 無し」の列）: 版 2 までの driver の前提を保つため。compute だけの GPU で block が無いと graphics を誤って出す。G41 の回答が「block 無し」の device をどう扱うかを決める。
3. **attach で未知の opcode の fault を起こす**: block の取得が fault すると reset が 1 回増える。reset は §5.4 の段 A と同じ待ちで、session はまだ無い。liveness の fence の後に置くので「ring が動かない」と「block を知らない」を分けられる。
4. **Vulkan 1.0 で隠せない不足を断る**（U8）: アプリには submit・pipeline の失敗として見える。どこで断るかは `vk-caps.c` の「要る flag」の検査 1 つに集め、U8 の回答で名乗り方を変えても検査は同じ。
5. **flag ごとの差の試験が表と同じ誤りを持つ**: 期待の差は試験の側に手で書き、`vk-caps.c` の表から作らない（§24.6）。
