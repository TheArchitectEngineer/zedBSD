<!-- awesome-plan project=zedbsd record=ws245-p001-design -->

# WS245 の設計: i915 の Gen12 系（display 12・13、最後に display 11）の display の書き直し

2026-10-15 P2（q991、ws245-p001）の初版を、2026-10-15 P1 が [design-review-1.md](design-review-1.md)（改訂 2）と
[design-review.md](design-review.md)（2 回目のレビューの原文、改訂 3）、ユーザーの決定 1〜4 と「対象の機種」（[ws.md](ws.md)）で改訂した物（**改訂 3**）。
親: [p001](phase001/phase.md)・[WS245](ws.md)。code は書かない（p002 以降）。
今の code（`src/drivers/gpu/i915/display/`、`intel/`）は「外から見える振る舞いと register の書き込みの列」を知るためだけに読んだ。
その構造・型・名前・制御の流れは、この設計に持ち込まない（§0.3）。指摘ごとの扱いは §8（レビュー 1）と §9（レビュー 2）。

決定の要約（この設計に入れた物）:
1. 口 `xd.h` を p003 で決め、own の file は最初からその口だけを呼ぶ。口の実装は旧 code への薄い adapter と xd の 2 つで、
   boot の語で丸ごと切り替える（状態を混ぜない）。adapter は旧 code の削除の Phase で消す（§6.1）。
2. **対象の機種は 5330 を特別扱いしない**（2026-10-15 ユーザー「すでに多数の機種で動作しており、5330を特別扱いはしません。Skylake～Raptor Lake Refreshまですべてサポートします。」）。
   xd は **display 12**（Tiger Lake、Rocket Lake、Alder Lake-S、Raptor Lake-S/HX と Refresh）と **display 13**（Alder Lake-P/N、Raptor Lake-P/U）を持ち、
   **display 11**（Ice Lake）を display 12・13 の後、規約の全文の見直しの前の Phase で新しく支える（2026-10-15 ユーザー「display 11は、i915のリファクタリングの既存WSに最後の方のphaseとして入れてください。」）。
   display 10 と display 9 は範囲外（display 9 は `gen9-*.c`、WS244）。5330（ADL-P）と 5320（TGL）は実機の確かめの 2 台で、設計の前提ではない。
   改訂 2 の決定 2「5330のみ」の読み（display 13 だけ）は誤りとして取り消した。
3. 手順の第一の出典は Linux v6.8.12 の code。Intel の公開の PRM（TGL・ACM）は register の意味と値の補助。code・comment・構造は写さず、
   値は block ごとに「Value(s) referenced:」、手順は「Procedure referenced: <版> <path> (<関数>)」を付ける（§5.4）。
4. PRM と Linux の値・手順が食い違う時は Linux を優先する（5330・5320 で動いている実績）。

## 0. 出典とライセンス

### 0.1 出典の優先（決定 3・4）

| 順 | 出典 | 使い方 |
| --- | --- | --- |
| 1 | **Linux v6.8.12** `drivers/gpu/drm/i915/`（展開 `/home/awe/zedBSD-worktrees/p1/build/ws229-p001/linux/linux-6.8.12/`、WS229 p001 が kernel.org の tarball を検証して展開した物、読むだけ） | 手順（register の列・待ち・条件分岐の規則）と値（offset・bit・表）の第一の出典。読む分岐は **`DISPLAY_VER == 11/12/13`、`IS_ICELAKE`、`IS_TIGERLAKE`、`IS_ROCKETLAKE`、`IS_ALDERLAKE_S`（RPL-S を含む）、`IS_ALDERLAKE_P`（ADL-N・RPL-P・RPL-U を含む）、`PCH_ICP/TGP/ADP`**。DG1・DG2・JSL/EHL・display 14 以降の分岐は読まない |
| 2 | 実機の実測。5330（ADL-P）: `plan/ws031/display-ref/`（Linux の register dump・dmesg・debugfs・DPCD・EDID）、`plan/ws051/tests/m3-5330-20261007/`（USB-C DP を挿した 2 画面の dump、`debugfs/`）。**5330 の VBT の生の bytes と `vbt-decode.txt` は 2026-10-14・15 のユーザーの判断で削除済み（無い）**。5320（TGL）: register の dump は無い（WS118 の遠隔 log だけ） | 値の照合と host 試験の oracle（§6.3）。display 12 の oracle は Linux の表の値だけ（実測は無い） |
| 3 | Intel の公開の PRM（下の表。`/home/awe/zedBSD-worktrees/p2/plan/ws245/temp/` に PDF と `pdftotext -layout` の text。git には入れない。再取得は `https://cdrdv2.intel.com/v1/dl/getcontent/<id>`） | register の意味（field の定義）と、Linux の code だけでは読めない理由（なぜその順か）の補助。Linux と食い違えば Linux |

PRM の版: Alder Lake（ADL-P・ADL-S）の版は公開されていない（2026-10-15 に索引 `developer-reference/1-0/overview.html` を確認: Alchemist、Rocket Lake、
Tiger Lake、Lakefield、DG1、Ice Lake 以前のみ）。Rocket Lake と Ice Lake の PRM は公開されているが**未取得**（この設計では読んでいない。display 11 の Phase と RKL の
register の意味の確認で必要になったら取得する: §6.2 の p014）。決定 3 の下では、PRM の有無は設計を止めない。

| 文書 | id | 版 | 補助として使う所 |
| --- | --- | --- | --- |
| TGL Vol 12 Display Engine（IHD-OS-TGL-Vol 12-12.21） | 705833 | 2021-12 | display 12 の mode set・power well・DC state・CDCLK・DPLL・DKL・combo PHY・AUX・watermark の手順の意味 |
| TGL Vol 2c Registers A–L / M–Z | 703046 / 703047 | 2021-12 | north display の register の field の定義（display 12・13 で共通の物） |
| TGL Vol 1 Preface / Vol 14 Workarounds | 703034 / 705836 | 2021-12 | ライセンスの文 / WA の意味 |
| ACM（DG2）Vol 12 Display Engine（IHD-OS-ACM-Vol 12-3.23） | 774520 | 2023-03 | **DG2（Xe-HPD）の文書。ADL-P と同じ display 13 だが CDCLK（squash）・PHY（SNPS）・PLL・port・pipe count の mailbox は別物で、ADL-P に当てない**。使うのは South Display Engine（PP・PWM・GMBUS・south の HPD）の register の **offset** の確認だけ。bit は Linux の ADL-P/ICP 系の値と 5330 の dump で照合（§5.2） |
| ACM Vol 2c Registers Part 1 / 2 | 774481 / 774482 | 2023-03 | south display の register の field、display 13 で TGL Vol 2c に無い register（TRANS_SET_CONTEXT_LATENCY など）の field |

### 0.2 ライセンス

- Linux v6.8.12 の i915 の display の file は全部 **MIT**（`SPDX-License-Identifier: MIT` か "Permission is hereby granted" の文。2026-10-15 に `i915_reg.h`、
  `intel_hotplug.c`、`intel_hotplug_irq.c`、`intel_tc.c`、`intel_display_power_map.c`、`intel_cdclk.c`、`intel_dmc.c`、`skl_watermark.c`、`intel_bios.c`、
  `intel_gmbus.c`、`intel_dpll_mgr.c`、`intel_ddi.c`、`intel_ddi_buf_trans.c`、`intel_dp_link_training.c`、`intel_combo_phy.c`、`intel_dkl_phy.c`、
  `intel_pps.c`、`intel_backlight.c`、`intel_display_power.c`、`intel_display_power_well.c`、`intel_display_irq.c`、`intel_vga.c`、`intel_display.c`、
  `intel_dp.c`、`intel_dp_aux.c`、`intel_opregion.c`、`intel_vbt_defs.h`、`intel_display_driver.c`、`intel_hdmi.c`、`intel_color.c` の先頭で確認）。
  `intel_step.c`・`soc/intel_pch.c`（この改訂で足した stepping と PCH の表の出典）も MIT（2026-10-15 確認）。guardrail の規則は MIT・GPL とも同じ。
- guardrail「MIT・GPL の出典のデータの引用」（2026-10-15）のとおり: 値・表はデータとして引用し block ごとに出典を示す。**code・comment・構造は写さない**。
  参考にした手順も block ごとに「Procedure referenced:」で示す（§5.4）。file の先頭には出典・派生の宣言・SHA を一切書かない。
- **PRM の Notices（Vol 1 Preface、TGL・ACM とも同文。レビュー 2 の M12）**: (1)「You may create software implementations based on this document … intended to
  execute on the Intel product(s) referenced in this document」: TGL の PRM が指す product は Tiger Lake、ACM は DG2 で、ADL-P/S・RKL は「referenced」ではない。
  (2)「You agree to grant Intel a non-exclusive, royalty-free license to any patent claim thereafter drafted …」（grant-back の条項）。(3)「legal analysis」での使用の禁止。
  決定 3 により実装の根拠は Linux の code で、PRM から取る物は register の意味の理解と、Linux に無い値（§5.3 で PRM を第一とする行）だけ。その値は
  「Value(s) referenced: IHD-OS-TGL-Vol 12-12.21 §<章> (<表の名前>)」で示し、PRM の文を comment に写さない（要約も自分の言葉で）。
  (1)〜(2) を受けるかは**ユーザーの判断**（§7 #11）。判断までは PRM の値を「照合」だけに留め、実装に入れる値は Linux の表から取る（この設計の §5.3 は既にそう）。
- 実機の実測（EDID・DPCD・register の語）は値の照合と試験の fixture に使う。panel の EDID と DPCD は「licence and redistribution not
  audited」の注記のまま（`tests/display/dp-fixture-latitude5330.h`）。**VBT・OpRegion の fixture は実物の bytes を使わず合成する**（§5.1、§7 #7。
  5330 の生の bytes はユーザーの判断で削除済み: レビュー 2 の M13）。
- `userland/base/licenses/i915-driver/`（LICENSE・Makefile）の整理は WS244 の最後。この WS は Gen12 の file から "Derived from the Linux kernel"
  （2026-10-15 の grep: 22 file、"Derived from the Linux kernel v6…" 26 箇所、"derived from the Linux kernel (…)" 6 箇所）と Linux の MIT の表示を消す所まで。

### 0.3 今の code の読み方（この設計で守った規則）

- 読んだのは「何をするか」: register の列と待ち、判断の規則、own の file から呼ばれる関数と型（§4.2 は 2026-10-15 に header の宣言と own の file の
  識別子を突き合わせて数えた）、試験が何を測るか（§6.4）、今の code がどの platform を受けるか（`device.c` の PCI ID の表: TGL と ADL-P/N・RPL-P/U だけ。
  **RKL・ADL-S・RPL-S の ID は今の code に無い**: §1.1）。
- 持ち込まない物: Linux の型（`intel_crtc_state`・`drm_i915_private`・`intel_encoder`・`i915_lcd_*` の環境）、関数の分け方（`pre_enable`/`enable`/
  `post_disable` の encoder の段、`crtc_enable` の hook、`intel_tc_phy_ops` の表、`intel_display_device_info` の feature flag の表）、atomic state の形（global obj、`i915_lcd_words` の recorder）、
  "world" の割り当て、Linux の名前（`hsw_*`・`icl_*`・`tgl_*`・`adlp_*`・`xelpd_*`）、comment の文。Linux の file の分け方も持ち込まない（§3.2 の境界は
  hardware の単位で決める）。platform の差は Linux の `IS_*` の分岐の形ではなく、§3.1 の platform の記述（データ）と、手順の中の少数の「流れの選択」で持つ。
- 残す物: 外向きの口（§4.1）、boot の語・log の文言のうち試験と運用が読む物（§4.4）、hardware の事実（§1）、試験の oracle（§6.3）。

## 1. 対象の hardware の事実

### 1.1 対象の platform（表 P1。Linux v6.8.12 の `intel_display_device.c`・`intel_display_power_map.c`・`intel_dpll_mgr.c`・`skl_watermark.c`・`intel_cdclk.c`・`intel_dmc.c`・`intel_step.c`・`soc/intel_pch.c` の値）

