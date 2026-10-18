<!-- awesome-plan project=zedbsd record=ws260-p001 -->

# ws260-p001 Gen9 の display の書き直しの設計

Status: cleared（2026-10-18 Q1 判定: design.md（b9fe9a6d6、merge 6d8b00743）、判断 5 点はユーザーが回答（ws.md））（旧: in-progress（2026-10-18 q1252-i01、P1））

- 親: [WS260](../ws.md)。Queue: q1252-i01。code は書かない。

## 実行の記録（2026-10-18 q1252-i01、P1）

- 設計: [design.md](../design.md)。棚卸し（22 file、9,590 行、外の呼び手）、写しの点検（comment の 7 語の一致 0、code の 5 token の一致は最大 0.19、`Procedure referenced:` の 56 組の構造の類似度 0.04〜0.71。字面の写しは無く、手順と分け方が Linux の関数に沿う。関数の head の comment に Linux の関数名 63 個）、xd との共有（既定は案 A: xd に display 9 の platform を足す）、段（p002〜p015、約 60 LW。golden の書き込みの列と数の計算の表を回帰の柱に、実機は最後に T1 が SV7 と 5330 で 1 回）、判断の点 5 つ。
- 点検の展開物は build/tmp/ws260-p001-*/（消すのは Q1）。code は書いていない。
