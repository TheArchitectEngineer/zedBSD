<!-- awesome-plan project=zedbsd record=ws282-p004a -->

# ws282-p004a xHCI の HS isoch IN の実地（URB の ring・worker・halt、`camcap --probe-only`）

- Parent: [ws282](../ws.md)、Queue: q1667 / q1667-i04（P6、実装だけ・試験なし、branch agent/p6-ws282c）
- Status: in-progress（実装済み・試験未実施。この Phase の本体は実機の実地で、それは未実施）

## 実装した事（`src/drivers/usb/usb-uvc-stream.c`、2a6f87540）

- start: COMMIT の再送 → `uvc_alternate_choose`（C ≥ P の最小、無ければ ENOSPC）→ SET_INTERFACE（EBUSY は 5 ms ごとに 10 回）→ isoch IN の endpoint → URB 4 本 × 32 packet（stride = alternate の packet、1 本 ≤ 32 KiB）、`kern_malloc` の buffer、`drv_usb_urb_reserve_transfer`（ENOTSUP/EOPNOTSUPP は予約なしで続ける）→ worker（`kthread_create`、既定の優先度）。最初の round は全部を「done・未投入」にして worker が 0→3 の順に投入する。
- 完了の callback: ring の bit を `done_mask` に立て waitq を起こすだけ。
- worker: `next` の bit が立つまで waitq で眠る（250 ms ごとに watchdog）、`drv_usb_urb_retired` を 1 ms ずつ 1 s まで待ってから packet を読む（OK/SHORT は actual が 0 なら空の interval として捨てる、MISSED・ERROR は数える）、再投入（EBUSY/EAGAIN は 1 ms ごとに 1 s まで、halt が来れば止める、ENODEV は fault(ENODEV)、他は fault(EIO)）、`next = (next + 1) % 4`。
- watchdog: URB の完了が 2 s 無ければ fault(EIO)。frame が max(2 s, 4 × interval) 無ければ `no_signal` を 1 回数えて `drv_cam_source_counters_changed`（frame が来れば解除）。
- halt（冪等）: stopping → 投入の途中を待つ → PENDING の URB を cancel → 全部を drain（1 s）→ worker の join → alternate 0 → 解放。drain しない URB があれば `halt_timeouts++`・EBUSY で URB と buffer を持ち続け、次の halt（start の前・detach）が取り戻す。
- `userland/tests/camcap/`（7e28d915d）の `--probe-only <ms>`: buffer を全部 QUEUE して STREAM_ON、ms だけ待って STREAM_OFF、counters を出す（frame は DEQUEUE しない。組み立ては kernel の側で走る）。

## 設計と違う判断

- packet の actual が 0 の物は payload にしない（design §4.5 の規則 1 は len < 2 を壊れた header とするが、isochronous では device が送る物の無い interval に 0 byte の packet が普通に来るので、数えると counter と ERROR が常に立つ）。
- 再投入の待ちは 1 tick でなく 1 ms の sleep（`sched_sleep` の tick に丸める）。

## build

- p003 と同じ（5 つの config の vmunix が rc 0・warning 0）。camcap: `make -j16 ZEDBSD_CONFIG=plan/ws282/tests/config.mk BUILD=build/ws282/p6c-amd64 build/ws282/p6c-amd64/bin/camcap` rc 0・warning 0、`check-dynamic-elf.py` まで通った。

## 未実施

- 実機（5330 + HDMI capture）の実地: 300 本の URB で MISSED/ERROR < 1%、FID の交互、`hw.usb` の isoch の counter（design §4.7 の受け入れ）。T1 の依頼の形（image: `plan/ws282/tests/config.mk`、`camcap -f mjpeg -w <低い mode> --probe-only 1200`、`camcap` の counters と dmesg の `usb-uvc:` の要約）。
- 偽の URB の層での ring の host 試験（2 本・3 本が同じ wake で終わる、wrap の直後、逆順の完了。design §4.5）は書いていない（stream.c が USB の core を直に呼ぶので、host で通すには drv_usb_* の stub が要る）。
