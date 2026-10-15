<!-- awesome-plan project=zedbsd record=ws245-p001-design -->

# WS245 の設計: Gen12（ADL-P、Latitude 5330）の display の書き直し

> **2026-10-15 の前提の訂正（次の改訂で本文を直す）**: 2026-10-15 ユーザー「すでに多数の機種で動作しており、5330を特別扱いはしません。Skylake～Raptor Lake Refreshまですべてサポートします。」 対象は display 12・13 の全機種で、5330 は対象機種の 1 つ（実測の資料がある機種）に過ぎない。本文の「5330 だけ」「ADL-P だけ」を前提にした所（§1・§2 の ADL-P の分岐だけの model、§6 の実機の確かめ、§7 #7 の 5330 の VBT の bytes を fixture にする案（2026-10-14 のユーザーの判断で VBT の raw は削除、ws233）など）は改訂で直す。

2026-10-15 P2（q991、ws245-p001）の初版を、2026-10-15 P1 が [design-review-1.md](design-review-1.md) の指摘と
ユーザーの決定 1〜4（[ws.md](ws.md)「ユーザーの決定」）で改訂した物（改訂 2）。親: [p001](phase001/phase.md)・[WS245](ws.md)。code は書かない（p002 以降）。
今の code（`src/drivers/gpu/i915/display/`、`intel/`）は「外から見える振る舞いと register の書き込みの列」を知るためだけに読んだ。
その構造・型・名前・制御の流れは、この設計に持ち込まない（§0.3 の「持ち込まない物」）。指摘ごとの扱いは §8。

決定の要約（この改訂で設計に入れた物）:
1. 口 `xd.h` を p003 で決め、own の file は最初からその口だけを呼ぶ。口の実装は旧 code への薄い adapter と xd の 2 つで、
   boot の語で丸ごと切り替える（状態を混ぜない）。adapter は p012 で消す（§6.1）。
2. 「5330のみ」。xd は display 12（Tiger Lake、5320）と display 13（ADL-P、5330）の両方を持つ（2026-10-15 ユーザーの補足「Tiger Lakeは実機で動作確認済みです。display 10,11以外は動作確認済みです。」「記録が食い違っているだけです。」）。改訂 2 で書いた「display 12 の分岐は持たない」は誤りで取り消した。display 12 の差（W1・T5・PLL の enable・Type-C の流れ・PSR など）の設計は次の改訂で足す（§4.4）。
3. 手順の第一の出典は Linux v6.8.12 の code。Intel の公開の PRM（TGL・ACM）は register の意味と値の補助。code・comment・構造は写さず、
   値は block ごとに「Value(s) referenced:」、手順は「Procedure referenced: <版> <path> (<関数>)」を付ける（§5.4）。
4. PRM と Linux の値・手順が食い違う時は Linux を優先する（5330 で動いている実績）。

## 0. 出典とライセンス

### 0.1 出典の優先（決定 3・4）

| 順 | 出典 | 使い方 |
| --- | --- | --- |
| 1 | **Linux v6.8.12** `drivers/gpu/drm/i915/`（展開 `/home/awe/zedBSD-worktrees/p1/build/ws229-p001/linux/linux-6.8.12/`、WS229 p001 が kernel.org の tarball を検証して展開した物、読むだけ） | 手順（register の列・待ち・条件分岐の規則）と値（offset・bit・表）の第一の出典。ADL-P の分岐（`IS_ALDERLAKE_P`、`DISPLAY_VER == 13`、`PCH_ADP`）だけを読む |
| 2 | 5330 の実測（`plan/ws031/display-ref/`: Linux の register dump・dmesg・debugfs・DPCD・EDID。`plan/ws051/tests/m3-5330-20261007/`: USB-C DP を挿した 2 画面の dump、`debugfs/i915_vbt.txt`（VBT 8704 byte の生の bytes）、`debugfs/i915_opregion.txt`） | 値の照合と host 試験の oracle（§6.3） |
| 3 | Intel の公開の PRM（下の表。`/home/awe/zedBSD-worktrees/p2/plan/ws245/temp/` に PDF と `pdftotext -layout` の text。git には入れない。再取得は `https://cdrdv2.intel.com/v1/dl/getcontent/<id>`） | register の意味（field の定義）と、Linux の code だけでは読めない理由（なぜその順か）の補助。Linux と食い違えば Linux |

PRM の版: Alder Lake（ADL-P）の版は公開されていない（2026-10-15 に索引 `developer-reference/1-0/overview.html` を確認: Alchemist、Rocket Lake、
Tiger Lake、Lakefield、DG1、Ice Lake 以前のみ）。決定 3 の下では、これは設計を止めない（手順は Linux の ADL-P の分岐から取る）。

| 文書 | id | 版 | 補助として使う所 |
| --- | --- | --- | --- |
| TGL Vol 12 Display Engine（IHD-OS-TGL-Vol 12-12.21） | 705833 | 2021-12 | mode set・power well・DC state・CDCLK・DPLL・DKL・combo PHY・AUX・watermark の手順の意味 |
| TGL Vol 2c Registers A–L / M–Z | 703046 / 703047 | 2021-12 | north display の register の field の定義 |
| TGL Vol 1 Preface / Vol 14 Workarounds | 703034 / 705836 | 2021-12 | ライセンスの文 / WA の意味 |
| ACM（DG2）Vol 12 Display Engine（IHD-OS-ACM-Vol 12-3.23） | 774520 | 2023-03 | display 13 の差分の意味、South Display Engine（PP・PWM・GMBUS・south の HPD）。**DG2 専用の手順（pipe count の mailbox、SNPS PHY）は ADL-P に当てない** |
| ACM Vol 2c Registers Part 1 / 2 | 774481 / 774482 | 2023-03 | south display と display 13 の register の field |

### 0.2 ライセンス

- Linux v6.8.12 の i915 の display の file は全部 **MIT**（`SPDX-License-Identifier: MIT` か "Permission is hereby granted" の文。2026-10-15 に `i915_reg.h`、
  `intel_hotplug.c`、`intel_hotplug_irq.c`、`intel_tc.c`、`intel_display_power_map.c`、`intel_cdclk.c`、`intel_dmc.c`、`skl_watermark.c`、`intel_bios.c`、
  `intel_gmbus.c`、`intel_dpll_mgr.c`、`intel_ddi.c`、`intel_ddi_buf_trans.c`、`intel_dp_link_training.c`、`intel_combo_phy.c`、`intel_dkl_phy.c`、
  `intel_pps.c`、`intel_backlight.c`、`intel_display_power.c`、`intel_display_power_well.c`、`intel_display_irq.c`、`intel_vga.c`、`intel_display.c`、
  `intel_dp.c`、`intel_dp_aux.c`、`intel_opregion.c`、`intel_vbt_defs.h`、`intel_display_driver.c`、`intel_hdmi.c`、`intel_color.c` の先頭で確認）。
  初版が「GPL の file（`intel_hotplug.c` など）」と書いたのは誤り。guardrail の規則は MIT・GPL とも同じなので扱いは変わらない。
- guardrail「MIT・GPL の出典のデータの引用」（2026-10-15）のとおり: 値・表はデータとして引用し block ごとに出典を示す。**code・comment・構造は写さない**。
  参考にした手順も block ごとに「Procedure referenced:」で示す（§5.4）。file の先頭には出典・派生の宣言・SHA を一切書かない。
- PRM（Vol 1 Preface の Notices、TGL・ACM とも同文）: 「you may publish an unmodified copy」、文書の中の code は 0BSD、「You may create software
  implementations based on this document … intended to execute on the Intel product(s) referenced」、「No rights are granted to create modifications or
  derivatives of this document」。決定 3 により実装の根拠は Linux の code なので、「referenced product（TGL・DG2）で動く実装」の条項が ADL-P の実装に
  及ぶかという問い（初版の §7 #11、レビュー M15）は設計を止めない。PRM から取る物は register の意味の理解と、Linux に無い値（§5.3 で PRM を第一とする
  行）だけで、その値は「Value(s) referenced: IHD-OS-TGL-Vol 12-12.21 §<章> (<表の名前>)」で示す。PRM の文を comment に写さない（要約も自分の言葉で）。
- 5330 の実測（EDID・DPCD・register の語・VBT の bytes）は値の照合と試験の fixture に使う。panel の EDID と DPCD は「licence and redistribution not
  audited」の注記のまま（`tests/display/dp-fixture-latitude5330.h`）。VBT の bytes（`m3-5330-20261007/debugfs/i915_vbt.txt`）は firmware の表であり、
  試験の fixture として tree に置く時は同じ注記を付ける（§7 #7）。
- `userland/base/licenses/i915-driver/`（LICENSE・Makefile）の整理は WS244 の最後。この WS は Gen12 の file から "Derived from the Linux kernel"
  （2026-10-15 の grep: 22 file、"Derived from the Linux kernel v6…" 26 箇所、"derived from the Linux kernel (…)" 6 箇所）と Linux の MIT の表示を消す所まで。

### 0.3 今の code の読み方（この設計で守った規則）

- 読んだのは「何をするか」: register の列と待ち、判断の規則、own の file から呼ばれる関数と型（§4.2 は 2026-10-15 に header の宣言と own の file の
  識別子を突き合わせて数えた）、試験が何を測るか（§6.4）。
- 持ち込まない物: Linux の型（`intel_crtc_state`・`drm_i915_private`・`intel_encoder`・`i915_lcd_*` の環境）、関数の分け方（`pre_enable`/`enable`/
  `post_disable` の encoder の段、`crtc_enable` の hook、`intel_tc_phy_ops` の表）、atomic state の形（global obj、`i915_lcd_words` の recorder）、
  "world" の割り当て、Linux の名前（`hsw_*`・`icl_*`・`tgl_*`・`adlp_*`・`xelpd_*`）、comment の文。Linux の file の分け方も持ち込まない（§3.2 の境界は
  hardware の単位で決める）。
- 残す物: 外向きの口（§4.1）、boot の語・log の文言のうち試験と運用が読む物（§4.4）、hardware の事実（§1）、試験の oracle（§6.3）。

## 1. 対象の hardware の事実（5330、display-ref と m3 の dump から）

| 項目 | 事実 | 出典 |
| --- | --- | --- |
| GPU | ADL-P Iris Xe、display 13（Linux の "xelpd"）、GT は graphics 12 | `device-info.c`、`dmesg-display.txt` |
| PCH | Alder Lake-P（ADP-P、Linux の `PCH_ADP`）。south display は PCH 側（GMBUS・PP・PWM・SHOTPLUG・SDE の割り込み）。**raw clock 19.2 MHz**（`dmesg-display.txt:35` "rawclk rate: 19200 kHz"。SFUSE_STRAP の bit で 24 か 19.2 を選び、RAWCLK_FREQ に分子・分母を書く: Linux `intel_cdclk.c` (cnp_rawclk)）。参照 clock（DSSM）の 38.4 MHz とは別の clock | `dmesg-display.txt`、Linux `cnp_rawclk` |
| 参照 clock | 38.4 MHz non-SSC（DSSM で読む） | `dbg-i915_shared_dplls_info.txt`（"PLL refclks: non-SSC: 38400 kHz"） |
| 内蔵 panel | eDP、AUO B133HAN、1920×1080、60 Hz（pixel 140.8 MHz）と 48 Hz、6 bpc（dither）、290×170 mm。DDI A / combo PHY A / pipe A / transcoder A / AUX A / DPLL0。DPCD 1.1、HBR（2.7 GHz）× 2 lane、eDP 1.4 の表示制御（0x700）あり、PSR 無し | `dbg-i915_display_info.txt`、`dp-fixture-latitude5330.h`、`regs-selected.txt` |
| DPLL0 の語 | CFGCR0 0x00e001a5（DCO 整数 0x1a5、小数の field は 0x3800 = 表の 0x7000 の半分: ADL-P の 38.4 MHz の WA、§2.6）、CFGCR1 0x00000088（HBR） | `dbg-i915_shared_dplls_info.txt`、Linux `intel_dpll_mgr.c` (ehl_combo_pll_div_frac_wa_needed) |
| transcoder A の語 | TRANS_DDI_FUNC_CTL 0x8a210002（DP SST、DDI A、2 lane、6 bpc） | `regs-selected.txt` |
| backlight | PCH の PWM controller 0、BLC_PWM_PCH_CTL2 0x00017700 = 周期 96000 = **19.2 MHz ÷ 200 Hz**（VBT block 43 の "PWM freq: 200"）、enable bit 31、極性 0 | `regs-selected.txt:6-7`、`vbt-decode.txt:470` |
| CDCLK | crawl あり・squash 無し（`dmesg-display.txt:17-18`）。m3（2 画面）の "Current CD clock frequency: 192000 kHz、Max 652800 kHz" | `dmesg-display.txt`、`m3-5330-20261007/debugfs/i915_cdclk_info.txt` |
| 外部の口 | HDMI-A-1（DDI B、combo PHY B、DVI mode で駆動）、DP-1 = TC1、DP-2 = TC2（USB-C、DP-alt。m3: TC2・pin assignment C・4 lane・1920×1280・pipe B・TC PLL 2） | `00-summary.txt`、`m3-5330-20261007/README.md` |
| DMC | `i915/adlp_dmc.bin` v2.20（main と pipe A/B の firmware）。package は `userland/firmware/i915/`（linux-firmware 20260410、sha256 は manifest） | `dbg-i915_dmc_info.txt`、`i915-firmware.manifest` |
| DBUF（1 画面） | 4 slice × 1024 block、MBUS join。pipe A の plane 1 に 0–4060（PLANE_BUF_CFG 0x0fdb0000）、cursor に 4060–4096（36 block） | `dbg-i915_ddb_info.txt`、`regs-selected.txt:61` |
| DBUF（2 画面、m3） | join 無し（MBUS_CTL 0x4438c = 0x00000700）、4 slice とも on（DBUF_CTL 0x45008/0x44fe8/0x44300/0x44304 = 0xc041c000）。pipe A は S1+S2 で PLANE_BUF_CFG 0x07db0000（0–2011、slice の組の先頭からの相対）、pipe B は S3+S4 で 0x07d50000。PLANE_WM0 は両 pipe とも 0x80004010。PIPE_MBUS_DBOX_CTL A 0xa9038804、B 0xb1038804 | `m3-5330-20261007/range-0x44000.txt`、`range-0x45000.txt`、`range-0x70000.txt` |
| power well | Linux の読み（`xelpd_power_wells`）: PW_1（index 0、DDI A/B・transcoder A・DBUF の機能）、PW_2（index 1、**pipe B/C/D と外部の port・AUX C-E/USBC/TBT・VGA**）、**PW_A（index 5、pipe A だけ、PW_2 と独立）**、PW_B/C/D（index 6..8、pipe ごと）、DDI IO A/B/TC1-4、AUX A/B/USBC1-4/TBT1-4、DC_off。5330 の dump: PWR_WELL_CTL2 0x00000c03 = PW_1 と PW_A が on、PW_2 は off | `regs-selected.txt:68`、`dbg-i915_power_domain_info.txt`、Linux `intel_display_power_map.c` |
| GOP | UEFI の GOP は eDP を pipe A で灯して起動する。framebuffer は stolen memory、GGTT の低い番地 | `takeover.c` の N0 の log、WS237 §2.2 |
| VT-d | 起動時に GFXVTBAR を読み、translation が有効なら display を使わない（今の N0 の規則） | `takeover.c` |

## 2. Gen12（ADL-P）の display の model（Linux v6.8.12 の ADL-P の分岐から、PRM を補助に）

名前は PRM の register の名前（大文字、`_` 区切り）で書く。各節の冒頭に、手順の出典の Linux の関数（Procedure referenced に書く物）を挙げる。
zedBSD の code の名前は §3。

### 2.1 ブロック図（データの流れと制御の流れ）

```
 memory ──GGTT──▶ plane 1 (pipe A..D) ──▶ pipe (src size, misc, color) ──▶ transcoder (timing, MSA, DDI func, DP transport)
                                                                                 │
                       DBUF (S1..S4) ◀── watermark/DDB ──┘                      ▼
                                                                 DDI (A, B: combo PHY / TC1..4: Dekel PHY + FIA + IOM)
 reference 38.4 MHz ──▶ DPLL0/1 (combo) ──DPCLKA_CFGCR0──▶ DDI clock ──TRANS_CLK_SEL──▶ transcoder clock
                   └──▶ TC PLL 1..4 (Dekel, port ごと) ──DDI_CLK_SEL──┘
                   └──▶ CDCLK PLL ──▶ CDCLK (pipe・DBUF・AUX の timer)
 PCH (south): raw clock 19.2 MHz ──▶ GMBUS (DDC), PP (panel power), SBLC PWM (backlight), SHOTPLUG (DDI/TC の HPD), SDE の割り込み
 north: DE の割り込み (pipe/port/misc/HPD TC・TBT), AUX (A, B, USBC1..4), PSR, DMC (DC5/6/9)
 power: PW_0 (PCI, clocks, 割り込み) → PW_1 (transcoder A, DDI A/B, DBUF) → PW_A (pipe A) / PW_2 (pipe B..D, 外部の port, AUX C-E/USBC/TBT) → PW_B/C/D
```

