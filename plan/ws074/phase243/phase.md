<!-- awesome-plan project=zedbsd record=ws074-p243 -->

# ws074-p243: Grid trackのresolved CSSOMを実レイアウトから取得

Status: in-progress
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: ws074-q20261018-07-i01 primary horizontal partial

## 根拠・目的

q03の公式testharnessで、grid-support-flexible-lengths、grid-inline-support-flexible-lengths、grid-layout-properties、grid-flex-track-intrinsic-sizes、computed-withcontent/implicit-track等の初回失敗が、getComputedStyleのfr/auto/noneの返却にある。現在bind/computed.c::computed_write_tracksは「used sizes are not kept」と明記し、style値のみを返す。layout/grid.cの実際のcolumns/rowsは一時grid_stateにあり、grid_releaseで失われる。

CSS Grid 2 §7.2.6（https://www.w3.org/TR/css-grid-2/#resolved-track-list）を確認。grid containerのresolved track listは実際の明示/暗黙trackを個別px値で返し、non-grid/display:none等はcomputed valueへ戻す。テストの期待値を生成するのではなく、実solverの値を公開する。

## 最初の有限部分の案

最大90分。primary Documentのhorizontal grid/inline-grid、既存solverで解いた名前なしのtrackを対象にする。固定/割合/fr/auto、暗黙track、repeat、auto-fit collapse、空gridとstyle変更を独立例で確認する。vertical/child Document、line-name/subgridの未実装文法、未解決のtrack sizing algorithmは明示して残し、当初のwhole基準を狭めない。

- layout/layout.hにarena所有のdouble used-track snapshotとvalid/countをprivate追加。layout/grid.cのfinal horizontal solver出力を、state解放前に保存する。intrinsic measurementをfinal値としてpublishしない。collapsed trackは0、空gridの内部placeholderを実trackと数えない。
- bind_hostにprivateのgrid track queryを追加し、caller-owned wb_vectorへpx値をcopyする。page側はprimary layoutをflushしてsnapshotをcopy、allocation失敗をstatusで返す。通常node_boxのcopy契約やpublic browser ABIは変えない。
- child Documentのgeometry_treeはquery内の一時arenaを即releaseする。そこからborrowed pointerを返さない。最初はqueryのfound=0で明示したfallbackとし、child対応は次の有限部分でowned copyを設計する。
- computed側はtemplate axisのみqueryし、foundなら全used値/noneをserialiseする。auto-columns/rowsやnon-gridのcomputed serializationは維持する。新しいsnapshotは次のlayout generationまでarenaが所有するが、呼出し元へは値をcopyする。

## Verification・依存・停止条件

p221の既存parse/auto track出力とq03の全ID/rawを前提とする。q06で今回のratio変更由来の後退が判明すれば先に対処する。独立geometry+CSSOMをChromium比較、固定Gridの該当testharnessを前後比較しwhole/subtestを分ける。旧PASS代表/全Grid502は必要な回帰境界として選定時に定める。host/target compile warning0、C全文manual review、変更範囲format、Acid3差0/Acid2非増加。full WS source最終conformanceはp100の関門を維持。Guardrail、C全文、browser-component全文適用。

snapshot lifetimeやsolverの実際の値を検証できなければfallbackを捏造せずattempt unclearedとして残す。現時点は計画のみ。Master/共有Queue/GitHubのprojectionはpending。

Event ws074-p243-planning-20261018: q06静的census中のread-only調査から追加。bind_boxへarena pointerを載せる案は、child queryが一時treeを解放するため採用せず、caller-owned vectorへのcopy queryを計画した。p221の既存scope/acceptanceは変更しない。

## q07 実行開始

Event ws074-p243-q07-start-20261018: q06を2e3680e6bへ保存。全FAIL725の開始前比較で今回sessionの後退0を確認し、上記primary horizontalの部分範囲を最大90分で実行する。検証に再使用するreftest runnerはQ1 cleanup方針へ合わせ、TemporaryDirectoryをretainへ変更する（検証条件や画像判定は不変）。
