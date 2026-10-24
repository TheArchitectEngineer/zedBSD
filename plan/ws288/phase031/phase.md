<!-- awesome-plan project=zedbsd record=ws288-p031 -->

# ws288-p031 ZYBO を GPU の target から外す（source は docs/old/ へ）

- 親: [WS288](../ws.md)。
- 状態: in-progress（2026-10-25 04:10 q1561 P1）
- 依存: なし。p030 の S1 の前に行う（p030 の U57 がこの決定で消える）。
- 承認: 2026-10-25 04:10 ユーザー「U項目は推しでOKです。U57について、ZYBOはもう合成しません。ターゲットから外し、ソースは docs/old/ 以下に移します。」

## 範囲（amaterrace-gpu）

- `boards/zybo/`・`spec/boards/zybo/` と ZYBO だけの RTL（VGA の `atg_dc_vga` など、ZYBO の board の top・XDC・tcl）を `docs/old/` の下に移す（`git mv`、中身は変えない、`docs/old/README.md` に「合成しない旧い target」と一言）。
- build・lint・sim・CI・Makefile・tools から ZYBO の target と shape を外す: `tools/lint.sh` の ZYBO の形、二つの形の TB（`+shape=zybo`、`tb_shape.vh`）を KC705 だけに、`make sim-quick` の組、`vivado-build.sh`・openXC7 の ZYBO の target、CI の job。parameter の既定値が ZYBO の値の所（LINE_BYTES 32 など）は、KC705 の値に変えるか今のままにするかを決めて記録（回路の意味を変える物は Q1 に相談）。
- README・`docs/ghci.md`・spec の ZYBO への言及を直す（README はユーザーの文書なので ZYBO の行を外すだけ）。
- zedBSD の側（`plan/tools/zybo/`、WS286・WS289 の ZYBO の OS の port）は対象外。

## 受け入れ

`git grep -i zybo` が `docs/old/` と履歴の説明の外に残らない（残す物は理由を記録）。`make lint` clean、変えた TB が PASS。`make sim`・`sim-quick`・`verilator` は T に回す。

## 記録

- 2026-10-25 04:10 Q1: 新設（ユーザーの決定）。
