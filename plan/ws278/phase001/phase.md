<!-- awesome-plan project=zedbsd record=ws278-p001 -->

# ws278-p001 Pi 5 の調べと設計

- 親: [ws278](../ws.md)、Queue: q1542 / q1542-i01（P3、2026-10-21）
- Status: in-progress（設計を書いた。Q1 の判定待ち。Fable の review は使わない、2026-10-21 ユーザー）

## 目的と受け入れの条件

BCM2712 と BCM2711 の差（boot の firmware・config.txt・kernel の形式と番地・device tree、GIC、UART、timer、A76 の cache・MMU、PCIe と RP1、SD、HDMI と framebuffer、V3D 7.1、WiFi・BT）を調べ、[design.md](../design.md) に方式・Phase の分け方・試験の方法・ユーザーの判断を書く。コードは書かない。

## 記録（2026-10-21 P3）

- 読んだ資料: design.md の冒頭。Raspberry Pi の dts（rpi-6.12.y）、firmware commit 3d301dd924bc の Pi 5 の dtb 2 つ（`dtc` で dts にして C1 と D0 を比べた: uart10 の SPI 121/120、pinctrl・aon-pinctrl の compatible と大きさ、gio_aon の bank の幅、vc6 の compatible）、公式文書（`os_check`・`pciex4_reset`・`enable_rp1_uart`・`kernel_2712.img`）。
- 結論: design.md §1 の表、§2 の方式（compile 時の board、rpi4 の HAL・driver に `HAL_BOARD_RPI5` の分岐、hal.h は不変）、§3 の Phase（p002〜p008、GPU は別 WS の提案）、§4 の D1〜D3、§5 の試験、§6 の危険 R1〜R5。
- HAL の API（hal.h）の変更: 不要。
- 未実施: 実機（無い。Q1 経由でユーザーに頼む）。`kernel_address` を Pi 5 の bootloader が守るかは文書に無く、実機で確かめる（R1）。
