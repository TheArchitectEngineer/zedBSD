# Codex Queue: rpi4-xhci-irq-20261011

Status: finished
Owner: Codex / codex/rpi4-xhci-irq
Approval: current user「enable slot doorbell completeでフリーズです」continues USB functional repair. Finite scope and criteria: [p013](phase013/phase.md). Base main4b70292ef.

| Attempt | Phase | Status | Dependency |
| --- | --- | --- | --- |
| rpi4-xhci-irq-20261011-i01 | [ws048-p013](phase013/phase.md) | cleared | p012-integrated source and new physical stop report |

Graph: p012/context → i01 → physical boot/USB acceptance/context. No next Queue. Shared projections/GitHub Q1 pending; no remote effects.

Outcome: finite source/host/build criteria cleared; [evidence](tests/irq-repair-20261011.md). Physical freeze recovery remains unverified and WS incomplete. User latest「お願いします。」confirms continuing this repair. No next Queue started. Concrete main integration pending; shared projections remain Q1 pending.


## Main integration follow-up

User explicitly approved「mainへ統合する」for `a1acbc44aac7384b4d0dce8e646ac19ca5e72980`. Clean main `4b70292ef` fast-forwarded to that exact commit without conflict. All eight integrated files were read back byte-identical to the verified private worktree; current config.mk is identical, and xHCI source SHA256 matches the final build evidence. The 19 focused host scenarios and warning0 arm64 kernel/raw-image checks therefore apply to the integrated source; no redundant rebuild. Earlier integration-pending statements record the prior state. Physical boot/USB recovery remains unverified and WS048 remains incomplete. No push or remote deployment/reboot. Shared Master/Queue/history/cache/GitHub projections remain Q1 pending.