ADL-P の PLL は DPLL0・DPLL1・TBT PLL・TC PLL 1..4（Linux `intel_dpll_mgr.c` の `adlp_plls`）。DPLL4 は無い（初版の記述を直した）。

### 2.2 display の init と uninit

Procedure referenced: Linux `intel_display_power.c` (icl_display_core_init, icl_display_core_uninit)。PRM TGL Vol 12 "Sequences to Initialize Display" は補助。

init（ADL-P の分岐だけ）: (1) DC state を disable（DC_STATE_EN = 0）。(2) SOUTH_DSPCLK_GATE_D の PCH DPMG unit の clock gate を disable（PCH が TGP 以降 DG1 未満: ADP-P は該当）。
(3) NDE_RSTWRN_OPT の PCH reset handshake。(4) combo PHY の init（§2.7）。(5) PW_1 を上げる（§2.5 の形）。(6) CDCLK の init（§2.6: 読んで妥当なら保ち、
妥当でなければ表の最小の値に設定する）。(7) DBUF: 使う slice の設定（DBUF_CTL の tracker state service、display 12+）→ slice の power を上げる。
(8) MBUS の abox の credit は **ADL-P では書かない**（Linux `icl_mbus_init` は `IS_ALDERLAKE_P` で return）。(9) BW_BUDDY（DRAM の種別と channel 数で page mask、表 W2）。
(10) resume なら DMC の program を載せ直す。(11) GEN11_CHICKEN_DCPR_2 の 4 bit（display 12.0〜13.0 の WA）と XELPD_DISPLAY_ERR_FATAL_MASK = ~0（display 13）。
uninit（DC9 の前）: DC state を disable → DMC の program を disable → DBUF を全部落とす → CDCLK の uninit（bypass へ: PLL off）→ PW_1 を落とす → combo PHY を un-init。
south の raw clock は init の前に RAWCLK_FREQ を書く（§1: SFUSE_STRAP で 19.2/24 を選ぶ。Procedure referenced: `intel_cdclk.c` (cnp_rawclk)）。

### 2.3 DisplayPort（eDP・外部 DP）の enable と disable

Procedure referenced: Linux `intel_ddi.c` (tgl_ddi_pre_enable_dp, intel_ddi_post_disable_dp, intel_ddi_prepare_link_retrain)、
`intel_dp_link_training.c` (intel_dp_start_link_train 以下)、`intel_display.c` (hsw_crtc_enable, hsw_crtc_disable)。PRM TGL Vol 12 "Sequences for DisplayPort" は補助。

enable（SST、MST は使わない）:
1. 使う power well を上げる（§2.5）。Type-C は AUX USBC の well が main link にも要る（§2.8）。
2. eDP は panel power を入れて on を待つ（§2.11）。
3. Type-C（TBT 以外）: DFLEXDPMLE1 に lane 数を書く（pin assignment と panel の lane 数の小さい方）。
4. port の PLL（§2.6）: combo は DPLL を立てて DPCLKA_CFGCR0 で DDI に map し、別の書き込みで clock を on にする。TC は TC PLL を立てて DDI_CLK_SEL と DPCLKA_CFGCR0 の TC clock off を消す。
5. DDI IO power（PWR_WELL_CTL_DDI の request → state）。combo は PHY が init 済みであること。
6. TC: Dekel の DP_MODE（lane 0/1、pin assignment に応じて x1/x2）。
7. 訓練: TRANS_CLK_SEL で port の clock を transcoder に向ける → TRANS_DDI_FUNC_CTL の DDI select・mode select（enable は後）→ DP_TP_CTL を enable、pattern 1 →
   voltage swing（§2.7、§2.8）→ combo: PORT_CL_DW10 で使う lane を power up → DDI_BUF_CTL を enable → DDI idle status が 0 になるまで →
   DP の仕様の訓練（clock recovery → channel EQ。DPCD の 0x102/0x103..、0x202.. の status、AUX_RD_INTERVAL の待ち）→ DP_TP_CTL を normal。
8. plane・pipe・transcoder: plane の設定 → transcoder の timing・M/N・MSA → pipe の色の register（§2.10: PIPE_BOTTOM_COLOR・GAMMA_MODE・CSC_MODE は毎回書く）→
   TRANS_DDI_FUNC_CTL を enable → TRANS_CONF を enable → pipe DMC を enable（§2.12）→ backlight を入れる。
disable: backlight off → plane off → TRANS_CONF off → off の status を待つ（2 frame）→ pipe DMC を disable → TRANS_DDI_FUNC_CTL を DDI select none で off →
DDI_BUF_CTL off → DP_TP_CTL off → 8 µs か idle status → TRANS_CLK_SEL を none → panel power off → DDI IO power off → PLL の map を外し、不要なら PLL を止める → power well を落とす。
訓練の途中で lane 数・rate を変える時は port を disable して PLL からやり直す。失敗したら、そこまで enable した物を disable の順で戻す。

### 2.4 HDMI/DVI の enable と disable

Procedure referenced: Linux `intel_ddi.c` (tgl_ddi_pre_enable_hdmi, intel_ddi_enable_hdmi, intel_ddi_post_disable_hdmi)、`intel_dpll_mgr.c` (icl_calc_wrpll)。

enable: power well → PLL（SSC 無し、HDMI の算法: DCO ≥ 7998 MHz、Pdiv 2/3/5/7、Kdiv 1/2/3、Kdiv ≠ 2 なら Qdiv = 1）→ DDI IO power →
TRANS_CLK_SEL → plane → transcoder の timing → 色の register → TRANS_DDI_FUNC_CTL → TRANS_CONF → **最後に port**（swing → PORT_CL_DW10 の全 lane → DDI_BUF_CTL → idle が消えるまで）。
disable: plane → TRANS_CONF（off を待つ）→ TRANS_DDI_FUNC_CTL none → DDI_BUF_CTL off → 8 µs → TRANS_CLK_SEL none → DDI IO off → PLL off → power well。
5330 の HDMI は今と同じく DVI mode（infoframe・scrambling 無し、24 bpp、TMDS ≤ 340 MHz）で駆動する（§7 #1）。

### 2.5 power well と DC state

Procedure referenced: Linux `intel_display_power_well.c` (hsw_power_well_enable, hsw_power_well_disable, icl_tc_phy_aux_power_well_enable)、
`intel_display_irq.c` (gen8_irq_power_well_post_enable, gen8_irq_power_well_pre_disable)、`intel_display_power.c` (gen9_set_dc_state, gen9_enable_dc5, skl_enable_dc6, bxt_enable_dc9)。
Values referenced: Linux `intel_display_power_map.c` (xelpd_power_wells, xelpd_pwdoms_*)、`i915_reg.h` (ICL_PW_CTL_IDX_*, XELPD_PW_CTL_IDX_*, TGL_PW_CTL_IDX_*)。

- **well の木（ADL-P）**: PW_1（index 0、always on 扱い、§2.2 で上げる）。**PW_A（index 5）は pipe A だけを持ち、PW_2（index 1）と独立**（5330 の dump 0x00000c03 がそれを示す: PW_1 と PW_A が on、PW_2 が off）。
  PW_2 は pipe B/C/D の well（PW_B/C/D、index 6/7/8）の親で、DDI C/D/E・TC1-4 の lane、AUX C/D/E/USBC1-4/TBT1-4、VGA、audio も持つ。DDI IO A/B（index は ICL の DDI A/B）と AUX A/B は PW_1 の下で PW_2 を要らない。
  初版の「PG1..PG5 を順に」（TGL の PRM）は ADL-P に当てない。表 W1（§5.3）は Linux の `xelpd_power_wells` の値（名前、CTL の instance、index、fuse の有無、irq_pipe_mask、domain）で持つ。
- **enable の形**: request → state を待つ（PRM の 30 µs、Linux は 1 ms: 大きい方）→ fuse のある well は FUSE_STATUS の PGn を 20 µs → **irq_pipe_mask のある well（PW_A..D）は、その pipe の DE_PIPE の IMR/IER を立て直す**（register が well の中にあり落ちると消える。
  今の own の `interrupts.c` の `drv_i915_pw_irq_ops`（post_enable/pre_disable、context と pipe mask）がこの hook で、xd-power はこれを呼ぶ: §4.2）。disable は pre_disable の hook（その pipe の割り込みを止めて drain）→ request を消す → state を待つ。
- **DC5/DC6**: DMC の program を載せた後、DC_STATE_DEBUG の bit 1:0（mask cores / mask memory up）を立て、DC_STATE_EN を "up to DC6"。mode set・AUX・GMBUS の間は DC off を保持する（Linux の DC_off well = POWER_DOMAIN の DC_OFF）。
  DC5↔DC6 の切り替えは disable を挟む。DC6 は DMC が載った時だけ許す（今と同じ）。DC3CO は持たない。
- **DC9（suspend）**: DC5/6 を禁止 → 全 mode set を disable（§2.14 の quiesce）→ 割り込みを mask → uninit（§2.2）→ DC9 allow。戻り: DC9 allow を消す → init → DMC → HPD の再 arm（§2.14）。
- **AUX の power**: AUX A/B は PWR_WELL_CTL_AUX の request → state。**AUX USBC1-4（TC の DP-alt・legacy）は、ADL-P では TCCOLD を block する手段そのもの**（§2.8）: request → state を待つ（TBT か legacy の時は timeout を期待し、失敗にしない: Linux の `timeout_expected`）→
  DKL_CMN_UC_DW27 の uc_health を 10 µs 待つ。AUX TBT1-4 は TBT の時だけ。
- 今の code との差: 今は DC_OFF を 17 ms 遅らせて返す。新しい model では「display の作業の間は DC off を保持し、作業が終わったら許す」を 1 つの refcount にする（§3.4）。

### 2.6 clock

Procedure referenced: Linux `intel_cdclk.c` (bxt_cdclk_init_hw, bxt_cdclk_uninit_hw, bxt_sanitize_cdclk, bxt_set_cdclk, bxt_get_cdclk, cnp_rawclk)、
`intel_dpll_mgr.c` (combo_pll_enable, combo_pll_disable, icl_calc_dp_combo_pll, icl_calc_wrpll, icl_calc_dpll_state, dkl_pll_enable, dkl_pll_disable, icl_calc_mg_pll_state, icl_ddi_combo_enable_clock, icl_ddi_tc_enable_clock)。
Values referenced: `intel_cdclk.c` (adlp_cdclk_table)、`intel_dpll_mgr.c` (icl_dp_combo_pll_19_2MHz_values, icl_dp_combo_pll_24MHz_values, tgl_tbt_pll_19_2MHz_values)。PRM TGL Vol 12 "Clocks" は補助。

- 参照: DSSM が示す 19.2/24/38.4 MHz。38.4 MHz は combo DPLL が 2 で割るので 19.2 の表を使う（Linux `icl_dp_combo_pll_*`）。
- combo DPLL（DPLL0/1）: DPLL_ENABLE の power → power state → DPLL_SSC（TGL の WA: CFGCR0 の SSC bit は効かず DPLL_SSC の sscen）→ DPLL_CFGCR0（DCO 整数・小数）→ DPLL_CFGCR1（Pdiv/Kdiv/Qdiv）→ 読み戻し → enable → lock（PRM 50 µs、Linux 1 ms: 大きい方）→ DPCLKA_CFGCR0 で map → 別の書き込みで clock on。
  **ADL-P の 38.4 MHz の WA（Linux `ehl_combo_pll_div_frac_wa_needed`: TGL・ADL-S・ADL-P で参照 38.4 MHz なら DCO の小数を 2 で割って書き、読む時は 2 倍）**: 5330 の CFGCR0 0x00e001a5 の小数の field 0x3800 は表の 0x7000 の半分で、これと一致する（§1）。xd-clock は表の値と WA の両方を持ち、host 試験で 0x00e001a5/0x88 を確かめる。
- TC PLL（Dekel、port ごと）: PORTTC_PLL_ENABLE（ADL-P の offset: Linux `ADLP_PORTTC_PLL_ENABLE`）の power → 10 µs → Dekel の window（§2.8）で rate 非依存の register → rate 依存の register（Linux `icl_calc_mg_pll_state` の定数、T4）→ 読み戻し → enable → lock（900 µs）→ DDI_CLK_SEL → DPCLKA_CFGCR0 の TC clock on。HDMI は算法（div1 ∈ {7,5,3,2}、div2 ∈ {10..1}、DCO 7.992–10 GHz）。
- **CDCLK（レビュー B3・M1 で改訂）**: ADL-P は **crawl（PLL の ratio を再 lock 無しで変える）が使え、squash は無い**（`dmesg-display.txt:17-18`）。表は Linux `adlp_cdclk_table`: 38.4 MHz で 179.2（divider 3、ratio 14）/192（2、10）/307.2（2、16）/556.8（2、29）/652.8（2、34）MHz。
  **方針**: (a) init（§2.2）で読んで妥当（PLL が lock していて、CDCLK_CTL の値が表のどれかと一致し、decimal が合う）なら保つ。妥当でなければ表の最小（179.2 MHz）に設定する（Linux `bxt_cdclk_init_hw` と同じ規則。ADL-P は VBT の既定を読まない）。
  (b) uninit（DC9 の前）で bypass（PLL off）に落とす（Linux `bxt_cdclk_uninit_hw`）。(c) resume の init で (a)。(d) **mode set は CDCLK を変えない**: 必要な pixel rate が今の CDCLK に入らない mode は拒む（5330 の GOP は 1080p の panel と m3 の 2 画面で 192 MHz で足りている）。
  xd-clock は表の全行と set の手順（voltage の mailbox: ADL-P は変更の前に SKL_PCODE_CDCLK_CONTROL の prepare → ready を待ち、後に HSW_PCODE_DE_WRITE_FREQ_REQ で voltage level。crawl の有無で PLL の relock か ratio の書き換えか）を持ち、(a)(b) で使う。mode set から CDCLK を変える事は §7 #2 に残す。
- **pipe count の mailbox は ADL-P では送らない**（Linux `intel_cdclk.c` の `DISPLAY_TO_PCODE_*` は `IS_DG2` だけ、`intel_pmdemand.c` は display 14 以上。初版の「display 13 の手順」は誤り）。今の code の `pmdemand` は xd に持ち込まない。

### 2.7 combo PHY（DDI A/B）

Procedure referenced: Linux `intel_combo_phy.c` (icl_combo_phys_init, icl_combo_phy_verify_state, icl_combo_phys_uninit)、`intel_ddi.c` (icl_ddi_combo_vswing_program, icl_combo_phy_set_signal_levels)、`intel_display_power_well.c`（PORT_CL_DW10 の lane power）。
Values referenced: `intel_combo_phy.c` (icl_procmon_values)、`intel_ddi_buf_trans.c` (adlp_combo_phy_trans_dp_hbr, adlp_combo_phy_trans_dp_hbr2_hbr3, adlp_combo_phy_trans_edp_hbr2, adlp_combo_phy_trans_edp_hbr3, icl_combo_phy_trans_hdmi)。PRM TGL Vol 12 "Combo PHY DDI Buffer" は補助。

init（PHY A が comp source、B は sink。DDI A の PHY は DC6 で保たれる）: PORT_COMP_DW0 の comp init が立っていれば済み → PORT_TX_DW8 の DCC clock select/div、PORT_PCS_DW1 の DCC mode → PHY_MISC の comp power down を消す →
procmon の値（PORT_COMP_DW3 の voltage/process から PORT_COMP_DW1/9/10、表 T7）→ comp source なら PORT_COMP_DW8 の irefgen → PORT_COMP_DW0 の comp init → PORT_CL_DW5 の CL power down enable。
voltage swing（訓練中も同じ手順）: eDP/DP なら PORT_PCS_DW1 の cmnkeeper → PORT_TX_DW4 の loadgen select（lane ごと、rate と lane 数の表）→ PORT_CL_DW5 の SUS clock 11b → PORT_TX_DW5 の training enable を消す →
PORT_TX_DW5 の scaling mode 010b、PORT_TX_DW2 の rcomp scalar 0x98、PORT_TX_DW4 の post cursor 2 = 0、PORT_TX_DW5 の rterm 110b・3 tap disable・cursor program 0・coeff polarity 0、表の swing/pre-emphasis の値（PORT_TX_DW2 の swing select、PORT_TX_DW4 の cursor coeff・post cursor 1、PORT_TX_DW7 の n scalar）→ training enable を立てる。
表 T1 は Linux の ADL-P の表（`adlp_combo_phy_trans_*`）を第一の出典にする（PRM の TGL の表と違う行があれば Linux: 決定 4）。oracle は m3 の `range-0x160000.txt`（combo PHY A: 0x162000–）と `range-0x16C000.txt`（PHY B）の PORT_TX/PCS/CL/COMP の語（`regs-selected.txt` には PORT_TX は無い: §6.3）。

### 2.8 Type-C PHY（Dekel）と FIA・IOM（レビュー B5・M7 で ADL-P の流れに書き直した）

