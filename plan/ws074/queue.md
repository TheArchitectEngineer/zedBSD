<!-- awesome-plan project=zedbsd record=queue -->

# q1055 / WS074 XHTML 1.0 Strict nbsp subset

Status: finished（2026-10-14 02:48–02:51 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: 2026-10-14 00:07 JST、この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を、[p204のq1055有限部分scope](phase204/phase.md) に適用。
Timebox: 2026-10-14 02:48 JST 開始、最大90分（04:18 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1055-i01 | [ws074-p204](phase204/phase.md) whole | XHTML1 Strict公開DTD識別子に限定し`nbsp`をU+00A0へ展開、別DTD/未知実体拒否。固定47 ID・Flex10件、host/XML/build/Acid回帰。WPT/Master/公開ABI変更なし。最大90分 | cleared | p203 q1054残り47 `nbsp` error証拠 |

Dependency graph: p173固定manifest → p189 q1024 shard → p192 q1025一意id output → p193 q1026 details.open → q1027-i01 → p193 whole判断 → p173最終100%判断。p189 whole/p190/p192 wholeは別の必要出力、p191 whole cleared。

Upcoming Work Outlook: p191の残るtextdecoration CSSOM、p189 runnerの環境対応、[p190](phase190/phase.md) reftest/crashtest coverage、p173全件baselineを候補とする。次の Queue も有限scopeを記録する。

Outcome: q1055-i01 / p204 whole cleared。XHTML1 Strict公開IDの`nbsp`をU+00A0へ解決、旧47 load errorは0へ、47 pixel FAILとして描画に進んだ。XML host249/249・DOM54/54・document16/16・binding34/34、warning0 build、固定Flex10/10 PASS、Acid3差0/Acid2差1865。[証拠](phase204/evidence/20261014-q1055/README.md)。p203 wholeも後続証拠でcleared。p190/p173全件100%は未達。

Outcome: q1049-i01 partial cleared、p199 whole uncleared。物理left/rightとcolumn fallbackを実装し、固定10件は5 local PASS/5 FAIL/0 timeoutへ改善。fixture12/12・CSS rule44/44・CSSOM12/12・mutation12/12・warning0 build・Acid3差0/Acid2差1865。[証拠](phase199/evidence/20261014-q1049/README.md)。fractional/min-height/writing-mode残差、p173全3,805件100%は未達。

Outcome: q1050-i01 / p200 whole cleared。正の分配余白の最近接丸めとreverse残差配分で固定vert001a/002/006が全てpixel exact PASS、固定10件は8 local PASS/2 FAIL。host12/12・CSS rule44/44・CSSOM12/12・mutation12/12・warning0 build・Acid3差0/Acid2差1865。[証拠](phase200/evidence/20261014-q1050/README.md)。p173全3,805件100%は未達。

Outcome: q1051-i01 / p201 whole cleared。nowrap縦Flexのauto高さ・pixel min-heightで固定vert001bがpixel exact PASS、固定10件は9 local PASS/1 FAIL。局所2ケース・host12/12・CSS rule44/44・CSSOM12/12・warning0 build・Acid3差0/Acid2差1865。[証拠](phase201/evidence/20261014-q1051/README.md)。p173全3,805件100%は未達。

Outcome: q1052-i01 / p202 scoped whole cleared。vertical-lr Flexの物理軸・float幅・line-left/rightを実装、固定10件は10/10 local pixel exact PASS。host21/21・CSS rule44/44・CSSOM12/12・mutation12/12・warning0 build・Acid3差0/Acid2差1865。[証拠](phase202/evidence/20261014-q1052/README.md)。後続証拠でp197/p198/p199もwhole cleared。p173全3,805件100%は未達。

Outcome: q1053-i01 partial cleared、p190 whole uncleared。固定静的1,561 IDを同条件再測定し518 local PASS/684 FAIL/359 error/0 timeout。全359 errorはDOCTYPE付きXHTMLのXML parser ENOTSUP、旧PASS→FAIL8件を別記。[証拠](phase190/evidence/20261014-q1053/README.md)。p173全3,805件100%は未達。

Outcome: q1054-i01 partial cleared、p203 whole uncleared。DOCTYPE外部IDを副作用なしで受理し、359旧error中312件は読み込み成功、122 local PASS/190 pixel FAIL/47 load error。残り47件はXHTML1 DTD由来`&nbsp;`。[証拠](phase203/evidence/20261014-q1054/README.md)。host XML243/243・DOM54/54・document16/16・binding34/34・warning0 build・固定Flex10/10 PASS・Acid3差0/Acid2差1865。p173全3,805件100%は未達。

