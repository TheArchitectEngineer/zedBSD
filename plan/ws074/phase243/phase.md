<!-- awesome-plan project=zedbsd record=ws074-p243 -->

# ws074-p243: Grid trackのresolved CSSOMを実レイアウトから取得

Status: uncleared（q09-i02 primary再検証partial cleared、whole残りあり）
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

## q07 結果・後続設計

Event ws074-p243-q07-uncleared-20261018: [証拠](evidence/20261018-q07/README.md)、WIP a46bfd5ac。Grid245→247 whole PASS・433 callback改善だが、fixed minmax rows spanの1旧PASS callbackがFAIL。実solverの末尾固定行が30pxへ増える不備をgetterが露出した。q07 scopeは別solver欠陥を除外していたためattempt uncleared。

Event ws074-p243-p244-redesign-20261018: [p244](../phase244/phase.md)を追加し、固定行のみをまたぐitemが末尾trackを伸ばさない修正と検証を独立させる。そのverified output後、新しいp243 partial attemptで全502/全callbackと独立14条件を再検証する。宣言値を返すfallbackへ戻して既存不備を隠さない。q07のuncleared歴史とp243 wholeのchild/vertical等の条件は維持。共有投影pending。

Event ws074-p243-q08-revalidation-selected-20261018: p244 q08-i01出力の後、q08-i02がprimaryの全502/全callbackを再検証する。q07 attempt unclearedは保持。p243 wholeは現在unclearedのまま。

Event ws074-p243-q08-i02-start-20261018: p244 q08-i01の独立7条件/静的529確認を受け、final binaryで公式Grid502/全callbackと独立14条件を再検証する。q07の旧PASS1件を再度確認し、wholeのchild/vertical等は保持する。

Event ws074-p243-q08-i02-uncleared-20261018: [証拠](evidence/20261018-q08-i02/README.md)。whole247維持、callback9改善/10後退でattempt uncleared。p244 i01のinitial clearanceもinvalidate。

Event ws074-p243-minimum-rule-redesign-20261018: p244がparse/cascade minimum ruleを保持してguard eligibilityを修正する設計へ変更。verified output後、新しいp243 partial attemptが全502/全callbackと14条件を再検証する。q07/q08 historyとwhole条件は維持。

Event ws074-p243-q09-revalidation-selected-20261018: p244 q09-i01のmetadata/eligibility verified output後、q09-i02が全502とq07の6,410 PASS callbackを再確認する。q07/q08のuncleared historyは維持。

Event ws074-p243-q09-i02-start-20261018: p244 q09-i01のtargeted/static proofを受け、固定binaryで全502/全callbackをq07 baselineと比較する。q08で増えた8 callbackも維持できるか確認する。

Event ws074-q09-i02-partial-clear-20261018: [全502の証拠](../phase243/evidence/20261018-q09-i02/README.md)。whole247維持、callback6,410→6,419・後退0、q08の10後退回復。i02 primary再検証部分cleared。q07/q08 historyとp243/p244 whole残りを維持。共有投影pending。

Event ws074-p246-interface-redesign-20261018: [p246](../phase246/phase.md)がprivate track maximum ruleをparse/cascadeへ追加し、min-content/max-contentの区別とminmaxのcomputed fallbackを保持する。horizontal plain min-content single-span contributionとintrinsic非stretchを修正する部分scope。新outputは全Grid502/全callbackとstatic529/独立例で検証し、一般span/row/vertical/line-name等の既存whole残りを保持。q09/q10歴史は不変。共有投影pending。

Event ws074-p246-q11-auto-parser-20261018: [p246](../phase246/phase.md)のq11はgrid-auto intrinsic/minmaxの早期拒否を除き既存共通track-size parserへ統一する範囲に改訂。新metadataとcomputed fallbackをimplicit cycleにも伝え、全502/static529/独立例で検証する。repeat/line-nameとfit-content sizing等whole残りは維持。

Event ws074-p246-computed-floor-design-correction-20261018: repeat/verticalの既存consumerもclamped sizeを読むため、先のfloor移動案をsource生成中に見直した。既存cascade floorを維持し、private css_track.computed_maximumへunfloored upper lengthをcopyしてcomputed writerがexplicit length minimumのときだけ読む。pointer/公開ABIは不変、repeatやverticalの未選定算法は変更しない。先の移動設計はsuperseded、証拠/旧historyは残す。

Event ws074-p246-q11-uncleared-20261018: [証拠](../phase246/evidence/20261018-q11/README.md)。新scalar/computed outputでGrid whole248維持、74 callback改善/24後退。static227→228/後退0。animation欠落をserializerが露出したためq11 uncleared、p248 native設計へ。値の区別を捨ててfalse PASSへ戻さず、general/child/vertical/names等の既存whole残りは保持。

Event ws074-p250-computed-fit-design-20261018: [p250](../phase250/phase.md)がmaximum_rule FIT_CONTENTと既存sizeにcomputed fit limitを保持する。calcを実際に解析し、computed fallbackで関数をserialize。native autoのfit capは未実装のまま維持するようstretch判定はknown min/maxのみへ明示化。新pointer/公開ABIなし。Grid502/静的529/独立computedとnative不変を確認し、known animation lossを全記録する。既存whole条件/historyは不変。
