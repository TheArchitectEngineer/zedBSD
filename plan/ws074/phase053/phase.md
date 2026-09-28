<!-- awesome-plan project=zedbsd record=ws074p053 -->

# ws074-p053: 部品としての browser の設計（engine と shell の依存の棚卸しと C API の案）

Phase ID: `ws074-p053`（2026-09-28 main が割り当て）
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p021

## 出典

2026-09-28 ユーザー（design.md §19）: browser を将来 `libbrowser.so` に分けられる部品にする。handle は不透明な構造体への pointer、
呼ぶ側が Vulkan の描画の先を渡して event を送ると描く、Wayland に依存しない。一度に作り直さず、徐々に移す。main の指示: この Phase は
棚卸しと C API の header の案（plan/ws074/ に）。.so の分割はまだしない。境界を越える小さな依存は触ったところで直す。

## 範囲

1. engine（`base/`・`vm/`・`js/`・`html/`・`dom/`・`css/`・`text/`・`layout/`・`paint/`・`image/`・`net/`・`bind/`・`page/`）と
   shell（`shell/`・`main.c`）の間の依存の棚卸し。
2. C API の案: [browser_view.h](browser_view.h)（build しない下書き。`cc -std=c89 -pedantic -fsyntax-only` で構文を確かめた）。
3. 小さな境界の直し: shell と main が `bind/`・`net/` に直接触れていた所を page の API に（`page_set_viewport`・
   `page_set_console`・`page_failure_reason`）。shell.c は `net/net.h` を include しなくなった。

## 棚卸し（2026-09-28 の source）

**engine から shell・Wayland への依存: 無い。** engine の file は `shell/`・`wayland-client.h`・`keiland.h`・`xdg-shell` を
include しない（`paint/gpu.h` の `vulkan/vulkan.h` だけ。これは部品の API でも要る）。engine は標準出力に書かない（診断の
`ZBROWSER` の行は shell と main だけ）。p021 の `image/`・`page/images.c` も境界を守る。

**shell・main から engine への依存（部品の API に置き換える所）:**

| 今の依存 | どこ | 部品の API での形 |
| --- | --- | --- |
| `struct page` の field を直接読む（`page->layout.document_height`・`page->paint`・`page->text`・`page->base`） | shell.c（scroll の上限、描画、題名の path）、main.c（dump・描画） | view の内部に隠す。scroll は engine が持つ（下）。描画は `browser_view_draw`・`_draw_pixels` |
| page の作成・読み込み・layout・paint・timer を shell が順に呼ぶ（`page_create`・`page_load_location`・`page_open_fonts`・`page_layout`・`page_paint`・`page_set_time`・`page_next_timer`・`page_settle`・`page_needs_layout`） | shell.c・main.c | `browser_view_create`・`_load_url`・`_resize`・`_process`・`_deadline`。layout と paint は engine が必要なときに自分で行い、`redraw` の callback で知らせる |
| scroll の位置と量（`scroll_y`、矢印・Page・Home・End・wheel の key の解釈） | shell.c | engine が持つ（browser の既定の動作）。shell は `browser_view_wheel`・`_key` を送るだけ |
| click と link の追従（`page_click` の後に `page_link_at`、`shell_follow`） | shell.c | engine が click の既定の動作として行い、`navigate` の callback で呼ぶ側が許すか決める |
| 履歴（history の配列、back・forward） | shell.c | engine の session history（`browser_view_go`、`history` の callback）。JS の `history` とも共有する |
| 題名（`page_title`）と場所（`page->base`） | shell.c | `title`・`location` の callback と `browser_view_title`・`_url` |
| 読み込みの失敗の理由（`net_tls_error`） | shell.c・main.c | **直した**: `page_failure_reason`。API では `load` の callback の reason |
| window の大きさを scripts に（`bind_window_set_viewport`） | shell.c・main.c | **直した**: `page_set_viewport`。API では `browser_view_resize` |
| console（`page->console` の代入） | shell.c・main.c | **直した**: `page_set_console`。API では `console` の callback |
| GPU の描画: `paint_gpu_open`（instance・device・format・final layout）と `paint_gpu_draw`（renderer の pass に作った framebuffer、自分で submit して fence を待つ） | shell/present.c | `browser_gpu` と `browser_target`（VkImage・view・format・layout）。framebuffer と pass は view の内部で image view ごとに作る。submit を自分でする形と、呼ぶ側の command buffer に記録する形（`_record`）。今の fence の待ちは部品では呼ぶ側の frame を止めるので、`_record` を主にする |
| GC の stack の base（`__builtin_frame_address(0)` を `page_create` へ） | shell.c・main.c | `browser_view_options.stack_base`（NULL なら thread の stack の範囲を engine が取る）。1 つの view は作った thread だけで使う |
| 入力の形（evdev の key code と button、Wayland の座標） | shell/window.c | DOM の key・code の名前と text、pointer は view の pixel の座標。evdev からの変換は shell に残る |
| 待ち（shell の `poll` と timer の期限） | shell.c | `browser_view_poll_fds`・`_deadline`・`_process`（p050 の非同期の loader が fd を持つようになったら中身が入る） |

