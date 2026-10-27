<!-- awesome-plan project=zedbsd record=ws290-p004 -->

# ws290-p004 core

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p004（版 4.5、ghci.md cb937e6）。
- 状態: planned
- 依存: p003

## 範囲

core: pci-attach（match・Bus Master の順）、identify・caps・CP_HEAP（flat は 16 KiB）・ring・fence・IRQ・poll・watchdog、fault（FAULTED に依らず reset、犯人の記録、U5）・reset・LOST、detach。1.1 の file（firmware・native・state・blit・frame・packets・hcr）と vk/・display・compiler を build から外し、呼ぶ所を消す。capability は p004 で callback を全部実装した物だけを出す（`gpu.h` の「Optional capabilities require their complete callback pair」。resource・blob・map は p005 で足す。R17）

詳細は design の該当の節（§4〜§16）と §14 の p004 の場面。

## 受け入れ

- amd64・riscv64 の build の warning 0、p004 の場面 PASS、build に入る file に 1.1 の名前が無い（grep）
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 22 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

design §19 の表のうち、この Phase の code に効く物を着手の時にここへ写す。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
