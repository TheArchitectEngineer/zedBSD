<!-- awesome-plan project=zedbsd record=ws068p009 -->

# ws068-p009: EGL の frame を 2 枚重ねる（frame in flight）

Phase ID: `ws068-p009`
Parent: [WS068](../ws.md)
Status: in-progress（2026-10-05 P1 generation17、q747。設計の第 1 版を下に書き、実装へ）
Phase disposition: normal
Queue: q747（P1、ベータ2。p009 → p004 → p007。p037 以降は保留のまま）

## 範囲（ws.md の表から）

frame を 2〜3 枚重ねる（EGL の frame in flight）。Venus で clear だけ 125 ms/frame、WSI 直接は 50 ms（[phase008](../phase008/phase.md) の 87 行:
差は EGL が 1 frame ずつ submit・present・fence を待つ作りによる）。この Phase は **2 枚**にする（3 枚は効果を見てから）。
対象は窓（Wayland）と display 直接の swapchain の surface。pbuffer（WS101 の compute、GLX の描画先）の振る舞いは変えない（回帰で確かめる）。

## 依存

p008（済み）。WS101 と libegl・libglesv2 を共有する（[WS101 guide](../../ws101/guide.md) の 7 章）。新規実装の期間（〜2026-10-10）を過ぎたら main の判断で 10/17 の後へ。

## 手順（2026-10-01 追記）

1. 今の形を読む（読むだけ）:
   - `userland/desktop/libegl/vulkan.c:1162` `vulkan_submit`: submit の後すぐ `vkWaitForFences`（1228 行）。acquire は 466 行。
   - `userland/desktop/libegl/zegl.h:158〜163`: command pool・command buffer・fence・semaphore（acquired・rendered）が surface に 1 組。`frames`（presented の数）は 182 行付近。
   - `userland/desktop/libegl/zegl.h:209` の `frame_done` の callback と、libGLESv2 の `gles_frame_wait`（`userland/desktop/libglesv2/query.c:147`）・
     garbage（`buffer.c:242` `gles_garbage_destroy`、`buffer.c:290` `gles_collect`）・spare（`gles.h:151〜159`）が「どの frame が終わったか」をどう知るか。
2. 設計を phase.md の「設計」に書く（実装の前）: slot を 2 つ（command buffer・fence・acquired・rendered を slot ごと）、submit では待たず、次に同じ slot を使う前に
   その slot の fence を待つ。`frame_done` を「slot の fence が signal した frame」ごとに呼ぶ形。`glReadPixels`・`glFinish`・query・sync（`glFenceSync`）・
   `gles_frame_wait` は対象の frame の fence を待つ。pbuffer は今の形（1 枚、すぐ待つ）を保つか、同じ形で 2 枚にするかを決めて理由を書く。
   設計の後、`design-reviewer` の review（AGENTS.md の運用）を main に頼む。
3. 実装（libegl と、要れば libglesv2 の frame の番号の扱い）。HAL・toolchain には触らない。
4. build と規約:
   ```
   make -j16 ZEDBSD_CONFIG=plan/ws068/tests/config-amd64-glsl.mk BUILD=build/ws068-p009-lib build/ws068-p009-lib/dynamic/libEGL.so build/ws068-p009-lib/dynamic/libGLESv2.so build/ws068-p009-lib/bin/egltest
   python3 plan/tools/style-check.py userland/desktop/libegl/*.c userland/desktop/libegl/zegl.h
   git diff --check
   ```
5. host の回帰:
   ```
   sh plan/ws068/tests/glsl-host/run.sh build/ws068-p009/glsl-host
   sh plan/ws101/tests/gles/run.sh build/ws068-p009/ws101-gles
   ```
