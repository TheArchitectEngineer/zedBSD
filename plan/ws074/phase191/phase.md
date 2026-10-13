<!-- awesome-plan project=zedbsd record=ws074-p191 -->

# ws074-p191: Interop 2025 textdecoration CSSOM 互換

Status: uncleared（2026-10-14 q1016 partial cleared、whole 16件PASS未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p173固定manifest](../phase173/evidence/20261013-q1012/manifest.json)、[p189 q1015 textdecoration全16件baseline](../phase189/evidence/20261014-q1015/README.md)。p189 wholeは不要。

## 目的と受け入れ

固定Interop 2025 textdecoration領域の全16 testharness IDをPASSさせる。`CSS.supports`、`CSSStyleDeclaration` の longhand/shorthand、computed style と値構文を一般的なCSSOM実装として修正する。WPT本体/期待を変えず、Acid3 score100/100・pixel差0の既存条件を守る。全16件の再測定、host/browser試験、build、source規約確認を証拠化した時に whole clearanceを判定する。p173全3,805件100%とは区別する。

## q1016-i01 の有限部分範囲

最大90分。現行browserに `CSS` globalがないため computed系の `CSS.supports(property,value)` が `ReferenceError` になる共通欠陥を修正する。既存のCSS宣言parserを使い、指定値を実際に受け入れる場合だけ `true` を返す。単一引数の基本的な宣言条件は可能な範囲で同じparserに接続し、未対応の複合条件を安易に`true`にしない。新しいCSS propertyやstyle serializationはこのQueueに含めない。小さなhost再現、build、固定16件、Acid3回帰を確認し、差が増えたら修正を戻す。`master.md` は変更しない。

適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)。公開ABI、Wayland境界、WPT本体は変更しない。source範囲は `userland/desktop/libbrowser/bind/` と必要な既存private宣言、host試験。q1016以後の値文法/CSSOM修正は別の有限Queueにする。

Event ws074-p191-design-20261014: q1015で全16件のbaselineを得て追加。p189のrunner coverageではbrowser実装差を扱わないため、p191でCSSOMを担当する。p173の分母・全件目標、p189/p190のrunner責務は維持。GitHub publication保留。

## q1016-i01 結果（2026-10-14 00:14 JST）

**partial scope cleared、p191 whole uncleared。** `CSS` globalと `supports(property,value)` を既存CSS宣言parserに接続し、基本的な単一引数宣言も判定する。小さなhost fixture、build warning0、既存CSSOM12/12・rule model44/44、固定textdecoration16件、Acid3回帰を確認。[証拠](evidence/20261014-q1016/README.md)。全16件では4 PASS/12 FAIL、subtest47 PASS/132 FAILで q1015 と件数不変。ただしcomputed系の `CSS is not defined` は解消し、computed値が空という不足が明確化。Acid3 score100/100・pixel差0、Acid2差1865不変。複合supports条件と他のCSSOM機能は未対応、次の有限Queueで扱う。GitHub publication保留。

Event ws074-p191-q1016-outcome-20261014: browserの一般的なCSS API入口を追加し、WPTのcomputed系が値判定まで進む。q1016-i01 partial cleared、p191全16 PASSは未達。p189/p190/p173への依存は維持。`master.md` 無変更。

## q1017-i01 の有限部分範囲

2026-10-14 00:16 JST、ユーザーの事前承認を適用、最大90分。公式 `text-decoration-line-computed.html` の18 subtestを対象に、`text-decoration-line` の `none`、各line keywordと重複なしの組合せ、spelling/grammar error のCSS値解析とcomputed style直列化を一般的に修正する。描画の既存underlineは維持し、他のline描画はこのQueueの基準に含めない。WPT本体/期待や公開ABI/`master.md` は変更しない。局所fixture、build、当該WPTとtextdecoration全16件、Acid3/Acid2回帰を測定し、差画素非増加を条件にする。全16件PASSは後続範囲。

## q1017-i01 結果（2026-10-14 00:19 JST）

