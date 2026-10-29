<!-- awesome-plan project=zedbsd record=ws313-p001 -->

# ws313-p001: 調べと設計

Status: in-progress
Queue: q1675-i01（P7）
WS: [WS313](../ws.md)

## 目的

ODROID-C5（Amlogic S905X5M）の起動の道・console・各 block の register と出典・GPU の世代を調べ、実装の Phase の表・実機の試験の方法・ユーザーに頼む物・人の判断の点を [design.md](../design.md) にまとめる。
2026-10-29 のユーザーの指示（ws.md）で、調べは短くして実装へ進む。

## 受け入れ

- design.md に、起動の道、console、各 block の場所と出典、GPU の世代、Phase の表と見積り、夜の試験の順、準備の物、判断の点がある。
- design-reviewer の 1 回の見直しを受け、指摘を直すか理由を書く。

## 結果（2026-10-29）

- 調べ: 配下の subagent（Web 3 本、code 2 本。並列の上限 2 の指示の前に 5 本を立て、指示の後に 3 本を止めた）と P7。
  出典は design.md の表（Hardkernel の kernel_common_drivers・linux・U-Boot、Linux 7.3+、hardkernel.com）。wiki.odroid.com は HTTP 403 で使うのを止めた。
- 確かめた事（資料の上）: GPU は Mali-G310 V2（ユーザーの「G31」は誤り）、Ethernet の MAC は EQoS 4.x・PHY は外付けの RGMII（ユーザーの「内蔵 PHY」は U-Boot の DTS の物で Linux とは違う）、GIC-400・PSCI smc・A55 の Aff1、UART uart_B 0xfe07a000 921600 baud、U-Boot は boot.scr を読む。
- host で Hardkernel の DTS から DTB を作れることを確かめた（`cpp` + `dtc`、`build/tmp/c5dtb/c5.dtb`）。
- 実機では何も確かめていない。
- design-reviewer: 未実施（配下の並列の上限 2 が埋まっているため、空いたら流す）。
