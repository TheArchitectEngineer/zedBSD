<!-- awesome-plan project=zedbsd record=ws074-p092 -->

# ws074-p092: Amazonの後続scriptが使うWeb API

Status: cleared（2026-09-30）
Disposition: normal
Parent: [WS074](../ws.md)
Queue: q503-i01

## 目的

p088で実行できるようになったAmazonのAUI後続scriptを止めるWeb APIを実装し、topのdynamic描画をChromiumへ近づける。長時間続くtimerのheadless settleも、実ページを診断できる時間に収める。

## 受け入れ条件

- `atob`・`btoa`、Amazonがfeature detection後に使うobserver API、`document.elementsFromPoint`の小さいChromium fixtureを通す。
- `fetch`のGETとResponseの基本をpageの非同期loaderとPromiseへ接続し、成功・HTTP error・network errorをfixtureで確認する。
- Amazon topのdynamic比較を180秒以内に終え、Uncaught、画素・inkの変化を記録する。
- searchでsettleが長引くtimerを特定し、virtual time budget内の処理を有界にする。

## 初期証拠

p088のdynamic topは69.23%/ink 61.20%、Uncaught 9。`fetch`、`MutationObserver`、`ResizeObserver`、`IntersectionObserver`、`atob`が不足する。searchは追加scriptとtimerの実行後に180秒を超えた。script無しの固定比較はtop 84.06%/ink 80.56%、search 76.32%/ink 34.48%。

## 実装と結果（2026-09-30）

- `atob`・`btoa`、初回のlayout recordを渡すobserver、`document.elementFromPoint`・`elementsFromPoint`、GETの`fetch`とResponseの`text`・`json`、最小の`XMLHttpRequest`を実装した。`fetch`はpageの非同期loaderへ接続し、Promiseをloader callbackのtask境界で解決する。Chromiumと共通の`web-api.html`を追加し、DOM回帰21/21、JS回帰14/14、非同期HTTP回帰18/18（fetchの200・404・network errorを含む）、ASan・UBSanで同じDOM 21/21・HTTP 18/18、style-check 0、host build warning 0。
- `amazon-capture.py`がscript文字列内の一重引用符の`<img src>`を二重引用符へ変え、正しいAmazonのscriptを壊していた。元の引用符を保持して再生成し、隠れていたFlyout templateを走らせた。
- templateが生成するsloppy modeの`with`をVMへ実装した。内側からobjectのpropertyを探し、無ければ静的bindingへ戻る。これで`not supported yet: with`は消えた。
- sign-in tooltipの黄色いログインボタンが画面幅まで広がり、navを覆っていた。絶対配置の親のshrink-to-fit測定中に、子inline-blockの`width:100%`を巨大な仮幅へ解決していたためだった。intrinsic幅の測定中はpercentageを未確定として内容幅を測り、最終幅では100%を解決する。専用のposition回帰を追加し、topは72.87%/ink 65.91%へ改善した。
- `MutationObserver`へ`childList`・`subtree`の変更通知、checkpointでのcallback、`takeRecords`、`disconnect`を実装した。append・insert・remove・replace、textContent、inner/outerHTML等のbindingの変更口を通知し、Chromiumと共通のfixtureを追加した。attributes・characterDataのoptionは受け付けるが、そのrecordは今後の範囲。
- headless timerは100 roundで有界になった。dynamic topは約32秒、XHR前65.79%/ink 57.19%からXHR後69.64%/ink 62.10%へ改善し、Mutation通知後は72.87%/ink 65.91%、Uncaught 4。capture修正前の69.23%との直接比較はできない（以前は壊れたscriptをChromiumもbrowserも実行していた）。dynamic searchは約65秒、73.79%/ink 28.78%、Uncaught 5。
- sign-in tooltipは親の`z-index:100`から子の`z-index:auto`を切り離していたため、Prime欄の背後へ沈んでいた。明示的なz-indexが作る外側のstacking levelにpositioned descendantを留め、描画順とhit testを揃えた。白いtooltipと黄色いbuttonがAccount & Listsの直下で前面に出る。Chromiumのvirtual-time画像にはtooltipが無いため、この正しい表示を含む最終値は71.81%/ink 64.58%、Uncaught 4。
- host plain buildはwarning 0。DOM回帰22/22、JS回帰14/14、非同期HTTP回帰18/18、position 22 checks。ASan・UBSanでDOM 22/22、HTTP 18/18、position 22 checks。style-check 0、`git diff --check` 0。guest・bootはこのWeb API Phaseでは未実施。GitHubへは未公開。
- 残る例外はtyped array等の次の機能の候補として残す。p092のWeb APIと有界な比較の受け入れ条件は満たした。