**partial scope cleared、p191 whole uncleared。** CSS値解析にline bit集合を追加し、computed styleで非継承とcanonical順を反映。既存underline描画を保持。[証拠](evidence/20261014-q1017/README.md)。対象WPT18/18 subtest PASS、固定textdecoration全16件は5 PASS/11 FAIL（q1016の4/12から1件改善）、subtest79 PASS/100 FAIL（47/132から32改善）。局所fixture、host build warning0、CSSOM12/12、rule model44/44。Acid3 score100/100・画素差0、Acid2差1865不変。inline CSSOM serializationなどの失敗は残る。GitHub publication保留。

Event ws074-p191-q1017-outcome-20261014: q1017-i01 partial cleared、p191 wholeは11 test FAILのためuncleared。次は既存CSSStyleDeclarationのtext-decoration-line複合値のcanonical化などを有限Queueで調べる。p173分母・他Phase依存は維持。

## q1018-i01 の有限部分範囲

2026-10-14 00:20 JST、ユーザーの事前承認を適用、最大90分。公式 `text-decoration-line-valid.html` の67 subtest中、逆順の複合line値49件がCSSStyleDeclarationから元の指定順で返り失敗する。既存CSS parserの解析結果を使って、`text-decoration-line` のinline CSSOM格納値を規定順へ直列化する。CSS-wide keyword・他のpropertyと不正値の動作を保ち、WPT本体/期待・公開ABI・`master.md`は変えない。局所fixture、build、対象WPT、固定領域全16件、Acid3/Acid2差画素非増加を確認する。p191全16件PASSはこのQueueの基準に含めない。

## q1018-i01 結果（2026-10-14 00:25 JST）

**partial scope cleared、p191 whole uncleared。** 共通CSS parserから `text-decoration-line` のbit集合を取り、inline CSSOMとcomputed styleのcanonical直列化を共有。[証拠](evidence/20261014-q1018/README.md)。対象WPT67/67 subtest PASS、固定textdecoration全16件6 PASS/10 FAIL、subtest128 PASS/51 FAIL。局所fixtureの直接property/setProperty/cssText、CSS-wide・他property・不正値、host build warning0、CSSOM12/12、rule model44/44を確認。Acid3 score100/100・画素差0、Acid2差1865不変。色/style/skip-ink/shorthandなどが残る。GitHub publication保留。

Event ws074-p191-q1018-outcome-20261014: q1018-i01 partial cleared、p191 wholeは10 test FAILのためuncleared。固定3,805分母と他Phase依存は維持。次の値parser/CSSOM不足は別の有限Queueにする。

## q1019-i01 の有限部分範囲

2026-10-14 00:26 JST、ユーザーの事前承認を適用、最大90分。固定textdecoration領域の `text-decoration-style`（valid/computed各5 subtest）と `text-decoration-skip-ink`（valid/computed各3 subtest）を、仕様の値・初期値・継承・computed CSSOMとして実装する。styleは非継承、skip-inkは継承。描画へのwavy/double/ink skipping反映はこの部分scope外であり、全機能対応を主張しない。invalid既存試験、局所fixture、build、4対象WPTと領域全16件、Acid3/Acid2差画素非増加を確認。WPT本体/期待・公開ABI・`master.md`は変更しない。

## q1019-i01 結果（2026-10-14 00:28 JST）

**partial scope cleared、p191 whole uncleared。** style/skip-inkの値parser、初期値、継承、computed CSSOMを追加。[証拠](evidence/20261014-q1019/README.md)。対象4 WPTは16/16 subtest PASS、invalid既存2 WPT PASS。固定textdecoration全16件は10 PASS/6 FAIL、subtest144 PASS/35 FAIL（q1018の6/10、128/51から改善）。局所fixture、host build warning0、CSSOM12/12、rule model44/44。Acid3 score100/100・画素差0、Acid2差1865不変。描画反映と残る色/shorthand等は未達。GitHub publication保留。

Event ws074-p191-q1019-outcome-20261014: q1019-i01 partial cleared。p191 wholeは6 test FAILのためuncleared。p173分母・p189/p190 runner依存は維持。残る互換は別の有限Queueへ。
