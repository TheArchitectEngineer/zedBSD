<!-- awesome-plan project=zedbsd record=ws288-p026 -->

# ws288-p026 RTL の書き方の直しと短い sim

- 親: [WS288](../ws.md)。規則: amaterrace-gpu の `docs/style.md`、zedBSD の [Guardrail](../../guardrail.md) の「source の中の開発の管理情報」。
- 状態: cleared（2026-10-25 01:02 Q1。amaterrace-gpu の merge 1e1a90c）
- 依存: なし（p009 の前に行う）。
- 承認: 2026-10-24 ユーザー「では、まずは気づいた点を直すところから始めましょう。simは長いものも残しつつ手短に確認できるものがあるといいですね。」（Q1 が SU の RTL の特徴を説明した時の「気づいた点」1〜3 への回答）

## 範囲（amaterrace-gpu）

1. **STATE の番号に名前を付ける**: `rtl/` の生の STATE の番号（例 `atg_su_sched.v` の `9'h190`・`9'h1E9`、cp・vf・rx・tu・rop にもある）を `include/ghci_regs.vh` の名前（BLT の範囲の `GHCI_S_BLT_*` と同じ形）に置き換える。名前と値は `docs/ghci.md` の STATE の表に合わせる。回路は変えない。
2. **開発の管理情報を source から外す**: `rtl/`・`boards/`・`include/`・`sim/`・`tools/`・`fw/`・`Makefile` の comment と文字列から、WS・Phase の番号（「Implemented in WS288 p008」など）と zedBSD の `plan/` への参照を外す。comment だけで意味が通る形に直す（design の節番号と `spec/` への参照は残してよい）。`spec/`・`docs/` は対象外（別途）。
3. **短い sim**: 長い TB（`atg_su` 約 25 分、`atg_top_shade`・`atg_top_tex` 2〜3 分など）は今のまま `make sim` に残す。それとは別に、数分で全 block を一通り確かめる短い組（例 `make sim-quick`）を作る。長い TB は plusarg などで縮めた版（program・scene の数を減らす）を短い組に入れる。どの TB が長いかは測って決め、短い組の時間の目安と中身を `sim/run.sh` の注記と README の道具の表に書く。

## 受け入れ

- `make lint` が clean（今は 106 項目）。
- 1・2 は回路を変えない: 変えた file の TB と `make verilator` が PASS。可能なら変更の前後で yosys の合成の結果（`make oxc-core` の cell の数）か netlist が同じであることを示す（時間がかかるなら 1 回だけ、または未実施と書く）。
- 3: 短い組が全部 PASS し、時間を測って記録。`make sim` の全部（長い物を含む）を 1 回流して PASS。
- commit は担当の branch に `WIP`、Q1 が amaterrace-gpu の main に `--no-ff` で merge。push はしない。

## 記録

- 2026-10-24 22:50 Q1: 新設、q1554 で P1 に。
- 2026-10-25 01:02 P1 → Q1: 8f4fe1d（STATE の名前）・41d56ab（WS・Phase の番号を外す、62 file）・a0b599b（絶対 path を相対に、ユーザー「絶対パスは相対にしましょう。」）・d0a5794（`make sim-quick`）。P1 の申告: lint 106 clean、`make sim` 80/80 PASS（961 s）、`make sim-quick` 78 PASS（345 s）、verilator 5 本 PASS。回路の不変: comment だけの file は comment を除いた token が同じ、part 1 の 15 module は yosys の proc 後の netlist が同じ、core 全体の generic の netlist の stat が同じ。`make oxc-core`（synth_xilinx）は cell の数が 163015 → 162201 と違う（FDRE・DSP・RAM は同じ、LUT・CARRY4・MUXF だけ。base の再実行は 163015 で再現。P1 の推定は行番号の変化で yosys の share・ABC の打ち切りが変わる、未証明）。
  - Q1 の確認: 4 つの SHA は commit。amaterrace-gpu の main（ae6f9c5）へ `--no-ff` で merge、衝突なし、merge の後に lint clean・`make sim-quick` 78 PASS 0 FAIL を確かめて 1e1a90c（親 ae6f9c5 d0a5794）。Q1 の判定: 受け入れを満たす（generic の netlist が同じなので回路は不変と判断、oxc-core の差は合成の道具の揺れ）→ cleared。
  - 残件: `sim/tb` の生の STATE の番号、`docs/ghci.md` に VF・RX・ROP・TU・vendor の範囲の名前が無い、CI の `make sim` の既定の timeout 300 s（atg_su 201 s・atg_top_tex は part E の後に約 10 分）。
