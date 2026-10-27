<!-- awesome-plan project=zedbsd record=ws290-p015 -->

# ws290-p015 規約の全文の見直し

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p015（版 4.5、ghci.md cb937e6）。
- 状態: planned
- 依存: p002〜p014

## 範囲

規約の全文の見直し: WS が変えた C の全部、全 host 試験、amd64・riscv64 の warning 0、tree に 1.1 の code が無い grep、stub の `drv_gpu_register` を `gpu.c` と照合、試験を `plan/tools.md` に登録を Q1 に依頼

詳細は design の該当の節（§4〜§16）と §14 の p015 の場面。

## 受け入れ

- 規約の違反 0、全 PASS、warning 0
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 10 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

design §19 の表のうち、この Phase の code に効く物を着手の時にここへ写す。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
