<!-- awesome-plan project=zedbsd record=ws162 -->
# WS162: FIDO2 の login

Status: planning（2026-10-05 追加、**ベータ2**（2026-10-05 ユーザー）、見積もり 2 LW。p001 の設計の第 1 版あり）
Master: [master](../master.md)
Primary Milestone: MG006

## 由来

ユーザー（2026-10-05）「FIDO2ログイン」

## 範囲（案、p001 の設計で確定する）

greeter と lock の画面で、登録した security key（WS161）で login・unlock する。登録は Settings の Users の頁。password との併用の規則は設計で決める。

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws162-p001](phase001/phase.md) | 要件と設計 | planning（設計の第 1 版、2026-10-05 P1。判断 H1〜H5（root の daemon の口を含む）と WS161 の判断待ち） | WS161 |
| ws162-p002 | sessiond の口と保存、検証の helper | planned | WS161 p003、p001 |
| ws162-p003 | greeter・lock の画面・Settings の Users、T1 | planned | p002 |
| ws162-p004 | 実機の UAT、全文規約 | planned | p003 |
