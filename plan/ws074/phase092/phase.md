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
