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

## 自動統合の承認と統合結果（2026-10-11）

ユーザー「mainへのコミットは自動でお願いします。」、確認回答「今後は自動で統合してください。」により、このセッションの承認済み作業のmainコミット/統合は以後自動実施する。従来のQ1担当による個別main統合確認をこの指示で置換する。pushや実機更新の指示ではない。共有記録への投影は引き続きQ1へ保留。

USB `3e9ace7b9` とGPU `461906448` を専用worktreeでmerge `afe01157f` にまとめ、現在のconfig.mkで `make -j16 build/arm64/vmunix` を実行。warning0/error0、arm64 ELF/raw-image check PASS。USB/GPU sourceが各個別検証版と一致することも確認。clean main `211be00dc` を `afe01157f` へfast-forwardし、変更17path全てが検証済みworktreeとbyte一致した。main統合完了。実機の新core判定/待ち時間/USBログ抑制は未確認で、WSの残る実機条件は保持。push・実機更新/再起動は未実施。

最新USBログでdevice1/port1 `2109:3431` class09、usb-hub、4portsの登録を確認し、slot1の反復Stopped通知がhub監視と整合することを確認。USBキーボードの入力成功を意味しない。GPUは旧版のV7〜V10成功とrender node登録までユーザー確認済み。
