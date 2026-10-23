<!-- awesome-plan project=zedbsd record=ws288-p004 -->

# WS288 p004 CP の子 module（ring・sequencer・fence・割り込み・fault・reset）の RTL と p023 の bug 2 件

- 親: [ws.md](../ws.md)。設計: [design.md](../design.md) §1.3、§2.2〜§2.7、§7.6、§8 の p004 の行。前の Phase: [phase003](../phase003/phase.md)、[phase023](../phase023/specs-existing.md)（spec と RTL の差）。
- 担当: P3（2026-10-23、Q1 の依頼）。状態: 下の「結果」。clear の判定は Q1。
- 範囲（Q1 の依頼）: (1) spec/gpu/cp/ の `atg_cp_regs/state/ring/seq/fence/ctl/irq/dbg` を spec から実装し、`atg_cp` の inline の logic を移して `atg_cp` を結線だけにする（module ごとの TB）。ring は版 3 の封筒、完全な FENCE、watchdog、FAULT、RESET の順。class V は firmware が無いので FAULT 種別 2。(2) bug 1: RESET で BLT の設定が消える（STATE は残るのに BLT の写しが消える）。(3) bug 2: HALTED/FAULTED で DBG が MC の quiesce に止められる。(4) atg_top_tb の「FAULTED の時の STATE の書き込み」が mask 0 で比べていて何も見ていない。lint 0 warning、sim/run.sh・Verilator を通す、振る舞いの変わる spec を直す。最後に ZYBO の 2D の bitstream を作り直し、板で ghci-blt-board.py と ring/fence を確かめる。
- ユーザーの方法: 「トップダウンにVerilig実装を進めてください。スタブ、ドライバの使用は恐れずに、パズルのように部分的に進めてください。」

## 置き場所と commit

- amaterrace-gpu worktree `/home/awe/amaterrace-worktrees/p3-cp`（branch `p3-cp`、base main 1886a2c）: 4a4d2da（子の実装と結線）、4b46104（atg_top_tb の ring の試験）、434e2be・65cc445（unit の TB、`sim/models/amb_mem.v`、`sim/tb/hcr_snoop.vh`）→ Q1 が main に a094748 で merge。その後 30e110a（include の IRQ bit と `ghci_packets.h`、Verilator の ring）、3a124a7（main の merge）、b23507b（spec、atg_top から `UNITS`）、7f3bc8e（`docs/ghci.md`）。
- zedBSD worktree `/home/awe/zedBSD-worktrees/p3-ws288-p004`（branch `agent/p3-ws288-p004`）: `plan/tools/zybo/ghci-blt-board.py`（91e1aa0ff: CAPS 0x00501803・DEVICE_VERSION 0.3、ring の試験。Q1 の許可 2026-10-23「change it yourself」）、この phase.md。

## 決めた事（判断と理由）

- **bug 1 は「RESET の後に STATE を block へ流し直す（replay）」で直した。** design §2.6・ghci.md §4 は STATE が RESET を越えて残ると決めているので、STATE を消す案は採らなかった。`atg_cp_state` が RESET の pulse の後に STATE[0..511] を 1 cycle 1 word で `st_*` に流し、その間（513 cycle、100 MHz で 5.1 µs）`atg_cp_ctl` は RESETTING のまま。RAM は 2 つ持つ（copy A は host の alias、copy B は CPU の口と replay）。ghci.md §4 に「Amaterrace は RESETTING の間に unit へ流し直す」と書いた。
- **bug 2 は「CP の MC client 0 の要求が valid の間だけ `mc_quiesce` を下げる」で直した。** MC の `HIGH_MASK` は client 0 だけを最上位にしているので、その間 MC が選ぶのは client 0 だけで、block（BLT）は quiesce のまま止まっている。DBG は HALTED と FAULTED でも答える（ghci.md §4 の表も FAULTED に debug port を足した）。atg_mc.md に例外を書いた。
- HALTED の条件: quiesce は HALT ですぐかかり（p003 の選択: block は次の memory access で止まる）、HALTED は `mc_idle` かつ ring が packet の境目の時。一度 HALTED になったら DBG などで memory が動いても HALTED のまま。sequencer の `halt` は FAULTED と RESETTING だけ（ring が境目で止まるので、渡し済みの packet は最後まで走る。STEP の 1 packet もこれで走る）。
- port と parameter の変更（Q1 の ACK 2026-10-23）: `atg_cp` に `rsp_tag`、`UNITS`・`HAS_CPU`・`HAS_VSYNC`、`DEVICE_VERSION` 0.3。ring に `core_rst_n`・`CLASS_V`、irq と dbg に `core_rst_n`（dbg は rst_n に core を繋ぐ）・`dbg_busy`、ctl に `ring_busy`・`replay_busy`・`aperture_base/limit` と `LINE_SHIFT`・`UNITS`・`SRC_BLOCK`・`SRC_CLIENT`、state に `replay`・`replay_busy`、seq に `UNITS`・`HAS_VSYNC`、fence に `POLL`、regs に `PERF_SRC`。`am_amb_mux` も実装した（P1 の stub。Q1 が P3 の担当にした）。atg_top は `UNITS` を `HAS_*`/`NSU` から作って渡す。
- CAPS[0] = 1、CAPS2 = 1（ring 1 本）。ISA_ID は SU がある時だけ "GSI1"。

