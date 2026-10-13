<!-- awesome-plan project=zedbsd record=ws014 -->

# WS014: virtio-gpu bring-up

<!-- awesome-plan-current:start -->
Status: closed（2026-10-13 ユーザー「WS014はクローズします。」。旧: incomplete、残りは p004 の最終 API と規約の全文だけで実装の残りは無かった）
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2, O4
Parent: [Master](../master.md)
Queue: なし
<!-- awesome-plan-current:end -->

## 目的

QEMU の virtio-gpu の上で zedBSD の GPU・表示の経路を成り立たせ、宣言した描画・表示の API（標準の Vulkan と `VK_KHR_display`・swapchain）から画面へ present できるようにする（2026-09-12 ユーザーの指示で初期の対象を i915 から QEMU の virtio-gpu に変えた）。i915 の実機、GLES2、desktop 全体の移植はこの WS の外。

## 結果（2026-10-13 close）

- 設計（p001）: 層の分け方、Vulkan の表示 API の案（Vulkan 1.0〜1.4 の core 234 と表示の拡張 41 の計 275 関数の U（user 空間）／K（GPU driver）の責務表）、K 側の操作を `struct drv_gpu_interface` の callback にする案。後続の p002〜p011 が実装したので Q1 が 2026-10-08 に cleared と判定。
- GPU の framework（p002）: `drv_gpu_register`・`drv_gpu_unregister` の動的な ops の登録、`/dev/gpuN`、session と世代つきの handle、cdev・devfs の動的化。
- QEMU＋Venus（p003）: 遠隔の QEMU＋Venus の loop と API の不足の修正、2D の scanout と Venus の画面の取得。
- 標準の Vulkan（p005）: WS030 の標準の library で 3D の shader・texture・depth、回転、再 open、console の所有権を実測。
- GPU の共有と最小の Wayland（p006）: kernel の handle、別 process への GPU の allocation の共有、Wayland の WSI と全画面の表示。
- p007〜p010: BLOB の直の表示、標準の external memory・fence の fd、同期と転送、topology と配置、GPU の完了の責任と fence の所属、GPU の待機・期限・所有権の共通化、backend の停止の契約、Venus から framework への移管、session の隔離。いずれも実 QEMU で受け入れ。
- p011（BUG-144・BUG-124）: amd64 の HAL の device の窓 2 GiB・Venus の上限 1 GiB・試験の hostmem 1G、ENOSPC を OUT_OF_DEVICE_MEMORY に。2026-10-03 ユーザーの判断で両 bug を resolved。
- GPU の core・Venus・libvulkan・libwayland の source は main にあり、後続の WS（WS030・WS031・WS035・WS099・WS113・WS141 など）が使っている。

## 制限・残り

- p004（p010 の後の最終の framework・API の整理と、変えた source の規約の全文の確認）は未実施のまま canceled。規約の整形はベータ3 の扱い（2026-10-08 ユーザー「コーディング規約による整形はベータ3」）。必要なら Future Work か、GPU の API の整理の別の WS の候補（勝手に移していない）。
- Vulkan の表示の API を OS の公式の ABI として凍結するかは未決定のまま。
- `plan/ws014/tests/` の host の fixture（`gpu-framework.c`・`gpu-test-fd.c`・`gpu-scanout.c`・`handle-fd.c`）と QEMU の道具（`venus_rfb.py`・`run-venus-remote.py`・`venus-qemu.py`・`vkdemo_oracle.py`）は他の WS の試験と source の注・README が使っている。削除の前に `plan/tools/` へ移すかを Q1 が決める（2026-10-13 の close の時点では残す）。

## Phase

| Phase | 内容 | 最終の状態 |
| --- | --- | --- |
| ws014-p001 | 設計の判断（層、Vulkan の表示 API の案、最小の OS の ABI、QEMU の環境） | cleared（2026-10-08 Q1 の判定） |
| ws014-p002 | GPU の framework だけ（動的な ops の登録、`/dev/gpuN`、handle） | cleared（q304・q305、2026-09-12） |
| ws014-p003 | QEMU＋Venus の loop と API の改善 | cleared（q306、2026-09-13） |
| ws014-p005 | 標準の API の 3D の shader・API の検証 | cleared（q307・q308、2026-09-13） |
| ws014-p006 | kernel の handle・GPU の共有・最小の Wayland | cleared（q309、2026-09-13） |
| ws014-p007 | GPU の review・BLOB の直の表示・同期と性能 | cleared（q310、2026-09-13） |
| ws014-p008 | GPU の完了の責任・fence の所属と描画の資源 | cleared（q311、2026-09-13） |
| ws014-p009 | GPU の review の対応と framework の共通化 | cleared（q312、2026-09-13） |
| ws014-p010 | GPU の監督の共通化の仕上げと局所の隔離 | cleared（q313、2026-09-14） |
| ws014-p011 | BUG-144: Model viewer の 8 個目の vkAllocateMemory -4 の測定と上限 | cleared（2026-10-03 Q1、ユーザーの判断） |
| ws014-p004 | p010 の後の最終 API の整理と規約の全文の確認 | canceled（2026-10-13、ユーザーの指示で WS を close。旧: planning） |

Phase の directory とこの WS だけの試験は、完了の規則（AGENTS.md）で削除する（git の履歴に残る、削除は Q1）。他から使われる試験の道具は上の「制限・残り」。
