<!-- awesome-plan project=zedbsd record=ws070p012 -->

# ws070-p012: titlebar の規約の照合と回帰（締め）

Phase ID: `ws070-p012`
Parent: [WS070](../ws.md)
Status: cleared（2026-09-27、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示で WS071 のサブエージェントが実行）
依存: p006、p011、WS071-p014

## 範囲

WS070 の titlebar（p007〜p011）の source の規約（`plan/coding-style.md`）の照合と、menu・files・zdesktop の回帰。boot test は
2026-09-27 のユーザーの指示で行わない。i915 実機は任意。

## 結果（2026-09-27）

- 規約: `plan/tools/style-check.py` で `zdesktop/titlebar.c`・`titlebar-shell.c`・`titlebar.h`・`icons.*`・`glass.c`・`menu*.c`・`shell.c`、
  `libzdesktop/titlebar.c`・`menu.c`、`libwayland/titlebar-protocol.c`・`menu-protocol.c`、`include/libc/zdesktop.h`、
  `tests/titlebar-probe/main.c` が 0（zdesktop の全 file が 0。ws071-p011 で残りを直した）。p006 で機械で見つかる形（条件・確保・入れ子・
  素通し）を直し、p011 の新しい code は書いた時に 0。
- 回帰（ws071-p011 と同じ image・guest の run、`build/closeout/`）: titlebar-p008・p009・p010・p011、menu-p002、menu-p003、
  files-regress 14、WS035 の zdesktop の 20 の試験 PASS（p070・p076 は 1 回目の ssh の切断の後の流し直しで PASS。ws071-p011 の記録）。
- 画面: `/home/awe/zedBSD-rpi4/build/ws070-shots/p012-20260927-venus-{p070-gears,tabs-overflow}.png`（他は p006・p011 の写し）。
- i915 実機: 未実施。

## 残り

- 全文の手の照合のうち、`menu-shell.c` の長い段落の中の comment（p006 の「残り」）は読んでいない。機械の検査は 0。
- TABS の補強（p011 の「残り」）。
- WS070 の Phase はすべて cleared。WS の完了の書き直しは、上の残りを補強の Phase にするかの判断と合わせて main の session に任せる。
