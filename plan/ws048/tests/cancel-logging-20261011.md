# WS048 p015 evidence: expected cancellation logging / 2026-10-11

User confirmed boot success after p014, then reported repeated completion=26 residual=1 and pending-events=0 snapshots. This establishes boot recovery; it does not establish full USB keyboard/peripheral acceptance.

Source findings: xHCI defines 26=Stopped and 27=Stopped Length Invalid. When software already set request->cancelling, these are expected Stop Endpoint outcomes. usb_hub_worker uses a 250ms interrupt-read timeout and a periodic rescan every eight polls. Its normal idle path cancels the outstanding read, so unconditional logs repeat. A one-byte residual is compatible with no hub-change data; exact slot1 identity has not been remotely checked.

Changes in src/drivers/pci/pci-xhci.c:

- Suppress Stopped/Stopped-Length logs only while the request is already cancelling. Other completion codes stay visible, preserving BUG-030 timeout/completion-race evidence.
- Move cancellation ring snapshots from every dequeue to the cancellation-failure retain path. Stop/reset/dequeue/restart, completion ownership and DMA lifetime are unchanged. Existing cancellation failure message remains.
- Disable/restore caller IRQ around snapshot event_lock, preventing same-CPU IRQ reentry now that ARM64 workers start with IRQ enabled. Sample dequeue under the lock. Skip snapshot MMIO/DMA after a published controller fault; its fault/failure reports remain available.

Verification: `make -j16 build/arm64/vmunix` exit0, warnings0/errors0, ELF and raw-image checks PASS. Current config.mk matches main. `git diff --check` PASS. Manual review covers cancellation-code gate, normal vs retain call sites, IRQ restore including fault early return, lock ordering and retained request ownership; final changed source reviewed against AGENTS/Guardrail/full coding-style.md. clang-format19.1.7 range output reviewed; authoritative one-argument-per-line and comment layout retained. No new mock tests: transfer state machine is unchanged, existing IRQ fixture does not exercise these diagnostic functions; focused control-flow review plus compilation is appropriate for this logging change. No runtime log-suppression claim.

Private disposable build log: build/rpi4-cancel-logging-build.log. SHA256:

```
56c11753594045c9edce375c3eefa3dd0c3f9bfb50de3e5ea81867d380760c06  config.mk
7a8c7e84236a97165a8769e6f05747191ae487c4312214967e0baea4b112db3e  src/drivers/pci/pci-xhci.c
a50ad9425c83d16d2fc432c19904f5b914d00966c93a19751a92e6e8a473def9  build/arm64/kernel.elf
be034d13066bf2428dd9e4946dda2c7c841370de238f3ad64bc891e8c647c199  build/arm64/vmunix
```

Finite source/build criteria cleared. Main integration pending concrete approval; physical log quietness and USB keyboard acceptance await user confirmation. No QEMU/SSH/deployment/push. WS048 incomplete, shared Master/Queue/history/cache/GitHub projections Q1 pending.
