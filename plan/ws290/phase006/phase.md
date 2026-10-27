<!-- awesome-plan project=zedbsd record=ws290-p006 -->

# ws290-p006 compiler

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p006（版 4.5、ghci.md cb937e6）。
- 状態: planned
- 依存: p002・p004

## 範囲

compiler: header・interface 表を捨て §12 の ABI（`gsi1-abi.h`）、資源の表、options、`compiler/gsi1_isa.h` を bea1ef9 の値（`GSI1_COLOR_ATTACHMENTS` 8、`GSI1_EXP_COLOR0_SRC1`、`GSI1_VS_REG_*`、lda の word 0〜3）に直す（R18）、FS の key に primitive の種類（R08）、compiler を build に戻す、gsi1 の試験と tools を移す

詳細は design の該当の節（§4〜§16）と §14 の p006 の場面。

## 受け入れ

- gsi1 の試験 PASS、build の warning 0。graphics の I/O は cf8be08 §12.10.3 で
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 22 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

design §19 の表のうち、この Phase の code に効く物を着手の時にここへ写す。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
