<!-- awesome-plan project=zedbsd record=ws258 -->

# WS258 全ドライバの出典の表記を guardrail に合わせる（file の先頭から block へ）

- ID: ws258、Master: [master.md](../master.md)、Primary Milestone: MG001
- Status: completed（2026-10-18 Q1: p001 cleared。残りは WS244（i915/intel の抜粋・GT の core）と WS259（rtl8822b-tables.inc）へ移した）
- Phase の directory は完了の規則で削除した。file ごとの点検と出典の照合の記録は git の b80dea3b8 の plan/ws258/phase001/phase.md にある。
- 起点（2026-10-18 ユーザー）: gen9-ddc.c の先頭の comment「The AUX transfer follows the Linux kernel 6.8.12's …」について「これはルール違反です」「この修正も全ドライバに対してP1で監査してほしいです」。
- 規則: [Guardrail](../guardrail.md) の「MIT・GPL の出典のデータの引用」（2026-10-15 ユーザー）: file の先頭の comment に参考にした source・引用したデータ・派生の宣言・出典の一覧を一切書かず、該当箇所に block 単位で `Values referenced:`・`Procedure referenced:` で書く。code・comment・構造は写さない。
- Q1 の直し: gen9-ddc.c（1a9fb5ae3）。先頭の comment から Linux の関数名を外し、各関数の block に出典を移した。

## 完了の条件

1. `src/drivers/` と `include/drivers/`（ドライバの header）の全 file で、先頭の comment に出典・参考・派生の記述が無い。必要な出典は該当の block に guardrail の形で書かれている。
2. file ごとの点検の記録（直した file と直す要の無かった file）。
3. 変えた file を含む build（amd64 の既定・i915 の I915_TESTS=y の組・Pi 4）が warning 0。comment だけの変更なので動作は変わらない。

## Phase

| Phase | 目的 | 状態 |
| --- | --- | --- |
| p001 | 全ドライバの先頭の comment の監査と直し（P1） | cleared（2026-10-18） |