Procedure referenced: Linux `intel_tc.c` (adlp_tc_phy_hpd_live_status, adlp_tc_phy_is_ready, adlp_tc_phy_take_ownership, adlp_tc_phy_is_owned, adlp_tc_phy_connect, adlp_tc_phy_disconnect, adlp_tc_phy_cold_off_domain, tc_phy_verify_legacy_or_dp_alt_mode, intel_tc_port_get_lane_mask, intel_tc_port_get_pin_assignment_mask, intel_tc_port_set_fia_lane_count, intel_tc_port_lock, intel_tc_port_unlock, intel_tc_port_disconnect_phy_work, intel_tc_port_get_link, intel_tc_port_put_link)、
`intel_dkl_phy.c` (dkl_phy_set_hip_idx, intel_dkl_phy_read, intel_dkl_phy_rmw)、`intel_ddi.c` (tgl_dkl_phy_set_signal_levels, adlp_ddi_... の DP_MODE)。
Values referenced: `i915_reg.h` (DDI_BUF_CTL_TC_PHY_OWNERSHIP, TCSS_DDI_STATUS, PORT_TX_DFLEXDPSP, PORT_TX_DFLEXPA1, PORT_TX_DFLEXDPMLE1, GEN11_DE_HPD_ISR の TC/TBT の bit, SDE_TC_HOTPLUG_ICP)、`intel_dkl_phy_regs.h`、`intel_ddi_buf_trans.c` (adlp_dkl_phy_trans_dp_hbr, adlp_dkl_phy_trans_dp_hbr2_hbr3, tgl_dkl_phy_trans_hdmi)。
PRM TGL Vol 12 "TypeC PHY DDI Buffer"・"Type-C Hot Plug Detection"・"Connect Flows" は補助。**TGL の "TCCOLD" の章（GT driver mailbox 0x80000026 で block/unblock）は TGL 専用で、ADL-P には当てない。**

- 構成: PHY は 2 lane、各 lane に TX1 と TX2。DP の x1/x2 は DP_MODE で lane ごとに選ぶ。Dekel の register は port ごとの window（HIP_INDEX_REG で bank を選び、1 つの lock の下で index → access）。FIA は modular（ADL-P: TC1/TC2 が FIA1、TC3/TC4 が FIA2。`tc_phy_load_fia_params`。初版の「FIA1 だけ」は誤り）。
- **live state**（connect の判定、割り込みの ISR から）: DE_HPD_ISR の TC の bit → DP-alt、TBT の bit → TBT、SDEISR の TC の bit → legacy。FIA の DFLEXDPSP の live state は ADL-P では読まない（Linux の adlp の分岐はそうしている）。
- **ready**: TCSS_DDI_STATUS(tc_port) の READY（0xffffffff が読めたら TCCOLD の中、not ready）。**所有**: DDI_BUF_CTL の bit 6（TC_PHY_OWNERSHIP）の rmw（取る/返す）、読んで確かめる。
- **TCCOLD の block（ADL-P）**: DP-alt と legacy は **その port の AUX USBC の power well を上げる事が block**（Linux `adlp_tc_phy_cold_off_domain` = legacy aux domain）。TBT は TC_COLD_OFF の domain（TBT の AUX）。mailbox は使わない。
- connect（DP-alt・legacy）: port の power（DDI IO の domain）を取る → 所有を取る → ready を確かめる → AUX USBC の well を上げる（TCCOLD の block）→ lane の数（DFLEXDPSP の lane assignment の mask）と pin assignment（DFLEXPA1）を読み、要る lane 数が入るか確かめる（legacy は 4 lane 固定）→ 失敗したらその段まで戻す（well を下げ、所有を返す）。
  TBT: AUX TBT の well だけ。
- disconnect: AUX USBC の well を下げる → DP-alt・legacy は所有を返す。**disconnect は link の refcount が 0 になってすぐではなく、unlock の時に 1 s の遅延 work で行う（その間に再び lock されれば取り消し）**（Linux `intel_tc_port_unlock` → `disconnect_phy_work`）。AUX の transaction と mode set の disable の後にこの道を通る。
- lane 数: enable の前に DFLEXDPMLE1 に ML の mask（lane 1/2/4 と lane reversal）を書く。
- hotplug: north の TC/TBT の HOTPLUG_CTL と DE_HPD の割り込み、south の SHOTPLUG_CTL_TC と SDE。reset・power state の後は「HPD を enable → 各 TC port の live state を読んで connect/disconnect を処理」。
- 今の code との一致: 今の `tc.c` も DDI_BUF_CTL の bit 6 と TCSS_DDI_STATUS を使い AUX の well で TCCOLD を block している（5330 で動いている実績）。初版の §7 #4（PRM の DFLEXDPPMS/DPCSSS の handshake か）は決定 4 で閉じた: ADL-P の Linux の流れを採る。

### 2.9 AUX と GMBUS

Procedure referenced: Linux `intel_dp_aux.c` (intel_dp_aux_xfer, intel_dp_aux_wait_done, skl_get_aux_send_ctl)、`intel_gmbus.c` (gmbus_xfer, do_gmbus_xfer, gmbus_wait, gmbus_xfer_read_chunk, gmbus_xfer_write_chunk, intel_gmbus_reset)。
Values referenced: `intel_dp_aux_regs.h`、`intel_gmbus_regs.h`、`intel_gmbus.c` (gmbus_pins_icp)。PRM TGL Vol 12 "AUX programming sequence"、ACM Vol 12 "GMBUS and GPIO" は補助。

- AUX: display が init 済み・AUX の power が入り・PSR が止まり・DC off を保持した状態で、DDI_AUX_DATA_*_[0-4] に message（header + data、big endian の pack）→ DDI_AUX_CTL に **timeout = 最大（TIME_OUT_MAX = 3 → 4000 µs。PRM の最小 600 µs より長い方を使う: レビュー m4 の混在を解いた。600 µs は AUX の timeout の最小値であって well の待ちではない）**・message size・sync pulse 32 / fast 18・START（bit 31）→ bit 31:30 が 01 になるまで待つ（busy の待ちは 10 ms）→ bit 25（receive error）なら 1 を書いて消し data を読まない → DATA を読む → done を消す。
  TC port は DDI_AUX_CTL の IO select（legacy/TBT）。再試行は今の規則（§2.13 の末尾）を保つ。
- GMBUS（PCH）: GMBUS0 の pin pair（**GMBUS の pin: 1 = DDI A、2 = DDI B、9..12 = TC1..4**。VBT の DDC pin とは別の番号で、対応は表 V3: §2.13）と速度（100 kHz）→ GMBUS1 に command（index、byte count、slave address、read、START/STOP の cycle）→ GMBUS2 の HW_RDY/HW_WAIT/SATOER/ACTIVE を待つ（1 回の待ち 50 ms）→ GMBUS3 を 4 byte ずつ → 終わりに GMBUS1 の STOP、GMBUS0 を 0 で解放。
  **NAK（SATOER）**: bus が idle になるのを待ってから SW_CLR_INT で片付け、GMBUS0 を解放する。**最初の message が NAK されたら 1 回だけ再試行**（Linux `gmbus_xfer` の "Passive adapters sometimes NAK the first probe"。今の `gmbus.c` の `retries = 1` も同じ）。それでも NAK なら失敗（ENXIO 相当）として返す。初版の §2.9「NAK は失敗として返す」と §2.13「1 回だけ再試行」はこれで 1 つに揃えた（m5）。
  EDID の segment は bit-bang が要るが 256 byte までは不要。

### 2.10 transcoder・pipe・plane・DBUF・watermark

Procedure referenced: Linux `intel_display.c` (intel_set_transcoder_timings, intel_set_pipe_src_size, intel_set_m_n, hsw_set_transconf, hsw_enable_transcoder, intel_disable_transcoder, intel_ddi_set_dp_msa)、
`intel_color.c` (icl_color_commit_arm)、`skl_universal_plane.c` (skl_plane_update_noarm, skl_plane_update_arm, skl_plane_disable_arm)、
`skl_watermark.c` (skl_compute_plane_wm, skl_compute_wm_levels, skl_compute_transition_wm, skl_allocate_plane_ddb, skl_ddb_entry_for_slices, mbus_ddb_offset, skl_write_plane_wm, intel_mbus_dbox_update, intel_dbuf_pre_plane_update, intel_dbuf_post_plane_update, adlp_check_mbus_joined, skl_setup_wm_latency, skl_wm_init, intel_enable_ipc)。
Values referenced: `skl_watermark.c` (adlp_allowed_dbufs, skl_cursor_allocation の 36 block)、`i915_reg.h` (PLANE_*, PIPE_MISC, PIPE_ARB_CTL, PIPE_BOTTOM_COLOR の名前は PRM の SKL_BOTTOM_COLOR 相当, MBUS_CTL, DBUF_CTL_S, PIPE_MBUS_DBOX_CTL, DISP_ARB_CTL2)。PRM TGL Vol 12 "Transcoder"・"Pipe"・"Planes"・"Display Watermark Programming" は補助。

- transcoder: TRANS_HTOTAL/HBLANK/HSYNC/VTOTAL/VBLANK/VSYNC（13 bit × 2、値 − 1）、TRANS_VSYNCSHIFT、display 13 は TRANS_SET_CONTEXT_LATENCY に（vblank start − vdisplay）を書き VBLANK の start は 0、
  DP の TRANS_DATAM1/N1、TRANS_LINKM1/N1（N の書き込みで arm）、TRANS_MSA_MISC、TRANS_DDI_FUNC_CTL（enable、DDI select、mode select、bpc、sync、port width。display 12+ は transcoder = pipe）、TRANS_DDI_FUNC_CTL2 = 0、TRANS_CONF（enable、state）。
- pipe: PIPE_SRCSZ、PIPE_MISC（bpc、dither）、PIPE_MISC2 は触らない、scaler は使わない（PS_CTRL off のまま）、**PIPE_BOTTOM_COLOR = 0・GAMMA_MODE（10 bit、LUT 無し）・CSC_MODE（0）は mode set のたびに書く**（GOP が残した値に頼らない: レビュー M14。PRM の名前は PIPE_BOTTOM_COLOR、Linux の macro 名 SKL_BOTTOM_COLOR は使わない: m6）、PIPE_FRMCNT と PIPE_SCANLINE（観測）、PIPE_ARB_CTL の prog slots（ADL-P の WA、TGL Vol 2c にも register はある）。
- plane 1: PLANE_CTL（enable、format XRGB8888、linear、rotation 0、ADL-P の ARB slots）、PLANE_STRIDE、PLANE_POS、PLANE_SIZE、PLANE_OFFSET、PLANE_COLOR_CTL、PLANE_WM_0..5、PLANE_WM_TRANS、PLANE_WM_SAGV（display 13）、PLANE_BUF_CFG、**PLANE_SURF（書くと arm、vblank で反映、PLANE_SURFLIVE が今の番地）**。flip は PLANE_SURF だけを書き、FLIP_DONE の割り込み（DE_PIPE の bit 3）か SURFLIVE で確かめる（今と同じ）。
- watermark: WM_LINETIME = roundup(8·htotal/pixel rate MHz)。level ごとに method 1 / method 2 の規則（Linux `skl_compute_plane_wm`、PRM の算法と同じ）→ blocks・lines。latency は GT driver mailbox 0x6（data 0: level 0–3、data 1: level 4–7、display 13 は 6 level。level 0 が 0 なら全 level に 3 µs を足す: WaWmMemoryReadLatency）。transition WM と SAGV WM は Linux の式（T6）。SAGV は mailbox 0xe で止めたまま。
  **IPC**: DISP_ARB_CTL2 の IPC enable を init で立てる（`dmesg-display.txt:26` has_ipc yes。Linux `skl_wm_init` → `intel_enable_ipc`。初版に無かった: m7）。
- **DBUF と MBUS（レビュー M3 で改訂）**: ADL-P の表（Linux `adlp_allowed_dbufs`。初版の「TGL は 2 slice」「ACM の表」は出典の誤り）: pipe A だけなら S1–S4 を join（MBUS_CTL の join と hashing 1x4）か S1+S2、pipe B だけなら S1–S4 の join か S3+S4、A+B なら A が S1+S2・B が S3+S4、C は S3+S4。
  **方針**: 5330 の 1 画面は GOP と Linux と同じく **join（4 slice）** で灯し、head（2 画面目）を灯す時に **resident の DDB を S1+S2 の相対の割り当てに書き換え、MBUS の join を解き、S3/S4 を head に充てる**。この「1 → 2 画面」の遷移は、Linux の `intel_dbuf_pre_plane_update`/`post_plane_update` と同じ順
  （join を解く: 先に新しい slice の power を上げ、MBUS_CTL の join を消し、resident の PLANE_BUF_CFG/WM を vblank に同期して書き、PIPE_MBUS_DBOX_CTL の credit を更新する。2 → 1 画面はその逆）で、xd-dbuf が「並ぶ pipe の集合」を入力に取って行う（§3.3 の plan に pipes の入力）。
  今の code の「resident を 2 pipe 用に灯し直す」（`present.c` の "lit again for two pipes"）と同じ結果を、plane の再 program（DDB・WM・DBOX の書き換え）だけで出す。
  join していない時の PLANE_BUF_CFG の start/end は**その pipe の slice の組の先頭からの相対**（Linux `mbus_ddb_offset`。m3 の pipe B の 0x07d50000 がそれを示す）。
  cursor の 36 block は Linux と同じく予約だけして使わない（cursor plane は disable のまま。§7 の未決から外し、oracle が Linux の dump と一致するようにした: M12）。DBUF_CTL の request → state（10 µs）、使わない slice は落とす。
- 割り込み: DISPLAY_INT_CTL の bit 31 を落として読み、DE_PIPE_IIR（vblank bit 0、FLIP_DONE bit 3、underrun）、DE_PORT_IIR（AUX done）、DE_MISC、DE_HPD（TC/TBT）、SDEIIR（PCH: DDI/TC の HPD、GMBUS）を ack し、bit 31 を戻す（今の own の `interrupts.c` と同じ。構造は保ち、入口の関数名だけ xd に向ける: §4.2）。

### 2.11 panel power・backlight・PSR

Procedure referenced: Linux `intel_pps.c` (intel_pps_init_delays, pps_init_registers, intel_pps_on_unlocked, intel_pps_off_unlocked, intel_pps_vdd_on_unlocked, intel_pps_vdd_off_sync_unlocked, wait_panel_on, wait_panel_off, wait_panel_power_cycle)、
`intel_backlight.c` (pch_setup_backlight, pch_enable_backlight, pch_disable_backlight, pch_set_backlight, pch_get_backlight, cnp_setup_backlight, cnp_enable_backlight, cnp_disable_backlight)。
Values referenced: `intel_pps_regs.h`、`intel_backlight_regs.h`。ACM Vol 12 "Panel Power and Backlight"、TGL Vol 12 "Panel Self Refresh" は補助。

- panel power（PCH の PP_CONTROL/PP_STATUS/PP_ON_DELAYS/PP_OFF_DELAYS。PPS 0 が 0xC7200、index 1 は 0xC7300 で SOUTH_CHICKEN1 の bit が要る。PP_CONTROL の power cycle delay は bit 8:4）: VBT の eDP の T1..T12 と既定。power on: VDD force on → power on → PP_STATUS の on を待つ（T1+T3）→ 訓練 → backlight（T8 の後）。off: backlight off（T9）→ power off → off を待つ → power cycle delay（T12）。VDD の単独 on は AUX の間だけ。
- backlight（PCH の SBLC_PWM_CTL1/FREQ/DUTY = 0xC8250/54/58。**PWM の clock は raw clock 19.2 MHz**、increment 1）: FREQ に周期 = raw clock / VBT の PWM 周波数（5330: 19.2 MHz / 200 Hz = 96000 = 0x17700、`regs-selected.txt:7`）、DUTY に duty、CTL1 で enable（bit 31）と極性（bit 29）。周波数の変更は PWM を止めて書く。
  初版の「38.4 MHz / 400 Hz」は raw clock の誤り（B2）。
- PSR: **使わない**（panel が PSR を持たない上、今も off）。SRD_CTL/PSR2_CTL は読んで、立っていれば disable の手順で止める。

### 2.12 DMC

Procedure referenced: Linux `intel_dmc.c` (intel_dmc_load_program, dmc_load_work_fn, parse_dmc_fw, parse_dmc_fw_header, parse_dmc_fw_package, parse_dmc_fw_css, disable_all_event_handlers, adlp_pipedmc_clock_gating_wa, intel_dmc_enable_pipe, intel_dmc_disable_pipe, intel_dmc_disable_program)。
Values referenced: `intel_dmc_regs.h` (DMC_PROGRAM, DMC_HTP, DMC_EVT_CTL, _ADLP_PIPEDMC_REG_MMIO_BASE_A, PIPEDMC_CONTROL, DMC_MMIO_START_RANGE/END_RANGE)、`i915_reg.h` (CLKGATE_DIS_PSL_EXT, PIPEDMC_GATING_DIS)。PRM TGL Vol 12 "DMC Firmware Package" は補助。

