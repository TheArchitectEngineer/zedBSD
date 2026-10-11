<!-- awesome-plan project=zedbsd record=ws197-p007-review-1 -->

# ws197-p007 の設計（4e9a42e93）の design-reviewer の結果 1（2026-10-11、Q1 から）

総評: このまま p007a1 に入らない。下を反映して設計を直す（Q9 の新しい形と音の route の model も一緒に）。

## BLOCKER

1. **X1・X3**: event と request の照合は endpoint->active だけ（pci-xhci.c:1709-1748 `xhci_event_request_locked`、:2587-2735 `transfer_claim`、:1751-1785 publish・unlink）。最初の event で request を終える。isoch は 1 URB に複数 TD＝複数 event、FIFO の 2 本目以降は照合できない。`xhci_urb_enqueue`:4003 は active!=NULL で EBUSY、:4171 publish 失敗で `__builtin_trap`、`xhci_request_release` :3642・:3666 も同じ前提。→ FIFO の先頭を active、残りを isoch_next の list。(1) event の TRB pointer で FIFO の各 request の TRB の範囲を探す (2) request に残り TD 数と packet ごとの結果、最後の TD の event でだけ completion へ (3) unlink は先頭を外して次を active に、active_count は request ごと。active を見る残りの所（:2445 :2531 :3446 :4457 :4532 :4701 :5275 :5354 :5560 :5692 :6957）の扱いを表に。
2. **X3・X4 Missed Service**: 見逃した TD の event が個々に来ない、TRB pointer 0 の MSE がある（Linux ep->skip）。最後の TD が見逃されると止まる。→ 飛ばしの規則（ある TD の event が来たら前の event の無い TD を missed で終える）、空の規則（Underrun/Overrun で queue の残りを全部 missed）、pointer 0 はこの規則だけで。
3. **X1・X2 ring の空き**: `ring_push`（:1024）に空きの計算が無い（:4385「Queue depth is one per endpoint」）。4 URB×32 packet×2 TRB で 256 を越え DMA を壊す。→ producer/consumer の位置を数え、足りなければ EAGAIN。isoch の buffer は 64 KiB を跨がせず 1 TD＝1 TRB に固定（X2 の続き TRB を不要に）。
4. **X5 cancel**: core の cancel は URB 単位。残りの URB の終わり方（誰が CANCELLED で完了させるか、callback の再投入）が無い。`xhci_device_quiesce`（:4702）は attempted の bitmask で dci ごとに 1 つしか cancel せず teardown が EBUSY。→「URB X の cancel は X と後ろ全部を CANCELLED、前は普通に完了」。Stop Endpoint→Set TR Dequeue を X の先頭へ→recovering を持ったまま後ろを HCD が CANCELLED（再投入は EBUSY）。device_quiesce・controller_drain_requests は queue 全部を回す。

## MAJOR

