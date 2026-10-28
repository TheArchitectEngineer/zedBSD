<!-- awesome-plan project=zedbsd record=ws282-p002 -->

# ws282-p002 camera の枠組み（/dev/camN）

- Parent: [ws282](../ws.md)、Queue: q1667 / q1667-i01（P6 の配下、実装だけ・試験なし）
- Status: in-progress（一部の実装済み・試験未実施、branch agent/p6-ws282、SHA 0eea3815d）。2026-10-29 01:5x、P6 の指示（優先度の調整）で途中で止めた。

## 実装した事と file（SHA 0eea3815d）

- `include/uapi/camera.h`: ABI（group `'C'`、ioctl 1〜18、format・capability・flag の定数、全 record の layout と `_Static_assert`）。host の cc で -m64・-m32 とも compile が通る。
- `include/drivers/camera/cam.h`: 枠組みの公開 API（design §2.1・§2.2）。設計からの足し: source の関数表に `host_payload_max`（GET_INFO の `host_payload_max` を source が答える口。design §3.2 の値を得る所が §2.2 に無かったので足した）。
- `src/drivers/camera/cam.c`: cdev の登録と番号（cam0〜3、dev_t 0x00170000 + N）、退役（GONE → mutex の下で stop を 1 回 → admission が 0 まで待つ → cdev の unregister）、admission（ioctl・mmap・close・open）、所有（SET_FORMAT・REQUEST_BUFFERS、他の open の TRY は BUFFERS 以降 EBUSY、所有者の TRY も streaming 中は EBUSY）、format の中継と `ENOSPC` の答えの copyout、0 埋めの buffer（`kern_pmem_alloc` + `kern_memset`、page 揃え）、cached の mmap（1 buffer の中、読みだけ、mapping が残る間は REQUEST_BUFFERS が EBUSY）、slot の API（frame_begin・take・done・discard・dropped・fault・counters_changed）、queue（空き列・完了列）、DEQUEUE（timeout_ms・O_NONBLOCK・EINTR・ETIMEDOUT・EINVAL・EIO・ENODEV）、poll（所有者だけ POLLIN、FAULT は POLLERR、退役は POLLERR|POLLHUP）、counters（sequence の飛びを数える）、状態機械（IDLE・BUFFERS・STREAMING・STOPPING・FAULT）、STREAM_OFF で全 buffer を未使用に。
- 設計と違う判断（範囲の中の技術の判断）: STREAM_ON は source の `start` の前に STREAMING にする（start の中で worker が take できるように）。start が失敗したら BUFFERS に戻し、ACTIVE の slot を空き列に戻す。DEQUEUE は FAULT でも完了列の frame を先に返し、無くなってから EIO。POLLPRI は出さず `drv_cam_source_counters_changed` は `poll_notify` だけ。`CAM_MEMORY_IMPORT` は `EOPNOTSUPP`（p008）、DMA の source は `ENOTSUP`。
- build: `Makefile` に `CONFIG_DRIVER_USB_UVC ?= n`（uvc の source が無いので今は n。uvc を書いたら y に）と `CONFIG_DRIVER_CAMERA ?= y`、`-DCONFIG_DRIVER_USB_UVC`・`-DCONFIG_DRIVER_CAMERA`。`platform/amd64/vmunix.mk` に cam.c（CAMERA か UVC が y の時）。
- userland（補助の担当が書いた。p004b の一部）: `userland/tests/camcommon/camcommon.{c,h}`（MJPEG の DHT の補い（ITU-T T.81 Annex K.3 の表、出典の comment 付き）、YUYV → RGB（BT.601/709、限定/全 range）、PPM の書き出し、/dev/camN の info・format・mode・interval・counters の表示）。**未レビュー**（規約の全文と表の値の照合は未実施、DHT の表は数の合計だけ手で確かめた）。`CAM_ENUM_INTERVAL` の `index` は「列全体の位置（8 から）」として使っている — kernel 側も source の enum_interval に index をそのまま渡すので、uvc の実装でこの意味に揃える事。

## build のコマンドと結果

- worktree に `build/llvm` が無いので、main の checkout の clang（`/home/awe/zedBSD-claude1/build/llvm/bin/clang`、読むだけ）で、`make -n ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws282/amd64 build/ws282/amd64/kern64/src/drivers/camera/cam.o` が出す compile の行（`-Wall -Wextra -Werror`、`--sysroot` を外した）をそのまま流した: rc 0、warning 0（log の `warning:` を数えた）。log は `build/ws282/logs/cam-compile.log`。
- `camcommon.c` は host の cc（`-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -I. -Iinclude`）で compile だけ: rc 0、warning 0。zedBSD の sysroot での build は未実施。
- 未実施: vmunix の link（`$(BUILD)/vmunix`）、rpi5 と vf2 の config での build、arm64・riscv64 の vmunix.mk への登録（未着手）。

## 書いたが流していない試験

- 無し（`cam-host-test` は未着手）。

## 飛ばした物と足りない物

- 無し（止めたのは優先度の調整による。設計の穴で飛ばした物は無い）。

## 再開点

1. `plan/ws282/tests/cam-host-test.c` と script（偽の source と kernel の関数の stub、design §7.1 の case）。
2. arm64・riscv64 の vmunix.mk に cam.c、rpi5・vf2 の config で build。
3. p003: `include/drivers/usb/usb-uvc.h`・`usb-uvc-desc.h`、`src/drivers/usb/usb-uvc.c`・`usb-uvc-desc.c`、PROBE/COMMIT、5 か所の登録（design §4.8）、`config/drivers/usb.drivers`・`menu.list` に UVC、`CONFIG_DRIVER_USB_UVC` を y。payload の組み立ては host で試験できるよう `usb-uvc-payload.c` に分ける案（stream.c は USB の API に依る）。
4. p004a・p004b、`userland/tests/camcap/`（main.c と package の Makefile、`plan/ws282/tests/config.mk` で `ZEDBSD_USER_PROGRAMS += camcap`）。
