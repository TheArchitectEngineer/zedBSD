<!-- awesome-plan project=zedbsd record=ws035p130 -->

# ws035-p130: 表示の surface と pipelines を READY の前に作る

Phase ID: `ws035-p130`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-29、サブエージェント、worktree `wt/ws035`。QEMU の Venus、実機は未実施）
Phase disposition: normal
Queue: なし（2026-09-29 main の割り当て「pipelines の作成を READY の前に」、[p129](../phase129/phase.md) の残り）

## 範囲と設計

login・Log Out の替わり目で、表示を取る側（session・greeter）は GO の後に `zwl_compose_output_open` で display の surface・swapchain・render pass と
pipelines・targets を作っていた。その間は前の持ち主の画を保つ（p126）が、止まって見える時間になる。

- display の surface（`vkCreateDisplayPlaneSurfaceKHR`）と surface の format の問い合わせは lease を取らない（libvulkan の WSI は swapchain の作成で
  `GPU_DISPLAY_CLAIM` する: `wsi-swapchain.c` の `platform->claim`）。
- なので surface・format・render pass と pipelines を READY の前（`enter_window_mode` の `zwl_handoff_wait` の前）に作り、GO の後は swapchain と
  targets だけにする。

## 実装

- `userland/desktop/vkdemo/display.c`・`display.h`: `vkdemo_display_choose_format()`（新、公開）: swapchain の作成と同じ `choose_format` で format だけを決める。
- `userland/desktop/wayland/compose.c`・`compose.h`・`zwl.h`: `zwl_compose_output_prepare()`（新）: surface と format、最初の 1 回は render pass と pipelines。
  `compose->output_prepared`。`zwl_compose_output_open()` は prepare（済みなら何もしない）→ swapchain（format が pipelines の format と違えば失敗）→ targets。
  `zwl_compose_output_close()` は prepare だけの surface も閉じる。
- `userland/desktop/wayland/display.c`: `enter_window_mode()` が `zwl_handoff_wait()` の前に `zwl_compose_output_prepare()`（失敗は GO の後にもう一度）。

## 検証（amd64、Venus の guest、worktree の graphical の login の image、前 `build/p130-before.img` = p038 の image、後 `build/p130-after.img`）

- `zdesktop-p126.sh` 3 周（1280x800）: 前・後とも黒 0・文字 console 0 で PASS。替わり目の保った画（dmesg、release → 次の最初の frame）:
  前 1163・1263・1199・1243・1411・1230 ms（平均 1252）→ 後 1169・1164・1143・1183・1235・1155 ms（平均 1175、約 80 ms 短い）。
  後の `ZWL STARTUP`: `output-pipelines` 約 160 ms と `output-display` 3〜7 ms は READY の前に移り、GO の後は `output-swapchain` 616〜739 ms と
  `output-targets` 83〜179 ms。
- 回帰 PASS: `zdesktop-p052.sh`（window → fullscreen → window: 2 回目の `MODE window` は prepare のやり直しを通る、switch 366 ms）、
  `zdesktop-p053.sh`（wl_shm・cursor・fullscreen）、`zdesktop-p101.sh`、boot test（`build/p130-after.img`）。
- build（worktree の `build/amd64`、graphical の login の image）warning 0。規約: `plan/tools/style-check.py` compose.c・display.c 0、vkdemo の display.c は変更の前後で同じ。

未実施: 実機（i915）。

## 分かったこと（残り）

- 替わり目の残りの主は GO の後の swapchain の作成（1280x800 で 616〜739 ms、libvulkan の WSI: claim と blob の image の確保）。libvulkan の件（ws.md の残り）。
- compositor の起動（READY まで）の画像ごとの約 200 ms（`zwl_host_image_create`: image の作成・memory の確保と map・layout の移行の submit と
  `vkQueueWaitIdle`、descriptor）。Venus の 1 回の同期の呼び出しが数十 ms かかる。次の Phase（p131）で起動の画像の layout の移行をまとめる。

## Resume point

2026-09-29: cleared。次は p131（起動時の画像の layout の移行をまとめ、wallpaper の読みとぼかしを短縮）。
