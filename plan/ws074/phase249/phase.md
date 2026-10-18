<!-- awesome-plan project=zedbsd record=ws074-p249 -->

# ws074-p249: zero-height lineとempty-through margin eligibility

Status: planned
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: 未選定（q12/q13後の候補）

## Observation・scope

p247の補助font-size-zero-1でglyph ink修正後も422pixel差。testの後続p y51.20、reference y67.20、zero pにはheight0の生成lineが1つある。block_childrenはheight/border/paddingが0ならemptyとし、生成lineやindependent formatting contextを見ていない。p198の親子BFC margin clearanceとは別のempty-through条件で、p198/p185のacceptanceは維持する。

[CSS2.2 §8.3.1](https://www.w3.org/TR/CSS22/box.html#collapsing-margins)と[§9.4.2](https://www.w3.org/TR/CSS22/visuren.html#inline-formatting)を適用。高さ0でも内容を持つlineはmarginを分けるが、collapsible whitespaceだけ等の無視すべきempty lineは区別する。単にline_count!=0を追加する前に、実際のinline builderが生成/除外する条件を最小例で確認する。

## First finite design candidate / criteria

普通のhorizontal normal-flow blockで、内容を持つzero lineと真にemptyなbox、in-flow子経由のline、float/absoluteの非参加、BFCのown margin eligibilityを既存height/frame判定と合わせる。percentage heightのdefiniteness等の未確認一般条件を一度に再設計しない。公開ABI/Wayland境界/WPT/期待値の変更無し。

直接/入れ子のzero font・zero line-height、collapsed/preserved whitespace、空inline/atomic、BFC/normal、positive/negative margin、frame/clearance対照をChromium比較する。補助zero-1の回復とp185/p198の既存証拠非悪化、固定Interopへの影響scopeを選定してold PASSを照合、host/target warning0・C full/format・Acid非増加。新Queueに正確な有限scopeと検証を保存するまでsource生成しない。full WS/targetはp100/後段。

Event ws074-p249-planned-from-q12-20261018: p247 inkを直すためにmargin結果を偽装せず、後続候補へ分離。q12 current scope/criteriaは不変、共有投影pending。

Event ws074-p247-q12-output-20261018: [p247証拠](../phase247/evidence/20261018-q12/README.md)でzero glyph native/DOM/paintと全eligible非悪化をverified。p249はこのoutput上でmargin差を独立に修正する候補。p247自身のclearanceは本Phaseへの転記だけに基づかずglyph/ink基準で検証済み。