- **X6**: CErr は isoch で 0（pci-xhci-control.h:103 は常に 3）。Max ESIT・Mult の式の誤り: HS isoch/interrupt の wMaxPacketSize bit12:11 は Max Burst（word1 15:8）へ、Mult（word0 9:8）は SS isoch だけ、SS の Max ESIT は companion の wBytesPerInterval、Hi は HCCPARAMS2 の LEC=1 で 0xFFFF 越えの時だけ（LEC の読みが tree に無い）。FS: packet、HS: packet×(bit12:11+1)、SS isoch は今は EINVAL、対象は FS だけと明記。
- **X6 の interrupt にも同じ直し**: 5330 の HID・hub・BT の全 Configure Endpoint が変わる。p007a1 は isoch だけ、interrupt は別 Phase か T1 の HID 回帰を条件に。
- **X7**: isoch の URB は transfer reservation を持てない（usb.c:2026 で EINVAL）→ 投入ごとに kern_malloc・dma alloc（約 250 回/秒）、完了は hard IRQ（xhci_irq→`xhci_completion_drain` :3061）で callback から再投入すると IRQ の中で確保。→ isoch でも reservation（usb core と HCD、HAL 不変）、再投入は usb-bt の worker からだけ。
- **X3 IN の bounce**（`xhci_completion_finish` :2788-2794）が連続の写しだけ。isoch は packet ごとに offset・actual → 全体か packet ごとに写す。enqueue で iso_packets を検査（offset+length ≤ URB 長、IN の length=wMaxPacketSize）。
- **X4**: 一部の xHC は isoch でも Babble・Transaction Error で Halted にする。isoch には core の halt の latch が無く `xhci_urb_enqueue`:4144 が EIO を返し続ける → 失敗の event の後に endpoint context を読み、Halted なら Reset Endpoint→Set TR Dequeue（`xhci_endpoint_recover` を流用）。
- **X8 試験**: QEMU の usb-audio は OUT だけ、IN の短い packet・Overrun・bounce・組み立て直しが試せない、MSE・Underrun も出ない見込み。→ TD の状態機械を pci-xhci-control.h の純粋な関数にして host 試験で全 completion code、p007a2 で 5330 の HCI Local Loopback（Intel の対応は要確認）で T1 が IN・OUT を測る。ktest の結果は klog でなく sysctl か node を serial.py/SSH で（console log 判定は禁止）、vmunix.mk の knob を影響範囲に、合否の閾値を数で。
- **S4 の UAPI**（Q9 は新しい形に変わるが同じ観点で）: dev_t（0x0013 は input-bridge が使用）、権限（child は uid を落とす → privsep の親の要求が要る）、ioctl の構造体（固定長・_IOWR・reserved）、close・bluetoothd の再起動・bootloader 中・BT_IOC_RESET の時の SCO の扱い、IN の ring の大きさ（推し約 200 ms で古い物を捨てる）、OUT の上限（推し約 40 ms、満ちたら EAGAIN、流していない時 ENOTCONN）。
- **S1・S4**: `drv_usb_device_reset`（usb.c:1349-1359）は全 interface の I/O の門が空・URB 0 を求める → interface 1 の isoch が残ると BT_IOC_RESET と p013 の reopen・reset が EBUSY。usb-bt の reset・detach は interface 0 の前に interface 1 の URB を cancel・drain、reset の後は SCO を 0 本に。
- **S4・B1**: SET_INTERFACE（usb.c:3208 `device_control_try_lock`）と HCI command（usb-bt.c:1114、EP0）が同じ門を取り合い互いに EBUSY → alternate の設定は session（command を書く thread）から、kernel も短く待ち直す。
- **S3 IN の組み立て直し**: packet が 1 つ落ちると SCO header の位置がずれる → handle と SCO packet の長さを kernel に渡し、落ちたら組み立て中を捨て、handle と長さが合う所で同期し直す。
- **B1・B2 と p006**: eSCO の S4 には HF features の bit 9 が要るが p006 は AT+BRSF=62 で立てない。AT+BCC は codec negotiation（bit 7）前提だが p006 H1 で出さない → p007b1 で bit 9 を足す（p006 と WS に記録）か S4 を外して S3・S1・D1。HF から開ける時は Setup Synchronous Connection。
- **B3・B4 送りの拍**: 受けからだけ拍を取ると受けが途切れた時に送りも止まる。7.5 ms は mSBC の値、CVSD alt 2 は 48 byte で 3 ms → 受けが N ms 来ない時は単調時計、周期は alt と packet 長から。
- **B2 UI**: 「音: このコンピュータ／スマホ」と button には Phone・libkeiland・phone-shell・backend の変更が要る（Guardrail 2026-10-08）、範囲と見積もりに無い → 表と見積もりに（音の route の model とまとめて）。

## MINOR

bluetoothd は今は単一 thread（main.c:730 poll）→ 受け渡しは pipe などの command の通路、共有の可変状態を持たない。流す間は suspend しない、GATED の時の isoch の enqueue は EBUSY。command_busy の間の completion の後回し（:3061）で 24 ms を越える危険 → 記録して ktest で測る。`drv_usb_urb_setup_isochronous`（usb.c:2309-2324）に PENDING・hcd_owned・offset/length の検査。Isoch TRB の TBC・TLBPC・TD Size・ISP は FS・HS の 1 packet で 0。usb-bt の worker の drain の最長 1 秒の待ち（usb-bt.c:61・:741）で SCO が止まる。1 sample の足し引きは click → 多相 FIR の位相の PI 制御、片道の遅れの予算（例 150 ms 以下）。Enhanced Accept の代わりに普通の Accept（eSCO の値つき）、Packet Status Flag の data 無しは無音。見積もり 27≠ws.md の 25、p007a1 は 11〜13、p007b1 は 10〜12 LW。計画に無い依存: privsep の SCO の口、bt-hci-loopback の transport、vmunix.mk の ktest の knob、p006 BRSF、Phone・compositor・libkeiland、usb core の iso reservation、5330 の descriptor（Q1 の許可）、AX211 の SCO が HCI か PCM か（p007a2 で早く確かめる）。

## 正しかった点

FS isoch の Interval、S2 の alt の選び方（btusb と一致）、set_alternate の前の URB の全 cancel、interface の claim の自動解放、core の reset が alt を戻す、audiod の socket 0666 と mono。
