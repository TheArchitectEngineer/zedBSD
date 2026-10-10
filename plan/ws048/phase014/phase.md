<!-- awesome-plan project=zedbsd record=ws048-p014 -->
# WS048 p014: ARM64 kernel worker IRQ entry

Status: cleared (limited source/build criteria; physical recovery unverified)
Disposition: normal
Parent: [WS048](../ws.md)
Queue: [thread-irq repair](../codex-thread-irq-20261011.md)

User reports physical freeze after p013 integration, screenshot stops at `enable slot completion wait irq=0` / `completion poll resumed`. With irq=0 the latter log does not mean sched_yield ran: that call is skipped. This invalidates any assumption that the wait is interruptible on this machine; physical acceptance remains unmet.

Source finding: ARM64 scheduler dispatches with IRQ disabled, but arm64_kernel_task_entry directly calls the new worker without unmasking IRQ. Existing amd64 entry explicitly enables IRQ, and arm64 user entry eret installs unmasked SPSR. All new ARM64 kernel workers inherit IRQ-off; xHCI's guarded yield and interrupt completion are suppressed. Fix the new kernel entry only, preserve resumed-context/caller interrupt state. HAL API unchanged (implementation repair permitted by existing user authority). Add bounded first Enable Slot event-consumption stage logs for any remaining hardware stall; do not guess DMA aliases or reset hardware policy.

Finite criteria: focused source/disassembly review of first-entry interrupt state and argument preservation, extracted command-wait host fixture, current-config warning0 arm64 kernel/raw-image build, full changed-source coding-style/Guardrail review and diff-check. Physical USB/boot acceptance remains p009/p006 and WS incomplete. No QEMU/SSH/deployment/push, toolchain or shared-plan changes. Main integration requires concrete commit approval. Investigation bounded to initial thread entry and completion polling in this attempt.

## Terminal outcome

Attempt rpi4-thread-irq-20261011-i01 clears its finite criteria. [Evidence](../tests/thread-irq-20261011.md) records source finding, generated instruction order, 11 host cases, current-config warning0 kernel/raw-image build, full changed-source review and limits. Main integration pending concrete approval. Physical acceptance remains unmet.


## Main integration follow-up / 2026-10-11

User explicitly approved「mainに統合してください」for `7723c323fe90866a424c49a852ad38df9ad08412`. Clean main2008584ae fast-forwarded to that exact commit without conflict. All six integrated files were read back byte-identical to the verified worktree, and both changed source hashes plus current config.mk match the recorded warning0 build evidence. Prior instruction-order check, 11 host scenarios and arm64 kernel/raw-image validation apply to this identical source; no redundant rebuild. Main integration is complete; earlier approval-pending text is historical. Physical freeze recovery and USB input remain unverified, WS048 incomplete. No push or remote deployment/reboot; shared Master/Queue/history/cache/GitHub projections remain Q1 pending.


## User hardware follow-up

User confirms「起動できました」after integration. Boot recovery is now user-confirmed; remaining log spam is handled in [p015](../phase015/phase.md). No USB keyboard/other device acceptance was stated, so WS048 remains incomplete. Earlier unverified boot statements are historical.