| platform | display | port と PHY | PLL | DBUF | power well の表 | PCH | DMC | CDCLK | stepping の表 | 今の code | 実機 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Tiger Lake**（TGL、UP3/UP4 は `tgl_uy`） | 12 | A・B = combo PHY A/B（PHY C は Linux も使わない）、TC1〜6 = Dekel PHY D〜I。FIA は modular か否かを DFLEXDPSP(FIA1) の bit で判定 | DPLL0/1（combo）、TBT PLL、TC PLL 1〜6（Dekel）。参照 38.4 MHz なら CFGCR0 の小数を半分に書く WA | S1・S2（2048 block）、join 無し。MBUS の abox 1・2（と abox 0）の credit を init で書く | `tgl_power_wells`: PW_1、DC_off、PW_2（fuse）、PW_3（pipe B、VGA、fuse）、DDI IO A/B/C/TC1〜6、PW_4（pipe C）、PW_5（pipe D）、**TC_cold_off（pcode）**、AUX A/B/C/USBC1〜6/TBT1〜6 | TGP（0xA080、0x4380） | `tgl_dmc_ver2_12.bin` | `icl_cdclk_table`（172.8/192/307.2/326.4/556.8/652.8 MHz @38.4）、crawl 無し・squash 無し、bypass = ref/2 | `tgl_revids` / `tgl_uy_revids`（display step は GT と別） | TGL の ID を display 12 として受ける。**TC port は未対応**（`power.c` の TC_cold_off の well は未移植） | 5320（TGL-UP3、eDP・USB-C/TB4。ユーザーが起動・eDP を確認済み。TC は未確認） |
| **Rocket Lake**（RKL） | 12 | A・B・TC1・TC2 = **全部 combo PHY**（PHY A〜D。A が B の、C が D の comp master。PHY_MISC は A・B だけ）。TC の流れは無い | DPLL0/1/**4**（combo）。map は DPCLKA_CFGCR0 の RKL の field | S1・S2、TGL と同じ表。abox 0 だけ | `rkl_power_wells`: PW_1、DC_off、**PW_2 は無い**、PW_3（pipe B、VGA）、PW_4（pipe C）、DDI IO A/B/TC1/TC2、AUX A/B/USBC1/USBC2。pipe は A〜C | TGP（0x4380） | `rkl_dmc_ver2_03.bin` | `rkl_cdclk_table`（同じ周波数、divider と ratio が 2 倍） | `rkl_revids` | **ID が無い（未対応）** | 無し |
| **Alder Lake-S**、**Raptor Lake-S/HX**、Refresh（`IS_ALDERLAKE_S`、RPL-S は subplatform） | 12 | A・TC1〜4 = **全部 combo PHY**（PHY A〜E。A が B・C の、D が E の master。PHY_MISC は A だけ）。TC の流れは無い | DPLL0〜3（combo）。map は **PHY ごとの DPCLKA_CFGCR**（ADL-S の register） | S1・S2、TGL と同じ表 | `adls_power_wells` = TGL の main と aux（TC_cold_off 無し） | ADP（0x7A80 ADP-S、0x7A00 ADP3） | `adls_dmc_ver2_01.bin` | `icl_cdclk_table` | `adls_revids` / `adls_rpls_revids` | **ID が無い（未対応）** | 無し（10.0.30.5 の i5-13400F は F で iGPU が無い） |
| **Alder Lake-P/N**、**Raptor Lake-P/U**（`IS_ALDERLAKE_P`、N・RPL・RPLU は subplatform） | 13 | A・B = combo PHY A/B、TC1〜4 = Dekel PHY F〜I。FIA は modular（TC1/2 = FIA1、TC3/4 = FIA2） | DPLL0/1、TBT PLL、TC PLL 1〜4。TC PLL の enable は ADL-P の offset（`ADLP_PORTTC_PLL_ENABLE`）。38.4 MHz の小数の WA あり | S1〜S4（4096 block）、pipe が 1 つ（A か B）なら MBUS join。abox の credit は書かない | `xelpd_power_wells`: PW_1、DC_off、PW_2（pipe B/C/D の親、port、AUX C〜E/USBC/TBT、VGA）、**PW_A（pipe A、PW_2 と独立）**、PW_B/C/D、DDI IO A/B/TC1〜4、AUX A/B/USBC1〜4/TBT1〜4 | ADP（0x5180 ADP-P、0x5480 ADP4） | `adlp_dmc.bin`（main と pipe A/B）。`DISPLAY_VER13_DMC_MAX_FW_SIZE` 0x20000 | `adlp_cdclk_table`（179.2/192/307.2/556.8/652.8 @38.4）、**crawl あり**、RPL-U は `rplu_cdclk_table`、ADL-P A0 は別表 | `adlp_revids` / `adlp_n_revids` / `adlp_rplp_revids` | ADL-P/N・RPL-P/U の ID を display 13 として受ける | 5330（ADL-P、eDP・HDMI・USB-C DP の 2 画面・suspend をユーザーが確認済み。dump あり） |
| **Ice Lake**（ICL、display 11 の Phase p014） | 11 | A・B = combo PHY A/B、C〜F = **MG PHY**（Dekel ではない。register は MG_* で window 無し）。FIA は FIA1 だけ | DPLL0/1、TBT PLL、**MG PLL 1〜4**（MG_PLL_* の register、`icl_mg_pll_write`） | S1・S2（2048 block）、`icl_allowed_dbufs`（1 pipe は 1 slice） | `icl_power_wells`: PW_1、DC_off、PW_2、PW_3（pipe B、VGA）、DDI IO A〜F、AUX A〜F、AUX TBT1〜4、PW_4（pipe C） | ICP（0x3480、0x3880） | `icl_dmc_ver1_09.bin`（**header v1**、program の先頭は 0x80000 固定、最大 0x6000） | `icl_cdclk_table`、bypass 50 MHz、voltage の段が TGL と違う | `icl_revids` | **ID が無い（未対応）** | 無し |

display 12 と 13 で共通の物: pipe/transcoder の register（display 12 以降は transcoder = pipe で transcoder EDP は無い。**ICL は transcoder EDP を DDI A の eDP に使う**）、
DP_TP_CTL/STATUS は transcoder ごと（display 12 以降。**ICL は port ごと**）、DE の割り込み（DE_PIPE/PORT/MISC/HPD、display 11 から同じ形）、HPD の pin の表
（north `hpd_gen11`: TC/TBT 1〜6、south `hpd_icp`: DDI A〜C と TC 1〜6。PCH が ICP・TGP・ADP で同じ bit）、GMBUS の pin（`gmbus_pins_icp`: 1・2・3・9〜14）、
raw clock（SFUSE_STRAP で 19.2/24 MHz を選び RAWCLK_FREQ に書く。ICP 以降は numerator の field あり）、PP・PWM の register（0xC7200・0xC8250）、AUX の register。

**今の code が受けない ID（RKL・ADL-S/RPL-S）**は、この WS で xd が新しく受ける。今動いている物を壊さない受け入れ（§4.4）は TGL と ADL-P 系に対して確かめ、RKL・ADL-S は
「机上（Linux の表と手順）」で作って host 試験で固定し、実機は無いので「未確認」と書く（§6.3）。

### 1.2 platform ごとの手順の差（表 P2。§2 の各節が参照する）

| 項目 | display 12（TGL・RKL・ADL-S） | display 13（ADL-P 系） | display 11（ICL、p014） |
| --- | --- | --- | --- |
| init の追加の step（§2.2） | DBUF_CTL の tracker state service = 8（`gen12_dbuf_slices_config`）、MBUS abox の credit（`icl_mbus_init`、abox 0 も）、BW_BUDDY（ADL-S と RKL A0 は WA の表）と TLB req timer 0x8（Wa_22010178259）、GEN11_CHICKEN_DCPR_2 の 4 bit、SOUTH_DSPCLK_GATE_D（PCH TGP/ADP） | tracker state は書かない、abox は書かない、BW_BUDDY（通常の表）、GEN11_CHICKEN_DCPR_2、XELPD_DISPLAY_ERR_FATAL_MASK = ~0、`adlp_display_wa_apply`（CLKGATE_DIS_5 の DPCE gating、CHICKEN_DCPR_1 の DDI clock reg access を消す）、PW_1 の enable の前に CHICKEN_DCPR_1 の DISABLE_FLR_SRC（Wa_16013190616） | abox 0 の credit、BW_BUDDY 無し、tracker 無し |
| power well の木（§2.5） | PW_1 → PW_2（fuse）→ PW_3（pipe B）→ PW_4（pipe C）→ PW_5（pipe D）。pipe A は PW_1 の下。RKL は PW_2 無し・PW_5 無し。TGL だけ TC_cold_off（pcode の well） | PW_1 → PW_A（pipe A）/ PW_2 → PW_B/C/D | PW_1 → PW_2 → PW_3（pipe B）→ PW_4（pipe C） |
| TC の流れ（§2.8） | TGL: live = DFLEXDPSP の TBT/TC の bit + SDEISR、ready = DFLEXDPPMS、所有 = DFLEXDPCSSS、TCCOLD の block = **GT driver mailbox 0x26 の well**（TC_cold_off）。RKL・ADL-S: TC の流れ無し | live = DE_HPD_ISR + SDEISR、ready = TCSS_DDI_STATUS、所有 = DDI_BUF_CTL bit 6、TCCOLD の block = AUX USBC の well | TGL と同じ FIA の流れ、TCCOLD の block = legacy は AUX の well と pcode の EXIT_TCCOLD（`icl_tc_cold_exit`）、他は TC_COLD_OFF の domain（well 無し） |
| TC PHY の register（§2.8） | Dekel（HIP_INDEX の window、DKL_*）、TGL の swing の表 | Dekel、ADL-P の swing の表、DP20BITMODE を消す、HDMI の loadgen select（ADL-P だけ）、DDI_BUF_CTL の PHY link rate の field と所有 bit | MG（直の register、MG_*）、ICL の MG の swing の表 |
| PLL（§2.6） | combo の値の表は共通（19.2/24 の表、38.4 は 19.2 の表）。TGL・ADL-S は 38.4 MHz の小数の WA。RKL は DPLL4 も候補、ADL-S は DPLL0〜3。DDI → PLL の map: TGL は DPCLKA_CFGCR0（ICL の field）、RKL は DPCLKA_CFGCR0 の RKL の field、ADL-S は ADLS_DPCLKA_CFGCR(phy) | 同じ表、WA あり、DPCLKA_CFGCR0（ICL の field）。TC PLL の enable の offset が違う。A0 step の CMTG の WA は持たない（A0 は支えない: §7 #14） | 同じ表、WA 無し（EHL だけ）、MG PLL は別の算法（`icl_calc_mg_pll_state` を MG の register へ） |
| CDCLK（§2.6） | PLL の disable → enable で変える（crawl 無し）。voltage の段は `tgl_calc_voltage_level`（312/326.4/556.8/652.8） | crawl（PLL ratio の書き換え）。voltage は同じ段。RPL-U は別表 | PLL disable → enable。`icl_calc_voltage_level`（312/556.8/652.8） |
| DBUF（§2.10） | 2 slice、`tgl_allowed_dbufs`（1 pipe は S1+S2、A+B は A=S2・B=S1）。MBUS join 無し。DBOX: A credit 2、BW 2、B 12、B2B の 3 field | 4 slice、`adlp_allowed_dbufs`、join（A か B 単独）。DBOX: A credit 6（join）/4、BW 2、B 8、B2B。PIPE_ARB_CTL の prog slots（Wa_22012358565）、PLANE_CTL の ARB slots、TRANS_SET_CONTEXT_LATENCY、PLANE_WM_SAGV（HW の SAGV WM）、WM level は 6 | 2 slice、`icl_allowed_dbufs`（1 pipe は 1 slice）。DBOX: A 2、BW 1、B 8。WM level 8 |
| watermark（§2.10） | level 8、latency の読みの加算 3 µs（display 12 以降）、SAGV の WM は software（level 0 を SAGV の block time で計算し、SAGV の可否で level を選ぶ）、transition の min 4 | level 6、HW の SAGV WM（PLANE_WM_SAGV） | level 8、加算 2 µs、transition の min 4 |
| DMC（§2.12） | package v2、header v3、main の program は header の start_mmioaddr、pipe DMC は 0x92000 から（package にあれば）、event handler の disable、TGL は main の CLK_MSEC・VBLANK_A の handler も disable、ADL-S は VBLANK_A | 同じ + pipe DMC の base 0x5F000、PIPEDMC gating の WA（Wa_16015201720）、PIPEDMC_CONTROL | package v1、header v1、program は 0x80000 から、handler の disable は無し（Linux も < 12 では行わない） |
| VBT（§2.13） | DVO port → DDI: TGL は共通の表、RKL は PHY の表（C/D → TC1/TC2）、ADL-S は B〜E → TC1〜4。DDC pin: TGL は `icp_ddc_pin_map`（TC5/6 まで）、RKL は `rkl_pch_tgp_ddc_pin_map`、ADL-S は `adls_ddc_pin_map` | `xelpd_port_mapping`（F〜I → TC1〜4）、`adlp_ddc_pin_map` | 共通の表、`icp_ddc_pin_map` |
| GOP（§2.14） | eDP は DDI A・pipe A（5320 で確認。RKL・ADL-S のデスクトップは GOP が HDMI/DP を灯す: 分類の規則は同じ） | 同左（5330） | eDP は transcoder EDP |

### 1.3 5330（ADL-P）の実測（display-ref と m3 の dump。display 13 の oracle）

| 項目 | 事実 | 出典 |
| --- | --- | --- |
| GPU | ADL-P Iris Xe、display 13（Linux の "xelpd"）、GT は graphics 12 | `device-info.c`、`dmesg-display.txt` |
| PCH | ADP-P（Linux の `PCH_ADP`）。south display は PCH 側（GMBUS・PP・PWM・SHOTPLUG・SDE の割り込み）。**raw clock 19.2 MHz**（`dmesg-display.txt:35` "rawclk rate: 19200 kHz"。SFUSE_STRAP の bit で 24 か 19.2 を選び、RAWCLK_FREQ に分子・分母を書く: Linux `intel_cdclk.c` (cnp_rawclk)）。参照 clock（DSSM）の 38.4 MHz とは別の clock | `dmesg-display.txt`、Linux `cnp_rawclk` |
| 参照 clock | 38.4 MHz non-SSC（DSSM で読む） | `dbg-i915_shared_dplls_info.txt`（"PLL refclks: non-SSC: 38400 kHz"） |
| 内蔵 panel | eDP、AUO B133HAN、1920×1080、60 Hz（pixel 140.8 MHz）と 48 Hz、6 bpc（dither）、290×170 mm。DDI A / combo PHY A / pipe A / transcoder A / AUX A / DPLL0。DPCD 1.1（0x000 = 0x11）、HBR（2.7 GHz）× 2 lane、**eDP 1.2（DPCD 0x700 = 0x01）**、PSR 無し | `dbg-i915_display_info.txt`、`dp-fixture-latitude5330.h`、`dpcd-drm_dp_aux0-000.bin`、`dpcd-drm_dp_aux0-700.bin`、`regs-selected.txt` |
| DPLL0 の語 | CFGCR0 0x00e001a5（DCO 整数 0x1a5、小数の field は 0x3800 = 表の 0x7000 の半分: 38.4 MHz の WA、§2.6）、CFGCR1 0x00000088（HBR） | `dbg-i915_shared_dplls_info.txt`、Linux `intel_dpll_mgr.c` (ehl_combo_pll_div_frac_wa_needed) |
| transcoder A の語 | TRANS_DDI_FUNC_CTL 0x8a210002（DP SST、DDI A、2 lane、6 bpc） | `regs-selected.txt` |
| backlight | PCH の PWM controller 0、BLC_PWM_PCH_CTL2 0x00017700 = 周期 96000 = **19.2 MHz ÷ 200 Hz**（VBT block 43 の PWM 200 Hz。`vbt-decode.txt` は削除済みなので、今は 96000 と 19.2 MHz から逆算した値）、enable bit 31、極性 0 | `regs-selected.txt:6-7` |
| CDCLK | crawl あり・squash 無し（`dmesg-display.txt:17-18`）。m3（2 画面）の "Current CD clock frequency: 192000 kHz、Max 652800 kHz" | `dmesg-display.txt`、`m3-5330-20261007/debugfs/i915_cdclk_info.txt` |
| 外部の口 | HDMI-A-1（DDI B、combo PHY B、DVI mode で駆動）、DP-1 = TC1、DP-2 = TC2（USB-C、DP-alt。m3: TC2・pin assignment C・4 lane・1920×1280・pipe B・TC PLL 2） | `00-summary.txt`、`m3-5330-20261007/README.md` |
| DMC | `i915/adlp_dmc.bin` v2.20（main と pipe A/B の firmware）。package は `userland/firmware/i915/`（linux-firmware 20260410、sha256 は manifest） | `dbg-i915_dmc_info.txt`、`i915-firmware.manifest` |
| DBUF（1 画面） | 4 slice × 1024 block、MBUS join。pipe A の plane 1 に 0–4060（PLANE_BUF_CFG 0x0fdb0000）、cursor に 4060–4096（36 block） | `dbg-i915_ddb_info.txt`、`regs-selected.txt:61` |
| DBUF（2 画面、m3） | join 無し（MBUS_CTL 0x4438c = 0x00000700）、4 slice とも on（DBUF_CTL 0x45008/0x44fe8/0x44300/0x44304 = 0xc041c000）。pipe A は S1+S2 で PLANE_BUF_CFG 0x07db0000（0–2011、slice の組の先頭からの相対）、pipe B は S3+S4 で 0x07d50000。PLANE_WM0 は両 pipe とも 0x80004010。PIPE_MBUS_DBOX_CTL A 0xa9038804、B 0xb1038804 | `m3-5330-20261007/range-0x44000.txt`、`range-0x45000.txt`、`range-0x70000.txt` |
| power well | PWR_WELL_CTL2 0x00000c03 = PW_1 と PW_A が on、PW_2 は off（1 画面の eDP） | `regs-selected.txt:68`、`dbg-i915_power_domain_info.txt` |
| GOP | UEFI の GOP は eDP を pipe A で灯して起動する。framebuffer は stolen memory、GGTT の低い番地 | `takeover.c` の N0 の log、WS237 §2.2 |
| VT-d | 起動時に GFXVTBAR を読み、translation が有効なら display を使わない（今の N0 の規則） | `takeover.c` |

5320（TGL）の実測は無い。display 12 の host 試験の期待値は Linux の表の値（T3・T5・W1 の display 12 の行）で、実機の照合は 5320（ユーザー）の確かめで代える（§6.3）。

## 2. display 11〜13 の display の model（Linux v6.8.12 の分岐から、PRM を補助に）

名前は PRM の register の名前（大文字、`_` 区切り）で書く。各節の冒頭に、手順の出典の Linux の関数（Procedure referenced に書く物）を挙げる。
platform の差は「**差（P2）**」の bullet に書き、書いていない物は display 11〜13 で共通。zedBSD の code の名前は §3。

### 2.1 ブロック図（データの流れと制御の流れ）

```
 memory ──GGTT──▶ plane 1 (pipe A..D) ──▶ pipe (src size, misc, color) ──▶ transcoder (timing, MSA, DDI func, DP transport)
                                                                                 │
                       DBUF (S1..S2 / S1..S4) ◀── watermark/DDB ──┘             ▼
                                                                 DDI (combo PHY の port / TC の port: Dekel (12, 13) か MG (11) + FIA + IOM)
 reference 19.2/24/38.4 MHz ──▶ DPLL0..n (combo) ──DPCLKA_CFGCR──▶ DDI clock ──TRANS_CLK_SEL──▶ transcoder clock
                   └──▶ TC PLL / MG PLL (port ごと) ──DDI_CLK_SEL──┘
                   └──▶ CDCLK PLL ──▶ CDCLK (pipe・DBUF・AUX の timer)
 PCH (south, ICP/TGP/ADP): raw clock 19.2/24 MHz ──▶ GMBUS (DDC), PP (panel power), SBLC PWM (backlight), SHOTPLUG (DDI/TC の HPD), SDE の割り込み
 north: DE の割り込み (pipe/port/misc/HPD TC・TBT), AUX (A..C, USBC1..6 / TBT), PSR, DMC (DC5/6/9)
 power: PW_0 (PCI, clocks, 割り込み) → PW_1 → 12: PW_2 → PW_3 (pipe B) → PW_4 (pipe C) → PW_5 (pipe D) / 13: PW_A (pipe A), PW_2 → PW_B/C/D
```

port と PHY と PLL の組は表 P1。**xd は port を「DDI の番号 + PHY の種類（combo / Dekel / MG / 無し）+ TC の番号」の platform の記述で扱い、
Linux の `enum port`/`enum phy` の 2 段の対応（`intel_port_to_phy`）は持ち込まない**（§3.1 の `struct i915_xd_port_desc`）。

### 2.2 display の init と uninit

Procedure referenced: Linux `intel_display_power.c` (icl_display_core_init, icl_display_core_uninit, gen12_dbuf_slices_config, icl_mbus_init, tgl_bw_buddy_init)、
`intel_display_wa.c` (adlp_display_wa_apply, xe_d_display_wa_apply)。PRM TGL Vol 12 "Sequences to Initialize Display" は補助。

init: (1) DC state を disable（DC_STATE_EN = 0）。(2) PCH が TGP か ADP なら SOUTH_DSPCLK_GATE_D の PCH DPMG unit の clock gate を disable（Wa_14011294188。ICP は不要）。
(3) NDE_RSTWRN_OPT の PCH reset handshake。(4) combo PHY の init（§2.7、platform の PHY の一覧）。(5) PW_1 を上げる（§2.5 の形。ADL-P は enable の前に CHICKEN_DCPR_1 の DISABLE_FLR_SRC）。
(6) CDCLK の init（§2.6: 読んで妥当なら保ち、妥当でなければ表の最小の値に設定する）。(7) DBUF: **display 12 は** 使う slice の DBUF_CTL に tracker state service = 8 を書き、
slice の power を上げる（ADL-P と ICL は tracker を書かない）。(8) MBUS の abox の credit: **display 12 と 11 は** abox 0（と display 12 の abox 1・2）に書く、**ADL-P は書かない**。
(9) BW_BUDDY（display 12・13: DRAM の種別と channel 数で page mask、表 W2。ADL-S と RKL の A0 は WA の表。display 12 は TLB req timer 0x8 も。該当が無ければ BW_BUDDY_CTL に disable）。
(10) resume なら DMC の program を載せ直す。(11) GEN11_CHICKEN_DCPR_2 の 4 bit（display 12.0〜13.0）と XELPD_DISPLAY_ERR_FATAL_MASK = ~0（display 13）。
(12) platform の WA: ADL-P は CLKGATE_DIS_5 の DPCE gating disable と CHICKEN_DCPR_1 の DDI clock reg access を消す。display 12 は `xe_d_display_wa_apply` の register（DPFC の chicken、CLKREQ_POLICY）。
uninit（DC9 の前）: DC state を disable → DMC の program を disable → DBUF を全部落とす → CDCLK の uninit（bypass へ: PLL off）→ PW_1 を落とす → combo PHY を un-init。
south の raw clock は init の前に RAWCLK_FREQ を書く（SFUSE_STRAP で 19.2/24 を選ぶ。ICP 以降は numerator の field も書く。Procedure referenced: `intel_cdclk.c` (cnp_rawclk)）。
DDI A の lane の数（DDI_BUF_CTL の DDI_A_4_LANES、PRM TGL Vol 12 "DDIA Lane Capability Control must be configured prior to enabling any ports"）は init で読んで保ち、変えない（今と同じ: レビュー 2 の m6）。

### 2.3 DisplayPort（eDP・外部 DP）の enable と disable

Procedure referenced: Linux `intel_ddi.c` (tgl_ddi_pre_enable_dp, hsw_ddi_pre_enable_dp, intel_ddi_post_disable_dp, intel_ddi_prepare_link_retrain, intel_ddi_enable_transcoder_clock)、
`intel_dp_link_training.c` (intel_dp_start_link_train 以下)、`intel_display.c` (hsw_crtc_enable, hsw_crtc_disable)。PRM TGL Vol 12 "Sequences for DisplayPort" は補助。

enable（SST、MST は使わない）:
1. 使う power well を上げる（§2.5）。Type-C は TCCOLD の block（platform の流れ: §2.8）が main link にも要る。
2. eDP は panel power を入れて on を待つ（§2.11）。
3. Type-C（TBT 以外）: DFLEXDPMLE1 に lane 数を書く（pin assignment と panel の lane 数の小さい方）。
4. port の PLL（§2.6）: combo は DPLL を立てて platform の DPCLKA の register で DDI に map し、別の書き込みで clock を on にする。TC は TC PLL（MG PLL）を立てて DDI_CLK_SEL と DPCLKA_CFGCR0 の TC clock off を消す。
5. DDI IO power（PWR_WELL_CTL_DDI の request → state）。combo は PHY が init 済みであること。
6. TC: Dekel の DP_MODE（lane 0/1、pin assignment に応じて x1/x2）。ICL は MG の DP_MODE（同じ意味、MG の register）。
7. 訓練: TRANS_CLK_SEL で port の clock を transcoder に向ける（display 13 は PHY の番号、12 は port の番号、11 は port の番号で field が違う。ICL の transcoder EDP は TRANS_CLK_SEL を書かない）→
   TRANS_DDI_FUNC_CTL の DDI select・mode select（enable は後）→ DP_TP_CTL（display 12 以降は transcoder の、11 は port の register）を enable、pattern 1 →
   voltage swing（§2.7、§2.8）→ combo: PORT_CL_DW10 で使う lane を power up → DDI_BUF_CTL を enable（ADL-P の TC は所有の bit と PHY link rate の field も）→ DDI idle status が 0 になるまで →
   DP の仕様の訓練（clock recovery → channel EQ。DPCD の 0x102/0x103..、0x202.. の status、AUX_RD_INTERVAL の待ち）→ DP_TP_CTL を normal。
   **display 11 の順は hsw の形**: PLL → DDI IO → MG の DP_MODE → buffer の準備 → lane の power → 訓練 → transcoder の clock（訓練の後に TRANS_CLK_SEL）。
8. plane・pipe・transcoder: plane の設定 → transcoder の timing・M/N・MSA → pipe の色の register（§2.10: PIPE_BOTTOM_COLOR・GAMMA_MODE・CSC_MODE は毎回書く）→
   TRANS_DDI_FUNC_CTL を enable → TRANS_CONF を enable → pipe DMC を enable（§2.12、package に pipe の firmware がある platform）→ backlight を入れる。
disable: backlight off → plane off → TRANS_CONF off → off の status を待つ（2 frame）→ pipe DMC を disable → TRANS_DDI_FUNC_CTL を DDI select none で off →
DDI_BUF_CTL off → DP_TP_CTL off → 8 µs か idle status → TRANS_CLK_SEL を none → panel power off → DDI IO power off → PLL の map を外し、不要なら PLL を止める → power well を落とす。
訓練の途中で lane 数・rate を変える時は port を disable して PLL からやり直す。失敗したら、そこまで enable した物を disable の順で戻す。
**入らない mode を拒む条件**（レビュー 2 の m5）: pixel rate が今の CDCLK に入る事、port clock が要る voltage level（`clock.c:819-840` の規則: 今の level より高い level が要るなら拒む）、
QGV の帯域（mailbox 0x10d の点の帯域に pipe の総 data rate が入る事。今の `modeset.c:3137-3141`）。3 つとも plan の段（§3.3）で判定する。

### 2.4 HDMI/DVI の enable と disable

Procedure referenced: Linux `intel_ddi.c` (tgl_ddi_pre_enable_hdmi, intel_ddi_enable_hdmi, intel_ddi_post_disable_hdmi)、`intel_dpll_mgr.c` (icl_calc_wrpll)。

enable: power well → PLL（SSC 無し、HDMI の算法: DCO ≥ 7998 MHz、Pdiv 2/3/5/7、Kdiv 1/2/3、Kdiv ≠ 2 なら Qdiv = 1）→ DDI IO power →
TRANS_CLK_SEL → plane → transcoder の timing → 色の register → TRANS_DDI_FUNC_CTL → TRANS_CONF → **最後に port**（swing → PORT_CL_DW10 の全 lane → DDI_BUF_CTL → idle が消えるまで）。
disable: plane → TRANS_CONF（off を待つ）→ TRANS_DDI_FUNC_CTL none → DDI_BUF_CTL off → 8 µs → TRANS_CLK_SEL none → DDI IO off → PLL off → power well。
HDMI は今と同じく DVI mode（infoframe・scrambling 無し、24 bpp、TMDS ≤ 340 MHz）で駆動する（§7 #1）。TC port の HDMI（Dekel の HDMI の swing の表 T2 と、ADL-P の loadgen select）は
host 試験だけで固定し、実機の確かめは一覧に載せる（5330 は TC の HDMI の adapter を持たない）。

### 2.5 power well と DC state

Procedure referenced: Linux `intel_display_power_well.c` (hsw_power_well_enable, hsw_power_well_disable, icl_tc_phy_aux_power_well_enable, tgl_tc_cold_off_power_well_enable, tgl_tc_cold_request, icl_tc_cold_exit)、
`intel_display_irq.c` (gen8_irq_power_well_post_enable, gen8_irq_power_well_pre_disable)、`intel_display_power.c` (gen9_set_dc_state, gen9_enable_dc5, skl_enable_dc6, bxt_enable_dc9)。
Values referenced: Linux `intel_display_power_map.c` (icl_power_wells, tgl_power_wells, rkl_power_wells, adls_power_wells, xelpd_power_wells)、`i915_reg.h` (ICL_PW_CTL_IDX_*, XELPD_PW_CTL_IDX_*, TGL_PW_CTL_IDX_*)。

- **well の木**は表 P1・P2。表 W1（§5.3）は platform ごとの well の一覧を**名前、CTL の instance（PWR_WELL_CTL / _AUX / _DDI）、index、fuse の有無、VGA の有無、irq_pipe_mask、
  TC/TBT の AUX か**の列で持つ。**Linux の POWER_DOMAIN の集合（`xelpd_pwdoms_*`）は持たない**（レビュー 2 の M11: software の抽象で hardware のデータではない）。
  「どの well が要るか」は xd の側で、platform の記述から hardware の単位で決める: pipe n → その pipe の well（12: PW_3/4/5、13: PW_A/B/C/D）と、その親（12: PW_2、13: PW_2。pipe A は親無し）；
  DDI n → DDI IO の well + 外部の port なら PW_2（12 は PW_2 と PW_3 を両方: Linux の `tgl_pwdoms_pw_3` が port を持つ）；AUX ch → AUX の well（TC/TBT は §2.8）；VGA → VGA を持つ well（12: PW_3、13: PW_2）。
  この対応は表 W1 の「機能」の列（PRM TGL Vol 12 "Functions Within Each Well" と 5330 の dump で自分で書く）。
- **enable の形**: request → state を待つ（PRM の 30 µs、Linux は 1 ms: 大きい方）→ fuse のある well は FUSE_STATUS の PGn を 20 µs（PW_1 は PG0 を前に、PG1 を後に）→
  **irq_pipe_mask のある well は、その pipe の DE_PIPE の IMR/IER を立て直す**（register が well の中にあり落ちると消える。今の own の `interrupts.c` の `drv_i915_pw_irq_ops`（post_enable/pre_disable、
  context と pipe mask）がこの hook で、xd-power はこれを呼ぶ: §4.2）。disable は pre_disable の hook（その pipe の割り込みを止めて drain。**drain に失敗したら well を落とさない**: `internal.h:421-424` の規則を保つ）→ request を消す → state を待つ。
- **DC5/DC6**: DMC の program を載せた後、DC_STATE_DEBUG の bit 1:0（mask cores / mask memory up）を立て、DC_STATE_EN を "up to DC6"。mode set・AUX・GMBUS の間は DC off を保持する。
  DC5↔DC6 の切り替えは disable を挟む。DC6 は DMC が載った時だけ許す（今と同じ）。DC3CO は持たない。
  **DC state の下で落ちる well は platform で違う**（12: PW_2 以下、13: PW_2・PW_A 以下、11: PW_2 以下）が、xd は「DC off を保持している間は mode set をする」規則なので、落ちる well の一覧は持たない。
- **DC9（suspend）**: DC5/6 を禁止 → 全 mode set を disable（§2.14 の quiesce）→ 割り込みを mask → SOUTH_CHICKEN1 の参照 clock の bit（今の `dc9.c` の判断。レビュー 2 の M4）→ uninit（§2.2）→ DC9 allow。
  戻り: DC9 allow を消す → init → DMC → HPD の再 arm（§2.14）。
- **AUX の power**: AUX A/B/C は PWR_WELL_CTL_AUX の request → state。**TC の AUX（USBC n / TBT n）**: request → state を待つ（TBT か legacy の時は timeout を期待し、失敗にしない: Linux の `timeout_expected`）→
  Dekel（12・13）は DKL_CMN_UC_DW27 の uc_health を 10 µs 待つ（ICL は MG の同じ意味の register）。**display 11 は legacy の TC の AUX の前に pcode の EXIT_TCCOLD（`icl_tc_cold_exit`、3 回まで、成功後 1 ms）**。
- **TGL の TC_cold_off の well**（display 12 の TGL だけ）: PWR_WELL_CTL の well ではなく **GT driver mailbox 0x26（TGL_PCODE_TCCOLD）** の block/unblock の要求で、block は
  「要求 → 応答の bit で block 中か確かめ、10 回まで 1 ms 置きに再試行」（`tgl_tc_cold_request`）。xd-power の well の表に「pcode の well」の種類として持ち、refcount は他の well と同じ。
- 今の code との差: 今は DC_OFF を 17 ms 遅らせて返す。新しい model では「display の作業の間は DC off を保持し、作業が終わったら許す」を 1 つの refcount にする（§3.4）。

### 2.6 clock

Procedure referenced: Linux `intel_cdclk.c` (bxt_cdclk_init_hw, bxt_cdclk_uninit_hw, bxt_sanitize_cdclk, bxt_set_cdclk, _bxt_set_cdclk, adlp_cdclk_pll_crawl, icl_cdclk_pll_update, bxt_get_cdclk, cnp_rawclk)、
`intel_dpll_mgr.c` (combo_pll_enable, combo_pll_disable, icl_calc_dp_combo_pll, icl_calc_wrpll, icl_calc_dpll_state, icl_get_combo_phy_dpll, mg_pll_enable, dkl_pll_write, icl_mg_pll_write, icl_calc_mg_pll_state, intel_tc_pll_enable_reg)、
`intel_ddi.c` (icl_ddi_combo_enable_clock, rkl_ddi_enable_clock, adls_ddi_enable_clock, icl_ddi_tc_enable_clock)。
Values referenced: `intel_cdclk.c` (icl_cdclk_table, rkl_cdclk_table, adlp_cdclk_table, rplu_cdclk_table, tgl_calc_voltage_level, icl_calc_voltage_level)、`intel_dpll_mgr.c` (icl_dp_combo_pll_19_2MHz_values, icl_dp_combo_pll_24MHz_values, tgl_tbt_pll_19_2MHz_values, icl_plls, tgl_plls, rkl_plls, adls_plls, adlp_plls)。PRM TGL Vol 12 "Clocks" は補助。

- 参照: DSSM が示す 19.2/24/38.4 MHz。38.4 MHz は combo DPLL が 2 で割るので 19.2 の表を使う。
- combo DPLL: DPLL_ENABLE の power → power state → DPLL_SSC（TGL の WA: CFGCR0 の SSC bit は効かず DPLL_SSC の sscen）→ DPLL_CFGCR0（DCO 整数・小数）→ DPLL_CFGCR1（Pdiv/Kdiv/Qdiv）→ 読み戻し → enable → lock（PRM 50 µs、Linux 1 ms: 大きい方）→ platform の DPCLKA の register で map → 別の書き込みで clock on。
  **差（P2）**: 38.4 MHz の小数の WA は TGL・ADL-S・ADL-P（`ehl_combo_pll_div_frac_wa_needed`。RKL と ICL は無し）。候補の PLL は TGL/ADL-P/ICL = DPLL0/1、RKL = DPLL0/1/4、ADL-S = DPLL0〜3。
  map の register は TGL/ADL-P/ICL = DPCLKA_CFGCR0（PHY ごとの 2 bit の field）、RKL = DPCLKA_CFGCR0 の RKL の field、ADL-S = PHY ごとの DPCLKA_CFGCR。
  5330 の CFGCR0 0x00e001a5 の小数の field 0x3800 は表の 0x7000 の半分で、WA と一致する（§1.3）。xd-clock は表の値と WA の両方を持ち、host 試験で 0x00e001a5/0x88 を確かめる。
- TC PLL（Dekel、display 12・13）: PLL の enable の register（TGL = MG_PLL_ENABLE の offset、ADL-P = ADLP_PORTTC_PLL_ENABLE）の power → 10 µs → Dekel の window（§2.8）で rate 非依存の register →
  rate 依存の register（Linux `icl_calc_mg_pll_state` の定数、T4）→ 読み戻し → enable → lock（900 µs）→ DDI_CLK_SEL → DPCLKA_CFGCR0 の TC clock on。HDMI は算法（div1 ∈ {7,5,3,2}、div2 ∈ {10..1}、DCO 7.992–10 GHz）。
  **MG PLL（display 11）**: 同じ算法の値を MG_* の register（REFCLKIN_CTL、CLKTOP2_CORECLKCTL1、CLKTOP2_HSCLKCTL、PLL_DIV0/1、PLL_LF、PLL_FRAC_LOCK、PLL_SSC、PLL_BIAS、PLL_TDC_COLDST_BIAS）に直に書く（window 無し、`icl_mg_pll_write`）。
- **CDCLK**: 表 T5 は platform ごと（P1）。
  **方針**: (a) init（§2.2）で読んで妥当（PLL が lock していて、CDCLK_CTL の値が表のどれかと一致し、decimal が合う）なら保つ。妥当でなければ表の最小に設定する（Linux `bxt_cdclk_init_hw` と同じ規則。VBT の既定は読まない）。
  (b) uninit（DC9 の前）で bypass（PLL off。display 12 以降の bypass = 参照/2、11 は 50 MHz）に落とす。(c) resume の init で (a)。(d) **mode set は CDCLK を変えない**: 必要な pixel rate が今の CDCLK に入らない mode は拒む（§2.3 の 3 条件）。
  set の手順（(a)(b) で使う）: pcode の SKL_PCODE_CDCLK_CONTROL の prepare → ready を待つ → PLL（**ADL-P は crawl: PLL の ratio を再 lock 無しで書き換える**。display 12・11 は PLL を disable → ratio → enable → lock）→ CDCLK_CTL（CD2X の divider と decimal）→ pcode に voltage level（SKL_PCODE_CDCLK_CONTROL）。
  **差（P2）**: voltage の段は display 12・13 = 312/326.4/556.8/652.8、11 = 312/556.8/652.8。RPL-U は `rplu_cdclk_table`。
- **pipe count の mailbox は送らない**（DG2 だけ）。今の code の `pmdemand` は xd に持ち込まない。

### 2.7 combo PHY

Procedure referenced: Linux `intel_combo_phy.c` (icl_combo_phys_init, icl_combo_phy_verify_state, icl_combo_phys_uninit, phy_is_master, has_phy_misc)、`intel_ddi.c` (icl_ddi_combo_vswing_program, icl_combo_phy_set_signal_levels)、`intel_display_power_well.c`（PORT_CL_DW10 の lane power）。
Values referenced: `intel_combo_phy.c` (icl_procmon_values)、`intel_ddi_buf_trans.c` (tgl_combo_phy_trans_*, tgl_uy_combo_phy_trans_dp_hbr2, rkl_combo_phy_trans_*, adls_combo_phy_trans_*, adlp_combo_phy_trans_*, icl_combo_phy_trans_*, icl_combo_phy_trans_hdmi)。PRM TGL Vol 12 "Combo PHY DDI Buffer" は補助。

init（PHY の一覧と master/slave は表 P1: ICL・TGL・ADL-P は A が master、RKL は A と C、ADL-S は A と D。PHY_MISC を持つ PHY: ADL-S は A だけ、RKL は A・B、他は全部。DDI A の PHY は DC6 で保たれる）:
PORT_COMP_DW0 の comp init が立っていれば済み → PORT_TX_DW8 の DCC clock select/div、PORT_PCS_DW1 の DCC mode（display 12 以降）→ PHY_MISC の comp power down を消す →
procmon の値（PORT_COMP_DW3 の voltage/process から PORT_COMP_DW1/9/10、表 T7）→ comp source（master）なら PORT_COMP_DW8 の irefgen → PORT_COMP_DW0 の comp init → PORT_CL_DW5 の CL power down enable。
voltage swing（訓練中も同じ手順）: eDP/DP なら PORT_PCS_DW1 の cmnkeeper → PORT_TX_DW4 の loadgen select（lane ごと、rate と lane 数の表）→ PORT_CL_DW5 の SUS clock 11b → PORT_TX_DW5 の training enable を消す →
PORT_TX_DW5 の scaling mode 010b、PORT_TX_DW2 の rcomp scalar 0x98、PORT_TX_DW4 の post cursor 2 = 0、PORT_TX_DW5 の rterm 110b・3 tap disable・cursor program 0・coeff polarity 0、表の swing/pre-emphasis の値（PORT_TX_DW2 の swing select、PORT_TX_DW4 の cursor coeff・post cursor 1、PORT_TX_DW7 の n scalar）→ training enable を立てる。
表 T1 は platform ごとの Linux の表を第一の出典にする（TGL は `tgl_*`（UP3/UP4 の HBR2 は `tgl_uy_*`）、RKL、ADL-S、ADL-P、ICL）。oracle は 5330 の m3 の `range-0x160000.txt`（combo PHY A: 0x162000–）と `range-0x16C000.txt`（PHY B）の PORT_TX/PCS/CL/COMP の語（ADL-P の行だけ）。

### 2.8 Type-C PHY（Dekel / MG）と FIA・IOM

Procedure referenced: Linux `intel_tc.c` (adlp_tc_phy_hpd_live_status, adlp_tc_phy_is_ready, adlp_tc_phy_take_ownership, adlp_tc_phy_is_owned, adlp_tc_phy_connect, adlp_tc_phy_disconnect, adlp_tc_phy_cold_off_domain,
icl_tc_phy_hpd_live_status, icl_tc_phy_is_ready, icl_tc_phy_take_ownership, icl_tc_phy_is_owned, icl_tc_phy_connect, icl_tc_phy_disconnect, icl_tc_phy_cold_off_domain, tgl_tc_phy_cold_off_domain, tgl_tc_phy_init, icl_tc_phy_init, tc_phy_load_fia_params,
tc_phy_verify_legacy_or_dp_alt_mode, intel_tc_port_get_lane_mask, intel_tc_port_get_pin_assignment_mask, intel_tc_port_set_fia_lane_count, intel_tc_port_lock, intel_tc_port_unlock, intel_tc_port_disconnect_phy_work, intel_tc_port_get_link, intel_tc_port_put_link)、
`intel_dkl_phy.c` (dkl_phy_set_hip_idx, intel_dkl_phy_read, intel_dkl_phy_rmw)、`intel_ddi.c` (tgl_dkl_phy_set_signal_levels, icl_mg_phy_set_signal_levels, icl_program_mg_dp_mode)。
Values referenced: `i915_reg.h` (DDI_BUF_CTL_TC_PHY_OWNERSHIP, TCSS_DDI_STATUS, PORT_TX_DFLEXDPSP, PORT_TX_DFLEXDPPMS, PORT_TX_DFLEXDPCSSS, PORT_TX_DFLEXPA1, PORT_TX_DFLEXDPMLE1, MODULAR_FIA_MASK, GEN11_DE_HPD_ISR の TC/TBT の bit, SDE_TC_HOTPLUG_ICP, TGL_PCODE_TCCOLD, ICL_PCODE_EXIT_TCCOLD)、`intel_dkl_phy_regs.h`、`intel_ddi_buf_trans.c` (tgl_dkl_phy_trans_*, adlp_dkl_phy_trans_*, icl_mg_phy_trans_*)。
PRM TGL Vol 12 "TypeC PHY DDI Buffer"・"Type-C Hot Plug Detection"・"Connect Flows"・"TCCOLD" は補助（display 12 の TGL の流れの意味）。

- 構成: PHY は 2 lane、各 lane に TX1 と TX2。DP の x1/x2 は DP_MODE で lane ごとに選ぶ。Dekel の register は port ごとの window（HIP_INDEX_REG で bank を選び、1 つの lock の下で index → access）。MG の register は直に読み書きする。
  FIA: TGL は DFLEXDPSP(FIA1) の modular の bit で「modular（TC1/2 = FIA1、…）か 1 つの FIA か」を決める。ADL-P は modular 固定。ICL は FIA1 だけ。
- **2 つの流れ**（xd-tc は platform の記述で選ぶ。状態機械（§3.5）と lock の規則（§3.4）は共通）:
  - **流れ A（ADL-P 系）**: live = DE_HPD_ISR の TC の bit → DP-alt、TBT の bit → TBT、SDEISR の TC の bit → legacy（FIA の DFLEXDPSP の live state は読まない）。ready = TCSS_DDI_STATUS(tc_port) の READY（0xffffffff が読めたら TCCOLD の中、not ready）。
    所有 = DDI_BUF_CTL の bit 6（TC_PHY_OWNERSHIP）の rmw、読んで確かめる。TCCOLD の block = DP-alt と legacy は**その port の AUX USBC の well を上げる事**、TBT は AUX TBT の well。mailbox は使わない。
  - **流れ B（TGL、ICL）**: live = DFLEXDPSP(FIA) の TBT/TC の live state の bit（0xffffffff なら TCCOLD の中で何も無し）と SDEISR の TC の bit → legacy。ready = DFLEXDPPMS の PHY mode status completed。
    所有 = DFLEXDPCSSS の PHY mode status not safe の bit を書く（0xffffffff なら失敗）。TCCOLD の block = **TGL: TC_cold_off の pcode の well（§2.5）を TBT/DP-alt/legacy の全部で**、**ICL: legacy は AUX の well（+ EXIT_TCCOLD の pcode）、TBT/DP-alt は well 無し（TC_COLD_OFF の domain は always-on 扱い）**。
    この流れは今の code に無い（TGL の TC は未対応: §1.1）ので、**新しい機能**として p010 で host 試験だけで作り、実機は 5320（ユーザー）の一覧に載せる。
- connect（DP-alt・legacy）: port の power（DDI IO）を取る → TCCOLD を block → ready を確かめる → 所有を取る（流れ A は所有 → ready の順、B は ready → 所有）→ lane の数（DFLEXDPSP の lane assignment の mask）と pin assignment（DFLEXPA1）を読み、要る lane 数が入るか確かめる（legacy は 4 lane 固定）→ 失敗したらその段まで戻す。
  TBT: TCCOLD の block（流れ A は AUX TBT の well、B の TGL は pcode の well）だけ。
- disconnect: 所有を返す（DP-alt・legacy）→ TCCOLD の block を外す。**disconnect は link の refcount が 0 になってすぐではなく、unlock の時に 1 s の遅延 work で行う（その間に再び lock されれば取り消し）**。AUX の transaction と mode set の disable の後にこの道を通る。
- lane 数: enable の前に DFLEXDPMLE1 に ML の mask（lane 1/2/4 と lane reversal）を書く。
- swing: Dekel は DKL_TX_PMD_LANE_SUS = 0、DKL_TX_DPCNTL0/1 の preshoot/de-emphasis/vswing（表 T2: TGL の表と ADL-P の表）、DKL_TX_DPCNTL2 の DP20BITMODE を消す、**ADL-P は DPCNTL2 の loadgen select（HDMI は lane ごとの値、DP は 0）**。MG は MG_TX*_SWINGCTRL/DRVCTRL の値（ICL の表）。
- hotplug: north の TC/TBT の HOTPLUG_CTL と DE_HPD の割り込み、south の SHOTPLUG_CTL_TC と SDE。reset・power state の後は「HPD を enable → 各 TC port の live state を読んで connect/disconnect を処理」。
  **RKL・ADL-S の TC1〜4 は combo PHY の普通の port**（TC の流れ無し）だが、HPD の pin は TC の pin（north の TC の bit と south の TC の bit の両方を arm する: Linux の `hpd_gen11`/`hpd_icp` と同じ）。
- 今の code との一致: 今の `tc.c` は流れ A（5330 で動いている実績）。初版の §7 #4（PRM の DFLEXDPPMS/DPCSSS の handshake か）は決定 4 で閉じた: ADL-P は流れ A、TGL・ICL は流れ B。

### 2.9 AUX と GMBUS

Procedure referenced: Linux `intel_dp_aux.c` (intel_dp_aux_xfer, intel_dp_aux_wait_done, skl_get_aux_send_ctl)、`intel_gmbus.c` (gmbus_xfer, do_gmbus_xfer, gmbus_wait, gmbus_xfer_read_chunk, gmbus_xfer_write_chunk, intel_gmbus_reset, get_gmbus_pin)。
Values referenced: `intel_dp_aux_regs.h`、`intel_gmbus_regs.h`、`intel_gmbus.c` (gmbus_pins_icp)。PRM TGL Vol 12 "AUX programming sequence"、ACM Vol 12 "GMBUS and GPIO" は補助。

- AUX: display が init 済み・AUX の power が入り・PSR が止まり・DC off を保持した状態で、DDI_AUX_DATA_*_[0-4] に message（header + data、big endian の pack）→ DDI_AUX_CTL に **timeout = 最大（TIME_OUT_MAX = 3 → 4000 µs。PRM の最小 600 µs より長い方）**・message size・sync pulse 32 / fast 18・START（bit 31）→ bit 31:30 が 01 になるまで待つ（busy の待ちは 10 ms）→ bit 25（receive error）なら 1 を書いて消し data を読まない → DATA を読む → done を消す。
  TC port は DDI_AUX_CTL の IO select（legacy/TBT）。再試行は今の規則（§2.13 の末尾）を保つ。
- GMBUS（PCH）: GMBUS0 の pin pair（**GMBUS の pin: 1 = DDI A、2 = DDI B、3 = DDI C、9..14 = TC1..6**（`gmbus_pins_icp`、PCH ICP/TGP/ADP で同じ）。VBT の DDC pin とは別の番号で、対応は platform ごとの表 V3: §2.13）と速度（100 kHz）→ GMBUS1 に command（index、byte count、slave address、read、START/STOP の cycle）→ GMBUS2 の HW_RDY/HW_WAIT/SATOER/ACTIVE を待つ（1 回の待ち 50 ms）→ GMBUS3 を 4 byte ずつ → 終わりに GMBUS1 の STOP、GMBUS0 を 0 で解放。
  **NAK（SATOER）**: bus が idle になるのを待ってから SW_CLR_INT で片付け、GMBUS0 を解放する。**最初の message が NAK されたら 1 回だけ再試行**。それでも NAK なら失敗（ENXIO 相当）として返す。
  EDID の segment は bit-bang が要るが 256 byte までは不要。

### 2.10 transcoder・pipe・plane・DBUF・watermark

Procedure referenced: Linux `intel_display.c` (intel_set_transcoder_timings, intel_set_pipe_src_size, intel_set_m_n, hsw_set_transconf, hsw_enable_transcoder, intel_disable_transcoder, intel_ddi_set_dp_msa, intel_enable_transcoder の PIPE_ARB_CTL)、
`intel_color.c` (icl_color_commit_arm)、`skl_universal_plane.c` (skl_plane_update_noarm, skl_plane_update_arm, skl_plane_disable_arm, adlp_plane_ctl_arb_slots)、
`skl_watermark.c` (skl_compute_plane_wm, skl_compute_wm_levels, skl_compute_transition_wm, tgl_compute_sagv_wm, skl_allocate_plane_ddb, skl_ddb_entry_for_slices, mbus_ddb_offset, skl_write_plane_wm, intel_mbus_dbox_update, intel_dbuf_pre_plane_update, intel_dbuf_post_plane_update, check_mbus_joined, skl_compute_dbuf_slices, skl_setup_wm_latency, skl_read_wm_latency, skl_wm_init, intel_enable_ipc, skl_need_wm_copy_wa)。
Values referenced: `skl_watermark.c` (icl_allowed_dbufs, tgl_allowed_dbufs, adlp_allowed_dbufs, skl_cursor_allocation の 36 block)、`i915_reg.h` (PLANE_*, PIPE_MISC, PIPE_ARB_CTL, PIPE_BOTTOM_COLOR, MBUS_CTL, DBUF_CTL_S, PIPE_MBUS_DBOX_CTL, DISP_ARB_CTL2, TRANS_SET_CONTEXT_LATENCY)。PRM TGL Vol 12 "Transcoder"・"Pipe"・"Planes"・"Display Watermark Programming" は補助。

- transcoder: TRANS_HTOTAL/HBLANK/HSYNC/VTOTAL/VBLANK/VSYNC（13 bit × 2、値 − 1）、TRANS_VSYNCSHIFT、**display 13 は TRANS_SET_CONTEXT_LATENCY に（vblank start − vdisplay）を書き VBLANK の start は 0**、
  DP の TRANS_DATAM1/N1、TRANS_LINKM1/N1（N の書き込みで arm）、TRANS_MSA_MISC、TRANS_DDI_FUNC_CTL（enable、DDI select、mode select、bpc、sync、port width。display 12 以降は transcoder = pipe。**display 11 の eDP は transcoder EDP で、EDP の input select（pipe A/B/C）を書く**）、TRANS_DDI_FUNC_CTL2 = 0、TRANS_CONF（enable、state）。
- pipe: PIPE_SRCSZ、PIPE_MISC（bpc、dither）、PIPE_MISC2 は触らない、scaler は使わない（PS_CTRL off のまま）、**PIPE_BOTTOM_COLOR = 0・GAMMA_MODE（10 bit、LUT 無し）・CSC_MODE（0）は mode set のたびに書く**、PIPE_FRMCNT と PIPE_SCANLINE（観測）、**display 13 は TRANS_CONF の enable の前に PIPE_ARB_CTL の prog slots を立てる**（Wa_22012358565）。
- plane 1: PLANE_CTL（enable、format XRGB8888、linear、rotation 0、**display 13 は ARB slots（XRGB8888 は 1）**）、PLANE_STRIDE、PLANE_POS、PLANE_SIZE、PLANE_OFFSET、PLANE_COLOR_CTL、PLANE_WM_0..n、PLANE_WM_TRANS、PLANE_WM_SAGV（display 13）、PLANE_BUF_CFG、**PLANE_SURF（書くと arm、vblank で反映、PLANE_SURFLIVE が今の番地）**。flip は PLANE_SURF だけを書き、FLIP_DONE の割り込み（DE_PIPE の bit 3）か SURFLIVE で確かめる（今と同じ）。
- watermark: WM_LINETIME = roundup(8·htotal/pixel rate MHz)。level ごとに method 1 / method 2 の規則と **選択の規則は Linux の `skl_compute_plane_wm`（決定 4: PRM の選択規則と違う所は Linux）**→ blocks・lines。latency は GT driver mailbox 0x6（data 0: level 0–3、data 1: level 4–7。**display 12 以降は全 level に 3 µs、11 は 2 µs を足す**（`adjust_wm_latency` の read latency）、level 0 が 0 なら WaWmMemoryReadLatency）。
  **差（P2）**: level は display 13 が 6、他は 8。SAGV の WM は display 13 が HW（PLANE_WM_SAGV に level 0 + SAGV の block time の値）、display 12 は software（同じ値を level 0 に使い SAGV を止めたままにする）、11 は無し。無効な level の語は level 0 の写し（`skl_need_wm_copy_wa`: display 11〜13 の WA）。transition WM の式（T6）は display 11 以降で min 4。SAGV は mailbox 0xe で止めたまま。
  **IPC**: DISP_ARB_CTL2 の IPC enable を init で立てる（display 11〜13 とも has_ipc）。
- **DBUF と MBUS**: 表は platform ごと（P2）。**ADL-P**: pipe が 1 つ（A か B。表に join の行がある物）なら S1–S4 を join（MBUS_CTL の join と hashing 1x4）、A+B なら A が S1+S2・B が S3+S4、C は S3+S4。**display 12**: 1 pipe は S1+S2、A+B は A=S2・B=S1、join 無し。**display 11**: 1 pipe は 1 slice（A は S1、C は S2）、A+B は S1/S2。
  **方針**: 1 画面は GOP と Linux と同じ割り当てで灯し、head（2 画面目）を灯す時に **resident の DDB を 2 画面の割り当てに書き換え（ADL-P は MBUS の join を解き）、残りの slice を head に充てる**。この「1 → 2 画面」の遷移は、Linux の `intel_dbuf_pre_plane_update`/`post_plane_update` と同じ順
  （先に新しい slice の power を上げ、MBUS_CTL の join を消し、resident の PLANE_BUF_CFG/WM を vblank に同期して書き、PIPE_MBUS_DBOX_CTL の credit（platform の値）を更新する。2 → 1 画面はその逆）で、xd-dbuf が「並ぶ pipe の集合」を入力に取って行う（§3.3 の plan に `run_pipes` の入力、§4.2 の契約）。
  今の code の「resident を 2 pipe 用に灯し直す」（`present.c` の "lit again for two pipes"）と同じ結果を、plane の再 program（DDB・WM・DBOX の書き換え）だけで出す。
  join していない時の PLANE_BUF_CFG の start/end は**その pipe の slice の組の先頭からの相対**（Linux `mbus_ddb_offset`。m3 の pipe B の 0x07d50000 がそれを示す）。
  cursor の 36 block は Linux と同じく予約だけして使わない（cursor plane は disable のまま）。DBUF_CTL の request → state（10 µs）、使わない slice は落とす。
- 割り込み: DISPLAY_INT_CTL の bit 31 を落として読み、DE_PIPE_IIR（vblank bit 0、FLIP_DONE bit 3、underrun）、DE_PORT_IIR（AUX done）、DE_MISC、DE_HPD（TC/TBT）、SDEIIR（PCH: DDI/TC の HPD、GMBUS）を ack し、bit 31 を戻す（今の own の `interrupts.c` と同じ。構造は保ち、入口の関数名だけ xd に向ける: §4.2）。

### 2.11 panel power・backlight・PSR

Procedure referenced: Linux `intel_pps.c` (intel_pps_init_delays, pps_init_registers, intel_pps_on_unlocked, intel_pps_off_unlocked, intel_pps_vdd_on_unlocked, intel_pps_vdd_off_sync_unlocked, wait_panel_on, wait_panel_off, wait_panel_power_cycle)、
`intel_backlight.c` (pch_setup_backlight, pch_enable_backlight, pch_disable_backlight, pch_set_backlight, pch_get_backlight, cnp_setup_backlight, cnp_enable_backlight, cnp_disable_backlight)。
Values referenced: `intel_pps_regs.h`、`intel_backlight_regs.h`。ACM Vol 12 "Panel Power and Backlight"、TGL Vol 12 "Panel Self Refresh" は補助。

- panel power（PCH の PP_CONTROL/PP_STATUS/PP_ON_DELAYS/PP_OFF_DELAYS。PPS 0 が 0xC7200、index 1 は 0xC7300 で SOUTH_CHICKEN1 の bit が要る。PP_CONTROL の power cycle delay は bit 8:4）: VBT の eDP の T1..T12 と既定。power on: VDD force on → power on → PP_STATUS の on を待つ（T1+T3）→ 訓練 → backlight（T8 の後）。off: backlight off（T9）→ power off → off を待つ → power cycle delay（T12）。VDD の単独 on は AUX の間だけ。
  **PP_ON_DELAYS/PP_OFF_DELAYS と PP_CONTROL の power cycle delay は毎回の enable で書く**（firmware が書いた値に頼らない。resume の後も同じ: レビュー 2 の m12）。
- backlight（PCH の SBLC_PWM_CTL1/FREQ/DUTY = 0xC8250/54/58。**PWM の clock は raw clock（19.2 か 24 MHz、§2.2）**、increment 1）: FREQ に周期 = raw clock / VBT の PWM 周波数（5330: 19.2 MHz / 200 Hz = 96000 = 0x17700）、DUTY に duty、CTL1 で enable（bit 31）と極性（bit 29）。周波数の変更は PWM を止めて書く。**FREQ は毎回の enable で書く**（m12）。
  PCH の 2 つ目の PWM controller（CTL1 の index 1）は VBT の controller の番号で選ぶ（今の code と同じ規則）。
- PSR: **使わない**（今も off）。SRD_CTL/PSR2_CTL は読んで、立っていれば disable の手順で止める。

### 2.12 DMC

Procedure referenced: Linux `intel_dmc.c` (intel_dmc_load_program, dmc_load_work_fn, parse_dmc_fw, parse_dmc_fw_header, parse_dmc_fw_package, parse_dmc_fw_css, disable_all_event_handlers, disable_dmc_evt, adlp_pipedmc_clock_gating_wa, intel_dmc_enable_pipe, intel_dmc_disable_pipe, intel_dmc_disable_program)。
Values referenced: `intel_dmc_regs.h` (DMC_PROGRAM, DMC_HTP, DMC_EVT_CTL, _ADLP_PIPEDMC_REG_MMIO_BASE_A, _TGL_PIPEDMC_REG_MMIO_BASE_A, PIPEDMC_CONTROL, DMC_MMIO_START_RANGE/END_RANGE, DMC_V1_MMIO_START_RANGE, TGL_MAIN_MMIO_START, DMC_EVT_CTL_EVENT_ID_CLK_MSEC, DMC_EVT_CTL_EVENT_ID_VBLANK_A)、`i915_reg.h` (CLKGATE_DIS_PSL_EXT, PIPEDMC_GATING_DIS)、`intel_dmc.c` (ICL/TGL/RKL/ADLS/ADLP の path と max fw size)。PRM TGL Vol 12 "DMC Firmware Package" は補助。

package = CSS header（128 byte、module type 9、version）→ package header（**v2（display 12・13）**: 32 entry、dmc id 0 = main、1..4 = pipe A..D、stepping/substepping の wildcard、offset は dword。**v1（display 11）**: 1 entry、main だけ）→
各 firmware（**DMC header v3**: signature 0x40403E3E、fwSize、**startMMIOAddr**、mmioCount ≤ 20 と addr/data。**v1（ICL）**: start は 0x80000 固定、mmioCount ≤ 8）。
載せ方: **(1) display 13 は CLKGATE_DIS_PSL_EXT の PIPEDMC gating disable を pipe A..D に立てる（Wa_16015201720: pipe A/B は常時、C/D は載せる間だけ）** → (2) **display 12・13 は DMC_EVT_CTL の全 handler（main と package にある pipe DMC、8 個ずつ）を disable（event id FALSE、edge 0→1）**（11 は行わない）→
(3) payload を **header の startMMIOAddr（main の program storage。TGL の main は 0x8F000 ではなく header の値）** から dword で書く（レビュー 2 の M2: 0x8F000 からは main の event handler の register で、program ではない）→
(4) header の MMIO pair を書く（main は 0x80000–0x8FFFF、pipe は platform の base（ADL-P 0x5F000、TGL 0x92000）からの範囲に限り、範囲外は package を拒む。pipe DMC の EVT_CTL は常に disable の語、**TGL は main の CLK_MSEC の handler も、TGL・ADL-S は VBLANK_A の handler も disable の語に差し替える**）→ (5) DC_STATE_DEBUG → (6) display 13 は pipe C/D の gating disable を戻す。
**pipe DMC は TRANS_CONF の enable の後に PIPEDMC_CONTROL(pipe) の enable を立て、disable の前に消す**（package に pipe の firmware がある時だけ: ADL-P の v2.20 は pipe A/B、TGL の v2.12 は main だけ（package の entry で判定））。
stepping は PCI revision から（表 S1、platform ごと）。firmware が無ければ DC5/6 を許さず、他は動く（今と同じ）。resume は載せ直し（§2.2 の (10)）。

### 2.13 VBT・OpRegion・EDID・DPCD（PRM に無い物）

Procedure referenced: Linux `intel_opregion.c` (intel_opregion_setup の mailbox 4 / RVDA の規則)、`intel_bios.c` (parse_general_features, parse_general_definitions, parse_ddi_ports, parse_edp, parse_lfp_panel_dtd, parse_lfp_backlight, parse_lfp_data, map_ddc_pin, map_aux_ch, dvo_port_to_port)。
Values referenced: `intel_vbt_defs.h`（block id、field の offset と幅、version の境、`ADLP_DDC_BUS_PORT_TC1..4`、`ADLS_DDC_BUS_PORT_TC1..4`、`RKL_DDC_BUS_DDI_D/E`、`ICL_DDC_BUS_*`、`TGL_DDC_BUS_*`）、`intel_bios.c` (icp_ddc_pin_map, rkl_pch_tgp_ddc_pin_map, adls_ddc_pin_map, adlp_ddc_pin_map, dvo_port_to_port の port_mapping/rkl_port_mapping/adls_port_mapping/xelpd_port_mapping)、`intel_opregion.c`（header と mailbox の offset）、`include/drm/display/drm_dp.h`（DPCD の address と bit）。

- OpRegion: PCI config 0xFC（ASLS）→ 8 KiB、signature "IntelGraphicsMem"、mailbox 4（VBT 6 KiB）0x400、2.1+ では RVDA/RVDS で mailbox 4 の外にもある。runtime（ACPI の notify、ASLE の backlight 要求、DIDL/CADL）は今も使っていないので持たない。
- VBT: header（"$VBT"、version）→ BDB（"BIOS_DATA_BLOCK"）。使う block: 1 general features、2 general definitions（child device）、12 driver features、27 eDP、40 LFP options、41/42 LFP data、43 LFP backlight。
  **表 V2（DVO port → DDI、platform ごと）**: 共通（TGL・ICL）: A/B/C → DDI A/B/C、D/E → DDI D/E、F/G/H/I → TC1..4。RKL: C/D → TC1/TC2。ADL-S: B/C/D/E → TC1..4。ADL-P: F/G/H/I → TC1..4（D/E は無い）。
  **表 V3（VBT の DDC pin → GMBUS の pin、platform ごと）**: ADL-P: 1 → 1（DDI A）、2 → 2（DDI B）、3/4/5/6 → 9/10/11/12（TC1..4）。ADL-S: 1 → 1、TC1..4 の値 → 9..12。RKL（TGP）: 1 → 1、2 → 2、DDI D/E の値 → 9/10。TGL・ICL（ICP の表）: DDI A/B/C と TC1..6 の値 → 1/2/3 と 9..14。AUX channel の値（0x40 → A、0x10 → B、0x60..0x90 → USBC1..4 …）。
  xd-vbt が返す物（今の code が使う物）: port の child（kind、AUX、GMBUS の pin、lane reversal、TC/TBT、HDMI level shift、DP の max rate/lanes）、panel type、LFP の DTD、backlight（PWM Hz、controller、min、極性）、eDP（bpp、PPS の T、rate/lanes、low vswing、HOBL）、SSC と AFC startup。VBT が無い時の既定（port A = eDP、B = DP+HDMI）も保つ。
  **fixture は合成**（§5.1、§7 #7）: Linux の layout の表（V1）から、使う 8 block だけを持つ VBT の bytes を試験の code が組み立てる（5330 の値: panel 1920×1080、PWM 200 Hz、eDP 2 lane HBR、DVO の port A = eDP・B = HDMI・F/G = DP）。実物の bytes は tree に置かない。
- EDID: VESA E-EDID 1.4 の base block（checksum、detailed timing × 4、物理の大きさ、bpc）、CEA-861 の拡張（HDMI の VSDB、SVD）。今の own の `output.c` の detailed timing の decode と CVT-RB は保つ。
- DPCD: 0x000.. の receiver caps、0x010.. の eDP 1.4 の supported link rates（今の code は eDP ≥ 1.4 を拒む。xd はこの表を読む。5330 の panel は eDP 1.2 で動作は変わらない）、0x100.. の link config、0x200.. の status、0x600 の power、0x700.. の eDP の表示制御、0xF0000 の LTTPR（読むだけ）。
- 今の AUX の規則で保つ物: 1 transaction は 20 byte まで、timeout は 4 ms（TIME_OUT_MAX）、sync pulse 32/fast 18、busy の待ち 10 ms、再試行 5 回（timeout は即、receive error は 400–500 µs 後）、native の再試行 32 回、I2C-over-AUX は 16 byte 単位で DEFER は 7 回まで、eDP の AUX の前に VDD を入れる、TC port は所有中だけ。GMBUS で保つ物: 100 kHz、read は 511 byte まで、待ちは 50 ms、NAK は §2.9 の規則、bit-bang の fallback は無し。EDID で保つ物: block ごとに 4 回まで、checksum、header の 8 byte、segment は block 2 から。

### 2.14 GOP の引き継ぎと suspend の戻り

Procedure referenced: Linux `intel_display_driver.c` (intel_display_driver_probe_nogem の intel_vga_disable、intel_display_driver_suspend、__intel_display_driver_resume)、`intel_vga.c` (intel_vga_disable)、`i915_driver.c` (i915_drm_suspend, i915_drm_resume の順)。

- 引き継ぎ: 起動時に（display への最初の書き込みの前に）PWR_WELL_CTL2・TRANS_CONF・TRANS_DDI_FUNC_CTL・PIPE_SRCSZ・PLANE_CTL/STRIDE/SIZE/SURF・DPLL_ENABLE・DPCLKA_CFGCR・DDI_BUF_CTL・PP_STATUS/CONTROL・PWM・VGA_CONTROL を**読むだけ**で、GOP が灯した pipe と port を知り、framebuffer の GGTT の範囲が driver の書く範囲と重ならないかを確かめ、VT-d の状態を見て「使う／使わない」を決める（今の N0 の規則をそのまま保つ: §3.6）。
  GOP の出力の分類（今の `output.c:347-372` の規則を保つ）: eDP → 止めて自分の mode set。DP-TC → sink を AUX で確かめてから GOP の出力を止める、駆動できなければ GOP の絵を残す。HDMI → sink の応答を待ってから。対応しない出力（RKL・ADL-S の TC の port を含めて xd が駆動できない物）→ GOP の絵を残し display を使わない。
  使うと決めた後、最初の表示の前に: **(1) legacy VGA plane を止める（VGA_CONTROL 0x41000 の bit 31、その前に VGA の sequencer の SR01 screen off）**、(2) GOP の出力を §2.3 の disable の順で止める（plane → transcoder → port → PLL → well）、(3) 自分の mode set（pipe の色の register を含めて全部書く）。Linux のように状態を読み出して atomic の state を再構成する事はしない。GOP の framebuffer の memory には書かない。
  **xd の start が takeover の前に失敗した時（platform の記述が無い、init の well が上がらない、DMC の parse の失敗は除く）は GOP の絵を残して display を使わない**（自動の逃げ道。レビュー 2 の M17）。
- **suspend**: (1) hotplug を止める（HPD の割り込みを mask、worker の pending を捨て、走っている probe を待つ）、(2) 全 screen を暗くする（display window を抜け、head は dark、resident は dark）、(3) TC の link を返し disconnect（遅延の work は待たずに同期で）、(4) DC off を保持 → uninit（§2.2）→ DC9 allow。
- **resume**: DC9 allow を消す → init（§2.2、CDCLK の sanitize、DMC の載せ直し）→ **HPD を再 arm（pin の enable、SHPD_FILTER_CNT（PCH ≤ TGP は 500 adj、ADP は 250）、HOTPLUG_CTL、IMR/IER）→ 全 connector の live state を読み直して connect/disconnect を処理（resume の間に抜き差しされた物）** → 次の表示で mode set。保存する register は無い（新しい mode set で全部書く。PPS の delay と PWM の FREQ も書き直す: §2.11）。
  **sleep の後に外部の出力が戻らなければ panel に戻す**（今の `present.c:766-777`、ws052-p009 の規則）は own の `present.c` の規則で、xd はその判断に要る「戻った／戻らない」の結果を §4.2 の契約で返す。
## 3. zedBSD の自前の構造

### 3.1 名前と置き場所、platform の記述

- 接頭辞 **`xd`**（PRM の "XeD" = Xe Display から。Linux の `xelpd`/`intel_` は使わない）。file は `src/drivers/gpu/i915/display/xd-<役割>.c/.h`、公開の関数 `drv_i915_xd_<役割>_<動詞>()`、static は `i915_xd_...`（既存の規則）、型 `struct i915_xd_<物>`、定数 `I915_XD_*`。
- register と表のデータは `src/drivers/gpu/i915/intel/display-regs-xd.h`（north）、`intel/display-regs-xd-south.h`（PCH）、`intel/display-tables-xd.h`（PLL・CDCLK・buf trans・DBUF・procmon・latency・well・stepping の表）、`intel/xd-vbt-layout.h`、`intel/xd-opregion-layout.h`、`intel/xd-dpcd.h`（使う address だけ）。既存の `intel/*.h`（`mreg.h`・`trans.h`・`dp.h`・`plane.h`・`vbt-defs.h` …）は新しい code からは include しない（WS244 が Gen9 の利用者ごと整理・削除する）。
- 1 つの集約の header **`display/xd.h`** が、own の file（present・head・output・control・backlight・display・scanout・bootfb・interrupts・device・tc-kern の後継）が使う型と関数を全部持つ（design policy §2.6: 公開 header は少なく、意図して包括的に。実装は口の下で丸ごと置き換えられる）。他の `xd-*.h` は xd の中だけで include する。
- **own の型の分離（p002）**: 今の `internal.h` には Gen9 と own の file が使う own の型（`i915_display`・`i915_scanout`・`i915_resident_display`・`i915_display_irq`・`i915_irq_vblank`・`i915_pch_state`・`i915_pw_irq_ops`・`i915_present_window`、mode の型）と Linux 由来の型が同居している（2026-10-15 の照合: `gen9-node.c` は `i915_lcd_mode`・`i915_resident_display`・`i915_scanout`、`gen9-backlight.c` は `i915_pch_state`、`gen9-flip.c` は `i915_scanout` を使う）。
  own の型は `display/display-internal.h`（own、Gen9 と xd の両方が include）に移し、mode の型は `struct i915_display_mode`（own。xd がそのまま使う）にする。Linux 由来の型は `internal.h` に残して旧 code の削除の Phase で file ごと消す。
- **platform の記述（この改訂で足した物。Linux の `intel_display_device_info` の feature flag の表は写さず、§1 の表 P1 を hardware の単位で持つ）**:
  ```c
  struct i915_xd_port_desc { uint8_t ddi; uint8_t phy_kind; /* COMBO, DEKEL, MG, NONE */ uint8_t phy; uint8_t tc; /* 0 = TC の流れ無し, 1..6 */ uint8_t aux; uint8_t hpd_north, hpd_south; uint8_t comp_master; };
  struct i915_xd_platform { uint8_t display_ver; uint8_t kind; /* ICL, TGL, RKL, ADLS, ADLP */ uint8_t pipes; uint8_t tc_flow; /* NONE, FIA (11/12), ADLP (13) */ uint8_t tccold; /* NONE, PCODE_WELL (TGL), AUX_WELL (ADLP), AUX_WELL_EXIT (ICL) */
                             const struct i915_xd_port_desc *ports; unsigned nports; const struct i915_xd_well_desc *wells; unsigned nwells; const struct i915_xd_pll_desc *plls; unsigned nplls; uint8_t pll_map_kind; /* CFGCR0_ICL, CFGCR0_RKL, CFGCR_ADLS */
                             const struct i915_xd_cdclk_row *cdclk; unsigned ncdclk; const uint16_t *voltage_steps; unsigned nvoltage; int cdclk_crawl; int frac_wa_38p4; const struct i915_xd_dbuf_rule *dbuf; unsigned ndbuf; uint8_t dbuf_slices; int mbus_join; const struct i915_xd_dbox_credit *dbox;
                             uint8_t wm_levels; uint8_t wm_read_latency_us; int hw_sagv_wm; const char *dmc_path; uint8_t dmc_header; uint32_t pipedmc_base; const struct i915_xd_step_row *steps; unsigned nsteps; uint8_t pch_kind; /* ICP, TGP, ADP */ uint8_t vbt_dvo_map, vbt_ddc_map; uint8_t swing_tables; unsigned wa; /* bit の集合: §2.2 の (11)(12)、PIPE_ARB_CTL、PIPEDMC gating、DMC の handler の差し替え … */ };
  ```
  `drv_i915_xd_platform_of(product id, revid, pch id)` が表 P1 の行（`intel/display-tables-xd.h` の `static const struct i915_xd_platform` × 5）を返す。**手順の中の platform の分岐はこの記述の field（`tc_flow`、`cdclk_crawl`、`mbus_join`、`wa` の bit …）で書き、`kind` で分岐する所は無い**（Linux の `IS_ALDERLAKE_P(...)` の形を持ち込まない）。
  PCI ID の表は今の `device.c` の `i915_product_listed` に RKL・ADL-S/RPL-S の群を足す（Linux の `INTEL_RKL_IDS`・`INTEL_ADLS_IDS`・`INTEL_RPLS_IDS`。`device.c` は own の file。p002）。PCH の ID は今の `drv_i915_detect_pch` に ICP/TGP/ADP の表（Linux `soc/intel_pch.c` の値）を持たせる。

### 3.2 module と責務（境界は hardware の単位で決め、Linux の file の分け方は持ち込まない）

| module | hardware の単位（境界の根拠） | 責務 | 持つ状態 | 呼ぶ物 |
| --- | --- | --- | --- | --- |
| `xd-io` | MMIO と GT driver mailbox（register の窓口。Linux は `intel_de.h`・`intel_uncore`・`intel_pcode.c` の 3 つだが、zedBSD では 1 つの「書き込みの記録を持つ窓口」） | read/write/rmw、posting read、`poll(reg, mask, value, timeout_us)`、µs/ms の sleep、GT driver mailbox の 1 command（`sb_lock` の下）、**書き込みの記録**（直近 N 個の (reg, value) の ring と名前付きの step: 試験と log 用）、試験の hook（read/write の差し替え） | `struct i915_xd_io { mmio; ring; trace on/off; hooks }` | `mmio.c`、`kern_udelay` |
| `xd-platform` | platform の記述（§3.1。表 P1 のデータと、PCI ID・revision・PCH ID からの選択） | `drv_i915_xd_platform_of`、stepping（表 S1）、port の一覧、「この port は駆動できるか」 | 無し（const の表） | — |
| `xd-vbt` | firmware の表（OpRegion は VBT の入れ物としてだけ使うので 1 つ） | OpRegion の map と検証、VBT の探索（mailbox 4 / RVDA、PCI ROM）、block の parse → `struct i915_xd_vbt`（port の表、panel、PPS の T、backlight、eDP、AFC）、platform の V2/V3 の表の適用、SHA-256（N0 の報告用） | parse の結果（値のみ） | `drv_pci`、`vm` の map、`xd-platform` |
| `xd-edid` | sink の記述（EDID/CEA。DP と HDMI の両方が使う純粋な decode） | base/CEA の decode → `struct i915_xd_edid`。detailed timing は own の `output.c` の decode を呼ぶ | 無し | `output.c` |
| `xd-aux` | north の AUX channel（DDI_AUX_CTL/DATA の hardware） | 1 transaction、native read/write、I2C-over-AUX、再試行の規則 | `struct i915_xd_aux { port, base, io select, stats }` × channel | `xd-io`、`xd-power`（AUX の well、DC off） |
| `xd-gmbus` | PCH の GMBUS controller（DDC） | 1 transfer（pin、address、index、長さ）、EDID の読み（128/256 byte）、NAK の片付け、reset | `struct i915_xd_gmbus { lock, pin }` | `xd-io` |
| `xd-power` | display の電源の木（PWR_WELL_CTL*・FUSE_STATUS・DC_STATE_EN と、TGL の pcode の well）と display 全体の init/uninit（PRM の "Sequences to Initialize Display" は well・CDCLK・DBUF・PHY をまたぐ 1 つの手順なので、ここが司令で各 module を呼ぶ） | well の表 W1（platform の記述から）と refcount、enable/disable（fuse、IRQ の hook、drain の失敗で落とさない）、DDI IO / AUX の well（TC は uc_health、ICL は EXIT_TCCOLD）、TGL の TC_cold_off（pcode）、DC state の refcount（DC6 は DMC 次第）、init/uninit（platform の step）、DC9 の enter/leave（SOUTH_CHICKEN1 を含む）、「pipe の register が読めるか」の照会、**pipe の well の post_enable/pre_disable の hook（`interrupts.c` の口）** | `struct i915_xd_power { well[]: refs; dc_off_refs; init_done; hooks }` | `xd-io`、`xd-ddi`（combo PHY の init）、`xd-clock`（CDCLK の init/uninit）、`xd-dbuf`（slice の init）、`xd-dmc`、`interrupts.c` の hook |
| `xd-clock` | clock の木（参照・CDCLK・DPLL・TC/MG PLL・port → transcoder の clock select。PRM の "Clocks" の 1 章） | 参照 clock（DSSM）、CDCLK の読み・sanitize・set（表 T5、voltage の mailbox、crawl か relock か）、最大 pixel rate、voltage level の判定、QGV の帯域の判定、combo DPLL の値（表 T3 + HDMI の算法 + 38.4 MHz の WA）と enable/disable、DDI への map（3 種の register）、TC PLL / MG PLL の値（T4 + 算法）と enable/disable、TRANS_CLK_SEL（3 種の field）、pixel/link clock の計算 | `struct i915_xd_clock { ref_khz; cdclk_khz; voltage; pll[]: owner, rate }` | `xd-io`、`xd-tc`（Dekel の window）、`xd-platform` |
| `xd-ddi` | DDI の buffer と combo PHY（DDI_BUF_CTL・DP_TP_CTL と PORT_* の combo PHY は 1 つの port の送信側。HDMI/DVI の DDI mode と TMDS の上限もここ） | combo PHY の init/uninit（master/slave、PHY_MISC の有無）、lane の power（PORT_CL_DW10）、swing の programming（表 T1、TC は `xd-tc` の表 T2 を呼ぶ）、DDI_BUF_CTL の語（ADL-P の TC の所有 bit と link rate を含む）と enable/disable と idle の待ち、DP_TP_CTL の pattern（transcoder か port の register）、HDMI/DVI の mode の判定（TMDS ≤ 340 MHz、EDID の HDMI の有無）、DDI A の lane の数 | 無し（port ごとの引数） | `xd-io`、`xd-tc` |
| `xd-tc` | Type-C の subsystem（Dekel/MG PHY + FIA + IOM + TCSS。所有・TCCOLD・lane・HPD の live が 1 つの hardware の約束） | Dekel の window（HIP_INDEX、lock）か MG の直の読み書き、DP_MODE、PMD lane sus、swing の表 T2、TC/MG PLL の register の列（`xd-clock` が呼ぶ）、状態機械（§3.5）: live state、mode（DP-alt/TBT/legacy）、ready、所有、TCCOLD の block（流れ A/B: §2.8）、pin assignment → lane、DFLEXDPMLE1、遅延の disconnect、**UCSI への報告（今の `tc-kern.c` の `drv_typec_display_report/_forget` の呼び出しを xd-tc が直に持つ。`tc-kern.c` は旧 code の環境の橋で、旧 code と一緒に消す: レビュー 2 の M4）** | `struct i915_xd_tc_port { mode, owned, lanes, links, live, disconnect timer }` × 6、window の lock | `xd-io`、`xd-power`（AUX USBC/TBT・DDI IO・TGL の pcode の well）、`typec.c`（報告） |
| `xd-panel` | PCH の panel power sequencer と backlight PWM（register も 0xC7200/0xC8250 で隣） | VBT と既定から T 値、VDD の on/off、panel の on/off と待ち、power cycle の待ち、PWM の周波数と duty、enable/disable、level ↔ duty、VBT の min、2 つ目の controller | `struct i915_xd_panel { t[], vdd_on, on, last_off_tick, period, min, level, bl_on }` | `xd-io`、`xd-vbt` |
| `xd-dmc` | DMC firmware engine（program storage・event handler・pipe DMC） | package の parse と選択（v1/v2、stepping）、WA、program の書き込み（header の番地）、MMIO pair（handler の差し替え）、載せ直し、pipe DMC の enable/disable、「DC6 を許せるか」 | `struct i915_xd_dmc { loaded mask, payloads, version }` | `xd-io`、`firmware.c` |
| `xd-dp` | DisplayPort の link の protocol（DPCD と訓練。hardware ではなく規格の単位: source 側の register は `xd-ddi`、sink は AUX 越し） | rate/lane の候補（DPCD × VBT × source）、bpp、M/N/TU、sink の power、DPCD の helper、訓練（CR/EQ、swing の要求 → `xd-ddi`）、link status の確認、fallback の次の候補 | `struct i915_xd_link { rate_khz, lanes, bpp, m/n, trained, train_set[4] }` | `xd-aux`、`xd-ddi`、`xd-io` |
| `xd-pipe` | pipe と transcoder（display 12 以降は transcoder = pipe。display 11 の transcoder EDP は「pipe A〜C のどれかに繋がる 4 つ目の transcoder」として同じ module が持つ） | timing、MSA、DATA/LINK M/N、TRANS_DDI_FUNC_CTL（EDP の input select を含む）、TRANS_CONF の enable/disable と待ち、PIPE_SRCSZ、PIPE_MISC、PIPE_ARB_CTL、scaler off、PIPE_BOTTOM_COLOR・GAMMA_MODE・CSC_MODE、frame counter、vblank の待ち、plane 1 の programming（ARB slots）、enable/disable、flip（PLANE_SURF）、FLIP_DONE/SURFLIVE、format と stride の検査 | 無し | `xd-io`、`xd-dbuf` |
| `xd-dbuf` | DBUF と MBUS と watermark（memory 側の帯域の単位: DBUF_CTL・MBUS_CTL・PIPE_MBUS_DBOX_CTL・PLANE_WM/BUF_CFG・latency の mailbox・SAGV・IPC） | latency（mailbox）と調整（platform の加算）、slice の選択と power（platform の表）、MBUS の join/unjoin と DBOX の credit、DDB の割り当て（相対の offset）、WM の level（platform の数）・transition・SAGV（HW/software）・LINETIME、無効な level の写し、SAGV off、IPC、1 ↔ 2 画面の遷移の順、**「並ぶ pipe の集合」と slice の状態の照会（§4.2）** | `struct i915_xd_dbuf { latency[8], levels, slices_on, joined, pipes }` | `xd-io` |
| `xd-hpd` | hotplug の hardware（north の HOTPLUG_CTL/DE_HPD、south の SHOTPLUG_CTL/SDE）と connector の worker | pin の表 H1、HPD の enable/re-arm（SHPD_FILTER_CNT）、割り込みの ack と queue（IRQ context）、**割り込みの rate の上限**（1 秒に 50 回を超えたら mask して worker で 2 分後に戻す: 今の `hotplug.c:124-129` と同じ閾値。poll への切り替えは持たない: §7 #4）、worker（live state → TC の flow → AUX/EDID の probe）、connector の記録と generation、retry、event の通知 | `struct i915_xd_hpd { connector[]: kind, port, state, generation, edid, mode; pending bits; work; rate }` | `xd-io`、`xd-tc`、`xd-aux`、`xd-gmbus`、`xd-edid`、`present.c`（通知）|
| `xd-screen` | zedBSD の「1 つの出力」（own の概念: mode set の合成。Linux の atomic に相当する物は持たない） | plan の計算（§3.3）、enable（§2.3/2.4 の順）、disable、flip、status、preflight、retained、resident の run と head の run、並ぶ pipe の集合を `xd-dbuf` に渡す、run の戻り（§4.2 の契約） | `struct i915_xd_screen { kind, port, pipe, pll, plan, phase, held wells, buffers }` × 2 | 上の全部 |
| `xd-takeover` | 起動時の GOP の引き継ぎ（own の概念） | N0 の読み出しと判断、GOP の出力の分類（§2.14）、VGA plane の停止、takeover（GOP の出力を disable の順で止める）、survey の log、start の失敗で GOP の絵を残す判断 | `struct i915_xd_firmware { pipes[], pll, ddi, pp, pwm, fb range, decision }` | `xd-io`、`xd-power`、`xd-screen`（disable） |
| `xd-display` | driver の段（own の概念: start/stop/suspend/resume） | §3.6 の段。`display.c` の公開の関数はこれを呼ぶ | `struct i915_xd { platform, io, vbt, power, clock, tc, panel, dmc, dbuf, hpd, screen[2], firmware, stage flags }` | 上の全部 |
| `xd-diag` | 診断（own） | register の survey、名前付き register の比較表（Linux の dump との差）、run の log、`i915.debug=display`、最後の手段の停止、capture（今の own の `capture.c` の record の口） | 無し | `xd-io` |

20 module（改訂 2 の 19 + `xd-platform`）。Linux の file と 1 対 1 に残る物（`xd-aux` ↔ `intel_dp_aux.c`、`xd-gmbus` ↔ `intel_gmbus.c`、`xd-dmc` ↔ `intel_dmc.c`、`xd-edid` ↔ drm_edid）は、hardware（AUX channel、GMBUS controller、DMC engine）や規格（EDID）がそれ自体で 1 単位なので残した。
内部の関数の分け方・名前・制御の流れは Linux と一致させない（監査は `plan/ws244/tests/inventory.py` の C/S と、関数名の対応の目視: p013）。

今の 60k 行（Linux 環境を含む）に対し、新しい xd は **1.6〜2.0 万行**の見込み（§6.5）。減る理由: MST・DSC・audio・VRR・FBC・色管理・cursor・scaler・PSR・DC3CO・HDCP・OpRegion の runtime・atomic の global state・"world" の環境・Linux の互換 layer（drm_dev、drmm、vblank worker、notifier）を持たない。増える理由（改訂 2 比）: platform の記述と 5 platform 分の表、TC の 2 つの流れ、MG PHY/PLL（p014）。

### 3.3 制御の流れ（screen の mode set）

```
plan   : mode (EDID/display.mode/CVT) + sink (DPCD/EDID) + platform (記述, ref clk, cdclk, dbuf) + vbt + run_pipes（並ぶ pipe の集合）
         → struct i915_xd_plan { mode, kind, port, pipe, transcoder, pll_id, link{rate,lanes,bpp,m/n/tu}, pll_words, dbuf{slices, joined, ddb, wm[], trans, sagv, linetime}, pps, fb }
         純関数。host 試験で register の語（§6.3 の oracle）を比べる。拒む条件（CDCLK・voltage・QGV: §2.3）はここ。
