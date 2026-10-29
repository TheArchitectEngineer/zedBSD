<!-- awesome-plan project=zedbsd record=ws243-p006 -->

# ws243-p006: GPU の道（Video Player が NVDEC の picture を GPU で描き、1080p60）

Parent: [WS243](../ws.md)、設計 [design.md](../design.md)（D4・I7）
Status: cleared（2026-10-30 q1677-i02 P5。RTX 2070 の実機で GPU の道の Video Player 1080p60（584/596、late 12）、hash 4 本一致、hang の非退行、restore）
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

- 2026-10-29 23:3x（e4a019432 の後）: Video Player の shader（picture.vert・picture.frag・overlay.frag）が nvrtx の SM75 の compiler で通る:
  `plan/tools/nvrtx/nvrtx-sm75-desktop-test.sh --sm 75` → 62 compiled, 0 refused, 0 disagreed（picture.frag 56 命令、IR と 200 入力で一致）。
- 実機の vmunix は e4a019432 の `build/ws243/vmunix`（cksum 1672525563、p005 の書き方の直しと video_parameters_create の直しを含む）。
- 10.0.10.3 は 2026-10-29 23:2x のユーザーの決定で P3 の 10GbE の作業が終わるまで P3 のもの。空いたら Q1 が知らせる。
- 実機の後の受け入れ（WS の完了）: V1 の hash（720p30・1080p30・1080p60・1080p24 が software と一致）、V4 の Video Player が 4 本とも
  `gpu_pictures=1`・「pictures the GPU holds」・`FAILED` 無し、1080p60 で `FRAMES shown` が clip の 596 枚近く・`late` が小さい、画面の PNG に動画。
  hang の非退行と restore（NVMe の kernel は vmunix-va1 1463007375 に戻す）。

## 実機の debug（2026-10-30 q1677-i02 P5、10.0.10.3 の RTX 2070、lock 00:19〜）

worktree に main（b2a94051d）を merge（74169045c、競合無し）。分かった事と直し（設計の訂正、計画に無かった依存）:

1. **Video Player が amd64 で GPU の道を使っていなかった**: `videoplayer.h` の `VP_GPU_PRESENT` が `__aarch64__`（Pi 4）だけ 1 で、amd64 は libkeiland の CPU の描きと
   readback の picture（1080p60 で 30 枚/12 秒）。設計の「Video Player は変えない」は誤り。→ `VP_GPU_PRESENT` を全ての arch で 1（presenter は import できない device では
   CPU で拡縮した picture を出す、Pi で使っている道）。Q1 の承認（2026-10-30 00:4x、要約）: amd64 でも VP_GPU_PRESENT=1 で進める。ユーザーの決定が動画 app での GPU の道の
   再生なので videoplayer は WS243 の範囲、ws.md に設計の訂正として記録、amd64 の Venus（QEMU）で再生を 1 回確かめる、i915・amdrdna の実機は未実施と書く。i915・amdrdna の実機では流していない（下の「未実施」）。
2. **export の識別の op が無かった**: libvulkan の `vkGetMemoryFdKHR`・import が送る `GPU_OP_GET_PHYSICAL_DEVICE_PROPERTIES2`（device・driver の UUID）を nvrtx の
   executor が拒み device lost。→ `vk-device.c` に応答（mali と同じ形、`device_identity`）。
3. **allocation の共有の能力が無かった**: nvrtx の `NODE_CAPABILITIES` に `GPU_CAP_ALLOCATION_SHARE` が無く export が FEATURE_NOT_PRESENT。→ `node.c` に足した
   （`share_export`・`share_import` は blob ならそのまま使える）。
4. **開始の止まり（main の 2.4 s）が blob の VRAM の消し**: 一時の log（commit しない）で実測、nvrtx の blob の作成の消し（register window の PRAMIN を 4 byte ずつ）が
   約 31.7 MB/s（8294400 byte で 254〜266 ms、16588800 byte で 508 ms、3133440 byte で 99 ms、`build/ws243/p006-hw2/tmp-clear.txt`）。全画面への resize の 8 MB の
   blob 6 つで約 1.6 s。readback の道の Video Player も同じく 3 s 止まっていた。BUG-353（WS309、glass maps の作り直しが大きさに比例して遅い）と同じ原因。
   → 256 KiB 以上の blob を driver の work channel の copy engine の fill で消す（`work.c` の `nvrtx_work_clear`: 1 page の push を map、blob の VRAM を map、
   128 KiB の行の多行の fill（`objects.c` の `nvrtx_push_copy_clear`、Mesa nvk の fill と同じく PITCH_IN・PITCH_OUT の両方）、fence を 2 s 待って unmap）。
   driver の channel の copy object は nvrtx.video の有無に関わらず作る。Q1 の承認（2026-10-30 01:1x、要約）: copy engine の fill で実装する。driver の
   work channel の copy object は nvrtx.video に関わらず常に作り、作れない時は PRAMIN。nvrtx の memory の core の普通の技術の判断として WS243 の範囲で扱う。
   実機で hang の非退行、1080p60 の止まりと late の減り、できれば BUG-353 の glass の時間（video=0）を確かめ、ws.md と BUG-353 に記録。最初の版は PITCH_IN が無く CE0_ERROR（RC type 39）になった（実機で 1 回、PITCH_IN を足して直った）。
