<!-- awesome-plan project=zedbsd record=ws083-p003b -->

# ws083-p003b: 実行器の video の module、capset の native の語と family 1、video の submit の骨組み

Status: cleared（2026-10-07 Q1 の判定: §9 の受け入れ（§8.1 の 2・3 行目: libvulkan の video.c の実際の byte 列の往復の試験 PASS（ASan/UBSan）、build warning 0）。MFX の命令と NV12 は p004）（旧: in-progress（q833、P1。2026-10-07 夜 着手））
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

- 2026-10-07 夜 単位 2（video の module の本体、往復の試験の前）:
  - `render/video.c`・`video.h`（新）: 物理の問い合わせ 3 つ（caps は要求の形の順に入れ子で返す、format は NV12 の 1 件、family の codec ops）、session（profile・family 1・flags 0・NV12・4096・slot 17・参照 16・header の名と版を検べ、最初の session で `drv_i915_worker_context_attach` を device の mutex の下で、hang した engine では INITIALIZATION_FAILED）、memory requirements（row store 4 本と MV buffer slot ＋1 本、4 KiB）、bind（全部か無し、memory の identity で持つ）、parameters（SPS・PPS を 1 件ずつ確保、create は template を写して同じ key を置き換え、update は持っている key・update 内の重複・容量超え・順番違いを全体で拒む）、記録（op ごとに record を確保、溢れは oversize）、submit（模擬 → 実行の 2 回の walk。D18 は EBADMSG → `VK_ERROR_DEVICE_LOST`、D17 は理由を log して飛ばし slot は動かす）、session の close での解放（hang の時は batch を保持）。
  - `command.c`: 記録の範囲に `0x1000a`〜`0x1000d`、op の record を begin・reset・free で解放（`i915_command_ops_clear`）、submit は queue の family で分け family 1 は `drv_i915_video_submit`（render の batch は query reset のために開く）、graphics の family の video の op は EIO（DEVICE_LOST）。`dispatch.c` の route（`0x10000`〜`0x10009`）、`objects.c` の close の解放、`gfx.h` の op kind 4 つと `u.video`、`internal.h` の object kind 2 つ、`platform/amd64/vmunix.mk` に video.c。
  - 未: MFX の命令（p004）。p003b の decode は検べと遷移だけで batch を書かず、VCS0 で何も走らない。NV12 の image（p004）が無いので D17 の 3・6・7 の image の検べは今は必ず「NV12 でない」で飛ばす。
  - 試験: `plan/ws031/tests/run-vk-host-tests.sh` の executor に video、stub に `drv_i915_worker_context_attach`、cmd の試験に `drv_i915_video_dispatch` の stub。全 10 個 PASS（plain・ASan/UBSan）。`make … vmunix` warning 0。

- 2026-10-07 夜 単位 3（engine を引数に取る run・quarantine・往復の試験）:
  - `render/draw.c/h`: 新 `drv_i915_gfx_batch_run(session, batch, va, engine)`（batch の終わり・barrier・その engine の session の context で run_sync）。`drv_i915_gfx_flush` は RCS0 でこれを使う（動きは同じ）。
  - `render/video.c`: session に batch の cursor、新 `i915_video_run`（空なら何もしない。VCS0 で走らせ ETIMEDOUT・EIO なら session を irq lock の下で quarantine、log、EIO → submit は DEVICE_LOST）。decode の検べの後に呼ぶ。MFX を書くのは p004 なので今は走らない。
  - 試験（新）: `plan/ws083/tests/host-video-wire.c`（libvulkan の video.c で全部の stream を file に書く）・`host-video-executor.c`（その file を実行器に流す）・`run-host-video-roundtrip.sh`。確かめる事: capset 176 byte と native の語、caps の入れ子の返事の各 field、format（NV12・optimal・usage）、queue family 2 つと token、session の作成と VCS0 の attach 1 回、memory requirements 8 本（各 4 KiB）、bind、parameters の作成と update、同じ update の 2 回目の拒否、submit: reset → 参照の IDR → P（全部 decode、skip 無し）、graphics の family で同じ物は DEVICE_LOST、非参照の P が残した inactive な slot 1 を読む decode は DEVICE_LOST、slot 0 を読む decode は成功、start code の無い 2 つ目の slice は skip（成功）、NV12 でない出力は skip、出力と layout の違う参照は skip、begin で picture 無しの slot 0 は無効になり読むと DEVICE_LOST、終わらない scope は DEVICE_LOST、hang した engine では DEVICE_LOST・戻れば成功、destroy、close で leak 0。

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws083/tests/run-host-video-roundtrip.sh` | libvulkan 側（gnu89・ASan/UBSan）で stream を書き、実行器側 plain・ASan/UBSan とも PASS |
| `sh plan/ws031/tests/run-vk-host-tests.sh` | 10 個とも plain・ASan/UBSan PASS |
| `sh plan/ws083/tests/run-host-libvulkan-video.sh` | PASS（p002 の試験、不変） |
| `make -j16 BUILD=build/p1-k ZEDBSD_CONFIG=config/ci/config-amd64.mk build/p1-k/vmunix` | 成功 warning 0 |

## 受け入れ（design §9 の p003b）と状態

- §8.1 の 2 行目（往復）・3 行目（D17 の検べ）: host で PASS。D17 は 5（start code）・6（参照の layout）・NV12 の検べを試した。1・2・4（parameter の値）・3（MB の数）・7（整列）・8・9 は読みと build（境界ごとの試験は SPS の値を変えた stream が要る、p004 の builder の試験と一緒に足す）。
- build warning 0。
- 残り（p004）: NV12 の image（Tile Y の layout、vkGetImageSubresourceLayout の PLANE_0/1）、format feature（D23）、MFX の命令の builder と batch の作成、§8.2 の QEMU 回帰（T1）。
- 状態: 実装と host 試験は済み。cleared の判定は Q1。
