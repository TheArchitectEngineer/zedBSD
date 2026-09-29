<!-- awesome-plan project=zedbsd record=ws074-p088 -->

# ws074-p088: 動的に挿入された外部script

Status: cleared（2026-09-30）
Disposition: normal
Parent: [WS074](../ws.md)
Queue: q502-i01

## 目的

scriptがDOMへ挿入した`<script src>`を非同期に取得して一度だけ実行し、成功時に`load`、失敗時に`error` eventを送る。Amazonのbootstrapが挿入するAUIの外部scriptを継続して実行できるようにする。

## 受け入れ条件

- `appendChild`・`insertBefore`・`replaceChild`などでdocumentへ接続された外部scriptを取得し、一度だけ実行する。
- 動的inline script、parser inserted、`innerHTML`で作られたscriptの実行条件をChromiumの小さいfixtureと合わせる。
- 外部scriptの`load`・`error` eventと順序をfixtureで確認する。
- Amazonの動的比較で取得・実行本数、Uncaught、画素・inkの変化を記録し、次のblockerを分ける。

## 実装

- DOM bindingの全挿入口から、documentへ接続されたsubtreeをpageへ通知する。parserが実行したscriptとfragment parserが作ったscriptにはalready-started flagを付け、移動しても再実行しない。
- 動的inline scriptは挿入中に同期実行する。外部scriptはpageのloaderで非同期取得し、成功時に`load`、取得・decode失敗時に`error`を送る。pending中のelementはGC rootにする。
- file/data URLも現在のscriptとmicrotaskが終わったcheckpointで実行する。`async=false`は挿入順を保ち、通常のdynamic scriptは到着順に実行できる。
- `HTMLScriptElement`の`src`・`type`・`async`をcontent attributeへ反映する。Amazonのloaderが使う`script.src = URL`が取得へ届く。

## 検証と結果

- Chromiumと同じfixtureで、appendChild・insertBefore・replaceChildのdynamic inline、parser scriptの移動、innerHTMLのinert script、外部scriptの実行と一度だけのload、missing scriptのerror、実行順を確認した。
- host plain buildはwarning 0。DOM回帰20/20。ASan・UBSanのfixtureはleakを含めてPASS。style-check 0、`git diff --check` 0。
- scriptを除いた固定比較は不変: top 84.06%/ink 80.56%、search 76.32%/ink 34.48%。
- dynamic診断ではtopのAUI後続scriptまで進み、画素69.23%/ink 61.20%、Uncaught 9。`fetch`、`MutationObserver`、`ResizeObserver`、`IntersectionObserver`、`atob`の不足が初めて現れた。searchは追加scriptとtimerを実行して180秒を超えた。次はp092でWeb APIとsettle時間を直す。
- hostだけで検証。guest・bootはこのDOM Phaseでは未実施。GitHubへは未公開。
