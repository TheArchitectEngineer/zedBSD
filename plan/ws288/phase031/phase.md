<!-- awesome-plan project=zedbsd record=ws288-p031 -->

# ws288-p031 ZYBO を GPU の target から外す（source は docs/old/ へ）

- 親: [WS288](../ws.md)。
- 状態: cleared（2026-10-25 05:13 Q1。T1-759 PASS）。以前: in-progress（2026-10-25 04:10 q1561 P1）
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
- 2026-10-25 04:27 P1 → Q1: d560d94（親 5a7a5c4）。ZYBO の board・spec・VGA（atg_dc_vga）・ZYBO の firmware の image・tb_shape.vh・ZYBO の lint の stub を docs/old/zybo/ へ git mv、atg_top_* の TB は KC705 だけ、lint.sh・fw-build.sh・vivado-build.sh から ZYBO、DC_CAPS[0] VGA を 0 に。P1 の申告: lint clean 101、単体 TB 7/7 PASS、atg_top_* 6 本は compile だけ（T1-759 に回した）。Q1 の判断: (1) atg_top と block の parameter の既定（LINE_BYTES 32 など）はそのまま（KC705 の値は board の top が渡す）、(2) Verilator の harness が既定の 32 B の形のままで KC705 の 512 bit の形を通らない → p030 S11（統合）で harness を KC705 の形にする、(3) STANDALONE と atg_dc_tmds は残す、(4) 32 B の top の網羅が無くなるのは可。
- 2026-10-25 05:11 Q1: d560d94 を含む p1-p031 3e24ad1 を amaterrace-gpu の main に merge（a87523c、衝突なし、merge の後 lint clean 103）。P1 の作った空の directory `rtl/common/fmt/` を Q1 が消した。T1-759（d560d94）と T1-762（main 5f3b747）の結果で clear を判定する。
- 2026-10-25 05:13 Q1: T1-759（d560d94）PASS（top 6 本、sim-quick 78、verilator 5、lint 101、log を Q1 が数えて確かめた）→ cleared。
