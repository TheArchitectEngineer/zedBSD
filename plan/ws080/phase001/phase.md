<!-- awesome-plan project=zedbsd record=ws080-p001 -->

# ws080-p001: `ld.coff` の設計

Status: in-progress
Disposition: normal
Parent: [WS080](../ws.md)
Queue: main の依頼（2026-09-29、worktree `.claude/worktrees/ws080-coff`、branch `wt/ws080`、main 5b0522e2 から）
Approval: main の依頼「WS080 の p001（設計）」。GS base は案 A（swapgs）に決定済み。HAL の差分は案として `plan/ws080/proposed/` に置き、適用しない。

## Resume point

2026-09-29: 事実の調査（HAL の GS・FS・入口、exec の #! の経路、mmap の配置、syscall の ABI、kernel の XMM の使用、clang + lld-link で PE の EXE・DLL・
forward・ordinal・import lib を作れること）まで。次: `design.md` と HAL の差分の案。
