<!-- awesome-plan project=zedbsd record=ws290-p013 -->

# ws290-p013 userland と UAPI

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p013（版 4.5、ghci.md cb937e6）。
- 状態: cleared（2026-10-28 P3 q1642-i01。受け入れ: ghcictl の build、amd64 の build の warning 0）
- 依存: p004

## 範囲

userland と UAPI: `GPU_OP_GHCI_*`・`CONFIG_GHCI_NATIVE` を外す、ghcictl、ghci-blit を削除の list に

詳細は design の該当の節（§4〜§16）と §14 の p013 の場面。

## 受け入れ

- ghcictl の build、amd64 の build の warning 0
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 4 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

無い（design §19）。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
- 2026-10-28 P3（q1642-i01、branch agent/p3-ws290f）: 実装。
  - 3f8a8f222: `Makefile` の CONFIG_GHCI_NATIVE の行だけ（comment の句・`?= n`・`-D`）を外した（Q1 の許可、別の commit）。
  - 6c4445f3a: `include/uapi/gpu-op.h` の `GPU_OP_GHCI_*`（0x10100〜0x10108・0x10180・0x10181）と comment の block を外した（U1）。`userland/base/ghcictl` を `list` と `display`（GPU_DISPLAY_QUERY と GPU_DISPLAY_MODE で mode を出す）だけに（U6: diag・paint・fwload を外した）。`plan/ws290/tests/config-*-ghci.mk` に ghcictl・ghci-display。
  - a3a1a5183: `userland/tests/ghci-blit/` の git rm（U6、Q1 の許可）と `plan/ws288/tests/config-*-ghci.mk` の CONFIG_GHCI_NATIVE と ghci-blit の 2 行だけ（Q1 の許可）。
  - 確かめた事: build（exit 0・warning 0）: config-amd64-ghci の vmunix・bin/ghcictl・bin/ghci-display（build/ws290-p013-amd64）、config-visionfive2-ghci の vmunix・bin/ghcictl（build/ws290-p013-vf2）、config/ci/config-amd64.mk の vmunix（build/ws290-p013-amd64-ci）。tree の C・header・mk に `GPU_OP_GHCI`・`CONFIG_GHCI_NATIVE` が残るのは build の外の `src/drivers/gpu/ghci/vk/`（p007 で書き直す）だけ。
  - 未実施: ghcictl の実行（実機は p014）。`plan/tools/zybo/ghci-blt-board.py`（1.1 の packet を組む道具）は Q1 が消す。