Coordination: [q1014](../history/queue-q1014.md) までの履歴を保持。共有 `plan/queue.md` / Master / GitHub publication は別の Q1 セッションとの競合を避け pending projection。WS074 lane の状態はこの file が所有。

Previous outcome: q1013-i01 partial cleared、p189 whole uncleared。選定testharness1件のWPT callbackから12 subtestとcompletionを取得し、4 PASS/8 FAIL、harness OK、runnerの結果はFAIL。status分類の unit5/5、py_compile、diff check PASS。[証拠](phase189/evidence/20261014-q1013/README.md)。残り1,782 testharnessと p173全件baseline、100%は未達。

Outcome: q1045-i01 uncleared、p195 whole uncleared。固定XHTML代表2件は直接file描画でも6秒timeout、referenceは即時描画。`--dump=dom`で最上位XHTMLがHTMLとして解釈されていることを確認。MIME/トップレベルXML接続は単一flex修正を超えるためコード変更なし。[証拠](phase195/evidence/20261014-q1045/README.md)。p173全件100%未達。

Outcome: q1046-i01 partial cleared、p196 whole uncleared。最上位XHTMLをXML解析へ接続し、固定10件timeoutを0へ改善、1 local PASS/9 FAIL。host MIME/DOM4/4・build warning0・Acid3差0/Acid2差1865。[証拠](phase196/evidence/20261014-q1046/README.md)。p195は後続証拠でwhole cleared。p173全件100%未達。

Outcome: q1047-i01 partial cleared、p197 whole uncleared。main-axis負余白でend/centerのoverflowを保持、固定横方向2 IDは双方local PASSへ、10件は3 PASS/7 FAIL。[証拠](phase197/evidence/20261014-q1047/README.md)。geometry5/5・host build warning0・Acid3差0/Acid2差1865。p173全件100%未達。

Outcome: q1048-i01 partial cleared、p198 whole uncleared。float BFC親/子margin非相殺を実装、geometry4/4、固定10件3 PASS/7 FAIL。vert001aとvert006差画素減、vert001bは別test側差で増。[証拠](phase198/evidence/20261014-q1048/README.md)。host build warning0・Acid3差0/Acid2差1865。p173全件100%未達。

Outcome: q1014-i01 partial cleared、p189 whole uncleared。19領域から条件を満たす15 IDを選定・実行、12 test FAIL・1 harness ERROR・1 completion無し・1 stdout無し、PASS0。4領域は未実行理由を保存。選定 unit2/2、py_compile、diff check PASS。[証拠](phase189/evidence/20261014-q1014/README.md)。q1013 と合わせユニーク16/1,783 testharnessを試行。ブラウザ/WPT/Master無変更、Acid3再計測省略。p173全件baseline・100%未達、共有Master/Queue/GitHub publication pending。

Outcome: q1015-i01 partial cleared、p189 whole uncleared。textdecorationの全16 IDを実測、4 PASS/12 FAIL、subtest47 PASS/132 FAIL。全16 completion取得。`CSS` global欠落とtext-decoration CSSOM不足を共通原因候補として記録。[証拠](phase189/evidence/20261014-q1015/README.md)。browser/WPT/Master無変更、p173全件baseline・100%未達。

Outcome: q1016-i01 partial cleared、p191 whole uncleared。`CSS.supports` を宣言parserへ接続。host fixture、build warning0、CSSOM12/12・rule model44/44、固定16件、Acid3差0を検証。textdecoration4 PASS/12 FAILと数不変だが `CSS is not defined` を解消。[証拠](phase191/evidence/20261014-q1016/README.md)。p173全件100%未達。

Outcome: q1017-i01 partial cleared、p191 whole uncleared。対象WPT18/18 subtest PASS、固定textdecoration全16件5 PASS/11 FAIL、subtest79 PASS/100 FAIL。host build・局所fixture・Acid3差0/Acid2差1865の回帰確認。[証拠](phase191/evidence/20261014-q1017/README.md)。全件100%未達。

Outcome: q1018-i01 partial cleared、p191 whole uncleared。対象WPT67/67 subtest PASS、固定textdecoration全16件6 PASS/10 FAIL、subtest128 PASS/51 FAIL。host build warning0・局所fixture・Acid3差0/Acid2差1865の回帰確認。[証拠](phase191/evidence/20261014-q1018/README.md)。全件100%未達。

Outcome: q1019-i01 partial cleared、p191 whole uncleared。対象4 WPT16/16 subtest・invalid既存2 WPT PASS、固定textdecoration全16件10 PASS/6 FAIL、subtest144 PASS/35 FAIL。host build warning0・局所fixture・Acid3差0/Acid2差1865の回帰確認。[証拠](phase191/evidence/20261014-q1019/README.md)。全件100%未達。

