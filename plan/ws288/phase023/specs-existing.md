<!-- awesome-plan project=zedbsd record=ws288-p023 part=2 -->

# WS288 p023 part 2: spec files of the already implemented modules

- Parent: [phase.md](phase.md) (P1, part 1), [ws.md](../ws.md), [design.md](../design.md) §7.6 (template, U37, U38).
- 2026-10-23 P2. amaterrace-gpu worktree `/home/awe/amaterrace-worktrees/p2-p023` (branch p2-p023, base main a217bf8), commit **99be12c** (spec only; no RTL change).
- Scope (Q1): only the modules that already exist; P1 writes the specs and empty modules of the new ones. The atg_cp and atg_top specs describe the RTL at a217bf8 and say that their port lists grow in part 1 (P1's spec/README); Q1 reconciles on merge.
- Language: English, like the rest of amaterrace-gpu (docs/ghci.md, docs/style.md). Section headings follow the 10 sections of design §7.6 in English. If P1 writes Japanese, Q1 decides which to keep.

## Files (amaterrace-gpu `spec/`, mirroring the RTL paths)

| spec | RTL | lines | ports | U38 |
| --- | --- | --- | --- | --- |
| spec/common/am_sync.md | rtl/common/am_sync.v | 43 | 4 | ok |
| spec/common/am_fifo.md | rtl/common/am_fifo.v | 66 | 9 | ok |
| spec/common/am_afifo.md | rtl/common/am_afifo.v | 111 | 10 | ok (two clocks: it is the CDC primitive) |
| spec/common/am_axi_regs.md | rtl/common/am_axi_regs.v | 173 | 33 | ok |
| spec/common/am_axi_master.md | rtl/common/am_axi_master.v | 293 | **45** | ports (AXI 29) |
| spec/gpu/mc/atg_mc.md | rtl/gpu/mc/atg_mc.v | 279 | **52** | ports (AXI 29); split proposed |
| spec/gpu/cp/atg_cp.md | rtl/gpu/cp/atg_cp.v | **481** | 39 | lines; split proposed (atg_cp_regs/state/ctl/irq/dbg) |
| spec/gpu/blt/atg_blt.md | rtl/gpu/blt/atg_blt.v | **696** | 22 | lines; split proposed (atg_blt_ctl/rd/wr/port) |
| spec/gpu/top/atg_top.md | rtl/gpu/top/atg_top.v | 317 | **57** | ports (two AXI interfaces) |
| spec/boards/zybo/atg_zybo_2d.md | boards/zybo/atg_zybo_2d.v | 218 | 1 | ok (design calls it zybo_2d) |

Lines by `wc -l`, ports by counting input/output declarations (script in the session, not kept).

## Decision needed (Q1 / user)

- U38's "≤ 40 ports" cannot be met by am_axi_master, atg_mc (as wiring) or atg_top: an AXI master is 29 signals, a host AXI slave 24. Proposal: count one standard AXI interface as one port group. Until decided they are recorded as exceptions in their specs.

## Differences found between the RTL and design.md / docs/ghci.md (recorded in the specs §10, not fixed)

1. STATE survives CTRL.RESET in the BRAM, but the BLT's shadow copy is reset by core_rst_n: after RESET the BLT runs with zero parameters until the host rewrites the BLT range (the TB rewrites every field per blit, so it does not see it).
2. The debug port waits in HALTED and FAULTED (MC quiesce), while ghci.md §4 lets HALTED take it like IDLE.
3. HALTED comes at memory idle with the blit frozen (p003 choice; design §2.6 says HALTING waits for the block's work).
4. RESET from RUNNING skips HALTING (same quiesce).
5. RING_EMPTY = (RING_WPTR == 0); FAULT_INFO[23:16] of an aperture fault is the client number, not the block; job tag 0; a BLT memory error has no address.
6. No watchdog, ring, STEP, fence; IRQ [8:7] absent; ISA_ID 0 (all expected for CP v0).
7. atg_mc: one HIGH_MASK level instead of design §4.1's three priority groups; no atomic port. am_axi_master: no AXI3 parameter (the board top truncates).
8. TB gap: atg_top_tb's "STATE write while FAULTED" read-back uses mask 0, so it checks nothing.

## Verification (host only; no QEMU, no board)

- `tools/lint.sh` (Verilator 5.032): 11/11 clean at a217bf8 (no RTL changed afterwards).
- `sim/run.sh`: PASS am_afifo, am_axi_master, am_axi_regs, atg_mc, atg_top.
- The specs were checked against the RTL by reading; no automated check of spec vs RTL exists.

## Residual

- U38 port-rule decision (above). The splits of atg_cp, atg_blt, atg_mc are proposals; the implementation Phases (p004 for the CP) decide.
- The RTL-vs-GHCI differences 1 and 2 need a fix in an implementation Phase (Q1 to assign).
- No unit TB for am_sync, am_fifo, atg_cp, atg_blt (covered through other TBs, listed in each spec §8).
