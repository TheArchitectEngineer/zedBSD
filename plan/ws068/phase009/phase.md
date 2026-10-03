<!-- awesome-plan project=zedbsd record=ws068p009 -->

# ws068-p009: EGL の frame を 2 枚重ねる（frame in flight）

Phase ID: `ws068-p009`
Parent: [WS068](../ws.md)
Status: planned（2026-10-01 に phase.md を作った。範囲は ws.md の表の行のまま）
Phase disposition: normal
Queue: なし

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

## 完了の条件

- 窓と display 直接で、submit の後に fence を待たず、2 つ目の frame の記録が前の frame の GPU の実行と重なる（設計の通りの code と、計測の前後の数）。
- 計測: Venus で `egltest --platform=display --frames=300 --delay-ms=0` の 1 frame の時間が前より短い（数を記録。目標の数は設計の時に決める。Venus の 10 ms 刻み（F-064）に注意）。実機の数は「未実施」でもよい（理由を書く）。
- 回帰: build の warning 0、style-check の新しい違反 0、glsl-host・ws101 gles host が PASS、Venus の egl-p008〜p030・glx-p013・p031・p033・x11-p004・p005 が全て exit 0、
  ws101 の venus.sh が PASS、gles-hw.sh が PASS（lock が取れなければ「未実施」で uncleared）、boot test が PASS。

## 確認

未実施。