package = CSS header（128 byte、module type 9、version）→ package header（v2: 32 entry、dmc id 0 = main、1..4 = pipe A..D、stepping/substepping の wildcard、offset は dword）→ 各 firmware（DMC header v3: signature 0x40403E3E、fwSize、startMMIOAddr、mmioCount ≤ 20 と addr/data）。
載せ方（レビュー M5 で足した物を含む）: **(1) CLKGATE_DIS_PSL_EXT の PIPEDMC gating disable を pipe A..D に立てる（Wa_16015201720: pipe A/B は常時、C/D は載せる間だけ）** → (2) **DMC_EVT_CTL の全 handler（main と pipe DMC）を disable（event id FALSE、edge 0→1）** → (3) payload を program storage（main は 0x8F000 から、pipe DMC は ADL-P の base 0x5F000 から）へ dword で書く → (4) header の MMIO pair を書く（main は 0x8F000–0x8FFFF、pipe は 0x5F000–0x5FFFF の範囲に限り、範囲外は package を拒む）→ (5) DC_STATE_DEBUG → (6) pipe C/D の gating disable を戻す。
**pipe DMC は TRANS_CONF の enable の後に PIPEDMC_CONTROL(pipe) の enable を立て、disable の前に消す**（§2.3 の順に入れた）。stepping は PCI revision から。firmware が無ければ DC5/6 を許さず、他は動く（今と同じ）。resume は載せ直し（§2.2 の (10)）。

### 2.13 VBT・OpRegion・EDID・DPCD（PRM に無い物）

Procedure referenced: Linux `intel_opregion.c` (intel_opregion_setup の mailbox 4 / RVDA の規則)、`intel_bios.c` (parse_general_features, parse_general_definitions, parse_ddi_ports, parse_edp, parse_lfp_panel_dtd, parse_lfp_backlight, parse_lfp_data, map_ddc_pin, map_aux_ch, dvo_port_to_port)。
Values referenced: `intel_vbt_defs.h`（block id、field の offset と幅、version の境、`ADLP_DDC_BUS_PORT_TC1..4`）、`intel_bios.c` (adlp_ddc_pin_map)、`intel_opregion.c`（header と mailbox の offset）、`include/drm/display/drm_dp.h`（DPCD の address と bit）。

- OpRegion: PCI config 0xFC（ASLS）→ 8 KiB、signature "IntelGraphicsMem"、mailbox 4（VBT 6 KiB）0x400、2.1+ では RVDA/RVDS で mailbox 4 の外にもある。runtime（ACPI の notify、ASLE の backlight 要求、DIDL/CADL）は今も使っていないので持たない（OpRegion は VBT を取り出すためだけ。own の `interrupts.c` の GSE の入口は捨てる）。
- VBT: header（"$VBT"、version）→ BDB（"BIOS_DATA_BLOCK"）。使う block: 1 general features、2 general definitions（child device）、12 driver features、27 eDP、40 LFP options、41/42 LFP data、43 LFP backlight。
  **表 V2（DVO port → DDI）**: A/B/C → DDI A/B/C、D/E → DDI D/E、F/G/H/I → TC1..4（HDMI の DVO も同じ）。**表 V3（VBT の DDC pin → GMBUS の pin。レビュー M4 で分けた）**: VBT の値 1 → GMBUS 1（DDI A）、2 → 2（DDI B）、**3/4/5/6 → GMBUS 9/10/11/12（TC1..4）**（Linux `adlp_ddc_pin_map`、`ADLP_DDC_BUS_PORT_TC1 = 3`）。AUX channel の値（0x40 → A、0x10 → B、0x60..0x90 → USBC1..4）。
  xd-vbt が返す物（今の code が使う物）: port の child（kind、AUX、GMBUS の pin、lane reversal、TC/TBT、HDMI level shift、DP の max rate/lanes）、panel type、LFP の DTD、backlight（PWM Hz、controller、min、極性）、eDP（bpp、PPS の T、rate/lanes、low vswing、HOBL）、SSC と AFC startup。VBT が無い時の既定（port A = eDP、B = DP+HDMI）も保つ。
  **5330 の VBT の生の bytes は `m3-5330-20261007/debugfs/i915_vbt.txt`（8704 byte、"$VBT ALDERLAKE-P"）にある**: p004 の fixture に使う（初版の §7 #12「ユーザーに取得を頼む」は不要になった）。
- EDID: VESA E-EDID 1.4 の base block（checksum、detailed timing × 4、物理の大きさ、bpc）、CEA-861 の拡張（HDMI の VSDB、SVD）。今の own の `output.c` の detailed timing の decode と CVT-RB は保つ。
- DPCD: 0x000.. の receiver caps、0x010.. の eDP 1.4 の supported link rates（今の code は eDP ≥ 1.4 を拒む。xd はこの表を読む。5330 の panel は eDP 1.1 相当で動作は変わらない）、0x100.. の link config、0x200.. の status、0x600 の power、0x700.. の eDP の表示制御、0xF0000 の LTTPR（読むだけ）。
- 今の AUX の規則で保つ物: 1 transaction は 20 byte まで、timeout は 4 ms（TIME_OUT_MAX）、sync pulse 32/fast 18、busy の待ち 10 ms、再試行 5 回（timeout は即、receive error は 400–500 µs 後）、native の再試行 32 回、I2C-over-AUX は 16 byte 単位で DEFER は 7 回まで、eDP の AUX の前に VDD を入れる、TC port は所有中だけ。GMBUS で保つ物: 100 kHz、read は 511 byte まで、待ちは 50 ms、NAK は §2.9 の規則、bit-bang の fallback は無し。EDID で保つ物: block ごとに 4 回まで、checksum、header の 8 byte、segment は block 2 から。

### 2.14 GOP の引き継ぎと suspend の戻り

Procedure referenced: Linux `intel_display_driver.c` (intel_display_driver_probe_nogem の intel_vga_disable、intel_display_driver_suspend、__intel_display_driver_resume)、`intel_vga.c` (intel_vga_disable)、`i915_driver.c` (i915_drm_suspend, i915_drm_resume の順: display の suspend → HPD の work の取り消し → … → DMC の resume → HPD の init → display の resume → poll disable)。

- 引き継ぎ: 起動時に（display への最初の書き込みの前に）PWR_WELL_CTL2・TRANS_CONF・TRANS_DDI_FUNC_CTL・PIPE_SRCSZ・PLANE_CTL/STRIDE/SIZE/SURF・DPLL_ENABLE・DPCLKA_CFGCR0・DDI_BUF_CTL・PP_STATUS/CONTROL・PWM・VGA_CONTROL を**読むだけ**で、GOP が灯した pipe と port を知り、framebuffer の GGTT の範囲が driver の書く範囲と重ならないかを確かめ、VT-d の状態を見て「使う／使わない」を決める（今の N0 の規則をそのまま保つ: §3.6）。
  使うと決めた後、最初の表示の前に: **(1) legacy VGA plane を止める（VGA_CONTROL 0x41000 の bit 31、その前に VGA の sequencer の SR01 screen off: 今の `takeover.c` と Linux `intel_vga_disable` と同じ。レビュー M14）**、(2) GOP の出力を §2.3 の disable の順で止める（plane → transcoder → port → PLL → well）、(3) 自分の mode set（pipe の色の register を含めて全部書く）。Linux のように状態を読み出して atomic の state を再構成する事はしない。GOP の framebuffer の memory には書かない。
- **suspend（レビュー M13 で足した）**: (1) hotplug を止める（HPD の割り込みを mask、worker の pending を捨て、走っている probe を待つ）、(2) 全 screen を暗くする（display window を抜け、head は dark、resident は dark）、(3) TC の link を返し disconnect（遅延の work は待たずに同期で）、(4) DC off を保持 → uninit（§2.2）→ DC9 allow。
- **resume**: DC9 allow を消す → init（§2.2、CDCLK の sanitize、DMC の載せ直し）→ **HPD を再 arm（pin の enable、SHPD_FILTER、HOTPLUG_CTL、IMR/IER）→ 全 connector の live state を読み直して connect/disconnect を処理（resume の間に抜き差しされた物）** → 次の表示で mode set。保存する register は無い（新しい mode set で全部書く）。

## 3. zedBSD の自前の構造

### 3.1 名前と置き場所

- 接頭辞 **`xd`**（PRM の "XeD" = Xe Display から。Linux の `xelpd`/`intel_` は使わない）。file は `src/drivers/gpu/i915/display/xd-<役割>.c/.h`、公開の関数 `drv_i915_xd_<役割>_<動詞>()`、static は `i915_xd_...`（既存の規則）、型 `struct i915_xd_<物>`、定数 `I915_XD_*`。
- register と表のデータは `src/drivers/gpu/i915/intel/display-regs-xd.h`（north）、`intel/display-regs-xd-south.h`（PCH）、`intel/display-tables-xd.h`（PLL・CDCLK・buf trans・DBUF・procmon・latency・well の表）、`intel/xd-vbt-layout.h`、`intel/xd-opregion-layout.h`、`intel/xd-dpcd.h`（使う address だけ）。既存の `intel/*.h`（`mreg.h`・`trans.h`・`dp.h`・`plane.h`・`vbt-defs.h` …）は新しい code からは include しない（WS244 が Gen9 の利用者ごと整理・削除する）。
- 1 つの集約の header **`display/xd.h`** が、own の file（present・head・output・control・backlight・display・scanout・bootfb・interrupts・device）が使う型と関数を全部持つ（design policy §2.6: 公開 header は少なく、意図して包括的に。実装は口の下で丸ごと置き換えられる）。他の `xd-*.h` は xd の中だけで include する。
- **own の型の分離（p002）**: 今の `internal.h` には Gen9 と own の file が使う own の型（`i915_display`・`i915_scanout`・`i915_resident_display`・`i915_display_irq`・`i915_irq_vblank`・`i915_pch_state`・`i915_pw_irq_ops`・`i915_present_window`、mode の型）と Linux 由来の型が同居している（2026-10-15 の照合: `gen9-node.c` は `i915_lcd_mode`・`i915_resident_display`・`i915_scanout`、`gen9-backlight.c` は `i915_pch_state`、`gen9-flip.c` は `i915_scanout` を使う）。
  own の型は `display/display-internal.h`（own、Gen9 と xd の両方が include）に移し、mode の型は `struct i915_display_mode`（own。§4.3 の `i915_xd_mode` はこれの別名ではなく、この own の型を xd がそのまま使う）にする。Linux 由来の型は `internal.h` に残して p012 で file ごと消す。

### 3.2 module と責務（レビュー M17: 境界は hardware の単位で決め、Linux の file の分け方は持ち込まない）

| module | hardware の単位（境界の根拠） | 責務 | 持つ状態 | 呼ぶ物 |
| --- | --- | --- | --- | --- |
| `xd-io` | MMIO と GT driver mailbox（register の窓口。Linux は `intel_de.h`・`intel_uncore`・`intel_pcode.c` の 3 つだが、zedBSD では 1 つの「書き込みの記録を持つ窓口」） | read/write/rmw、posting read、`poll(reg, mask, value, timeout_us)`、µs/ms の sleep、GT driver mailbox の 1 command（`sb_lock` の下）、**書き込みの記録**（直近 N 個の (reg, value) の ring と名前付きの step: 試験と log 用）、試験の hook（read/write の差し替え） | `struct i915_xd_io { mmio; ring; trace on/off; hooks }` | `mmio.c`、`kern_udelay` |
| `xd-vbt` | firmware の表（OpRegion は VBT の入れ物としてだけ使うので 1 つ。Linux の `intel_opregion.c`/`intel_bios.c` の 2 file をまとめた） | OpRegion の map と検証、VBT の探索（mailbox 4 / RVDA、PCI ROM）、block の parse → `struct i915_xd_vbt`（port の表、panel、PPS の T、backlight、eDP、AFC）、SHA-256（N0 の報告用） | parse の結果（値のみ） | `drv_pci`、`vm` の map |
| `xd-edid` | sink の記述（EDID/CEA。DP と HDMI の両方が使う純粋な decode） | base/CEA の decode → `struct i915_xd_edid`。detailed timing は own の `output.c` の decode を呼ぶ | 無し | `output.c` |
| `xd-aux` | north の AUX channel（DDI_AUX_CTL/DATA の hardware） | 1 transaction、native read/write、I2C-over-AUX、再試行の規則 | `struct i915_xd_aux { port, base, io select, stats }` × channel | `xd-io`、`xd-power`（AUX の well、DC off） |
| `xd-gmbus` | PCH の GMBUS controller（DDC） | 1 transfer（pin、address、index、長さ）、EDID の読み（128/256 byte）、NAK の片付け、reset | `struct i915_xd_gmbus { lock, pin }` | `xd-io` |
| `xd-power` | display の電源の木（PWR_WELL_CTL*・FUSE_STATUS・DC_STATE_EN）と display 全体の init/uninit（PRM の "Sequences to Initialize Display" は well・CDCLK・DBUF・PHY をまたぐ 1 つの手順なので、ここが司令で各 module を呼ぶ） | well の表 W1 と refcount、enable/disable（fuse、IRQ の hook）、DDI IO / AUX の well（TC は uc_health）、DC state の refcount（DC6 は DMC 次第）、init/uninit、DC9 の enter/leave、「pipe の register が読めるか」の照会 | `struct i915_xd_power { well[]: refs; dc_off_refs; init_done }` | `xd-io`、`xd-ddi`（combo PHY の init）、`xd-clock`（CDCLK の init/uninit）、`xd-dbuf`（slice の init）、`xd-dmc`、`interrupts.c` の hook |
| `xd-clock` | clock の木（参照・CDCLK・DPLL・TC PLL・port → transcoder の clock select。PRM の "Clocks" の 1 章。Linux は `intel_cdclk.c` と `intel_dpll_mgr.c` の 2 file） | 参照 clock（DSSM）、CDCLK の読み・sanitize・set（表 T5、voltage の mailbox、crawl）、最大 pixel rate、combo DPLL の値（表 T3 + HDMI の算法 + 38.4 MHz の WA）と enable/disable、DDI への map、TC PLL の値（T4 + 算法）と enable/disable、TRANS_CLK_SEL、pixel/link clock の計算 | `struct i915_xd_clock { ref_khz; cdclk_khz; pll[]: owner, rate }` | `xd-io`、`xd-tc`（Dekel の window） |
| `xd-ddi` | DDI の buffer と combo PHY（DDI_BUF_CTL・DP_TP_CTL と PORT_* の combo PHY は 1 つの port の送信側。HDMI/DVI の DDI mode と TMDS の上限もここ。Linux は `intel_ddi.c`・`intel_combo_phy.c`・`intel_ddi_buf_trans.c`・`intel_hdmi.c` に散る） | combo PHY の init/uninit、lane の power（PORT_CL_DW10）、swing の programming（表 T1、TC は `xd-tc` の表 T2 を呼ぶ）、DDI_BUF_CTL の語と enable/disable と idle の待ち、DP_TP_CTL の pattern、HDMI/DVI の mode の判定（TMDS ≤ 340 MHz、EDID の HDMI の有無） | 無し（port ごとの引数） | `xd-io`、`xd-tc` |
| `xd-tc` | Type-C の subsystem（Dekel PHY + FIA + IOM + TCSS。所有・TCCOLD・lane・HPD の live が 1 つの hardware の約束。Linux は `intel_tc.c` と `intel_dkl_phy.c` の 2 file） | Dekel の window（HIP_INDEX、lock）を通す読み書き、DP_MODE、PMD lane sus、swing の表 T2、TC PLL の register の列（`xd-clock` が呼ぶ）、状態機械（§3.5）: live state、mode（DP-alt/TBT/legacy）、ready、所有、AUX USBC の well による TCCOLD の block、pin assignment → lane、DFLEXDPMLE1、遅延の disconnect、UCSI への報告（今の `drv_typec_display_report` の口を保つ） | `struct i915_xd_tc_port { mode, owned, lanes, links, live, disconnect timer }` × 4、window の lock | `xd-io`、`xd-power`（AUX USBC/TBT・DDI IO の well） |
| `xd-panel` | PCH の panel power sequencer と backlight PWM（ACM Vol 12 "Panel Power and Backlight" の 1 章、register も 0xC7200/0xC8250 で隣。Linux は `intel_pps.c` と `intel_backlight.c`） | VBT と既定から T 値、VDD の on/off、panel の on/off と待ち、power cycle の待ち、PWM の周波数と duty、enable/disable、level ↔ duty、VBT の min | `struct i915_xd_panel { t[], vdd_on, on, last_off_tick, period, min, level, bl_on }` | `xd-io`、`xd-vbt` |
| `xd-dmc` | DMC firmware engine（program storage・event handler・pipe DMC） | package の parse と選択、WA、program の書き込み、MMIO pair、載せ直し、pipe DMC の enable/disable、「DC6 を許せるか」 | `struct i915_xd_dmc { loaded mask, payloads, version }` | `xd-io`、`firmware.c` |
| `xd-dp` | DisplayPort の link の protocol（DPCD と訓練。hardware ではなく規格の単位: source 側の register は `xd-ddi`、sink は AUX 越し） | rate/lane の候補（DPCD × VBT × source）、bpp、M/N/TU、sink の power、DPCD の helper、訓練（CR/EQ、swing の要求 → `xd-ddi`）、link status の確認、fallback の次の候補 | `struct i915_xd_link { rate_khz, lanes, bpp, m/n, trained, train_set[4] }` | `xd-aux`、`xd-ddi`、`xd-io` |
| `xd-pipe` | pipe（display 12+ は transcoder = pipe で、timing・色・plane 1 が 1 つの pipe の register 群。Linux は `intel_display.c`・`intel_color.c`・`skl_universal_plane.c` に散る） | timing、MSA、DATA/LINK M/N、TRANS_DDI_FUNC_CTL、TRANS_CONF の enable/disable と待ち、PIPE_SRCSZ、PIPE_MISC、scaler off、PIPE_BOTTOM_COLOR・GAMMA_MODE・CSC_MODE、frame counter、vblank の待ち、plane 1 の programming、enable/disable、flip（PLANE_SURF）、FLIP_DONE/SURFLIVE、format と stride の検査 | 無し | `xd-io`、`xd-dbuf` |
| `xd-dbuf` | DBUF と MBUS と watermark（memory 側の帯域の単位: DBUF_CTL・MBUS_CTL・PIPE_MBUS_DBOX_CTL・PLANE_WM/BUF_CFG・latency の mailbox・SAGV・IPC） | latency（mailbox）と調整、slice の選択と power、MBUS の join/unjoin と DBOX の credit、DDB の割り当て（相対の offset）、WM の level・transition・SAGV・LINETIME、SAGV off、IPC、QGV の帯域の判定、1 ↔ 2 画面の遷移の順 | `struct i915_xd_dbuf { latency[8], levels, slices_on, joined, pipes }` | `xd-io` |
| `xd-hpd` | hotplug の hardware（north の HOTPLUG_CTL/DE_HPD、south の SHOTPLUG_CTL/SDE）と connector の worker | pin の表 H1、HPD の enable/re-arm、割り込みの ack と queue（IRQ context）、worker（live state → TC の flow → AUX/EDID の probe）、connector の記録と generation、retry、event の通知 | `struct i915_xd_hpd { connector[]: kind, port, state, generation, edid, mode; pending bits; work }` | `xd-io`、`xd-tc`、`xd-aux`、`xd-gmbus`、`xd-edid`、`present.c`（通知）|
| `xd-screen` | zedBSD の「1 つの出力」（own の概念: mode set の合成。Linux の atomic に相当する物は持たない） | plan の計算（§3.3）、enable（§2.3/2.4 の順）、disable、flip、status、preflight、retained、resident の run と head の run、並ぶ pipe の集合を `xd-dbuf` に渡す | `struct i915_xd_screen { kind, port, pipe, pll, plan, phase, held wells, buffers }` × 2 | 上の全部 |
| `xd-takeover` | 起動時の GOP の引き継ぎ（own の概念） | N0 の読み出しと判断、GOP の出力の分類、VGA plane の停止、takeover（GOP の出力を disable の順で止める）、survey の log | `struct i915_xd_firmware { pipes[], pll, ddi, pp, pwm, fb range, decision }` | `xd-io`、`xd-power`、`xd-screen`（disable） |
| `xd-display` | driver の段（own の概念: start/stop/suspend/resume） | §3.6 の段。`display.c` の公開の関数はこれを呼ぶ | `struct i915_xd { io, vbt, power, clock, tc, panel, dmc, dbuf, hpd, screen[2], firmware, stage flags }` | 上の全部 |
| `xd-diag` | 診断（own） | register の survey、名前付き register の比較表（Linux の dump との差）、run の log、`i915.debug=display`、最後の手段の停止 | 無し | `xd-io` |

