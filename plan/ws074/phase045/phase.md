<!-- awesome-plan project=zedbsd record=ws074p045 -->

# ws074-p045: 窓 2: titlebar の URL の欄、link、戻る・進む・再読み込み

Phase ID: `ws074-p045`
Parent: [WS074](../ws.md)
Status: in-progress（2026-09-27）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）

## 範囲

p014 から分けた部分（正常系のワンパス）:

- zdesktop の titlebar の CONTROLS（libzdesktop の `zdesktop_titlebar_*`、使うだけ）: 戻る・進む・再読み込み（GENERIC）・場所
  （BREADCRUMB: URL の path の部分を並べ、押すと URL の全体を編集する欄になり、Enter でその page へ）。design.md §17 の D3 の既定は
  TABS と窓の中の toolbar だが、それは p041（shell 2）で行う。ワンパスでは CONTROLS で URL の欄を持つ。
- link: 左 click の位置の fragment の要素から `<a href>` を探し、今の file からの相対の位置を解いて開く（`file:` だけ。`#` だけの
  link、`?` と `#` の後ろは今は無視）。
- 履歴（戻る・進む）、再読み込み、題名の更新。key: Alt+← / Alt+→、F5、Ctrl+L（場所の編集）。
- 試験の page `pages/second.html`（相対の link）。

## 受け入れ

1. amd64 の build（warning 0）、style-check 0。host: link の位置の解決と hit test の host の試験（plain・ASan）、golden。
2. guest（Venus、`--glass`）: link の click で second.html へ、titlebar の戻る・進む、場所の欄に path を打って Enter で blocks.html へ、
   題名が変わる。画面を撮る。
3. boot test。