6. Venus（image を作り、[guide.md](../guide.md) 5.3 の形で全 egl・glx の試験）:
   ```
   plan/ws068/tests/build-glsl-image.sh build/ws068-p009-glsl > build/ws068-p009/img.log 2>&1; echo "exit=$?"
   export GUEST_RUNTIME=$PWD/build/ws068-p009-run
   sh plan/ws035/tests/zdesktop-guest.sh start build/ws068-p009-glsl/hdd-image.img
   sh plan/ws035/tests/zdesktop-guest.sh wait --timeout 240
   ```
   その上で guide.md 5.3 の for の行（egl-p008〜glx-p033）と x11-p004・x11-p005。効果の計測: 同じ guest で
   `time /bin/egltest --platform=display --frames=300 --delay-ms=0 --token=t`（p009 の前の library と後の library を `plan/tools/guest/guest.py put` で入れ替えて比べる）。
   `sh plan/ws035/tests/zdesktop-guest.sh stop`。
7. WS101 の回帰（compute の pbuffer の道）: WS101 の guide 5.3 の `plan/ws101/tests/gles/build-image.sh` と `venus.sh`（`ws101 venus: PASS`）。
8. 実機（passthrough）: `BUILD=build/ws068-p009-gles plan/ws101/tests/hw/gles-hw.sh build/ws068-p009/gles-hw`（`gles-hw: PASS`）。p038 が出来ていれば `es-hw.sh` も。
9. boot test: `OUTPUT=build/ws068-p009/boot plan/tools/boot-test.sh build/ws068-p009-glsl/hdd-image.img`。PNG をユーザーに見せる。

## 設計（2026-10-05 P1 generation17 の第 1 版。code を読んだ結果）

### 今の形（読んだ所）

- libegl（`userland/desktop/libegl/vulkan.c`）: surface に command buffer・fence・`acquired`・`rendered` が 1 組。`vulkan_submit` は submit・present の後すぐ
  `vkWaitForFences`。`zegl_surface_present` の後に `frame_done`（libGLESv2 の `gles_frame_done`: `state->frame++` と `gles_collect`）。pbuffer も同じ道（present しない）。
- libGLESv2 の「frame」の考え: `state->frame` は記録中の frame の番号で、**番号が進めばそれより前の submit は全て GPU で終わった**、が全体の前提。
  - `gles_collect`（`buffer.c`）: garbage を全部壊す、stream の chunk を先頭の 1 つに戻す、descriptor pool を全部 reset、`set_cache` を消す、古い spare を捨てる。
  - `buffer->used != state->frame` なら device の copy を in place で書き換える（`buffer.c` 370 行、map の 1937 行）。
  - `query_finish`（`query.c` 994 行）・`glClientWaitSync`（1105 行）: `frame < state->frame` なら終わったとみなす。
  - readback（`draw.c` 260 行）・FBO（`framebuffer.c` 720 行）・query は `zegl_frame_flush`（submit して待つ）の後に `state->frame++` と `gles_collect`。
  - `glFinish` は何もしない（「Nothing is running」）。
- GLX（libGL）の描画先は pbuffer（p010）、WS101 の compute も pbuffer。swapchain は `minImageCount` 3。

### 変える形

1. **libegl に slot を 2 つ**（`ZEGL_SLOTS 2`）: slot ごとに command buffer・fence・`acquired`・`rendered`。`surface->command` は今の slot の command buffer を指す
   （libGLESv2 の記録の code はそのまま）。frame N は slot `N % 2`。
2. **present では待たない**: frame N を submit・present した後、**もう一方の slot（frame N−1）の fence** を待ってから返す。CPU は frame N+1 の記録を GPU の frame N の実行と重ねられ、
   重なりは最大 1 frame（2 枚）。`zegl_frame_begin` が使う slot は前の present の終わりで待った物なので、command buffer を reset してよい。