5. **最適化の失敗で系が落ちない**: Q1 の指示（2026-10-30 01:2x、要約）: 消しの失敗で driver の channel が止まり app が開けない状態は
   直す。CE error・fence の時間切れの後は CE の消しを使わず PRAMIN に戻す。driver の work channel は WS250 の回復の道で戻す（できなければ少なくとも app の作成が
   壊れないように）。この最適化の失敗で系が落ちないように作り、できれば fault の注入で実機で 1 回確かめる。→ submit の失敗・fence の時間切れで
   `clear_off` を立て以後は CPU の消し。時間切れの VRAM は返さず（GPU がまだ書くかもしれない）、blob は別の VRAM を CPU で消して作成は成功。video の可否は起動時の
   copy object（`work.copy_ready`）で見る（driver の channel が後で recovery に止められても video は消えない）。試験用の boot の行 `nvrtx.clear.fault=1` で最初の
   消しの PITCH_IN を 0 にする。driver の work channel の作り直し（再 open）は WS250 の recovery にも無い（stop・free だけ）ので実装していない。

- build: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws243 build/ws243/vmunix` → rc 0、warning 0、`amd64 vmunix check: PASS`、cksum 3454572979（6a1c18de0）。
  Video Player `build/ws243/bin/videoplayer` cksum 1717266091（warning 0）。
- host: `plan/ws243/tests/copy-clear-host-test.sh`（新）8/0、`plan/tools/nvrtx/nvrtx-gsp-host-test.sh` 393/0、`nvrtx-vk-host-test.sh` 86/0、`nvrtx-vk-d3-host-test.sh` 209/0。

### 実機の結果（QEMU ではない）

- fault の注入（vmunix 3454572979、`nvrtx.clear.fault=1` を一時に足し、後で外した）: 最初の消し（compositor の 8100 KiB）の PITCH_IN が 0 → CE0_ERROR（type 39、chid 4）→
  recovery が chid 4 を stop・free（17 ms）→ 消しは fence の時間切れ（EBUSY、8100 KiB は保持）→「the CPU clears from now on」。desktop は生き、Video Player は開いて
  GPU の道（gpu_pictures=1）で再生（PRAMIN に戻ったので開始の止まりも戻り late 143）。
- 本番 `PERF_BUILD=build/ws243 sh plan/ws243/tests/rtx-p004.sh build/ws243/p006-hw3 build/ws243/vmunix build/ws243/p006-restore/vmunix-nvme video hang restore`
  （01:27〜、RESTORE は NVMe の元の kernel を vmunix.old から写し cksum 1463007375 を確かめた物）:
  - V0 nvdec-channel ok（c4b0）、copy object 4・refused 0。
  - V1 hash 4 本 MATCH: 1080p30 150/150、1080p60 596/596、720p30 90/90、1080p24 149/149（open_us は 839 ms → 101〜144 ms）。
  - V2（readback の道、機能の試験）3.5 fps。V3 mview 59.87 fps（単独）・60.21 fps（decode と並行）、hitches 0。
  - V4 Video Player 全画面 4 本とも `PRESENT own=1 gpu_pictures=1`・「pictures the GPU holds」・read-back 0・FAILED 無し・PRESENTED gpu=1:
    1080p60 shown=584/596 late=12（decode 120 枚/2 s、copy 0）、1080p30 148/150 late 2、720p30 88/90 late 2、1080p24 148/149 late 1。
    比べ: 直す前（00:5x）の 1080p60 は shown=448 late=148（開始の止まり 2.4 s）、readback の道は 30 枚。
  - 全画面の V4 の keiland-shot は 4 本とも 0 byte（全画面の時は compositor が合成せずに出す道で、撮れない模様）。画面の PNG は窓の再生で撮る（下）。

- 窓（1280x720）の画面の PNG（`VIDEO_STEPS=v4 V4_OPTIONS="--width=1280 --height=720" V4_CLIP=h264-high-1080p60 … rtx-p004.sh build/ws243/p006-hw4 … video restore`）:
  shot 6222418 byte に Video Player の窓と動画（P5 の worktree の `build/review/ws243-p006-videoplayer-window-1080p60.png`）、shown 543/596・late 53（合成の道）。
- hang（本番の hang の段、video=0）: S0 49.6 fps・S2 lost 2003 ms→control ok・S3 5/5・S4 2/2・S5 ended・control ok（after=4074）、vram clear の行 0。restore: 1463007375、Debian。
- S5 の control の 4 s の調べ（Q1 が退行を疑った）: `plan/ws243/tests/rtx-s5-check.sh build/ws243/p006-s5b build/ws243/vmunix "build/ws243/vmunix@nvrtx.clear.cpu=1"`
  （kernel 1831541809、新しい boot の行 `nvrtx.clear.cpu=1` で CE の消しを切る）: CE ありもなしも S5 は kill 0.05/0.5/1.5 s とも lost 1・stop 2.4〜3.9 s・control after=4300〜4305 ms、
  S2 は lost 2053/2054 ms・control ok。WS250 の記録（phase004 の r3「control ok（4301 ms、GR の停滞の後ろ）」）と同じで、退行ではない。p004 の run1h の after=6 は
  kill が止まる draw の前に当たった回（lost・stop の行が無い）。
- Venus（QEMU、amd64、`plan/ws243/tests/config-amd64-venus-video.mk` の image、`venus-videoplayer.sh`）: 手で確かめた run（02:04）で `PRESENT own=1 gpu_pictures=1` → Vulkan Video
  無し → `MEDIA backend=libavcodec`、`PRESENTED … gpu=0`、FAILED 0、窓に動画（shown 45/90、lavapipe の CPU で遅い）。script の記録の run は下。

## 未実施・残り

- i915（5330）・amdrdna の実機の Video Player（amd64 で自前の presenter の道になった）。
- BUG-353 の glass maps の作り直しの時間（video=0、`plan/ws309/tests/rtx-bug353-pair.sh`）。
- 最後の kernel 1831541809（`nvrtx.clear.cpu=1` の行を足した物）の V1・V4 は流していない（3454572979 との差は boot の行の読みだけ。S5・S2・T は 1831541809 で流した）。
