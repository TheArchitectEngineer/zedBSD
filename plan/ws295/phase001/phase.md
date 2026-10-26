<!-- awesome-plan project=zedbsd record=ws295-p001 -->

# ws295-p001 調査: xe の driver の全体像と Phase の分割

- 親: [WS295](../ws.md)。
- 状態: planned（2026-10-27 00:18 Q1 新設。担当の起動はユーザーの指示の時）

## 範囲（code は書かない）

1. 対象の世代ごとの差: Meteor Lake（Xe-LPG、standalone の media GT、display 14・C10/C20 PHY）、Lunar Lake（Xe2、display 20、ISA の変化）、discrete の Arc（Alchemist・Battlemage、VRAM・PCIe・GuC・GSC）。Linux の xe（固定の版、tarball を署名と sha256 で確かめる）と Mesa の intel の compiler（brw・elk、Xe2）を読む。
2. GuC の submission（firmware の版、load、ADS、CTB、context の登録、reset、SLPC）。
3. i915 から写せる物（display の xd の部品、GT の部品、Vulkan の executor（WS293 の後の vk/）、compiler）と、写した後に直す所。
4. firmware（GuC・DMC・GSC・HuC の要否）と zedBSD-firmware への追加。
5. 実機と試験の手順（Lunar Lake の機械、5330 の i915 の回帰と同じ形の自動の script）。
6. 全体の Phase の分割と LW の見積もり、判断の一覧。

## 受け入れ

- 調査の文書（plan/ws295/survey.md）と Phase の案・LW・判断の一覧がユーザーに示されている。