初版の 25 module から 19 に減らした: `xd-opregion` → `xd-vbt`、`xd-pcode` → `xd-io`、`xd-phy` + `xd-hdmi` → `xd-ddi`、`xd-dkl` → `xd-tc`、`xd-pps` + `xd-backlight` → `xd-panel`、`xd-trans` + `xd-plane` → `xd-pipe`、`xd-wm` → `xd-dbuf`（名前を hardware に合わせた）。
Linux の file と 1 対 1 に残る物（`xd-aux` ↔ `intel_dp_aux.c`、`xd-gmbus` ↔ `intel_gmbus.c`、`xd-dmc` ↔ `intel_dmc.c`、`xd-edid` ↔ drm_edid）は、hardware（AUX channel、GMBUS controller、DMC engine）や規格（EDID）がそれ自体で 1 単位なので残した。
内部の関数の分け方・名前・制御の流れは Linux と一致させない（監査は `plan/ws244/tests/inventory.py` の C/S と、関数名の対応の目視: p012）。

今の 60k 行（Linux 環境を含む）に対し、新しい xd は **1.2〜1.6 万行**の見込み（§6.5）。減る理由: MST・DSC・audio・VRR・FBC・色管理・cursor・scaler・PSR・DC3CO・HDCP・OpRegion の runtime・storm 管理・atomic の global state・"world" の環境・Linux の互換 layer（drm_dev、drmm、vblank worker、notifier）を持たない。

### 3.3 制御の流れ（screen の mode set）

```
plan   : mode (EDID/display.mode/CVT) + sink (DPCD/EDID) + platform (ref clk, cdclk, dbuf) + vbt + pipes（並ぶ pipe の集合）
         → struct i915_xd_plan { mode, kind, port, pipe, pll_id, link{rate,lanes,bpp,m/n/tu}, pll_words, dbuf{slices, joined, ddb, wm[], trans, sagv, linetime}, pps, fb }
         純関数。host 試験で register の語（§6.3 の oracle）を比べる。
enable : power(wells) → panel on → tc lanes → pll → ddi io → tc dp_mode → [DP: clk_sel, ddi_func(no enable), dp_tp, swing, lanes, buf_ctl, train]
         → dbuf/wm → plane → trans timing/msa/mn → color → ddi_func enable → trans_conf → pipe dmc → [HDMI: swing, lanes, buf_ctl] → backlight
         各 step は `int` を返し、失敗したら「その step まで」を disable の順で戻す。step ごとに xd-io の trace に名前を残す。
disable: 逆順（§2.3/2.4）。TRANS_CONF の off の待ちと DDI の idle を確かめ、確かめられなかった物は「retained」（well と buffer を保持、以後その screen を灯さない: 今の挙動）。
flip   : PLANE_SURF を書く → FLIP_DONE の completion を待つ（timeout 100 ms）→ SURFLIVE で確認。
2 画面 : head の enable の前に xd-dbuf が「A+B」の割り当てを計算し、resident の DDB/WM/DBOX を vblank に同期して書き換え（join を解く）、head の slice を上げる（§2.10）。head の disable の後に逆。
```

phase（screen の状態）: `OFF → PLANNED → POWERED → LINKED → LIT → (RETAINED)`。enable の step の失敗は phase を戻して disable を走らせる。
screen は 2 つ（resident = GOP の出力、head = 2 つ目）。**pipe の割り当て**: resident は GOP が灯していた pipe（eDP なら A、GOP が HDMI/DP を灯していた時は B）、**head は resident と違う pipe（resident が A なら B、resident が B なら A）**。今の `head-rules.c` の `drv_i915_head_pipes_conflict` の規則（同じ pipe は拒む）を plan の段に入れ、衝突が起きない割り当てにした（m8）。

### 3.4 資源の所有権と寿命、並行性と lock

- **所有**: `struct i915_xd` は `struct i915_display` の中（device の寿命）。well・PLL・DBUF slice・TC の所有・AUX の well は xd-power/xd-clock/xd-dbuf/xd-tc の refcount で持ち、screen が enable で取り disable で返す。device の stop は screen を止めてから well を返し、返せなかった物は log して保持（今の「abandoned」）。
- **誰が触るか**: display の register を書くのは **request worker（display window）だけ**（今と同じ: present・head・backlight・control の要求は worker に queue される）。例外は (a) 起動の段（start worker、window の前）、(b) hotplug worker（AUX/GMBUS の probe と TC の connect/disconnect。screen の register は触らない）、(c) 割り込み（ack と completion だけ）。
- **lock（レビュー M6 で規則を足した）**:
  - `xd.lock`（mutex）: hotplug worker と request worker の間で xd-power/xd-tc/xd-aux の refcount と状態を守る。
  - **DDI_BUF_CTL の 2 つの書き手**: bit 6（TC の所有）は xd-tc が `xd.lock` の下で rmw する。bit 31（enable）と lane/rate の field は request worker の screen の enable/disable が書く。両者が同じ register を rmw するので、規則は「**TC port の DDI_BUF_CTL は、その port の link の refcount が 0 の間だけ xd-tc が触り、refcount > 0（screen が POWERED 以降）の間は screen だけが触る**」。screen は enable の最初で `xd_tc_get_link`（`xd.lock` の下で connect と refcount++）、disable の最後で `xd_tc_put_link` を呼ぶ。hotplug worker は refcount > 0 の port の所有を変えない（unplug は live state の変化として screen に「partner gone」を通知し、screen の disable が put_link する）。
  - **AUX の共有**: 1 channel に 1 つの mutex（`aux[ch].lock`）。訓練（request worker）と probe（hotplug worker）が同じ channel を使う時は transaction ごとに取る。hotplug worker は LINKED/LIT の screen の port を再 probe しない（HPD short は link status の読みだけ、`aux[ch].lock` の下）。
  - IRQ context は `device IRQ lock`（既存）の下で pending bit と completion だけ。xd-tc の Dekel の window は spinlock（割り込みからは使わない）。`sb_lock`（mailbox）は既存の mutex。
  - lock の順: device IRQ lock は最内。`xd.lock` → `aux[ch].lock` → `sb_lock` → Dekel の window。
- **割り込みと DMA**: display の DMA は plane の scan out（GGTT）と DMC の program（MMIO）だけ。vblank/FLIP_DONE/AUX done/HPD を使う。AUX は割り込み無しの poll でもよい（今は poll。保つ）。
- **失敗と回復**: 各 step の timeout は表 T8。timeout → その screen を disable、再試行は呼ぶ側（present の relight、DP の fallback）。訓練の失敗 → rate/lane を下げて再試行（最低 RBR×1 まで）。TC の partner が消えた → unplugged として window を抜ける。DMC が無い → DC6 無しで続ける。VBT が無い → 既定（eDP on A、PPS の既定、backlight 200 Hz）。

### 3.5 状態遷移（TC port と hotplug の connector）

```
TC port: FREE ──(live: DP-alt/legacy/TBT)──▶ DETECTED ──(connect ok: owned, AUX well up, lanes)──▶ CONNECTED ──(get_link, links>0)──▶ IN_USE
            ▲                                     │ abort（戻す）                                 │ put_link → links 0        │ partner gone → screen が put_link
            │                                     │                                               ▼                           │
            └──(disconnect: 1 s の遅延 work、再び lock されたら取り消し)─── CONNECTED (links 0) ◀──────────────────────────────┘
connector: ABSENT ──(HPD long / live=1 / boot readout)──▶ PROBING ──(DPCD/EDID ok)──▶ CONNECTED ──(HPD long, live=0)──▶ ABSENT
                                                          │ fail → retry (≤2 回、1 s) → ABSENT
           CONNECTED ──(HPD long, live=1)──▶ PROBING（EDID を読み直し、変わっていれば generation を増やして通知: m9）
           CONNECTED ──(HPD short)──▶ CONNECTED (link status を確かめ、落ちていれば retrain の要求)
```
IN_USE → FREE は無条件ではない（M7）: links が 0 になった後に遅延の disconnect を通る。generation は CONNECTED になる度と EDID が変わった時に増える。storm の検出と poll への切り替えは**持たない**（§7 #4）。

### 3.6 start/stop/suspend/resume の段（xd-display、`display.c` の公開の関数との対応）

| `display.c` の口（保つ） | 今の中身（Linux の probe の段） | 新しい中身（`xd.h` の段） |
| --- | --- | --- |
| `drv_i915_display_create` | worlds の確保 | `struct i915_xd` を zero にし、io・lock を用意、boot の語で実装（old/xd）を選ぶ（§6.1） |
| `drv_i915_display_init_opregion` | OpRegion/VBT/DRAM/帯域 | `xd_vbt_locate_parse` → DRAM の種別（mailbox）を `xd_dbuf` に |
| `drv_i915_display_init_noirq` | drm_dev、bios、VGA、power map、N0、core init、DMC、mode config… | `xd_takeover_readout`（N0、読むだけ）→ 判断 → `xd_power_init`（§2.2、CDCLK・DBUF・IPC を含む）→ `xd_dmc_load`（非同期のまま） |
| `drv_i915_display_irq_prepare` | PCH の判定、割り込みの bind | 同じ（`interrupts.c` は own。入口を `xd_hpd_irq_*` に、power の hook は `xd_power` が受ける） |
| `drv_i915_display_init_nogem` | survey、nogem front、setup outputs、readout、sanitize | `xd_diag_survey` → `xd_dbuf_read_latency` → port の表（VBT）→ `xd_takeover_plan`（GOP の出力の分類だけ。register は書かない） |
| `drv_i915_display_register` | HPD init、IPC、TC/DP-ext/HPD start、driver register | `xd_hpd_start`（pin の enable、boot の readout）、`xd_tc_start`、DC off を返す（takeover が残っていれば保持） |
| `drv_i915_display_resident_deps` | 同左 | screen[0] に resident の output を設定 |
| `drv_i915_display_stop_early` | 最後の手段の停止、HPD stop、TC stop、unregister | `xd_screen_force_off`、`xd_hpd_stop`、`xd_tc_stop`、DC off を取る |
| `suspend_begin/end`、`resume_begin/end` | INIT ref、core uninit、DC9 | `xd_hpd_quiesce` → `xd_screen_dark_all` → `xd_tc_release_all` → `xd_power_hold_dc_off` → `xd_power_uninit` → `xd_power_dc9_enter`；`dc9_leave` → `xd_power_init` → `xd_dmc_reload` → `xd_hpd_rearm`（live state の読み直し）→ 返す |
| `drv_i915_display_stop_outputs` / `fini` | eDP fini、nogem fini、state fini、DMC fini… | `xd_screen_release`、`xd_dmc_free`、`xd_power_release`（保持した well の log）、map の解除 |

## 4. 外向きの口と境界

### 4.1 保つ口（変えない）

- GPU node の operation（`drv_gpu_ops`、`uapi/gpu-display.h`）: display の query/claim/present/wait/release/refresh/power/events。`display.c` の `drv_i915_display_bind_ops` 以下。
- `display.h` の start/stop/suspend/resume の関数（§3.6 の左の列）と `drv_i915_display_panel`・`_which`・`_events`・`_resident_identity`・`_output_*`。
- `present.h`・`head.h`・`control.h`・`backlight.h`・`scanout.h`・`bootfb.h`（own の file の口）。
- Gen9 の display（`gen9-*.c`、`intel/display-regs-gen9*.h`）の code は変えない。ただし Gen9 は Gen12 の物を次のとおり使っているので（2026-10-15 の照合。初版の「一切触らない」は誤り: M8）、p002 で own に移す:
  (a) `internal.h` の own の型（§3.1 の分離で `display-internal.h` へ）、(b) `gen9-outputs.c` の `drv_i915_adjust_wm_latency()`（`takeover.c`）、`bootfb.c` の `drv_i915_n1_console_fb()`（`takeover.c`）、`device.c` の `drv_i915_n0_lite_gen9()`（`takeover.c`）→ **Gen9 側（`gen9-outputs.c`・`bootfb.c`）と `display.c` に自前の実装として移す**（値は同じ、code は書き直す）、
  (c) `gen9-node.c` の `drv_i915_delayed_queue/_cancel_sync/_work_init`（`hotplug-internal.h` の遅延 work の helper）→ own の `display-internal.h` の小さな helper に移す。
- boot の語: `i915.debug=display`、`display.mode=WxH[@R]`。log の接頭辞 `i915: `。

### 4.2 動く口（own の file の呼び出し先が `xd.h` に変わる所。p003 で全部付け替える: 決定 1）

own の file が今呼ぶ Gen12 の関数を、header の宣言（655 個）と own の file の識別子を突き合わせて数えた（2026-10-15）: `present.c` 7、`head.c` 17、`output.c` 15、`control.c` 1、`backlight.c` 3、`bootfb.c` 1、`interrupts.c` 4、**`display.c` 78**、`device.c` 2、`gen9-outputs.c` 1、`gen9-node.c` 3。
`internal.h` の Linux 由来の型（`i915_lcd_state`・`i915_lcd_modeset_cfg`・`i915_lcd_kernel`・`i915_lcd_kernel_deps`・`i915_native_report`・`i915_hpd_output`・`i915_display_output.state`・`i915_display_head.k/params/cfg`・`i915_lcd_flip_result`・`i915_lcd_aux_emit`・`i915_gop_output`・`i915_hpd_summary`・`i915_native_pipe`）も own の file から消える。
own の file の構造は変えず、**呼び出し側の行だけを `xd.h` の口に直す**。

