<!-- awesome-plan project=zedbsd record=ws243-p006 -->

# ws243-p006: GPU の道（Video Player が NVDEC の picture を GPU で描き、1080p60）

Parent: [WS243](../ws.md)、設計 [design.md](../design.md)（D4・I7）
Status: in-progress（2026-10-29 q1677-i02 P5。kernel の側の実装・build・host 試験まで。実機は未実施、10.0.10.3 の枠待ち）
担当: P5（worktree p5-ws243、branch agent/p5-ws243）

## ユーザーの決定（原文、2026-10-29 22:4x、Q1 経由）

「NVDECですが、動画アプリでの再生で、NVDECからGPUバッファに描画して、それをコンポジタが描画する流れにして、1080p60が再生できるようにしてください。CPUメモリはあくまでも機能性テストの1ステップです。」

解釈（Q1）: 目的は動画の app が 1080p60 を再生できること。NVDEC が GPU の buffer へ decode し、app は CPU を通さず（export・共有）に使い、compositor が描く。
受け入れは RTX 2070 での 1080p60 の再生の計測を含む。CPU の readback は機能の試験（hash）の 1 段だけ。host cached な memory type は要らない。

## 設計（P5、2026-10-29）

既にある道を使い、足りないのは nvrtx の kernel の側だけ（libmedia・libvulkan・Video Player は変えない）。

1. **libmedia の GPU の道**（`vkvideo-runtime.c` の `vkvideo_gpu_capabilities`）が求める物: external memory の 2 つの拡張（nvrtx は `GPU_CAP_SHARE` を持つので libvulkan が出す）、
   DPB と出力の一致（nvrtx は COINCIDE を出す）、`vkGetPhysicalDeviceVideoFormatPropertiesKHR` が NV12 に DPB|DST|SAMPLED の usage と
   MUTABLE_FORMAT|EXTENDED_USAGE の create flag を出すこと。これで libmedia は decode の image を export（OPAQUE_FD）し、readback をしない。
2. **Video Player**（`present.c` の `present_import_make`）: fd を import し、NV12 の image（SAMPLED、同じ flag）に bind、PLANE_0 を R8_UNORM、
   PLANE_1 を R8G8_UNORM の view にして自分の shader で YUV → RGB を描き、自分の窓（swapchain）へ。compositor はその窓を合成する。CPU は画素に触れない。
3. **nvrtx の変更**:
   - `vk-video.c`: video の format の usage に SAMPLED、create flag に MUTABLE_FORMAT|EXTENDED_USAGE。
   - `vk-device.c`: NV12 の optimal の feature に SAMPLED_IMAGE。
   - `vk-resource.c`: NV12 の image の usage に SAMPLED を許す。video の image の view は NV12（decoder の物）か、PLANE_0 の R8_UNORM（plane 1）・
     PLANE_1 の R8G8_UNORM（plane 2）。`struct nvrtx_vk_view` に `plane`。
   - `exec-push.c`: `nvrtx_exec_texture_header_plane`（block linear、block は 1 GOB 幅・**2 GOB 高**（decoder の書く形、copy engine の 0x1010 と同じ）、
     1 level、component sizes R8 = 0x1d・G8R8 = 0x18、無い channel は 0、alpha は 1、view の swizzle を通す。値は Mesa 25.0.7 の clb097tex.h）。
   - `vk-execute.c`（`execute_texture`）: video の image は plane の view だけ sample する。luma は plane_offset[0] に width×height、chroma は
     plane_offset[1] に width/2×height/2。block の行は decoder の pitch（align(width, 64)）と同じになる（R8 は 1 byte、R8G8 は 2 byte）。
4. **同期**: libmedia は decode の fence の後に picture を渡す（readback の道と同じ順）。app の push は texture の cache の無効化から始まる（既存）。
5. **確かめ**: host 試験（texture header の field）、実機で Video Player の `PRESENT gpu_pictures=`、libmedia の「pictures the GPU holds」、
   `FRAMES shown/late`、画面の PNG、1080p60 の clip。readback の道の hash（V1）は機能の試験として同じ回に流す。

## 実装（2026-10-29 22:4x）

- `src/drivers/gpu/nvrtx/vk/{nvrtx-vk-objects.h,nvrtx-exec.h,exec-push.c,vk-execute.c,vk-resource.c,vk-device.c,vk-video.c}`（上の 3）。
- 試験: `plan/ws243/tests/plane-header-host-test.{c,sh}`（新）。`plan/ws243/tests/rtx-video-check.sh` に V4（Video Player を全画面で 12 秒、clip ごと、
  6 秒目の画面、PRESENT・GPU holds・FRAMES・DONE・pictures の行）、V1 の上限 300 秒。
- clip（git に入れない）: `build/ws243/clips/h264-high-1080p60.mp4`（`/home/awe/Video.mp4` を 4 回つなぎ各 frame を 1/60 秒に、libx264 High、B 2、crf 20、596 枚、
  `ffmpeg -stream_loop 3 -i Video.mp4 -an -vf "setpts=N/60/TB" -r 60 -c:v libx264 -profile:v high -bf 2 -crf 20 -pix_fmt yuv420p`）と `.ref`（nv12-hashes.py）。
- Video Player: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws243 build/ws243/bin/videoplayer`（warning 0）。

## 確かめた事（host）

- `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws243 build/ws243/vmunix` → rc 0、warning 0、`amd64 vmunix check: PASS`（cksum 1788951379）。
- `plan/ws243/tests/plane-header-host-test.sh` → 40 checks, 0 failed（1920x1088・1280x720・720x480 の両 plane: 512 byte の位置、R8・G8R8、source、
  address、block linear、block 1x2x1 GOB、大きさ、2D 1 level、行の幅 = decoder の pitch。swizzle）。
- 既存: `nvdec-host-test.sh` 88/0、`wire-host-test.sh` 32/0、`nvrtx-vk-host-test.sh` 86/0、`nvrtx-vk-d3-host-test.sh` 209/0。

## 未実施・残り

- 実機: `PERF_BUILD=build/ws243 sh plan/ws243/tests/rtx-p004.sh OUT build/ws243/vmunix RESTORE video hang restore`（V1 の hash に 1080p60 も入る、V4 が GPU の道）。
  見るもの: V4 の `PRESENT … gpu_pictures=1`、「pictures the GPU holds」、1080p60 の `FRAMES shown` が 12 秒で約 596（clip の全部）・`late` が小さい、画面の PNG が動画。
- 起こりうる問題: 別の open（Video Player）の session での import した memory の GPU の address、NVDEC の書きと texture の cache、BL の sample の形。
- 1080p60 で足りない時の次の手は実機の結果を見て決める。
