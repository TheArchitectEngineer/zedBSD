<!-- awesome-plan project=zedbsd record=ws221-p013 -->
# ws221-p013: USB CDC NCM の送受信の速さ（5330 の RTL8156）

Status: in-progress（2026-10-30 P3: 実装、codec の host 試験 PASS、5330 は未測）
Parent: [WS221](../ws.md)
Queue: q1689 / q1689-i01（P3）
依存: p003（CDC NCM の jumbo）、p012（ACK の間隔: 5330 の受けが 555〜696 → 1225 Mbit/s）

## 由来と目標

- ユーザー（原文、2026-10-29）:「P3で、2.5GのUSBが遅いことは、ドライバの問題と思いますので、5330を使って修正してほしいです。」
- Q1（2026-10-30）: 目標は Linux の r8152 の 2353 Mbit/s にできるだけ近く。xHCI の「endpoint ごとに要求 1 つ」が天井と測れたら、数字と HCD の変更の案を Q1 へ（xHCI は driver で HAL ではないが、他の USB 機器も使うので Q1 が判断）。
- 今（p012 の後、30 s、相手 centris）: send 272、recv 1225 Mbit/s。TX の dropped は TX packets の約 0.7 倍（queue 8 で満杯の drop）。

## 調べ（driver の今の形）

- `src/drivers/usb/usb-cdc-ncm-net.c`: TX は URB 1 つ、1 つの NTB に datagram 1 つ（`ncm_tx_submit` → `drv_usb_cdc_ncm_build_ntb16`）、転送中は queue（`NCM_TX_QUEUE_MAX` 8）に積み、完了ごとに先頭を 1 つ送る。RX は URB 1 つと buffer 1 つ（`ntb_in_max_size`、jumbo の build で 32 KB）、完了を poll が取り、parse してから再び出す（rearm）。
- xHCI（`src/drivers/pci/pci-xhci.c` の `xhci_request_publish_locked`）は非 isochronous の endpoint ごとに要求 1 つだけ。URB を重ねて出すには HCD の変更が要る（この Phase ではしない）。

## 設計

1. TX: 転送の完了（と空いている時の送り）で、queue の frame を `dwNtbOutMaxSize`（profile の `ntb_out_max_size`）と `wNtbOutMaxDatagrams`（`tx_max_datagrams`）に収まるだけ 1 つの NTB16 に詰める。codec（`usb-cdc-ncm.c`）に複数の datagram の builder を足す: NTH16、datagram を `wNdpOutDivisor`・`wNdpOutPayloadRemainder` の位置に順に、最後の datagram の後に `wNdpOutAlignment` で揃えた NDP16（項目 n ＋ 終わりの 0）、block の長さは今の ZLP の規則（`finish_block_length`）。queue を 8 → 64。
2. RX: buffer を 2 つにし、完了を取ったら先にもう 1 つの buffer で URB を出し直してから、終わった buffer を parse する（device が次の NTB を埋める間に parse する）。
3. 確かめ: codec の host 試験（複数の datagram の NTB を組み、今の parser で読み戻して一致、境界: 詰まる数・揃え・ZLP・上限）、5330 の `NO_VF2=1 5330-ncm.sh`（30 s、send・recv、errors・dropped）、QEMU の usb-net（CDC ECM、回帰だけ）、build の warning 0。

## 実装（2026-10-30）

- codec: `drv_usb_cdc_ncm_build_ntb16_frames`（`include/drivers/usb/usb-cdc-ncm.h`・`usb-cdc-ncm.c`）。datagram を順に置き、1 つ足すごとに NDP（項目 n ＋ 終わりの 0）を後ろに置いた block の長さ（ZLP の 1 byte を含む）を確かめ、buffer・`ntb_out_max_size`・`tx_max_datagrams` に収まる間だけ足す。
- driver（`usb-cdc-ncm-net.c`）: `limits.tx_max_datagrams` を 1 → `DRV_USB_CDC_NCM_MAX_TX_DATAGRAMS`（32）、TX の queue を 8 → 64（64 bit の機械、32 bit は 8 のまま）。`ncm_tx_submit` は frame の列を受け、block に載らなかった物は queue の頭へ順に戻す。送りの完了で queue の頭から最大 32 を渡す。RX は buffer 2 つ、完了で先にもう 1 つで URB を出し直してから parse する。
- host 試験: `plan/ws221/tests/run-ncm-ntb-frames-test.sh`（`ncm-ntb-frames-test.c`）: 揃え 4/0/4・8/2/8、block 2 KB・16 KB・32 KB、datagram の上限 3・32、長さを変えた frame を詰めて codec の parser で読み戻し（数・長さ・中身）、ZLP の規則、入らない最初の frame・長すぎる・短すぎる frame の拒否。ASan・UBSan の run とも PASS。

## 結果

| kernel | send Mbit/s | recv Mbit/s | 後の counter |
| --- | --- | --- | --- |
| p012 の受け側の後（bbc43c185 の tree、30 s、`NO_VF2=1 5330-ncm.sh`） | 272.0 | 1224.8 | RX errors 0、TX dropped 1015896 |
| p013（NTB に最大 32 datagram・16 KB、queue 64、RX の buffer 2 つ。fe31561e8 の tcp） | **1360.8** | **1396.1** | RX errors 9、TX dropped 688840 |
| Linux の r8152（vendor の mode、WS221 p003 の記録） | 2353（向きの記録なし） | — | — |

- send は 5 倍。TX の dropped は queue 64 の満杯（TCP が保って送り直す）。RX errors 9 は前の run にもある（4・8）。
