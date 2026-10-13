<!-- awesome-plan project=zedbsd record=ws074-p204 -->

# ws074-p204: XHTML 1.0 Strict nbsp entity subset

Status: cleared（2026-10-14 q1055-i01）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p203 q1054](../phase203/evidence/20261014-q1054/README.md)の残り47 `nbsp` error IDと外部ID DOCTYPE parser。

## 目的と受け入れ

XHTML 1.0 Strict の確定した公開DTD識別子を持つdocumentで、`nbsp`の宣言済み実体値U+00A0を内部表から解決する。一般XMLや別DTDの未知実体、内部subsetは拒否を維持する。q1054で残った固定47 IDの読込errorが0となり、XML host/固定Flex/Acid回帰を満たすことをwhole clearance条件とする。XHTML全実体表・外部DTD取得/既定属性・script付きXHTML、p190/p173全件100%は別。

適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)、[XHTML 1.0 Strict DTD](https://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd)、[XML 1.0 §4.1/§5.1](https://www.w3.org/TR/xml/#sec-references)。libbrowser内部のみ変更し、外部ネットワーク読込、WPT、公開ABI、`plan/master.md`を変更しない。

## q1055-i01 の有限部分範囲

2026-10-14 02:48 JST、このchatのユーザーによるInterop 2025全件100%までの有限Queue事前承認を適用。最大90分（04:18 JSTまで）。固定47件の同一公開DTD識別子を照合した上で、`xml/parser.c`がこのDOCTYPEに限り`nbsp`をU+00A0へ展開する小さい表を追加する。DOCTYPEのpublic/system構文検証、root照合、内部subset拒否、未知実体拒否を維持する。host XMLで認可対象/別DTD/DOCTYPE無し/未知名/属性中を検証し、固定47 ID・固定Flex10 ID・warning0 build・XML host・Acid3/Acid2非増加を再測定する。全実体表や外部DTD取得、p190全1,561 ID再測定は含めない。新しいerrorまたは回帰があれば証拠を記録してunclearedで停止する。

Event ws074-p204-created-20261014: q1054の残り47件は全て`<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN" ...>`と`&nbsp;`の組み合わせでInvalid argument。XML一般の未知実体を解決済みと見なさず、既知DTDに限定した値をp203から分離。p190/p173分母は不変、GitHub publication保留。

## q1055-i01 結果（2026-10-14）

**cleared。** XHTML 1.0 Strict公開識別子と`html`ルートだけで`nbsp`をU+00A0へ展開。host XMLは属性/本文の値、一般XML・SYSTEMのみ・別ルート・未知名の拒否を検証し249/249。旧load error47件は全て読込成功、0 local PASS/47 pixel FAIL/0 error。[証拠](evidence/20261014-q1055/README.md)。XML DOM54/54・document16/16・binding34/34、warning0 build、固定Flex10/10 pixel PASS、Acid3 score100/100・差0/480000、Acid2差1865/120000不変。47件の画素差はwriting modes実装残差としてp190に残す。Event ws074-p204-q1055-outcome-20261014: q1055-i01 whole cleared、p203の旧359件load errorも後続証拠で解消。p190/p173全件受け入れは未達、GitHub publication保留。