enable : power(wells, TCCOLD) → panel on → tc lanes → pll → ddi io → tc dp_mode → [DP: clk_sel, ddi_func(no enable), dp_tp, swing, lanes, buf_ctl, train]
         → dbuf/wm → plane → trans timing/msa/mn → color → ddi_func enable → arb_ctl → trans_conf → pipe dmc → [HDMI: swing, lanes, buf_ctl] → backlight
         各 step は `int` を返し、失敗したら「その step まで」を disable の順で戻す。step ごとに xd-io の trace に名前を残す。display 11 の DP は訓練の後に clk_sel（§2.3 の 7）。
disable: 逆順（§2.3/2.4）。TRANS_CONF の off の待ちと DDI の idle を確かめ、確かめられなかった物は「retained」（well と buffer を保持、以後その screen を灯さない: 今の挙動）。
flip   : PLANE_SURF を書く → FLIP_DONE の completion を待つ（timeout 100 ms）→ SURFLIVE で確認。
2 画面 : head の enable の前に xd-dbuf が「A+B」の割り当てを計算し、resident の DDB/WM/DBOX を vblank に同期して書き換え（ADL-P は join を解く）、head の slice を上げる（§2.10）。head の disable の後に逆。
```

phase（screen の状態）: `OFF → PLANNED → POWERED → LINKED → LIT → (RETAINED)`。enable の step の失敗は phase を戻して disable を走らせる。
screen は 2 つ（resident = GOP の出力か、移動の後の出力、head = 2 つ目）。**pipe の割り当て**: resident は GOP が灯していた pipe（eDP なら A、GOP が HDMI/DP を灯していた時は B）、**head は resident と違う pipe**。今の `head-rules.c` の `drv_i915_head_pipes_conflict` の規則（同じ pipe は拒む）を plan の段に入れ、衝突が起きない割り当てにした。
display 11 の transcoder は pipe と別に plan が決める（eDP = EDP、他は pipe と同じ番号）。

### 3.4 資源の所有権と寿命、並行性と lock

- **所有**: `struct i915_xd` は `struct i915_display` の中（device の寿命）。well・PLL・DBUF slice・TC の所有・AUX の well は xd-power/xd-clock/xd-dbuf/xd-tc の refcount で持ち、screen が enable で取り disable で返す。device の stop は screen を止めてから well を返し、返せなかった物は log して保持（今の「abandoned」）。
- **誰が触るか**: display の register を書くのは **request worker（display window）だけ**（今と同じ: present・head・backlight・control の要求は worker に queue される）。例外は (a) 起動の段（start worker、window の前）、(b) hotplug worker（AUX/GMBUS の probe と TC の connect/disconnect。screen の register は触らない）、(c) 割り込み（ack と completion だけ）、
  **(d) DMC の load の worker（レビュー 2 の M9 (a)）**: 非同期の load は display の register（program storage、MMIO pair、DC_STATE_DEBUG、CLKGATE_DIS_PSL_EXT）を書くので、xd-dmc は load の間 DC off の refcount を 1 つ持ち（DC6 に入らない）、`xd.lock` の下で「載せ中」の印を立て、request worker の mode set は載せ中なら pipe DMC の enable を飛ばし、載せ終わった時に worker が LIT の pipe の PIPEDMC_CONTROL を立てる（request worker に queue する: 自分では書かない）。DC6 は載せ終わって refcount を返した後に許す。
- **読み手（M9 (c)）**: syscall の文脈（query・which・claim・events・resident_identity）が読む connector の表と generation は `xd.lock` の下で写しを取る（hotplug worker が書く）。head の claim が `irq_lock` の下で読む状態（`head.c:461-477`）は own の file の規則で変えない。
- **lock**:
  - `xd.lock`（mutex）: hotplug worker と request worker と DMC worker の間で xd-power/xd-tc/xd-aux の refcount と状態、connector の表を守る。
  - **DDI_BUF_CTL の 2 つの書き手（ADL-P の流れ A）**: bit 6（TC の所有）は xd-tc が `xd.lock` の下で rmw する。bit 31（enable）と lane/rate の field は request worker の screen の enable/disable が書く。規則は「**TC port の DDI_BUF_CTL は、その port の link の refcount が 0 の間だけ xd-tc が触り、refcount > 0（screen が POWERED 以降）の間は screen だけが触る**」。screen は enable の最初で `xd_tc_get_link`（`xd.lock` の下で connect と refcount++）、disable の最後で `xd_tc_put_link` を呼ぶ。流れ B は所有が別の register（DFLEXDPCSSS）なので衝突は無いが、同じ規則で扱う。
  - **TC の partner gone（M9 (b)）**: hotplug worker は refcount > 0 の port の所有を変えない。unplug は live state の変化として screen に「partner gone」を通知し、**request worker の screen が disable（put_link）した後に**、hotplug worker の遅延の disconnect が所有を返す（§3.5 の `IN_USE → (screen を止める要求) → CONNECTED (links 0) → FREE` の 2 段）。今の「unplugged として window を抜ける」（present.c:757-807）と同じ順。
  - **AUX の共有**: 1 channel に 1 つの mutex（`aux[ch].lock`）。訓練（request worker）と probe（hotplug worker）が同じ channel を使う時は transaction ごとに取る。hotplug worker は LINKED/LIT の screen の port を再 probe しない（HPD short は link status の読みだけ、`aux[ch].lock` の下）。
  - IRQ context は `device IRQ lock`（既存）の下で pending bit と completion だけ。xd-tc の Dekel の window は spinlock（割り込みからは使わない）。`sb_lock`（mailbox）は既存の mutex。
  - lock の順: device IRQ lock は最内。`xd.lock` → `aux[ch].lock` → `sb_lock` → Dekel の window。**全部の mutex が LOCK_RANK_DEVICE なので kernel の rank の検査では守られない（M9 (d)）**: 順序は文書（この節と `xd.h` の comment）で定め、host 試験の fake の lock の記録で順序の違反を検出する（`run-xd-host-tests.sh` の lock の順の検査）。
- **割り込みと DMA**: display の DMA は plane の scan out（GGTT）と DMC の program（MMIO）だけ。vblank/FLIP_DONE/AUX done/HPD を使う。AUX は割り込み無しの poll でもよい（今は poll。保つ）。
- **失敗と回復**: 各 step の timeout は表 T8。timeout → その screen を disable、再試行は呼ぶ側（present の relight、DP の fallback）。訓練の失敗 → rate/lane を下げて再試行（最低 RBR×1 まで）。TC の partner が消えた → unplugged として window を抜ける。DMC が無い → DC6 無しで続ける。VBT が無い → 既定（eDP on A、PPS の既定、backlight 200 Hz）。platform の記述が無い（未知の ID）→ display を使わない（bootfb）。

### 3.5 状態遷移（TC port と hotplug の connector）

```
TC port: FREE ──(live: DP-alt/legacy/TBT)──▶ DETECTED ──(connect ok: TCCOLD blocked, owned, lanes)──▶ CONNECTED ──(get_link, links>0)──▶ IN_USE
            ▲                                     │ abort（戻す）                                 │ put_link → links 0        │ partner gone → screen に通知 → screen の disable が put_link
            │                                     │                                               ▼                           │
            └──(disconnect: 1 s の遅延 work、再び lock されたら取り消し)─── CONNECTED (links 0) ◀──────────────────────────────┘
