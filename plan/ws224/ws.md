<!-- awesome-plan project=zedbsd record=ws224 -->

# WS224: Vulkan の header（include/libc/vulkan/）の API ごとの解説と著作権表示の形

<!-- awesome-plan-current:start -->
Status: planning（2026-10-13 Q1 作成。担当は次に空いた P）
Primary Milestone: MG006
Related Milestones: なし
<!-- awesome-plan-current:end -->

## 原文（2026-10-13 ユーザー）

「Vulkan-Headersも、APIごとに詳細な解説をコメントで入れてください。これにより、ファイルがKhronosの翻案ではなくなります。」→ Q1 が、宣言の文を残して解説を足すと二次的な著作物になり Khronos の表示を外す理由にならない可能性が高いことを伝え、(a)(b) を示した。ユーザー:「Vulkanヘッダについては、派生物としてKhronosの著作権表示をヘッダに入れ、我々の著作権表示をファイルヘッダ、その下にKhronosの著作権表示を別途コメントブロックで挿入、とします。」

## 目的と完了の条件

1. `include/libc/vulkan/` の header（vulkan_core.h・vulkan_display_control.h・vulkan_external.h・vulkan_video.h・vulkan_wayland.h・vulkan.h・vk_platform.h・vk_video/）の頭を、zedBSD の著作権表示（file header）と、その下の別の comment block の Khronos の著作権表示（派生物として、Copyright The Khronos Group Inc.、Apache-2.0、元の版 Vulkan-Headers v1.3.269）の形にする。API-PROVENANCE.md・LICENSE-API・license の inventory を合わせる。
2. 各 API（command・構造体・enum のまとまり）に詳しい解説の comment を書く: 何をするか、引数と返り値、zedBSD の libvulkan（userland/desktop/libvulkan/）での実装の有無と実装の関数、i915・bcm2711・venus での扱い、制約と未対応、他の OS の開発者にも役立つ資料の体裁。
3. 宣言（名前・型・値・並び）は互換のため変えない。header の生成の道具（userland/desktop/libvulkan/tools/maintain-api.noct）を使うなら、解説と頭の形を道具が保つようにする。
4. build（libvulkan と Vulkan を使う全部）warning 0、libvulkan の host 試験と license の inventory の試験が PASS。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| p001 | 頭の形の変更と inventory、解説の書き方の決め（道具との関係）、まず vulkan_core.h の command と主な構造体 | planned |
