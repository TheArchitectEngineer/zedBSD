<!-- awesome-plan project=zedbsd record=ws161-p003 -->

# ws161-p003: kernel の USB CCID と smart card の slot（`/dev/smartcardN`）

Phase ID: `ws161-p003`
Parent: [WS161](../ws.md)
Status: in-progress（2026-10-05 P1 generation19。実装と build・host 試験まで、T1 の QEMU の試験待ち）
Phase disposition: normal
Queue: q770 の後（Q1 の指示「WS161 の kernel を先に」、2026-10-05）

## 範囲

p001 §9.3・§9.3.1（承認 U2、node の名前は `/dev/smartcardN`、Q1: 承認の範囲の中の詰め）と見直しの M7〜M10・m9・m10。外の仕様は
`docs/reference/security-keys.md`。

## 実装

- UAPI `include/uapi/ccid.h`（cfe99700、docs と一緒に先に）: `ccid_info`・`ccid_status`・`ccid_transmit`（64 bit の pointer）・`ccid_event`、
  ioctl `CCID_GET_INFO`・`_GET_STATUS`・`_POWER_ON`・`_POWER_OFF`・`_TRANSMIT`（group 'S'）。
- 汎用の class `src/drivers/generic/smartcard.c`・`include/drivers/generic/smartcard.h`: node `smartcardN`（rdev 0x00100000+N、最大 16）、
  共有の open（open ごとに 16 個の事象）、`CCID_POWER_ON` で claim、claim の持ち主の最後の close で power off、TRANSMIT は claim と電源の入った
  card が要る（`EPERM`・`ENXIO`）、待ちは既定 30 秒・最大 120 秒、command と答えは kernel の buffer を通して後で消す。transport の操作は slot の
  mutex で 1 つずつ、unregister は mutex の下で transport を外す。事象 `KERN_SYSTEM_EVENT_USB`（subject `smartcardN`）。
- USB の transport `src/drivers/usb/usb-ccid.c`（`CONFIG_DRIVER_USB_CCID`、既定 y、amd64・pcat）: class 0x0B、APDU の水準の reader だけ、bulk の
  対、slot は最大 4、reader ごとに command は 1 つ（bSeq を合わせ、他の bSeq の答えは捨てる）、時間の延長は期限まで待ち、期限で ABORT（control と
  `PC_to_RDR_Abort`）、答えの chaining を集める、message は `dwMaxCCIDMessageLength` と 64 KiB の小さい方。worker が 500 ms ごとに GetSlotStatus で
  card の出し入れを知る（interrupt の NotifySlotChange は v1 では使わない）。電圧は reader が選ぶか 1.8・3・5 V の順。
- 純粋な部分 `src/drivers/usb/usb-ccid-proto.c`・`include/drivers/usb/usb-ccid.h`（class 記述子・水準・header・答えの読み）。
- 試験の kernel だけの loopback の card `src/drivers/generic/smartcard-loopback.c`（`CONFIG_SECURITY_KEY_TEST_LOOPBACK`）: SELECT（FIDO の AID で
  "FIDO_2_0" 90 00、他は 6A 82）、長い答えの 61 xx と GET RESPONSE、抜いて戻す（事象 2 つ）。
- `devfs.c`（p002 で）: `smartcard*` は 0600。`sessiond/seat.c`（p002 で）: `/dev/smartcard0〜7` を seat の利用者に。
- 試験: `userland/tests/smartcard-probe`、`plan/ws161/tests/smartcard-p003.sh`、`plan/ws161/tests/ccid-proto-host-test.sh`。

## 確認

- host: `plan/ws161/tests/ccid-proto-host-test.sh` PASS（ASan・UBSan）。
- build（warning 0）: amd64 の製品の kernel（12.39 MiB）、試験の config の kernel と `smartcard-probe`、`config/ci/config-pcat.mk`。
- QEMU（T1）: **未実施**。依頼: 試験の config の image で `smartcard-p003.sh`（p002 の `hidraw-p002.sh` と同じ image）。
- 実機: **未実施**（ACR1252U は p006 の UAT。`smartcard-probe -i` で reader の情報、card の ATR、FIDO の SELECT）。

## 残り

- T1 の結果。
- USB の CCID の実物の確かめ（UAT）: ACR1252U の class 記述子（APDU の水準か、slot の数、`dwMaxCCIDMessageLength`）、YubiKey の CCID の interface。
