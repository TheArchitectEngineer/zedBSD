<!-- awesome-plan project=zedbsd record=ws161 -->

# WS161: YubiKey のサポート

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」。根拠: p001〜p005 cleared、2026-10-10 ユーザー「FIDO2のキー登録とログインができました。」（USB の YubiKey）、p006 の実機の UAT は 2026-10-13 ユーザー「OKでした」。Linux・FreeBSD の FIDO2 は 2026-10-13 夜 ユーザー「Linux/FreeBSDではFIDO2はまだ入れなくていいです。…ベータ3にします。」）
<!-- retained-after-completion --> phase001 の phase.md は残す: userland/base/fidoctl/main.c:10、userland/base/libpasskey/nfc.h:9・os.h:9・25・os-linux.c:191 の comment が設計 §9.4 を参照している。source の comment を直す（または仕様を docs/ へ移す）までの間
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

YubiKey を使えるようにする。最初は USB の HID の FIDO2（CTAPHID）、目標は NFC の CTAP2。

## ユーザーの決定

- 2026-10-05「YubiKeyサポート（USBのFIDO2が最初。NFCのCTAP2 が目標）」。ベータ2（2026-10-05 ユーザー）。
- 2026-10-05 夕: 第 2 版の決定。`usb-hid` の hidraw（`/dev/input/hidrawX`、`/dev/fidoN` は無し）、YubiKey の CCID と ACR1252U の NFC の reader は共通の `usb-ccid`、独自の libpasskey（CTAPHID・NFC の APDU・CTAP2・CBOR・PIN/UV、暗号は OpenSSL）、OTP は非対応。判断 U1〜U5 をユーザーが承認（CCID の node の名前は `/dev/smartcardN`）。
- 2026-10-08 ユーザー: 規約の全文の Phase はベータ3 → p007 は WS の外の一括の見直しへ。
- 2026-10-10 UAT「FIDO2のキー登録とログインができました。」（USB の YubiKey）
- 2026-10-13「これ以上ベータ2に機能追加しないつもりなので、実装を終わらせましょう。」（q954 の p006）。同日 (c)「Linux・FreeBSD には入れない」、夜「Linux/FreeBSDではFIDO2はまだ入れなくていいです。あとでLinux標準のFIDO2について調べて入れますが、ベータ3にします。」→ os-freebsd.c と fidoctl の Makefile.linux・Makefile.freebsd は main から外した。
- 2026-10-13 実機の UAT「OKでした」（USB の YubiKey）

## 結果

- kernel: `usb-hid` の hidraw（`include/uapi/hidraw.h`、seat の一覧、試験の loopback）、`usb-ccid`（`include/uapi/ccid.h`、`/dev/smartcardN`）。userland: libpasskey（cbor・transport-hid・transport-nfc・ctap2・pin・verify・os 層）と道具 `fidoctl`。いずれも zedBSD だけ。
- 証拠: QEMU の T1-197・T1-199（hidraw・smartcard の loopback）、T1-274（fidoctl-p004）、T1-374（p005）。実機: USB の YubiKey の登録と login（2026-10-10・10-13 のユーザーの確かめ）。

## 制限・移管

- NFC（ACR1252U と YubiKey 5 NFC）の実機の確かめは記録に無い（未実施）。
- Linux・FreeBSD の FIDO2 は入れない（ユーザーの決定、ベータ3 の段は 2026-10-27 に取り消し。やるなら新しい WS）。
- FIDO2 の login（greeter・lock）は WS172・WS199。
- 規約の全文（p007）は WS の外の一括の見直しへ。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws161-p001 | 要件と設計 | cleared（U1〜U5 はユーザーが承認） |
| ws161-p002 | kernel: `usb-hid` の hidraw、seat の一覧、試験の loopback | cleared（T1-197・T1-199） |
| ws161-p003 | kernel: `usb-ccid`、seat の一覧 | cleared（T1-199） |
| ws161-p004 | libpasskey と `fidoctl`、host 試験 | cleared（T1-274） |
| ws161-p005 | libpasskey: transport-nfc と `/dev/smartcard*` | cleared（T1-374） |
| ws161-p006 | 実機の UAT（USB・NFC）、Linux・FreeBSD の build | in-progress のまま WS を閉じた（USB の UAT は OK、NFC は未実施、Linux・FreeBSD はユーザーの決定で入れない） |
| ws161-p007 | 規約の全文の見直し（directory なし） | 規約の全文は WS の外の一括の見直しへ |

## 記録の所在

phase001 は上の理由で残す。他の Phase の記録と試験は削除する（Q1）。git の commit `fb1f6afee` の `plan/ws161/` にある。source の comment が名指す host 試験（`hidraw-describe-host-test.sh`・`ccid-proto-host-test.sh`）は plan/tools/ へ移す。Queue の履歴は [plan/history](../history/index.md)。
