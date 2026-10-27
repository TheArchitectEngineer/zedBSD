<!-- awesome-plan project=zedbsd record=ws290-p002 -->

# ws290-p002 語の層

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p002（版 4.5、ghci.md cb937e6）。
- 状態: planned
- 依存: p001

## 範囲

語の層: `ghci-regs.h`・`ghci-cmd.h`・`cmd.c`（§4.4 の全部）・`caps.c`（decode と CP_HEAP の勘定）・ring の計画と fence64・EXTCAP の走査（`check.c` に新しい名前で足す）。build に入る今の file は変えない（新しい file は build に入れない）。`ghci-ref-host-test.c`（§14、R23）

詳細は design の該当の節（§4〜§16）と §14 の p002 の場面。

## 受け入れ

- words と ref の試験 PASS、新しい file を amd64 と riscv64 の kernel の flag で compile して warning 0（R23）、`CONFIG_DRIVER_PCI_GHCI=y` の amd64 が今のまま build
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 14 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

design §19 の表のうち、この Phase の code に効く物を着手の時にここへ写す。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
