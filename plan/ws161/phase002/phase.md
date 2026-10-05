<!-- awesome-plan project=zedbsd record=ws161-p002 -->

# ws161-p002: kernel の hidraw（`/dev/input/hidrawN`）

Phase ID: `ws161-p002`
Parent: [WS161](../ws.md)
Status: cleared（2026-10-05 Q1: T1-197・T1-199 で hidraw-p002 PASS（QEMU の loopback の key）。実機の xHCI の interrupt OUT は UAT（p006））。以前: in-progress（2026-10-05 P1 generation19。実装と build・host 試験まで、T1 の QEMU の試験待ち）
Phase disposition: normal
Queue: q770 の後（Q1 の指示「WS161 の kernel を先に」、2026-10-05）

## 範囲

p001 §9.2（承認 U1・U3・U4）と §9.10 の見直しの B1・M1・M2・m6・m7。

## 実装（7a1d339c）

- UAPI `include/uapi/hidraw.h`（承認 U1）: `struct hidraw_info`・`hidraw_descriptor`（4096 byte）・`hidraw_text`（64 byte）、ioctl `HIDRAW_GET_INFO`・
  `_GET_DESCRIPTOR`・`_GET_NAME`・`_GET_PHYS`（group 'H'）、read・write・poll の意味は Linux の hidraw と同じ（write の先頭の 1 byte は report ID、無ければ 0）。
- 汎用の class `src/drivers/generic/hidraw.c`・`include/drivers/generic/hidraw.h`: `drv_hidraw_register`・`_input`・`_unregister`。node `hidrawN`（rdev 0x000f0000+N、
  最大 16）、open ごとの 64 個の ring、出力は class の mutex で 1 つずつ、unregister は mutex の下で transport を外し、record は cdev の finalizer で消える。
  事象 `KERN_SYSTEM_EVENT_INPUT`（subject `hidrawN`）。
- 純粋な記述の読み `src/drivers/generic/hidraw-describe.c`（`drv_hidraw_describe`: top の usage、番号付き、入力と出力の大きさ）。
- `usb-hid.c`: report の記述の top が FIDO（0xF1D0・0x01）の interface は raw: 記述を残して input の parser に通さず、`hidraw` を publish、IN の report は
  そのまま `drv_hidraw_input`、出力は interrupt OUT（`drv_usb_interrupt`、5 秒）か SET_REPORT。片付けを `usb_hid_free` にまとめた。
- `devfs.c`: `hidraw*` を `/dev/input` に、`hidraw*`・`smartcard*` は 0600。
- `sessiond/seat.c`（承認 U3）: `/dev/input/hidraw*` と `/dev/smartcard0〜7` を seat の利用者に 0600、戻しは root の 0600。
- 試験の kernel だけの loopback の鍵（承認 U4）`src/drivers/generic/hidraw-loopback.c`（`CONFIG_SECURITY_KEY_TEST_LOOPBACK`、`-DSECURITY_KEY_TEST_LOOPBACK`、
  `vfs.c` で登録）: CTAPHID の INIT・PING（複数の packet）・WINK・不明の command の ERROR・順の誤りの ERR_INVALID_SEQ。
- 試験の program `userland/tests/hidraw-probe`、試験の config `plan/ws161/tests/config-amd64-hidraw.mk`、QEMU の試験 `plan/ws161/tests/hidraw-p002.sh`、
  host 試験 `plan/ws161/tests/hidraw-describe-host-test.sh`。

## 確認

- host: `plan/ws161/tests/hidraw-describe-host-test.sh` PASS（ASan・UBSan）。
- build（warning 0）: amd64 の製品の kernel（12.38 MiB）、試験の config の kernel と `hidraw-probe`、`config/ci/config-pcat.mk`、rpi4（USB HID を入れた build）、
  `sessiond`。
- QEMU（T1）: **未実施**。依頼: 試験の config の image で `hidraw-p002.sh`。
- 実機: **未実施**（YubiKey の interrupt OUT は p006 の UAT）。

## HIDRAW_GRAB（判断 V1 の (a)、2026-10-05 ユーザーの承認）

`HIDRAW_GRAB`（`_IOW('H', 4, int)`）: 掴んだ open だけが入力の report を受け、他の open の write は `EBUSY`、2 つ目の grab も `EBUSY`、掴んだ file の最後の close で放す。docs（`docs/reference/security-keys.md`）を先に直し、`hidraw.c` と `hidraw-probe`（grab の確かめ）を足した。build warning 0。T1 の `hidraw-p002.sh` が grab も確かめる（probe の "HIDRAW grab ok"）。

## 残り

- T1 の結果。
- 判断 V1 は (a) に決まり実装した。V2 は今のまま（ユーザー）。
