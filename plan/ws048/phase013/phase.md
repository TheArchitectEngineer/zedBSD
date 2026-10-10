<!-- awesome-plan project=zedbsd record=ws048-p013 -->
# WS048 p013: xHCI interrupt acknowledgement and fault containment

Status: cleared (limited source/host/build criteria; physical acceptance remains pending)
Disposition: normal
Parent: [WS048](../ws.md)
Queue: [rpi4-xhci-irq-20261011](../codex-irq-repair-20261011.md)

User reports freeze at `enable slot doorbell complete` on p012-integrated kernel. Doorbell publication returned; completion-wait recovery still unverified. IRQ callback runs before its PCI dispatcher issues EOI. Current IRQ path does not reject fatal/HCE state before event DMA reads, does not finish EHB on an empty event ring, has no posted IMAN acknowledgement readback, and drains events without a per-dispatch limit. Linux primary reference: xhci-ring.c, xhci_irq/clear_interrupt_pending/update_erst_dequeue; factual protocol behavior only, original implementation. This is a source finding, not proof of the specific physical stopping cause.

Finite scope: correct acknowledgements and EHB ownership for IRQ vs polling; bounded IRQ event batch, controller-fault refusal/interrupt containment without freeing DMA or claiming halt; first Enable Slot wait scheduling stages. Reuse production command-wait fixture and add a focused IRQ register/event model exercising exact extracted functions; current config warning0 arm64 kernel/raw-image checks; final full changed-source standards review and diff-check. No DMA-alias guess, MSI, GPU, boot0/CI, HAL API, toolchain, SSH probing/deployment/reboot/push or shared plan/cache writes. Main integration is separately approved. Actual USB/boot acceptance remains p009/p006; WS incomplete. Standards: AGENTS/Guardrail/full coding-style.md/.clang-format.

User runtime DMA evidence: `DMA PCI 400000000 CPU 0 size c0000000 window 100000000`. This confirms PCI alias16GiB, CPU0, firmware range3GiB, programmed power-of-two window4GiB. Earlier uncertainty over runtime DMA base is now resolved; no alias change is justified by this log alone.


## Terminal outcome / 2026-10-11

Attempt rpi4-xhci-irq-20261011-i01 cleared its finite source/host/build criteria. [Final evidence](../tests/irq-repair-20261011.md) records exact checks, hashes, full changed-source review, limitations and next-stage logs. Both host fixtures pass (11 + 8 scenarios); current-config arm64 kernel/raw-image build and format checks pass, warning/error 0. Final poll/fault publication is serialized under the event lock. This does not clear physical USB/boot acceptance or complete WS048. Main integration and shared projections remain pending; no push or remote action.


## Main integration follow-up

User explicitly approved「mainへ統合する」for `a1acbc44aac7384b4d0dce8e646ac19ca5e72980`. Clean main `4b70292ef` fast-forwarded to that exact commit without conflict. All eight integrated files were read back byte-identical to the verified private worktree; current config.mk is identical, and xHCI source SHA256 matches the final build evidence. The 19 focused host scenarios and warning0 arm64 kernel/raw-image checks therefore apply to the integrated source; no redundant rebuild. Earlier integration-pending statements record the prior state. Physical boot/USB recovery remains unverified and WS048 remains incomplete. No push or remote deployment/reboot. Shared Master/Queue/history/cache/GitHub projections remain Q1 pending.
