<!-- awesome-plan project=zedbsd record=ws071p011 -->

# ws071-p011: App Home の項目、規約の照合、回帰（締め）

Phase ID: `ws071-p011`
Parent: [WS071](../ws.md)
Status: cleared（2026-09-27、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行）
依存: p002〜p010、p012〜p017

## 範囲

design §1 の決定: zdesktop の App Home に「Files」を足す。WS071 で変えた source の規約の照合。全部の回帰（ユーザーの指示
2026-09-27: 回帰の全体は締めの Phase だけで、boot test は行わない）。i915 実機は任意。

## 実装（2026-09-27）

- `userland/base/zdesktop/home.c`: 既定の一覧（`/etc/zdesktop/apps.conf` が無いとき）の最後に `Files`（`/bin/zdesktop-files`、
  検索語 files file manager folder finder browse、色 0x2f7cf6）。最後に足したのは、既存の 6 つの位置を i915 の capture の scenario
  （`plan/ws031/tests/i915-capture.py` の Terminal・Model viewer・Gears・X terminal の位置）で変えないため。
- `plan/ws035/tests/zdesktop-p069.sh`: `apps=6` → `apps=7`。
- 新 `plan/ws071/tests/files-p011.sh`: Home を開き（apps=7、Files の icon）、Files の icon で zdesktop-files が起き、窓が出て
  titlebar に control が並ぶ。
- 規約: `plan/tools/style-check.py` で zdesktop-files の全 file と zdesktop の全 file が 0（`ui-tabs.c` の段落 1 つ、zdesktop の
  `protocol.c`・`seat.c` の break の前の空行、`main.c` の flush の条件・perf の段落・条件演算子、`wire.c` の return の comment と
  `CMSG_LEN` の変数、`input.c` の `strncmp` の変数と段落、libwayland の `event.c` の段落を直した）。

## 検証（amd64、QEMU の Venus、2026-09-27）

回帰の全体は ws070-p012・ws035-p058 と同じ image・同じ guest の 1 回の run（`build/closeout/`）:

- 新 files-p011 PASS（Home に 7 つ、Files の icon で zdesktop-files の窓と titlebar の control）。
- files-regress（p002〜p008・p012〜p015・p009・p017・p010、14）PASS。
- WS070: titlebar-p008・p009・p010・p011、menu-p002、menu-p003 PASS。
- WS035 の zdesktop の試験（menu-regress）: p053・p059・p062〜p065・p068・p069・p071・p072・p014・p077〜p081・p057・p055 PASS。
  p070 と p076 は 1 回目に guest の ssh の接続が切れて FAIL（p076 の log に "Connection to 127.0.0.1 closed by remote host"、p070 は
  process の数の読みが空）。同じ image で流し直して両方 PASS（`build/closeout/menu-rerun/`、p070 の Gears と zterm の画面を見た）。
- build warning 0。boot test はユーザーの指示（2026-09-27）で行わない。i915 実機: 未実施。

## 結果

cleared。画面: `/home/awe/zedBSD-rpi4/build/ws071-shots/p011-20260927-venus-{home,files-from-home}.png`。
WS071 の Phase はすべて cleared。WS の完了（ws.md の完了の形への書き直し、Phase の directory の削除、使い続ける試験の
`plan/tools/` への移動）は、下の「残り」を補強の Phase にするかの判断と合わせて main の session に任せる。

## 残り（後の補強）

- App Home から開いた zdesktop-files の既定の大きさ（1120x720）が 1280x800 の画面で下にはみ出す（`files-p011` の files.png）。
  zdesktop の置き場の決め方か、client が出力の大きさ（`xdg_toplevel.configure_bounds`）で大きさを決める形が要る。
- ws070-p011 の TABS の残り（phase011 の「残り」）。
- 規約: 機械の検査は 0。全文の手の照合（段落の意味、comment の文）は p010・p011 で変えた file を読んだ範囲。
