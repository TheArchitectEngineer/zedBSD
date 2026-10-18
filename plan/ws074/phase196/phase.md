<!-- awesome-plan project=zedbsd record=ws074-p196 -->

# ws074-p196: top-level XML/XHTML document loading

Status: uncleared（q19 scoped partial cleared、whole scripting/環境/API等未達）
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

## q1046-i01 結果（2026-10-14 02:07 JST）

**partial scope cleared、whole uncleared。** 最上位file/HTTPでMIMEを保持し、script-free XHTMLを既存XML parser/projectionに接続。XML DOMの子を既存DocumentにadoptしてWindowを維持。[証拠](evidence/20261014-q1046/README.md)。固定timeout10 IDは同じ5秒条件で0 timeoutへ改善、1 local PASS/9 FAIL。HTTP XML/HTMLとfile XHTMLのDOM試験4/4、host build warning0、XML DOM54/54・resource105/105・frame load52/52、Acid3差0・Acid2差1865を確認。残り9描画FAILはFlexbox等の描画Phaseへ。p196 wholeにはscript付きXHTMLとload lifecycleの検証が残る。その他XML MIMEとXMLエラー表示はこのPhaseの受け入れ対象外。p190/p173全件100%未達。

Event ws074-p196-q1046-outcome-20261014: q1046-i01 partial cleared。10件timeoutを全解消、描画不一致9件と残余XML lifecycleを分離。GitHub publication保留。

Event ws074-p196-q18-xhtml-blocker-and-redesign-20261018: [p190 q18 evidence](../phase190/evidence/20261018-q18/README.md)。公開SDK first-ready clientはnative10対照中9/10、XHTMLのみ未対応。独立XMLでChrome3 lifecycle行/native0を確認、最上位page_load_xhtmlがscriptsを実行せずloadを発火していた。q18-i01 uncleared/i02未着手uncleared、required criterionは弱めない。p196のgeneric HTML-namespace classic inline/CDATA実行をload前へ接続する有限outputを次Queue候補にし、p196 verified output→p190同10対照→scripted413へ依存を改訂。q1046 script-free/q17既存清算は保持。native codeはこのQueueでは変更せず、source不変の1369確認済みPASSを維持。shared projection pending。

## q19 exact redesign / execution scope（2026-10-18、16:28 JST bound）

[Queue](../queue.md)でp196 classic inline producer→p190同10controls→scripted413を選定。engine生成はpage/page.cだけ。既存strict XML parse/project/adoptの後・page_fire_loadの前に、Window-owned XHTMLのHTML namespace/local lowercase scriptをgeneric既存inline backendで実行するpartial。全script snapshotとsaved currentScriptをrootしてからcallbackを開始し、connected/started/template-inert・namespace/type/srcとdetachedを守る。self/next removalとGC、currentScript設定/復元、通常例外で後続が続くことを確認。外部/module・parser streaming timing/document.write等のfull XML scriptingは残りとして保持し、script-free q1046の結果を不当にwhole clearへ変えない。

p190のq18 clientと同じ10 positive/negative対照はXMLをskipせず再実行する。必要outputが成立してからfirst-ready/fuzzy/graph/pixel controls→actual413へ進む。新engine final pairの全1491/static1561/strict48でold whole491/839/39維持・全callbacksを照合し、未対応APIやtestdriverは非PASSとして保存。C全文/browser component全文/format/new logic style0/host/target/DOM/XML/Acidを適用。上限を延ばさず未達ならunclearedとresumeを残す。shared projection pending。

Event ws074-p196-q19-selected-redesign-20261018: q18 XML脚本不足をscopeのあるnative producerへ分離し、必要outputのverified preflight→p190同controls→413へ実行順を確定。origin p190/foreign p196の各自身の依存・検証・resumeを同時更新。q18 mandatory criterionは弱めず、新attemptで再検証する。承認は自走契約、旧1369 PASSは新final source測定が終わるまでlast verifiedとして保持。

Event ws074-p196-q19-partial-cleared-20261018: [q19 evidence](evidence/20261018-q19/README.md)。全1491 harnessは491 PASS・callback11531 PASS/20226 FAIL/14 NOTRUNを維持、whole/callback後退・追加・欠損・順序変更0。全static1561は839 PASS/722 FAIL・全reference pixel metrics unchanged。strict48は39 PASS/9 readiness_missingを全ID対応で維持。scripted413は全native attempt/568 capturesで82 PASS・73 pixel FAIL・242 unsupported_environment・16 readiness_missing。確認済み1451/3805（38.134034%）、新規計測PASS82増。harness未attempt292、環境/API/worker等の未達は分母に保持。 producer/consumer/measurementの各partialはcleared、whole Phaseは未達を維持。独立Chrome XML5行・native snapshot安全性・22controls・target warning0/C新logic0/DOM35/XML249+54/Acid非増加を確認。source/tool1c6991441、全raw/checkpoint/ID/pair/input hash read-back済み。q18未達は履歴保持、shared projection/target full/p100はpending。
