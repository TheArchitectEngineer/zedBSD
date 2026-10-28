<!-- awesome-plan project=zedbsd record=ws282-p002 -->

# ws282-p002 camera の枠組み（/dev/camN）

- Parent: [ws282](../ws.md)、Queue: q1667 / q1667-i01（P6 の配下、実装だけ・試験なし） → q1667-i03（P6、branch agent/p6-ws282b、基点 1190b629c）
- Status: in-progress（枠組みと host 試験の source は実装済み・試験未実施、branch agent/p6-ws282b、先頭 f30258b62。残りは「再開点」）。2026-10-29 01:5x、P6 の指示（優先度の調整）で途中で止めた → 2026-10-29 q1667-i03 で続き。

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

## 2026-10-29 P6（q1667-i03、branch agent/p6-ws282b、基点 1190b629c）

- 5ff18cf2e: `platform/arm64/vmunix.mk`・`platform/riscv64/vmunix.mk` に cam.c（amd64 と同じ条件、`CONFIG_DRIVER_CAMERA` か `CONFIG_DRIVER_USB_UVC` が y の時）。build: `make -j16 ZEDBSD_CONFIG=config/ci/config-<amd64|rpi5|visionfive2>.mk BUILD=build/ws282/p6b-<名> build/ws282/p6b-<名>/vmunix` が 3 つとも rc 0・warning 0、cam.o が 3 つとも出来た（amd64・arm64 は vmunix check PASS）。rpi4 は未 build。
- f30258b62: `plan/ws282/tests/cam-host-test.c`（約 1,300 行）と `run-cam-host-test.sh`。plan/ws035/tests/audio-framework.c の fixture の形（kernel の関数の stub、cdev の registry、0x5a で埋めた pmem、device mapping の記録）に、偽の source（YUYV と MJPEG の 640x480、幅 640 超は ENOSPC、start・stop の数）。case: GET_INFO と record の version/size の拒否、ENUM_FORMAT・ENUM_MODE・ENUM_INTERVAL、ENOTTY、所有（2 つ目の open の SET_FORMAT・REQUEST_BUFFERS が EBUSY、所有者が BUFFERS の時の TRY_FORMAT が EBUSY、REQUEST_BUFFERS 0 で手放す）、ENOSPC の payload_bytes、確保直後の buffer が 0、mmap_offset の式、mmap の EACCES・EINVAL・EBUSY、mapping の残る間の REQUEST_BUFFERS が EBUSY、STREAM_ON の EINVAL・EBUSY・start の失敗、QUEUE_BUFFER の重複、O_NONBLOCK の EAGAIN、timeout の ETIMEDOUT、EINTR、frame で起きる DEQUEUE、metadata、枯渇の drop と sequence の飛び、discard、非所有者の poll、STREAM_OFF で EINVAL と未使用への戻し、FAULT（POLLERR、完了の frame を先に返してから EIO、STREAM_OFF で回復）、MJPEG の used_bytes と capacity での切り詰め、close の後も mapping が残る間は stream が続き munmap で止まる事、退役（進行中の ioctl を待つ・stop 1 回・ENODEV・POLLERR|POLLHUP・退役の後の close が stop を呼ばない）、最後に memory・page・mapping の leak 0。uapi/camera.h を -m32・-m64 で compile（`_Static_assert` の layout）。
- 確かめた事: `sh plan/ws282/tests/run-cam-host-test.sh --compile-only`（host の cc、-Werror、通常と ASan/UBSan の 2 つ）が rc 0・warning 0、layout の compile が通った。**試験は流していない**（指示）。
- 流せば落ちると読んだ所（試験で確かめる事、直していない）: (1) design §7.1 の「close の後も mapping が残る間は stream が続き munmap で止まる」は、今の cam.c では close の時に `cam_stream_stop` を呼ぶので `case_close_with_mapping` が落ちるはず。(2) 同じ所で、mapping が device の参照を持たないので、mapping が残ったまま退役して最後の close が来ると `cam_device_release` が buffer の page を解放し、user の mapping が解放済みの page を指す。どちらも p002 の範囲の実装の直し（mapping に device の参照を持たせ、最後の munmap で stop と解放）が要る。
- 規約: cam-host-test.c は関数の定義の形・頭の comment・宣言の位置まで当てたが、全文の見直しは p011。cam.c の規約の全文の見直しも未実施（p011）。

## 再開点（2026-10-29 P6、q1667-i03）

1. 上の (1)(2) の cam.c の直し（mapping が device の参照を持つ、owner の close は mapping が残る間は stream を止めない、最後の munmap で stop と解放）。
2. p003 から（下の元の再開点の 3〜4）。rpi4 の config での build も。