## 作った物（amaterrace-gpu）

| path | 中身 |
| --- | --- |
| `rtl/gpu/cp/atg_cp.v` | 結線だけ（369 行）。子 8 つと `am_amb_mux`（ring > fence > dbg、tag の [7:6]） |
| `rtl/gpu/cp/atg_cp_regs.v` | 識別・CAPS・EXTCAP の list（2 entry 目まで 0x100/0x108、3 つ目から 0x180）・VEND・SCRATCH・COUNTER・PERF、snoop と alias |
| `rtl/gpu/cp/atg_cp_state.v` | STATE、lock、µop 優先、CPU の口、replay |
| `rtl/gpu/cp/atg_cp_ctl.v` | CP の状態機械（8 状態）、CTRL/STATUS、KICK、locked、watchdog、AXI_STUCK、aperture、FAULT_* |
| `rtl/gpu/cp/atg_cp_ring.v` | RING_*、版 3 の封筒（class 0/1/2/3）、doorbell の stale の規則、fault 種別 3・2・1、RETIRE |
| `rtl/gpu/cp/atg_cp_seq.v` | class N の 14 µop と DRAW_RAW・DBG_MARK、待ちの規則、fault 種別 2・3 |
| `rtl/gpu/cp/atg_cp_fence.v` | FENCE・RESULT・WAIT_FENCE（poll 64 cycle）、FENCE_LAST |
| `rtl/gpu/cp/atg_cp_irq.v`・`atg_cp_dbg.v` | IRQ [8:0]（RING_EMPTY は立ち上がり）、debug port |
| `rtl/common/am_amb_mux.v` | 固定優先、ready 待ちの間の lock、tag で応答を戻す |
| `sim/tb/*_tb.v` | 9 本の unit TB（spec の §8 の PASS 名）。`sim/models/amb_mem.v`（AMB の memory model）、`sim/tb/hcr_snoop.vh` |
| `sim/tb/atg_top_tb.v` | CAPS・版、mask 0 の穴の修正、FAULTED と HALTED の DBG、RESET の後に残った STATE で FILL（replay）、ring の試験一式（下） |
| `sim/verilator/harness.cpp` | ring で FILL と FENCE、割り込み（`ghci_packets.h` を使う） |
| `include/ghci_packets.h`・`ghci_regs.h/.vh` | 封筒と µop の番号、IRQ [7] RESULT・[8] RETIRE |
| `spec/gpu/cp/*.md`・`spec/common/am_amb_mux.md`・`spec/gpu/mc/atg_mc.md` | 変えた port と振る舞い、§10 Status |
| `docs/ghci.md` | §9 command set（封筒と µop）、§4 の HALTED/FAULTED の DBG と replay、IRQ [8:7]、Appendix |

