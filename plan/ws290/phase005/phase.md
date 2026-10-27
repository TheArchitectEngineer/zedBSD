<!-- awesome-plan project=zedbsd record=ws290-p005 -->

# ws290-p005 session・VM

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p005（版 4.5、ghci.md cb937e6）。
- 状態: planned
- 依存: p004

## 範囲

session・VM（chunk・勘定・map・unmap・close・再生、flat の恒等の VA）、memory（host、4 GiB の上限）、resource・blob・map・read・write、share・import。local は ENOTSUP。+ `SHARE | ALLOCATION_SHARE`

詳細は design の該当の節（§4〜§16）と §14 の p005 の場面。

## 受け入れ

- p005 の場面 PASS、build の warning 0
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 26 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

design §19 の表のうち、この Phase の code に効く物を着手の時にここへ写す。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
