<!-- awesome-plan project=zedbsd record=ws235-p008 -->

# ws235-p008 規約の全文の見直し

- 親: [WS235](../ws.md)。
- 状態: cleared（2026-10-29 11:1x P1、q1669-i04）。
- 依存: p002〜p007。

## 範囲

WS235 の C: `src/drivers/ethernet/rtl8125.c`・`include/drivers/ethernet/rtl8125.h` の全文、`src/drivers/pci/pci-pcat.c`・`pci.c`・`include/drivers/pci/pci.h`・Makefile の
WS235 の hunk だけ。plan/coding-style.md の全文と guardrail の「出典」「開発の管理情報」で、書き方だけを直す（動作は変えない）。

## 記録（8ff470285）

- conformance-reviewer（P1 の helper）が見直し、P1 が diff を読んで確かめた。
- rtl8125.c（約 40 か所）: 呼び出しの引数の中の呼び出し（log の引数の read32・profile の時間、read-modify-write の read8、sched_sleep の deadline）を変数に、
  条件演算子 3 か所を if に、呼び出しの結果を直に返す所（csi_read）を変数に、閉じ括弧の後の空行と段落の comment（約 14）、目的の comment の無い if・待ちの後の settle（約 12）、
  `set_phy_patch_mode` を 1 つの `phy_ocp_modify` に（同じ引数）、二重の空行。
- pci.c: MSI-X の disestablish の WS235 の hunk で条件の中の呼び出しを外し、comment を足した。pci-pcat.c: clamp の前に comment。rtl8125.h・pci.h・Makefile は直す所無し。
- 残した物: pci.c・pci-pcat.c の WS235 の hunk の外の既存の書き方（WS235 の行でない）。`receive_frames` の `if (empty)` の中の `packet != NULL` の確かめは
  empty の時 packet が必ず NULL で届かない（動作の問題でなく、見直しの範囲で消さない）。`reset_phy`・`advertise_all`・`configure_phy` の 1 つの comment の下の
  複数の read・check・write（各 check はすぐ後にある）。
- 出典は block ごと（file の先頭に無い）、source に WS・Phase・Queue・Bug の番号・日付・plan/ への参照は無い。

## 確かめ

- build: `make ZEDBSD_CONFIG=plan/ws235/tests/config-amd64-rtl8127-profile.mk BUILD=build/ws235/amd64-p build/ws235/amd64-p/vmunix`、
  `make ZEDBSD_CONFIG=plan/ws235/tests/config-amd64-rtl8127-perf.mk BUILD=build/ws235/amd64-n build/ws235/amd64-n/vmunix` とも exit 0・warning 0（11:06、編集の後）、
  amd64 vmunix check PASS、`git diff --check` clean、`plan/tools/style-check.py src/drivers/ethernet/rtl8125.c` の指摘 0（前は 25）。
- 未実施: 見直しの後の実機の非退行（書き方だけの変更。最後の実機の確かめは [p006](../phase006/phase.md) の final-1、ae8cb27df）。host 試験は無い（host で動く部分が無い）。
