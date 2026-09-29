<!-- awesome-plan project=zedbsd record=ws074-p092 -->

# ws074-p092: Amazonの後続scriptが使うWeb API

Status: in-progress（2026-09-30）
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

## 中間結果（2026-09-30）

- `atob`・`btoa`、初回のlayout recordを渡すobserver、`document.elementFromPoint`・`elementsFromPoint`、GETの`fetch`とResponseの`text`・`json`、最小の`XMLHttpRequest`を実装した。`fetch`はpageの非同期loaderへ接続し、Promiseをloader callbackのtask境界で解決する。Chromiumと共通の`web-api.html`を追加し、DOM回帰21/21、JS回帰14/14、非同期HTTP回帰18/18（fetchの200・404・network errorを含む）、ASan・UBSanで同じDOM 21/21・HTTP 18/18、style-check 0、host build warning 0。
- `amazon-capture.py`がscript文字列内の一重引用符の`<img src>`を二重引用符へ変え、正しいAmazonのscriptを壊していた。元の引用符を保持して再生成し、隠れていたFlyout templateを走らせた。
- templateが生成するsloppy modeの`with`をVMへ実装した。内側からobjectのpropertyを探し、無ければ静的bindingへ戻る。これで`not supported yet: with`は消えた。
- headless timerは100 roundで有界になった。dynamic topは約32秒、XHR前65.79%/ink 57.19%からXHR後69.64%/ink 62.10%へ改善し、Uncaught 4。capture修正前の69.23%との直接比較はできない（以前は壊れたscriptをChromiumもbrowserも実行していた）。dynamic searchは約65秒、73.79%/ink 28.78%、Uncaught 5。`fetch`の非同期化によるtopの画素値の変化はない。残りはXHRの非同期loader接続、MutationObserverの変更通知、searchの`URLSearchParams`・`Intl`等。
