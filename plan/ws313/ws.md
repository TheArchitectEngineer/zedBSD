<!-- awesome-plan project=zedbsd record=ws313 -->

# WS313: ODROID-C5（Amlogic S905X5M）の BSP と Mali の GPU で、起動して desktop を表示する

Status: planning
Primary Milestone: MG008
Resume point: 今夜のユーザーの実機の試験（[tests/README.md](tests/README.md)）の結果で各 Phase を直す。desktop（p009）は Vulkan の段（[gpu.md](gpu.md) §11、約 155 LW）が無いと出ない。担当は P7（q1675-i02）。

## ユーザーの指示（原文、2026-10-29）

「WSを立てます。P7の作業として開始します。ODROID-C5基板が届きました。基板を見たところ、SoC＋メモリ＋LAN/USB/AudioのドライバICくらいしかついていないです。amlogicのこのSoCに対応してほしいです。BSPを追加して、ODROID-C5というボードにします。Amlogic S905X5MがSoCです。BSPはbsp-s905x5m/でいいと思います。情報としては、正しいかわかりませんが、EthernetのMACはDesignWare MAC、PHYはAmlogic Internal MII PHY (rmii?)だそうです。あと、Mali G31のドライバ(Valhalla)も必要です。これがでかいですね。起動してデスクトップが表示できるところまでをゴールにして、WSを作ります。」

## ユーザーの指示（原文、2026-10-29、進め方）

「WS313の進め方は、Linuxを参考に、カーネル、ドライバを揃えておいてください。実機テストなしでコードだけ書いて揃えておいてください。今日の夜、まとめてテストします。」

- 解釈（Q1 経由）: p001 の調べと設計は短く（起動の道・console・各 block の register と出典・GPU の世代・Phase の表）、design-reviewer は 1 回で、すぐ実装へ。
  実機の試験なしで、起動と console・割り込み・timer・SMP・MMC・USB・Ethernet・display・GPU（Mali と Vulkan）・desktop の config まで code を書いて揃える。
  確かめは build（warning 0）と、書ける所の host の試験まで（この board の HAL は QEMU の `virt` では起動しないので QEMU の起動は無い。2026-10-29 design-reviewer）。各 Phase は「実装済み・実機未試験」と記録し cleared にしない。
  夜の実機の試験の手順（媒体への書き込み・UART の配線・確かめる順・判定）を phase.md と `plan/ws313/tests/` の script にまとめる。

## ユーザーの指示（原文、2026-10-29、並列化）

「WS313は調査、コード作成などで、2つまでサブエージェントを作って並列化し、効率化してください。」

- 解釈（Q1 経由）: P7 は配下のサブエージェントを同時に 2 つまで使ってよい。配下も同じ worktree の中で、互いに別の file・directory を持たせる
  （例: 一つは GPU（Mali の driver と Vulkan）、一つは周辺の driver（MMC・USB・Ethernet・display））。配下にも AGENTS.md の規則を渡す。
  commit は同じ index を同時に触らないよう順に。上限に当たったら Q1 が配下を止める事がある。

## ユーザーの判断（2026-10-29、§8）

- 2026-10-29 §8 の判断（ユーザーのクリック、Q1 の中継。前置きで WS・状態・未決・用語を説明した）:
  - bootloader の binary（Amlogic の BL2・BL31 と Hardkernel の U-Boot）: 「取得して image に入れる」→ build で Hardkernel の公開の binary を取得し SHA-256 で検証して image に入れる。再配布の条件（Amlogic の blob の license）を確かめて記録する（notice の file）。
  - GPU の CSF の firmware（mali_csffw.bin）: 「取得・検証・同意 (推し)」→ 他の firmware と同じく linux-firmware から取得・SHA-256 で検証・license への同意の形で image に。
  - UART の対話での判定: 「許可する (推し)」→ AGENTS.md §9 の例外を ODROID-C5 の実機に広げる（生きた serial を対話で読む。network と画面が出た後は SSH と画面の PNG）。
  - 起動の道（Hardkernel の U-Boot＋boot.scr＋booti）・DTB の出所（Hardkernel の DTS から build）・範囲（eMMC・OTG・Audio・HDMI の mode の変更は後回し）は Q1 の判断で推しどおり。

