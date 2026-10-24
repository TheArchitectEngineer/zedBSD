<!-- awesome-plan project=zedbsd record=ws288-p026 -->

# ws288-p026 RTL の書き方の直しと短い sim

- 親: [WS288](../ws.md)。規則: amaterrace-gpu の `docs/style.md`、zedBSD の [Guardrail](../../guardrail.md) の「source の中の開発の管理情報」。
- 状態: in-progress（2026-10-24 22:50 Q1、q1554）
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
