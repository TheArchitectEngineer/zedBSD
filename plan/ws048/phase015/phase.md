<!-- awesome-plan project=zedbsd record=ws048-p015 -->
# WS048 p015: quiet expected xHCI cancellation

Status: cleared (limited source/build criteria; runtime log suppression unverified)
Disposition: normal
Parent: [WS048](../ws.md)
Queue: [cancel logging](../codex-cancel-logging-20261011.md)

Approval: user confirms「起動できました」after p014, then reports repeated cancelled-request completion=26 residual=1 and cancel snapshot pending-events=0. Boot recovery is user-confirmed; USB keyboard functionality remains unconfirmed. Source defines 26=Stopped, 27=Stopped Length Invalid. usb_hub_worker intentionally cancels its quiet interrupt read after USB_HUB_POLL_MS=250ms and rescans every eight polls. One-byte hub status endpoint is consistent with this report; exact slot device identity has not been obtained.

Scope: suppress expected Stopped/Stopped-Length messages for requests already cancelling; collect ring diagnostic on actual cancellation failure rather than each successful poll timeout. Make the diagnostic's event lock IRQ-safe now that workers are correctly interruptible. Preserve transfer/cancellation ownership, error reporting, timeouts, hub polling and DMA behavior. Full review of changed code against Guardrail/coding-style; warning0 current-config arm64 kernel build, focused existing IRQ fixture where applicable; no new mock suite, QEMU/SSH/deployment/push/shared-plan changes. Main integration remains separately approved. Physical log suppression unverified until user boot.

## Terminal outcome

Source review and current-config warning0 kernel/raw-image build/check PASS. [Evidence](../tests/cancel-logging-20261011.md) documents the narrow logging change, IRQ-safe diagnostic snapshot and checks/limitations. Existing IRQ fixture does not cover this helper and was not rerun; no test suite added for the diagnostic-only change. Main integration pending, physical log quietness unverified.
