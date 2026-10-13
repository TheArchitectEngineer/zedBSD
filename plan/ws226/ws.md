<!-- awesome-plan project=zedbsd record=ws226 -->

# WS226: Khronos の Vulkan 1.3 の header をそのまま使い、libvulkan で Vulkan 1.3 の API を実装しきる

<!-- awesome-plan-current:start -->
Status: planning（2026-10-13 Q1 作成。担当は次に空いた P、まず調べと設計）
Primary Milestone: MG006
Related Milestones: なし
<!-- awesome-plan-current:end -->

## 原文（2026-10-13 ユーザー）

「じゃあ、Vulkan 1.3のKhronosヘッダをそのまま使って、APIをlibvulkanで実装しきることに変更します。WSを作ります。Video extensionが1.0のまま使えるなど、不整合があったわけですし、きちんと整理し直します。」
「さきほどの、独自VulkanヘッダにするWSはブロッキングのままです。」（WS225 は blocked のまま）

（経緯: 同じ日、zedBSD の `include/libc/vulkan/vulkan_core.h` は Khronos の Vulkan-Headers v1.3.269 から Vulkan 1.0 の 137 command と WSI の 18 command を選んで整形した物（18,582 行 → 5,720 行、612 → 155 の command、独自の API は無し）と Q1 が確かめた。WS224 で zedBSD の表示の下に Khronos の表示を別の block で入れた。）

## 目的と完了の条件（案、p001 で確定）

1. `include/libc/vulkan/` を Khronos の Vulkan-Headers（Vulkan 1.3 の版、p001 で版を固定）の file をそのまま（改変なし）入れた形にする。表示は Khronos のままで、zedBSD の表示は付けない（そのまま写した file のため）。zedBSD だけの物（独自の拡張・wire の header など）は別の file に分ける。header の取得・検証・更新の手順（版と sha256 の固定、AGENTS.md の外部 package の規則に倣う）と API-PROVENANCE.md・LICENSE-API・license の inventory を合わせる。WS224 の選択と整形の header はこれで置き換わる。
2. libvulkan（`userland/desktop/libvulkan/`）が Vulkan 1.3 の core の全部の command（1.1・1.2・1.3 で core に入った物を含む）を実装し、`vkEnumerateInstanceVersion`・`apiVersion` で 1.3 を名乗る。device ごとに足りない feature は Vulkan の規則（feature・limit・format の報告）で正しく示す。
3. 不整合を整理する: 1.0 を名乗りながら 1.1 以降の機能や extension を前提にしている所（例: Vulkan Video の extension が 1.0 のまま使える）、extension と core の版の関係、`vkGetInstanceProcAddr`・`vkGetDeviceProcAddr` が返す関数の範囲、promoted な extension の扱いを Vulkan の仕様どおりにする。
4. 対象の device: i915（5320・5330）、bcm2711（Pi 4）、venus（QEMU）。各 device が 1.3 の必須の feature を満たすか、満たさない時の扱い（1.3 を名乗れない device は 1.x を名乗る、など）を p001 で決める。
5. 試験: Vulkan の conformance の試験（Khronos の VK-GL-CTS の該当の部分、license を確かめて取得・build する、使えなければ代わりの試験）と、zedBSD の app（compositor・libkeiland・Video・vkdemo など）の回帰。build の warning 0。
6. WS225（仕様から書き直した独自の header、blocked）との関係: WS225 は blocked のまま。WS226 の後に WS225 を始める時は、この WS の 1.3 の API の全部を入力にする。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| p001 | 調べと設計: 今の libvulkan の実装の範囲と 1.3 との差の一覧、不整合の一覧、device ごとの 1.3 の必須の feature、header の取り込みの形、conformance の試験の入手、段の分け → design-reviewer | planned |