**process 全体の状態（view ごとにできないもの）:** `net/cookie.c` の cookie jar（`cookie_jar`、全 view で共有、本来は profile ごと）、
`net/tls.c` の OpenSSL の library・context・CA の file・理由の文字列（process で 1 つ。理由は view ごとの load の結果に移す）、
`js/builtin_math.c` の `Math.random` の状態（realm ごとにする）、`image/decode.c` の bitmap の serial（process で一意のままでよい）。
いずれも 1 thread で使う前提。複数の thread で view を使うのは範囲外。

## API の案の要点（[browser_view.h](browser_view.h)）

- handle `struct browser_view *`（不透明）。作成の options に version・font・GPU（NULL なら CPU だけ）・callback・stack の base・
  大きさと scale。
- 読み込み（URL・HTML の文字列と base URL）、reload・stop・履歴の移動、大きさの変更、題名と URL。
- main loop は呼ぶ側のもの: engine は block せず、poll する fd と期限を返し、`_process` で進む。
- 描画: 呼ぶ側の VkImage へ submit するか、呼ぶ側の command buffer に記録する。CPU の描画は pixel の配列へ（試験と headless）。
- 入力: pointer（移動・button・離れる）、wheel、key（DOM の key・code・text・押す・離す・repeat）、focus。既定の動作（scroll、link、
  form）は engine が行い、scripts が取り消せる。
- process 全体の設定（CA の file）は view の外の関数。

## 移し方（2026-09-28 main が番号を割り当て: 1 → p054、2 → p055、3 → p056、4 → p050 の一部、5 → p057）

1. view の型を作り、page と今の shell の state のうち engine の持つべきもの（scroll、履歴、timer の epoch、題名の変化の検出）を
   その中へ移す。shell は view の API だけを呼ぶ。main の headless の mode も同じ API に（CPU の描画、dump）。
2. GPU の描画を `browser_target` の形に（view 内の image view ごとの framebuffer、`_record` と `_draw`）。present.c は swapchain と
   同期だけを持つ。
3. 入力を DOM の key の形に（evdev からの変換を shell に）、既定の動作を engine へ。
4. p050（非同期の loader）で `_poll_fds`・`_deadline`・`_process` に中身を入れる。
5. `libbrowser.so` への分割、公開の header（`include/libc/` の適所）、2 つ目の使い手の試作（例 System Settings の小さな窓）。

## 確認

- header の案: `cc -std=c89 -pedantic -Wall -Wextra -Werror -fsyntax-only` 通過、style-check 0。
- 境界の直し: host の build、golden の dump 28/28、guest の image の build と窓の試験・boot test（下の結果）。

## 結果（2026-09-28）

cleared。結果の数は ws.md の Resume point と main への報告にも書いた。

- 棚卸しと API の案は上のとおり。engine は既に Wayland・shell に依存していない。shell・main から engine への依存 13 種を表にした。
- 境界の直し（3 つの関数）の後: host の build、golden の dump 28/28、amd64 の image の build（warning 0）。Venus の guest で
  `browser-p045.sh` status 0（titlebar・link・履歴・location）、`browser-p030.sh` status 0（scripts の console の CONSOLE の行、
  timer、click）。boot test PASS（`/home/awe/zedBSD-rpi4/build/ws074-shots/p053-20260928-boot-login.png`）。
- style-check: `browser_view.h`、`page/page.c`・`page.h`、`shell/shell.c`、`main.c` 0。
