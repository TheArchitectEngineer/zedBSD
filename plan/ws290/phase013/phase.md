<!-- awesome-plan project=zedbsd record=ws290-p013 -->

# ws290-p013 userland と UAPI

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p013（版 4.5、ghci.md cb937e6）。
- 状態: planned
- 依存: p004

## 範囲

userland と UAPI: `GPU_OP_GHCI_*`・`CONFIG_GHCI_NATIVE` を外す、ghcictl、ghci-blit を削除の list に

詳細は design の該当の節（§4〜§16）と §14 の p013 の場面。

## 受け入れ

- ghcictl の build、amd64 の build の warning 0
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 4 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

design §19 の表のうち、この Phase の code に効く物を着手の時にここへ写す。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
