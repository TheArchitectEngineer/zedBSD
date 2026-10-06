<!-- awesome-plan project=zedbsd record=ws161 -->
# WS161: YubiKey のサポート

Status: planning（2026-10-05 追加、**ベータ2**（2026-10-05 ユーザー）、見積もり 4 LW → 第 2 版で 6〜7 LW（U5）。p001 の設計の第 2 版（§9）あり、q770）
Master: [master](../master.md)
Primary Milestone: MG006

## 由来

ユーザー（2026-10-05）「YubiKeyサポート（USBのFIDO2が最初。NFCのCTAP2 が目標）」

## 範囲（案、p001 の設計で確定する）

1. USB の HID の FIDO2（CTAPHID）で YubiKey と話す（`usb-hid` の hidraw `/dev/input/hidrawN`、userland の libpasskey）。
2. NFC の CTAP2: ACR1252U（USB の CCID）の共通の `usb-ccid`（`/dev/smartcardN`、APDU の交換）と libpasskey の NFC の transport。
3. 鍵の情報・PIN・登録・認証の道具 `passkey` と試験。詳細は p001 §9（第 2 版）。

## 第 2 版への変更（2026-10-05 夕のユーザーの決定）

`usb-hid` の hidraw（`/dev/input/hidrawX`、`/dev/fidoN` は無し）、YubiKey の CCID と ACR1252U の NFC の reader は共通の `usb-ccid`、独自の
libpasskey（CTAPHID・NFC の APDU・CTAP2・CBOR・PIN/UV、暗号は OpenSSL）、OTP は非対応。段を p002〜p007 に組み直した（実行前、p001 §9.6）。
判断 U1〜U5（p001 §9.9）は 2026-10-05 ユーザーが承認（CCID の node の名前は `/dev/smartcardN`）。

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws161-p001](phase001/phase.md) | 要件と設計 | planning（第 1 版 q734、第 2 版 §9 q770。U1〜U5 承認済み） | — |
| [ws161-p002](phase002/phase.md) | kernel: `usb-hid` の hidraw、`include/uapi/hidraw.h`、seat の一覧、試験の loopback。T1 | in-progress（実装 7a1d339c、build と host 試験済み、T1 待ち。V1 は判断待ち） | p001、U1・U3・U4 |
| [ws161-p003](phase003/phase.md) | kernel: `usb-ccid`、`include/uapi/ccid.h`、seat の一覧。T1 | in-progress（docs・UAPI cfe99700、実装と host 試験済み、T1 待ち） | p001、U2・U3 |
| [ws161-p004](phase004/phase.md) | libpasskey: cbor・transport-hid・ctap2・pin・verify・os 層、道具 `fidoctl`、host 試験 | cleared（2026-10-07、T1-274） | p002（os 層だけ） |
| ws161-p005 | libpasskey: transport-nfc と `/dev/smartcard*`、host 試験 | planned | p003・p004 |
| ws161-p006 | 実機の UAT（YubiKey 5 の USB、ACR1252U と YubiKey 5 NFC）、Linux・FreeBSD の build | planned | p005 |
| ws161-p007 | 全文規約の見直し | planned | p006 |
