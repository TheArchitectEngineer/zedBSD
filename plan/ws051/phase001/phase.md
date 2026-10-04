<!-- awesome-plan project=zedbsd record=ws051p001 -->

# ws051-p001: 調査と設計（USB-C の DisplayPort Alternate Mode）

Phase ID: `ws051-p001`
Parent: [WS051](../ws.md)
Status: in-progress（2026-10-04。[design.md](../design.md) 第 1 版を書いた。design-reviewer のレビュー待ち、§10 の判断待ち）
Phase disposition: normal
Queue: q680 / q680-i01（P1）

## 調査の結果（2026-10-04、P1 generation15、読みのみ）

- **UCSI への依存**: Linux の i915（v6.8.12 の `display/intel_tc.c`、MIT）は Type-C の port の mode（disconnected・TBT-alt・DP-alt・legacy）を
  UCSI を使わずに決める。ADL-P は `adlp_tc_phy_ops`: live status は `GEN11_DE_HPD_ISR` の TC・TBT の bit と `SDEISR`（legacy）、PHY の ready は
  `TCSS_DDI_STATUS` の READY（0xffffffff なら TC cold）、ownership は `DDI_BUF_CTL` の `TC_PHY_OWNERSHIP`、TC cold は power domain で block、
  lane は FIA の params（`tc_phy_load_fia_params`）、DP-alt か legacy かの確認は `tc_phy_verify_legacy_or_dp_alt_mode`。Alternate Mode に入るのは
  PD controller・EC の firmware と IOM（PMC の mux）。→ **WS051 の画面の出力は WS050（UCSI）に必須の依存を持たない見込み**。ws.md の依存
  （「WS050（UCSI）と i915 の display が前提」）を「i915 の display が前提、UCSI は補助」に直す提案をする（Q1 へ）。
- **今の zedBSD の i915 の display**（`src/drivers/gpu/i915/display/`、Linux v6.8.12 から派生、MIT）: 出力は 1 つ（eDP か DDI B の HDMI、
  `output.c`、起動時に 1 回選び hotplug を追わない）。combo PHY（A・B）だけを扱う（`phy.c`）。Type-C の部分は未移植: `intel_tc.c` 全体
  （`hotplug.c` の「intel_tc_port_connected() is not ported」、`dp-internal.h` の `intel_tc_port_lock` などは空の macro）、Dekel（DKL）PHY と
  その buffer translation、TC PLL（`takeover.c` は MG/TC PLL の読み出しの表だけ）、TC の port の AUX と power well。external の DP の detect
  （`intel_dp_detect`）も未移植。
- 必要な移植の候補（設計で範囲と順を決める）: `intel_tc.c`（ADL-P の分）、`intel_dkl_phy.c` と DDI の DKL の buffer translation、TC PLL の計算と
  有効化（`intel_dpll_mgr.c` の DKL PLL）、TC の AUX の power well と TC cold、外部 DP の detect・DPCD・EDID（AUX の I2C）・link training
  （`dp.c` の eDP 用の link training を外部 DP へ）、`output.c` の出力の選択に USB-C の DP を足す（`display=` の値、HDMI と同じ 1 画面の model）。
- Linux の参照の source: 別 session の scratch に v6.8.12 の i915 の display の source がある（`plan/ws031/linux-parity/linux-reference/` の名前）。
  repository の中の置き場所と取得の手順は WS031 の文書（`plan/ws031/i915-rebuild-rules.md` など）で確かめる。

## 再開点

- `plan/ws031/i915-rebuild-rules.md`（移植の規則）と WS075 の display の文書を読み、design.md（範囲、移植する Linux の関数の一覧、実機の
  試験の方法（QEMU に Type-C は無い）、Phase の案、依存の訂正の提案）を書く。design-reviewer でレビューする。

## 設計の第 1 版（2026-10-04、P1 generation16）

- `plan/ws031/i915-rebuild-rules.md`（移植の規則）、WS031 の 5330 の参照の dump（`display-ref/`）、Linux v6.8.12 の参照の source
  （`plan/ws031/linux-parity/linux-reference/i915-src/display/`）を読み、[design.md](../design.md) を書いた: ADL-P の DP-alt の分担（firmware・IOM・
  i915）、5330 の事実（TC1 = VBT DP-F・AUX-F、TC2 = DP-G・AUX-G、HBR3、TC PLL 1〜4、AUX_USBC の well）、今の i915 の未移植の部分、移植する Linux の
  関数の一覧、1 画面の出力の model、試験（bare metal の Linux で register の正解値）、Phase の案 p001〜p006。
- 発見: `takeover.c` の VBT の DVO port の code が Linux と違い（DPG 16・HDMIG 15）、5330 の TC2 の child（DP-G = 15）を HDMI-G と読み違える
  （XXX の注の「TC1〜TC4 は達しない」は誤り）。p002 で直す（WS031・WS075 の source なので Q1 に知らせた）。ADL-P の DP-alt の TC cold は AUX_USBC の
  domain で防ぐので、`power.c` の未構築の TC cold off の well は TBT-alt にだけ要る。
- 依存の訂正（ws.md）は 2026-10-04 のユーザーの決定 5 とその補足で決まり、ws.md を直した。

## 再開点

- design-reviewer の敵対的レビュー（次）。§10 の 1〜3（TBT-alt を範囲外、1 画面の model、hotplug の範囲）を Q1 経由でユーザーに確かめる。
