<!-- awesome-plan project=zedbsd record=ws074-p203 -->

# ws074-p203: XML external-ID DOCTYPE without DTD processing

Status: cleared（2026-10-14 q1054-i01 partial、q1055後続証拠で旧359件のload error解消）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p190 q1053](../phase190/evidence/20261014-q1053/README.md)の359 DOCTYPE error IDと[p196 q1046](../phase196/evidence/20261014-q1046/README.md)の厳密XHTML解析。

## 目的と受け入れ

XML 1.0 non-validating processorとして、内部subsetの無いDOCTYPEのroot名と任意のSYSTEM/PUBLIC外部識別子だけを構文検証して受け入れ、外部DTDの取得・展開は行わない。未知の実体と内部subset、壊れた宣言は従来どおり拒否する。固定q1053の359 error IDでDOCTYPE読込errorが解消し、XML host/Acid/固定Flex回帰を満たすことをwhole clearance条件とする。外部DTDで宣言された実体や属性既定値の完全実装、script付きXHTML、p190/p173全件100%は別。

適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)、[XML 1.0 §2.8/§5.1](https://www.w3.org/TR/xml/#sec-prolog-dtd)。libbrowser内部のみ変更し、外部ネットワーク読込、WPT、公開ABI、`plan/master.md`を変更しない。

## q1054-i01 の有限部分範囲

2026-10-14 02:43 JST、このchatのユーザーによるInterop 2025全件100%までの有限Queue事前承認を適用。最大90分（04:13 JSTまで）。`xml/parser.c`のDOCTYPE分岐を、document root前・1回だけの`<!DOCTYPE Name (SYSTEM "..." | PUBLIC "..." "...")? >`（内部subset無し）に限定して解析する。外部識別子をURLとして取得せず、既存未知実体拒否を維持する。hostで合法・壊れた・内部subset・未定義実体を検証し、q1053のerror359 ID・固定Flex10件を同条件再測定、warning0 build・XML/DOM host・Acid3/Acid2非増加を確認する。p190全1,561 ID census、外部DTD完全実装、script付きXHTMLは範囲外。失敗が残れば原因と上限を記録してunclearedで停止する。

Event ws074-p203-created-20261014: q1053固定1,561件の359 errorはすべてDOCTYPE付きXHTMLの厳密XML parser ENOTSUP。W3C XML 1.0はnon-validating processorに外部subset取得を義務付けないが、読んだ内部subsetは処理を要する。このため内部subsetは引き続き非対応として拒否し、外部IDだけを読込副作用なしに受け入れる。p196/p190/p173の受け入れは不変、GitHub publication保留。

## q1054-i01 結果（2026-10-14）

**部分scope cleared、p203 whole uncleared。** 外部IDだけのDOCTYPE構文を外部取得なしで受理し、内部subset・未知実体は拒否。固定359 ID中312件の読み込みを解消し、122 local PASS/190 pixel FAIL/47 load error。[証拠](evidence/20261014-q1054/README.md)。残り47件は全てXHTML 1.0 DTD由来の`&nbsp;`を使い、限定non-validating parserでは解決しない。host XML model243/243・DOM54/54・document16/16・binding34/34、warning0 build、固定Flex10/10 pixel PASS、Acid3 score100/100・差0/480000、Acid2差1865/120000不変。p190/p173全件受け入れは未達。Event ws074-p203-q1054-outcome-20261014: q1054-i01 partial cleared、p203 wholeは47 load errorでuncleared。未知実体を無条件許容せず、別範囲でXHTML標準実体を扱う。GitHub publication保留。

## q1055後続判定（2026-10-14）

Event ws074-p203-q1055-followup-20261014: [p204](../phase204/phase.md)でXHTML1 Strictの既知`nbsp`を限定的に解決し、q1054残り47 IDのload errorは47→0。[後続証拠](../phase204/evidence/20261014-q1055/README.md)。q1054の312成功と合わせて固定359 IDのDOCTYPE読込error解消を確認。q1054 attemptのpartial結果は改変せず、p203 wholeを後続証拠でcleared。画素不一致はp190へ残し、p173全件100%は未達。GitHub publication保留。
