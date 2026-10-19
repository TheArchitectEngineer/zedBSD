<!-- awesome-plan project=zedbsd record=ws270-p001 -->

# ws270-p001 VF2 の Keiland の設計

- 親: [ws270](../ws.md)、Queue: q1335 / q1335-i01（P1、2026-10-19）
- Status: in-progress（改訂 2 が出来た、design-reviewer 待ち。2026-10-19 P1 q1338: [design.md](../design.md) 版 2、[レビュー 1](../design-review-1.md) の B1・M1〜M7・Minor 1〜12 に対応（design 末尾の表）。判断は WS270-D1・D2・D3・D5（D4 は外した）。版 1 は q1335）

## 目的

VisionFive 2 で Keiland の desktop を動かす道を決め、`plan/ws270/design.md` に書く。コードは書かない。

## 受け入れの条件

1. 今の Keiland（userland/desktop の compositor・libkeiland・libkeiland-backend-zedbsd・shell・app）と依存（Wayland・libvulkan・font・input・seat・logind 相当・audio）を読み、riscv64 で build できるかと足りない物（package・arch の分岐・toolchain の要否）を一覧にする。他の port（amd64・arm64 の Pi 4）の desktop の image の作り方と比べる。
2. WS269 D5 (a)（2026-10-19 22:04 ユーザー）に従い、WS269 p010（GPU の Vulkan）までに進める範囲（service の起動、seat・input（USB keyboard・mouse、WS256 p020）、display の node（WS269 p007 の gpu0）の claim、graphical の login の道）と、描画を始める条件を決める。Keiland は Vulkan でしか描かない（WS269 design §2）事を前提に、画面なしで確かめる方法（画面に 1 行、log を画面に出す等、AGENTS.md の判定の規則に合わせる）を書く。
3. 実装の Phase の分割（依存・LW の見込み・T1（QEMU の riscv64-virt）と実機の確認の点）と、ユーザーの判断が要る点（D1…）。HAL の API の変更が要るなら差分の案を proposed/ に置くだけ。
