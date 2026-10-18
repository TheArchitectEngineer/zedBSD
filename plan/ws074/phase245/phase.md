<!-- awesome-plan project=zedbsd record=ws074-p245 -->

# ws074-p245: boxを生成しない要素のauto minimum resolved value

Status: cleared（q10 no-box own acceptance、WS最終target/p100は残る）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: ws074-q20261018-10-i01

## Objective・scope・criteria

公式grid-item-min-auto-size-001の4 callback中、display:none自身とdisplay:none祖先の下の2件が、min-width/min-heightのautoを返してFAIL。CSS Sizing 3 §3.2（https://drafts.csswg.org/css-sizing-3/#automatic-minimum-size）はboxが生成されない場合のauto minimumのresolved valueを0とする。actual boxを持つgrid/flex itemはautoを維持する。

最初の最大90分は**確実にboxがないと分かった要素**だけを扱う。通常CSS2 boxの一般auto minimumやpreferred ratioのすべての文脈を一度に変更しない。geometryが不明/利用不可というだけでbox無しと推定しない。

## Design

private bind_boxにknown no-boxのmetadataを追加する案。page_node_boxのprimary layoutが成功しfirst boxがNULLのときだけセットし、childの一時treeもlayout成功後に同じproofをcopyできる。font/viewport未準備・retired child等はunknownのまま。既存のreturn found=0と追加のno-box proofを区別する。arena pointerは返さない。

computed_write_usedはauto min-width/min-heightかつno-box proofがある場合だけ0pxをserializeする。実際のgrid/flex item・display:contentsを挟んで生成された子・明示min値・unknown geometryは既存経路へ渡す。公開browser ABI、layout track計算、minimumのused geometryは変更しない。

## Verification・dependencies

p243/q08で保存した公式Grid rawとgeometryの既存copy契約を前提とする。独立例はnone自身/none祖先/contents自身/contents下の実grid・flex item/明示minimum/ratioとbox無し/unknown hostを区別し、Chromiumで比較する。公式対象1 IDの4/4、影響するGrid/Flex minimum/computed parsingの固定subsetを前後比較、旧whole/callback PASS後退0。DOM/CSSOM host、host/target compile warning0、新コードC全文/format、Acid3差0/Acid2非増加。

own completionはこの確実なno-box resolved valueと影響範囲を検証したこと。WS最終の全source/target関門はp100に保持。Guardrail/C全文/browser-component全文を適用。未知のbox状態の推測や期待値の偽装でclearしない。

Event ws074-p245-planning-20261018: q08のreadonly調査から追加。node_boxのfound=0はlayout未準備も含むため、それだけを根拠に0pxへ変える案は採用せず、成功したlayoutのno-box proofをcopyする。q08の実装scope/acceptanceは変更しない。Master/共有Queue/GitHub projection pending。

Event ws074-p245-q10-start-20261018: ユーザー自走指示で上記no-box scope最大90分を選定。固定minimum/computed/parsing subsetはq03適格のGrid64/Flex110、計174 ID。q09の新Grid rawとq03 Flex rawに加えて同じq09 pairでbefore再測定し、新コードで全ID/全callback比較する。unknownはCLIのfont未指定で実際にlayout未準備を作り、computed autoが維持されることを確認。childは一時treeが解放された後もscalar proofだけを読む。

Event ws074-p245-q10-cleared-20261018: [証拠](evidence/20261018-q10/README.md)。独立19/19 Chromium一致・unknown維持、固定174 whole107→109/callback989→993・後退0、対象Grid/Flex各4/4、DOM28/28、host/target warning0、Acid3差0/Acid2差873。own no-box acceptanceを満たしPhase/q10-i01 cleared。GitHub publish/closeはpending、共有投影pending。一般CSS2/ratio contextとWS最終target/p100を達成したとは扱わない。