3. **flush は今と同じく待つ**（`zegl_frame_flush`: その submit の fence を待つ。`vkQueueSubmit` の fence の signal は同じ queue で先に submit した物を全て含むので、待った後は全部終わり）。
4. **新しい `zegl_surface_retire(surface)`**: submit 済みの物を全部待つ（両方の slot の fence）。`glFinish`・前の frame を待つ query・sync・map、swapchain の作り直し（今の `vkDeviceWaitIdle` の前）、surface を閉じる時に使う。
5. **pbuffer は 1 枚のまま**（今と同じく submit してすぐ待つ）: GLX と WS101 の compute の振る舞いを変えない。速さの効果は窓と display 直接だけで要る。
6. **libGLESv2 に「GPU で終わった frame」を明示**: `state->done`（終わったと分かっている最後の frame の番号）を足す。`frame_done` に「今の frame も終わったか」の引数を足し、
   present の後（窓・display 直接）は `done = 今の frame − 1`、flush・retire・pbuffer の後は `done = 今の frame` とする。番号の進め方は今と同じ（submit ごとに `state->frame++`）。
   - in place の書き換え（`buffer.c` 370 行）は `buffer->used <= state->done` の時だけ、それ以外は別の device buffer。map（1937 行）は `used > done` なら retire してから。
   - `query_finish`・`glClientWaitSync`: `frame <= done` なら終わり、`done < frame < state->frame` なら retire、`frame == state->frame` なら今の通り flush。
   - `glFinish`: retire。
7. **`gles_collect` を frame の番号つきに**: garbage・stream の chunk・descriptor pool に「使った frame」を付け、`done` 以下の物だけを壊す・戻す・reset する。
   stream と descriptor pool は frame ごとの列（最大 2 列）にし、終わった列を次の frame が使う。`set_cache` は frame が変わるたびに消す（今と同じ）。spare の年は今の通り。
8. depth の image は slot で共有する（同じ queue の上で、render pass の外部の依存（late fragment test の書き込み → 次の early fragment test）で順序を保つ。足りなければ slot ごとに持つ）。
9. libegl と libGLESv2 の間の callback（`struct zegl_gles`）は内部の口で、公開の API・UAPI・HAL は変えない。

### 効果の目標

- Venus で `egltest --platform=display --frames=300 --delay-ms=0` の 1 frame が今（clear だけで約 125 ms）より短い。WSI 直接の 50 ms に近づくのが目標で、
  100 ms 以下を合格の目安にする（Venus の 10 ms 刻み（F-064）に注意）。

### 危険と確かめ

- libGLESv2 の「番号が進めば全部終わった」の前提を使う所を全部直す必要がある（grep の結果: `state->frame` との比較が 4 か所、`gles_collect`、`frame++` が 3 か所、`glFinish`）。
  見落とすと GPU が読んでいる buffer を書き換える（描画の乱れ）。回帰の egl-p008〜p030 と ws101 の venus.sh で確かめる。
- WS101 と libegl・libglesv2 を共有するので、WS101 の host 試験（`plan/ws101/tests/gles/run.sh`）も build の後に流す。
- 運用の変更（2026-10-03）により、Venus・実機の試験は実装の担当が流さず T1 に依頼する（上の手順 6〜9 は T1 への依頼の中身として使う）。

## 完了の条件

- 窓と display 直接で、submit の後に fence を待たず、2 つ目の frame の記録が前の frame の GPU の実行と重なる（設計の通りの code と、計測の前後の数）。
- 計測: Venus で `egltest --platform=display --frames=300 --delay-ms=0` の 1 frame の時間が前より短い（数を記録。目標の数は設計の時に決める。Venus の 10 ms 刻み（F-064）に注意）。実機の数は「未実施」でもよい（理由を書く）。
- 回帰: build の warning 0、style-check の新しい違反 0、glsl-host・ws101 gles host が PASS、Venus の egl-p008〜p030・glx-p013・p031・p033・x11-p004・p005 が全て exit 0、
  ws101 の venus.sh が PASS、gles-hw.sh が PASS（lock が取れなければ「未実施」で uncleared）、boot test が PASS。

## 実装（2026-10-05、P1 generation17、commit 69492884）