Outcome: q1020-i01 partial cleared、p191 whole uncleared。対象2 WPT6/6 subtest・invalid既存WPT PASS、固定textdecoration全16件12 PASS/4 FAIL、subtest150 PASS/29 FAIL。host build warning0・局所fixture・Acid3差0/Acid2差1865の回帰確認。[証拠](phase191/evidence/20261014-q1020/README.md)。全件100%未達。

Outcome: q1021-i01 partial cleared、p191 whole uncleared。invalid WPT3/3 subtest PASS、固定textdecoration全16件13 PASS/3 FAIL、subtest153 PASS/26 FAIL。host build warning0・局所fixture・Acid3差0/Acid2差1865の回帰確認。[証拠](phase191/evidence/20261014-q1021/README.md)。全件100%未達。

Outcome: q1022-i01 partial cleared、p191 whole uncleared。valid17/17・shorthand5/5 subtest、固定textdecoration全16件15 PASS/1 FAIL、subtest165 PASS/14 FAIL。host build warning0・CSSOM12/12・rule model44/44、Acid3差0/Acid2差1865の回帰確認。[証拠](phase191/evidence/20261014-q1022/README.md)。全件100%未達。

Outcome: q1023-i01 / p191 whole cleared。固定textdecoration全16/16 test、179/179 subtest PASS。host build warning0・CSSOM12/12・rule model44/44、Acid3差0/Acid2差1865の回帰確認。[証拠](phase191/evidence/20261014-q1023/README.md)。p173全3,805件100%は未達。

Outcome: q1024-i01 partial cleared、p189 whole uncleared。選定100件の実測は9 PASS/68 FAIL/3 harness error/7 no completion/13 no output。4領域の単一HTTP候補0を記録。[証拠](phase189/evidence/20261014-q1024/README.md)。p173全3,805件100%は未達。

Outcome: q1025-i01 partial cleared、p192 whole uncleared。一意id global lookupを実装し、局所fixture・native property65/65・realm30/30・Acid3差0/Acid2差1865の回帰確認。2 WPTの先頭ReferenceErrorを解消したが双方別の未実装でPASS未達。[証拠](phase192/evidence/20261014-q1025/README.md)。p173全件100%未達。

Outcome: q1026-i01 partial cleared、p193 whole uncleared。HTMLDetailsElement.open反映を追加、fixture・host回帰・Acid3差0/Acid2差1865を確認。対象WPTは次のhidden until-found差分でPASS未達。[証拠](phase193/evidence/20261014-q1026/README.md)。p173全件100%未達。

Outcome: q1027-i01 partial cleared、p193 whole uncleared。固定hidden IDL2 WPTが17/17・1/1 subtest PASS、beforematchはfragment navigation未完でno output。host build warning0・native property65/65・Acid3差0/Acid2差1865。[証拠](phase193/evidence/20261014-q1027/README.md)。p173全件100%未達。

Outcome: q1028-i01 partial cleared、p193 whole uncleared。同一文書hash・fragment target・beforematch revealをfixtureで確認。固定WPTはcompletion付きFAILへ進行し、次の `Element.attributes` 欠落を特定。host build warning0・native property65/65・realm30/30・Acid3差0/Acid2差1865。[証拠](phase193/evidence/20261014-q1028/README.md)。p173全件100%未達。

Outcome: q1029-i01 partial cleared、p194 whole uncleared。Element.attributesのlive read surfaceで固定beforematch WPT1/1 PASS。別WPTはreveal順序差でFAIL。host build warning0・native property65/65・realm30/30・Acid3差0/Acid2差1865。[証拠](phase194/evidence/20261014-q1029/README.md)。p173全件100%未達。

Outcome: q1030-i01 partial cleared、p193 whole uncleared。hidden/openのatom同一性を修正し固定中断2 WPT PASS。別WPTはtoggle待ちでno output。host build warning0・native property65/65・realm30/30・Acid3差0/Acid2差1865。[証拠](phase193/evidence/20261014-q1030/README.md)。p173全件100%未達。

Outcome: q1031-i01 partial cleared、p193 whole uncleared。非同期toggleをcoalesceし固定hidden/details WPT1/1 PASS、先行beforematch PASS維持。host build warning0・native property65/65・realm30/30・Acid3差0/Acid2差1865。[証拠](phase193/evidence/20261014-q1031/README.md)。p173全件100%未達。

Outcome: q1032-i01 partial cleared、p193 whole uncleared。固定details testharness全18件は6 PASS/10 FAIL/2 no_output。25 reftest/3 crashtestはp190 runner待ち。[証拠](phase193/evidence/20261014-q1032/README.md)。コード無変更、p173全件100%未達。

