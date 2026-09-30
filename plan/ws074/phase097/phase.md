<!-- awesome-plan project=zedbsd record=ws074-p097 -->

# ws074-p097: 複数の公開siteの画像・DOM比較とlayout改善

Status: cleared（2026-09-30）
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

## 追加された範囲

実行中のユーザー指示により、1024x690と1280x900の現実的なviewportでの比較、GitHubのJavaScriptの追跡、固定WPT reftest、
Acid2・Acid3の履歴的診断もこのattemptへ加えた。修正数の上限は維持し、次の大きな機能は別Phase候補へ残した。

## デモと固定取得

- 阿部寛のホームページをguestで表示し、Keilandのdesktopとbrowserのwindow枠を含むfull-screen画像を保存した。ユーザー向けのcopyは
  `zedbsd-abe-hiroshi-full-screen.png`、Amazonの同じ形式は`zedbsd-amazon-full-screen.png`。
- `site-capture.py`は固定Chrome 153 User-Agent、source encoding、frameの固定化、assetのlocal化、入力hashをmanifest schema 3へ記録する。
  GitHubだけはscript付きの`github-local.html`も作り、他の比較用にはscriptを除いた固定HTMLを使った。assetは390件、取得失敗0。
- 10 URLのうち9件はHTTP 200でchallenge signalなし。MediumだけはHTTP 403・challenge signal 2件で固定版を作れず、比較から除外した。
  阿部寛のページはCP932を正しく読み、2つのframeを1ページへ固定した。GitHubの`zedBSD` URLは現在のrepository名`Kei`へ到達した。

### Amazonデモ画像の訂正（2026-09-30）

初回に成果物としたAmazon画像はJavaScript完了後の表示を示しておらず、証跡として無効だった。現行のguest browserでscript付き固定版を再実行すると、
普通のJavaScript objectへ大きな数値keyを定義した後にdense elementsを数GiBへ誤拡張し、SIGSEGVでwindowが閉じる問題を再現した。
`vm_object_define`でsparse numeric slotがdense rangeの`length`を進める処理をArrayだけに限定し、同じ並びの回帰を`host-object`へ加えた。

修正後は1280x800のguest画面、1200x690のbrowser windowで70秒後もprocessが生存した。JavaScript再描画後のlogin flyout、sale card、discount badge、
「最近閲覧した商品とおすすめ商品」を目視し、画面全体を`zedbsd-amazon-full-screen.png`へ差し替えた。consoleには未対応API等によるTypeErrorとfetch失敗が残るため、
この画像はJavaScript有効・再描画到達の証跡であり、Amazon互換の完了を示すものではない。詳細は`results/amazon-js-guest.txt`。

## Chromium比較

両engineへ同じ固定HTML・font・locale・Chrome User-Agentを渡した。DOMはnode種別・tag・属性・textのJSONLへ正規化し、順序を保つ一致率を取った。
boxは同じtagを文書順に貪欲対応する粗い診断値であり、DOM差が大きいpageの準拠率とは扱わない。

| page | 1024x690 pixel / ink / DOM / box | 1280x900 pixel / ink |
| --- | ---: | ---: |
| 阿部寛 | 86.06 / 36.56 / 100.00 / 25.16 | 88.05 / 30.05 |
| Wikipedia | 74.55 / 2.77 / 99.93 / 0.00 | 85.89 / 2.75 |
| Hacker News | 86.25 / 84.72 / 100.00 / 0.00 | 89.34 / 88.22 |
| danluu.com | 90.61 / 19.02 / 100.00 / 24.80 | 92.94 / 18.09 |
| GitHub | 93.13 / 61.80 / 86.00 / 12.40 | 94.96 / 64.30 |
| Reddit | 96.68 / 0.23 / 25.35 / 60.00 | 97.96 / 0.23 |
| The Verge | 86.87 / 50.55 / 91.24 / 0.68 | 70.56 / 34.48 |
| Yahoo! JAPAN | 83.95 / 49.10 / 94.25 / 2.31 | 88.71 / 52.16 |
| Craigslist | 100.00 / 0.00 / 99.91 / 9.47 | 100.00 / 0.00 |

