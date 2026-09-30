<!-- awesome-plan project=zedbsd record=ws103 -->

# WS103: compositor を libvulkan だけにする（GPU の UAPI の直の ioctl を無くす）

<!-- awesome-plan-current:start -->
Status: incomplete（p001・p002 cleared）
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2
Parent: [Master](../master.md)
Queue: q510（ws103-p003）
Resume point: 2026-09-30 夜 p002 cleared（起動の問い合わせを VK_KHR_display へ、`--direct` の削除。QEMU と 5330 の passthrough で確かめた）。次は独立の p003（libvulkan の import の照合）・p005（WSI の fence）、その後 p004。実行は Queue の承認の後
<!-- awesome-plan-current:end -->

## 目標（2026-09-30 ユーザー）

ユーザー:「私はKeilandコンポジターがlibvulkanのみを使用していると思っていたのですが、ioctlを使ってしまっているのですか？」→ Q1 が残っている直の ioctl を
説明し、「規則にして今移す」を選んだ。規則は [Guardrail](../guardrail.md)。

## 背景: Linux と FreeBSD への移植（2026-09-30 ユーザー）

ユーザー:「Keilandデスクトップ一式を、LinuxとFreeBSDでも動くようにしようと思っているからです。」→ WS103 の設計（p001）は、Linux・FreeBSD の
Mesa の Vulkan でも同じ code が動く形を前提にする。zedBSD の libvulkan に独自の拡張を足して逃げず、Mesa が持つ標準の拡張
（VK_KHR_display、VK_KHR_external_memory_fd と VK_EXT_external_memory_dma_buf・VK_EXT_image_drm_format_modifier、VK_KHR_external_fence_fd・
VK_KHR_external_semaphore_fd、VK_EXT_acquire_drm_display など）の範囲で設計し、zedBSD の libvulkan に足りない物はその標準の拡張として足す。

2026-09-30 夜の更新: Linux・FreeBSD の構成はユーザーの決定で [F-065](../future/F-065-keiland-portable.md) に記録した。app は我々の WSI を持つ libvulkan を使い、後段の system の libvulkan（Mesa やベンダーの物）に chain する（後段の WSI は使わない）。
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

## 決定（2026-09-30 夜 ユーザー）

| # | 判断 | 決定 |
| --- | --- | --- |
| D1 | 範囲 | **案 A**: zedBSD の上で GPU の直の ioctl を全て消し、buffer・fence の OS 固有の部分（記述の型 `gpu_image_descriptor`、OPAQUE_FD、fence の世代）を OS の backend の境界の後ろ（zedBSD の macro か module）に閉じ込める。Linux・FreeBSD の backend は作らない（[F-065](../future/F-065-keiland-portable.md)） |
| D2 | `--direct` と、Vulkan が開けないときにそれへ落ちる道 | **消す**。`GPU_DISPLAY_CLAIM`・`PRESENT`・`RELEASE` と `schedule_direct`・`zwl_present`・`zwl_unscan` が無くなる。Vulkan が開けなければ compositor は起動を失敗させる（greeter と同じ）。Q1 の説明（WS014 p006 の合成しない最初の compositor の名残、repository に `--direct` を渡すものは無い）の後に決定 |
| D3 | buffer の記述の安全の確かめ（V3） | **libvulkan の中へ**: import した memory に image を bind するとき、libvulkan が kernel の記述と image の作り方（幅・高さ・形式・stride）を照らす。compositor は ioctl を持たない |

## 調べた事（2026-09-30 夜 Q1、コードの読み）

