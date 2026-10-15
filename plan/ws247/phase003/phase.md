<!-- awesome-plan project=zedbsd record=ws247-p003 -->

# ws247-p003: nvrtx の compiler（sm75.c・sm75.h）に p001 と同じ出典の規則を当てる

Status: in-progress（q987〜q989、2026-10-16）
Parent: [WS247](../ws.md)
Approval: 2026-10-16 ユーザー「sm75.[ch]にも同じルールを適用して監査、修正して」

## 範囲

[p001](../phase001/phase.md) と同じ: 定数・表の block に「Value(s) referenced:」、手順を参考にした関数に「Procedure referenced:」を block の直前の複数行の comment で付ける。referenced: の段落の外の出典の名前（nouveau・Mesa・NVK・NAK・Linux・open-gpu）を除く。開発の管理情報（段・試験の回・WS 番号）も除く。**comment だけを変える。** WS246 p002 の code の refactor（latency の if の連鎖を表に、構造の見直し）は含まない。

## 分担

| Queue | 担当 | 範囲 |
| --- | --- | --- |
| q987 | P1 | compiler/sm75.h（全体） |
| q988 | P2 | compiler/sm75.c の 1〜3026 行（main 7f489c51f の行番号、nvrtx_sm75_binary_free の前まで） |
| q989 | P3 | compiler/sm75.c の 3027 行〜末尾（nvrtx_sm75_binary_free から） |

## 確かめ

p001 と同じ（cpp の比較、build warning 0、監査の grep）。Q1 が merge の後に host 試験 sm75・desktop・vk-d3・vk。

## 結果

（Q1 が書く）
