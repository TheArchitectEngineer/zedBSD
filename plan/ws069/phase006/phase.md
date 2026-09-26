<!-- awesome-plan project=zedbsd record=ws069p006 -->

# ws069-p006: WS069 の規約の全文との照合と回帰（最後）

Phase ID: `ws069-p006`
Parent: [WS069](../ws.md)
Status: in-progress（q492-i01）
Phase disposition: normal
Queue: q492-i01
承認: 2026-09-27 ユーザーの自走の指示（「私が止めるまで自走を続けてほしい」）と WS069 の計画の最後の Phase

## 範囲

- WS069 で足した・変えた C の全文を [coding-style.md](../../coding-style.md) の全文（§2〜§14 の checklist）と照合する:
  `userland/base/zdesktop-x11server/`（p008〜p011、全 file）、`userland/X11/libX11/xlib.c` の XPending と `wr()`（p010）、
  `userland/X11/libGL/glx.c` の swap の段、`userland/X11/zgears/main.c` の FRAME・SUM・watchdog（p010）、
  `src/kern/net/unix-socket.c`・`src/kern/net/socket.c` の待ち（p010、legacy の件数を増やさない）。
  Xzed は p009 で ws069 の前へ戻したので対象外。
- style-check（自動）に加え、自動で見られない項目（§12 の環境変数、直接の return、成功の return が最後、三項演算子、file-scope の変数の
  comment、名前）を目で見る。見つけた違反はこの Phase で直す。
- 回帰: build warning 0、Venus の x11-p003〜p005・zdesktop-p070、i915 実機の `zdesktop-x11` の run、boot test。

## 受け入れ

1. 範囲の新しい file は style-check の指摘 0、変えた legacy file は件数が増えない。目の照合で見つけた違反を直した（または理由を記録）。
2. build warning 0、Venus の 4 試験 PASS、実機の run で 6 検査 PASS（BUG-056 の run は数えない）、boot test PASS。
3. WS069 の受け入れを確かめて ws.md を完了の形にする（受け入れ 1 の rootful は 2026-09-27 ユーザーの判断「rootlessのみでOK」で外した）。