- window mode の表示は既に Vulkan だけ（VK_KHR_display と swapchain）。画面の claim・release は libvulkan の swapchain の作成・破棄の中（`libvulkan/wsi-display.c`、`wsi-swapchain.c`）。compositor の CLAIM・PRESENT・RELEASE は `--direct` の道だけ。
- `GPU_GET_INFO`・`DISPLAY_QUERY`・`DISPLAY_MODE` は起動時に 1 回、大きさと refresh を得るだけ（`display.c:55-91`）。VK_KHR_display の問い合わせで代わる。
- `RESOURCE_IMPORT` は buffer ごとの記述の照合（`display.c:124-129`、`memcmp`）。libvulkan の OPAQUE_FD の import（`libvulkan/memory.c:820-900`）も内部で同じ ioctl を呼ぶが、確かめるのは memory type と大きさだけ。
- `FENCE_QUERY` は commit のたびに世代の照合（`display.c:316`、`protocol.c:1645`）。`vkImportFenceFdKHR`（OPAQUE_FD）と `vkGetFenceStatus` で代われるが、世代は標準の Vulkan から見えない。i915 の native で external fence が出るか（`GPU_CAP_FENCE`）は未確認。
- UAPI の型は `zwl.h` の構造体（`struct gpu_resource_import`、`struct gpu_display_info`、`lease`）と `keiland_gpu_buffer_v1` の wire（64 byte の記述）に入っている。

## 達成基準

| # | 基準 | 確かめ方 |
| --- | --- | --- |
| V1 | GPU の直の ioctl は Vulkan の API（zedBSD では libvulkan）へ移す。OS 固有の部分は OS ごとの source の module（zedBSD は `gpu-zedbsd.c`）に閉じ、それ以外の `userland/desktop/wayland/` の source は GPU の UAPI を include せず、GPU の ioctl を呼ばない。compositor は GPU の fd を持たない。evdev の ioctl は対象の外（2026-09-30 ユーザーの決め。module の入れ替えへの言い回しの改訂は 2026-09-30 夜 ユーザー「書き直してOKです」） | `plan/ws103/tests/v1-check.sh`（grep と、GPU の UAPI の header を `#error` にした `-fsyntax-only`。design §2.6）と、build・試験 |
| V2 | 起動・login・Log Out・Shut Down（WS099 の C1）、app の起動と窓の操作、全画面、keyboard が今と同じに動く | C1・C9・WS079-p010・boot test（QEMU の Venus）、5330 の passthrough |
| V3 | 安全の確かめ（buffer の記述と実物の一致、他の client の画像を読めない）が libvulkan の import の側で保たれる | host か guest の試験（偽の記述の buffer を拒む） |
| V4 | 性能が落ちない（C6 と app の最初の frame、Venus の client の present と compositor の frame の間隔）。p002 の前（基準）と p006 の後を比べる（言い回しの改訂は同上） | WS075 の measure-apps と WS099 の import-launch、QEMU の Venus と 5330 の passthrough（design §2.7） |

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [ws103-p001](phase001/phase.md) | 調査と設計（[design.md](design.md)） | cleared（q508） | — |
| [ws103-p002](phase002/phase.md) | compositor: 起動の問い合わせを VK_KHR_display へ、`--direct` の削除（design §2.1・2.2） | uncleared（q509: 5330 の smoke が未達、変更の前も同じく失敗。QEMU の C1・C2・boot test は PASS） | — |
| [ws103-p003](phase003/phase.md) | libvulkan: dedicated allocation と import の記述の照合、bind の守り（§2.3） | in-progress（q510） | — |
| ws103-p004 | compositor: dedicated の import、wire の値の確かめ、`RESOURCE_IMPORT`・`DESTROY` を消す、`gpu-zedbsd.c` の buffer の部分（§2.3・2.5） | planned | p002、p003 |
| ws103-p005 | libvulkan: WSI が Wayland の present ごとに新しい fence を送る（§2.4） | planned | — |
| ws103-p006 | compositor: fence を poll だけに、`/dev/gpu0` と `--gpu` を消す、UAPI の型を閉じる、`v1-check.sh`（§2.4〜2.6） | planned | p004、p005 |
| ws103-p007 | 規約の全文、回帰、5330、V4 の計測（§3） | planned | p002〜p006 |