## 確認（host。QEMU は使わない）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| lint | `tools/lint.sh`（Verilator 5.032） | merge の後（3a124a7 + b23507b）: FAILED 0（全 module と atg_top の全 shape が clean） |
| unit と system の sim | `sim/run.sh`（iverilog） | merge の後 17/17 PASS（am_amb_mux、atg_cp_ctl/dbg/fence/irq/regs/ring/seq/state、atg_top ほか） |
| seed | `SEED=2/3/42 sim/run.sh atg_top atg_cp_ring am_amb_mux` | 全 PASS（ring と mux の TB は固定の seed） |
| Verilator | `sim/verilator/run.sh` | PASS（ring の FILL と FENCE を含む） |
| TB の感度（mutation） | RESULT の 2 word の順を入れ替え / ring の stale の判定を外す / harness の期待色を 1 bit 変える | それぞれ atg_top / atg_cp_ring / verilator が FAIL（戻して PASS） |

atg_top_tb の ring の試験: SET_REG+BLIT の FILL と FENCE（IRQ FENCE と RETIRE、RPTR = WPTR、RING_EMPTY）、class 3 の詰め物で環の端 → 0 で RESULT・WAIT_IDLE・DBG_MARK・FENCE、WAIT_FENCE が DBG の書き込みで解ける、HALT 中の doorbell と STEP で 1 packet ずつ、HALTED での DBG、RUNNING の KICK（6）と STATE（7）、class V（firmware 無し、2、address と tag）→ RESET → ring を 0 から（回復）、WPTR を越える packet（3）、aperture の外の fence（1、client 0）、watchdog（4）。ZYBO と KC705 の形の両方。

## Vivado（ZYBO 2D、`tools/vivado-build.sh zybo atg_zybo_2d`、Vivado 2026.1）

- 最初の build（merge の後の 3a124a7 + b23507b）は **WNS −1.301 ns**: ring の状態 → `cur + k` と `base + 4 × want` の 2 つの加算 → line の比較 → `n_valid` → seq → `uop_we` → `st_we` → BLT の state の CE（19 段）。ring の「欲しい dword の address」を register（`pa`、packet の中は +4）にして加算を外した（46cd2de）。
- 46cd2de: **WNS +0.191 ns、WHS +0.080 ns**（100 MHz）、LUT 5011（p003 は 3802）、FF 4483、BRAM 1 tile（STATE の 2 つの BRAM18）。CP 1750 LUT（ring 446、dbg 424、fence 278、regs 252、ctl 148、seq 116）、BLT 2748、MC 307。最も遅い path は ring の `pa` → BLT の state の CE（9 段）。bitstream sha256 cf3588b252c7c18a…（2083942 byte、`build/vivado/atg_zybo_2d/` は git に入れない）。
- 46cd2de の後も lint FAILED 0、`sim/run.sh atg_cp_ring atg_top` PASS（全体の 17/17 は b23507b で確認、46cd2de は ring だけの変更）。

## 板（ZYBO、実機。2026-10-23）

- SD 起動の ZYBO（10.0.30.2、root/root）、host の lock `/tmp/zedbsd-zybo-board.lock` は script が持つ。driver 無し、`fpgaload` と `fpgareg` だけ。
- `plan/tools/zybo/ghci-blt-board.py --bit atg_zybo_2d-p004.bit`（46cd2de の bitstream）: **3 回とも全 check PASS**（seed 1792761444・1792761583・1792761706。2・3 回目は `--bit` 無しで同じ bitstream のまま）。p003 の check（識別、DEVICE_VERSION 0.3、CAPS 0x00501803、EXTCAP/PERF、aperture、DBG、fill/copy/16 bpp/blend/tile/untile/重なりの 11 の blit、PERF、IRQ_SET、HALTED の IRQ、aperture の fault → RESET）に加えて p004 の check:
  - ring の FILL（SET_REG 11 組 + BLIT）と FENCE: `fpgareg wait` が `irq count 1 status 0x00000105`（FENCE + RING_EMPTY + RETIRE）、FENCE_LAST = 値、fence の word を DBG で読んで一致、RPTR = WPTR（27）、FILL の窓が model と一致。
  - HALTED で DBG の読み（bug 2 の修正の実機の確認）。
  - firmware 無しの class V → FAULT（`status 0x00000002`、FAULT_INFO 0x5c002000 = tag 0x5C・種別 2、FAULT_ADDR = header の address）→ RESET → WPTR 0 → ring を 0 から使って FENCE が通る（回復）。