| own の file | 今呼ぶ物（数） | 新しい口（`xd.h`） |
| --- | --- | --- |
| `present.c` | `drv_i915_lcd_kernel_resident_run`、`_modeset_flip_nowait/poll/wait_screen`、`_modeset_retained`、`_show_retained`、`drv_i915_hpd_topology_touch`（7） | `drv_i915_xd_screen_run(display, screen, serve, ctx)`、`_screen_flip_arm/poll/wait`、`_screen_retained`、`drv_i915_xd_hpd_touch` |
| `head.c` | `drv_i915_lcd_modeset_select/prepare/commit_enable/commit_disable/status/plane_disable/plane_released/flip_nowait/poll/settle`、`_lcd_kernel_locks_init/bind_ops/preflight/fill_cfg`、`_lcd_output_params/cfg/link_fallback`（17） | `drv_i915_xd_screen_plan(display, 1, output, pipes, &plan)`、`_screen_light(display, 1, &plan, fb)`、`_screen_dark`、`_screen_flip_*`、`_screen_status`、`_screen_preflight`、`_screen_link_fallback` |
| `output.c` | `drv_i915_lcd_compute_hdmi/dp_ext`、`drv_i915_display_panel_mode/size_mm`、`drv_i915_dp_ext_probe/sink_copy/link_limits/aux_emit`、`drv_i915_tc_kern_port_of`、`drv_i915_hpd_output/_count/_take/_summary/_probe_connector/_edid_bytes`（15）と `drv_i915_gop_output_read/_name` | `drv_i915_xd_plan_hdmi/_dp/_edp`、`drv_i915_xd_panel_mode/size`、`drv_i915_xd_sink_probe(display, connector, &sink)`、`drv_i915_xd_hpd_connector(display, i, &info)`、`_connector_count`、`_connector_take`、`drv_i915_xd_firmware_output(display, &out)`/`_name` |
| `control.c` | `drv_i915_pipe_frame_read`（1） | `drv_i915_xd_pipe_frame(display, pipe)` |
| `backlight.c` | `drv_i915_lcd_modeset_brightness_get/brightness/backlight`（3） | `drv_i915_xd_backlight_get/set/power` |
| `display.c` | §3.6 の段の 78 個（power domains、cdclk、dbuf、bw、dmc、hpd、tc-kern、dp-ext、edp device、nogem、state、capture、dc9、vga、opregion、native、pmdemand、quirks、worlds…） | `drv_i915_xd_*` の段（§3.6、約 20 個）、`drv_i915_xd_step(revid)`、`drv_i915_xd_power_well_on`、`drv_i915_xd_firmware_*`。capture（`capture.c`、own の試験用の record）は `xd-diag` に口を移す。`dc9.c`（own）は `xd-power` の DC9 の段に吸収して消す（p012） |
| `scanout.c`、`bootfb.c` | `drv_i915_n1_console_fb`（takeover） | own に移す（4.1） |
| `interrupts.c` | `drv_i915_hpd_de_irq/pch_irq`、`drv_i915_display_power_is_enabled`、`drv_i915_opregion_gse_entry`（4）と、power.c が呼ぶ `drv_i915_pw_irq_ops`（post_enable/pre_disable の hook） | `drv_i915_xd_hpd_irq_north(display, iir)`、`_irq_south(display, iir)`、`drv_i915_xd_power_pipe_on(display, pipe)`、GSE は捨てる。hook は `xd.h` の `struct i915_xd_irq_hooks`（同じ 2 つの関数、context と pipe mask）で `xd-power` に渡す |
| `diagnostics.c`（Gen12、書き直し） | — | `xd-diag` に |
| `device.c` | `drv_i915_n0_lite_gen9`、`drv_i915_lcd_debug_set`（2） | Gen9 の own、`drv_i915_xd_diag_set` |
| `gen9-outputs.c`、`gen9-node.c` | `drv_i915_adjust_wm_latency`、`drv_i915_delayed_*`（4） | own（4.1） |
| `tests/execution/ktest-display.c`・`ktest-display-probe.c`・`ktest-gt.c` | `internal.h` の Linux の型（`i915_display_core`・`i915_dmc`・`i915_power_domains`…）と `clock.h`・`dmc.h`・`phy.h`・`power.h`・`takeover.h`・`vbt-parse.h`・`watermark.h`・`hotplug.h` | §6.4（M9） |

`struct i915_display` から消える field（**p012 で**。p003〜p011 の間は旧の adapter が使うので残す）: `drm_dev`、`vbt_state`、`vga_client`、`power_domains`、`pmdemand`、`cdclk`、`pwc`、`edp_dev`、`dcore`、`dmc_dev`、`dstate`、`nogem`、`dprobe`、`dram_info`、`bw_state`、`dmc_wq/modeset_wq/flip_wq`、`opd/p2_opd`、`n0/n0_mmio`、`gop`、`dkl`、`tck`、`rctx/rlcd`、`lk`、`lcdb_locks`、`resident_run_env/rep`、`fill_cfg_pn`、`show_*`、`i915_vbt_buf`、`g_dmc_arena`、`dc_trace`、`read_opregion_*`、`chain_*`、`g_vga_io`、`*_world`、`drm_inited` … の段の flag。
残る field: `device`、`irq`/`irq_vblank`（own）、`output`/`gop_output`（型は `struct i915_xd_output` に）、`backlight`、`refresh`、`rd`、`window`、`head`（`k/params/cfg` → `struct i915_xd_plan`）、`resident_buf`、`resident_serve*`、`resident_front/up`、`pch`、`absent`、`hpd_started`、新しい `struct i915_xd xd`。

### 4.3 `xd.h` の型（境界の型、Linux の型を置き換える。p003 で決める）

```c
struct i915_display_mode { uint32_t clock_khz; uint16_t h[4], v[4]; int hsync_pos, vsync_pos; uint16_t width_mm, height_mm; uint8_t bpc; };  /* own、Gen9 と共有（§3.1） */
struct i915_xd_sink { uint8_t kind; /* EDP, DP, HDMI */ uint8_t dpcd_rev, max_lanes; uint32_t max_rate_khz; uint8_t edp_caps[4]; uint8_t edid[256]; unsigned edid_len; struct i915_xd_edid parsed; };
struct i915_xd_plan { struct i915_display_mode mode; uint8_t kind, port, pipe, pll_id, tc_port, pipes; struct { uint32_t rate_khz; uint8_t lanes, bpp; uint32_t data_m, data_n, link_m, link_n, tu; } link;
                      uint32_t pll_words[10]; struct { uint8_t slices; int joined; uint16_t ddb_start, ddb_end, linetime; uint32_t wm[8], trans, sagv, buf_cfg, dbox; } dbuf; uint32_t fb_surf, fb_stride, fb_w, fb_h; };
struct i915_xd_output { uint8_t kind; int none, has_connector; unsigned connector, tc_port; int port; struct i915_xd_plan plan; struct i915_xd_sink sink; const char *mode_source; };
struct i915_xd_connector_info { uint8_t kind; int port, connected; uint64_t generation; const char *name; uint32_t w, h, refresh_mhz, w_mm, h_mm; };
struct i915_xd_firmware_output { uint8_t kind; /* NONE, EDP, HDMI, DP_TC, OTHER */ unsigned pipe, pipes; int port; };
struct i915_xd_irq_hooks { void (*post_enable)(void *ctx, unsigned pipe_mask); int (*pre_disable)(void *ctx, unsigned pipe_mask); void *ctx; };
struct i915_xd_ops { /* §6.1: 口の実装の表。p003 では old の adapter が、p009 から xd が埋める。p012 で表を消し直接の関数にする */ ... };
```
（host 試験が register の語を比べるのは `struct i915_xd_plan` と xd-io の trace。）

### 4.4 変えない振る舞い（受け入れの基準になる物）と、決定 2 の帰結

- 起動: GOP が eDP を灯した 5330 で、display を使う判断（N0 の規則）、GOP の出力の分類、最初の present で takeover して panel が灯る。VT-d が有効なら使わない。
- present/flip の性能: vblank 同期の flip、FLIP_DONE の待ち 100 ms、frame counter の確認（起動時 10 frame/1 s、定常 30 frame/1.5 s）。
- backlight device（`/dev/backlight/backlight0`）の get/set/power、range は PWM の周期（96000）。
- hotplug: HDMI（DDI B）と USB-C DP（TC1/TC2）の plug/unplug で topology の event、head の claim で 2 画面、unplug で window を抜ける、TC の partner gone。
- suspend/resume: DC9 を通って戻り、次の present で灯る。
- log: `i915: N0: …`、`i915: takeover: …`、`i915: gen9 …`（Gen9 は無関係）、試験の一覧が読む文言は p011 で列挙。
- **display 12（M19、2026-10-15 訂正）**: 今の code の display 12（Tiger Lake、Latitude 5320）の分岐は実機で動作確認済み（2026-10-15 ユーザーの補足「Tiger Lakeは実機で動作確認済みです。display 10,11以外は動作確認済みです。」「記録が食い違っているだけです。」）。xd は display 12 と 13 の両方を持ち、display 12 の差は次の改訂で設計する。それまでの下の文（改訂 2 の誤った方針）は取り消し: xd は display 13 だけを持ち、`device-info` が display 12 を返す node は「display なし（bootfb のまま、i915 の display の段を飛ばす）」にする。5320 の i915 の display はこの WS で失われる（ユーザーの決定 2。戻すなら別の WS で表 W1・T5・PLL の enable・PSR の差を足す）。

## 5. データの扱い（register の offset・bit・表）

### 5.1 形

- header は zedBSD の著作権 header と file の説明だけ。各 block（1 register 群、1 表、1 手順）の直前に複数行の comment:
  ```c
  /*
   * The combo PHY DPLL words for the DisplayPort link rates at a 19.2 or
   * 38.4 MHz reference: DCO integer and fraction, and the P, K and Q dividers.
   * At 38.4 MHz the fraction is written halved (the ADL-P workaround).
   *
   * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dpll_mgr.c
   * (icl_dp_combo_pll_19_2MHz_values), cross-checked against IHD-OS-TGL-Vol 12-12.21
   * "DisplayPort Mode PLL Values".
   */
  ```
  Linux の値は Linux を第一の出典、PRM は照合（「cross-checked」）。Linux に無い値は「Values referenced: IHD-OS-TGL-Vol 12-12.21 §<章> (<表の名前>)」だけ。
- 名前は独自: `I915_XD_<REG>` / `I915_XD_<REG>_<FIELD>`（PRM の register の名前を大文字で。Linux の macro 名（`ICL_DPCLKA_CFGCR0`、`SKL_BOTTOM_COLOR`、`_MMIO_PIPE`）は使わない）。instance は `#define I915_XD_TRANS_HTOTAL(trans) (0x60000 + 0x1000 * (trans))` の形（offset の算術はデータ）。
- 表は `static const struct` の配列。配列の要素の comment に行の意味（rate、voltage level）を書く。表の構造（struct の field の並び）は自分で決める（Linux の struct を写さない）。
- VBT・OpRegion・DPCD・EDID の layout は **offset と幅の表**（`#define I915_XD_VBT_CHILD_DVO_PORT 2` …）で持ち、packed struct は作らない。読みは `i915_xd_get8/16/32(bytes, off)`。
- 試験の fixture（5330 の EDID・DPCD・VBT・register の語）は今の `tests/display/dp-fixture-latitude5330.h` と `plan/ws031/display-ref/`、`plan/ws051/tests/m3-5330-20261007/` を使う。

### 5.2 register の出典の対応（grep で確かめた物。第一の出典は Linux、PRM は照合）

| register の群 | 第一の出典（Linux v6.8.12） | 照合（PRM） |
| --- | --- | --- |
| pipe・plane・transcoder・DDI_BUF_CTL・DP_TP_CTL/STATUS・TRANS_DDI_FUNC_CTL/2・TRANS_CLK_SEL・PIPE_SRCSZ・PIPE_MISC・PLANE_*・WM_LINETIME・PIPE_FRMCNT/SCANLINE・PIPE_BOTTOM_COLOR・PIPE_ARB_CTL・PIPE_MBUS_DBOX_CTL・GAMMA_MODE・CSC_MODE・VGA_CONTROL | `i915_reg.h`、`intel_color_regs.h` | TGL Vol 2c（PIPE_MISC は ACM Vol 2c。PIPE_ARB_CTL・PIPE_MBUS_DBOX_CTL・PIPE_BOTTOM_COLOR も TGL Vol 2c にある: 初版の「Linux だけ」を直した） |
| DPLL_ENABLE/CFGCR0/CFGCR1/SSC・DPCLKA_CFGCR0・DDI_CLK_SEL・CDCLK_CTL/PLL_ENABLE・DSSM・PORTTC PLL の enable（ADL-P） | `i915_reg.h`（`ADLP_PORTTC_PLL_ENABLE`、`TGL_DPLL_ENABLE`） | TGL Vol 2c |
| PWR_WELL_CTL/CTL_AUX/CTL_DDI・FUSE_STATUS・DC_STATE_EN/DEBUG・DBUF_CTL・DISPLAY_INT_CTL・DE_PIPE/PORT/MISC/HPD の割り込み・DISP_ARB_CTL2 | `i915_reg.h` | TGL Vol 2c（DISP_ARB_CTL2 は PRM に無い） |
| MBUS_CTL、BW_BUDDY、GEN11_CHICKEN_DCPR_2、XELPD_DISPLAY_ERR_FATAL_MASK、CLKGATE_DIS_PSL_EXT、SOUTH_DSPCLK_GATE_D、TRANS_SET_CONTEXT_LATENCY | `i915_reg.h` | ACM Vol 2c（TRANS_SET_CONTEXT_LATENCY）、他は PRM に無い |
| PORT_COMP/CL/TX/PCS（combo PHY）、PHY_MISC | `intel_combo_phy_regs.h` | TGL Vol 2c |
| DKL_*、HIP_INDEX_REG0/1、PORT_TX_DFLEX*（FIA）、TCSS_DDI_STATUS | `intel_dkl_phy_regs.h`、`i915_reg.h` | TGL Vol 2c / ACM Vol 2c |
| DDI_AUX_CTL/DATA | `intel_dp_aux_regs.h` | TGL Vol 2c |
| SRD_CTL/STATUS、PSR2_CTL | `intel_psr_regs.h` | TGL Vol 2c |
| GMBUS0–5、PP_*、SBLC_PWM_*、SHOTPLUG_CTL_DDI/TC、SHPD_FILTER_CNT、RAWCLK_FREQ、NDE_RSTWRN_OPT、SOUTH_CHICKEN1、SFUSE_STRAP | `intel_gmbus_regs.h`、`intel_pps_regs.h`、`intel_backlight_regs.h`、`i915_reg.h` | ACM Vol 2c（SBLC_PWM_CTL1/FREQ C8250h/C8254h、PP_STATUS/CONTROL C7200h/C7204h、GMBUS0 C5100h、SHOTPLUG_CTL_DDI/TC C4030h/C4034h、RAWCLK_FREQ C6204h は Linux の ADL-P の offset と一致: 2026-10-15 確認） |
| SDEISR/IMR/IIR/IER、TC/TBT の HOTPLUG_CTL | `i915_reg.h` | ACM Vol 2c / TGL Vol 2c |
| DMC の program storage、DMC_EVT_CTL/HTP、PIPEDMC_CONTROL | `intel_dmc_regs.h` | TGL Vol 2c（PIPEDMC_CONTROL はある） |
| GT driver mailbox と command（latency 0x6、QGV 0x10d、SAGV 0xe、CDCLK control、DE write freq） | `intel_gt_regs.h`、`i915_reg.h` | TGL Vol 2c（GTDRIVER_MAILBOX_INTERFACE）、Vol 12 の手順 |

### 5.3 持つ表（名前は独自、出典を付ける）

