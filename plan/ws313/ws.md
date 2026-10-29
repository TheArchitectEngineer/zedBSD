<!-- awesome-plan project=zedbsd record=ws313 -->

# WS313: ODROID-C5（Amlogic S905X5M）の BSP と Mali の GPU で、起動して desktop を表示する

Status: planning
Primary Milestone: MG008
Resume point: p001（調べと設計）。担当は P7（q1675）。

## ユーザーの指示（原文、2026-10-29）

「WSを立てます。P7の作業として開始します。ODROID-C5基板が届きました。基板を見たところ、SoC＋メモリ＋LAN/USB/AudioのドライバICくらいしかついていないです。amlogicのこのSoCに対応してほしいです。BSPを追加して、ODROID-C5というボードにします。Amlogic S905X5MがSoCです。BSPはbsp-s905x5m/でいいと思います。情報としては、正しいかわかりませんが、EthernetのMACはDesignWare MAC、PHYはAmlogic Internal MII PHY (rmii?)だそうです。あと、Mali G31のドライバ(Valhalla)も必要です。これがでかいですね。起動してデスクトップが表示できるところまでをゴールにして、WSを作ります。」

## ユーザーの指示（原文、2026-10-29、進め方）

「WS313の進め方は、Linuxを参考に、カーネル、ドライバを揃えておいてください。実機テストなしでコードだけ書いて揃えておいてください。今日の夜、まとめてテストします。」

- 解釈（Q1 経由）: p001 の調べと設計は短く（起動の道・console・各 block の register と出典・GPU の世代・Phase の表）、design-reviewer は 1 回で、すぐ実装へ。
  実機の試験なしで、起動と console・割り込み・timer・SMP・MMC・USB・Ethernet・display・GPU（Mali と Vulkan）・desktop の config まで code を書いて揃える。
  確かめは build（warning 0）と、書ける所の host の試験・QEMU の `virt` での起動まで。各 Phase は「実装済み・実機未試験」と記録し cleared にしない。
  夜の実機の試験の手順（媒体への書き込み・UART の配線・確かめる順・判定）を phase.md と `plan/ws313/tests/` の script にまとめる。

## 目標（Q1 の解釈）

- 新しい board「ODROID-C5」を足す。SoC は Amlogic S905X5M（arm64）。BSP は `src/hal/arm64/bsp-s905x5m/`（既存の `bsp-rpi4`・`bsp-rpi5` と同じ並び）、platform の build（`platform/arm64/` の board の分け方に倣う）、`config/ci/config-odroid-c5.mk`。
- 受け入れ: 実機の ODROID-C5 で zedBSD が起動し、desktop（Keiland）が表示される。
- 要る物（p001 で確かめて Phase に分ける）: 起動の道（Amlogic の BL2/BL31・U-Boot か UEFI か、kernel の置き場所と DTB）、UART の console、GIC・timer・SMP（PSCI）、clock・reset・pinctrl の最小、SD/eMMC（Amlogic の MMC）、USB（xHCI と PHY、keyboard・mouse）、Ethernet（DesignWare の MAC と内蔵の PHY、ユーザーの情報は「正しいかわからない」ので p001 で確かめる）、display（Amlogic の VPU・HDMI の出力、framebuffer）、GPU（Mali の driver と Vulkan、Keiland の compositor は Vulkan だけ）。Audio は desktop の表示の受け入れに要らないので後（p001 で順を決める）。
- **GPU の名前の確かめ**: ユーザーの情報は「Mali G31 (Valhalla)」。Mali-G31 は Bifrost の世代で、Valhalla は Mali-G310 などの世代（Q1 の知識、未確認）。S905X5M の GPU が G310（Valhalla）か G31（Bifrost）かを p001 で資料と実機（GPU の ID の register）で確かめ、ユーザーに報告する。
- HAL は既存の口で足りる見込み。HAL を変える必要があると主張する時は AGENTS.md §7 のとおり理由を具体的に。
- 出典: Linux（GPL）の meson・panfrost・stmmac の driver と Mesa の panfrost/panvk は値・表を block ごとの出典で使えるが、code・comment・構造は写さない（guardrail）。

## Phase（案、p001 で確定）

| Phase | 目的 | 状態 | 依存 |
| --- | --- | --- | --- |
| p001 | 調べと設計（S905X5M と ODROID-C5 の資料、起動の道、console、各 block の register と出典、GPU の世代の確かめ、実機の準備（UART・電源・媒体）、Phase の表） | planning | — |
| p002〜 | 実装（p001 で分割: 起動と console → 割り込み・timer・SMP → MMC → USB → Ethernet → display → GPU → desktop） | planning | p001 |
| 最後 | 規約の全文の見直し | planning | 全て |
