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

## 2026-10-08 実装（q858 の後、P1）: 範囲 1・4

Q1 の ACK: 範囲 1〜5、1 → 4 → 2・3 の順。2 の head の帯に出す物はユーザーに確認中（Q1）。

### 1. head の窓のリサイズ

調べたこと（読み取り）: head での press は p007 の `remote` でも `window_at` → `HIT_FRAME` → `kwl_toplevel_resize_start` に届く。効かない原因として見つけた物:
- `frame_under_pointer`（shell.c）が anchor の帯（system bar・左右の desktops の swipe・下の Wiseview）を平面の座標のまま当て、`pointer_x >= server->width - DESKTOP_EDGE` で head の全ての点を外していた → head の窓の縁で resize の矢印が一度も出ない（縁が見つからない）。
- `resize_limit`（toplevel.c）が上限を anchor の幅・高さに、上端の限りを anchor の system bar の下（`window_lowest`）にしていた → anchor より大きい head では anchor の大きさで止まり、head の y が anchor と違うと上端の限りがずれる。

直し: `frame_under_pointer` の帯は pointer が anchor にある時だけ。`resize_limit` と `window_lowest` は窓の出力（`kwl_outputs` の slot、表示していなければ anchor）の大きさと `outputs[slot].y + kwl_output_top(slot)`（head は title bar の分だけ）。client の move の始まり（`move_anchor`）の上端の限りも同じ関数で窓の出力の物に。
- 実機で効かなかった原因がこの 2 つで全部かは、QEMU で見られない（usb-tablet は絶対座標で anchor だけ）。5330 でユーザーが確かめる。

### 4. App Home の時の head

- `home.c`: stage（黒い glass と上の中央の光）を `home_stage(x, y, width, height)` に分け、`kwl_home_draw`（anchor）と新しい `kwl_home_draw_head`（head の平面の矩形 `server->view_*`、icon・時計・検索は描かない）が使う。
- `shell.c` の `kwl_glass_draw_head`: App Home の progress が 0 より大きい間（開く・開いている・閉じる）は stage だけを描き、窓・popup を描かない（pointer は heads.c が描く）。head での press は App Home が見えている間は食べる（見えない窓を押さない）。release は従来どおり `kwl_home_button` が取る。
- `heads.c` の `heads_shows`: App Home が見えている間は mask の 4 を立て、head を毎 frame 描く（閉じた後の 1 frame も前の mask で描く）。
- 開く・閉じる動きの間、head は stage が出て窓が消えるだけ（anchor の窓が奥へ下がる動きは head には無い）。

### 確認（host・build）

- build: `make -j16 BUILD=build/p1-wl ZEDBSD_CONFIG=plan/ws113/tests/config-amd64-p005.mk build/p1-wl/bin/wayland` exit 0、warning 0。`make keiland-linux`（gcc）warning・error 0。
- host: `host-plane.sh` PASS（plain、ASan/UBSan）、`host-output-switch.sh` PASS。style-check は変えた file で増えない（shell.c 5→5、home.c 3→3、heads.c・toplevel.c 0）。
- 未実施: QEMU（T1、範囲 2・3 の後にまとめて依頼）: Super+Shift+Right で head 1 へ移した窓で App Home を開閉して head の PNG（stage だけ・閉じて窓が戻る）。head の縁の drag は実機（5330）。
