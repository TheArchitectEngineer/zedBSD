<!-- awesome-plan project=zedbsd record=ws163 -->
# WS163: 数字 6 桁の login

Status: planning（2026-10-05 追加、段と見積もりは未定）
Master: [master](../master.md)
Primary Milestone: MG006

## 由来

ユーザー（2026-10-05）「数字6桁のログイン」

## 範囲（案、p001 の設計で確定する）

greeter と lock の画面で数字 6 桁の PIN で login・unlock する。PIN の保存（hash）、試行の制限、password との関係、sudo・SSH では使わないかを設計で決める。

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| ws163-p001 | 要件と設計 | planning | — |
