<!-- awesome-plan project=zedbsd record=ws269-p010k1 -->

# ws269-p010k1: WSI の readback の道（clear だけ、compiler なし）

- ID: ws269-p010k1（親 [ws269](../ws.md)）
- Status: in-progress（q1386-i01 は userland の側。実機の確かめは P1 の q1385（transfer の job・vk-transfer・vk-queue）の merge の後）
- Disposition: normal
- Queue: q1386 / q1386-i01（2026-10-20 P2）
- 設計: [design-p010.md](../design-p010.md) 版 2.2 の §7・§8.2
- 依存: O-p010f（transfer の copy・clear。前半は main、job への組み込みは P1 の q1385）、O-P007（display の node）

## 受け入れ条件

OPTIMAL の swapchain の image → `vkCmdCopyImageToBuffer` → host visible の buffer を map → display の node に CPU で present、semaphore・fence。gpu-selftest の `present-clear`（`vkstage present`、ClearColorImage を swapchain の image へ、3 秒 present）がこの道を通る。実機: 単色の画面。

## 調べた事（2026-10-20 P2）

libvulkan の WSI の readback の道は既にある（`userland/desktop/libvulkan/wsi-swapchain.c`）。PowerVR で通る順:

1. `swapchain_create`: 先に shared（native の image）の道を試す。p010l までは executor が linear の SAMPLED を出さず（`POWERVR_FORMAT_LINEAR_SAMPLED` 0）、external の chain・export・import は ENOTSUP（FEATURE_NOT_PRESENT）なので、`swapchain_native_images_free` の後に `swapchain_readback_create`（copied の道）に落ちる。
2. `swapchain_image_create`: swapchain の image は **OPTIMAL**、usage は client の物 ＋ TRANSFER_SRC。executor の format の表は 4 つの 8 bit RGBA で OPTIMAL に COLOR_ATTACHMENT・TRANSFER_SRC・TRANSFER_DST を出している（`image-state.c` の FEATURES_RENDER_COLOR）ので受ける。vk 側の features の変更は要らなかった。
3. `swapchain_readback_create`: 幅 × 高さ × 4 byte の詰めた行の buffer（TRANSFER_DST）を HOST_VISIBLE | HOST_COHERENT の memory に作り map する（executor は 2 つの memory type を出す）。
4. `present_copy`: barrier（executor は記録しない）、`vkCmdCopyImageToBuffer`（OPTIMAL＝twiddled の読み、P1 の transfer の job）、buffer の barrier。submit は present の wait semaphore を待つ。終わってから map した buffer を display の node に CPU で写す（`surface->platform->present`）。
5. display の node と PowerVR の renderer の対: `wsi-display-nodes.c` の `inventory_create` が、display だけの node（dc8200、`GPU_DEVICE_DISPLAY`）を最初の renderer（PowerVR、`GPU_DEVICE_RENDER`）に対にする。

## 変えた物（2026-10-20 P2）

- `userland/tests/vkstage/main.c` の `present`: acquire の semaphore → ClearColorImage の submit（acquire を待ち、clear の semaphore と fence を signal）→ present（clear の semaphore を待つ）、**3 秒**出す（`VKSTAGE_PRESENT_SECONDS`、design §8.2）。PASS の行に秒を出す。semaphore の destroy を足した。
- `userland/tests/gpu-selftest/main.c`: `present-clear` の説明（3 秒、readback の道）。段の動きは変わらない。
- `src/drivers/gpu/powervr/vk-pass.c`: decode する render area を memset で初期化（gcc -O1 の maybe-uninitialized）。`plan/ws269/tests/p009/run.sh` の `-Wno-maybe-uninitialized` を外した。

## 確認（2026-10-20）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| executor の host 試験（gcc -O1、-Wno-maybe-uninitialized なし） | `sh plan/ws269/tests/p009/run.sh OUT` | `powervr executor host test: 165 passed, 0 failed` |
| vkstage（host の llvmpipe） | `cc ... userland/tests/vkstage/main.c -lvulkan`、`clear`・`draw`・`present` | clear・draw PASS。present は host に display が無く `FAIL, no display -3`（期待どおり） |
| gpu-selftest の host 試験 | `sh plan/ws269/tests/p010/selftest-host.sh OUT` | 0 failed |
| VF2 の試験の image | `timeout 3600 make ZEDBSD_CONFIG=plan/ws269/tests/p010/config-visionfive2-selftest.mk BUILD=build/p010k1/img disk-image -j16` | rc=0、vkstage・gpu-selftest・libvulkan・powervr に warning 0、`rootfs/bin/vkstage` あり |

- 未実施: 実機（VF2 の HDMI の単色の画面、gpu-selftest の `present-clear: ok`）。readback の道を通る present の host 試験（libvulkan の WSI を fake の device で回す試験は無く、作らなかった）。
- 実機の前提: P1 の q1385 の transfer の job（ClearColorImage・CopyImageToBuffer）と vk-queue の submit が main に入ること。

## 残り

- 実機（T1）: VF2 の selftest の image で `gpu-selftest: present-clear: ok` と画面の単色（clear の色 0.25, 0.5, 0.75）を見る。依頼文: 「`make ZEDBSD_CONFIG=plan/ws269/tests/p010/config-visionfive2-selftest.mk SELFTEST_FIRMWARE=powervr-firmware BUILD=build/<dir> disk-image` の image を VF2 に書いて起動し、HDMI に 3 秒の単色（RGB ≒ 64, 128, 191）が出ること、console の `gpu-selftest: present-clear: ok` を写真で。」（P1 の q1385 の merge の後）。
