<!-- awesome-plan project=zedbsd record=ws265-p001 -->

# ws265-p001 設計

Status: cleared（2026-10-19 Q1 判定: 設計（design.md）を Q1 が読み、到達点まで実装できた）（旧: in-progress（2026-10-19 P1、q1257。cleared は Q1 の判定））

- 親: [WS265](../ws.md)。結果: [design.md](../design.md)。
- 起動の道は QEMU の source（`hw/ppc/spapr.c`、`hw/ppc/spapr_vhyp_mmu.c`、`target/ppc/mmu-hash64.c`）と、gdbstub で止めた最小の ELF で確かめた（design §1・§2）。
