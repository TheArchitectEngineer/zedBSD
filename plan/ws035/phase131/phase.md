<!-- awesome-plan project=zedbsd record=ws035p131 -->

# ws035-p131: 起動時の画像の layout の移行を 1 回の submit と 1 回の wait にまとめる

Phase ID: `ws035-p131`
Parent: [WS035](../ws.md)
Status: in-progress（2026-09-29、サブエージェント、worktree `wt/ws035`）
Phase disposition: normal
Queue: なし（2026-09-29 main の割り当て「ws035-p131（起動時の 4 つの画像の layout の移行を 1 回の submit と 1 回の wait にまとめる）」。Venus の同期の
遅さそのものと swapchain の作成は Future Work F-056、WS035 では Keiland の側で同期の回数を減らすことだけ）

## 範囲と原因

[p130](../phase130/phase.md) の計測: compositor の起動で `zwl_host_image_create`（`shm.c`）が画像 1 つに約 200 ms。そのうち layout の移行
（`image_layout`: command buffer の確保・記録・終わり・submit で約 50 ms、`vkQueueWaitIdle` で約 50 ms）が約 100 ms。起動時の画像は 4 つ
（arrow・wallpaper・ぼかした wallpaper・glyph の atlas）。Venus では 1 回の同期の呼び出しが数十 ms かかる（F-056）。

## 実装（`userland/desktop/wayland/shm.c`・`compose.c`・`compose.h`）

- `zwl_host_image_batch_begin()`・`zwl_host_image_batch_end()`（新）: `zwl_compose_open` が `compose_objects` の後に begin、glass の後に end。
  その間の `image_layout` は、最初の画像が command buffer を確保・begin し、以後の画像は同じ command buffer に barrier を記録するだけ
  （`image_layout_record`、新）。end が 1 回の submit と 1 回の `vkQueueWaitIdle` と free。
- host の書き込みは barrier の submit の前（PREINITIALIZED の linear image への書き込みは許され、submit が host の書き込みを見えるようにする）。
  最初の frame は `zwl_compose_open` が返った後なので、全ての移行が終わってから sample する。
- 起動の後に作る画像（client の wl_shm の buffer、cursor の形）は今までどおり 1 つずつ submit と wait。

## 検証

**QEMU（amd64、Venus の guest 1920x1280、graphical の login の image。前 `build/p130-after.img`、後 `build/p131-after.img`、2 回ずつ）**:

| `ZWL STARTUP` | 前 | 後 |
| --- | --- | --- |
| vulkan-objects-arrow | 345・347 ms | 266・268 ms |
| wallpaper | 676・690 ms | 502・511 ms |
| glyphs | 285・279 ms | 180・185 ms |
| **compose の計** | **1511・1524 ms** | **1235・1251 ms** |
| login の READY まで（sessiond の `session ready waited_ms`） | 1740・1808 ms | 1515・1523 ms |

- 画面: `build/ws035-shots/p131-20260929-desktop-1920.png`（wallpaper・ぼかし・system bar の印と時計の文字、`zdesktop-check.py` PASS）。

（回帰の結果は下に追記）

## Resume point

2026-09-29: 実装と起動の計測まで。回帰（p126・p052・p053・p128・boot test）を `build/p131-regress.sh` で実行中。
