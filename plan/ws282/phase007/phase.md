<!-- awesome-plan project=zedbsd record=ws282-p007 -->

# ws282-p007 Linux の xHCI の quirk の表と Pi 5・Pi 4・VF2

- Parent: [ws282](../ws.md)、Queue: q1667 / q1667-i05（P6、実装だけ・試験なし、branch agent/p6-ws282d）
- Status: in-progress（表と試験の config だけ。3 枚の板の確認（T1）と IRQ の中の複写の時間は未実施）

## Linux の xHCI の quirk の表（Values referenced: Linux rpi-6.12.y の `drivers/usb/host/xhci-pci.c`・`xhci-ring.c`・`xhci-mem.c`・`xhci.h`・`drivers/usb/dwc3/host.c`、2026-10-29 に GitHub の raw を GET で読んだ。code は写していない）

| 板（controller） | Linux の quirk | Linux での意味（読んだ範囲） | zedBSD の xHCI の今 | isoch への影響と勧め |
| --- | --- | --- | --- | --- |
| Pi 4・VF2（VIA VL805、1106:3483） | `XHCI_TRB_OVERFETCH` | segment の後ろを先読みするので segment の pool を 2 倍の大きさ・align に（`xhci_mem_init`） | ring は 4096 byte の coherent に 256 TRB ちょうど（`ring_alloc`）、後ろの余白無し | IOMMU の無い板では後ろの 4 KiB の読みは害が無い見込み。fault・Ring の乱れが出たら ring を 8 KiB 取って前半だけ使う（pci-xhci.c、WS197 の source、Q1） |
| 同 | `XHCI_AVOID_DQ_ON_LINK`（downstream） | Set TR Dequeue の先が link TRB なら次へ進める（`xhci_move_dequeue_past_td`） | `ring_push` が enqueue を link の位置に置かないので、isoch の Set TR Dequeue（producer）は link に当たらない | 影響無しの見込み |
| 同 | `XHCI_EP_CTX_BROKEN_DCS` | 停止の後の endpoint context の DCS が誤る（TRB の cycle を使う） | `xhci_isoch_dequeue_slot` は context の pointer の位置だけを読み DCS を使わない（Set TR Dequeue の DCS は driver の cycle） | 影響無しの見込み |
| 同 | `XHCI_VLI_SS_BULK_OUT_BUG`・`XHCI_VLI_HUB_TT_QUIRK`（firmware < 0x0138C0）・`XHCI_LPM_SUPPORT`・`XHCI_RESET_ON_RESUME` | SS の bulk OUT、hub の TT、LPM、resume | — | HS の isoch IN には当たらない。hub の下の FS の device（TT）は camera では使わない |
| Pi 5（RP1 の DWC3、xhci-plat） | `xhci-sg-trb-cache-size-quirk`・`write-64-hi-lo-quirk`・`quirk-broken-port-ped`（≤ 3.00a） | SG の bulk の TRB の cache、64 bit の register の書きの順、Port Disable | isoch は 1 TD = 1 TRB で SG でない | 影響無しの見込み |
| 5330（Intel） | `XHCI_AVOID_BEI` | isoch の TRB の BEI を続けて付けすぎない（`trb_block_event_intr`、interval の下限 8・上限 32 の記憶） | BEI は 8 TD ごとと最後を外す（`DRV_XHCI_ISOCH_IOC_EVERY` 8） | 同じ型。drop が出たら 8 を小さく |
| 全部 | TBC/TLBPC | `xhci_get_burst_count`・`xhci_get_last_burst_packet_count` は quirk で変わらない（HS は TBC 0、TLBPC = TDPC − 1） | p005 の `drv_xhci_isoch_burst_bits`（仕様の式で MBS = mult）と同じ値 | — |

## 試験の config（実装した事）

- `plan/ws282/tests/config-rpi4.mk`・`config-rpi5.mk`・`config-visionfive2.mk`: 各板の CI の config に `camcap camcheck`。

## IRQ の中の staging の複写の時間（design §7.3 の L7）

- 測り方の案（未実装）: `xhci_isoch_finish` の packet の `kern_memcpy` の前後の単調時刻を `io_stats` の新しい event（`IO_XHCI_ISOCH_COPY`、calls = request、bytes に ns）に足す。`include/uapi/io-stats.h` は他の WS の UAPI で version の上げ（9 → 10）が要るので、Q1 に依頼として返す。代わりに既存の event だけで読むなら、複写の byte は今は数えていない（`IO_USB_STAGING_COPY` は別の経路）。

## 未実施

- Pi 5・Pi 4・VF2 で `camcap` 1 frame と `camcheck` 30 frame（T1、各板の config の image）。non-coherent の板での画素の壊れ（staging の複写は non-coherent で一度も走っていない）。複写の時間。