connector: ABSENT ──(HPD long / live=1 / boot readout)──▶ PROBING ──(DPCD/EDID ok)──▶ CONNECTED ──(HPD long, live=0)──▶ ABSENT
                                                          │ fail → retry (≤2 回、1 s) → ABSENT
           CONNECTED ──(HPD long, live=1)──▶ PROBING（EDID を読み直し、変わっていれば generation を増やして通知）
           CONNECTED ──(HPD short)──▶ CONNECTED (link status を確かめ、落ちていれば retrain の要求)
```
IN_USE → FREE は無条件ではない: links が 0 になった後に遅延の disconnect を通る。generation は CONNECTED になる度と EDID が変わった時に増える。storm は rate の上限で mask する（§3.2 の xd-hpd）。poll への切り替えは持たない（§7 #4）。

### 3.6 start/stop/suspend/resume の段（xd-display、`display.c` の公開の関数との対応）

`display.h` の段の名前（`init_opregion`・`init_noirq`・`irq_prepare`・`init_nogem`・`register`）は Linux の probe の段で、呼ぶ側の `device.c` は棚卸しで derived（レビュー 2 の M11 (b)）。**この WS では段の名前と分け方を「`device.c` が決める外の制約」として保ち**、own の段（start・serve・suspend・resume・stop）への縮約は WS244 の `device.c` の整理と一緒に行う（§7 #12）。

| `display.c` の口（保つ） | 今の中身（Linux の probe の段） | 新しい中身（`xd.h` の段） |
| --- | --- | --- |
| `drv_i915_display_create` | worlds の確保 | `struct i915_xd` を zero にし、io・lock を用意、platform の記述を選ぶ、boot の語で実装（old/xd）を選ぶ（§6.1） |
| `drv_i915_display_init_opregion` | OpRegion/VBT/DRAM/帯域 | `xd_vbt_locate_parse` → DRAM の種別（mailbox）を `xd_dbuf` に |
| `drv_i915_display_init_noirq` | drm_dev、bios、VGA、power map、N0、core init、DMC、mode config… | `xd_takeover_readout`（N0、読むだけ）→ 判断 → `xd_power_init`（§2.2、CDCLK・DBUF・IPC を含む）→ `xd_dmc_load`（非同期、§3.4 (d)） |
| `drv_i915_display_irq_prepare` | PCH の判定、割り込みの bind | 同じ（`interrupts.c` は own。入口を `xd_hpd_irq_*` に、power の hook は `xd_power` が受ける） |
| `drv_i915_display_init_nogem` | survey、nogem front、setup outputs、readout、sanitize | `xd_diag_survey` → `xd_dbuf_read_latency` → port の表（VBT と platform）→ `xd_takeover_plan`（GOP の出力の分類だけ。register は書かない） |
| `drv_i915_display_register` | HPD init、IPC、TC/DP-ext/HPD start、driver register | `xd_hpd_start`（pin の enable、boot の readout）、`xd_tc_start`、DC off を返す（takeover が残っていれば保持） |
| `drv_i915_display_resident_deps` | 同左 | screen[0] に resident の output を設定 |
| `drv_i915_display_stop_early` | 最後の手段の停止、HPD stop、TC stop、unregister | `xd_screen_force_off`、`xd_hpd_stop`、`xd_tc_stop`、DC off を取る |
| `suspend_begin/end`、`resume_begin/end` | INIT ref、core uninit、DC9 | `xd_hpd_quiesce` → `xd_screen_dark_all` → `xd_tc_release_all` → `xd_power_hold_dc_off` → `xd_power_uninit` → `xd_power_dc9_enter`；`dc9_leave` → `xd_power_init` → `xd_dmc_reload` → `xd_hpd_rearm`（live state の読み直し）→ 返す |
| `drv_i915_display_stop_outputs` / `fini` | eDP fini、nogem fini、state fini、DMC fini… | `xd_screen_release`、`xd_dmc_free`、`xd_power_release`（保持した well の log）、map の解除 |

## 4. 外向きの口と境界

### 4.1 保つ口（変えない）

- GPU node の operation（`drv_gpu_ops`、`uapi/gpu-display.h`）: display の query/claim/present/wait/release/refresh/power/events。`display.c` の `drv_i915_display_bind_ops` 以下。
- `display.h` の start/stop/suspend/resume の関数（§3.6 の左の列）と `drv_i915_display_panel`・`_which`・`_events`・`_resident_identity`・`_output_*`、
  **own の口で §3.6 の段に無い物（レビュー 2 の M4）**: `drv_i915_display_output_moved/_back/_fail_back/_lost`（`present.c`）、`_gpu_retained`・`_abandoned`・`_irq_sync_failed`（`device.c`）、`_session_close`（`session.c`）、`_destroy`。これらは `display.c` の own の規則（出力の移動・戻り・放棄）で、呼び出し先が xd の口に変わるだけ。
- `present.h`・`head.h`・`control.h`・`backlight.h`・`scanout.h`・`bootfb.h`（own の file の口）。`head.c` の `drv_i915_lcd_modeset_flip_settle` の呼び出しも §4.2 の表に入れた。
- UCSI への報告の口 `drv_typec_display_report`・`drv_typec_display_forget`（`src/drivers/typec/typec.c`）: 呼ぶ側が `tc-kern.c` から xd-tc に変わる。
- Gen9 の display（`gen9-*.c`、`intel/display-regs-gen9*.h`）の code は変えない。ただし Gen9 は Gen12 の物を次のとおり使っているので、p002 で own に移す:
  (a) `internal.h` の own の型（§3.1 の分離で `display-internal.h` へ）、(b) `gen9-outputs.c` の `drv_i915_adjust_wm_latency()`（`takeover.c`）、`bootfb.c`（own）の `drv_i915_n1_console_fb()`（`takeover.c`）、`device.c` の `drv_i915_n0_lite_gen9()`（`takeover.c`）→ **own の file（`gen9-outputs.c`・`bootfb.c`・`display.c`）に自前の実装として移す**（値は同じ、code は書き直す。WS244 の (1) と同じ file を触るので、順序は §6.2 の依存）、
  (c) `gen9-node.c` の `drv_i915_delayed_queue/_cancel_sync/_work_init`（`hotplug-internal.h` の遅延 work の helper）→ own の `display-internal.h` の小さな helper に移す。
- **boot の語（レビュー 2 の M17: 今ある語を全部挙げる）**: `display=`（auto・hdmi・edp・panel。今は log するだけ）、`display.mode=WxH[@R]`、`i915.start=`（auto・manual）、`i915.debug=`（off・display）、`i915.gen9=`（on・off）、`i915.gen9.display=`（copy・flip・backlight・modeset・full）。
  kernel の boot の parser（`src/kern/boot.c` の `parameter_names` の表と値の検査）は**表に無い名前を黙って無視する**ので、`i915.display=xd|old` を足すには `kern/boot.c`（WS245 の file の外）に名前と値の検査を足す必要がある → **p003 の仕事として Q1 の承認を取る**（§6.1）。log の接頭辞 `i915: `。

### 4.2 動く口（own の file の呼び出し先が `xd.h` に変わる所。p003 で全部付け替える: 決定 1）

own の file が今呼ぶ Gen12 の関数を、header の宣言（655 個）と own の file の識別子を突き合わせて数えた（2026-10-15）: `present.c` 7、`head.c` 18（`flip_settle` を含む）、`output.c` 15、`control.c` 1、`backlight.c` 3、`bootfb.c` 1、`interrupts.c` 4、**`display.c` 78**、`device.c` 2、`gen9-outputs.c` 1、`gen9-node.c` 3、**`tc-kern.c` 17**（power の get/put、dkl の read/write/rmw、tc の 10 関数、`drv_i915_vbt_encoder_for_port`）、**`capture.c`**（`hotplug.h`）、**`dc9.c`**（`power.c` の SOUTH_CHICKEN1）。
`internal.h` の Linux 由来の型（`i915_lcd_state`・`i915_lcd_modeset_cfg`・`i915_lcd_kernel`・`i915_lcd_kernel_deps`・`i915_native_report`・`i915_hpd_output`・`i915_display_output.state`・`i915_display_head.k/params/cfg`・`i915_lcd_flip_result`・`i915_lcd_aux_emit`・`i915_gop_output`・`i915_hpd_summary`・`i915_native_pipe`）も own の file から消える。
own の file の構造は変えず、**呼び出し側の行だけを `xd.h` の口に直す**。

| own の file | 今呼ぶ物（数） | 新しい口（`xd.h`） |
| --- | --- | --- |
| `present.c` | `drv_i915_lcd_kernel_resident_run`、`_modeset_flip_nowait/poll/wait_screen`、`_modeset_retained`、`_show_retained`、`drv_i915_hpd_topology_touch`（7） | `drv_i915_xd_screen_run(display, screen, serve, ctx, &result)`、`_screen_flip_arm/poll/wait`、`_screen_retained`、`drv_i915_xd_hpd_touch` |
| `head.c` | `drv_i915_lcd_modeset_select/prepare/commit_enable/commit_disable/status/plane_disable/plane_released/flip_nowait/poll/settle/flip_settle`、`_lcd_kernel_locks_init/bind_ops/preflight/fill_cfg`、`_lcd_output_params/cfg/link_fallback`（18） | `drv_i915_xd_screen_plan(display, 1, output, run_pipes, &plan)`、`_screen_light(display, 1, &plan, fb)`、`_screen_dark`、`_screen_flip_*`、`_screen_flip_settle`、`_screen_status`、`_screen_preflight`、`_screen_link_fallback`、`drv_i915_xd_dbuf_state(display, &st)` |
| `output.c` | `drv_i915_lcd_compute_hdmi/dp_ext`、`drv_i915_display_panel_mode/size_mm`、`drv_i915_dp_ext_probe/sink_copy/link_limits/aux_emit`、`drv_i915_tc_kern_port_of`、`drv_i915_hpd_output/_count/_take/_summary/_probe_connector/_edid_bytes`（15）。`drv_i915_gop_output_read/_name` は `output.c` 自身の関数（own。呼び先が xd-takeover の読みに変わる） | `drv_i915_xd_plan_hdmi/_dp/_edp`、`drv_i915_xd_panel_mode/size`、`drv_i915_xd_sink_probe(display, connector, &sink)`、`drv_i915_xd_hpd_connector(display, i, &info)`、`_connector_count`、`_connector_take`、`drv_i915_xd_firmware_output(display, &out)`/`_name` |
| `control.c` | `drv_i915_pipe_frame_read`（1） | `drv_i915_xd_pipe_frame(display, pipe)` |
| `backlight.c` | `drv_i915_lcd_modeset_brightness_get/brightness/backlight`（3） | `drv_i915_xd_backlight_get/set/power` |
| `display.c` | §3.6 の段の 78 個（power domains、cdclk、dbuf、bw、dmc、hpd、tc-kern、dp-ext、edp device、nogem、state、capture、dc9、vga、opregion、native、pmdemand、quirks、worlds…） | `drv_i915_xd_*` の段（§3.6、約 20 個）、`drv_i915_xd_step(platform, revid)`、`drv_i915_xd_power_well_on`、`drv_i915_xd_firmware_*`。capture（`capture.c`、own の試験用の record）は `xd-diag` に口を移す。`dc9.c`（own）は `xd-power` の DC9 の段に吸収して消す（p013） |
| `tc-kern.c`（own、Zlib） | Gen12 の 17 関数と UCSI の報告 | file ごと消す（p013）。役割（kernel の環境の橋、UCSI の報告）は xd-tc が直に持つ（§3.2）。p003〜p012 の間は旧 code の一部として残る |
| `scanout.c`、`bootfb.c` | `drv_i915_n1_console_fb`（takeover） | own に移す（4.1） |
| `interrupts.c` | `drv_i915_hpd_de_irq/pch_irq`、`drv_i915_display_power_is_enabled`、`drv_i915_opregion_gse_entry`（4）と、power.c が呼ぶ `drv_i915_pw_irq_ops`（post_enable/pre_disable の hook） | `drv_i915_xd_hpd_irq_north(display, iir)`、`_irq_south(display, iir)`、`drv_i915_xd_power_pipe_on(display, pipe)`、GSE は捨てる。hook は `xd.h` の `struct i915_xd_irq_hooks`（同じ 2 つの関数、context と pipe mask）で `xd-power` に渡す |
| `diagnostics.c`（Gen12、書き直し） | — | `xd-diag` に |
| `device.c` | `drv_i915_n0_lite_gen9`、`drv_i915_lcd_debug_set`（2）、PCI ID の表 | Gen9 の own、`drv_i915_xd_diag_set`、ID の表に RKL・ADL-S/RPL-S を足す（p002） |
| `gen9-outputs.c`、`gen9-node.c` | `drv_i915_adjust_wm_latency`、`drv_i915_delayed_*`（4） | own（4.1） |
| `tests/execution/ktest-display.c`・`ktest-display-probe.c`・`ktest-gt.c`・`ktest-sync.c`、`tests/display/*-ktest.c`、`capture.c` の試験 | `internal.h` の Linux の型と `clock.h`・`dmc.h`・`phy.h`・`power.h`・`takeover.h`・`vbt-parse.h`・`watermark.h`・`hotplug.h` | §6.4 |

**screen の run と plan の契約（レビュー 2 の M5。own の `present.c`・`head.c`・`display.c` の移動・戻り・2 画面の規則が頼る物）**:

| 口 | 入力 | 出力・戻り |
| --- | --- | --- |
| `_screen_plan(display, idx, output, run_pipes, &plan)` | output（connector、mode の出典）、`run_pipes` = この screen と並んで灯る pipe の集合（resident だけなら自分の pipe、head を灯す時は A+B） | 0 / EINVAL（入らない mode: §2.3 の 3 条件）/ EBUSY（pipe の衝突）。`plan.dbuf` は `run_pipes` の割り当て |
| `_screen_light(display, idx, &plan, fb)` | plan と framebuffer | 0 / EAGAIN（**訓練の失敗で link を下げて再試行できる**: `plan.link` を fallback の候補に更新して返す。呼ぶ側（present/head）が再び plan → light）/ ENODEV（TC の partner gone、sink 無し）/ EIO（timeout: その screen は disable 済み）/ ETIME（retained: well と buffer を保持したまま） |
| `_screen_run(display, idx, serve, ctx, &result)` | resident の run（window の中で flip を serve） | `result.reason` = SERVED / UNPLUGGED（partner gone・HPD long で live 0: 呼ぶ側が window を抜け、firmware の出力へ戻すか panel を試す）/ SPARED（head の 2 画面のために resident を 2 pipe の割り当てで灯し直した: 今の "lit again for two pipes"）/ FAILED |
| `_screen_dark(display, idx)` | — | 0 / ETIME（retained） |
| `_dbuf_state(display, &st)` | — | `st.pipes`（灯っている pipe の集合）、`st.slices_on`、`st.joined`（`head.c:1411-1425` の log と head の claim の判定が読む） |
| `_hpd_connector(display, i, &info)` | connector の番号 | kind、port、connected、generation、EDID の要約、「TC の partner がいるか」 |

出力の移動（lease が無い時の claim が resident を別の connector へ移す `i915_display_move`）、失敗・unplug の時の firmware の出力への戻り、sleep の後の panel への戻りは own の `display.c`・`present.c` の規則で、xd は上の戻りの値で「できた／できなかった／partner がいない」を返すだけ。

`struct i915_display` から消える field（**p013 で**。p003〜p012 の間は旧の adapter が使うので残す）: `drm_dev`、`vbt_state`、`vga_client`、`power_domains`、`pmdemand`、`cdclk`、`pwc`、`edp_dev`、`dcore`、`dmc_dev`、`dstate`、`nogem`、`dprobe`、`dram_info`、`bw_state`、`dmc_wq/modeset_wq/flip_wq`、`opd/p2_opd`、`n0/n0_mmio`、`gop`、`dkl`、`tck`、`rctx/rlcd`、`lk`、`lcdb_locks`、`resident_run_env/rep`、`fill_cfg_pn`、`show_*`、`i915_vbt_buf`、`g_dmc_arena`、`dc_trace`、`read_opregion_*`、`chain_*`、`g_vga_io`、`*_world`、`drm_inited` … の段の flag。
残る field: `device`、`irq`/`irq_vblank`（own）、`output`/`gop_output`（型は `struct i915_xd_output` に）、`backlight`、`refresh`、`rd`、`window`、`head`（`k/params/cfg` → `struct i915_xd_plan`）、`resident_buf`、`resident_serve*`、`resident_front/up`、`pch`、`absent`、`hpd_started`、新しい `struct i915_xd xd`。

### 4.3 `xd.h` の型（境界の型、Linux の型を置き換える。p003 で決める）

```c
struct i915_display_mode { uint32_t clock_khz; uint16_t h[4], v[4]; int hsync_pos, vsync_pos; uint16_t width_mm, height_mm; uint8_t bpc; };  /* own、Gen9 と共有（§3.1） */
struct i915_xd_sink { uint8_t kind; /* EDP, DP, HDMI */ uint8_t dpcd_rev, edp_rev, max_lanes; uint32_t max_rate_khz; uint8_t edp_caps[4]; uint8_t edid[256]; unsigned edid_len; struct i915_xd_edid parsed; };
struct i915_xd_plan { struct i915_display_mode mode; uint8_t kind, port, pipe, transcoder, pll_id, tc_port, run_pipes; struct { uint32_t rate_khz; uint8_t lanes, bpp; uint32_t data_m, data_n, link_m, link_n, tu; } link;
                      uint32_t pll_words[12]; struct { uint8_t slices; int joined; uint16_t ddb_start, ddb_end, linetime; uint32_t wm[8], trans, sagv, buf_cfg, dbox; } dbuf; uint32_t fb_surf, fb_stride, fb_w, fb_h; };
