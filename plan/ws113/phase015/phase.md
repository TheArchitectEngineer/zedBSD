<!-- awesome-plan project=zedbsd record=ws113-p015 -->
# ws113-p015: 2 つ目以降の display の窓の状態（dock・floating・整列）、bar、リサイズ、App Home の背景

Status: planned（P1、WS090 の今の単位の後）
Disposition: normal
Parent: [WS113](../ws.md)

## 由来（2026-10-08 ユーザーの UAT、5330 の実機、HDMI）

「・2つめのディスプレイにカーソール移動でき、ウィンドウも移動できました。
・2つめのディスプレイではウィンドウのリサイズができませんでした。
・2つめのディスプレイでウィンドウの最大化をどうするか悩みますね。ドックするためにバーを足すか、最大サイズにして終わるか。ドックを追加して、ドッキング可能にするのがよさそうです。ディスプレイごとに、ドック状態かフローティング状態かアレンジメント状態かを、状態として持つのがよさそうです。また、App Home画面にしたとき、2つめ以降のディスプレイはApp Homeの背景だけにするのがよさそうです。」

## 範囲（案、P1 が詰める）

1. 2 つ目以降の display の窓のリサイズ（縁の drag）を効くようにする（今は効かない、BUG）。
2. display ごとに bar（dock の bar）を持ち、その display で docking できる（p007 の「head の窓は dock の前に anchor へ戻る」を置き換え）。
3. display ごとに窓の配置の状態（docked・floating・整列（WS181 の arrangement））を持つ。
4. App Home を開いた時、2 つ目以降の display は App Home の背景（暗い stage・壁紙の blur）だけにする。
5. 試験: host、QEMU（Venus 2 出力、head 1 の bar・dock・整列・リサイズ・App Home の背景）、5330 の実機（ユーザー）。
