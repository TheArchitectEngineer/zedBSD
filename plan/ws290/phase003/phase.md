<!-- awesome-plan project=zedbsd record=ws290-p003 -->

# ws290-p003 model・stub・script

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p003（版 4.5、ghci.md cb937e6）。
- 状態: planned
- 依存: p002

## 範囲

model・stub・script（§14）。WS288 の 1.1 の試験の削除の list を Q1 へ

詳細は design の該当の節（§4〜§16）と §14 の p003 の場面。

## 受け入れ

- model の自己試験 PASS
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 16 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

design §19 の表のうち、この Phase の code に効く物を着手の時にここへ写す。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
