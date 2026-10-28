<!-- awesome-plan project=zedbsd record=ws221-p003 -->
# ws221-p003: driver（CDC ECM・CDC NCM の jumbo）

Status: in-progress（2026-10-29 P3 q1664: ECM・NCM の実装と host 試験は済み（8aa242098）、5330 の実機の試験の途中）
Parent: [WS221](../ws.md)
Queue: q1664 / q1664-i01（P3）
設計: [p001](../phase001/phase.md) §3.5（ECM）・§3.6（NCM）・§4.1（descriptor の読み）、§9 は全部推し。Q1（2026-10-29）: 「p003（RTL8156 の SET_MAX_DATAGRAM_SIZE の試し）」、主の実機は VF2、5330 は従。

## descriptor の読み（§4.1）

5330 の RTL8156（Debian の `lsusb -v`、2026-10-29）: config 2 NCM・config 3 ECM とも `wMaxSegmentSize` 1518、NCM の `bmNetworkCapabilities` 0x2b（bit 3 SetMaxDatagramSize あり）。1514 ではないので止めずに進めた。1518 は 1500 の MTU に VLAN の tag の 4 byte を足した値と読める（推測）。Pi 4 の UGREEN は未読（Pi 4 は今の主の実機ではない）。

## 済み（commit）

| 項目 | commit | 確かめ |
| --- | --- | --- |
| ECM: `frame_max` = min(`wMaxSegmentSize`, 14 + `NET_MTU_MAX`)、`mtu_max` = `frame_max` − 14、受けの URB を MTU に依らない `rx_length`（bulk-in の packet の倍数に切り上げて 1 packet 足す。1514・512 なら 2048）、送り・受けの検査を `frame_max` に、受けを `alloc_for(…, PACKET_BUF_ALLOC_RECEIVE)`、pool の不足は `rx_dropped`（他は従来どおり `rx_errors`）、attach の log に `mtu_max=%u rx_urb=%zu`。`set_mtu` の op は足さない（net-device の `mtu_max` の検査で足りる。hardware の設定は無い） | 8aa242098 | build warning 0（5 platform）。QEMU は下 |
| NCM codec: `drv_usb_cdc_ncm_limits.max_datagram_size`（0 = 標準 1514）、`negotiate_nth16` が 1514〜limit の間で両方の NTB に 1 datagram が入る最大を二分探索で求める（layout は datagram と共に増えるだけ）、`profile_valid` は 1514 以上、limit が 1514 未満なら EINVAL。設計の `DRV_USB_CDC_NCM_MIN_MAX_DATAGRAM_SIZE` への改名はしない（WS004 の試験 `usb-cdc-ncm-wire-test.c` が今の名前を使い、WS004 の物は触らない設計のため。意味の comment を足した） | 8aa242098 | `plan/ws221/tests/run-ncm-jumbo-wire-test.sh` PASS（通常と ASan・UBSan の 2 回）: 32K/16K の block で 9014、device の 8K の block で 8162、driver の 16K/8K で 8161、1514〜9014 の全ての limit で結果が limit 以下で単調、9014 × 3 を 1 つの受けの block で parse、9015 は block ごと拒否、送りの build も 9015 を拒否、limit 0 → 1514、limit 1000 → EINVAL、64K の block で limit 65535 → 65505。WS004 の fixture（run の script は `rm -rf` を使うので使わず、同じ compile を build/tmp/ws004-ncm で手で）PASS（通常と sanitizer） |
| NCM binding: descriptor の `wMaxSegmentSize` を保つ、jumbo の build（`PACKET_BUF_JUMBO_COUNT` ≠ 0）で受け 32 KB・送り 16 KB の NTB、limits の max は bit 3 の device なら 9014（`NCM_DATAGRAM_MAX`）、無ければ `wMaxSegmentSize`、`SET_MAX_DATAGRAM_SIZE` を 9014 → `wMaxSegmentSize` → 1514 の順に試し各回 `GET_MAX_DATAGRAM_SIZE` で読み戻す（読み戻しが小さければそれを使う、`wMaxSegmentSize` を超える値は読み戻しで確かめられた時だけ使う）、`mtu_max` = profile の max − 14、受けを `alloc_for`、pool・queue の不足はその datagram だけ `rx_dropped` にして block の残りを続ける、attach の log に `segment ntb_in ntb_out max_datagram read_back caps mtu_max` | 8aa242098 | 5330 の実機（下） |
| 試験: `ncm-jumbo-wire-test.c`・`run-ncm-jumbo-wire-test.sh`、`5330-ncm.sh`・`config-5330-jumbo.mk` | 8aa242098、886df6fd0 | — |

設計からの差: `wMaxSegmentSize` が 1518 の RTL8156 で 9000 を試すため、bit 3 の device には descriptor を超える 9014 を先に頼む（p001 §3.6 は min(`wMaxSegmentSize`, …) だった）。device が読み戻しで確かめた値だけを使い、確かめられなければ descriptor の値へ下げる。

## 試験

（5330 の結果をここに書く）

## 残り

- 5330 の実機: attach の log（max_datagram と read_back）、`ifconfig ue0 mtu <mtu_max>`、VF2（9000）への大きい ping、1500 での TCP の送受（2.5G の比べ）。
- QEMU: §8.2 (5) の非退行（`qemu-mtu.sh`・`qemu-tcp.sh`、attach の log の `mtu_max=1500 rx_urb=2048`）。`tcp-receive-speed.sh`・`lan-hotplug.sh`・`lan-link-down.sh` は未実施。
