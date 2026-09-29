<!-- awesome-plan project=zedbsd record=ws074-p097 -->

# ws074-p097: 複数の公開siteの画像・DOM比較とlayout改善

Status: in-progress
Disposition: normal
Parent: [WS074](../ws.md)
Queue: q506-i01

## 目的

阿部寛のホームページを最初のデモにし、Wikipedia、Hacker News、danluu.com、GitHubのrepository page、Reddit、The Verge、Medium、Yahoo! JAPAN、Craigslistを同じ固定Chrome User-Agentで保存する。browserとChromiumの画像・box・DOM treeを比較し、複数siteまたは標準fixtureへ一般化できるlayout差を直す。

## 受け入れ条件

- 阿部寛のホームページをzedBSD browserで開き、Keilandのwindow枠とdesktopを含むguest画面全体のscreenshotをデモ用に保存する。
- 対象10 pageを固定取得し、HTTP status・最終URL・challenge signal・入力hashをmanifestへ記録する。challenge等で同じ内容を取得できないpageは、その事実をreportに残して比較対象から外す。
- browserとChromiumを同じviewport・font・locale・時刻・Chrome User-Agentで描画し、画素・ink・boxの指標をJSONへ記録する。
- 両engineのDOMを、node種別・tag・属性・textを含む共通の正規形へ変換し、順序を保った一致率と差分をpageごとに記録する。DOMの差と描画の差を対応付けられるようにする。
- 差を複数pageで調べ、このQueueでは一般化できる原因を最大4件修正する。site固有の座標補正は入れない。
- 変更した機能のtargeted regressionをplain・ASanで通し、style checkと`git diff --check`を通す。guestはデモscreenshotの確認に使い、aggregate smokeは行わない。

## 対象

| tag | URL |
| --- | --- |
| abe | `http://abehiroshi.la.coocan.jp/` |
| wikipedia | `https://ja.wikipedia.org/wiki/メインページ` |
| hacker-news | `https://news.ycombinator.com/` |
| danluu | `https://danluu.com/` |
| github | `https://github.com/awemorris/zedBSD` |
| reddit | `https://www.reddit.com/` |
| the-verge | `https://www.theverge.com/` |
| medium | `https://medium.com/` |
| yahoo-japan | `https://www.yahoo.co.jp/` |
| craigslist | `https://www.craigslist.org/area/tokyo` |

## 前提・規約

- p094の再現可能な比較器とp096の汎用capture tool、固定Chrome User-Agentを土台にする。
- [Guardrail](../../guardrail.md)と[コーディング規約](../../coding-style.md)を適用する。HAL、公開ABI、vendor sourceは変更しない。
- 公開siteへの取得はpageとassetをcacheし、反復比較ではnetworkへ再取得しない。取得物・画像・reportは`build/`だけに置く。
- ユーザーの指示どおり、最初に阿部寛のホームページのfull-screenデモ画像を作り、その後に比較と修正を進める。
