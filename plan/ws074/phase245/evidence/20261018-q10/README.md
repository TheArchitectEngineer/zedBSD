# q10 / no-box auto minimum cleared

2026-10-18、private bind_boxへ成功したlayoutのno-box proofを追加。primary/childのfirst box無しだけを根拠にauto min-width/min-heightのresolved valueを0pxへ。未知のgeometry・実box・明示minimumは従来経路。childの一時arena解放後に借用pointerを残さずscalarだけをcopy。通常node_boxのfound契約と公開browser ABIは不変。

仕様: [CSS Sizing 3 auto minimum](https://drafts.csswg.org/css-sizing-3/#automatic-minimum-size)のbox無しのresolved zeroを実装。通常CSS2 box一般・ratioの全minimum contextはこのPhaseの範囲外。guardrailに従い対象3sourceの管理commentも除去。

- 独立19条件は修正前11/19→19/19でChromium153と一致。none自身/祖先、contents自身/実item、Grid/Flex、visibility:hidden、明示px/%、ratio、動的変更、actual childを区別。
- font未指定の実際にlayout未準備な対照はbefore/after byte一致、autoを維持。
- 固定Grid64/Flex110、174 ID同一条件: whole107→109 PASS、callback989→993 PASS、旧whole/callback PASS後退0、ID/名前/重複順/checkpoint欠損0。対象Grid/Flex min-auto-sizeは各4/4 PASS。全rawとsummary同梱。
- DOM28/28、native CSSOM12/12・mutation12/12。host全source buildとtarget exact clang 2source compile warning0。C全文/manual reviewとformat19、style候補37→37で新規0、diff check通過。
- Acid3 score100/inventory100・画素差0。Acid2差873で不変。最初のsandbox内Acidはlocalhost socket EPERMのため測定せず、そのlogを保持して通常のrequire_escalated実行で測定完了。自動承認の拒否はない。

p245自身のno-box acceptanceはcleared、q10-i01 cleared。WS074全3,805 ID、production link/runtime、p100全source conformance等は未達。通常CSS2 box一般のauto minimumは未変更。共有Master/Queue/GitHub projection pending。push無し。
