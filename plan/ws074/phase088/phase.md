<!-- awesome-plan project=zedbsd record=ws074-p088 -->

# ws074-p088: 動的に挿入された外部script

Status: in-progress（2026-09-30）
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