struct i915_xd_output { uint8_t kind; int none, has_connector; unsigned connector, tc_port; int port; struct i915_xd_plan plan; struct i915_xd_sink sink; const char *mode_source; };
struct i915_xd_connector_info { uint8_t kind; int port, connected, partner; uint64_t generation; const char *name; uint32_t w, h, refresh_mhz, w_mm, h_mm; };
struct i915_xd_firmware_output { uint8_t kind; /* NONE, EDP, HDMI, DP_TC, OTHER */ unsigned pipe, pipes; int port; };
struct i915_xd_run_result { uint8_t reason; /* SERVED, UNPLUGGED, SPARED, FAILED */ int error; };
struct i915_xd_dbuf_state { uint8_t pipes, slices_on; int joined; };
struct i915_xd_irq_hooks { void (*post_enable)(void *ctx, unsigned pipe_mask); int (*pre_disable)(void *ctx, unsigned pipe_mask); void *ctx; };
struct i915_xd_ops { /* §6.1: 口の実装の表。p003 では old の adapter が、p009 から xd が埋める。p013 で表を消し直接の関数にする */ ... };
```
（host 試験が register の語を比べるのは `struct i915_xd_plan` と xd-io の trace。）

### 4.4 変えない振る舞い（受け入れの基準になる物）

- 起動: GOP が eDP を灯した 5330・5320 で、display を使う判断（N0 の規則）、GOP の出力の分類、最初の present で takeover して panel が灯る。VT-d が有効なら使わない。GOP が eDP 以外を灯していた時の規則は §2.14。
- present/flip の性能: vblank 同期の flip、FLIP_DONE の待ち 100 ms、frame counter の確認（起動時 10 frame/1 s、定常 30 frame/1.5 s）。
- backlight device（`/dev/backlight/backlight0`）の get/set/power、range は PWM の周期（5330 は 96000。VBT の 200 Hz と raw clock 19.2 MHz から）。
- hotplug: HDMI（DDI B）と USB-C DP（TC1/TC2）の plug/unplug で topology の event、head の claim で 2 画面、unplug で window を抜ける、TC の partner gone、**出力の移動と戻り（`i915_display_move`、firmware の出力への戻り、sleep の後の panel への戻り）、head を灯す時の resident の 2 pipe の灯し直し**（§4.2 の契約）。
- suspend/resume: DC9 を通って戻り、次の present で灯る。
- log: `i915: N0 …`、`i915: N1 …`、`i915: takeover: …`、`i915: resident …`、`i915: display …`、`i915: hpd …`、`i915: TC: …`、`i915: dmc: …`、`i915: bootfb: …`、`i915: gen9 …`（Gen9 は無関係）は保つ（AAT・T1 の script・ユーザーの目視が読む接頭辞）。Linux の段の名前を含む文言（`P2`・`P3`・`P5c`・`P7 intel_opregion_register`、`Found …`、`bw:`、`cdclk:`、`opregion …`）は捨てる。残す文言と捨てる文言の一覧は p011 で確定し §6.4 に書く。
- **display 12**: 今の code の TGL の分岐（5320 で確認済み: eDP・backlight・suspend）を xd の display 12 の行で保つ。TGL の TC port と RKL・ADL-S は新しい機能（§1.1）で、受け入れは host 試験と「一覧に載せた実機の確かめ」。

## 5. データの扱い（register の offset・bit・表）

### 5.1 形

- header は zedBSD の著作権 header と file の説明だけ。各 block（1 register 群、1 表、1 手順）の直前に複数行の comment:
  ```c
  /*
   * The combo PHY DPLL words for the DisplayPort link rates at a 19.2 or
   * 38.4 MHz reference: DCO integer and fraction, and the P, K and Q dividers.
   * At 38.4 MHz the fraction is written halved on the platforms that need it
   * (the platform description says which).
   *
   * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dpll_mgr.c
   * (icl_dp_combo_pll_19_2MHz_values), cross-checked against IHD-OS-TGL-Vol 12-12.21
   * "DisplayPort Mode PLL Values".
   */
  ```
  Linux の値は Linux を第一の出典、PRM は照合（「cross-checked」）。Linux に無い値は「Values referenced: IHD-OS-TGL-Vol 12-12.21 §<章> (<表の名前>)」だけ。
- 名前は独自: `I915_XD_<REG>` / `I915_XD_<REG>_<FIELD>`（PRM の register の名前を大文字で。Linux の macro 名（`ICL_DPCLKA_CFGCR0`、`SKL_BOTTOM_COLOR`、`_MMIO_PIPE`）は使わない）。instance は `#define I915_XD_TRANS_HTOTAL(trans) (0x60000 + 0x1000 * (trans))` の形（offset の算術はデータ）。platform で offset が違う register（TC PLL の enable、pipe DMC の base、ADL-S の DPCLKA_CFGCR）は platform の記述の field か、別の名前の define。
- 表は `static const struct` の配列。配列の要素の comment に行の意味（rate、voltage level、platform）を書く。表の構造（struct の field の並び）は自分で決める（Linux の struct を写さない）。
- VBT・OpRegion・DPCD・EDID の layout は **offset と幅の表**（`#define I915_XD_VBT_CHILD_DVO_PORT 2` …）で持ち、packed struct は作らない。読みは `i915_xd_get8/16/32(bytes, off)`。
- 試験の fixture: 5330 の EDID・DPCD・register の語は今の `tests/display/dp-fixture-latitude5330.h` と `plan/ws031/display-ref/`、`plan/ws051/tests/m3-5330-20261007/` を使う。**VBT と OpRegion の fixture は合成**（試験の code が V1 の layout の表から bytes を組み立てる。実物の bytes は使わない: §7 #7）。display 12 の fixture は Linux の表の値から（実測は無い）。

