<!-- awesome-plan project=zedbsd record=ws288-p037 -->

# ws288-p037 算術の共通の module（乗算・除算・加算・減算・shift を am_ の module に限る）

- 親: [WS288](../ws.md)。規則: amaterrace-gpu の `docs/style.md`（この Phase で規則を足す）。
- 状態: in-progress（2026-10-25 12:58 Q1、q1577 P1）
- 依存: なし。p036（P2、FPU の am_mul・TU・ROP・RX の削減）・p034（P1）・p035（P3、DC）と同じ file を触るので、block ごとに担当を分ける（下）。

## 決定（2026-10-25 12:58 ユーザー）

「すべてのモジュールにおいて、乗算器は * の記号ではなく、自前のモジュールにするのがいいですね。その自前のモジュールでのみ * を使います。また、加算器、減算器、除算器、シフターなども、同じです。乗算器、除算器、加算器、減算器、シフターなどは、共通で使うモジュールとして定義して、モジュール内でインスタンス化することで、真に必要な個数だけ利用してください。こうすることで、必要な個数を抑えるとともに、DSPセルの選択のように、FPGAボードで最適な出力を得られやすくなります。そのモジュールがDSPセルになるかをチェックすればいいので。」

## 範囲（amaterrace-gpu）

1. `docs/style.md` に規則を足す: 信号の算術（`*` `/` `%` `+` `-`、変数の量の shift）は `rtl/common/` の共通の module の中だけで書き、他の module はそれを instance にする。同じ module の中で同時に使わない演算は 1 つの instance を共有する。対象の外（Q1 の案、ユーザーの確認待ち）: parameter・localparam の式（幅の計算など elaboration の時の定数）、定数の量の shift（配線）、TB と `sim/`。
2. 共通の module: `am_mul`（P2 が p036 で追加、use_dsp）、`am_add`・`am_sub`（または `am_addsub`、carry の入出力）、`am_div`（整数、多 cycle）、`am_shift`（論理・算術、左右、barrel）。各 module に TB と、Vivado の OOC で何の cell になるか（DSP48E1・CARRY4・LUT）の記録。
3. RTL の書き換え（回路の機能は変えない、TB は PASS のまま）。担当の分け方: P1 = 共通の module・規則・CP・MC・SU（FPU・PK を除く）・VF・BLT・その他。P2 = p036 で触る FPU・PK・fmt・TU・ROP・RX。P3 = DC（p035 で移す時）。
4. lint の点検: `tools/lint.sh` か別の script で、`rtl/common/` の外の信号の算術の記号を数えて 0 を確かめる（例外は理由付きで表に）。

## 受け入れ

- 規則が docs/style.md にあり、共通の module の TB が PASS、各 module の cell の種類を記録。
- `rtl/common/` の外の信号の算術の記号が 0（例外の表を除く）。
- make sim・verilator・lint が PASS（T に回す）。KC705 の合成の LUT・DSP の前後。

## 記録

- 2026-10-25 12:58 Q1: 新設、P1 に（p034 の R6 が P3 の 398a738 の merge 待ちの間）。
- 2026-10-25 13:10 ユーザーの回答（Q1 の問い「小さな counter や pointer の +1、比較（< や ==）も共通の module にしますか？」）: 「counter も含める」（選択肢の説明: 信号の + - は counter の +1 も全部、共通の module を instance にする。比較・parameter の式・定数の shift だけ対象外）。→ 範囲の 1 の「対象の外」は、parameter・localparam の式、定数の量の shift、比較（< > == など）、TB と sim/ に確定。
- 2026-10-25 13:10 P1 → Q1: 95b6481（p1-p037、基点 481030c）: docs/style.md の Arithmetic 節、am_add・am_sub・am_addsub・am_div（1 bit/cycle）・am_shift、am_mul は P2 の 40e8377 の file を同じ内容で、sim/tb/am_arith_tb.v PASS、spec/common の 5 本（cell は期待値、OOC 未実施）、tools/arith-lint.py（Verilator の XML で数える、今 1203、うち include/ の function 157）。
- 2026-10-25 13:10 Q1 の判断（P1 の問い）: (a) include/*.vh の function の算術も規則の対象（ユーザー「すべてのモジュールにおいて」）。lint は include/ も数える。function は module に作り直す。FP・format・TU の function（atg_su_fp・fp16・tu_fn・fmt_pack・fmt_unpack・tu_fmt）は P2、depth などその他は P1。(b) CP・MC・SU の書き換えは p034 が main に入ってから。先に VF・BLT（avg/favg は P2）。
