<!-- awesome-plan project=zedbsd record=ws164 -->
# WS164: OS の起動時の Welcome の画面

Status: planning（2026-10-05 追加、**ベータ2**（2026-10-05 ユーザー）、見積もり 1 LW。p001 の設計の第 1 版あり）
Master: [master](../master.md)
Primary Milestone: MG006

## 由来

ユーザー（2026-10-05）「OS起動時のWelcome画面（すでにあるStartでもいいかも。要検討）」

## 範囲（案、p001 の設計で確定する）

最初の起動（または毎回）の Welcome の画面。既存の Start（Files の Today など）で足りるかの検討から始める。

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws164-p001](phase001/phase.md) | 要件と設計 | planning（設計の第 1 版、2026-10-05 P1 q732。判断 H1〜H4 待ち） | — |
| ws164-p002 | 設定の key・compositor の起動・Settings の welcome の mode・host 試験・T1 | planned | p001、H1〜H4 |
| ws164-p003 | 全文規約の見直し | planned | p002 |
