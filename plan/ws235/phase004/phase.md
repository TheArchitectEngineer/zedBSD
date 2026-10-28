<!-- awesome-plan project=zedbsd record=ws235-p004 -->

# ws235-p004 RX・TX の ring（v1 の descriptor）、1 queue・1 vector、net の worker への接続

- 親: [WS235](../ws.md)。
- 状態: planned（2026-10-29 P1）。
- 依存: [p002](../phase002/phase.md)。実機は p003 の link と同じ枠で。

## 範囲（p001 の設計の段 3）

1. ring: v1 の descriptor（FreeBSD rge の形、RTL8127 も v1。fbsd-if_rge.c:1158-1200 の RXCFG・0xeb58 bit0・0xd8 bit1 を落とす・RTL8127 の 0x20e4 bit 0x04・
   MAC OCP 0xe00c・0xc0c2）、RX・TX とも coherent の DMA（256 byte 揃え）、RX の buffer 2 KiB と RXMAXSIZE をそれに、TX は `packet_buf_physical` →
   `drv_dma_physical_address(..., 64, ...)` で in place、届かなければ copy。
2. open の MAC の設定（rge_init の ring の address・RXCFG・TXCFG・CSI 0x70c・0x890・RSS を切る・RXQUEUE・CFG1・0xc140/0xc142・CMD の TX・RX の有効・IMR）。
3. 割り込み: ISR を返し IMR を閉じて `net_device_schedule_poll`、poll の最後に IMR を開ける。IMTYPE_NONE。link の kthread が worker を周期で起こす。
4. close: IMR を閉じ → TX・RX を止め → DMA の停止を待ち → packet を返す。
5. OpenBSD と FreeBSD の差（p001 の中 7）の採る側を記録する。