### 5.2 register の出典の対応（grep で確かめた物。第一の出典は Linux、PRM は照合）

| register の群 | 第一の出典（Linux v6.8.12） | 照合（PRM） |
| --- | --- | --- |
| pipe・plane・transcoder・DDI_BUF_CTL・DP_TP_CTL/STATUS（transcoder と port の両方の offset）・TRANS_DDI_FUNC_CTL/2・TRANS_CLK_SEL・PIPE_SRCSZ・PIPE_MISC・PLANE_*・WM_LINETIME・PIPE_FRMCNT/SCANLINE・PIPE_BOTTOM_COLOR・PIPE_ARB_CTL・PIPE_MBUS_DBOX_CTL・GAMMA_MODE・CSC_MODE・VGA_CONTROL | `i915_reg.h`、`intel_color_regs.h` | TGL Vol 2c（PIPE_MISC は ACM Vol 2c。PIPE_ARB_CTL・PIPE_MBUS_DBOX_CTL・PIPE_BOTTOM_COLOR も TGL Vol 2c にある） |
| DPLL_ENABLE/CFGCR0/CFGCR1/SSC・DPCLKA_CFGCR0（ICL/RKL の field）・ADLS の DPCLKA_CFGCR・DDI_CLK_SEL・CDCLK_CTL/PLL_ENABLE・DSSM・TC PLL の enable（MG_PLL_ENABLE / ADLP_PORTTC_PLL_ENABLE）・MG_* の PLL の register | `i915_reg.h`、`intel_dpll_mgr.c` | TGL Vol 2c（ADL-S の register と MG の register は PRM に無いか未取得: Linux だけ） |
| PWR_WELL_CTL/CTL_AUX/CTL_DDI・FUSE_STATUS・DC_STATE_EN/DEBUG・DBUF_CTL・DISPLAY_INT_CTL・DE_PIPE/PORT/MISC/HPD の割り込み・DISP_ARB_CTL2・CHICKEN_DCPR_1 | `i915_reg.h` | TGL Vol 2c（DISP_ARB_CTL2 は PRM に無い） |
| MBUS_CTL・MBUS_ABOX_CTL、BW_BUDDY、GEN11_CHICKEN_DCPR_2、XELPD_DISPLAY_ERR_FATAL_MASK、CLKGATE_DIS_PSL_EXT・CLKGATE_DIS_5、SOUTH_DSPCLK_GATE_D、TRANS_SET_CONTEXT_LATENCY、CLKREQ_POLICY | `i915_reg.h`、`intel_display_wa.c` | ACM Vol 2c（TRANS_SET_CONTEXT_LATENCY）、他は PRM に無い |
| PORT_COMP/CL/TX/PCS（combo PHY）、PHY_MISC | `intel_combo_phy_regs.h` | TGL Vol 2c |
| DKL_*、HIP_INDEX_REG0/1、MG_TX*/MG_DP_MODE（ICL）、PORT_TX_DFLEX*（FIA）、TCSS_DDI_STATUS | `intel_dkl_phy_regs.h`、`i915_reg.h` | TGL Vol 2c / ACM Vol 2c（MG は未取得の ICL の PRM） |
| DDI_AUX_CTL/DATA | `intel_dp_aux_regs.h` | TGL Vol 2c |
| SRD_CTL/STATUS、PSR2_CTL | `intel_psr_regs.h` | TGL Vol 2c |
| GMBUS0–5、PP_*、SBLC_PWM_*、SHOTPLUG_CTL_DDI/TC、SHPD_FILTER_CNT、RAWCLK_FREQ、NDE_RSTWRN_OPT、SOUTH_CHICKEN1、SFUSE_STRAP | `intel_gmbus_regs.h`、`intel_pps_regs.h`、`intel_backlight_regs.h`、`i915_reg.h` | ACM Vol 2c（SBLC_PWM_CTL1/FREQ C8250h/C8254h、PP_STATUS/CONTROL C7200h/C7204h、GMBUS0 C5100h、SHOTPLUG_CTL_DDI/TC C4030h/C4034h、RAWCLK_FREQ C6204h は Linux の ICP 系の offset と一致: 2026-10-15 確認） |
| SDEISR/IMR/IIR/IER、TC/TBT の HOTPLUG_CTL | `i915_reg.h` | ACM Vol 2c / TGL Vol 2c |
| DMC の program storage、DMC_EVT_CTL/HTP、PIPEDMC_CONTROL、pipe DMC の base（TGL/ADL-P） | `intel_dmc_regs.h` | TGL Vol 2c（PIPEDMC_CONTROL はある） |
| GT driver mailbox と command（latency 0x6、QGV 0x10d、SAGV 0xe、CDCLK control、TGL の TCCOLD 0x26、ICL の EXIT_TCCOLD） | `intel_gt_regs.h`、`i915_reg.h`、`intel_pcode.h` | TGL Vol 2c（GTDRIVER_MAILBOX_INTERFACE）、Vol 12 の手順 |

### 5.3 持つ表（名前は独自、出典を付ける。platform の列を持つ）

| 表 | 内容 | 第一の出典（Linux v6.8.12） | 照合 |
| --- | --- | --- | --- |
| P1 platform の記述 | §1.1 の表（5 行）と port の一覧 | `intel_display_device.c` (icl_display, tgl_display, rkl_display, adl_s_display, xe_lpd_display)、`intel_display.c` (intel_phy_is_combo, intel_phy_is_tc, intel_port_to_phy) | 5330 の dump、5320 の確かめ |
| S1 stepping | PCI revision → display の step（ICL、TGL、TGL UY、RKL、ADL-S、RPL-S、ADL-P、ADL-N、RPL-P） | `intel_step.c` (icl_revids, tgl_revids, tgl_uy_revids, rkl_revids, adls_revids, adls_rpls_revids, adlp_revids, adlp_n_revids, adlp_rplp_revids) | 5330 の revid |
| T1 combo の swing | platform ごと（TGL/TGL UY、RKL、ADL-S、ADL-P、ICL）の DP/eDP/HDMI の行（swing select、n scalar、cursor coeff、post cursor 1/2） | `intel_ddi_buf_trans.c` (tgl_*, rkl_*, adls_*, adlp_*, icl_* の combo の表) | TGL Vol 12 "Voltage Swing Programming"、m3 の PORT_TX の語（ADL-P） |
| T2 TC の swing | Dekel（TGL、ADL-P）の DP/HDMI の行、MG（ICL）の行 | `intel_ddi_buf_trans.c` (tgl_dkl_phy_trans_*, adlp_dkl_phy_trans_*, icl_mg_phy_trans_*) | TGL Vol 12 "DP Voltage Swing Table" |
| T3 combo DPLL の DP の値 | 8 rate × 2 参照、38.4 MHz の小数の WA（platform の flag） | `intel_dpll_mgr.c` (icl_dp_combo_pll_19_2MHz_values, icl_dp_combo_pll_24MHz_values, ehl_combo_pll_div_frac_wa_needed) | TGL Vol 12 "DisplayPort Mode PLL Values"、5330 の 0x00e001a5/0x88 |
| T4 TC/MG PLL の DP の値 | rate 依存の register（Dekel と MG で register が違い、値の算法は同じ） | `intel_dpll_mgr.c` (icl_calc_mg_pll_state の定数, tgl_tbt_pll_19_2MHz_values) | TGL Vol 12 "TypeC PHY PLL Programming for Displayport"、m3 の TC PLL 2 の語 |
| T5 CDCLK | platform ごとの表（ICL/TGL/ADL-S、RKL、ADL-P、RPL-U）と voltage の段 | `intel_cdclk.c` (icl_cdclk_table, rkl_cdclk_table, adlp_cdclk_table, rplu_cdclk_table, tgl_calc_voltage_level, icl_calc_voltage_level) | TGL Vol 12 "CDCLK PLL Ratio and Divider Programming"、m3 の 192000 kHz |
| T6 watermark | latency の調整（+3/+2 µs、6/8 level）、transition WM の式、SAGV の加算、DBUF の割り当て（platform の表、join）、DBOX の credit（platform）、cursor の予約 36 | `skl_watermark.c` (icl_allowed_dbufs, tgl_allowed_dbufs, adlp_allowed_dbufs, skl_compute_transition_wm, intel_mbus_dbox_update, skl_cursor_allocation, adjust_wm_latency) | TGL Vol 12 "Watermark Algorithm"、5330 の 0x80004010/0x0fdb0000、m3 の 0x07db0000/0x07d50000 |
| T7 procmon | 0.85/0.95/1.05 V × dot-0/1 の COMP_DW1/9/10 | `intel_combo_phy.c` (icl_procmon_values) | TGL Vol 12 "Procmon Reference Values" |
| T8 timeout | PG fuse 20 µs、PW state 1 ms（PRM 30 µs）、DBUF 10 µs、DPLL lock 1 ms（PRM 50 µs）、TC PLL lock 900 µs、DDI idle 1 ms/TC 3 ms、TRANS_CONF off 2 frame、AUX 4000 µs、uc_health 10 µs、AUX USBC の well（TBT/legacy は timeout を許す）、TC の ready 500 ms、GMBUS 50 ms、FLIP_DONE 100 ms、TGL の TCCOLD の要求 10 回 × 1 ms、ICL の EXIT_TCCOLD 3 回 + 1 ms | Linux の各関数と今の code の値 | TGL Vol 12 の各手順（短い方） |
| W1 power well の表 | platform ごとの well（名前、CTL の instance、index、fuse の有無、VGA の有無、irq_pipe_mask、TC/TBT の AUX か、pcode の well か）と「機能」（pipe・DDI・AUX・VGA との対応。自分で書く） | `intel_display_power_map.c` (icl_power_wells, tgl_power_wells, rkl_power_wells, adls_power_wells, xelpd_power_wells)、`i915_reg.h` の index | 5330 の PWR_WELL_CTL2 0xc03、`dbg-i915_power_domain_info.txt`、TGL Vol 12 "Functions Within Each Well" |
| W2 BW_BUDDY の page mask | DRAM の種別と channel 数ごと、ADL-S/RKL A0 の WA の表 | `intel_display_power.c` (tgl_buddy_page_masks, wa_1409767108_buddy_page_masks) | TGL Vol 12 "Bandwidth Buddy Registers" |
| H1 HPD の pin | DDI A/B/C → SHOTPLUG_CTL_DDI の nibble、TC1-6 → SHOTPLUG_CTL_TC と TC/TBT の HOTPLUG_CTL の nibble、SDE の bit、DE_HPD の bit、SHPD_FILTER_CNT の値（PCH） | `i915_reg.h` (SDE_DDI_HOTPLUG_ICP, SDE_TC_HOTPLUG_ICP, GEN11_DE_HPD_*, SHPD_FILTER_CNT_*)、`intel_hotplug_irq.c`（`hpd_icp`/`hpd_gen11`） | ACM Vol 12 "Hot Plug Pins"（DG2 の SDE の bit の表は ADL-P と違う: Linux を採る）、m3 の `range-0xC4000.txt`（SDEISR 0xc4000 = 0x00010000: DDI A の HPD が live） |
| V1 VBT の block の layout | block id、field の offset と幅、version の境 | `intel_vbt_defs.h` | 合成の fixture（5330 の値） |
| V2 DVO port → DDI | platform ごと（共通、RKL、ADL-S、ADL-P） | `intel_bios.c` (dvo_port_to_port の 4 つの表) | 5330 の `00-summary.txt` の connector の並び |
| V3 VBT の DDC pin → GMBUS の pin | platform ごと（ICP、RKL/TGP、ADL-S、ADL-P） | `intel_bios.c` (icp_ddc_pin_map, rkl_pch_tgp_ddc_pin_map, adls_ddc_pin_map, adlp_ddc_pin_map)、`intel_gmbus.c` (gmbus_pins_icp) | `00-summary.txt` の HDMI の bus |
| O1 OpRegion の layout | header、mailbox の offset、RVDA/RVDS | `intel_opregion.c` | 合成の fixture |
| D1 DPCD の address と bit | 0x000..、0x100..、0x200..、0x600、0x700..、0xF0000 | `include/drm/display/drm_dp.h` | VESA DP 1.4（会員のみ）、5330 の DPCD |
| E1 EDID の offset | base block、CEA の tag | 今の own の `output.c` | VESA E-EDID 1.4（公開）、5330 の EDID |
| C1 PCH の ID | ISA bridge の device ID の上位 → ICP/TGP/ADP | `soc/intel_pch.c`、`soc/intel_pch.h` | 5330 の PCH の ID |

### 5.4 引用の規則（決定 3。guardrail「MIT・GPL の出典のデータの引用」の形で、file ごとではなく block ごと）

- **値**（offset・bit・表の 1 行・定数・timeout）: その block の直前の複数行 comment に「Value referenced: Linux v6.8.12 <path> (<define・表の名前>)」（複数なら「Values referenced: …」）。PRM から取った値は「Value(s) referenced: IHD-OS-TGL-Vol 12-12.21 §<章> (<表の名前>)」。両方で確かめた物は Linux を先に、PRM を「cross-checked against …」で続ける。
- **手順**（register の列、待ち、条件分岐の規則）: 手順を実装する関数（または連続する step の block）の直前の複数行 comment に「Procedure referenced: Linux v6.8.12 <path> (<関数の名前>)」。1 つの関数が複数の Linux の関数を参考にしたなら関数名を `,` で並べる。PRM の章も参考にしたなら「and IHD-OS-TGL-Vol 12-12.21 "<章の名前>"」を続ける。
- **書かない物**: file の先頭の出典・派生の宣言・SHA-256（guardrail）。Linux の code・comment の文・関数の分け方・型・制御の流れ（決定 3「参考にする」は手順の内容であって形ではない）。PRM の文の転記。Linux の POWER_DOMAIN の集合や feature flag の表（software の抽象）。
- **comment の文**: 「何のためにこの register を書くか」を自分の言葉で書く。Linux の comment を訳した文も書かない。
- **形**: 複数行の `/*`、` * `、` */`（1 行の `/* … */` にしない。guardrail 2026-10-15）。
- **監査**（p013）: `grep -L "referenced:" display/xd-*.c intel/*-xd*.h` で出典の無い file が無いこと、`grep -n "Derived from\|derived from the Linux" display/ intel/` の Gen12 の file が 0 件、`plan/ws244/tests/inventory.py` の Gen12 の file の C/S の行が 0、関数名の対応表（xd の関数 ↔ Linux の関数）の目視で「1 対 1 の写し」が無いこと。
## 6. 書き直しの順序、Phase の分割、切り替え、試験、見積もり

### 6.1 切り替えの方法（決定 1: 案 (a)。選ばなかった案も残す）

**選んだ案 (a): 口 `xd.h` を先に決め（p003）、own の file は最初からその口だけを呼ぶ。口の実装は「旧 code への薄い adapter」と「xd」の 2 つで、boot の語で丸ごと切り替える。**

- p003 で `xd.h`（§4.2・§4.3 の口と型）と `struct i915_xd_ops`（口の実装の表）を決め、own の file（present・head・output・control・backlight・interrupts・display・device）の呼び出し側の行を `xd.h` に付け替える。`xd.h` の関数は `display->xd.ops->...` を呼ぶ薄い wrapper。
- **旧の adapter**（`display/old-xd.c`、1 file）: `struct i915_xd_ops` の各関数を、今の Gen12 の関数（`drv_i915_lcd_*`・`drv_i915_hpd_*`・`drv_i915_dp_ext_*`・`drv_i915_tc_kern_*`・§3.6 の段）を呼んで実装する。型の変換（`struct i915_xd_plan` ↔ `i915_lcd_modeset_cfg`/`i915_display_head.k/params/cfg`、`i915_xd_output` ↔ `i915_display_output`、`i915_xd_connector_info` ↔ `i915_hpd_output`、`i915_xd_run_result` ↔ resident run の戻り）は adapter の中だけ。
  この file は Linux 由来の型を触るので "Derived from the Linux kernel" の表示を保ち、**p013 で消す**（出典の監査の対象外）。adapter は旧 code の state（power の refcount、world）だけを使い、xd の state は作らない。`tc-kern.c` は旧 code の側に残る。
- **xd の実装**（`display/xd-*.c`）: p004〜p008 は host 試験だけで育て、p009 で `struct i915_xd_ops` を埋める。p009 から boot の語 **`i915.display=xd|old`** で `drv_i915_display_create` がどちらの ops を使うか選ぶ。
  **既定は p009・p010 の間は `old`**（xd は `i915.display=xd` の opt-in。レビュー 2 の B3: p009 の xd は eDP だけで、HDMI・USB-C・head・移動を持たないので、既定にすると 5330・5320 で退行する）。**p011 で xd を既定にする**（p009・p010 の実機の確かめ（§6.3）が済んでから。§7 #0）。`old` は p013 で消すまで逃げ道。
  1 つの boot で 1 つの実装だけが動き、**2 つの実装の state は混ざらない**（旧の adapter は `struct i915_display` の旧 field を、xd は `display->xd` を使う）。
- **p009 の xd（opt-in）の規則**: GOP の出力が eDP でなければ GOP の絵を残して display を使わない（§2.14 の分類の「対応しない出力」と同じ道。レビュー 2 の M6）。hotplug・TC・HDMI・head は p010 まで「無い」と答える（connector の数 0）。
- p013 で adapter・旧 code・`i915.display=old`・`struct i915_xd_ops` を消し、`xd.h` の wrapper を直接の関数にする（機械的な rename）。
- **boot の語の追加（レビュー 2 の M17）**: `i915.display=` は `src/kern/boot.c` の `parameter_names` と値の検査（xd・old の 2 語、他は拒んで log）に足す。WS245 の file の外なので p003 の Queue で Q1 の承認を取る（HAL ではない）。綴りの誤りは「無視」ではなく「不明な値」の log にする。
  単独起動の実機で xd が灯らない時の戻し方は ESP の `zedbsd.cfg` の書き換え（別の機械で）か、`i915.display=old` を焼いた 2 つ目の boot entry（試験の image の build で用意する: §6.3）。xd の start が takeover の前に失敗した時は自動で GOP の絵を残す（§2.14）。
- この案の cost: wrapper と ops の表（約 45 の口）が p003〜p013 の間だけ増える。adapter は旧 code の構造に引かれるが、使い捨てで監査の対象外。利点: own の file の付け替えが 1 回で済む（p003）、p011 の切り替えが「既定の語を変える」だけ、逃げ道 `i915.display=old` が実在する。design policy §2.6「implementation behind the interface replaceable as a whole」に沿う。
- **kernel の大きさ（レビュー 2 の m8）**: `vmunix` の上限は 16 MiB（`AMD64_KERNEL_MAX_BYTES`）で、main の amd64 の kernel は約 13.4 MiB、i915 の試験の組は 1.4 MiB を足す。旧 code（~1.5 MB の object）と xd（§6.5、~0.5 MB の見込み）が並ぶ p009〜p012 の間、`I915_TESTS=y` の image が上限を越える恐れがある。p009 の受け入れに「試験の組（display・display_ktest）の image が上限に入る事（`size`）」を入れ、入らなければ ktest の組を p009 から消す（§6.4 の退役を前倒し）。
- 選ばなかった案 (b)「逃げ道は commit の rollback と割り切る」: 5330 はユーザーの日常の機械で、起動できない版を main に入れない運用との兼ね合いで、1 つの image で戻れる方を採った。
- 選ばなかった案 A「群ごとに旧 file を置き換える（edid → vbt → aux …）」: 旧 code の型が群の境界をまたいで深く結ばれているので、群ごとに adapter を書く事になり、その adapter が Linux の構造を写す。
- 選ばなかった案 C「build 時の切り替え（config.mk の `I915_DISPLAY_OLD=y`）」: ops の表が要らないが、ユーザーが 2 つの image を作り分ける事になる。boot の語の方が確かめやすい。
- 選ばなかった案 D「p009 から xd を既定にする」（改訂 2 の案）: B3 のとおり p010 まで退行する。

### 6.2 順序と Phase

依存は「→」の左が前提。p004・p005・p006 は p002 だけに依存し互いに独立（host 試験だけなので並べられる）。p007 は p004〜p006 に、p008 は p006 に依存する。

