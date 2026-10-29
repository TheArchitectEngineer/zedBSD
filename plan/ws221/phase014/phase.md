<!-- awesome-plan project=zedbsd record=ws221-p014 -->
# ws221-p014: xHCI の bulk endpoint に転送を重ねる（opt-in の queue depth）

Status: planned（2026-10-30 P3: 設計。実装は次の起動）
Parent: [WS221](../ws.md)
Queue: q1689 / q1689-i01（P3）
依存: p013（NCM の NTB の複数の datagram・RX の 2 つの buffer）

## 由来

- ユーザー（原文、2026-10-29）:「P3で、2.5GのUSBが遅いことは、ドライバの問題と思いますので、5330を使って修正してほしいです。」
- Q1（2026-10-30、承認）: HCD の変更は HAL でなく driver の変更で、ユーザーの指示は USB の 2.5G を Linux に近づけること。条件: endpoint ごとの opt-in（bulk だけ）、他の全ての device は depth 1 で今と同じ、host 試験（TRB の address による event の照合、完了の順、複数が出ている時の cancel・STALL・reset、ASan・UBSan）、実機 5330（NCM の send・recv、USB storage の非退行（stick があれば read/write）、HID の非退行（USB の HID があれば））、全 platform の build warning 0、実装の前に設計を phase.md に。
- p013 の測り: 方向ごとに転送 1 つで、send は 17404 byte の block の 1 回 約 92 µs（bus 約 40 µs）、recv は 32 KB の 1 回 約 185 µs（bus 約 75 µs）。完了から次の submit までの間（割り込み → network worker の poll → 組み立て・parse → submit）が半分ほど。

## 今の xHCI（`src/drivers/pci/pci-xhci.c`、読んだ所）

- `struct xhci_endpoint` は `active`（要求 1 つ）。`xhci_request_publish_locked` は active が埋まっていれば EBUSY、`xhci_request_unlink_locked` は active を空に。
- submit（`xhci_urb_enqueue` の非 isochronous の経路、4150 行付近）: `ep->active != NULL || ep->recovering` なら EBUSY。`recovering` を submit の組み立ての間の排他に使い、TD を ring に積んで doorbell。TD の TRB は `normal_count < XHCI_RING_TRBS - 1` だけを確かめる（1 つの TD が ring に入るか）。
- 転送の event（`xhci_event_request_locked`）: `endpoint->active` を `drv_xhci_transfer_event_matches`（`include/drivers/pci/pci-xhci-lifecycle.h`、純粋な関数）で照合。完了（2850 行付近）は event の時に unlink し、completion の列に積み、`xhci_completion_finish` が後で callback。STALL は `recovering`・`stall_publishing` で admission を閉じ、USB core が halt を latch するまで保つ。
- cancel（`xhci_urb_dequeue` → `xhci_cancel_request`）: Stop Endpoint、Set TR Dequeue を software の producer（enqueue）に（ring を空に）、Running を待って DMA の持ち主を返す。active の 1 つだけを扱う。
- teardown の走査（4882 行の device の drain は dci ごとの `attempted` の bit、5459 行の controller の drain、7161 行の suspend）は endpoint の `active` を見る。
- isochronous は別の FIFO（`pci-xhci-isoch.c`、`xhci_isoch_cancel` は endpoint を丸ごと空に）を持つ。

## 設計

1. endpoint に `queue_depth`（既定 1）、要求の列（FIFO、submit の順）と数。`active` は列の頭のまま（depth 1 の全ての経路は今と同じ形で動く）。要求に `queue_next`。
2. opt-in: USB core の endpoint に「重ねてよい要求の数」（例 `drv_usb_endpoint_set_queue_depth(endpoint, n)`、bulk だけ、上限 4）。HCD の op（`drv_usb_hcd` の任意の hook、無い HCD は 1 のまま）。NCM の attach で bulk-in・bulk-out に 4・2。他の driver は呼ばない。HAL の API（`include/hal/hal.h`）は変えない。
3. admission: `ep->active != NULL` の代わりに「列の数 ≥ depth」で EBUSY。ring の空き: 出ている要求の TRB の和 ＋ 新しい TD ＋ link の 1 が `XHCI_RING_TRBS` を超えない事を確かめる（超えれば EBUSY、class driver は後で出す）。`recovering` の submit の排他はそのまま。
4. event の照合: 列の頭から順に `drv_xhci_transfer_event_matches` で照合し、頭でない要求に合った event は「順の外」として diagnose（xHCI は TD を ring の順に終えるので、頭より後ろが先に終わる事は無い）。完了は頭を unlink し、次を `active` に。
5. cancel・STALL・reset（depth > 1 の endpoint）: どれか 1 つの cancel は endpoint の全ての要求を cancel する（isochronous と同じ形）。Stop Endpoint → Set TR Dequeue を enqueue に（今の形で ring は空に）→ 列の全てを submit の順に CANCELLED で完了。STALL は頭が STALL で完了し、残りは IO_ERROR（または CANCELLED）で順に完了してから USB core の endpoint recovery（今の形）。reset・切断・suspend の走査は列の全てを扱う（4882 行の `attempted` は dci の bit でなく「その dci の頭の要求」で）。
6. 純粋な部分を `include/drivers/pci/pci-xhci-queue.h`（列の push・頭の取り外し・TRB による照合・ring の空きの計算）に出し、host 試験（`plan/ws221/tests/xhci-queue-test.c`、ASan・UBSan）: 順の完了、TRB の address の照合（頭・後ろ・ring の wrap）、空きの不足、cancel で全てが順に、STALL の頭と残り。
7. NCM: RX の URB を 4（buffer 4 つ、完了の順に parse、空いた buffer で出し直す）、TX の URB を 2（組み立てた block を順に）。cancel は全ての URB を出した順に（HCD は 1 つの cancel で全てを返す）。

## 確かめ

- host: 上の xhci-queue の試験、`plan/ws197/tests/run-xhci-isoch-test.sh`（isochronous の非退行）、NCM の codec の試験。
- build: amd64・pcat・rpi4・rpi5・zybo・VF2・5330・RTL8127 の config の vmunix の warning 0。
- QEMU: usb-net（CDC ECM、depth 1 の経路）・usb-storage の回帰（qemu-tcp と USB storage の起動）。
- 実機 5330: NCM の send・recv（30 s、`NO_VF2=1 5330-ncm.sh`）、USB storage（stick があれば読み書き）、USB HID（あれば反応）。

## 危険

- xHCI は全ての USB 機器が使う。depth 1 の経路を変えない事を host 試験と QEMU と実機で確かめる。cancel・STALL の複数の要求は NCM だけが使う。

## 結果

（未）