| 表 | 内容 | 第一の出典（Linux v6.8.12） | 照合 |
| --- | --- | --- | --- |
| T1 combo の swing | DP HBR / HBR2-3 / eDP HBR2・HBR3 の行（swing select、n scalar、cursor coeff、post cursor 1/2）、HDMI の行。ADL-P の表 | `intel_ddi_buf_trans.c` (adlp_combo_phy_trans_dp_hbr, adlp_combo_phy_trans_dp_hbr2_hbr3, adlp_combo_phy_trans_edp_hbr2, adlp_combo_phy_trans_edp_hbr3, icl_combo_phy_trans_hdmi) | TGL Vol 12 "Voltage Swing Programming"（TGL の表）、m3 の PORT_TX の語 |
| T2 Dekel の swing | DP の行、HDMI の行 | `intel_ddi_buf_trans.c` (adlp_dkl_phy_trans_dp_hbr, adlp_dkl_phy_trans_dp_hbr2_hbr3, tgl_dkl_phy_trans_hdmi) | TGL Vol 12 "DP Voltage Swing Table" |
| T3 combo DPLL の DP の値 | 8 rate × 2 参照、38.4 MHz の小数の WA | `intel_dpll_mgr.c` (icl_dp_combo_pll_19_2MHz_values, icl_dp_combo_pll_24MHz_values, ehl_combo_pll_div_frac_wa_needed) | TGL Vol 12 "DisplayPort Mode PLL Values"、5330 の 0x00e001a5/0x88 |
| T4 TC PLL の DP の値 | rate 依存の register | `intel_dpll_mgr.c` (icl_calc_mg_pll_state の定数)、`tgl_tbt_pll_19_2MHz_values` | TGL Vol 12 "TypeC PHY PLL Programming for Displayport"、m3 の TC PLL 2 の語 |
| T5 CDCLK | 38.4（と 19.2/24）MHz の 5 行（cdclk、divider、ratio）、crawl | `intel_cdclk.c` (adlp_cdclk_table) | TGL Vol 12 "CDCLK PLL Ratio and Divider Programming"、m3 の 192000 kHz |
| T6 watermark | latency の調整（+3 µs、6 level）、transition WM の式、SAGV の加算、DBUF の割り当て（4 slice、pipe の組、join）、DBOX の credit、cursor の予約 36 | `skl_watermark.c` (adlp_allowed_dbufs, skl_compute_transition_wm, intel_mbus_dbox_update, skl_cursor_allocation) | TGL Vol 12 "Watermark Algorithm"、5330 の 0x80004010/0x0fdb0000、m3 の 0x07db0000/0x07d50000 |
| T7 procmon | 0.85/0.95/1.05 V × dot-0/1 の COMP_DW1/9/10 | `intel_combo_phy.c` (icl_procmon_values) | TGL Vol 12 "Procmon Reference Values" |
| T8 timeout | PG fuse 20 µs、PW state 1 ms（PRM 30 µs）、DBUF 10 µs、DPLL lock 1 ms（PRM 50 µs）、TC PLL lock 900 µs、DDI idle 1 ms/TC 3 ms、TRANS_CONF off 2 frame、AUX 4000 µs、uc_health 10 µs、AUX USBC の well（TBT/legacy は timeout を許す）、TC の ready 500 ms、GMBUS 50 ms、FLIP_DONE 100 ms | Linux の各関数と今の code の値 | TGL Vol 12 の各手順（短い方） |
| W1 power well の表 | ADL-P の well（名前、CTL の instance、index、fuse の有無、irq_pipe_mask、domain の集合）、AUX channel → domain | `intel_display_power_map.c` (xelpd_power_wells, xelpd_pwdoms_pw_2, xelpd_pwdoms_pw_a..d, xelpd_pwdoms_dc_off, icl_pwdoms_*)、`i915_reg.h` の index | 5330 の PWR_WELL_CTL2 0xc03、`dbg-i915_power_domain_info.txt` |
| W2 BW_BUDDY の page mask | DRAM の種別と channel 数ごと | `intel_display_power.c` (tgl_buddy_page_masks) | TGL Vol 12 "Bandwidth Buddy Registers" |
| H1 HPD の pin | DDI A/B → SHOTPLUG_CTL_DDI の nibble、TC1-4 → SHOTPLUG_CTL_TC と TC/TBT の HOTPLUG_CTL の nibble、SDE の bit、DE_HPD の bit | `i915_reg.h` (SDE_DDI_HOTPLUG_ICP, SDE_TC_HOTPLUG_ICP, GEN11_DE_HPD_*)、`intel_hotplug_irq.c`（MIT。pin の表 `hpd_icp`/`hpd_gen11`） | ACM Vol 12 "Hot Plug Pins"（DG2 の SDE の bit の表は ADL-P と違う: Linux を採る）、m3 の `range-0xC4000.txt`（SDEISR 0xc4000 = 0x00010000: DDI A の HPD が live） |
| V1 VBT の block の layout | block id、field の offset と幅、version の境 | `intel_vbt_defs.h` | 5330 の VBT の bytes（m3）、`vbt-decode.txt` |
| V2 DVO port → DDI | ADL-P の対応 | `intel_bios.c` (dvo_port_to_port) | `vbt-decode.txt` |
| V3 VBT の DDC pin → GMBUS の pin | 1/2/3..6 → 1/2/9..12 | `intel_bios.c` (adlp_ddc_pin_map)、`intel_gmbus.c` (gmbus_pins_icp) | `vbt-decode.txt` の DDC pin と `00-summary.txt` の HDMI の bus |
| O1 OpRegion の layout | header、mailbox の offset、RVDA/RVDS | `intel_opregion.c` | m3 の `debugfs/i915_opregion.txt` |
| D1 DPCD の address と bit | 0x000..、0x100..、0x200..、0x600、0x700..、0xF0000 | `include/drm/display/drm_dp.h` | VESA DP 1.4（会員のみ）、5330 の DPCD |
| E1 EDID の offset | base block、CEA の tag | 今の own の `output.c` | VESA E-EDID 1.4（公開）、5330 の EDID |

### 5.4 引用の規則（決定 3。guardrail「MIT・GPL の出典のデータの引用」の形で、file ごとではなく block ごと）

- **値**（offset・bit・表の 1 行・定数・timeout）: その block の直前の複数行 comment に「Value referenced: Linux v6.8.12 <path> (<define・表の名前>)」（複数なら「Values referenced: …」）。PRM から取った値は「Value(s) referenced: IHD-OS-TGL-Vol 12-12.21 §<章> (<表の名前>)」。両方で確かめた物は Linux を先に、PRM を「cross-checked against …」で続ける。
- **手順**（register の列、待ち、条件分岐の規則）: 手順を実装する関数（または連続する step の block）の直前の複数行 comment に「Procedure referenced: Linux v6.8.12 <path> (<関数の名前>)」。1 つの関数が複数の Linux の関数を参考にしたなら関数名を `,` で並べる。PRM の章も参考にしたなら「and IHD-OS-TGL-Vol 12-12.21 "<章の名前>"」を続ける。
  初版の §5.4「Linux の関数は Procedure referenced に書かない」は guardrail と食い違っていたので捨てた（M16）。
- **書かない物**: file の先頭の出典・派生の宣言・SHA-256（guardrail）。Linux の code・comment の文・関数の分け方・型・制御の流れ（決定 3「参考にする」は手順の内容であって形ではない）。PRM の文の転記。
- **comment の文**: 「何のためにこの register を書くか」を自分の言葉で書く。Linux の comment を訳した文も書かない。
- **形**: 複数行の `/*`、` * `、` */`（1 行の `/* … */` にしない。guardrail 2026-10-15）。
- **監査**（p012）: `grep -L "referenced:" display/xd-*.c intel/*-xd*.h` で出典の無い file が無いこと、`grep -n "Derived from\|derived from the Linux" display/ intel/` の Gen12 の file が 0 件、`plan/ws244/tests/inventory.py` の Gen12 の file の C/S の行が 0、関数名の対応表（xd の関数 ↔ Linux の関数）の目視で「1 対 1 の写し」が無いこと。

## 6. 書き直しの順序、Phase の分割、切り替え、試験、見積もり

### 6.1 切り替えの方法（決定 1: 案 (a)。選ばなかった案も残す）

**選んだ案 (a): 口 `xd.h` を先に決め（p003）、own の file は最初からその口だけを呼ぶ。口の実装は「旧 code への薄い adapter」と「xd」の 2 つで、boot の語で丸ごと切り替える。**

- p003 で `xd.h`（§4.2・§4.3 の口と型）と `struct i915_xd_ops`（口の実装の表）を決め、own の file（present・head・output・control・backlight・interrupts・display・device）の呼び出し側の行を `xd.h` に付け替える。`xd.h` の関数は `display->xd.ops->...` を呼ぶ薄い wrapper。
- **旧の adapter**（`display/old-xd.c`、1 file）: `struct i915_xd_ops` の各関数を、今の Gen12 の関数（`drv_i915_lcd_*`・`drv_i915_hpd_*`・`drv_i915_dp_ext_*`・`drv_i915_tc_kern_*`・§3.6 の段）を呼んで実装する。型の変換（`struct i915_xd_plan` ↔ `i915_lcd_modeset_cfg`/`i915_display_head.k/params/cfg`、`i915_xd_output` ↔ `i915_display_output`、`i915_xd_connector_info` ↔ `i915_hpd_output`）は adapter の中だけ。
  この file は Linux 由来の型を触るので "Derived from the Linux kernel" の表示を保ち、**p012 で消す**（出典の監査の対象外）。adapter は旧 code の state（power の refcount、world）だけを使い、xd の state は作らない。
- **xd の実装**（`display/xd-*.c`）: p004〜p008 は host 試験だけで育て、p009 で `struct i915_xd_ops` を埋める。p009 から boot の語 **`i915.display=xd|old`** で `drv_i915_display_create` がどちらの ops を使うか選ぶ（既定は §7 #0 の判断: この設計は xd を既定にする）。1 つの boot で 1 つの実装だけが動き、**2 つの実装の state は混ざらない**（旧の adapter は `struct i915_display` の旧 field を、xd は `display->xd` を使う）。
- p012 で adapter・旧 code・`i915.display=old`・`struct i915_xd_ops` を消し、`xd.h` の wrapper を直接の関数にする（機械的な rename）。
- この案の cost: wrapper と ops の表（約 40 の口）が p003〜p012 の間だけ増える。adapter は旧 code の構造に引かれるが、使い捨てで監査の対象外。利点: own の file の付け替えが 1 回で済む（p003）、p009 の切り替えが「表を差し替える」だけ、逃げ道 `i915.display=old` が実在する（初版の案は own の file を p008 で付け替えるので `old` では compile も通らなかった: B4）。design policy §2.6「implementation behind the interface replaceable as a whole」に沿う。
- 選ばなかった案 (b)「逃げ道は commit の rollback と割り切る」: 5330 はユーザーの日常の機械で、起動できない版を main に入れない運用との兼ね合いで、1 つの image で戻れる方を採った。
- 選ばなかった案 A「群ごとに旧 file を置き換える（edid → vbt → aux …）」: 旧 code の型が群の境界をまたいで深く結ばれているので、群ごとに adapter を書く事になり、その adapter が Linux の構造を写す。
- 選ばなかった案 C「build 時の切り替え（config.mk の `I915_DISPLAY_OLD=y`）」: ops の表が要らないが、ユーザーが 2 つの image を作り分ける事になる。boot の語の方が確かめやすい。
- 旧 code を残す間の cost: build 時間と `vmunix` の大きさ（display の旧 code は ~1.5 MB の object）。p003 から p012 の間。

### 6.2 順序と Phase

依存は「→」の左が前提。p004・p005・p006 は p002 だけに依存し互いに独立（host 試験だけなので並べられる。初版の「p005〜p007 が独立」は p007 が p005 に依存していて誤り: M11）。

| Phase | 内容 | 受け入れ条件（host で確かめられる物は host、実機の物は 5330） | 依存 | LW |
| --- | --- | --- | --- | ---: |
| p002 | **土台**: `intel/display-regs-xd.h`・`-south.h`・`display-tables-xd.h`（§5 の形で、p004〜p008 が使う register と表を全部）、`xd-io`（read/write/rmw/poll/trace/mailbox/試験の hook）、host の試験の枠（`plan/ws245/tests/run-xd-host-tests.sh`: xd の file + fake の register の model + ASan/UBSan）、`internal.h` の own の型の分離（`display-internal.h`、`struct i915_display_mode`）、Gen9 と `device.c`・`bootfb.c`・`gen9-node.c` の own への移設（§4.1 の (b)(c)）。**xd の file は p002 から `AMD64_I915_SOURCES` に入れる**（kernel の build で warning を早く見る。呼ばれない: m11） | (a) 全 register の offset が Linux の値と一致する表の試験（`tests/display/host-xd-regs-test.c`: 旧 `intel/*.h` の define と比べる。**この試験は旧 header と一緒に p012 で消し、代わりに m3/display-ref の dump の offset と名前の一覧で比べる試験に置き換える**: m10）；(b) xd-io の poll/trace/mailbox の試験；(c) build warning 0、`git diff --check`；(d) 移設した関数の既存の試験（host-gen9-outputs、host-bootfb、host-native-decide）が通る；(e) QEMU: Venus の image の起動（T1 に boot-test.sh を依頼。Gen9 の own の型の分離が壊れていない事） | — | 4 |
| p003 | **口と adapter**: `xd.h`（§4.2・§4.3）、`struct i915_xd_ops`、wrapper、`old-xd.c`（旧 code への adapter）、own の file の付け替え（present・head・output・control・backlight・interrupts・display・device）、`display.c` の段を `xd.h` の段に、`i915.display=` の語（p003 では `old` しか無く、既定も `old`） | (a) own の file が Gen12 の header（`internal.h` の Linux の型、`modeset.h`・`hotplug.h` …）を include しない（grep）；(b) 既存の own の host 試験（host-native-decide、host-bootfb、host-gen9-*）が通る；(c) build warning 0；(d) **5330（ユーザー）**: 今と同じに起動・eDP・backlight・HDMI/USB-C の plug・suspend の戻り（adapter 越しでも振る舞いが変わらない事。試験の一覧に載せる）；(e) QEMU: Venus の image の起動 | p002 | 6 |
| p004 | **VBT・EDID**: `xd-vbt`（OpRegion を含む）、`xd-edid` | 5330 の VBT の bytes（m3 の `debugfs/i915_vbt.txt`）と OpRegion（`debugfs/i915_opregion.txt`）と EDID の fixture で、今の parser と同じ値（port の表、panel の DTD、PPS の T、backlight 200 Hz、eDP の bpp/rate/lanes、DDC → GMBUS の pin、AUX）を返す host 試験（`host-xd-vbt-test.c`）；VBT 無しの既定；壊れた VBT（size・offset・block の overrun）の拒否 | p002 | 3 |
| p005 | **AUX・GMBUS**: `xd-aux`、`xd-gmbus` | `dp-fake-hw.c` の AUX の model（oracle として残す、bind だけ hook）で native read/write・I2C-over-AUX・DEFER/NACK/timeout/receive error の再試行が今と同じ回数と結果；`hpd-model.c` の GMBUS で 128/256 byte の EDID 読みと NAK（最初の message の 1 回の再試行）/timeout | p002 | 2 |
| p006 | **power・DMC・clock**: `xd-power`（well の表 W1、PW_A の独立、IRQ の hook、init/uninit、DC、DC9）、`xd-dmc`（WA・event handler の disable・pipe DMC を含む）、`xd-clock`（CDCLK の読み・sanitize・set・uninit、combo DPLL と WA、TC PLL の語、map） | host: (a) init の手順の register の列が §2.2 の順（fake の model に request/state/fuse の挙動）と、PW_A が PW_2 無しで上がる事、PW_A の post_enable で hook が呼ばれる事；(b) well の refcount の get/put と失敗の巻き戻し；(c) DMC package の parse（`adlp_dmc.bin` の実物で main/pipe A/B）と MMIO pair の範囲検査、載せ方の register の列（gating の WA → handler の disable → payload → pair）；(d) DPLL の語: HBR@38.4 → 0x00e001a5/0x88、m3 の TC PLL 2 の語（`host-dpext-link.c`・`host-dkl-test.c` の期待値を移す）；(e) CDCLK: 読み（m3 の 192000）、妥当な値を保つ、妥当でない値を 179.2 MHz にする列、uninit で bypass | p002 | 6 |
| p007 | **DDI・DP・panel**: `xd-ddi`（combo の init、swing の表 T1、lanes、DDI_BUF_CTL、DP_TP、HDMI/DVI の mode）、`xd-dp`（link の方針、訓練、DPCD の helper）、`xd-panel`（PPS、backlight） | host: (a) `lcd-fake-hw.c` の訓練の model（CR/EQ の挙動、oracle）で eDP HBR×2 の訓練が今と同じ DPCD の列で通り、"CR never"/"EQ never" で fallback；(b) swing の register の語が m3 の `range-0x160000.txt`（PHY A）/`range-0x16C000.txt`（PHY B）の PORT_TX_DW* と一致；(c) PPS の T の計算と on/off の順、VDD；(d) backlight の level ↔ duty、FREQ 0x17700 = 19.2 MHz / 200 Hz | p004、p005、p006 | 5 |
| p008 | **pipe・DBUF**: `xd-pipe`（timing、色、plane、flip）、`xd-dbuf`（latency、slice、join/unjoin、DDB、WM、DBOX、IPC、SAGV） | host: (a) eDP 1920×1080@60 の timing・M/N・MSA・DDI_FUNC_CTL（0x8a210002）・PIPE_SRCSZ・PLANE_*・PIPE_BOTTOM_COLOR/GAMMA_MODE の語が `regs-selected.txt`/`regs-dump.txt` と一致（`host-lcd-test.c` の期待値を移す）；(b) 1 画面の WM（PLANE_WM0 0x80004010、BUF_CFG 0x0fdb0000、LINETIME 0x77）；(c) 2 画面（A+B）の割り当てが m3 と一致（MBUS_CTL 0x700、DBUF_CTL × 4、A 0x07db0000、B 0x07d50000、DBOX A 0xa9038804/B 0xb1038804）と、1 → 2 → 1 の遷移の register の列；(d) flip の arm と FLIP_DONE の model | p002、p006 | 5 |
| p009 | **screen・takeover・display の段・切り替え**: `xd-screen`（eDP の plan/enable/disable/flip/run）、`xd-takeover`（N0 の読みと規則、GOP の出力の分類、VGA の停止、takeover の disable）、`xd-display`（§3.6）、`xd-diag`、`struct i915_xd_ops` の xd 側、`i915.display=xd` を既定に | host: (a) 一画面の mode set の register の列が §2.3 の順（`host-lcd-modeset-test.c` の family A の順序の検査を移す）；(b) N0 の判断の規則（`host-native-decide-test.c` と同じ 14 検査）；(c) takeover の disable の順と VGA_CONTROL。**5330（ユーザー）**: 起動して GOP の画面から desktop に切り替わる、backlight の get/set、`i915.display=old` でも起動する。QEMU: Venus の image の起動（T1） | p003、p007、p008 | 6 |
| p010 | **hotplug・Type-C・外部 DP・HDMI**: `xd-hpd`、`xd-tc`（ADL-P の流れ、遅延の disconnect）、screen の DP-TC/HDMI の plan と enable、head の 2 画面（DBUF の遷移を含む） | host: (a) `hpd-model.c` の register で HPD の decode/plug/unplug/retry/EDID の変化（`hpd-ktest` の case を host に移す）；(b) TC の connect/disconnect/abort の順（所有 → ready → AUX USBC の well → lane）と巻き戻し、links 0 の後の遅延の disconnect（`host-tc.c` の検査を移す）；(c) DP-ext の probe と fallback（`host-dpext.c`）；(d) HDMI の mode の選択。**5330（ユーザー）**: HDMI の plug で 2 画面、USB-C DP（TC2）の plug で 2 画面、unplug で戻る、plug したまま起動 | p009 | 6 |
| p011 | **suspend の戻り・残り**: suspend の quiesce（HPD の停止、screen の dark、TC の release）、DC9 の enter/leave、DMC の載せ直し、HPD の再 arm と live state の読み直し、backlight device と control（refresh・power）の確認、診断の boot の語、log の文言の一覧 | host: suspend と resume の register の列（§2.14）、resume の間に抜かれた connector が ABSENT になる。**5330（ユーザー）**: suspend → resume で panel が戻る、backlight が戻る、resume の後の HDMI の plug が動く | p009、p010 | 3 |
| p012 | **旧 code の削除と出典の監査**: 旧 40 file と `internal.h` の Linux の型、`old-xd.c`、`dc9.c`、`i915.display=old`、`struct i915_xd_ops`（wrapper を直接の関数に）、`intel/` の Gen12 だけが使う header（WS244 と分担）、`tests/execution/ktest-display.c`・`ktest-display-probe.c`（Linux の型に依存: 消し、kernel で意味のある検査（起動の段の順、well の読み）は `xd.h` の上の小さな ktest に書き直す）、`ktest-gt.c` の `display/hotplug.h`・`power.h` の include を `xd.h` に、display の ktest の set（`vmunix.mk` の 3 行）の整理、旧 host 試験の削除（§6.4）、`host-xd-regs-test.c` の置き換え、"Derived from the Linux kernel" と Linux の MIT の表示を Gen12 の file から消す | `inventory.py` の Gen12 の file の C と S の行が 0、file の先頭に出典が無い（grep）、block ごとの "Value(s) referenced:"/"Procedure referenced:"（複数行 comment）の grep、関数名の対応表の目視、build warning 0（`I915_TESTS=y` でも）、Venus の image の boot-test（T1）。**5330（ユーザー）**: p009〜p011 の 3 つを旧 code 無しで | p011、ユーザーの確認（p009〜p011） | 3 |
| p013 | **規約の全文の見直し**（conformance-reviewer） | `plan/coding-style.md` の全文の checklist、`git diff --check`、build | p012 | 2 |

