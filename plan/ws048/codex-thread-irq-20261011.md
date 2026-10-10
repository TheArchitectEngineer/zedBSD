# Codex Queue: rpi4-thread-irq-20261011

Status: finished
Owner: Codex / codex/rpi4-thread-irq
Approval: user latest「フリーズです」with p013 boot screenshot continues the requested RPi4 boot/USB repair. Base main2008584ae. Exact finite scope and criteria: [p014](phase014/phase.md).

| Attempt | Phase | Status | Dependency |
| --- | --- | --- | --- |
| rpi4-thread-irq-20261011-i01 | [ws048-p014](phase014/phase.md) | cleared | integrated p013 source / new physical irq=0 evidence |

Graph: p013/context → i01 → physical boot/USB acceptance/context. No next Queue. Shared projections/GitHub Q1 pending.

Outcome: finite source/build criteria cleared, [evidence](tests/thread-irq-20261011.md). Physical boot/USB unverified. Main integration pending; no next Queue.


## Main integration follow-up / 2026-10-11

User explicitly approved「mainに統合してください」for `7723c323fe90866a424c49a852ad38df9ad08412`. Clean main2008584ae fast-forwarded to that exact commit without conflict. All six integrated files were read back byte-identical to the verified worktree, and both changed source hashes plus current config.mk match the recorded warning0 build evidence. Prior instruction-order check, 11 host scenarios and arm64 kernel/raw-image validation apply to this identical source; no redundant rebuild. Main integration is complete; earlier approval-pending text is historical. Physical freeze recovery and USB input remain unverified, WS048 incomplete. No push or remote deployment/reboot; shared Master/Queue/history/cache/GitHub projections remain Q1 pending.
