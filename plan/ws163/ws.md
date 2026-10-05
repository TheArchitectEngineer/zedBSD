<!-- awesome-plan project=zedbsd record=ws163 -->
# WS163: 数字 6 桁の login

Status: planning（2026-10-05 追加、**ベータ2**（2026-10-05 ユーザー）、見積もり 1 LW。p001 の設計の第 1 版あり）
Master: [master](../master.md)
Primary Milestone: MG006

## 由来

ユーザー（2026-10-05）「数字6桁のログイン」

## 範囲（案、p001 の設計で確定する）

greeter と lock の画面で数字 6 桁の PIN で login・unlock する。PIN の保存（hash）、試行の制限、password との関係、sudo・SSH では使わないかを設計で決める。

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws163-p001](phase001/phase.md) | 要件と設計 | planning（設計の第 1 版、2026-10-05 P1 q733。判断 H1〜H5（root の daemon の口を含む）待ち） | — |
| ws163-p002 | sessiond の PIN、host 試験 | planned | p001、H1〜H5 |
| ws163-p003 | greeter と lock の画面、Settings の Users、T1 | planned | p002 |
| ws163-p004 | 全文規約の見直し | planned | p003 |