reportは`build/ws074-sites-final-1024/report.json`と`build/ws074-sites-final-1280/report.json`。RedditとCraigslistは可視contentがほぼ無い
curtainの固定版なので、高いpixel値を表示成功とは数えない。Amazon topも同じ現実的な2サイズで取り直し、1024x690は83.63%/ink 77.63%、
1280x900は72.88%/ink 65.76%、DOMは約77.5%、Uncaughtは両方4件だった。無限に広いcanvasだけでなく小さいviewportを継続して比較する。

## 一般化した修正

1. intrinsic widthの計測で`margin-left/right:auto`を長さへ足さないようにした。Wikipediaで262144pxまで膨らむ経路と、入れ子のflex fixtureを直した。
2. HTMLのpresentational hintをspecificity 0のauthor declarationとしてcascadeへ加えた。table/cellの`width`・`height`、`cellspacing`、
   `align`・`valign`・`bgcolor`、fontの`color`、cellの`nowrap`をsanitiseしてCSSへ変換する。
3. table gridへ`rowspan`を実装した。前のrowから占有されたcolumnを避け、span全体の必要高をrowへ配り、`vertical-align`でcontentを置く。
   阿部寛の1024画像は初期79.76%/ink 12.13%から86.06%/ink 36.56%へ改善した。
4. Acid3の最初のtop-level停止原因だった`window.postMessage`を、同一browsing contextの後続taskで`MessageEvent`を送るAPIとして実装した。
   `data`・`origin`・`lastEventId`・`source`・`ports`とconstructorを持つ。frame間通信、structured clone、transferは後続範囲。

## GitHubのJavaScript

Chromium側のlocal CSS取得を`--allow-file-access-from-files`で許可し、以前のCSSなしbaselineを無効にした。修正後の静的GitHubは1024で
93.13%/ink 61.80%。一方、script付き固定版にある実行対象10本は全て`type=module`で、browserはmodule scriptを明示的にskipする。
そのためUncaught 0と静的版と同じ画像は、hydration成功を意味しない。parserはmodule grammarを読めるがcompiler/runtimeは`import`・`export`をまだ実行できない。
module graphの取得・link・評価と、`performance`等の後続APIをws074-p098候補へ分けた。

## WPTとAcid

- WPTはcommit `2d66b9b7998bb58c336138c178323ddee857b586`を固定し、licenseを再確認した。`run-wpt-reftests.py`はlocal HTTPでresourceと
  Ahemを配り、scriptなしCSS2 reftest 5904件からfirst-level directoryのround-robinで100件を固定抽出する。結果は44 pass、56 fail、0 error。
- `run-acid-tests.py`はWPTの履歴的なAcid2/Acid3を使用する。Acid2は説明を隠して顔をviewportへ出すharnessでexact matchは不合格、
  pixel agreement 90.56%。Acid3は変更せず、`postMessage`前はscoreが`JS`のままだったが、修正後は9/100、pixel agreement 40.35%。
  次の大きな不足はDOM Traversal・Range等。WPT自身の注意どおり、Acidの結果は現在の標準への適合証明ではない。
- 数は`results/wpt-reftest.txt`と`results/acid.txt`、詳細reportは`build/ws074-wpt-reftest-p097-final/report.json`と
  `build/ws074-acid-final/report.json`へ保存した。

## 回帰と結果

- Amazon訂正の`host-object`はplain・ASanとも100 checks、0 failed。guestの同じscript付き固定版は70秒後もbrowser processが生存し、追加のSIGSEGVは無かった。
- host buildはplain・ASanともwarning 0。DOM 22/22（MessageEventと非同期postMessageを含む）、position 22 checksを両方で通した。
- `intrinsic`・`tables`のDOM/style/layout/paint goldenはplain・ASanで16/16一致。tableの専用画像はpixel 97.56%/ink 91.73%。
- Python 5本は`py_compile`、変更したC/headerは`style-check.py` 0件、`git diff --check` 0件。
- guestは先行した阿部寛とAmazonのfull-screenデモだけに使い、指示どおりaggregate smokeは実施していない。GitHubへは未公開。

受け入れ条件を満たしたためq506-i01をclearedとする。残るCSS2 reftest群はp036、DOM testharnessはp047、GitHubのmodule実行はp098候補で扱い、
次のQueueは自動では開始しない。
