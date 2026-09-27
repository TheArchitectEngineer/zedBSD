<!-- awesome-plan project=zedbsd record=ws071p013 -->

# ws071-p013: タブ

Phase ID: `ws071-p013`
Parent: [WS071](../ws.md)
Status: planned（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main の Queue への反映は main の session）

## 範囲

2026-09-27 に元の p008 から分けた。[design.md](../design.md) §3（tab bar）、§10.1（File・Window のタブの項目）、spec §30:

- model: `fm_app` の `tabs[FM_TABS]`（8）に New Tab（今の場所を複製）、Close Tab（最後の 1 つなら窓を閉じる）、選択、前後。
  タブを替えるときは検索の走査を止め、Quick Look・Info・名前の編集を閉じ、folder と Home は読み直す（mtime の変化も拾う）。
- tab bar: タブが 2 つ以上のときだけ toolbar の下（高さ 34）に pill のタブ（場所の名前、今のタブは白、× で閉じる、click で選ぶ）。
  sidebar・content・preview はその分下がる。
- key と menu: Ctrl+T（New Tab）、Ctrl+W（Close Tab）、Ctrl+Tab・Ctrl+Shift+Tab（Next・Previous Tab）、Ctrl+PageDown・PageUp。
  File に New Tab・Close Tab、Window に Previous Tab・Next Tab（タブが 1 つなら無効）。folder の中 click（middle）で新しいタブに開く。

## 受け入れ

1. host の画面（タブ 2・3 つ、閉じた後）と action・key の試験。
2. Venus（QEMU）で Ctrl+T・click・Ctrl+Tab・Ctrl+W・menu の項目が動く。画面を撮る。
3. warning 0、`style-check.py` 0、回帰（p002〜p008、p012）PASS。
