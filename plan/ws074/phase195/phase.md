<!-- awesome-plan project=zedbsd record=ws074-p195 -->

# ws074-p195: Interop 2025 flexbox XHTML render timeout

Status: uncleared（2026-10-14 q1045-i01）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p190 q1044](../phase190/evidence/20261014-q1044/README.md) の固定10 timeout ID・5秒上限の証拠。p190 whole clearanceは不要。

## 目的と受け入れ

固定Interop 2025 の静的reftest 10 IDが集中する `css/css-flexbox/flexbox-justify-content-*` XHTML のheadless render長時間停止を一般欠陥として切り分ける。10 IDが同じ再現条件でtimeout 0となり、既存のhost/Acid3回帰を維持したことを全Phase clearance条件とする。描画のmatch PASSまでは本Phaseの受け入れに含めず、reftest差画素はp190/p173へ残す。

適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)。libbrowser内に閉じ、公開ABI/Wayland境界を変更しない。`plan/master.md`は変更しない。

## q1045-i01 の有限部分範囲

2026-10-14 01:57 JST、ユーザーの事前承認を適用、最大90分。q1044 timeout10 IDのうち代表2件を直接file/localhostで再現し、5秒停止がparse/layout/paint/runner待機のどこかを有界に診断する。一般的な単一欠陥であれば最小修正し、10 IDを同じ5秒・固定font/viewport条件で再測定する。host build warning0/関係試験とAcid3/Acid2差画素非増加を確認する。大きなflexレイアウト再設計や無関係な描画FAIL、WPT/公開ABI/`master.md`変更は含めない。根因がこの範囲で確定しなければ試行をunclearedで止め、stack/タイミングと再開条件を保存する。

Event ws074-p195-created-20261014: p190 q1044で固定静的全1,561件のlocal baselineを得たが、flexbox justify-content XHTML10件が5秒timeout。runnerではなくbrowser内部の一般欠陥かを分離する新Phase。p190のrunner受け入れ・p173全件ゴールは変えない。GitHub publication保留。

## q1045-i01 結果（2026-10-14 02:02 JST）

**uncleared。** `flexbox-justify-content-horiz-003.xhtml` と `004.xhtml` は直接file描画でも6秒timeout、`003-ref.xhtml` は即時描画。gdbで003を中断したスタックは `inline_cut_text` → `layout_inline` → `layout_block` → `layout_flex`。ただしこのlayout負荷は根因でなく、`--dump=dom` ではXHTML宣言がHTMLコメントとなり、53個の自己終了`<div/>`がHTML parserで閉じられず深い入れ子になった。最上位 `page_load_file` / `page_load_location` は常に `page_load_html` に進み、HTTP MIMEもparser選択へ渡さない。一方iframeには既存の厳密XML parserとDOM projectionがある。[再現・stack証拠](evidence/20261014-q1045/README.md)。

単一flexbox欠陥の最小修正ではないため、このQueueではコードを変更しない。10 ID再測定・host/Acid回帰は修正なしのため省略。最上位文書のContent-Type処理、XML parser接続と脚本/DOM lifecycleの設計を[p196](../phase196/phase.md)で扱う。p190/p173全件の受け入れは未達。Event ws074-p195-q1045-uncleared-20261014: root causeとscope境界を記録。GitHub publication保留。
