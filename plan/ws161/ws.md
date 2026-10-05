<!-- awesome-plan project=zedbsd record=ws161 -->
# WS161: YubiKey のサポート

Status: planning（2026-10-05 追加、**ベータ2**（2026-10-05 ユーザー）、見積もり 4 LW。p001 の設計の第 1 版あり）
Master: [master](../master.md)
Primary Milestone: MG006

## 由来

ユーザー（2026-10-05）「YubiKeyサポート（USBのFIDO2が最初。NFCのCTAP2 が目標）」

## 範囲（案、p001 の設計で確定する）

1. USB の HID の FIDO2（CTAPHID）で YubiKey と話す（kernel の USB HID の raw の口、userland の libfido2 に当たる物）。
2. 目標: NFC の CTAP2（NFC の reader の driver が要る）。
3. 鍵の登録・認証の command の試験。

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws161-p001](phase001/phase.md) | 要件と設計 | planning（設計の第 1 版、2026-10-05 P1 q734。判断 H1〜H6（UAPI・外部 package）待ち） | — |
| ws161-p002 | kernel の `usb-fido`・`/dev/fidoN`・試験の loopback・seat、T1 | planned | p001、H1・H4 |
| ws161-p003 | libcbor・libfido2 の package と zedBSD の backend、道具 | planned | p002、H2 |
| ws161-p004 | 実機の UAT（YubiKey）、Linux・FreeBSD | planned | p003 |
| ws161-p005 | 全文規約の見直し | planned | p004 |
