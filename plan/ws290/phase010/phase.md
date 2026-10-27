<!-- awesome-plan project=zedbsd record=ws290-p010 -->

# ws290-p010 転送の shader による代替

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p010（版 4.5、ghci.md cb937e6）。
- 状態: planned
- 依存: p009

## 範囲

転送の shader による代替（§11.6、U10）: `vk-meta.c`（内部の shader を IR で組む）、compute の copy・fill・update・query・buffer と image の写し・resolve、描画の blit、転送の後の bind の置き直し。gsi1model.py での試験

詳細は design の該当の節（§4〜§16）と §14 の p010 の場面。

## 受け入れ

- p010 の場面 PASS、build の warning 0
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 20 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

design §19 の表のうち、この Phase の code に効く物を着手の時にここへ写す。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