## 目標（Q1 の解釈）

- 新しい board「ODROID-C5」を足す。SoC は Amlogic S905X5M（arm64）。BSP は `src/hal/arm64/bsp-s905x5m/`（既存の `bsp-rpi4`・`bsp-rpi5` と同じ並び）、platform の build（`platform/arm64/` の board の分け方に倣う）、`config/ci/config-odroid-c5.mk`。
- 受け入れ: 実機の ODROID-C5 で zedBSD が起動し、desktop（Keiland）が表示される。
- 要る物（p001 で確かめて Phase に分ける）: 起動の道（Amlogic の BL2/BL31・U-Boot か UEFI か、kernel の置き場所と DTB）、UART の console、GIC・timer・SMP（PSCI）、clock・reset・pinctrl の最小、SD/eMMC（Amlogic の MMC）、USB（xHCI と PHY、keyboard・mouse）、Ethernet（DesignWare の MAC と内蔵の PHY、ユーザーの情報は「正しいかわからない」ので p001 で確かめる）、display（Amlogic の VPU・HDMI の出力、framebuffer）、GPU（Mali の driver と Vulkan、Keiland の compositor は Vulkan だけ）。Audio は desktop の表示の受け入れに要らないので後（p001 で順を決める）。
- **GPU の名前の確かめ**: ユーザーの情報は「Mali G31 (Valhalla)」。Mali-G31 は Bifrost の世代で、Valhalla は Mali-G310 などの世代（Q1 の知識、未確認）。S905X5M の GPU が G310（Valhalla）か G31（Bifrost）かを p001 で資料と実機（GPU の ID の register）で確かめ、ユーザーに報告する。
- HAL は既存の口で足りる見込み。HAL を変える必要があると主張する時は AGENTS.md §7 のとおり理由を具体的に。
- 出典: Linux（GPL）の meson・panfrost・stmmac の driver と Mesa の panfrost/panvk は値・表を block ごとの出典で使えるが、code・comment・構造は写さない（guardrail）。

## Phase（p001 の設計、[design.md](design.md) §5）

実機なしで揃える。実装の Phase は「実装済み・実機未試験」と記録し、cleared にしない（ユーザーの指示、2026-10-29）。

| Phase | 目的 | 状態 | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調べと設計 | in-progress（design-reviewer の 1 回） | — |
| [p002](phase002/phase.md) | HAL の BSP（起動・UART・FDT・GIC・timer・PSCI）と build の配線・config | in-progress（実装済み・実機未試験） | p001 |
| [p003](phase003/phase.md) | 媒体（DTB・boot.scr・SD の image）と夜の試験の手順・script | in-progress（image の build まで済み・実機未試験） | p002 |
| [p004](phase004/phase.md) | SD（meson の MMC）と root | in-progress（実装済み・実機未試験） | p002 |
| [p005](phase005/phase.md) | USB（PHY・CRG の xHCI） | in-progress（実装済み・実機未試験） | p002 |
| [p006](phase006/phase.md) | Ethernet（EQoS・RGMII の PHY） | in-progress（実装済み・実機未試験） | p002 |
| [p007](phase007/phase.md) | display（U-Boot の mode の OSD1） | in-progress（実装済み・実機未試験） | p002 |
| [p008](phase008/phase.md) | GPU（Mali-G310）と Vulkan の device | in-progress（kernel の driver は実装済み・実機未試験。Vulkan の段は設計だけ） | p002 |
| [p010](phase010/phase.md) | Valhall の shader compiler（`src/drivers/gpu/mali/compiler/`） | in-progress（実装済み・host の試験 PASS・実機未試験） | p008 |
| [p011](phase011/phase.md) | render の node・scheduler・Vulkan の executor・CS の builder | in-progress（実装済み・実機未試験） | p008・p010 |
| p009 | desktop（Keiland の config） | planned | p007・p010・p011 |
| 最後 | 規約の全文の見直し | planned | 全て |

Audio（line-out）は desktop の表示の受け入れに要らないので Future Work の案（Q1 へ）。
