<!-- awesome-plan project=zedbsd record=ws103 -->

# WS103: compositor を libvulkan だけにする（GPU の UAPI の直の ioctl を無くす）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: 2026-09-30 に立てた。**始める時期はユーザーが指示する**（2026-09-30 ユーザー「この標準Vulkan化の作業は、タイミングを見て実行を指示させてください。WSとして作成しておいてください。」）。指示の後は p001（調査と設計、High）から。2026-09-30 夜 ユーザーが最優先の WS にした（master の優先順位の節）。実行の指示（Queue の承認）はまだ
<!-- awesome-plan-current:end -->

## 目標（2026-09-30 ユーザー）

ユーザー:「私はKeilandコンポジターがlibvulkanのみを使用していると思っていたのですが、ioctlを使ってしまっているのですか？」→ Q1 が残っている直の ioctl を
説明し、「規則にして今移す」を選んだ。規則は [Guardrail](../guardrail.md)。

## 背景: Linux と FreeBSD への移植（2026-09-30 ユーザー）

ユーザー:「Keilandデスクトップ一式を、LinuxとFreeBSDでも動くようにしようと思っているからです。」→ WS103 の設計（p001）は、Linux・FreeBSD の
Mesa の Vulkan でも同じ code が動く形を前提にする。zedBSD の libvulkan に独自の拡張を足して逃げず、Mesa が持つ標準の拡張
（VK_KHR_display、VK_KHR_external_memory_fd と VK_EXT_external_memory_dma_buf・VK_EXT_image_drm_format_modifier、VK_KHR_external_fence_fd・
VK_KHR_external_semaphore_fd、VK_EXT_acquire_drm_display など）の範囲で設計し、zedBSD の libvulkan に足りない物はその標準の拡張として足す。

2026-09-30 夜の更新: Linux・FreeBSD の構成はユーザーの決定で [F-065](../future/F-065-keiland-portable.md) に記録した。app は我々の WSI を持つ libvulkan を使い、後段の Mesa に chain する（Mesa の WSI は使わない）。
client と compositor の間は全 OS で我々の独自の protocol 1 本で、運ぶ中身（zedBSD は kernel handle、Linux・FreeBSD は dma-buf と sync_file）だけが OS で変わる。
このため上の「Mesa の標準の拡張の範囲」は、compositor の buffer・fence の受け側を OS の backend の境界の後ろに置く、という形で p001 の設計に反映する（p001 で見直す）。

## 今の直の ioctl（2026-09-30 main の調べ）

| 何のために | ioctl | 場所 |
| --- | --- | --- |
| 起動時の表示の情報とモード | `GPU_GET_INFO`・`GPU_DISPLAY_QUERY`・`GPU_DISPLAY_MODE` | display.c `zwl_gpu_open` |
| app の buffer の記述を kernel の値で確かめる | `GPU_RESOURCE_IMPORT`・`GPU_RESOURCE_DESTROY` | display.c `zwl_gpu_import`（protocol.c から buffer ごと）・objects.c |
| app の fence が済んだかを待たずに確かめる | `GPU_FENCE_QUERY` | display.c `zwl_fence_ready`・protocol.c |
| greeter と session の間の表示の受け渡し | `GPU_DISPLAY_CLAIM`・`GPU_DISPLAY_RELEASE` | display.c `claim_display`・`zwl_unscan`（handoff.c・objects.c・main.c） |
| Vulkan の device が無いときの予備の表示 | `GPU_DISPLAY_PRESENT` | display.c `schedule_direct`・`zwl_present` |

入力（evdev）の ioctl は対象の外。全画面モード（WS035 の D0）は ws099-p015 で消したので、直の表示を持つ理由はもう無い。

## 達成基準（案）

| # | 基準 | 確かめ方 |
| --- | --- | --- |
| V1 | GPU の直の ioctl はできる限り Vulkan の API（zedBSD では libvulkan）へ移す。移せない物だけを zedBSD の時だけ build される macro で囲み、macro を外した build（Linux・FreeBSD を想定）では `userland/desktop/wayland/` に GPU の UAPI の include と ioctl が 0。evdev の ioctl は対象の外（2026-09-30 ユーザーの決め） | grep、macro の有り・無しの両方の build と、有りの build の試験 |
| V2 | 起動・login・Log Out・Shut Down（WS099 の C1）、app の起動と窓の操作、全画面、keyboard が今と同じに動く | C1・C9・WS079-p010・boot test（QEMU の Venus）、5330 の passthrough |
| V3 | 安全の確かめ（buffer の記述と実物の一致、他の client の画像を読めない）が libvulkan の import の側で保たれる | host か guest の試験（偽の記述の buffer を拒む） |
| V4 | 性能が落ちない（C6 と app の最初の frame）。macro を有効にした build と無効にした build の差を測り、差の無い直の ioctl は macro で残さず消す | WS075 の measure-apps と WS099 の import-launch |

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| ws103-p001 | 調査と設計: libvulkan（i915・Venus の両方の経路）が持つ拡張（VK_KHR_display の問い合わせ、VK_KHR_external_memory_fd の import での記述の確かめ、VK_KHR_external_fence_fd、display の取得と解放と process の間の受け渡し）を調べ、足りない物を libvulkan に足す設計。Vulkan の無い予備の経路を消してよいかの確認。各 ioctl を「Vulkan の API へ移す」と「移せない（zedBSD の macro で囲む）」に分け、理由を書く。Phase の分け方 | planned | — |
| ws103-p002 以降 | p001 で決める（例: 起動の問い合わせ → fence → import の確かめ → 表示の受け渡し → 予備の経路の削除 → 試験と 5330） | planning | p001 |