| Phase | 内容 | 受け入れ条件（host で確かめられる物は host、実機の物は「実機」。QEMU と実機の証拠は分けて書く） | 依存 | LW |
| --- | --- | --- | --- | ---: |
| p002 | **土台**: `intel/display-regs-xd.h`・`-south.h`・`display-tables-xd.h`（§5 の形で、p004〜p008 が使う register と表を全部。**表は 5 platform の行を持つ**（ICL の行は p014 で足す））、`xd-io`（read/write/rmw/poll/trace/mailbox/試験の hook）、`xd-platform`（表 P1・S1・C1、`device.c` の ID の表に RKL・ADL-S/RPL-S を足す、PCH の ID）、host の試験の枠（`plan/ws245/tests/run-xd-host-tests.sh`: xd の file + **own の register の model（`tests/display/xd-fake-regs.c`、Linux の型を使わない）** + ASan/UBSan + lock の順の記録）、`internal.h` の own の型の分離（`display-internal.h`、`struct i915_display_mode`）、Gen9 と `device.c`・`bootfb.c`・`gen9-node.c` の own への移設（§4.1 の (b)(c)）。**xd の file は p002 から `AMD64_I915_SOURCES` に入れる**（kernel の build で warning を早く見る。呼ばれない） | (a) 全 register の offset が Linux の値と一致する表の試験（`tests/display/host-xd-regs-test.c`: 旧 `intel/*.h` の define と比べる。**この試験は旧 header と一緒に p013 で消し、代わりに m3/display-ref の dump の offset と名前の一覧で比べる試験に置き換える**）；(b) xd-io の poll/trace/mailbox の試験；(c) platform の選択（5 platform の代表の ID と revid → 記述と step）；(d) build warning 0、`git diff --check`；(e) 移設した関数の既存の試験（host-gen9-outputs、host-bootfb、host-native-decide）が通る；(f) QEMU: Venus の image の起動（T1 に boot-test.sh を依頼。Gen9 の own の型の分離が壊れていない事） | WS244 の (1) と `gen9-outputs.c` を同時に触らない順序（Q1 の Queue） | 6 |
| p003 | **口と adapter**: `xd.h`（§4.2・§4.3）、`struct i915_xd_ops`、wrapper、`old-xd.c`（旧 code への adapter）、own の file の付け替え（present・head・output・control・backlight・interrupts・display・device）、`display.c` の段を `xd.h` の段に、`i915.display=` の語（`kern/boot.c`、Q1 の承認。p003 では `old` しか実装が無く、既定も `old`） | (a) own の file が Gen12 の header（`internal.h` の Linux の型、`modeset.h`・`hotplug.h` …）を include しない（grep。`tc-kern.c`・`capture.c`・`dc9.c` は旧 code の側として例外）；(b) 既存の own の host 試験（host-native-decide、host-bootfb、host-gen9-*）が通る；(c) build warning 0；(d) **T1（5330 の passthrough）**: adapter 越しの image で `I915_TEST_SET=display` の lcdb・hdmib・dual（§6.3）が今と同じ PASS；(e) **実機（ユーザー、一覧）**: 5330 と 5320 で今と同じに起動・eDP・backlight・HDMI/USB-C の plug・suspend の戻り（adapter 越しでも振る舞いが変わらない事）；(f) QEMU: Venus の image の起動 | p002 | 7 |
| p004 | **VBT・EDID**: `xd-vbt`（OpRegion を含む、4 つの V2/V3 の表）、`xd-edid` | 合成の VBT の bytes（5330 の値と、RKL・ADL-S・TGL の DVO/DDC の値の 4 組）と合成の OpRegion と EDID の fixture で、今の parser と同じ値（port の表、panel の DTD、PPS の T、backlight 200 Hz、eDP の bpp/rate/lanes、DDC → GMBUS の pin、AUX）を返す host 試験（`host-xd-vbt-test.c`）；VBT 無しの既定；壊れた VBT（size・offset・block の overrun）の拒否 | p002 | 3 |
| p005 | **AUX・GMBUS**: `xd-aux`、`xd-gmbus`、own の register の model の AUX・GMBUS の部分 | `dp-fake-hw.c` の AUX の挙動を own の model に書き直した物（oracle は今の model の期待値）で native read/write・I2C-over-AUX・DEFER/NACK/timeout/receive error の再試行が今と同じ回数と結果；GMBUS で 128/256 byte の EDID 読みと NAK（最初の message の 1 回の再試行）/timeout | p002 | 3 |
| p006 | **power・DMC・clock**: `xd-power`（表 W1 の 4 platform、PW_A の独立（13）と PW_2→PW_3 の木（12）、TGL の pcode の well、IRQ の hook、init/uninit の platform の step、DC、DC9）、`xd-dmc`（v2 package、WA・event handler の disable・差し替え・pipe DMC）、`xd-clock`（CDCLK の 4 表と crawl/relock、voltage・QGV の判定、combo DPLL と WA と 3 種の map、TC PLL（Dekel）の語） | host: (a) init の手順の register の列が §2.2 の順（platform ごとに差の step が入る／入らない事）、PW_A が PW_2 無しで上がる事（13）、PW_3 が PW_2 の後に上がる事（12）、PW_A/PW_3 の post_enable で hook が呼ばれる事、drain の失敗で well を落とさない事；(b) well の refcount の get/put と失敗の巻き戻し、TGL の pcode の well の要求の再試行；(c) DMC package の parse（`adlp_dmc.bin`・`tgl_dmc_ver2_12.bin` の実物で main/pipe の entry）と MMIO pair の範囲検査と差し替え、載せ方の register の列（gating の WA → handler の disable → **header の番地への payload** → pair）；(d) DPLL の語: HBR@38.4 → 0x00e001a5/0x88（ADL-P・TGL）、RKL の DPLL4 と ADL-S の CFGCR の map、m3 の TC PLL 2 の語（`host-dpext-link.c`・`host-dkl-test.c` の期待値を移す）；(e) CDCLK: 読み（m3 の 192000）、妥当な値を保つ、妥当でない値を最小にする列（13 は crawl、12 は relock）、uninit で bypass、voltage の段 | p002 | 8 |
| p007 | **DDI・DP・panel**: `xd-ddi`（combo の init（master/slave と PHY_MISC の platform の差）、swing の表 T1 の 4 組、lanes、DDI_BUF_CTL（ADL-P の TC の bit）、DP_TP（transcoder）、HDMI/DVI の mode、DDI A の lane）、`xd-dp`（link の方針、訓練、DPCD の helper）、`xd-panel`（PPS、backlight）、own の model の訓練の部分 | host: (a) `lcd-fake-hw.c` の訓練の挙動を own の model に書き直した物で eDP HBR×2 の訓練が今と同じ DPCD の列で通り、"CR never"/"EQ never" で fallback（EAGAIN の契約）；(b) swing の register の語が m3 の `range-0x160000.txt`（PHY A）/`range-0x16C000.txt`（PHY B）の PORT_TX_DW* と一致（ADL-P）、TGL・RKL・ADL-S の行は Linux の表の値と一致；(c) PPS の T の計算と on/off の順、VDD、delay の register を毎回書く事；(d) backlight の level ↔ duty、FREQ 0x17700 = 19.2 MHz / 200 Hz、24 MHz の raw clock の時の周期 | p004、p005、p006 | 6 |
| p008 | **pipe・DBUF**: `xd-pipe`（timing、色、plane、flip、ARB、context latency）、`xd-dbuf`（latency の platform の加算、2/4 slice の表、join/unjoin、DDB、WM 6/8 level、SAGV の HW/software、DBOX の platform の credit、IPC、1 ↔ 2 画面の遷移） | host: (a) eDP 1920×1080@60 の timing・M/N・MSA・DDI_FUNC_CTL（0x8a210002）・PIPE_SRCSZ・PLANE_*・PIPE_BOTTOM_COLOR/GAMMA_MODE の語が `regs-selected.txt`/`regs-dump.txt` と一致（`host-lcd-test.c` の期待値を移す）；(b) 1 画面の WM（PLANE_WM0 0x80004010、BUF_CFG 0x0fdb0000、LINETIME 0x77）；(c) 2 画面（A+B）の割り当てが m3 と一致（MBUS_CTL 0x700、DBUF_CTL × 4、A 0x07db0000、B 0x07d50000、DBOX A 0xa9038804/B 0xb1038804）と、1 → 2 → 1 の遷移の register の列；(d) display 12 の 2 slice の割り当て（1 pipe = S1+S2、A+B = S2/S1、join 無し、DBOX の credit 2/2/12）と level 8 の WM；(e) flip の arm と FLIP_DONE の model | p002、p006 | 6 |
| p009 | **screen・takeover・display の段・xd の opt-in**: `xd-screen`（eDP の plan/enable/disable/flip/run と §4.2 の契約）、`xd-takeover`（N0 の読みと規則、GOP の出力の分類、VGA の停止、takeover の disable、start の失敗で GOP を残す）、`xd-display`（§3.6）、`xd-diag`（capture の口を含む）、`struct i915_xd_ops` の xd 側、`i915.display=xd`（既定は `old` のまま） | host: (a) 一画面の mode set の register の列が §2.3 の順（`host-lcd-modeset-test.c` の family A の順序の検査を移す）、display 12 と 13 の両方の記述で；(b) N0 の判断の規則（`host-native-decide-test.c` と同じ 14 検査）；(c) takeover の disable の順と VGA_CONTROL；(d) GOP が eDP 以外を灯していた時に何も書かない事；(e) 試験の組の image が 16 MiB に入る事。**T1（5330 の passthrough、`i915.display=xd`）**: eDP の resident run（lcdb の観測。VBT 無しの既定の道）。**実機（ユーザー、一覧）**: 5330 と 5320 で `i915.display=xd` で起動して GOP の画面から desktop に切り替わる、backlight の get/set、`old` でも起動する。QEMU: Venus の image の起動（T1） | p003、p007、p008 | 7 |
| p010 | **hotplug・Type-C・外部 DP・HDMI・head**: `xd-hpd`（rate の上限を含む）、`xd-tc`（流れ A（ADL-P）と流れ B（TGL: 新しい機能）、遅延の disconnect、UCSI の報告）、RKL・ADL-S の TC の port（combo の普通の port + TC の HPD の pin）、screen の DP-TC/HDMI の plan と enable、head の 2 画面（DBUF の遷移を含む）、出力の移動・戻りの契約（UNPLUGGED/SPARED） | host: (a) own の model の HPD の register で HPD の decode/plug/unplug/retry/EDID の変化（`hpd-ktest` の case を host に移す）、rate の上限；(b) TC の connect/disconnect/abort の順（流れ A: 所有 → ready → AUX USBC の well → lane、流れ B: pcode の well → ready → 所有 → lane）と巻き戻し、links 0 の後の遅延の disconnect、partner gone の 2 段（`host-tc.c` の検査を移す）；(c) DP-ext の probe と fallback（`host-dpext.c`）；(d) HDMI の mode の選択；(e) 2 画面の plan（A+B）と resident の灯し直し（SPARED）の列。**T1（5330 の passthrough、xd）**: hdmib・dual・dual_share・hdmi_hpd・aux の scenario（§6.3）。**実機（ユーザー、一覧）**: 5330 の HDMI の plug で 2 画面、USB-C DP（TC2）の plug で 2 画面、unplug で戻る、plug したまま起動、出力の移動（lease 無しの claim）。5320 の USB-C DP（流れ B、新しい機能: 動かなくても p010 は cleared、結果は Bug か Future Work） | p009 | 8 |
| p011 | **suspend の戻り・既定の切り替え・残り**: suspend の quiesce（HPD の停止、screen の dark、TC の release）、DC9 の enter/leave、DMC の載せ直し、HPD の再 arm と live state の読み直し、backlight device と control（refresh・power）の確認、診断の boot の語、log の文言の一覧（§4.4 の残す／捨てる）、**`i915.display` の既定を `xd` に** | host: suspend と resume の register の列（§2.14）、resume の間に抜かれた connector が ABSENT になる、PPS と PWM の register を resume の後に書き直す事。**実機（ユーザー、一覧）**: 5330 と 5320 で suspend → resume で panel が戻る、backlight が戻る、resume の後の HDMI の plug が動く。既定の切り替えは p009・p010 の 5330 の実機の確かめ（ユーザー）が済んでから（§7 #0） | p009、p010、p009〜p010 の実機の確かめ | 4 |
| p012 | **platform の網羅**: 5 platform の記述（ICL を除く 4 つ）で plan → enable → disable の register の列を host で流し、ADL-P は dump と、TGL・RKL・ADL-S は Linux の表の値と比べる。GOP が HDMI/DP を灯すデスクトップ（RKL・ADL-S）の分類と takeover の列。TGL の TC の流れ B の列 | host: 4 platform × {eDP、HDMI、DP-TC（TGL・ADL-P）} の列が期待値と一致、RKL/ADL-S の「TC1 は combo」の列、未知の ID で display を使わない事。実機は無い（RKL・ADL-S は「未確認」と書く） | p011 | 3 |
| p013 | **旧 code の削除と出典の監査**: 旧 40 file と `internal.h` の Linux の型、`old-xd.c`、`tc-kern.c`、`dc9.c`、`i915.display=old`、`struct i915_xd_ops`（wrapper を直接の関数に）、`intel/` の Gen12 だけが使う header（WS244 と分担）、`tests/execution/ktest-display.c`・`ktest-display-probe.c`（Linux の型に依存: 消し、kernel で意味のある検査は `xd.h` の上の小さな ktest に書き直す）、`ktest-gt.c`・`ktest-sync.c` の include を `xd.h` に、display の ktest の set（`vmunix.mk` の 3 行）の整理、旧 host 試験の削除（§6.4、path は Q1 へ）、`host-xd-regs-test.c` の置き換え、"Derived from the Linux kernel" と Linux の MIT の表示を Gen12 の file から消す | `inventory.py` の Gen12 の file の C と S の行が 0、file の先頭に出典が無い（grep）、block ごとの "Value(s) referenced:"/"Procedure referenced:"（複数行 comment）の grep、関数名の対応表の目視、build warning 0（`I915_TESTS=y` でも）、Venus の image の boot-test（T1）、T1 の passthrough の scenario が xd だけで PASS。**実機（ユーザー、一覧）**: p009〜p011 の確かめを旧 code 無しで | p011、p012、ユーザーの確認（p009〜p011） | 4 |
| p014 | **display 11（Ice Lake）**: 表 P1 の ICL の行と表の ICL の値（wells、`icl_allowed_dbufs`、`icl_cdclk_table`、swing の combo/MG の表、`icl_revids`、ICP の DDC の表）、**MG PHY と MG PLL**（xd-tc と xd-clock の MG の register の列）、transcoder EDP（xd-pipe の transcoder の番号と EDP の input select、TRANS_CLK_SEL を書かない）、DP_TP_CTL の port の offset、display 11 の DP の enable の順（§2.3 の 7）、DMC の v1 package（0x80000、handler の disable 無し）、TC の流れ B の ICL の TCCOLD（AUX の well + EXIT_TCCOLD）、latency の +2 µs、ICL の ID を `device.c` に。ICL の PRM（公開）を必要なら取得して register の意味を確かめる | host: ICL の記述で eDP（transcoder EDP）・HDMI（combo B）・DP-TC（MG）の plan → enable の列が Linux の手順の順と表の値に一致、DMC v1 の parse、ICL の TC の connect の列。**実機は無い**（「未確認」と書き、一覧に載せる）。build warning 0 | p013 | 8 |
| p015 | **規約の全文の見直し**（conformance-reviewer） | `plan/coding-style.md` の全文の checklist、`git diff --check`、build | p014 | 3 |

LW の合計 **76**（±40 %。換算は 1 LW ≈ 実時間 15〜25 分（2026-10-14 の較正、2026-10-15 承認）で、76 LW ≈ 実時間 19〜32 時間）。改訂 2 の 51 LW からの増分: platform の記述と 4 platform 分の表（+5）、TGL の TC の流れ B（+3）、display 12 の well・PLL・CDCLK・DBUF の差（+3）、own の register の model の書き直し（+3）、p012 の網羅（+3）、p014 の display 11（+8）。
**実機の往復は別枠**（レビュー 2 の M16）: p003・p009・p010・p011・p013 の 5330・5320 の確かめで不具合が出た時の往復を **6〜10 LW** と見込む（上の 76 に含めない。判断待ち・実機の待ちも含めない）。
p004・p005・p006 は互いに独立なので担当が空けば並べられる（host 試験だけで QEMU を塞がない）。p009 以降は実機の確かめが要るので、1 つずつ。

Phase ごとの出力: p002・p004〜p008 は「新しい file + host 試験」だけで kernel の振る舞いは変わらない（p002 の ID の表の追加だけは RKL・ADL-S を「display 12 の platform」として受ける: 旧 code の display 12 の道に入るので、p002 では ID を足すだけで旧 code の道には入れない（bootfb のまま）。xd の記述には入れる）。p003 は own の file の付け替え（振る舞いは変わらない筈: 実機で確かめる）。p009 は opt-in、p011 が既定を変える Phase。

**他の WS との依存（レビュー 2 の M14）**: WS244（Gen9 の file と `intel/` の header、license の package。p002 と p013 が同じ file を触る）、WS051（incomplete、p004c・p005b は今の `tc.c`・`dp-ext.c` の上: p010 が置き換える）、WS113（残りの p008・p012・p013 の i915 の実機、BUG-266: p010 の移動の契約が対象）、WS084（p003・p004 の takeover の乖離: p009 の xd-takeover が対象）、WS075（描画の高速化、display の code は触らない）。
これらの残りの Phase は「p013 の後に xd の上でやり直すか、cancel するか」をユーザーが決める（§7 #16）。それまで Q1 は WS051・WS113・WS084 の display の file を触る Queue を p003 の後に入れない（同じ file の衝突）。

### 6.3 試験の道

- **host**（各 Phase、`plan/ws245/tests/run-xd-host-tests.sh`、ASan/UBSan、数秒）: own の register の model（`tests/display/xd-fake-regs.c`: platform の記述から register の挙動（request → state、fuse、lock、idle、AUX の応答、GMBUS の応答、訓練の sink の応答）を返す。**今の `dp-fake-hw.c`・`lcd-fake-hw.c`・`hpd-model.c` は Linux 由来の環境の型と `internal.h` に依存するので bind だけの差し替えでは済まない（レビュー 2 の M15）: 挙動の期待値だけを oracle として移し、model は書き直す**）に対して「register の語」「書き込みの順」「待ちと timeout」「失敗の巻き戻し」「lock の順」を比べる。
  oracle は (a) 5330 の Linux の dump（`display-ref/regs-selected.txt`（lines 60–61 の無名の行が PLANE_WM0/BUF_CFG）、`regs-dump.txt`、m3 の `range-0x*.txt`、`intel_reg-dump.txt`、`debugfs/`）の語、(b) Linux の手順の順、(c) 今の試験の期待値（移す物は §6.4）、(d) display 12 は Linux の表の値。
- **置き場所**: 試験の source は tree の中（`src/drivers/gpu/i915/tests/display/host-xd-*.c`・`xd-fake-regs.c`、kernel には入れない）。走らせる script `plan/ws245/tests/run-xd-host-tests.sh` と合成の fixture の生成の code は `plan/ws245/tests/` に置き、**p013 の clearance の時に Q1 に `plan/master.md` の Tools・試験の一覧への登録を頼む**（WS237 の `plan/ws237/tests/` と同じ形）。
- **T1 の 5330 の passthrough（レビュー 2 の B4。agent の実機の道）**: `plan/tools/hw5330/README.md` §1.2 の passthrough（5330 自身が KVM の host、iGPU を vfio-pci で guest に渡し、本物の LCD を駆動。`/tmp/i915-hw.lock` の下で T1 が流す）を p003・p009・p010・p013 の終わりに使う。
  **制約**: guest には OpRegion が見えない（ASLS = 0）ので VBT 無しの既定の道で走る（xd-vbt の既定: port A = eDP、B = DP+HDMI。5330 の TC の port は既定に無いので passthrough では USB-C は試せない）、GOP の takeover は確かめられない（guest の firmware は OVMF）、suspend は確かめられない。これらは単独起動（ユーザー）に残す。
  scenario は今の `I915_TEST_SET=display`（lcdb lcdc lcdd lcdg lcdo lcdr hdmib dual dual_share n1 aux hdmi_edid hdmi_hpd）のうち **lcdb・hdmib・dual・dual_share・hdmi_hpd・hdmi_edid・aux を xd の口（`xd.h`）に付け替えて残す**（LCD の resident buffer の読み戻しと HDMI の plug の観測は xd でも同じ意味）。lcdc/lcdd/lcdg/lcdo/lcdr/n1 は Linux の型の検査で p013 で消す。付け替えは p009（lcdb）と p010（他）。2026-09-28 の「古いグラフィックの試験の driver は捨ててよい」との関係は §7 #15。
  試験の image は `plan/ws245/tests/config-xd.mk`（WS の config.mk での build と個別の file の複写だけ: 2026-10-04 の規則）で作り、boot の行は `i915.display=xd` と `old` の 2 つの entry。
- **QEMU**: QEMU には i915 が無い。QEMU で見る物は「i915 の無い image（Venus）の起動が壊れていない」だけ（T1 に boot-test.sh を依頼: p002、p003、p009、p013）。
- **実機（ユーザーの確かめ。試験の一覧に載せ、ユーザーに今すぐ頼まない。QEMU・passthrough の証拠と分けて書く）**:
  1. p003: 5330・5320 で adapter 越しで今と同じ（起動・eDP・backlight・HDMI/USB-C の plug・suspend）。
  2. p009: 5330・5320 で `i915.display=xd`: 起動 → GOP の画面 → desktop（takeover）。`i915: N0` と `i915: takeover:` の log。backlight の get/set。`old` での起動。
  3. p010: 5330 で HDMI の plug/unplug（head の 2 画面）、USB-C の DP の plug/unplug（TC2、2 画面）、unplug 中の present が止まらない、plug したまま起動、出力の移動。5320 で USB-C の DP（流れ B、新しい機能）。
  4. p011: 5330・5320 で suspend → resume → desktop が戻る、backlight の値が戻る、resume の後の plug。
  5. p013: 旧 code を消した後の 2〜4。
  6. RKL・ADL-S/RPL-S（p012）、ICL（p014）: 実機が無い。「未確認」のまま一覧に載せ、機械が手に入った時に 2〜4 を流す。
- **kernel の execution 試験**: `I915_TESTS=y` の `ktest-display.c`・`ktest-display-probe.c` は Linux の型で display の probe の段を検査している（3452・3078 行）。xd の段は host 試験で検査するので、p013 でこの 2 つを消し、kernel でしか測れない物（init の後の well と DBUF の実際の state、DMC の version）だけを `xd.h` の上の小さな ktest（`ktest-xd-display.c`、200 行程度）に書き直す。`ktest-gt.c`・`ktest-sync.c` は `display/power.h`・`hotplug.h` を include しているので `xd.h` に向ける。`vmunix.mk` の display の ktest の 3 行は p013 で整理する。
- 負荷・耐久（hotplug の繰り返し、suspend の繰り返し）は別枠（ユーザーに確かめてから）。

### 6.4 今の試験の扱い（2026-10-06 の基準: 変更に追従する時に、参照されていない物は消す）

| 今の試験 | 扱い |
| --- | --- |
| `tests/display/host-dp-test.c`、`host-lcd-test.c`、`host-lcd-modeset-test.c`、`host-capture-test.c` | 走らせる script が無い（README だけが名指す）。期待値（register の語、順序）を p007〜p009 の xd の host 試験に**移し**、file は p013 で消す（path を Q1 へ）。`host-capture-test.c` は own の `capture.c` の試験: p009 で `xd-diag` の口に付け替えて残す |
| `tests/display/dp-fake-hw.c/.h`、`lcd-fake-hw.c/.h`、`hpd-model.c/.h`、`gen9-fake-hw.c/.h` | Linux 由来の環境の型と `internal.h`・`dp-sink.h` に依存（`lcd-fake-hw.c` は 204 箇所）。**挙動の期待値を oracle として own の model（`xd-fake-regs.c`）に写し、p013 で消す**（gen9-fake-hw は Gen9 の物で残す） |
| `tests/display/edp-ktest.c`、`edp-sync-ktest.c`、`lcd-modeset-ktest.c`、`lcd-show-ktest.c`、`lcdg-ktest.c`、`opregion-ktest.c`、`hpd-ktest.c`、`display-ktest.c/.h`、`scanout-ktest.c`（kernel の test build、`display_ktest`/`display_ktest2` の組） | Linux の型に依存。p013 で消す（`scanout-ktest.c` は own なら残す: p013 で判定）。`hpd-ktest` の case は p010 の host 試験に移す |
| `tests/execution/ktest-display.c`、`ktest-display-probe.c`、`ktest-gt.c`、`ktest-sync.c`（`execution` の組） | §6.3 の扱い（p013）。`ktest-gt.c`（power well と HPD）・`ktest-sync.c`（DRAM・BW・power well）は `xd.h` の口に付け替えて残す |
| `tests/display/lcd-run.c`、`lcd-flip.c`、`lcd-gpu.c/.h`、`lcd-opregion.c`、`lcd-run.h`、`scenarios.h`、`hdmi-output.c`、`hdmi-hotplug.c`、`aux.c`（`display` の組の実機の scenario）、`tests/display/README.md` | **lcdb・hdmib・dual・dual_share・hdmi_hpd・hdmi_edid・aux は xd の口に付け替えて残す**（T1 の passthrough の道: §6.3）。lcdc/lcdd/lcdg/lcdo/lcdr/n1 と `lcd-opregion.c` は p013 で消す。README は残す file だけを名指すよう p013 で直す |
| `tests/display/host-kernel.c`、`host-test.c/.h` | own の host 試験の枠。残す |
| `plan/ws051/tests/host-tc.c`、`host-tc-tables.c`、`host-dpext.c`、`host-dpext-link.c`、`host-dkl-test.c`、`host-power-map.c`、`host-vbt-pll.c`、`host-n1-tc.c`、`host-unplug-gop.c`、`plan/bugs/BUG-266/tests/`、`BUG-268/tests/` | 期待値（TC PLL の語、DKL の語、power の表、fallback の列）を p006/p007/p010 に移す。file は WS051 の物で `master.md` の Tools（`host-vbt-pll.sh`・`host-tc.sh`）から参照されているので、p013 で Q1 に報告して一覧ごと整理（xd の試験に置き換わる） |
| `host-native-decide-test.c`、`host-bootfb-test.c`、`host-gen9-*` | 残す（own・Gen9）。native-decide は p009 で xd-takeover に付け替える |
| `plan/ws031/display-ref/`、`plan/ws051/tests/m3-5330-20261007/` | 残す（oracle のデータ。VBT の bytes は無い） |

### 6.5 大きさの見積もり（新しい xd）

| module | 行（概算） | 根拠 |
| --- | --- | --- |
| regs/tables の header | 3600 | 今の `intel/` の Gen12 の define のうち使う物（~750 register・bit）と表 P1/S1/T1〜T8/W1/W2/H1/V1〜V3/O1/D1/E1/C1 の 5 platform 分 |
| io・platform・diag | 1200 | |
| vbt・edid | 1700 | 今の vbt.c の 4978 行は 16 block の全 field と version の分岐。使う 8 block と 4 組の表に絞る |
| aux・gmbus | 900 | |
| power・dmc・clock | 3200 | 4 platform の well の木、pcode の well、CDCLK の crawl/relock、3 種の map、DMC v1/v2 |
| ddi・dp・panel | 2600 | master/slave の差、Dekel の 2 つの表 |
| pipe・dbuf | 2200 | 2/4 slice、SAGV の HW/software、transcoder EDP |
| tc・hpd | 2600 | 流れ A/B、MG の register |
| screen・takeover・display | 2400 | 契約の戻り |
| 計 | ~20400 | 今の 60k（Linux 環境）の 1/3。p003 の `xd.h`・wrapper・`old-xd.c`（~1500 行）は p013 で消える。p014 の MG の分（~900）を含む |

## 7. 危険と未決の判断（ユーザーに聞く点。決定 1〜4 と対象の機種で閉じた物は §8・§9 の表に）

| # | 点 | 既定（この設計） | 聞く理由 |
| --- | --- | --- | --- |
| 0 | `i915.display` の既定を `xd` にする時期 | **p011**（p009・p010 を 5330・5320 で確かめてから）。p009・p010 の間は `old` が既定で xd は opt-in。`old` は p013 まで逃げ道 | 5330 はユーザーの日常の機械。p009 の xd は eDP だけ（レビュー 2 の B3） |
| 1 | HDMI を DVI mode（infoframe・scrambling・audio 無し、≤ 340 MHz）のままにするか | **そのまま**（今の振る舞いを保つのが完了の条件） | HDMI 2.0 の 4K60 や audio は別の WS |
| 2 | mode set から CDCLK を変える事（init/uninit の設定は持つ: §2.6） | 持たない（入らない mode は拒む: CDCLK・voltage・QGV） | 5330 の GOP の 192 MHz は 1080p と m3 の 2 画面に足りている。4K の HDMI は別 WS |
| 3 | PSR・DC3CO・DC6 の扱い | PSR は off のまま。DC6 は DMC が載った時に許す（今と同じ）。DC3CO は持たない | 5330 の panel は PSR 無し |
| 4 | hotplug の storm: rate の上限（50/s で mask、2 分で戻す）だけ持ち、poll への切り替えは持たない | 上限だけ（今の閾値と同じ） | poll の thread は複雑さの割に必要になった記録が無い |
| 5 | OpRegion の runtime（ACPI の notify、ASLE の backlight 要求、DIDL/CADL）と ACPI の backlight の key | 持たない（今も持たない） | brightness の key は Keiland が backlight device で扱う |
| 6 | VBT・OpRegion の layout の出典が Linux（MIT）だけである事 | guardrail の「値の引用」で可 | 公開の仕様書が無い。VBT の block の構造を "data" と見る解釈の確認 |
| 7 | VBT・OpRegion の fixture を**合成の bytes**（Linux の layout の表と 5330 の値から試験の code が組み立てる）にする事。実物の bytes は tree に置かない | 合成（レビュー 2 の M13: 5330 の生の bytes はユーザーが 2026-10-14 に削除した未監査の data） | 改訂 2 の「実物を置く」の置き換え |
| 8 | 旧 code と adapter を p003〜p013 の間 tree に残す事（licence の表示も残る） | 残す（§6.1） | WS244 の licence の整理との順序 |
| 9 | eDP 1.4 の link rate の表（DPCD 0x010）を新しく支える事 | 支える（小さい） | 今の code は拒む。5330 では使われない |
| 10 | kernel の execution 試験（`ktest-display`・`ktest-display-probe`）を消して `xd.h` の上の小さな ktest に置き換える事 | 置き換える（§6.3） | 6500 行の検査を捨てる。xd の host 試験で同じ事を測る |
| 11 | PRM の Notices（§0.2）: TGL・ACM の PRM を ADL-P/S・RKL の実装の「補助」に使う事（grant の範囲）と patent の grant-back の条項を受けるか | 受けない形: PRM は register の意味の理解と照合だけに使い、実装に入れる値は Linux の表から取る（§5.3 の第一の出典は全部 Linux）。「Values referenced: IHD-OS-…」だけの block は作らない | レビュー 2 の M12。法的な判断はユーザー |
| 12 | `display.h` の段（Linux の probe の段の名前）を保つか、WS244 の `device.c` の整理と一緒に own の段（start・serve・suspend・resume・stop）へ縮めるか | この WS では保つ（§3.6）。縮約は WS244 で | レビュー 2 の M11 (b)。`device.c` は棚卸しで derived |
| 13 | main への merge の時期: p002（新しい file だけ）はいつでも。**p003 以降（own の file を変える）は 2026-10-17 の OSC のデモの後**。ベータ2 の「デモの 1 週間前から bug 修正だけ」の方針の下で p003 を merge してよい時期 | p003 以降はデモの後 | master.md の「デモまでの進め方」。レビュー 2 の M18-4 |
| 14 | ADL-P の A0 stepping（`adlp_a_step_cdclk_table`、CMTG の WA）を支えるか | 支えない（製品に A0 は無い。A0 なら display を使わない） | 表の行と WA が減る |
| 15 | 5330 の passthrough を agent の実機の道にし、`I915_TEST_SET=display` の scenario の 7 つを xd の口に付け替えて残す事（2026-09-28 の「古いグラフィックの試験の driver は捨ててよい」との関係） | 付け替えて残す（T1 の道が無いと p009〜p013 の帰還がユーザーだけになる: レビュー 2 の B4） | 2026-09-28 の判断は Gen9 の古い driver の試験の話で、display の scenario は別と読んだ（確認） |
| 16 | WS051・WS113・WS084 の残りの display の Phase（今の code の上）を、p013 の後に xd の上でやり直すか cancel するか。WS244 との分担（Gen9 の file、`intel/` の header、license の package の順序） | p013 の後に Q1 が各 WS の残りを xd の上の Phase として立て直す。WS244 の (1) は p002 の後 | レビュー 2 の M14・M18-7 |
| 17 | TGL の TC port（流れ B、今の code に無い新しい機能）を p010 の範囲に入れる事 | 入れる（host 試験で作り、5320 の確かめは一覧。動かなくても p010 は cleared） | 「Skylake〜Raptor Lake Refresh を全部支える」の読み。延ばすなら Future Work |
| 18 | RKL・ADL-S/RPL-S を実機無しで「机上 + host 試験」で支える事（§1.1） | 支える（p012 で網羅。「未確認」と明記） | 実機が無い。機械が手に入ったら一覧の確かめ |

