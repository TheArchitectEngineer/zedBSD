<!-- awesome-plan project=zedbsd record=ws161 -->
# WS161: YubiKey のサポート

Status: planning（2026-10-05 追加、段と見積もりは未定）
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
| ws161-p001 | 要件と設計 | planning | — |