Outcome: q1033-i01 partial cleared、p190 whole uncleared。選定静的reftest10件は既存runnerで4 provisional PASS/6 FAIL、PASS1件はreference-chain無視のfalse pass。[証拠](phase190/evidence/20261014-q1033/README.md)。コード無変更、p173全件100%未達。

Outcome: q1034-i01 partial cleared、p190 whole uncleared。静的match-only reference chainの16辺上限と誤PASS防止を追加。局所graph4/4 PASS、固定10件は3 PASS/7 FAIL/0 errorへ訂正（先のfalse PASS1件がFAIL）。Python compile/diff check PASS。[証拠](phase190/evidence/20261014-q1034/README.md)。browser/WPT/Master無変更、p173全件100%未達。

Outcome: q1035-i01 partial cleared、p193 whole uncleared。UA CSSのfirst summary list-item既定値を追加し、固定details-display WPTは8/8 subtest PASS。host build warning0・CSS rule model44/44・CSSOM12/12、Acid3差0/480000・Acid2差1865/120000不変。[証拠](phase193/evidence/20261014-q1035/README.md)。p173全件100%未達。

Outcome: q1036-i01 partial cleared、p193 whole uncleared。ancestor reveal候補をevent前にsnapshot/GC root化。固定infinite-loop WPT1/1 PASS、先行4件PASS、element-removal002 no_output維持。host build warning0・native property65/65・realm30/30・pre-insertion29/29、Acid3差0/480000・Acid2差1865/120000不変。[証拠](phase193/evidence/20261014-q1036/README.md)。p173全件100%未達。

Outcome: q1037-i01 partial cleared、p190 whole uncleared。固定details rootの静的reftest全21件は12 local PASS/9 FAIL/0 error。focusの残り4 reftestは別directory未実行。FAIL分類はreference-chain2、details-content pseudo5、revert1、inline layout1。[証拠](phase190/evidence/20261014-q1037/README.md)。コード無変更、p173全件100%未達。

Outcome: q1038-i01 partial cleared、p190 whole uncleared。hidden directory固定reftest4件中静的2件は0 local PASS/2 FAIL、scripted2件は未実行。q1037と合わせdetails focus25件中23件実測、12 local PASS/11 FAIL/2未実行。[証拠](phase190/evidence/20261014-q1038/README.md)。コード無変更、p173全件100%未達。

Outcome: q1039-i01 partial cleared、p190 whole uncleared。固定details crashtest3件のlocal renderer測定は3 PASS/0 crash/0 timeout/0 no-image。分類control5/5、Python compile/diff check PASS。[証拠](phase190/evidence/20261014-q1039/README.md)。残り45 crashtest未測定、browser/WPT/Master無変更、p173全件100%未達。

Outcome: q1040-i01 partial cleared、p190 whole uncleared。固定crashtest全48 IDは48 local PASS/0 crash/0 timeout/0 error/0 unsupported。選択/分類control6/6、Python compile/diff check PASS。[証拠](phase190/evidence/20261014-q1040/README.md)。公式環境同等性/reftest coverageは残り、browser/WPT/Master無変更、p173全件100%未達。

Outcome: q1041-i01 partial cleared、p190 whole uncleared。anchor-positioning固定reftest77 ID中静的21件は0 local PASS/21 FAIL、scripted56件未実行。ID別照合と代表差分、diff check PASS。[証拠](phase190/evidence/20261014-q1041/README.md)。browser/WPT/Master無変更、p173全件100%未達。

Outcome: q1042-i01 partial cleared、p190 whole uncleared。固定reftest1,974 IDを静的適格1,561/script402/scripted reference5/variant6に分類。局所selection3/3+chain4/4、既存details/anchor42件の結果/画素差一致。広域100件は28 local PASS/72 FAIL。[証拠](phase190/evidence/20261014-q1042/README.md)。browser/WPT/Master無変更、p173全件100%未達。

Outcome: q1043-i01 uncleared、p190 whole uncleared。静的全1,561 ID中先頭380まで105 local PASS/273 FAIL/2 error。遅いXHTMLでcase別60秒、runnerは最終reportのみのため中断し、ID/statusを[部分証拠](phase190/evidence/20261014-q1043/README.md)へ保存。残り1,181未測定。次はcase別上限と逐次checkpoint。browser/WPT/Master無変更、p173全件100%未達。

Outcome: q1044-i01 partial cleared、p190 whole uncleared。case別5秒上限・逐次fsync JSONLで固定静的1,561 IDを全測定し500 local PASS/1,051 FAIL/10 timeout/0他error。未対応413 IDは保持。100件sample結果/画素差一致、checkpoint1,561行一致、局所control9/9。[証拠](phase190/evidence/20261014-q1044/README.md)。browser/WPT/Master無変更、p173全件100%未達。
