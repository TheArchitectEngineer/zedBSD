<!-- awesome-plan project=zedbsd record=ws165 -->
# WS165: 手書きの入力

Status: planning（2026-10-05 追加、**ベータ2**（2026-10-05 ユーザー）、見積もり 4 LW。p001 の設計の第 1 版あり）
Master: [master](../master.md)
Primary Milestone: MG006

## 由来

ユーザー（2026-10-05）「手書き入力（ゴール設定が難しいので段階化する）」

## 範囲（案、p001 の設計で確定する）

段階化: 段 1 の goal を設計で決める（例: 英数字の単一の文字の認識 → 日本語のひらがな・漢字 → 連続の筆記）。WS102 のスクリーンキーボードの手書きと WS095 の IME との関係を整理する。

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws165-p001](phase001/phase.md) | 要件と段の設計（段 1: 1 文字ずつの英数字・かな・記号、点群の照合） | planning（設計の第 1 版、2026-10-05 P1 q739。判断 H1〜H4 待ち） | — |
| ws165-p002 | 手本の data と点群の照合、host 試験 | planned | p001、H1〜H3 |
| ws165-p003 | compositor の `zwl_hand_recognize`、ink の記録、T1 | planned | p002 |
| ws165-p004 | 実機の UAT、全文規約 | planned | p003 |