危険:
- **ADL-P/S の PRM が無い**: 手順は Linux の分岐から取る（決定 3）。Linux の分岐を読み落とす（`IS_ALDERLAKE_P`・`IS_ALDERLAKE_S`・`IS_ROCKETLAKE`・`DISPLAY_VER` の条件）のが最大の危険。p006〜p010 の受け入れに「Linux の platform の分岐の一覧（関数ごと）と xd の記述の field の対応」の表を含める。
- **display 12 の実測が無い**: 5320 の dump は無く、RKL・ADL-S は実機が無い。display 12 の host 試験の期待値は Linux の表の値で、「Linux と同じ値を書く」事しか確かめられない。5320 の確かめ（ユーザー）が display 12 の唯一の実機の証拠。
- **GOP の引き継ぎ**: takeover の disable の順を間違えると画面が消えたまま戻らない。p009 の host 試験で順序を固定し、実機では `i915.display=old` が逃げ道。
- **Type-C**: IOM の ready と AUX USBC の well の挙動は実機でしか試せない。p010 の実機の確かめが要る。TGL の流れ B は実機の実績が無い。
- **DBUF の遷移（1 ↔ 2 画面）**: vblank に同期した DDB の書き換えを誤ると resident の underrun。p008 の host 試験で列を固定し、p010 の実機で確かめる。
- **DMC**: 載せ方を間違えると DC6 で display が止まる。p006 の parse の試験 + p011 の suspend の実機。
- **試験の資産の移行**: 旧試験の期待値を落とすと退行に気づかない。§6.4 の表で移す物を名指した。
- **LW の超過**: p006・p010・p014 が大きい。p010 は流れ B を切り出せる（§7 #17）。p003 は adapter の型の変換が肥大しうる（`xd.h` の型を小さく保つ）。
- **照合の Linux の tree が P1 の worktree の `build/` にある事**: 出典の表記は「Linux v6.8.12 <path> (<関数>)」で版と path が自己完結し、tree の場所に依らない。tree が消えたら kernel.org の `linux-6.8.12.tar.xz` を取り直して検証する（WS229 p001 の手順）。担当ごとに読み取り専用で使う。
## 8. レビュー 1（design-review-1.md）の指摘ごとの扱い

B1（記録の誤り）は Q1 が取り消し済み。下の表は B2〜B6・M1〜M19・m1〜m14。「直した」は元の資料で事実を確かめて設計を変えた物、「直さない」は理由を書いた物。

| 指摘 | 扱い | 確かめた事実 / 理由 | 節 |
| --- | --- | --- | --- |
| B2 raw clock 38.4 MHz | 直した | `dmesg-display.txt:35` rawclk 19200 kHz、`vbt-decode.txt:470` PWM 200 Hz、96000 = 19.2 MHz / 200 Hz。Linux `cnp_rawclk` は SFUSE_STRAP で 19.2/24 を選ぶ | §1、§2.1、§2.2、§2.11 |
| B3 CDCLK を変えないと init/uninit の矛盾 | 直した | Linux `bxt_cdclk_init_hw`（sanitize、妥当でなければ最小に設定）と `bxt_cdclk_uninit_hw`（bypass）。xd-clock に読み・sanitize・set・uninit を入れ、mode set からの変更だけを §7 #2 に残した | §2.2、§2.6、p006 |
| B4 `i915.display=old` が成り立たない | 直した（決定 1、案 (a)） | 口 `xd.h` と ops の表を p003 で決め、own の file を付け替え、旧 code は adapter で口を実装、p009 で表を差し替え、p012 で消す | §4.2、§4.3、§6.1、p003 |
| B5 Type-C を TGL の機構で書いている | 直した | Linux `adlp_tc_phy_ops`: live は DE_HPD_ISR + SDEISR、ready は TCSS_DDI_STATUS、所有は DDI_BUF_CTL bit 6、TCCOLD の block は AUX USBC の well（`adlp_tc_phy_cold_off_domain`）、lane は DFLEXDPSP/DFLEXPA1、FIA は modular。mailbox 0x80000026 は `tgl_tc_cold_request`（TGL）だけ | §2.5、§2.8、§3.2 |
| B6 power well の構造と DE_PIPE の割り込みの再設定 | 直した | `xelpd_pwdoms_pw_a` は PIPE_A だけ、`xelpd_pwdoms_pw_2` は B/C/D + port、dump 0xc03 = PW_1 + PW_A。`hsw_power_well_post_enable` → `gen8_irq_power_well_post_enable`（irq_pipe_mask）。今の own の `interrupts.c` に同じ hook（`drv_i915_pw_irq_ops`）がある | §1、§2.5、§4.2、§4.3 |
| M1 CDCLK は crawl、表は adlp_cdclk_table | 直した | `dmesg-display.txt:17-18`、`adlp_cdclk_table` の 38.4 MHz の 5 行 | §2.6、T5 |
| M2 pipe count の mailbox は DG2 専用 | 直した | `intel_cdclk.c` の `DISPLAY_TO_PCODE_*` は `if (!IS_DG2) return`、`intel_pmdemand.c` は display 14+ | §2.6 |
| M3 DBUF の表の出典、MBUS の join、1→2 画面 | 直した | `adlp_allowed_dbufs`（A 単独は join か S1+S2、A+B は S1+S2 / S3+S4）、`mbus_ddb_offset`、m3 の 2 画面の実測（MBUS_CTL 0x700、A 0x07db0000、B 0x07d50000）。plan に pipes の入力 | §1、§2.10、§3.2、§3.3、p008 |
| M4 VBT の DDC pin と GMBUS の pin | 直した | `adlp_ddc_pin_map`（VBT 3..6 → GMBUS 9..12）、`gmbus_pins_icp` | §2.9、§2.13、V3 |
| M5 DMC の WA、handler の disable、PIPEDMC_CONTROL | 直した | `adlp_pipedmc_clock_gating_wa`（Wa_16015201720）、`disable_all_event_handlers`、`intel_dmc_enable_pipe`（crtc enable の後） | §2.3、§2.12 |
| M6 lock の規則（DDI_BUF_CTL の 2 つの書き手、AUX の共有） | 直した | link の refcount で DDI_BUF_CTL の書き手を分け、AUX は channel ごとの mutex | §3.4 |
| M7 TC の IN_USE → FREE が無条件 | 直した | Linux `intel_tc_port_unlock` → 1 s の `disconnect_phy_work`、`link_refcount` | §2.8、§3.5 |
| M8 §4.2 の表が網羅していない、「Gen9 は一切触らない」は誤り | 直した（数は訂正） | own の file の呼び出しを header の宣言と照合: display.c 78、head.c 17、output.c 15 …。`tc-kern.h` の export は 5（`tc.h` は 19）で「17」ではないが、表の不足は事実。Gen9 は `internal.h` の own の型（`i915_scanout`・`i915_resident_display`・`i915_lcd_mode`・`i915_pch_state`）と `drv_i915_delayed_*` を使う。`dc9.c`・`capture.c` の扱いを足した | §3.1、§4.1、§4.2 |
| M9 kernel の execution 試験が抜けている | 直した | `ktest-display.c`（3452 行）・`ktest-display-probe.c`（3078 行）は `internal.h` の Linux の型に依存、`ktest-gt.c` は `power.h`・`hotplug.h` を include | §6.3、§6.4、p012 |
| M10 host 試験の置き場所 | 直した | source は tree の `tests/display/`、script と fixture は `plan/ws245/tests/` で p012 に master.md の Tools へ登録（WS237 と同じ形） | §6.3 |
| M11 依存の誤り | 直した | p007 は p006 に依存。独立は p004〜p006。p011 は p010 にも依存（HPD の再 arm）。p012 は p011 | §6.2 |
| M12 oracle の不整合 | 直した（一部は指摘と違う） | PLANE_WM0/BUF_CFG は `regs-selected.txt` の 60–61 行（無名）と m3 の `range-0x70000.txt` にある。PORT_TX は `regs-selected.txt` には無く m3 の `range-0x160000.txt`/`range-0x16C000.txt` にある。top の `i915_cdclk_info.txt` は空だが `debugfs/i915_cdclk_info.txt` に 192000 kHz。cursor の DDB は Linux と同じ 36 block の予約にして oracle を一致させた | §1、§2.10、§6.3 |
| M13 suspend・resume の quiesce と HPD の再 arm | 直した | Linux `i915_drm_suspend`（display の suspend → HPD の work の取り消し）、`i915_drm_resume`（DMC → HPD init → display の resume → poll disable） | §2.14、§3.6、p011 |
| M14 GOP の引き継ぎで VGA plane と色の register | 直した | Linux `intel_vga_disable`（probe で 1 回）、`icl_color_commit_arm`（BOTTOM_COLOR・GAMMA_MODE・CSC_MODE を毎回）。今の `takeover.c` も VGA_CONTROL 0x41000 を扱う | §2.3、§2.10、§2.14 |
| M15 PRM の許諾が ADL-P に及ぶか | 直した（決定 3・4） | 手順の根拠を Linux に移し、PRM は意味の理解と Linux に無い値だけ。PRM の文は写さない | §0.2 |
| M16 §5.4 が guardrail と食い違う | 直した（決定 3） | Procedure referenced に Linux の関数を書く規則に | §5.4 |
| M17 module が Linux の file と 1 対 1 | 直した | 境界の根拠（hardware の単位）の列を足し、25 → 19（opregion→vbt、pcode→io、phy+hdmi→ddi、dkl→tc、pps+backlight→panel、trans+plane→pipe、wm→dbuf）。残る 1 対 1 は hardware/規格が 1 単位の物 | §3.2 |
| M18 LW の過小評価と換算 | 直した（換算は 2026-10-15 に 1 LW ≈ 15〜25 分へ訂正） | 1 LW ≈ 1〜2 時間で揃え、足した作業を数えて 36 → 51 LW | §6.2 |
| M19 Tiger Lake を支えない事は機能を失う | 直した（改訂 3） | 改訂 2 は「display 12 の分岐は持たない」としたが誤り（2026-10-15 ユーザー「Tiger Lakeは実機で動作確認済みです。」）。改訂 3 で display 12（TGL・RKL・ADL-S）を xd の対象に入れた（表 P1・P2） | 決定 2、§1.1、§4.4 |
| m1 ADL の公開 PRM の有無の再確認 | 直さない | 決定 3 で PRM は補助になり、ADL-P の版の有無は設計を止めない。2026-10-15 の索引の確認を記録に残す（再確認はしていない） | §0.1 |
| m2 DPLL4 | 直した | `adlp_plls` は DPLL0/1・TBT・TC PLL 1..4。DPLL4 は無い | §2.1、§2.6 |
| m3 CFGCR0 の WA | 直した | `ehl_combo_pll_div_frac_wa_needed`（ADL-P・38.4 MHz で小数を半分）。5330 の 0x00e001a5 の 0x3800 = 0x7000 / 2 | §1、§2.6、T3 |
| m4 AUX の 600 µs と timeout の混在 | 直した | 600 µs は DDI_AUX_CTL の timeout の最小値（PRM）。xd は TIME_OUT_MAX（4000 µs）を使う。TCCOLD の 600 µs は TGL の mailbox の話で ADL-P には無い | §2.9 |
| m5 GMBUS の NAK | 直した | Linux `gmbus_xfer`: idle を待って SW_CLR_INT、最初の message は 1 回再試行。今の `gmbus.c` も `retries = 1` | §2.9、§2.13 |
| m6 register の名前 | 直した | PRM の名前は PIPE_BOTTOM_COLOR（TGL Vol 2c に 8 箇所）、PIPE_ARB_CTL も TGL Vol 2c にある | §2.10、§5.1、§5.2 |
| m7 IPC の enable | 直した | `dmesg-display.txt:26` has_ipc yes、Linux `intel_enable_ipc`（DISP_ARB_CTL2） | §2.10、§3.2 |
| m8 resident と head の pipe の衝突 | 直した | head は resident と違う pipe（`head-rules.c` の規則を plan に） | §3.3 |
| m9 HPD long で CONNECTED の再 probe | 直した | Linux `intel_hotplug_detect_connector`（epoch の変化で CHANGED） | §3.5 |
| m10 p002 の試験と p011 | 直した | 旧 header と比べる試験は p012 で dump と比べる試験に置き換える | p002、p012 |
| m11 p002 から kernel の build に入れる | 直した | xd の file は p002 から `AMD64_I915_SOURCES` に | p002 |
| m12 hotplug の license は MIT、grep の文字列 | 直した | `intel_hotplug.c`（MIT の文）・`intel_hotplug_irq.c`（SPDX MIT）。今の tree の文字列は "Derived from the Linux kernel v6…" 26 箇所と "derived from the Linux kernel (…)" 6 箇所、22 file | §0.2、§5.4、H1 |
| m13 照合の Linux の tree が P1 の build/ に依存 | 直した（制限として記録） | 出典の表記は版と path で自己完結。tree の取り直しの手順を書いた | §0.1、§7 の危険 |
| m14 記述の細部 | 直した（改訂 3、§9） | design-review.md の原文の m1〜m16 として扱った | §9 |

合計: 直した 37（M8・M12 は指摘の一部を訂正しつつ直した。M19・m14 は改訂 3 で直した）、直さない 1（m1）。

## 9. レビュー 2（design-review.md、初版 522 行への原文のレビュー）の指摘ごとの扱い

レビュー 2 は初版（6bccaef57）を対象に書かれ、改訂 2 がレビュー 1（レビュー 2 の要約）で既に直した物を含む。「直した（改訂 2）」は改訂 2 で直っていた物を改訂 3 で確かめた物、「直した」は改訂 3 で直した物、「直さない」は理由を書いた物。事実は Linux v6.8.12 の source（path は `drivers/gpu/drm/i915/`）か今の code で確かめた。

| 指摘 | 扱い | 確かめた事実 / 理由 | 節 |
| --- | --- | --- | --- |
| B1 ACM（DG2）を display 13 の差分として ADL-P に当てている | 直した（改訂 2 で squash・pipe count・pmdemand は消えていた。改訂 3 で ACM の使い方を「south display の offset の確認だけ」に限定し、DG2 専用の物を列挙） | `intel_display_device.c:690-705` xe_lpd は crawl、squash は xe_hpd。`intel_cdclk.c` の `DISPLAY_TO_PCODE_*` は `IS_DG2`。`intel_phy_is_snps` は DG2 だけ | §0.1、§2.6 |
| B2 Type-C の model が TGL の物 | 直した（改訂 2 で ADL-P の流れ。改訂 3 で TGL・ICL の流れ B を分けて書き、矛盾していた 2 つの bullet を 1 つに） | `intel_tc.c:706-750, 941`: `tgl_tc_phy_ops` は `icl_tc_phy_*`（DFLEXDPSP/DPPMS/DPCSSS）+ `tgl_tc_phy_cold_off_domain`（TC_COLD_OFF の pcode の well）、`adlp_tc_phy_ops` は ISR・TCSS_DDI_STATUS・DDI_BUF_CTL bit 6・AUX の well | §1.2、§2.5、§2.8 |
| B3 seam の一度の切り替えと `old` の逃げ道と Phase の分割が両立しない | 直した（改訂 2 の決定 1 の案 (a) で own の file は p003 で付け替え。改訂 3 で **p009・p010 の既定を `old` に**し、xd の既定は p011） | 改訂 2 は p009 で xd を既定にしていた（§6.1 の旧文）。p009 の xd は eDP だけなので退行する | §6.1、§6.2、§7 #0 |
| B4 試験の道が 5330 の passthrough を使わない | 直した | `plan/tools/hw5330/README.md` §1.2（passthrough は日常の試験、`/tmp/i915-hw.lock`）、`vmunix.mk:218-229` の `I915_TEST_SET=display`。制約（OpRegion 無し、GOP の takeover と suspend は不可）を書き、7 scenario を xd に付け替える | §6.3、§6.4、§7 #15 |
| M1 raw clock 19.2 MHz、96000 = 19.2 MHz / 200 Hz | 直した（改訂 2） | `cnp_rawclk`: SFUSE_STRAP で 24/19.2。`dmesg-display.txt:35` | §1.3、§2.11 |
| M2 DMC の載せ方（0x8F000 は handler の register）と欠落 | 直した（改訂 2 で WA・handler・PIPEDMC は入っていたが「main は 0x8F000 から」の誤りが残っていた。改訂 3 で header の startMMIOAddr に直し、TGL の handler の差し替えと v1 を足した） | `intel_dmc_load_program`: `DMC_PROGRAM(start_mmioaddr, i)`。`parse_dmc_fw_header`: v3 は header の値、v1 は 0x80000。`TGL_MAIN_MMIO_START 0x8F000` は EVT_CTL の base。`disable_dmc_evt`: TGL の CLK_MSEC、TGL/ADL-S の VBLANK_A | §2.12 |
| M3 Gen9 との境界（internal.h の型、output.c の口） | 直した（改訂 2） | `gen9-node.c`・`gen9-backlight.c`・`gen9-flip.c` の own の型の使用。`display-internal.h` へ分離、`struct i915_display_mode` を共有 | §3.1、§4.1 |
| M4 §4.2 から抜けている own の file と口 | 直した | `tc-kern.c`（739 行、17 関数、UCSI の報告）、`capture.c`、`dc9.c`、`display.h` の `_output_moved/_back/_fail_back/_lost`・`_gpu_retained`・`_abandoned`・`_session_close`・`_irq_sync_failed`・`_destroy`、`head.c` の `flip_settle` を表に入れ、扱いを決めた | §3.2、§4.1、§4.2 |
| M5 出力の移動と 2 pipe の DBUF の再点灯の契約 | 直した | `present.c` の "lit again for two pipes"、`i915_display_move`、EAGAIN の fallback。`run_pipes`・`i915_xd_run_result`（UNPLUGGED/SPARED）・EAGAIN・`_dbuf_state` の契約の表を足した | §4.2、§4.3、§4.4 |
| M6 GOP が eDP 以外を灯して起動した時の p008（改訂 2 の p009）の振る舞い | 直した | p009 の xd は opt-in で、GOP が eDP 以外なら GOP の絵を残す（host 試験 (d)） | §2.14、§6.1、p009 |
| M7 power well と割り込みの結びつき（drain の失敗で落とさない） | 直した（改訂 2 で hook は入っていた。改訂 3 で「drain の失敗で well を落とさない」を明記し p006 の試験に） | `interrupts.c:185-187` の `drv_i915_pw_irq_ops`、`internal.h:421-424` | §2.5、§3.2、p006 |
| M8 VGA の無効化と VGA の decode | 直した（改訂 2） | `intel_vga_disable`、今の `takeover.c` の VGA_CONTROL 0x41000 | §2.14 |
| M9 並行性の穴 (a) DMC の worker (b) TC の IN_USE→FREE (c) syscall の読み手 (d) rank | 直した | (a) load の間 DC off を保持し PIPEDMC の enable は request worker に queue、(b) partner gone は screen の disable の後に所有を返す 2 段、(c) connector の表の写しは `xd.lock` の下、(d) 全 mutex が LOCK_RANK_DEVICE なので順序は文書と host 試験の lock の記録で守る（`include/kern/lock.h:44`） | §3.4、§3.5 |
| M10 watermark の oracle と PRM の算法 | 直した（改訂 2 で cursor 36 と 4 slice の出典。改訂 3 で「選択の規則は Linux の `skl_compute_plane_wm`（決定 4）」を明記） | `skl_compute_plane_wm` の method 1/2 の選択、`adlp_allowed_dbufs`。TGL の PRM の 12031 行は 2 slice の表（指摘のとおり） | §2.10、T6 |
| M11 出典の規則（Procedure referenced）と構造の持ち込み（W1 の domain の集合、display.h の段） | 直した（§5.4 は改訂 2。改訂 3 で W1 から domain の集合を外し「機能」の列に、段の扱いを §7 #12 に） | guardrail:105。`xelpd_pwdoms_*` は software の抽象 | §2.5、§5.3、§5.4、§3.6、§7 #12 |
| M12 PRM の license の範囲 | 直した | `tgl-vol1-preface.txt:23-30` の 3 点を §0.2 に書き、判断を §7 #11 に。既定は「PRM は照合だけ」 | §0.2、§7 #11 |
| M13 5330 の VBT の生の bytes は削除済み | 直した | `plan/ws233/ws.md:34`、m3 の `debugfs/` に `i915_vbt.txt` は無い（2026-10-15 の ls）。fixture は合成 | §0.1、§0.2、§2.13、§5.1、§7 #7 |
| M14 Phase の依存の誤りと他の WS との依存 | 直した | p013 ← p011・p012・ユーザー。p004〜p006 が独立。WS244・WS051・WS113・WS084・WS075 の依存を §6.2 に、扱いを §7 #16 に | §6.2、§7 #16 |
| M15 試験の資産の一覧と fake の model の付け替え | 直した | `lcd-fake-hw.c` の 204 箇所、`dp-fake-hw.h:26`・`lcd-fake-hw.h:34` の `internal.h`。own の model `xd-fake-regs.c` に書き直し、execution の組と display の組の扱いを表に | §6.3、§6.4、p002・p005・p007 |
| M16 LW の換算と実機の往復 | 直した | 1 LW ≈ 15〜25 分で 76 LW（≈ 19〜32 h）。実機の往復 6〜10 LW を別枠に。p009/p010 を分け、p012 を足した | §6.2 |
| M17 `i915.display=old` は boot の parser で無視される。他の boot の語 | 直した | `src/kern/boot.c:38-58, 250-263`（表に無い名前は record_unknown）。今の語を §4.1 に列挙、`kern/boot.c` の変更を p003 で Q1 の承認、start の失敗で GOP を残す | §2.14、§4.1、§6.1 |
| M18 §7 の判断の抜け（9 件） | 直した | 1 ACM → §0.1 で閉じた（判断不要）、2 PRM → #11、3 passthrough → #15、4 切り替えの時期 → #13、5 PCI ID の範囲 → 対象の機種で閉じた（全部受ける）、6 tc-kern → §3.2 で閉じた、7 WS の分担 → #16、8 退行の期間 → #0（退行させない）、9 合成の VBT → #7 | §7 |
| m1 ADL-P に無い資源（DPLL4、DDI C、DDI D/E） | 直した（改訂 2 で消えていた。改訂 3 では多機種なので、platform ごとの資源を表 P1 に分けて書いた） | `rkl_plls` に DPLL4、`adls_plls` に DPLL2/3 | §1.1、§2.6 |
| m2 eDP の版 | 直した | `dpcd-drm_dp_aux0-700.bin` の 0x700 = 0x01 → eDP 1.2、0x000 = 0x11 | §1.3、§2.13 |
| m3 VBT の DDC pin と GMBUS の pin | 直した（改訂 2。改訂 3 で 4 platform の表に） | `map_ddc_pin` の 4 つの表 | §2.13、V3 |
| m4 MBUS の join は pipe A だけではない | 直した | `check_mbus_joined` は active_pipes の行の `join_mbus`。`adlp_allowed_dbufs` の join の行は A 単独と B 単独 | §2.10 |
| m5 「入らない mode は拒む」の条件に voltage level と QGV | 直した | 今の `clock.c:819-840`・`modeset.c:3131-3141` の規則を plan の 3 条件に | §2.3、§3.3、§4.2 |
| m6 DDI A の lane capability | 直した | PRM TGL Vol 12 の前提。init で読んで保つ | §2.2 |
| m7 「kernel には link するが呼ばれない」と「p008 で足す」の矛盾 | 直した（改訂 2） | p002 から `AMD64_I915_SOURCES` | p002 |
| m8 kernel の 16 MiB の上限 | 直した | `AMD64_KERNEL_MAX_BYTES` 0x1000000、`vmunix.mk:218`（試験は 1.4 MiB）。p009 の受け入れに `size` の確認 | §6.1、p009 |
| m9 PCI revision → stepping の表 | 直した | `intel_step.c` の 9 つの表を S1 に | §1.1、S1 |
| m10 `output.c` の自身の関数、`bootfb.c` は own | 直した | `output.c:212, 268`。§4.1・§4.2 の文を直した | §4.1、§4.2 |
| m11 storm の検出は「今と同じ」ではない | 直した | `hotplug.c:124-129`（50/s、2 分）。rate の上限を xd-hpd に持ち、poll だけ持たない | §3.2、§3.5、§7 #4 |
| m12 suspend の戻りで PPS・PWM を毎回書く、panel への戻り | 直した | `present.c:766-777`。§2.11 に毎回書く事、§2.14 に own の規則との境界 | §2.11、§2.14 |
| m13 DC5/DC6 の「PG3 以上を落とし」は TGL の言い方 | 直した（改訂 2 で文は消えていた。改訂 3 で platform ごとの落ちる well を書き、xd は一覧を持たないと明記） | — | §2.5 |
| m14 §6.4 に log の文言の一覧が無い | 直した（一部） | 今の code の `"i915: <接頭辞>"` の数（resident 50、gen9 46、display 41、hpd 21、N0 15、takeover 6 …）。残す接頭辞と捨てる接頭辞を §4.4 に。file ごとの全文の一覧は p011 で確定 | §4.4、p011 |
| m15 p002 の試験と旧 header の削除 | 直した（改訂 2） | p013 で置き換える | p002、p013 |
| m16 TGL を支えない事は退行 | 直した | 対象の機種（display 12 を含む） | 決定 2、§1.1、§4.4 |

合計: 直した 38（うち改訂 2 で直っていて改訂 3 で確かめた物 9: M1・M3・M8・m3（表の分割は 3）・m7・m13・m15 と B1・B2 の一部）、直さない 0。レビュー 2 の「確かめられなかった事」のうち、m8 の xd の object の大きさは未測定のまま（p009 の受け入れで測る）、DFLEXDPSP の ADL-P での読みは試さない（流れ A は ISR を使う）。
