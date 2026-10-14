# T1-643: BUG-311 の Bluetooth の 5 回目（Pi 4 の実機）2026-10-15
（Q1 の依頼は番号 T1-641 だったが、T1-641 は R7c で使用済みのため T1-643 とした。）
Pi 4 kei@10.0.30.2（実機）、image a9f0765（beta2+ga9f0765、/ は rw）。kernel・bluetoothd の差し替え・reboot はしていない。SSH と dmesg と /var/log/messages で判定。

## 結果: FAIL（Bluetooth）。WiFi は scan が通った
- dmesg `rpi4-bt: the line at 115200 baud: CR cb01, IFLS 10`（3 回とも同じ）、`controller ready after about 22 ms`、`bt-hci: /dev/bluetooth0: Raspberry Pi 4 Bluetooth 0000:0000`。
- 送受信: 25 bytes out（01 03 0c 00 01 01 10 00 01 03 0c 00 01 01 10 00 01 1a 0c 01 00 01 09 10）に対し 14 bytes in（04 0e 04 01 03 0c 00 が 2 回 = reset の完了 2 つだけ）。0 with errors。2 回目の reset 後も `01 01 10`（read local version）への 7 でも 13 byte の答えは無い。FR 091（CTS asserted、send FIFO empty、idle）。
- `bt show`: `STATE error local-version Connection timed out (a second reset was answered; neither a 7-byte nor a 13-byte answer came)`、`POWER on`。7 と 13 のどちらの問いにも答えていない（reset だけ答える）。
- `bt scan 10`: `BT SCAN devices=0`（controller が error のため）。
- bluetoothd（/var/log/messages）: state=error を 3 回（error 42、stray 0、malformed 0）→ `3 starts failed in a row; not trying again until restarted`。
- WiFi: `wifi wlan0 search start` → `list` で 6 件（elecom-84a0c3 ch1 rssi -49、elecom-84a0c3-5G ch40 rssi -52 ほか。WPA2/SAE）。dmesg に `bcm43455: event 69 status 8/0` が繰り返す。
後始末: 何も変更していない。ローカルの QEMU は起動していない。
