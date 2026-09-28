<!-- awesome-plan project=zedbsd record=ws074p045 -->

# ws074-p045: 窓 2: titlebar の URL の欄、link、戻る・進む・再読み込み

Phase ID: `ws074-p045`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）

## 範囲

p014 から分けた部分（正常系のワンパス）:

- zdesktop の titlebar の CONTROLS（libkeiland の `keiland_titlebar_*`、使うだけ）: 戻る・進む・再読み込み（GENERIC）・場所
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

## 結果（2026-09-27）

cleared。

- 書いたもの:
  - `layout/hit.c`: `layout_hit`（点の下の text の fragment の box。normal flow では重ならないので最初に見つかったもの）。
  - `page/link.c`: `page_link_at`（hit の box から祖先の `<a href>`）、`page_resolve_file`（今の file の絶対 path から: 絶対 path、
    `file://`（host は捨てる）、相対 path、`.`・`..` の正規化（root より上へは行かない）、`?` と `#` から後ろを捨てる、`%XX` の
    復号。他の scheme は EPROTONOSUPPORT）。WHATWG の URL の parser は p015。
  - `shell/titlebar.c`: zdesktop の titlebar の CONTROLS（戻る・進む・再読み込み（GENERIC）・場所（BREADCRUMB: path の部分、編集の
    text は `file://` の URL））。libkeiland の `keiland_titlebar_*` を使うだけ（変えていない）。compositor に titlebar が無ければ
    zdesktop の既定の titlebar と key だけで動く。
  - `shell/shell.c`: 履歴（64 まで、新しい一歩で先の歩みを捨てる）、左 click（押した所から 4 px 以内で離す）で link を開く、
    titlebar の戻る・進む・再読み込み・場所（押すと編集、Enter で開く）、key（Alt+← / Alt+→、F5、Ctrl+L）、題名の更新。開けない
    page は今の page を残して `ZBROWSER ERROR load …` を書く。test 用の行 `ZBROWSER LINK`・`NAVIGATE`・`TITLEBAR`・`ERROR`。
  - `shell/window.c`: pointer の位置と button の event。main の xdg-shell v4（configure_bounds）の NULL の slot は desktop の
    サブエージェントが入れた（merge 済み）。
  - build: browser の link に libkeiland（`platform/amd64/vmunix.mk`、package の REQUIRE）。
  - 試験の page `pages/second.html`（相対の link 2 つ、`./blocks.html#top`）と golden `second.{dom,style,layout,paint}`
    （Chrome と box 5/5）。
- 試験:
  - host: `host-link`（解決 12 件、first.html の「link」の上の link が `second.html`、「note」と余白は link でない、題名）22/22
    （plain・ASan）。golden 12/12（plain・ASan）。
  - guest（Venus、`--glass`、`plan/ws074/tests/browser-p045.sh`）: titlebar に 4 つの control。「link to the second page」の click で
    second.html（題名も変わる、back=1）。titlebar の戻るで first.html（forward=1）、進むで second.html。Ctrl+L・Ctrl+A・path・Enter で
    blocks.html。F5 で同じ page をもう一度。Alt+← で second.html。zdesktop と browser の log に ERROR 無し。status 0（QEMU の証拠）。
  - amd64 の build: warning 0。boot test: PASS。実機: 未実施。
- 画像（`/home/awe/zedBSD-rpi4/build/ws074-shots/`）: `p045-20260927-titlebar.png`（control のある titlebar）、`p045-20260927-link.png`
  （link の後の second.html と breadcrumb）、`p045-20260927-location.png`（場所の欄の編集）、`p045-20260927-blocks.png`、
  `p045-20260927-boot-login.png`。
- commit: `70703e33`（hit test・link・pointer）、`b7d56fe7`（titlebar・履歴・link）、`8c772cf6`（guest の試験）と、この記録の commit。

## 後回し（follow-up）

- link の上の cursor の形（手）と hover、押している間の link の色、中 click・新しい窓。
- `#fragment` の位置への scroll と同じ文書の中の link、`?` の query（http の後）。URL の parser は p015。
- 履歴の scroll の位置の復元、再読み込みの cache。
- xdg_wm_base v4 の configure_bounds で最初の窓の大きさを決める（files と同じ）。
- TABS の titlebar と窓の中の toolbar（D3 の既定、p041）。