LW の合計 **51**（±40 %。**1 LW ≈ 担当 1 人の実時間 1〜2 時間**で見積もった: 51 LW ≈ 51〜102 時間の担当の実時間。初版の 36 LW は換算が揃っておらず、B4 の adapter、B3 の CDCLK、B5 の TC、B6 の IRQ の hook、M3 の DBUF の遷移、M5 の DMC、M9 の ktest、M13 の quiesce、own の型の分離を数えていなかった: M18）。
p004・p005・p006 は互いに独立なので担当が空けば並べられる（host 試験だけで QEMU を塞がない）。p009 以降は 5330 のユーザーの確かめが要るので、1 つずつ。

Phase ごとの出力: p002・p004〜p008 は「新しい file + host 試験」だけで kernel の振る舞いは変わらない。p003 は own の file の付け替え（振る舞いは変わらない筈: 5330 で確かめる）。p009 が最初に kernel の振る舞いを変える Phase。

### 6.3 試験の道

- **host**（各 Phase、`plan/ws245/tests/run-xd-host-tests.sh`、ASan/UBSan、数秒）: fake の register の model に対して「register の語」「書き込みの順」「待ちと timeout」「失敗の巻き戻し」を比べる。oracle は (a) 5330 の Linux の dump（`display-ref/regs-selected.txt`（lines 60–61 の無名の行が PLANE_WM0/BUF_CFG）、`regs-dump.txt`、m3 の `range-0x*.txt`（PORT_TX は `range-0x160000.txt`/`range-0x16C000.txt`、DBUF/MBUS は `range-0x44000.txt`/`range-0x45000.txt`、plane は `range-0x70000.txt`）、`intel_reg-dump.txt`、`debugfs/`）の語、(b) Linux の手順の順、(c) 今の試験の期待値（移す物は §6.4）。
  fake の model は今の `dp-fake-hw.c`・`lcd-fake-hw.c`・`hpd-model.c`（own の試験 code、register の挙動の model）を **oracle として残し**、bind する関数だけを xd-io の hook（read/write を差し替えられる）に変える。
- **置き場所（M10）**: 試験の source は tree の中（`src/drivers/gpu/i915/tests/display/host-xd-*.c`、kernel には入れない）。走らせる script `plan/ws245/tests/run-xd-host-tests.sh` と fixture（VBT・OpRegion の bytes）は `plan/ws245/tests/` に置き、**p012 の clearance の時に Q1 に `plan/master.md` の Tools・試験の一覧への登録を頼む**（WS237 の `plan/ws237/tests/` と同じ形: WS が complete でも一覧から参照される物は残る）。
- **QEMU**: QEMU には i915 が無い（VFIO の passthrough は 5330 を host にする必要があり、ユーザーの機械なので使わない）。QEMU で見る物は「i915 の無い image（Venus）の起動が壊れていない」だけ（T1 に boot-test.sh を依頼: p002、p003、p009、p012）。
- **5330 の実機**（ユーザーの確かめ、試験の一覧に載せる物。QEMU の証拠と分けて書く）:
  1. p003: adapter 越しで今と同じ（起動・eDP・backlight・HDMI/USB-C の plug・suspend）。
  2. p009: 起動 → GOP の画面 → desktop（takeover）。`i915: N0:` と `i915: takeover:` の log。backlight の get/set。`i915.display=old` での起動。
  3. p010: HDMI の plug/unplug（head の 2 画面）、USB-C の DP の plug/unplug（TC2、2 画面）、unplug 中の present が止まらない、plug したまま起動。
  4. p011: suspend → resume → desktop が戻る、backlight の値が戻る、resume の後の plug。
  5. p012: 旧 code を消した後の 2〜4。
- **kernel の execution 試験（M9）**: `I915_TESTS=y` の `ktest-display.c`・`ktest-display-probe.c` は Linux の型で display の probe の段を検査している（3452・3078 行）。xd の段は host 試験で検査するので、p012 でこの 2 つを消し、kernel でしか測れない物（init の後の well と DBUF の実際の state、DMC の version）だけを `xd.h` の上の小さな ktest（`ktest-xd-display.c`、200 行程度）に書き直す。`ktest-gt.c` は `display/power.h`・`hotplug.h` を include しているので `xd.h` に向ける。`vmunix.mk` の display の ktest の 3 行（lcd-run・display-ktest・lcd-show …）は p012 で整理する。Gen12 の kernel の test build は p012 で `I915_TESTS=y` の build が通る事を確かめる（実行は 5330 でユーザーが試験の image を焼く時: 一覧に載せる）。
- 負荷・耐久（hotplug の繰り返し、suspend の繰り返し）は別枠（ユーザーに確かめてから）。

### 6.4 今の試験の扱い（2026-10-06 の基準: 変更に追従する時に、参照されていない物は消す）

| 今の試験 | 扱い |
| --- | --- |
| `tests/display/host-dp-test.c`、`host-lcd-test.c`、`host-lcd-modeset-test.c`、`host-capture-test.c` | 走らせる script が無い（README だけが名指す）。期待値（register の語、順序）を p007〜p009 の xd の host 試験に**移し**、file は p012 で消す（path を Q1 へ） |
| `tests/display/dp-fake-hw.c`、`lcd-fake-hw.c`、`hpd-model.c` | 残す（oracle）。bind の関数だけ xd-io の hook に合わせる（p005/p007/p010） |
| `tests/display/edp-ktest.c`、`edp-sync-ktest.c`、`lcd-modeset-ktest.c`、`lcd-show-ktest.c`、`lcdg-ktest.c`、`opregion-ktest.c`、`hpd-ktest.c`、`display-ktest.c`、`scanout-ktest.c`（kernel の test build） | Linux の型に依存。p012 で消す（scanout-ktest は own なら残す: p012 で判定）。hpd-ktest の case は p010 の host 試験に移す |
| `tests/execution/ktest-display.c`、`ktest-display-probe.c`、`ktest-gt.c` | §6.3 の M9 の扱い（p012） |
| `tests/display/lcd-run.c`、`lcd-flip.c`、`lcd-gpu.c`、`lcd-opregion.c`、`hdmi-output.c`、`hdmi-hotplug.c`、`aux.c`（実機の scenario） | ユーザーの実機の確かめに置き換わる（§6.3）。p012 で消す |
| `plan/ws051/tests/host-tc.c`、`host-tc-tables.c`、`host-dpext.c`、`host-dpext-link.c`、`host-dkl-test.c`、`host-power-map.c`、`host-vbt-pll.c`、`host-n1-tc.c`、`host-unplug-gop.c`、`plan/bugs/BUG-266/tests/`、`BUG-268/tests/` | 期待値（TC PLL の語、DKL の語、power の表、fallback の列）を p006/p007/p010 に移す。file は WS051 の物で `master.md` の Tools（`host-vbt-pll.sh`・`host-tc.sh`）から参照されているので、p012 で Q1 に報告して一覧ごと整理（xd の試験に置き換わる） |
| `host-native-decide-test.c`、`host-bootfb-test.c`、`host-gen9-*` | 残す（own・Gen9）。native-decide は p009 で xd-takeover に付け替える |
| `plan/ws031/display-ref/`、`plan/ws051/tests/m3-5330-20261007/` | 残す（oracle のデータ） |

### 6.5 大きさの見積もり（新しい xd）

| module | 行（概算） | 根拠 |
| --- | --- | --- |
| regs/tables の header | 2700 | 今の `intel/` の Gen12 の define のうち使う物（~700 register・bit）と表 T1〜T8/W1/W2/H1/V1〜V3/O1/D1 |
| io・diag | 900 | |
| vbt・edid | 1600 | 今の vbt.c の 4978 行は 16 block の全 field と version の分岐。使う 8 block に絞る |
| aux・gmbus | 900 | |
| power・dmc・clock | 2600 | CDCLK の set と IRQ の hook を足した |
| ddi・dp・panel | 2400 | |
| pipe・dbuf | 1900 | DBUF の遷移を足した |
| tc・hpd | 1900 | |
| screen・takeover・display | 2200 | |
| 計 | ~17100 | 今の 60k（Linux 環境）の 3/10。p003 の `xd.h`・wrapper・`old-xd.c`（~1500 行）は p012 で消える |

## 7. 危険と未決の判断（ユーザーに聞く点。決定 1〜4 で閉じた物は §8 の表に）

| # | 点 | 既定（この設計） | 聞く理由 |
| --- | --- | --- | --- |
| 0 | p009 の切り替えの既定。`i915.display=xd` を既定にして `old` を逃げ道にするか、`old` を既定にして `xd` を試すか | **xd を既定**（p009 を 5330 で確かめるまで main に merge しない運用も可） | 5330 はユーザーの日常の機械。起動できない版を main に入れない運用との兼ね合い |
| 1 | HDMI を DVI mode（infoframe・scrambling・audio 無し、≤ 340 MHz）のままにするか | **そのまま**（今の振る舞いを保つのが完了の条件） | HDMI 2.0 の 4K60 や audio は別の WS |
| 2 | mode set から CDCLK を変える事（init/uninit の設定は持つ: §2.6） | 持たない（入らない mode は拒む） | 5330 の GOP の 192 MHz は 1080p と m3 の 2 画面に足りている。4K の HDMI は別 WS |
| 3 | PSR・DC3CO・DC6 の扱い | PSR は off のまま。DC6 は DMC が載った時に許す（今と同じ）。DC3CO は持たない | 5330 の panel は PSR 無し |
| 4 | hotplug の storm 検出と poll への切り替え | 持たない。短い pulse は link status を確かめ、落ちていれば retrain の要求（今と同じ） | 2 分の再有効化や poll の thread は複雑さの割に 5330 で必要になった記録が無い |
| 5 | OpRegion の runtime（ACPI の notify、ASLE の backlight 要求、DIDL/CADL）と ACPI の backlight の key | 持たない（今も持たない） | brightness の key は Keiland が backlight device で扱う |
| 6 | VBT・OpRegion の layout の出典が Linux（MIT）だけである事 | guardrail の「値の引用」で可 | 公開の仕様書が無い。VBT の block の構造を "data" と見る解釈の確認 |
| 7 | 5330 の VBT・OpRegion の生の bytes（m3 の `debugfs/i915_vbt.txt`・`i915_opregion.txt`、firmware の表）を試験の fixture として tree に置く事 | 置く（EDID・DPCD と同じ「licence and redistribution not audited」の注記で） | Dell の firmware のデータの再配布 |
| 8 | 旧 code と adapter を p003〜p012 の間 tree に残す事（licence の表示も残る） | 残す（§6.1） | WS244 の licence の整理との順序 |
| 9 | eDP 1.4 の link rate の表（DPCD 0x010）を新しく支える事 | 支える（小さい） | 今の code は拒む。5330 では使われない |
| 10 | kernel の execution 試験（`ktest-display`・`ktest-display-probe`）を消して `xd.h` の上の小さな ktest に置き換える事 | 置き換える（§6.3） | 6500 行の検査を捨てる。xd の host 試験で同じ事を測る |

危険:
- **ADL-P の PRM が無い**: 手順は Linux の ADL-P の分岐から取る（決定 3）。Linux の分岐を読み落とす（`IS_ALDERLAKE_P`・`DISPLAY_VER == 13`・`PCH_ADP` の条件）のが最大の危険。p006〜p010 の受け入れに「Linux の ADL-P の分岐の一覧（関数ごと）と xd の対応」の表を含める。
- **GOP の引き継ぎ**: takeover の disable の順を間違えると画面が消えたまま戻らない。p009 の host 試験で順序を固定し、実機では `i915.display=old` が逃げ道。
- **Type-C**: IOM の ready と AUX USBC の well の挙動は実機でしか試せない。p010 の実機の確かめが要る。
- **DBUF の遷移（1 ↔ 2 画面）**: vblank に同期した DDB の書き換えを誤ると resident の underrun。p008 の host 試験で列を固定し、p010 の実機で確かめる。
- **DMC**: 載せ方を間違えると DC6 で display が止まる。p006 の parse の試験 + p011 の suspend の実機。
- **試験の資産の移行**: 旧試験の期待値を落とすと退行に気づかない。§6.4 の表で移す物を名指した。
- **LW の超過**: p003・p009・p010 が大きい。p009 は eDP だけに絞り、HDMI/DP は p010 に分けた。p003 は adapter の型の変換が肥大しうる（`xd.h` の型を小さく保つ）。
- **照合の Linux の tree が P1 の worktree の `build/` にある事（m13）**: 出典の表記は「Linux v6.8.12 <path> (<関数>)」で版と path が自己完結し、tree の場所に依らない。tree が消えたら kernel.org の `linux-6.8.12.tar.xz` を取り直して検証する（WS229 p001 の手順）。担当ごとに読み取り専用で使う。

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
| M18 LW の過小評価と換算 | 直した | 1 LW ≈ 1〜2 時間で揃え、足した作業を数えて 36 → 51 LW | §6.2 |
| M19 Tiger Lake を支えない事は機能を失う | 指摘どおり（2026-10-15 訂正: display 12 は実機で動作確認済み、xd に含める。設計は次の改訂） | display 12 の分岐は持たない。5320 の i915 の display は失われる（bootfb）。§7 から外し §4.4 に帰結を書いた | §4.4 |
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
| m14 記述の細部 | 直さない | 要約に細部が無く、どの記述かが分からない。design-review.md の原文が届いたら扱う | — |

合計: 直した 36（M8・M12 は指摘の一部を訂正しつつ直した）、直さない 2（m1・m14）。
