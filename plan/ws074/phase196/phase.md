<!-- awesome-plan project=zedbsd record=ws074-p196 -->

# ws074-p196: top-level XML/XHTML document loading

Status: in-progress（2026-10-14 q1046-i01）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p195 q1045の診断](../phase195/evidence/20261014-q1045/README.md)により最上位XHTMLがHTML解析されると確定。p190 whole clearanceは不要。

## 目的と受け入れ

HTTP `Content-Type: application/xhtml+xml` とローカル `.xhtml` / `.xht` の最上位文書を厳密XMLとして解析し、既存のXML DOM projectionで実際のPage/Windowへ結び、XHTMLのstyle/render/脚本・load lifecycleを維持する。HTMLページはHTML parserのまま。固定Interop reftestのXHTML10件が5秒内に画像を作り、XML DOM・HTML/Acid回帰を通すことが最低条件。全Interop描画一致は[p190](../phase190/phase.md)/[p173](../phase173/phase.md)に残る。

適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)。変更はlibbrowser内部に限定し、公開ABI/Wayland境界と`plan/master.md`を変更しない。HTTP MIMEを失う既存ロード経路は契約を追跡して直す。

## q1046-i01 の有限部分範囲

2026-10-14 02:04 JST、ユーザーの「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を適用、最大90分（03:34 JSTまで）。最上位のfile/HTTP読み込みと非同期view responseのMIME経路を調べ、既存XML parser/projectionの再利用でscript-free XHTMLを最小限に正しく描く。HTML/XML分岐の小さなhost試験を作り、固定XHTML timeout10 IDを5秒・800×600・固定fontで再測定する。warning0 host build、関連host試験、Acid3/Acid2差画素非増加を確認する。script付きXHTML一般、SVG/XML一般、XMLエラー表示、全reftest pixel一致、WPT/公開ABI/Master変更は範囲外。安全な内部接続で完結しなければstack/設計障害を記録してunclearedで止める。

Event ws074-p196-created-20261014: [p195](../phase195/phase.md)で最上位XHTML誤解析が固定10件timeoutの根因と判明。p190 runner/p173全件目標は不変。p195からMIMEとトップレベルXML接続という独立の一般欠陥を分離。GitHub publication保留。
