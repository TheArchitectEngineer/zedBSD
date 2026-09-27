<!-- awesome-plan project=zedbsd record=ws035p058 -->

# ws035-p058: zdesktop の規約の照合と回帰（sq001 の締め）

Phase ID: `ws035-p058`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-27、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。元は sq001 の planned）

## 範囲

zdesktop（secondary queue とその後の Phase で変えた全 source、`userland/base/zdesktop/`）の規約（`plan/coding-style.md`）の照合と回帰。
boot test は 2026-09-27 のユーザーの指示で行わない。

## 結果（2026-09-27）

- 規約: `plan/tools/style-check.py userland/base/zdesktop/*.c *.h` が 0。締めで直したもの（ws071-p011 と同じ commit）: `main.c`（flush の
  条件の中の呼び出しを変数に、`zwl_cycles`・`zwl_perf_report` の段落、条件演算子を if に）、`wire.c`（return の前の comment、`CMSG_LEN` を
  変数に）、`seat.c`・`protocol.c`（break の前の空行）、`input.c`（`strncmp` を変数に、段落の comment）。
- 回帰（`build/closeout/`、ws071-p011・ws070-p012 と同じ image・guest）: zdesktop の試験 p053・p059・p062〜p065・p068〜p072・p014・
  p076〜p081・p057・p055 PASS（p070・p076 は 1 回目に guest の ssh の接続が切れ、流し直しで PASS）。menu・titlebar・files の回帰も PASS。
- 画面: `/home/awe/zedBSD-rpi4/build/ws035-shots/p058-20260927-venus-p057-{over,gone}.png`、p070 の Gears と zterm は
  `/home/awe/zedBSD-rpi4/build/ws070-shots/p012-20260927-venus-p070-gears.png`。
- i915 実機: 未実施。

## 残り

- 機械の検査が見ない規則（段落の意味、comment の文の質）の全文の手の照合は、この締めで直した file の範囲だけ。
