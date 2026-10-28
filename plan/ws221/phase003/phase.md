<!-- awesome-plan project=zedbsd record=ws221-p003 -->
# ws221-p003: driver（CDC ECM・CDC NCM の jumbo）

Status: cleared（2026-10-29 06:07 P3 q1664: 8aa242098。host・QEMU の非退行 PASS、5330 の RTL8156 は CDC NCM で max datagram 1518（mtu_max 1504）と実機で確かめた）
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

- host: `sh plan/ws221/tests/run-ncm-jumbo-wire-test.sh` → `ncm-jumbo-wire: PASS` × 2（上の表）。WS004 の fixture PASS × 2。
- build: `config/ci/config-{amd64,pcat,rpi4,rpi5,zybo}.mk` の vmunix が全部 rc=0・warning 0（8aa242098 の tree）。
- QEMU（`build/ws221-jumbo/hdd-image.img`、8aa242098 の tree）:
  - `qemu-mtu.sh` → `qemu-mtu: PASS`（ok 12、出力 build/tmp/qemu-mtu-2.txt）。`qemu-tcp.sh … 20` → `qemu-tcp: PASS`。
  - attach の行（SSH の `dmesg`）: `usb-cdc-ecm: ue0 mac=52:54:00:33:00:01 segment=1514 mtu_max=1500 rx_urb=1600`（bulk-in の packet が 64 byte なので 1600。§8.2 (5) の `rx_urb=2048` は 512 byte の packet の時の例）。
  - `plan/ws033/tests/tcp-receive-speed.sh` 3 回とも cksum 一致（6.83・7.09・7.01 MB/s）、`lan-hotplug.sh` 全項目 ok、`lan-link-down.sh` は plugged・relinked が ok、link-down は「skipped: no carrier event」（QEMU の usb-net が link を伝えない、試験の想定どおり）。出力 build/tmp/ws033-reg-1/。
- 5330 の実機（`sh plan/ws221/tests/5330-ncm.sh build/ws221-5330/hdd-image.img ncm1`、NVMe から起動、`5330-run: PASS`、21 秒で SSH、終わりに Debian に戻った。summary は build/ws221-5330/ncm1/summary.txt）:
  - `usb-cdc-ncm: ue0 … segment=1518 ntb_in=32768 ntb_out=16384 max_datagram=1518 read_back=1518 caps=0x2b mtu_max=1504`、link 2500 Mb/s。9014 を頼んだ後の読み戻し（または 9014 の拒否の後の 1518）で 1518。**RTL8156 は CDC NCM の mode では jumbo を取らない**（最大 MTU 1504）。9000 には vendor の mode の driver が要り、本 WS の範囲の外（p001 §4.1 の 3、ユーザーの判断）。
  - `ifconfig ue0 mtu 1504` rc=0、VF2（試験の間だけ 9000）へ `ping -s 1476` → 1484 bytes × 3、`-s 1472` → 1480 bytes × 3。
  - 1500 で centris（10.0.10.2、route 越し）と tcp-perf 10 秒: 送り 268.6 Mbps、受け 708.2 Mbps。ue0 の後の counter: RX errors 0 dropped 0、TX errors 0 dropped 522837（NCM の送りは 1 NTB に 1 datagram、queue 8 で満杯の drop）。Linux の r8152 は 2353 Mbps。NCM の速さは本 WS の 1GbE の目標の外なので記録だけ（Future Work の候補）。
  - VF2 の en0 は終わりに 1500 に戻した（script が読み戻した）。

## 残り

無し（p003 の範囲）。Pi 4 の UGREEN の descriptor の読みと実機は、Pi 4 が主の実機でないので行っていない（p006 で機械が決まれば）。