- bug 1（RESET の後の STATE）は板では「reset の後の fill」が SRC/CLIP を書き直さずに通る事でしか見ていない（直接の確認は atg_top_tb）。
- 未実施: watchdog・WAIT_FENCE・STEP・RESULT・wrap は板では流していない（sim だけ）。KC705 は未実施。QEMU は使っていない。

## P1 の router の木（MC）との統合（2026-10-23、Q1 の依頼）

- Q1 の `int-rt`（b9d7d00 = a094748 + P1 の milestone 4: MC が 2 分岐の router の木）を p3-cp に merge した。merge したまま（a094748 の CP）では bug 2 の修正が効かず、atg_top_tb は「DBG stays busy」で落ちていた。木の MC では quiesce が root で止めるので、root の組み立ての buffer に入った BLT の要求の後ろで CP の要求が待つ。
- 直し方（98885bf）: quiesce で root を止めるのをやめ、**葉で止める**ようにした。`am_amb_to_rt` に `hold`（新しい packet を始めない。header を送り済みの write は data の flit も送るので、router が packet の残りを待って詰まる事は無い）、`atg_mc_tree` に `hold[NCLIENT]`、`atg_mc` に `QUIESCE_EXEMPT` を足し、hold = quiesce & ~QUIESCE_EXEMPT とした。木の中に入っている要求は root が grant する（流し切る。`am_axi_master` の halt は 0 に固定）。`axi_idle` は `pend`（client から受け取って、まだ grant していない要求の数）が 0 であることも要る。atg_top は client 0（CP）を除外にする。atg_cp の `mc_quiesce` は quiesce そのままに戻した（HIGH_MASK を使った細工は外した）。atg_mc_tb の quiesce の検査は「quiesce の窓の中で 1 client が受け取られるのは最大 1 回（write の data の flit）」にした。`atg_mc_arb.v`・`.md` を `git rm` で消した（Q1 の指示）。spec: atg_mc・atg_mc_tree・am_amb_to_rt。
- 確認: lint 97/97 clean（atg_mc_arb を除いて 98 − 1）、`sim/run.sh` 18/18 PASS、atg_top・atg_mc が seed 2/3/42 で PASS、Verilator PASS。mutation: QUIESCE_EXEMPT を 0 にすると atg_top が FAIL（戻して PASS）。
- Vivado（98885bf）: **WNS +0.570 ns、WHS +0.026 ns**、LUT 5027（CP 1270、MC 763 = 木 436 + aperture 259、BLT 2811）。bitstream sha256 fc682ba06cee2e5c…。
- 板（ZYBO 実機、98885bf の bitstream）: `ghci-blt-board.py` が **3 回とも全 check PASS**（seed 1792762754・1792762881・1792763004）。p004 の ring の check・HALTED の DBG・class V → RESET → 回復を含む。

## 残り・制限

- HALTING は block の非同期の仕事の終わりを待たない（p003 の選択、block は凍る）。RESET は RUNNING から HALTING を経ない。watchdog と BLT の error の JOB_TAG は 0。MC の refusal の FAULT_INFO[23:16] は client 番号（design とは差、spec に記録）。
- seq の DRAW/DISPATCH/LOAD_CONST/DRAW_RAW/WAIT_VSYNC は busy の model でしか試していない（VF・SU・RX は後の Phase）。`HAS_VSYNC` は atg_top から渡していない（DC の Phase で）。
- class V の CPU への受け渡し（`v_*`）は ring の TB の model だけ（atg_cpu は P2、p017）。
- ring は 1 line の buffer で先読みをしない（性能は後で）。
- KC705 の外の client（DC）は quiesce で止められたまま（以前と同じ）。DC は scanout を続けるべきだが、`pend` が DC の要求も数えるので、除外するには AXI_IDLE の数え方も一緒に直す必要がある（DC/KC705 の Phase で）。