| 部分 | 内容 | 場所 |
| --- | --- | --- |
| slot | `ZEGL_SLOTS` 2。slot ごとに command buffer・fence・acquire と render の semaphore・未待ちの印（`pending`）。`surface->command`・`fence`・`acquired`・`rendered` は今の slot の写し（`vulkan_slot_use`）で、libGLESv2 の記録の code は変えない | `userland/desktop/libegl/zegl.h`、`vulkan.c`（`vulkan_frame_objects`・`zegl_surface_close`） |
| present | `vulkan_submit` の present は fence を待たず、slot に印を付けて次の slot へ移り、その slot の前の frame の fence だけを待つ。present が失敗しても slot は移る（submit 済みなので）。flush は今の通りその submit を待ち、全部の印を消す | `vulkan.c`（`vulkan_submit`） |
| 待ち | `zegl_retire`（queue を idle まで待つ）、`zegl_surface_settled`（待たずに fence の状態を聞く）。`eglWaitClient`・`eglWaitGL` は `zegl_retire`。`exports.map` に 2 つを足した | `vulkan.c`、`egl.c`、`exports.map` |
| frame の番号 | `state->done`（GPU で終わった最後の frame）と `state->flight`（frame を走らせたままの window、比べるだけ）。`frame_done(context, finished)`。`gles_frame_finished(state, finished)` が番号を進めて `done` を決め、`gles_collect`。別の surface の frame が走っている時は先に `zegl_retire` | `libglesv2/gles.h`・`gles.c`（`gles_frame_done`）・`buffer.c` |
| 片付け | garbage・stream の chunk・descriptor pool に frame の番号。`gles_collect` は `done` 以下だけを壊す・空にする（空の chunk は 1 つ残す）・reset。`gles_stream` と pool の割り当ては走っている frame の物を使わない | `buffer.c`（`gles_collect`・`gles_stream`・`gles_garbage_keep`）、`draw.c`（`gles_draw_descriptors`） |
| in place の書き換え | `buffer->used <= state->done` の時だけ（`gles_buffer_sync`・`gles_buffer_writable`） | `buffer.c` |
| 待つ API | `query_finish`: `done` 以下は済み、走っている frame は `gles_frame_retire`、記録中は今の通り flush。fence sync の状態は `flight` の surface の `zegl_surface_settled`。`glFinish` は `gles_frame_retire` | `query.c`、`gles.c` |
| query の slot | 返した slot は、それを書いた frame が終わるまで使わない（`written[]`）。前は同じ frame の中で返した slot を upload の queue で reset して使い直していた（記録済みで未 submit の segment と衝突しうる、潜在の不具合） | `query.c` |
| pbuffer | 1 枚のまま（submit してすぐ待つ、`frame_done(context, 1)`） | `vulkan.c` |

## 確認

| 確認 | 結果 |
| --- | --- |
| `make -j16 ZEDBSD_CONFIG=plan/ws068/tests/config-amd64-glsl.mk BUILD=build/ws068-p009-lib …/libEGL.so …/libGLESv2.so …/libGL.so …/bin/egltest` | rc=0、warning 0 |
| `python3 plan/tools/style-check.py userland/desktop/libegl/*.c userland/desktop/libegl/zegl.h userland/desktop/libglesv2/*.c userland/desktop/libglesv2/gles.h` | 指摘 0 |
| `git diff --check` | 問題なし |
| `sh plan/ws068/tests/glsl-host/run.sh build/ws068-p009/glsl-host` | `glsl-host: PASS` |
| `sh plan/ws101/tests/gles/run.sh build/ws068-p009/ws101-gles` | `ws101 gles host test PASS` |
| 比べるための前の library | `build/ws068-p009-base/dynamic/libEGL.so`・`libGLESv2.so`（commit 69492884 の 1 つ前の source） |
| Venus（T1）・WS101 の venus.sh・実機・boot test | **未実施**（T1 に依頼。手順 6〜9 の中身） |

