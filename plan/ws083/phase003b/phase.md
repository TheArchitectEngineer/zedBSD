<!-- awesome-plan project=zedbsd record=ws083-p003b -->

# ws083-p003b: 実行器の video の module、capset の native の語と family 1、video の submit の骨組み

Status: in-progress（q833、P1。2026-10-07 夜 着手）
Disposition: normal
Parent: [WS083](../ws.md)

## 範囲（Q1 の ACK 2026-10-07 夜: 範囲 1〜5、src/kern/boot.c の i915.debug の video・display,video の受け取りの変更も許可（i915 の device.c の照合と一緒に、他の kernel の boot の語は変えない））

[design.md](../design.md) 第 3.1 版 §9 の p003b と、p003a から回した物:

1. 実行器の video の module（`render/video.c`・`video.h` 新）: object kind VIDEO_SESSION・PARAMETERS、dispatch の route（`0x10000`〜`0x10009`、wire は p002 の §4.2）、CMD `0x1000a`〜`0x1000d` は `drv_i915_gfx_rec_dispatch` の範囲に足し gfx op kind VIDEO_*。parameters は D22（1 件ずつ確保、rollback）。
2. capset の 176 byte と native の bit、family 1 は「GT に VCS0 があり boot に i915.debug=video」の時だけ（D19）。`vkGetDeviceQueue2` の family を queue に覚え、submit で queue から family を引く（§3.2 の family の規則、D18）。timeline は 3 以上も RCS0（§4.3）。
3. video の submit: §6.5 の slot の模擬（GPU の前に submit 全体）、D18 は `VK_ERROR_DEVICE_LOST`、§6.6 の D17（飛ばして log）、video 専用の batch と cursor、engine を引数に取る run で VCS0 に、最初の vkCreateVideoSessionKHR で `drv_i915_worker_context_attach`、video の hang で session の quarantine・video の batch の保持、video が死んでいれば create は `VK_ERROR_INITIALIZATION_FAILED`・submit は `VK_ERROR_DEVICE_LOST`。MFX の命令の中身と NV12 の image は p004。
4. kernel の boot の parser: `src/kern/boot.c` の i915.debug に `video` と `display,video`、`device.c` の照合。
5. host 試験: 往復（libvulkan の byte 列を実行器へ）、slot の遷移・D18・family の規則、D17 の境界、`plan/ws031/tests/i915-vk-cmd-test.c` の capset の assert。build（vmunix・libvulkan）warning 0。

UAPI・HAL・toolchain・display/ は触らない。

## 記録

- 2026-10-07 夜 単位 1（boot・capset・family・timeline）:
  - `src/kern/boot.c`: i915.debug= が `video` と `display,video` も受ける（他の語・他の parameter は不変）。`device.c`: 新 `i915_boot_word_listed`（`,` で区切った語の並びの照合）で `display` と `video` を読み、`drv_i915_render_video_request()` に渡し、video の時に log 1 行。i915.start= は今の `i915_boot_word`。
  - `worker.c/h`: 新 `drv_i915_worker_video_state()`（0・ENODEV（worker か VCS0 が無い）・EIO（hang した））。
  - `render/vulkan.c`・`internal.h`・`render.h`: attach で「boot の要求 && VCS0 が使える」なら `vk->video = 1`、その時だけ capset を 176 byte（byte 168 tag `0x5a4e4154`、byte 172 bit 0）と log。門が閉じていれば今の 168 byte。
  - `render/instance.c/h`: queue family を video の時 2 つ（family 1 は `VK_QUEUE_VIDEO_DECODE_BIT_KHR`、queue 1、timestamp 0、granularity 0）、`vkGetDeviceQueue2` が family を読み、family 1 の queue は別の token で覚える（video でない device の family 1 と 2 以上は EINVAL）、新 `drv_i915_render_queue_family()`。
  - `session.c`: `drv_i915_engine_for_timeline` は 0 → copy の record、他の全て → RCS0（3 以上の EINVAL を無くす、§4.3）。
  - 試験: `plan/ws031/tests/i915-vk-cmd-test.c` に門の 3 つの場合（要求だけ・要求と VCS0・hang）、`i915-vk-render-stubs.inc` に `drv_i915_worker_video_state` の stub、新 `plan/ws083/tests/host-boot-video.c`・`run-host-boot-video.sh`。

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws031/tests/run-vk-host-tests.sh "cmd res resdispatch"` | plain・ASan/UBSan PASS |
| `sh plan/ws083/tests/run-host-boot-video.sh` | 10 checks 0 failures |
| `sh plan/ws118/tests/host-boot-i915-test.sh build/tmp/ws083-boot-host`（既存、不変の確認） | 19 checks 0 failures |
| `make -j16 BUILD=build/p1-k ZEDBSD_CONFIG=config/ci/config-amd64.mk build/p1-k/vmunix` | 成功 warning 0 |
