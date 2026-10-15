<!-- awesome-plan project=zedbsd record=ws245-p001-design -->

# WS245 の設計: Gen12（ADL-P、Latitude 5330）の display の書き直し

2026-10-16 P2（q991、ws245-p001）。親: [p001](phase001/phase.md)・[WS245](ws.md)。code は書かない（p002 以降）。
今の code（`src/drivers/gpu/i915/display/`、`intel/`）は「外から見える振る舞いと register の書き込みの列」を知るためだけに読んだ。
その構造・型・名前・制御の流れは、この設計に持ち込まない（§3 の「持ち込まない物」の表）。

## 0. 出典とライセンス

### 0.1 公開の PRM の入手の状況

Intel の公開の PRM（Programmer's Reference Manual、intel.com の "Graphics for Linux Developer Reference"）には
**Alder Lake（ADL-P）の版は無い**（2026-10-16 に索引 `developer-reference/1-0/overview.html` を確認: Alchemist、Rocket Lake、Tiger Lake、
Lakefield、DG1、Ice Lake 以前のみ。01.org は閉じており、x.org の mirror は 403）。ADL-P の display は Linux の言い方で display 13（XE_LPD）で、
Tiger Lake（display 12）の display engine に pipe D・DBUF 4 slice・PIPE_MISC2・CDCLK の squash などを足した物である。公開の文書のうち
次の 2 組を第一の出典にする（`plan/ws245/temp/` に PDF と `pdftotext -layout` の text を置いた。git には入れない。再取得は
`https://cdrdv2.intel.com/v1/dl/getcontent/<id>`）。

| 文書 | id | 版 | 使う所 |
| --- | --- | --- | --- |
| TGL Vol 12 Display Engine（IHD-OS-TGL-Vol 12-12.21、433 頁） | 705833 | 2021-12 | mode set の手順（display の init/uninit、DP・HDMI の enable/disable、DC5/6/9、DMC の package、CDCLK・DPLL・DKL PLL の programming、power well、combo PHY の init と voltage swing、Type-C の flow と TCCOLD、AUX の手順、hotplug の手順、watermark の算法） |
| TGL Vol 2c Registers A–L / M–Z（1442 + 1722 頁） | 703046 / 703047 | 2021-12 | north display の register の offset と bit（pipe・plane・transcoder・DDI・PHY・DKL・DPLL・CDCLK・PWR_WELL・DBUF・DE の割り込み・HPD の north・AUX・PSR・DSB） |
| TGL Vol 1 Preface / Vol 14 Workarounds | 703034 / 705836 | 2021-12 | ライセンスの文 / display の WA（DPLL_SSC の sscen、PCH の display clock） |
| ACM（Alchemist/DG2）Vol 12 Display Engine（IHD-OS-ACM-Vol 12-3.23、372 頁） | 774520 | 2023-03 | display 13 の差分（CDCLK の squash、pipe count の mailbox、PSR2 の扱い）と、**South Display Engine**（panel power、backlight PWM、GMBUS、south の hotplug と割り込み） |
| ACM Vol 2c Registers Part 1 / 2 | 774481 / 774482 | 2023-03 | south display の register（GMBUS0–5、PP_CONTROL/STATUS/ON/OFF_DELAYS、SBLC_PWM_CTL1/FREQ/DUTY、SHOTPLUG_CTL_DDI/TC、SHPD_FILTER_CNT、RAWCLK_FREQ、NDE_RSTWRN_OPT、TCSS_DDI_STATUS）と display 13 の PIPE_MISC・CDCLK_SQUASH_CTL |

register の名前ごとにどの巻が持つかは `temp/` の text を grep して確かめた（§5.2 の表）。**どの PRM にも無い物**（ADL-P 固有の offset・表）は
Linux v6.8.12 の MIT の file（`i915_reg.h`、`display/intel_*_regs.h`、`intel_vbt_defs.h`、`intel_opregion.c`、`include/drm/display/drm_dp.h`）の
**値だけ**を、guardrail の「Value(s) referenced:」の形で引く（§5）。Linux の展開は `/home/awe/zedBSD-worktrees/p1/build/ws229-p001/linux/linux-6.8.12/`
（WS229 p001 が検証した tarball、読むだけ）。

### 0.2 ライセンス

- PRM（Vol 1 Preface の Notices、TGL・ACM とも同文）: 「you may publish an unmodified copy」「code included in this document is licensed subject to
  Zero-Clause BSD (0BSD)」「You may create software implementations based on this document … intended to execute on the Intel product(s)
  referenced」「No rights are granted to create modifications or derivatives of this document」。→ PRM から **手順と値を読んで実装する**ことは
  許される。PRM の文章を長く写した comment や、PRM の表をそのまま載せた文書は「derivative of the document」になり得るので、
  comment は自分の言葉で書き、表は値だけを持ち「Value(s) referenced: IHD-OS-TGL-Vol 12-12.21 §<章> (<表の名前>)」と出典を付ける。
  PRM の文は SPDX の id を持たない（0BSD は「code included in this document」の分だけ）。
- Linux v6.8.12 の値の出典: `i915_reg.h`（MIT、"Permission is hereby granted"）、`display/intel_*_regs.h`（SPDX MIT）、`intel_vbt_defs.h`（MIT）、
  `intel_opregion.c`（MIT）、`include/drm/display/drm_dp.h`（MIT）。値の引用は guardrail（2026-10-16）の「MIT・GPL の出典のデータの引用」のとおり
  block ごとに「Value(s) referenced: Linux v6.8.12 <path> (<define・表の名前>)」。**code・comment・構造は写さない**。GPL の file（`intel_hotplug.c` など）も
  値だけなら引けるが、この設計では GPL の file から引く値は無い（hotplug の pin の bit も `i915_reg.h`（MIT）にある）。
- 転記の形: file の先頭には出典を書かない（zedBSD の著作権 header と file の説明だけ）。出典は block の直前の複数行 comment。
  `userland/base/licenses/i915-driver/` の整理は WS244 の最後（この WS では Gen12 の file から "Derived from the Linux kernel" と
  Linux の MIT の表示を消す所まで）。
- hardware の読み出し（`plan/ws031/display-ref/` の 5330 の Linux の register dump・DPCD・EDID、`plan/ws051/tests/m3-5330-20261007/` の USB-C DP の dump）は
  値の照合に使う。panel の EDID と DPCD は「licence and redistribution not audited」と注記されたままなので、試験の fixture としての扱いは今と同じ
  （tree の中の `tests/display/dp-fixture-latitude5330.h`）。

### 0.3 今の code の読み方（この設計で守った規則）

- 読んだのは「何をするか」: register の列と待ち、判断の規則、外の file から呼ばれる関数と型（§4）、試験が何を測るか（§6.4）。
- 持ち込まない物: Linux の型（`intel_crtc_state`・`drm_i915_private`・`intel_encoder`・`i915_lcd_*` の環境）、関数の分け方
  （`pre_enable`/`enable`/`post_disable` の encoder の段、`crtc_enable` の hook）、atomic state の形（global obj、`i915_lcd_words` の recorder）、
  "world" の割り当て、Linux の名前（`hsw_*`・`icl_*`・`tgl_*`・`skl_*`）、comment の文。
- 残す物: 外向きの口（§4.1）、boot の語・log の文言のうち試験と運用が読む物（§4.4）、hardware の事実（§1）、試験の oracle（§6.4）。

## 1. 対象の hardware の事実（5330、display-ref と m3 の dump から）

| 項目 | 事実 | 出典 |
| --- | --- | --- |
| GPU | ADL-P Iris Xe、display 13、GT は graphics 12 | `device-info.c`、`dmesg-display.txt` |
| PCH | Alder Lake-P（ADP-P）。south display は PCH 側（GMBUS・PP・PWM・SHOTPLUG・SDE の割り込み）。raw clock 38.4 MHz | `dmesg`、PRM ACM Vol 12 South Display General |
| 参照 clock | 38.4 MHz non-SSC（DSSM で読む） | `dbg-i915_shared_dplls_info.txt`（"PLL refclks: non-SSC: 38400 kHz"） |
| 内蔵 panel | eDP、AUO B133HAN、1920×1080、60 Hz（pixel 140.8 MHz）と 48 Hz、6 bpc（dither）、290×170 mm。DDI A / combo PHY A / pipe A / transcoder A / AUX A / DPLL0。DPCD 1.1、HBR（2.7 GHz）× 2 lane、eDP 1.4 の表示制御（0x700）あり、PSR 無し | `dbg-i915_display_info.txt`、`dp-fixture-latitude5330.h`、`regs-selected.txt` |
| DPLL0 の語 | CFGCR0 0x00e001a5、CFGCR1 0x00000088（HBR、38.4 MHz 参照） | `dbg-i915_shared_dplls_info.txt` |
| transcoder A の語 | TRANS_DDI_FUNC_CTL 0x8a210002（DP SST、DDI A、2 lane、6 bpc） | `regs-selected.txt` |
| backlight | PCH の PWM、max 96000（PWM の周期の値）、VBT の controller 0 | `00-summary.txt`、`regs-selected.txt`（BLC_PWM_FREQ 0x17700 = 96000） |
| 外部の口 | HDMI-A-1（DDI B、combo PHY B、pipe/transcoder B、DVI mode で駆動）、DP-1 = TC1、DP-2 = TC2（USB-C、DP-alt。m3 の dump: TC2・pin C・4 lane・1920×1280・pipe B・TC PLL 2） | `00-summary.txt`、`plan/ws051/tests/m3-5330-20261007/` |
| DMC | `i915/adlp_dmc.bin` v2.20（main と pipe A/B の firmware を使う）。package は `userland/firmware/i915/`（linux-firmware 20260410、sha256 は manifest） | `dbg-i915_dmc_info.txt`、`i915-firmware.manifest` |
| DBUF | 4 slice × 1024 block。Linux は pipe A だけの時に plane 1 に 0–4060、cursor に 4060–4096 | `dbg-i915_ddb_info.txt` |
| power well | Linux の読み: PW1（always on）、PW2、PW_A..D（pipe ごと）、DDI IO A/B/TC1-4、AUX A..E/USBC1-4/TBT1-4、DC_off | `dbg-i915_power_domain_info.txt`、PRM TGL Vol 12 "Functions Within Each Well"（TGL は PG1..PG5。ADL-P は pipe ごとの well に分かれる: §2.5） |
| GOP | UEFI の GOP は eDP を pipe A で灯して起動する。framebuffer は stolen memory、GGTT の低い番地 | `takeover.c` の N0 の log、WS237 §2.2 |
| VT-d | 起動時に GFXVTBAR を読み、translation が有効なら display を使わない（今の N0 の規則） | `takeover.c` |

## 2. Gen12（ADL-P）の display の model（PRM から）

名前は PRM の register の名前（大文字、`_` 区切り）で書く。zedBSD の code の名前は §3。

### 2.1 ブロック図（データの流れと制御の流れ）

```
 memory ──GGTT──▶ plane 1 (pipe A..D) ──▶ pipe (src size, misc, color) ──▶ transcoder (timing, MSA, DDI func, DP transport)
                                                                                 │
                       DBUF (S1..S4) ◀── watermark/DDB ──┘                      ▼
                                                                 DDI (A, B: combo PHY / TC1..4: Type-C PHY + FIA)
 reference 38.4 MHz ──▶ DPLL0/1/4 (combo) ──DPCLKA_CFGCR0──▶ DDI clock ──TRANS_CLK_SEL──▶ transcoder clock
                   └──▶ DKL PLL (TC port) ──DDI_CLK_SEL──┘
                   └──▶ CDCLK PLL ──▶ CDCLK (pipe・DBUF・AUX の timer)
 PCH (south): raw clock 38.4 MHz ──▶ GMBUS (DDC), PP (panel power), SBLC PWM (backlight), SHOTPLUG (DDI/TC の HPD), SDE の割り込み
 north: DE の割り込み (pipe/port/misc/HPD TC・TBT), AUX (A..E, USBC1..4), PSR, DMC (DC5/6/9)
 power: PG0 (PCI, clocks, 割り込み) → PG1 (pipe A, transcoder A, DDI A..C, DBUF の機能) → PG2 (VDSC A) → PG3 (pipe B, 外部, TC) → PG4 (C) → PG5 (D)
```

### 2.2 display の init と uninit（PRM TGL Vol 12 "Sequences to Initialize Display"）

init: (1) NDE_RSTWRN_OPT の PCH reset handshake を立てる。south の raw clock を GMBUS・HPD・panel power より前に設定する。(2) combo PHY の init
（§2.7）。(3) PG1: FUSE_STATUS の PG0 を待つ（20 µs）→ PWR_WELL_CTL2 の PW1 request → state を待つ（38.4 MHz なら 30 µs）→ FUSE の PG1 を待つ（20 µs）。
(4) CDCLK を立てる（§2.6）。(5) 最初の DBUF（DBUF_CTL の power request → state、10 µs）。(6) MBUS の credit。(7) BW_BUDDY（DRAM の種別と channel 数で
page mask を選ぶ。表は PRM の "Bandwidth Buddy Registers"）。
uninit（DC9 の時だけ）: 全部の mode set を disable → DBUF を全部落とす → CDCLK を止める → AUX IO を落とし PW1 の request を消す（10 µs、poll しない）→ combo PHY を un-init。
今の code（`power.c` の core init）は PRM と同じ順で、ADL-P では (6) を飛ばし、(7) の後に GEN11_CHICKEN_DCPR_2 の bit 24–27 と XELPD_DISPLAY_ERR_FATAL_MASK を書く（Linux の WA。offset は Linux から引く）。

### 2.3 DisplayPort（eDP・外部 DP）の enable と disable（PRM TGL Vol 12 "Sequences for DisplayPort"）

enable（SST、MST は使わない）:
1. 使う power well を上げる（§2.5）。Type-C は AUX の power が main link にも要る。
2. panel power sequencing が要るなら（eDP）panel power を入れて enabled を待つ（§2.11）。
3. Type-C（TBT 以外）: DFLEXDPMLE に lane 数を書く（FIA の pin assignment と panel の lane 数の小さい方）。
4. port の PLL（§2.6）: combo は DPLL を立てて DPCLKA_CFGCR0 で DDI に map し、別の書き込みで clock を on にする。TC は DKL PLL を立てて DDI_CLK_SEL と DPCLKA_CFGCR0 の TC clock off を消す。
5. DDI IO power（PWR_WELL_CTL_DDI の request → 20 µs、TC の static は 30 µs）。combo は PHY が init 済みであること。
6. TC: DKL の DP_MODE（lane 0/1、pin assignment に応じて x1/x2）。
7. 訓練: TRANS_CLK_SEL で port の clock を transcoder に向ける → TRANS_DDI_FUNC_CTL の DDI select・mode select（enable は後）→ DP_TP_CTL を enable、pattern 1 →
   voltage swing（§2.7、§2.8）→ combo: PORT_CL_DW10 で使う lane を power up → DDI_BUF_CTL を enable → DDI idle status が 0 になるまで（1 ms）→
   DP の仕様の訓練（clock recovery → channel EQ。DPCD の 0x102/0x103..、0x202.. の status、AUX_RD_INTERVAL の待ち）→ DP_TP_CTL を normal。
8. plane・pipe・transcoder: plane を設定（後でもよい）→ transcoder の timing・M/N/TU → TRANS_DDI_FUNC_CTL2（port sync は使わない）→ TRANS_DDI_FUNC_CTL を enable → TRANS_CONF を enable → backlight を入れる。
disable: backlight off → plane off → TRANS_CONF off → off の status を待つ（2 frame）→ TRANS_DDI_FUNC_CTL を DDI select none で off → DDI_BUF_CTL off → DP_TP_CTL off（idle にしない）→
8 µs か idle status → TRANS_CLK_SEL を none → panel power off → DDI IO power off → PLL の map を外し、不要なら PLL を止める → power well を落とす。
訓練の途中で lane 数・rate を変える時は port を disable して PLL からやり直す（PRM の Notes）。失敗したら、そこまで enable した物を disable の順で戻す。

### 2.4 HDMI/DVI の enable と disable（"Sequences for HDMI and DVI"）

enable: power well → PLL（SSC 無し、HDMI の formula: symbol clock = DCO/(Pdiv·Qdiv·Kdiv)/5、DCO ≥ 7998 MHz、Pdiv 2/3/5/7、Kdiv 1/2/3、Kdiv ≠ 2 なら Qdiv = 1）→ DDI IO power →
TRANS_CLK_SEL → plane → transcoder の timing → TRANS_DDI_FUNC_CTL → TRANS_CONF → **最後に port**（swing → PORT_CL_DW10 の全 lane → DDI_BUF_CTL → idle が消えるまで 500 µs）。
disable: plane → TRANS_CONF（off を待つ）→ TRANS_DDI_FUNC_CTL none → DDI_BUF_CTL off → 8 µs → TRANS_CLK_SEL none → DDI IO off → PLL off → power well。
5330 の HDMI は今と同じく DVI mode（infoframe・scrambling 無し、24 bpp、TMDS ≤ 340 MHz）で駆動する（§7 の未決 3）。

### 2.5 power well と DC state（"Sequences for Power Wells"、"Display C5/C6"、"Display C9"）

- TGL の PRM は PG1..PG5（PG3 = pipe B・外部・VGA・TC の DDI と AUX、PG4 = pipe C、PG5 = pipe D）で「順に上げ、飛ばさない」。ADL-P は Linux の読み（`dbg-i915_power_domain_info.txt`、`intel_display_power_map.c` の xelpd）では PW2 の下に **PW_A..PW_D（pipe ごと）** があり、DDI IO と AUX の well は PWR_WELL_CTL_DDI / PWR_WELL_CTL_AUX の instance。
  PRM に ADL-P の表が無いので、**well の index と domain の対応は Linux の表の値を引く**（§5.3 の表 W1）。enable の形は PRM と同じ: request → state を 20 µs → FUSE の PGn を 20 µs。disable は request を消して state を 20 µs。
- DC5/DC6: DMC の program を載せた後、PG3 以上を落とし、0x45520（DC_STATE_DEBUG）の bit 1:0 を 11b、DC_STATE_EN を "up to DC6"。mode set と AUX の間は DC5/6 を禁止する（PRM の Programming Note）。DC5↔DC6 の切り替えは disable を挟み program を載せ直す。
- DC9（suspend）: DC5 を禁止 → 全 mode set を disable → 割り込みを mask → 保存 → uninit → DC9 allow。戻り: DC9 allow を消す → init → 復元 → 割り込み。
- AUX の power: combo の AUX IO は PWR_WELL_CTL_AUX の request（PG1 と並行してよい）。TC の AUX は「connect の間だけ」、request → ack → DKL_CMN_UC_DW27 の uc_health（10 µs）。
- 今の code との差: 今は DC6 を DMC の有無で gate し、DC_OFF を 17 ms 遅らせて返す。新しい model では「display の作業の間は DC off を保持し、作業が終わったら許す」を 1 つの refcount にする（§3.4）。

### 2.6 clock（"Clocks"、"Sequences for Changing CD Clock Frequency"、"XeD Port Clock Programming"、"TypeC PHY PLL Programming"）

- 参照: 19.2/24/38.4 MHz、DSSM が示す。38.4 MHz は DPLL が 2 で割るので 19.2 の表を使う。
- combo DPLL（DPLL0/1、DPLL4 は U/Y には無い）: DPLL_ENABLE の power → power state（すぐ）→ DPLL_SSC（TGL の WA: CFGCR0 の SSC bit は効かず DPLL_SSC の sscen を使う）→ DPLL_CFGCR0（DCO 整数・小数）→ DPLL_CFGCR1（Pdiv/Kdiv/Qdiv）→ 読み戻し → 必要なら voltage の mailbox → enable → lock（50 µs）→ DPCLKA_CFGCR0 で map → 別の書き込みで clock on。
  DP の rate ごとの値は PRM の表 "DisplayPort Mode PLL Values"（1.62/2.16/2.7/3.24/4.32/5.4/6.48/8.1 GHz × 24 MHz と 19.2/38.4 MHz）。5330 の HBR・38.4 MHz は DCO 0x1A5/0x7000、Pdiv 3、Kdiv 2 → CFGCR0 0x00e001a5、CFGCR1 0x88 と一致する（§1）。
  ADL-P の WA（38.4 MHz で DCO の小数を半分にする #22010492432）は Linux の値で持つ。
- DKL PLL（TC port、display は PLL1 を使う）: MGPLL_ENABLE（ADL-P は PORTTC_PLL_ENABLE の instance、offset は Linux）の power → 10 µs → rate 非依存の DKL の register → rate 依存の register（PRM の表、DP の 1.62/2.7/5.4/8.1）→ 読み戻し → enable → lock（900 µs）→ DDI_CLK_SEL → DPCLKA_CFGCR0 の TC clock on。HDMI は PRM の算法（div1 ∈ {7,5,3,2}、div2 ∈ {10..1}、DCO 7.992–10 GHz）。
- CDCLK: PRM の表（38.4 MHz: 172.8/192/307.2/326.4/556.8/652.8 MHz の PLL ratio・CD2X・decimal）と、display 13 の squash（PLL を 1305.6 MHz に固定し CDCLK_SQUASH_CTL の window で割る、ACM Vol 12）。変更の手順: 全 mode set を disable（CD2X だけなら不要）→ voltage の "Sequence Before" mailbox → PLL の enable/disable/relock（200 µs）か CDCLK_CTL の divider → "Sequence After" → CDCLK を使う機能の再設定。
  **方針**: GOP が設定した CDCLK を読んでそのまま使い、必要な pixel rate が入らない mode は拒む（今の code と同じ。CDCLK の変更は別の Phase に残す: §6.2）。
- pipe count の mailbox（display 13、ACM Vol 12 "Sequence for Pipe Count Change"）: pipe の well を上げる前・落とした後に GT driver mailbox 0x7 で pipe 数を伝える。今の code はこれを pcode の `pmdemand` として持つ。新しい model では xd-power の pipe well の上げ下げに付ける。

### 2.7 combo PHY（"Combo PHY DDI Buffer"）

init（PHY A が comp source、B/C は sink。DDI A の PHY は DC6 で保たれる）: PORT_COMP_DW0 の comp init が立っていれば済み → PORT_TX_DW8 の DCC clock select/div、PORT_PCS_DW1 の DCC mode → PHY_MISC の comp power down を消す →
procmon の値（PORT_COMP_DW3 の voltage/process から PORT_COMP_DW1/9/10、表は PRM "Procmon Reference Values"）→ comp source なら PORT_COMP_DW8 の irefgen → PORT_COMP_DW0 の comp init → PORT_CL_DW5 の CL power down enable。
voltage swing（訓練中も同じ手順）: eDP/DP なら PORT_PCS_DW1 の cmnkeeper → PORT_TX_DW4 の loadgen select（lane ごと、rate と lane 数の表）→ PORT_CL_DW5 の SUS clock 11b → PORT_TX_DW5 の training enable を消す →
PORT_TX_DW5 の scaling mode 010b、PORT_TX_DW2 の rcomp scalar 0x98、PORT_TX_DW4 の post cursor 2 = 0、PORT_TX_DW5 の rterm 110b・3 tap disable・cursor program 0・coeff polarity 0、表の swing/pre-emphasis の値（PORT_TX_DW2 の swing select、PORT_TX_DW4 の cursor coeff・post cursor 1、PORT_TX_DW7 の n scalar）→ training enable を立てる。
表: PRM "Voltage Swing Programming" の表（TGL: DP HBR、HBR2/3、eDP、HDMI）。**ADL-P 固有の表**（Linux `adlp_combo_phy_trans_dp_hbr` など）は PRM に無いので Linux の値を引く（§5.3 の T1）。

### 2.8 Type-C PHY（Dekel）と FIA（"TypeC PHY DDI Buffer"、"Type-C Hot Plug Detection"、"Connect Flows"、"TCCOLD"）

- PHY は 2 lane、各 lane に TX1 と TX2。DP の x1/x2 は DP_MODE で lane ごとに選ぶ。swing: DKL_TX_PMD_LANE_SUS を消す → DKL_TX_DPCNTL0/1 に swing・preshoot・de-emphasis（PRM の "DP Voltage Swing Table"、HDMI の表）→ DKL_TX_DPCNTL2 の 20 bit mode を 0。
- DKL の register は port ごとの window（HIP_INDEX_REG で bank を選ぶ）で読み書きする（Linux: 0x168000 + 0x1000·n、index 0x1010a0/a4。PRM Vol 2c に HIP_INDEX_REG0/1 あり）。
- AUX の power は connect の間だけ入れる（DP-alt・static）。TCCOLD は GT driver mailbox 0x80000026（block: data 0、unblock: data 1、200 µs、block の確認は data の bit 0 を 600 µs まで繰り返す）で止める。
- connect（DP-alt）: TCCOLD を block → DFLEXDPPMS の DPPMSTC が 1（IOM が lane を DP にした）→ DFLEXDPCSSS の DPPMSTC を 1（safe mode を抜ける）→ DFLEXDPSP の live state を確かめる（TBT なら TBT の flow）→ DFLEXDPSP の DPX4TXLATC で lane 数 → AUX（DDI_AUX_CTL の IO select = legacy、PWR_WELL_CTL_AUX の non-TBT）で DPCD/EDID → mode set。abort したら DPCSSS を 0 に戻し TCCOLD を unblock。
  disconnect: mode set を disable → DPCSSS を 0 → TCCOLD を unblock。
- hotplug: TC/TBT は north の HOTPLUG_CTL（TC・TBT の instance）と DE_HPD の割り込み、live state は DFLEXDPSP（割り込みの ISR は信用しない）。reset・power state の後は「HPD を enable → 各 TC port の DPPMS/DPCSSS/live state の真理表で connect/disconnect を処理」。
- 今の code との差: 今は DFLEXDPPMS/DFLEXDPCSSS を使わず DDI_BUF_CTL の bit 6（PHY ownership、Linux の display 13 の読み）と TCSS_DDI_STATUS の ready で所有を取る。**ADL-P（display 13）では Linux は FIA の DPPMS/DPCSSS の代わりに DDI_BUF_CTL の ownership bit と TCSS_DDI_STATUS を使う**。PRM の TGL の flow と Linux の display 13 の flow のどちらを採るかは §7 の未決 4（設計の既定: display 13 の register は ACM Vol 2c の TCSS_DDI_STATUS と DDI_BUF_CTL の定義で確かめ、Linux と同じ所有の取り方にする。DFLEX の lane・pin の読みは PRM どおり）。

### 2.9 AUX（"AUX programming sequence"）と GMBUS（ACM Vol 12 "GMBUS and GPIO"）

- AUX: display が init 済み・AUX の power が入り・PSR/GTC が止まり・DC5/6 が禁止された状態で、DDI_AUX_DATA_*_[0-4] に message（header + data、big endian の pack）→ DDI_AUX_CTL に timeout（600 µs 以上。LTTPR のため最大 4000 µs を推奨）・message size・START（bit 31）→ bit 31:30 が 01 になるまで待つ → bit 25（receive error）なら 1 を書いて消し data を読まない → DATA を読む → bit 30（done）を消す。
  TC port は DDI_AUX_CTL の IO select（legacy/TBT）。reply の DEFER/NACK/timeout の再試行の回数は DP の仕様（ここでは今の挙動: DEFER は最大 7 回、timeout は 5 回を保つ）。
- GMBUS（PCH）: GMBUS0 の pin pair（port 1 = DDI A、2 = DDI B、9 = TC1 の DDC、ACM の表; ADL-P の pin の番号は Linux の `intel_gmbus.c` の表の値）と速度（100 kHz）→ GMBUS1 に command（index、byte count、slave address、read、START/STOP の cycle）→ GMBUS2 の HW_RDY/HW_WAIT/SATOER/ACTIVE を待つ → GMBUS3 を 4 byte ずつ → 終わりに GMBUS1 の STOP、GMBUS0 を 0 で解放。EDID の segment は bit-bang が要るが 256 byte までは不要。
  timeout: 1 回の待ちは 50 ms、NAK は失敗として返す。

### 2.10 transcoder・pipe・plane・watermark（"Transcoder"、"Pipe"、"Planes"、"Display Watermark Programming"）

- transcoder: TRANS_HTOTAL/HBLANK/HSYNC/VTOTAL/VBLANK/VSYNC（13 bit × 2、値 − 1）、TRANS_VSYNCSHIFT、display 13 は TRANS_SET_CONTEXT_LATENCY に（vblank start − vdisplay）を書き VBLANK の start は 0（Linux の display 13 の読み）、
  DP の TRANS_DATAM1/N1、TRANS_LINKM1/N1（N の書き込みで arm）、TRANS_MSA_MISC（bpc、sync の極性、colorimetry）、TRANS_DDI_FUNC_CTL（enable、DDI select、mode select、bpc、sync、port width、eDP の入力 pipe は無い: display 12+ は transcoder = pipe）、TRANS_DDI_FUNC_CTL2 = 0、TRANS_CONF（enable、state、interlace）。
- pipe: PIPE_SRCSZ（幅 − 1、高さ − 1）、PIPE_MISC（bpc、dither、pixel rounding）、scaler は使わない（PS_CTRL を off のまま）、SKL_BOTTOM_COLOR、GAMMA_MODE（既定の 10 bit、LUT 無し）、PIPE_FRMCNT と PIPE_SCANLINE（観測）、PIPE_ARB_CTL の prog slots（ADL-P の WA、Linux）。
- plane 1: PLANE_CTL（enable、format XRGB8888、linear、rotation 0、ADL-P の ARB slots）、PLANE_STRIDE（64 byte 単位）、PLANE_POS、PLANE_SIZE、PLANE_OFFSET、PLANE_COLOR_CTL、PLANE_WM_0..5、PLANE_WM_TRANS、PLANE_WM_SAGV（display 13）、PLANE_BUF_CFG、**PLANE_SURF（書くと arm、vblank で反映、PLANE_SURFLIVE が今の番地）**。flip は PLANE_SURF だけを書き、FLIP_DONE の割り込み（DE_PIPE の bit 3）か SURFLIVE で確かめる（今と同じ）。
- watermark（PRM の算法）: WM_LINETIME = roundup(8·htotal/pixel rate MHz)。plane ごとに level ごと: method 1 = latency·pixel rate·Bpp/512 + 1、method 2 = ceil(latency·pixel rate/htotal)·blocks per line（linear: ceil(width·Bpp/512) + 1）、linear は「latency ≥ line time なら method 2、そうでなければ method 1」（極端に小さい htotal の特例あり）→ blocks = ceil + 1、lines = ceil(blocks/blocks per line)。DBUF の割り当ては pipe の slice の範囲（ADL-P の表 "DBUF Allocation … Pipes with Enabled Planes"、PRM に 4 slice の表あり: 行 12031 付近）から plane 1 に全部（cursor は使わないので 0。ただし Linux は 36 block を cursor に残す。cursor の DDB を 0 にする決定は §7 の未決 5）。
  latency は GT driver mailbox 0x6（data 0: level 0–3、data 1: level 4–7、1 byte ずつ、display 13 は 6 level）。level 0 が 0 なら全 level に 3 µs を足す（PRM "Memory Latency Data Definition"）。SAGV は mailbox 0xe で止めたまま（今と同じ）。transition WM と SAGV WM は Linux の式（§5.3 の T6）。
- DBUF の power と MBUS: 使う slice の DBUF_CTL の request → state（10 µs）。pipe A だけなら 4 slice を join（MBUS_CTL の join と 1x4、Linux の値）、2 pipe なら A/D が S1+S2、B/C が S3+S4。PIPE_MBUS_DBOX_CTL の credit（ADL-P の WA、Linux の値）。
- 割り込み: DISPLAY_INT_CTL の bit 31 を落として読み、DE_PIPE_IIR（vblank bit 0、FLIP_DONE bit 3、underrun）、DE_PORT_IIR（AUX done）、DE_MISC、DE_HPD（TC/TBT）、SDEIIR（PCH: DDI/TC の HPD、GMBUS）を ack し、bit 31 を戻す（今の `interrupts.c` と同じ。own の file なので構造は保ち、入口の関数名だけ新しい物に向ける）。

### 2.11 panel power・backlight・PSR（ACM Vol 12 "Panel Power and Backlight"、TGL Vol 12 "Panel Self Refresh"）

- panel power（PCH の PP_CONTROL/PP_STATUS/PP_ON_DELAYS/PP_OFF_DELAYS）: VBT の eDP/LFP の T1..T12 と PP の delay の既定（eDP の仕様の T3 ≤ 200 ms、T12 ≥ 500 ms）。power on: VDD force on → PP_CONTROL の power on → PP_STATUS の on を待つ（T1+T3）→ 訓練 → backlight（T8 の後）。off: backlight off（T9）→ power off → off を待つ → power cycle delay（T12）。VDD の単独 on は AUX の間だけ（今と同じ）。
- backlight（PCH の SBLC_PWM_CTL1/FREQ/DUTY、PWM clock = raw clock 38.4 MHz、increment 1）: FREQ に周期（raw clock / PWM 周波数、VBT の block 43 の Hz）、DUTY に duty、CTL1 で enable と極性。周波数の変更は PWM を止めて書く。5330 は controller 0、max 96000（= 38.4 MHz / 400 Hz）。
- PSR: **使わない**（panel が PSR を持たない上、今も off）。SRD_CTL/PSR2_CTL は読んで、立っていれば disable の手順（SRD_CTL を消し SRD_STATUS の idle を待つ）で止める（GOP が立てる事は無い）。

### 2.12 DMC（"DMC Firmware Package"）

package = CSS header（128 byte、module type 9、version）→ package header（v2: 32 entry、dmc id 0 = main、1..4 = pipe A..D、stepping/substepping の wildcard、offset は dword）→ 各 firmware（DMC header v3: signature 0x40403E3E、fwSize、startMMIOAddr、mmioCount ≤ 20 と addr/data）。
載せ方: payload を program storage（main は 0x8F000 から、pipe DMC は ADL-P の base: Linux）へ dword で書く → header の MMIO pair を書く（main は 0x8F000–0x8FFFF、pipe は 0x5F000–0x5FFFF の範囲に限る）→ DC_STATE_DEBUG。stepping は PCI revision から（A0..）。firmware が無ければ DC5/6 を許さず、他は動く（今と同じ）。

### 2.13 VBT・OpRegion・EDID・DPCD（PRM に無い物）

- OpRegion: PCI config 0xFC（ASLS）→ 8 KiB、signature "IntelGraphicsMem"、mailbox 1（ACPI）0x100、2（SWSCI）0x200、3（ASLE）0x300、4（VBT 6 KiB）0x400、5（ASLE 拡張）0x1C00。VBT は 2.1+ では RVDA（OpRegion からの相対）/RVDS で mailbox 4 の外にもある。**runtime（DRDY/ARDY/CADL/DIDL、ASLE の backlight 要求、ACPI notify）は今も使っていないので、この WS でも使わない**（OpRegion は VBT を取り出すためだけ）。
- VBT: header（"$VBT"、version）→ BDB（"BIOS_DATA_BLOCK"、block の列）。使う block: 1 general features（panel fitting、backlight 系の flag）、2 general definitions（child device: DVO port → DDI、device type、AUX channel、DDC pin、lane reversal、TC/TBT の flag、HDMI の level shift）、12 driver features、27 eDP（panel ごとの rate/lane、PPS の T 値、low vswing、HOBL）、40 LFP options、41 LFP data pointers + 42 LFP data（timing、panel type）、43 LFP backlight（PWM 周波数、controller、min brightness、極性）。version ごとの field の有無は `intel_vbt_defs.h` の comment（MIT）の値で持つ。ADL-P の DVO port → DDI の対応（DVO の A/B/C → DDI A/B/C、D/E → ADL-P の D/E、**F/G/H/I → TC1..4**、HDMI の DVO も同じ）、AUX channel の値（0x40 → A、0x10 → B、0x60..0x90 → USBC1..4）、DDC の pin（1 = DDI A、2 = DDI B、9..12 = TC1..4）は Linux の値（§5.3 V2）。
  今の code が VBT から取って使っている物（新しい xd-vbt が返す物）: port の child（kind: DP/eDP/HDMI、AUX、DDC pin、lane reversal、TC/TBT、HDMI level shift、DP の max rate/lanes）、panel type（LVDS options → 0xFF なら EDID の PnP id で探す）、LFP の DTD（generic DTD が優先）、backlight（PWM Hz、controller、min、極性、16 bit の level）、eDP（bpp、PPS の T1_T3/T8/T9/T10/T11_T12、rate/lanes、low vswing、HOBL）、general features の SSC と AFC startup、LFP power の HOBL。VBT が無い時の既定（port A = eDP、B/C = DP+HDMI、version 155）も保つ。
- panel power と backlight の register（PCH、ADP-P）: PPS は 0xC7200 から 0x100 ごとに 2 組（index は VBT の backlight controller、index 1 は SOUTH_CHICKEN1 の bit 2 が要る）。PP_CONTROL の power cycle delay は bit 8:4（PP_DIVISOR は無い）。PWM は controller 0 が 0xC8250/54/58（CTL1/FREQ1/DUTY1、enable bit 31、極性 bit 29）、controller 1 が 0xC8350..。ACM Vol 2c の SBLC_PWM_CTL1/FREQ（C8250h/C8254h）、PP_STATUS/PP_CONTROL（C7200h/C7204h）、GMBUS0（C5100h）、SHOTPLUG_CTL_DDI/TC（C4030h/C4034h）、RAWCLK_FREQ（C6204h）は Linux の ADL-P の offset と一致する（2026-10-16 に確認）。
- EDID: VESA E-EDID 1.4 の base block（128 byte、checksum、detailed timing 18 byte × 4、物理の大きさ、bpc は 0x14 の bit）、CEA-861 の拡張（HDMI の VSDB の有無、short video descriptor）。今の `output.c`（自前）の detailed timing の decode と CVT-RB は保つ。
- DPCD: 0x000.. の receiver caps（rev、max rate、max lanes、enhanced framing、TPS3/4、0x00E の拡張 caps の bit → 0x2200）、0x010.. の eDP 1.4 の supported link rates（**今の code は eDP ≥ 1.4 を EINVAL で拒む。新しい xd-dp はこの表を読む**: 5330 の panel は eDP 1.1 相当なので動作は変わらない）、0x100.. の link config（BW set、lane count、TPS set、lane の swing/pre-emphasis 0x103..、downspread 0x107）、0x200.. の status（sink count、IRQ vector、lane status 0x202..、align 0x204、adjust request 0x206..）、0x600 の power、0x700.. の eDP の表示制御、0xF0000 の LTTPR（読むだけ）。address と bit は `drm_dp.h`（MIT）の値。
- 今の AUX の規則で保つ物: 1 transaction は 20 byte まで、timeout は 4 ms（TIME_OUT_MAX = 3）、sync pulse 32/fast 18、busy の待ち 10 ms、再試行 5 回（timeout は即、receive error は 400–500 µs 後）、native の再試行 32 回、I2C-over-AUX は 16 byte 単位で DEFER は 7 回まで、eDP の AUX の前に VDD を入れる、TC port は所有中だけ。GMBUS で保つ物: 100 kHz、read は 511 byte まで（それ以上は byte count override）、待ちは 50 ms、NAK は SW_CLR_INT で片付けて 1 回だけ再試行、bit-bang の fallback は無し（今も無い）。EDID で保つ物: block ごとに 4 回まで、checksum、header の 8 byte、segment は block 2 から。

### 2.14 GOP の引き継ぎと suspend の戻り

- 引き継ぎ: 起動時に（display への最初の書き込みの前に）PWR_WELL_CTL2・TRANS_CONF・TRANS_DDI_FUNC_CTL・PIPE_SRCSZ・PLANE_CTL/STRIDE/SIZE/SURF・DPLL_ENABLE・DPCLKA_CFGCR0・DDI_BUF_CTL・PP_STATUS/CONTROL・PWM を**読むだけ**で、GOP が灯した pipe と port を知り、framebuffer の GGTT の範囲が driver の書く範囲と重ならないかを確かめ、VT-d の状態を見て「使う／使わない」を決める（今の N0 の規則をそのまま保つ: §3.6）。
  使うと決めた後、最初の表示の前に GOP の出力を PRM の disable の順で止めてから（plane → transcoder → port → PLL → well）自分の mode set を行う（今の N1 の「takeover」。Linux のように状態を読み出して atomic の state を再構成する事はしない）。GOP の framebuffer の memory には書かない。
- suspend: 出力を止める（display window を抜ける）→ well を保持 → DC9 の手順（uninit を含む）。resume: DC9 を抜け init → DMC を載せ直す → 次の表示で mode set。保存する register は無い（新しい mode set で全部書く）。今の挙動と同じ。

## 3. zedBSD の自前の構造

### 3.1 名前と置き場所

- 接頭辞 **`xd`**（PRM の "XeD" = Xe Display から。Linux の `xelpd`/`intel_` は使わない）。file は `src/drivers/gpu/i915/display/xd-<役割>.c/.h`、公開の関数 `drv_i915_xd_<役割>_<動詞>()`、static は `i915_xd_...`（既存の規則）、型 `struct i915_xd_<物>`、定数 `I915_XD_*`。
- register と表のデータは `src/drivers/gpu/i915/intel/display-regs-xd.h`（north）、`intel/display-regs-xd-south.h`（PCH）、`intel/display-tables-xd.h`（PLL・CDCLK・buf trans・DBUF・procmon・latency の表）、`intel/xd-vbt-layout.h`、`intel/xd-opregion-layout.h`、`intel/xd-dpcd.h`（使う address だけ）。既存の `intel/*.h`（`mreg.h`・`trans.h`・`dp.h`・`plane.h`・`vbt-defs.h` …）は新しい code からは include しない（WS244 が Gen9 の利用者ごと整理・削除する）。
- 1 つの集約の header `display/xd.h` が、自前の file（present・head・output・control・backlight・display・scanout・bootfb・interrupts）が使う型と関数を全部持つ（design policy §2.6: 公開 header は少なく、意図して包括的に）。他の `xd-*.h` は xd の中だけで include する。

### 3.2 module と責務（file 単位）

| module | 責務 | 持つ状態 | 呼ぶ物 |
| --- | --- | --- | --- |
| `xd-io` | MMIO の read/write/rmw、posting read、`poll(reg, mask, value, timeout_us)`、µs/ms の sleep、**書き込みの記録**（試験と log 用: 直近 N 個の (reg, value) の ring と、名前付きの step）| `struct i915_xd_io { struct i915_mmio *mmio; ring; trace on/off; }` | `mmio.c`、`kern_udelay` |
| `xd-vbt` | VBT の探索（OpRegion mailbox 4 / RVDA、PCI ROM）、検証、block の parse → `struct i915_xd_vbt`（port の表、panel、PPS の T、backlight、eDP の rate/lane/low vswing/HOBL、AFC） | parse の結果（値のみ、生の bytes は保持しない） | `xd-opregion` |
| `xd-opregion` | ASLS の map、header と mailbox の検証、VBT の位置と大きさ、SHA-256（N0 の報告用） | `struct i915_xd_opregion { present, mapped, version, vbt_offset, vbt_size, sha }` | `drv_pci`、`vm` の map |
| `xd-edid` | EDID の base/CEA の decode → `struct i915_xd_edid { preferred mode, modes[], size_mm, bpc, hdmi, audio }`。detailed timing の decode は `output.c` の `drv_i915_output_edid_timing()` を呼ぶ | 無し（純関数） | `output.c` |
| `xd-aux` | AUX channel（port → DDI_AUX_CTL/DATA の base、IO select、power の domain）の 1 transaction、native read/write、I2C-over-AUX、DPCD の helper（read N bytes、write、link status、sink power） | `struct i915_xd_aux { port, base, power domain, stats }` | `xd-io`、`xd-power`（AUX IO、DC off） |
| `xd-gmbus` | GMBUS の 1 transfer（pin、address、index、長さ）、EDID の読み（128/256 byte）、reset | `struct i915_xd_gmbus { lock, pin }` | `xd-io` |
| `xd-power` | well の表（§5.3 W1）と refcount、enable/disable の手順、FUSE、DDI IO / AUX IO の well（TC は uc_health と TCCOLD）、DC state（off の refcount、DC6 の許可は DMC 次第）、display の init/uninit、DC9 の enter/leave、pipe count の mailbox、combo PHY の init（`xd-phy` を呼ぶ）、「pipe の register が読めるか」の照会 | `struct i915_xd_power { well[]: req/state/refs; dc_off_refs; init_done; core_up }` | `xd-io`、`xd-phy`、`xd-clock`（CDCLK の init）、`xd-pcode` |
| `xd-pcode` | GT driver mailbox（`sb_lock` の下）: 1 command の送受と timeout。latency・SAGV・QGV・voltage・pipe count・TCCOLD の command は呼ぶ側が持つ | 無し | `mmio.c` |
| `xd-clock` | 参照 clock（DSSM）、CDCLK の読み（PLL ratio・CD2X・squash → kHz）と最大 pixel rate の判定、combo DPLL の値の選択（表 + HDMI の算法）と enable/disable、DDI への map、DKL PLL の値（表 + 算法）と enable/disable、port clock → transcoder（TRANS_CLK_SEL）、pixel clock・link clock の計算 | `struct i915_xd_clock { ref_khz; cdclk_khz; pll[]: owner, rate }` | `xd-io`、`xd-dkl`、`xd-pcode` |
| `xd-phy` | combo PHY の init/uninit、lane の power（PORT_CL_DW10）、voltage swing と pre-emphasis の programming（表 T1）、DDI_BUF_CTL の語の組み立てと enable/disable と idle の待ち | 無し（port ごとの引数） | `xd-io` |
| `xd-dkl` | Dekel の window（HIP_INDEX）を通す読み書き（lock）、DP_MODE、PMD lane sus、PCS softreset、swing の表（T2）、PLL の register の列 | `struct i915_xd_dkl { lock }` | `xd-io` |
| `xd-tc` | Type-C port の状態機械（§3.5）: live state、mode（DP-alt/TBT/legacy）、pin assignment → lane、所有（connect/disconnect/abort）、TCCOLD の block/unblock、FIA の lane 数、UCSI への報告（今の `tc-kern` の `drv_typec_display_report` の口を保つ） | `struct i915_xd_tc_port { mode, owned, lanes, links, live }` × 4 | `xd-io`、`xd-pcode`、`xd-power`（AUX USBC） |
| `xd-pps` | panel power sequencer: VBT と既定から T 値、VDD の on/off、panel の on/off と待ち、backlight の enable bit、power cycle の待ち | `struct i915_xd_pps { t[], vdd_on, on, last_off_tick }` | `xd-io`、`xd-vbt` |
| `xd-backlight` | PCH の PWM: 周波数と duty の読み書き、enable/disable、level ↔ duty、VBT の min | `struct i915_xd_backlight { period, min, level, on }` | `xd-io`、`xd-vbt` |
| `xd-dmc` | package の parse と選択、program の書き込み、MMIO pair、載せ直し、「DC6 を許せるか」 | `struct i915_xd_dmc { loaded mask, copy of payloads, version }` | `xd-io`、`firmware.c` |
| `xd-dp` | DP の link の方針: rate/lane の候補（DPCD × VBT × source）、bpp、M/N/TU、sink の power、訓練（CR/EQ、swing の要求 → `xd-phy`/`xd-dkl`）、DP_TP_CTL の pattern、link status の確認、fallback の次の候補 | `struct i915_xd_link { rate_khz, lanes, bpp, m/n, trained, train_set[4] }` | `xd-aux`、`xd-phy`、`xd-dkl`、`xd-io` |
| `xd-hdmi` | HDMI/DVI の mode の判定（TMDS clock の上限 340 MHz、EDID の HDMI の有無）、DDI の mode（DVI）、SCDC は使わない | 無し | `xd-gmbus`、`xd-edid` |
| `xd-trans` | transcoder と pipe: timing、MSA、DATA/LINK M/N、DDI_FUNC_CTL、CLK_SEL、TRANS_CONF の enable/disable と待ち、PIPE_SRCSZ、PIPE_MISC、scaler off、bottom color、frame counter、vblank の待ち（割り込み or scanline） | 無し | `xd-io` |
| `xd-plane` | plane 1 の programming、enable/disable、flip（PLANE_SURF）、FLIP_DONE/SURFLIVE の確認、format と stride の検査 | 無し | `xd-io`、`xd-wm` |
| `xd-wm` | latency（mailbox）と調整、DBUF の slice の選択と power、MBUS の join、DDB の割り当て、WM の level・transition・SAGV・LINETIME、SAGV off、QGV の帯域の判定 | `struct i915_xd_wm { latency[8], levels, slices_on, sagv_off }` | `xd-io`、`xd-pcode` |
| `xd-hpd` | hotplug: pin の表、north/south の HPD の enable、割り込みの ack と queue（IRQ context）、worker（live state → TC の flow → AUX/EDID の probe）、connector の記録と generation、retry、event の通知（topology sequence、unplugged、TC partner gone） | `struct i915_xd_hpd { connector[]: kind, port, state, generation, edid, mode; pending bits; work }` | `xd-io`、`xd-tc`、`xd-aux`、`xd-gmbus`、`xd-edid`、`present.c`（通知）|
| `xd-screen` | 1 つの出力の mode set の合成（§3.3）: plan の計算、enable（§2.3/2.4 の順）、disable、flip、status、preflight、retained の扱い。resident の run（灯す → serve → 止める）と head の run | `struct i915_xd_screen { kind, port, pipe, pll, plan, phase, held wells, buffers }` × 2 | 上の全部 |
| `xd-takeover` | 起動時の読み出し（N0）と判断、GOP の出力の分類、takeover（GOP の出力を disable の順で止める）、survey の log | `struct i915_xd_firmware { pipes[], pll, ddi, pp, pwm, fb range, decision }` | `xd-io`、`xd-power`、`xd-screen`（disable） |
| `xd-display` | start/stop/suspend/resume の段（§3.6）。`display.c` の公開の関数はこれを呼ぶ | `struct i915_xd { io, vbt, opregion, power, clock, dkl, tc, pps, backlight, dmc, wm, hpd, screen[2], firmware, stage flags }` | 上の全部 |
| `xd-diag` | 診断: register の survey、名前付き register の比較表（Linux の dump との差、§6.4）、run の log の印字、`i915.debug=display` の扱い、最後の手段の停止 | 無し | `xd-io` |

今の 60k 行（Linux 環境を含む）に対し、新しい xd は **1.2〜1.6 万行**の見込み（§6.5）。減る理由: MST・DSC・audio・VRR・FBC・色管理・cursor・scaler・PSR・DC3CO・HDCP・OpRegion の runtime・storm 管理・atomic の global state・"world" の環境・Linux の互換 layer（drm_dev、drmm、vblank worker、notifier）を持たない。

### 3.3 制御の流れ（screen の mode set）

```
plan   : mode (EDID/display.mode/CVT) + sink (DPCD/EDID) + platform (ref clk, cdclk, dbuf) + vbt
         → struct i915_xd_plan { mode, kind, port, pipe, pll_id, link{rate,lanes,bpp,m/n/tu}, pll_words, wm{...}, pps, fb }
         純関数。host 試験で register の語（§6.4 の oracle）を比べる。
enable : power(wells) → pps on → tc lanes → pll → ddi io → dkl dp_mode → [DP: clk_sel, ddi_func(no enable), dp_tp, swing, lanes, buf_ctl, train]
         → wm/dbuf → plane → trans timing/msa/mn → ddi_func enable → trans_conf → [HDMI: swing, lanes, buf_ctl] → backlight
         各 step は `int` を返し、失敗したら「その step まで」を disable の順で戻す（PRM の Notes）。step ごとに xd-io の trace に名前を残す。
disable: 逆順（§2.3/2.4）。TRANS_CONF の off の待ちと DDI の idle を確かめ、確かめられなかった物は「retained」（well と buffer を保持、以後その screen を灯さない: 今の挙動）。
flip   : PLANE_SURF を書く → FLIP_DONE の completion を待つ（timeout 100 ms）→ SURFLIVE で確認。
```

phase（screen の状態）: `OFF → PLANNED → POWERED → LINKED → LIT → (RETAINED)`。enable の step の失敗は phase を戻して disable を走らせる。
screen は 2 つ（resident = GOP の出力、head = 2 つ目）。pipe は resident が A（eDP）か B（GOP が HDMI/DP を灯していた時）、head は B。

### 3.4 資源の所有権と寿命、並行性と lock

- **所有**: `struct i915_xd` は `struct i915_display` の中（device の寿命）。well・PLL・DBUF slice・TC の所有・AUX の power は xd-power/xd-clock/xd-wm/xd-tc の refcount で持ち、screen が enable で取り disable で返す。device の stop は screen を止めてから well を返し、返せなかった物は log して保持（今の「abandoned」）。
- **誰が触るか**: display の register を書くのは **request worker（display window）だけ**（今と同じ: present・head・backlight・control の要求は worker に queue される）。例外は (a) 起動の段（start worker、window の前）、(b) hotplug worker（AUX/GMBUS の probe と TC の所有。screen の register は触らない）、(c) 割り込み（ack と completion だけ）。
- **lock**: `xd.lock`（mutex）が hotplug worker と request worker の間で xd-power/xd-tc/xd-aux の refcount と状態を守る。IRQ context は `device IRQ lock`（既存）の下で pending bit と completion だけ。xd-dkl の window は spinlock（割り込みからは使わない）。`sb_lock`（pcode）は既存の mutex。lock の順: device IRQ lock は最内、`xd.lock` → `sb_lock` → dkl。
- **割り込みと DMA**: display の DMA は plane の scan out（GGTT）と DMC の program（MMIO）だけ。vblank/FLIP_DONE/AUX done/HPD を使う。AUX は割り込み無しの poll でもよい（今は poll。保つ）。
- **失敗と回復**: 各 step の timeout は PRM の値（表 §5.3 T8）。timeout → その screen を disable、再試行は呼ぶ側（present の relight、DP の fallback）。訓練の失敗 → rate/lane を下げて再試行（最低 RBR×1 まで）。TC の partner が消えた → unplugged として window を抜ける。DMC が無い → DC6 無しで続ける。VBT が無い → 既定（eDP on A、PPS の既定、backlight 200 Hz）。

### 3.5 状態遷移（TC port と hotplug の connector）

```
TC port: FREE ──(live: DP-alt/legacy/TBT)──▶ DETECTED ──(connect flow ok)──▶ OWNED ──(links>0)──▶ IN_USE
            ▲                                     │ abort                    │ disconnect / partner gone
            └─────────────────────────────────────┴──────────────────────────┘
connector: ABSENT ──(HPD long / live=1 / boot readout)──▶ PROBING ──(DPCD/EDID ok)──▶ CONNECTED ──(HPD long, live=0)──▶ ABSENT
                                                          │ fail → retry (≤2 回、1 s) → ABSENT
           CONNECTED ──(HPD short)──▶ CONNECTED (link status を確かめ、落ちていれば retrain の要求)
```
generation は CONNECTED になる度と EDID が変わった時に増える（display の topology sequence に反映）。storm の検出と poll への切り替えは**持たない**（§7 の未決 6）。

### 3.6 start/stop/suspend/resume の段（xd-display、`display.c` の公開の関数との対応）

| `display.c` の口（保つ） | 今の中身（Linux の probe の段） | 新しい中身 |
| --- | --- | --- |
| `drv_i915_display_create` | worlds の確保 | `struct i915_xd` を zero にし、io・lock を用意 |
| `drv_i915_display_init_opregion` | OpRegion/VBT/DRAM/帯域 | `xd_opregion_locate` → `xd_vbt_parse` → DRAM の種別（mailbox）を `xd_wm` に |
| `drv_i915_display_init_noirq` | drm_dev、bios、VGA、power map、N0、core init、DMC、mode config… | `xd_takeover_readout`（N0、読むだけ）→ 判断 → `xd_power_init`（init の手順、§2.2）→ `xd_clock_read_cdclk` → `xd_dmc_load`（非同期のまま） |
| `drv_i915_display_irq_prepare` | PCH の判定、割り込みの bind | 同じ（`interrupts.c` は own。入口を `xd_hpd_irq_*` に） |
| `drv_i915_display_init_nogem` | survey、nogem front、setup outputs、readout、sanitize | `xd_diag_survey` → `xd_wm_read_latency` → port の表（VBT）→ `xd_takeover_plan`（GOP の出力の分類だけ。register は書かない） |
| `drv_i915_display_register` | HPD init、IPC、TC/DP-ext/HPD start、driver register | `xd_hpd_start`（pin の enable、boot の readout）、`xd_tc_start`、DC off を返す（takeover が残っていれば保持） |
| `drv_i915_display_resident_deps` | 同左 | screen[0] に resident の output を設定 |
| `drv_i915_display_stop_early` | 最後の手段の停止、HPD stop、TC stop、unregister | `xd_screen_force_off`、`xd_hpd_stop`、`xd_tc_stop`、DC off を取る |
| `suspend_begin/end`、`resume_begin/end` | INIT ref、core uninit、DC9 | `xd_power_hold_dc_off` → `xd_power_uninit` → `xd_power_dc9_enter`；`dc9_leave` → `xd_power_init` → `xd_dmc_reload` → 返す |
| `drv_i915_display_stop_outputs` / `fini` | eDP fini、nogem fini、state fini、DMC fini… | `xd_screen_release`、`xd_dmc_free`、`xd_power_release`（保持した well の log）、map の解除 |

## 4. 外向きの口と境界

### 4.1 保つ口（変えない）

- GPU node の operation（`drv_gpu_ops`、`uapi/gpu-display.h`）: display の query/claim/present/wait/release/refresh/power/events。`display.c` の `drv_i915_display_bind_ops` 以下。
- `display.h` の start/stop/suspend/resume の関数（§3.6 の左の列）と `drv_i915_display_panel`・`_which`・`_events`・`_resident_identity`・`_output_*`。
- `present.h`・`head.h`・`control.h`・`backlight.h`・`scanout.h`・`bootfb.h`（own の file の口）。
- Gen9 の display（`gen9-*.c`、`intel/display-regs-gen9*.h`）: 一切触らない。Gen9 が Gen12 の file から呼ぶ物は 2 つだけ: `drv_i915_adjust_wm_latency()`（`takeover.c`、`gen9-outputs.c:91`）と `drv_i915_n1_console_fb()`（`takeover.c`、`bootfb.c:226`）、および `device.c:1516` の `drv_i915_n0_lite_gen9()`（`takeover.c`）。これらは **Gen9 側（`gen9-outputs.c`・`bootfb.c`）と `display.c` に自前の実装として移す**（p002 の最初の仕事。値は同じ、code は書き直す）。
- boot の語: `i915.debug=display`、`display.mode=WxH[@R]`。log の接頭辞 `i915: `。

### 4.2 動く口（own の file の呼び出し先が xd に変わる所）

own の file は Gen12 の module を **`drv_i915_lcd_*`・`drv_i915_hpd_*`・`drv_i915_dp_ext_*`・`drv_i915_tc_kern_*`・`drv_i915_native_*`** などの名前と、`internal.h` の Linux 由来の型（`i915_lcd_state`・`i915_lcd_modeset_cfg`・`i915_lcd_kernel`・`i915_lcd_kernel_deps`・`i915_native_report`・`i915_hpd_output`・`i915_display_output.state`・`i915_display_head.k/params/cfg`）で呼んでいる。これらは xd の口に置き換える（own の file の **呼び出し側の行を直す**。own の file の構造は変えない）。

| own の file | 今呼ぶ物 | 新しい口（`xd.h`） |
| --- | --- | --- |
| `present.c` | `drv_i915_lcd_kernel_resident_run`、`_modeset_flip_nowait/poll/wait_screen`、`_modeset_retained`、`_show_retained`、`drv_i915_hpd_topology_touch` | `drv_i915_xd_screen_run(display, screen, serve, ctx)`、`_screen_flip_arm/poll/wait`、`_screen_retained`、`drv_i915_xd_hpd_touch` |
| `head.c` | `drv_i915_lcd_modeset_select/prepare/commit_enable/commit_disable/status/plane_disable/plane_released/flip_*`、`_lcd_kernel_locks_init/bind_ops/preflight/fill_cfg`、`_lcd_output_params/cfg/link_fallback` | `drv_i915_xd_screen_plan(display, 1, output, &plan)`、`_screen_light(display, 1, &plan, fb)`、`_screen_dark`、`_screen_flip_*`、`_screen_status`、`_screen_preflight`、`_screen_link_fallback` |
| `output.c` | `drv_i915_lcd_compute_hdmi/dp_ext`、`drv_i915_display_panel_mode/size_mm`（`state.c`）、`drv_i915_dp_ext_probe/sink_copy/link_limits/aux_emit`、`drv_i915_tc_kern_port_of`、`drv_i915_hpd_output/_count/_take/_summary/_probe_connector/_edid_bytes`、`drv_i915_gop_output_read/_name` | `drv_i915_xd_plan_hdmi/_dp/_edp`、`drv_i915_xd_panel_mode/size`、`drv_i915_xd_sink_probe(display, connector, &sink)`、`drv_i915_xd_hpd_connector(display, i, &info)`、`_connector_count`、`_connector_take`、`drv_i915_xd_firmware_output(display, &out)`/`_name` |
| `control.c` | `drv_i915_pipe_frame_read`（`vblank.c`） | `drv_i915_xd_trans_frame(display, pipe)` |
| `backlight.c` | `drv_i915_lcd_modeset_brightness_get/brightness/backlight` | `drv_i915_xd_backlight_get/set/power` |
| `display.c` | §3.6 の全部、`drv_i915_detect_pch`（own）、`drv_i915_adlp_display_step`（`clock.c`）、`drv_i915_power_well_is_enabled`、`drv_i915_opregion_read_data/log`、`drv_i915_native_precheck/log` … | `drv_i915_xd_*` の段（§3.6）、`drv_i915_xd_step(revid)`、`drv_i915_xd_power_well_on`、`drv_i915_xd_firmware_*` |
| `scanout.c`、`bootfb.c` | `drv_i915_n1_console_fb`（takeover） | own に移す（4.1） |
| `interrupts.c` | `drv_i915_hpd_de_irq/pch_irq`、`drv_i915_display_power_is_enabled`、`drv_i915_opregion_gse_entry` | `drv_i915_xd_hpd_irq_north(display, iir)`、`_irq_south(display, iir)`、`drv_i915_xd_power_pipe_on(display, pipe)`、opregion の GSE は捨てる（runtime 無し） |
| `diagnostics.c`（Gen12、書き直し） | — | `xd-diag` に |
| `device.c` | `drv_i915_n0_lite_gen9`、`drv_i915_lcd_debug_set` | Gen9 の own、`drv_i915_xd_diag_set` |
| `gen9-outputs.c` | `drv_i915_adjust_wm_latency` | Gen9 の own（4.1） |

`struct i915_display` から消える field: `drm_dev`、`vbt_state`、`vga_client`、`power_domains`、`pmdemand`、`cdclk`、`pwc`、`edp_dev`、`dcore`、`dmc_dev`、`dstate`、`nogem`、`dprobe`、`dram_info`、`bw_state`、`dmc_wq/modeset_wq/flip_wq`、`opd/p2_opd`、`n0/n0_mmio`、`gop`、`dkl`、`tck`、`rctx/rlcd`、`lk`、`lcdb_locks`、`resident_run_env/rep`、`fill_cfg_pn`、`show_*`、`i915_vbt_buf`、`g_dmc_arena`、`dc_trace`、`read_opregion_*`、`chain_*`、`g_vga_io`、`*_world`、`drm_inited` … の段の flag。残る field: `device`、`irq`/`irq_vblank`（own）、`output`/`gop_output`（型は `struct i915_xd_output` に）、`backlight`、`refresh`、`rd`、`window`、`head`（`k/params/cfg` → `struct i915_xd_plan`）、`resident_buf`、`resident_serve*`、`resident_front/up`、`pch`、`absent`、`hpd_started`、新しい `struct i915_xd xd`。

### 4.3 `xd.h` の型（境界の型、Linux の型を置き換える）

```c
struct i915_xd_mode { uint32_t clock_khz; uint16_t h[4], v[4]; int hsync_pos, vsync_pos; uint16_t width_mm, height_mm; uint8_t bpc; };
struct i915_xd_sink { uint8_t kind; /* EDP, DP, HDMI */ uint8_t dpcd_rev, max_lanes; uint32_t max_rate_khz; uint8_t edp_caps[4]; uint8_t edid[256]; unsigned edid_len; struct i915_xd_edid parsed; };
struct i915_xd_plan { struct i915_xd_mode mode; uint8_t kind, port, pipe, pll_id, tc_port; struct { uint32_t rate_khz; uint8_t lanes, bpp; uint32_t data_m, data_n, link_m, link_n, tu; } link;
                      uint32_t pll_words[10]; struct { uint16_t linetime; uint32_t wm[8], trans, sagv, buf_cfg; uint8_t slices; } wm; uint32_t fb_surf, fb_stride, fb_w, fb_h; };
struct i915_xd_output { uint8_t kind; int none, has_connector; unsigned connector, tc_port; int port; struct i915_xd_plan plan; struct i915_xd_sink sink; const char *mode_source; };
struct i915_xd_connector_info { uint8_t kind; int port, connected; uint64_t generation; const char *name; uint32_t w, h, refresh_mhz, w_mm, h_mm; };
struct i915_xd_firmware_output { uint8_t kind; /* NONE, EDP, HDMI, DP_TC, OTHER */ unsigned pipe, pipes; int port; };
```
（host 試験が register の語を比べるのは `struct i915_xd_plan` と xd-io の trace。）

### 4.4 変えない振る舞い（受け入れの基準になる物）

- 起動: GOP が eDP を灯した 5330 で、display を使う判断（N0 の規則）、GOP の出力の分類、最初の present で takeover して panel が灯る。VT-d が有効なら使わない。
- present/flip の性能: vblank 同期の flip、FLIP_DONE の待ち 100 ms、frame counter の確認（起動時 10 frame/1 s、定常 30 frame/1.5 s）。
- backlight device（`/dev/backlight/backlight0`）の get/set/power、range は PWM の周期（96000）。
- hotplug: HDMI（DDI B）と USB-C DP（TC1/TC2）の plug/unplug で topology の event、head の claim で 2 画面、unplug で window を抜ける、TC の partner gone。
- suspend/resume: DC9 を通って戻り、次の present で灯る。
- log: `i915: N0: …`、`i915: takeover: …`、`i915: gen9 …`（Gen9 は無関係）、試験の一覧が読む文言は §6.4 で列挙。

## 5. データの扱い（register の offset・bit・表）

### 5.1 形

- header は zedBSD の著作権 header と file の説明だけ。各 block（1 register 群、1 表）の直前に複数行の comment:
  ```c
  /*
   * The combo PHY DPLL words for the DisplayPort link rates at a 19.2 or
   * 38.4 MHz reference: DCO integer and fraction, and the P, K and Q dividers.
   *
   * Values referenced: IHD-OS-TGL-Vol 12-12.21 "DisplayPort Mode PLL Values"
   * (the 19.2 or 38.4 MHz rows), cross-checked against Linux v6.8.12
   * drivers/gpu/drm/i915/display/intel_dpll_mgr.c (icl_dp_combo_pll_19_2MHz_values).
   */
  ```
  PRM の値は PRM を第一の出典、Linux は照合（「cross-checked」）。PRM に無い値は「Values referenced: Linux v6.8.12 <path> (<name>)」だけ。
- 名前は独自: `I915_XD_<REG>` / `I915_XD_<REG>_<FIELD>`（PRM の register の名前を大文字で。Linux の macro 名（`ICL_DPCLKA_CFGCR0`、`_MMIO_PIPE`）は使わない）。instance は関数 `i915_xd_reg_pipe(base, pipe)` ではなく `#define I915_XD_TRANS_HTOTAL(trans) (0x60000 + 0x1000 * (trans))` の形（offset の算術はデータ）。
- 表は `static const struct` の配列。配列の要素の comment に PRM の行の意味（rate、voltage level）を書く。表の構造（struct の field の並び）は自分で決める（Linux の struct を写さない）。
- VBT・OpRegion・DPCD・EDID の layout は **offset と幅の表**（`#define I915_XD_VBT_BDB_HEADER_SIGNATURE 0`、`I915_XD_VBT_CHILD_DVO_PORT 2` …）で持ち、packed struct は作らない（WS244 の棚卸しが「struct の配置は構造の写し」と見た物を避ける）。読みは `i915_xd_get8/16/32(bytes, off)`。
- 試験の fixture（5330 の EDID・DPCD・register の語）は今の `tests/display/dp-fixture-latitude5330.h` と `plan/ws031/display-ref/`、`plan/ws051/tests/m3-5330-20261007/` を使う（出典は file の comment に既に書かれている）。

### 5.2 register の出典の対応（grep で確かめた物）

| register の群 | 第一の出典 | 照合 |
| --- | --- | --- |
| pipe・plane・transcoder・DDI_BUF_CTL・DP_TP_CTL/STATUS・TRANS_DDI_FUNC_CTL/2・TRANS_CLK_SEL・PIPE_SRCSZ・PIPE_MISC・PLANE_*・CUR_*・WM_LINETIME・PIPE_FRMCNT/SCANLINE | TGL Vol 2c（PIPE_MISC は ACM Vol 2c） | `i915_reg.h` |
| DPLL_ENABLE/CFGCR0/CFGCR1/SSC・DPCLKA_CFGCR0・DDI_CLK_SEL・CDCLK_CTL/PLL_ENABLE・DSSM | TGL Vol 2c | `i915_reg.h` |
| CDCLK_SQUASH_CTL、PORTTC PLL の enable の offset（ADL-P）、DPLL4 | ACM Vol 2c / Linux | `i915_reg.h`（`ADLP_PORTTC_PLL_ENABLE`、`TGL_DPLL_ENABLE`） |
| PWR_WELL_CTL/CTL_AUX/CTL_DDI・FUSE_STATUS・DC_STATE_EN・DBUF_CTL・DISPLAY_INT_CTL・DE_PIPE/PORT/MISC/HPD の割り込み | TGL Vol 2c | `i915_reg.h` |
| MBUS_CTL、PIPE_MBUS_DBOX_CTL、BW_BUDDY、GEN11_CHICKEN_DCPR_2、XELPD_DISPLAY_ERR_FATAL_MASK、CLKGATE_DIS_PSL_EXT、PIPE_ARB_CTL、PIPEDMC_CONTROL、DC_STATE_DEBUG、TRANS_SET_CONTEXT_LATENCY、TRANS_CMTG_CHICKEN | Linux（PRM に無い、または Vol 12 の本文にだけ名前） | — |
| PORT_COMP/CL/TX/PCS（combo PHY）、PHY_MISC | TGL Vol 2c | `intel_combo_phy_regs.h` |
| DKL_*、HIP_INDEX_REG0/1、PORT_TX_DFLEX*（FIA） | TGL Vol 2c | `intel_dkl_phy_regs.h`、`intel_tc.c` は読まない（GPL でない MIT だが code なので） |
| TCSS_DDI_STATUS | ACM Vol 2c | `i915_reg.h` |
| DDI_AUX_CTL/DATA | TGL Vol 2c | `intel_dp_aux_regs.h` |
| SRD_CTL/STATUS、PSR2_CTL、TRANS_VRR_* | TGL Vol 2c | `intel_psr_regs.h` |
| GMBUS0–5、PP_*、SBLC_PWM_*、SHOTPLUG_CTL_DDI/TC、SHPD_FILTER_CNT、RAWCLK_FREQ、NDE_RSTWRN_OPT、SOUTH_CHICKEN1 | ACM Vol 2c | `intel_gmbus_regs.h`、`intel_pps_regs.h`、`intel_backlight_regs.h`、`i915_reg.h` |
| SDEISR/IMR/IIR/IER（PCH の割り込み）、TC/TBT の HOTPLUG_CTL の offset | ACM Vol 2c（south interrupt）/ TGL Vol 2c の HOTPLUG_CTL | `i915_reg.h` |
| DMC の program storage の base（main 0x8F000、ADL-P の pipe DMC）、DMC_EVT_CTL/HTP | Linux `intel_dmc_regs.h` | — |
| GT driver mailbox（GEN6_PCODE_MAILBOX/DATA/DATA1）と command（latency 0x6、pipe count 0x7、QGV 0x10d、SAGV 0xe、TCCOLD 0x26、voltage 0x7） | TGL Vol 2c（GTDRIVER_MAILBOX_INTERFACE）、Vol 12 の手順 | `intel_gt_regs.h`/`i915_reg.h`（MIT） |

### 5.3 持つ表（名前は独自、出典を付ける）

| 表 | 内容 | 第一の出典 | 照合（Linux v6.8.12） |
| --- | --- | --- | --- |
| T1 combo の swing | DP HBR / HBR2-3 / eDP の 10 行（swing select、n scalar、cursor coeff、post cursor 1/2）、HDMI の 7 行。**ADL-P の表** | TGL Vol 12 "Voltage Swing Programming" の表（TGL）／ADL-P は Linux | `intel_ddi_buf_trans.c` の `adlp_combo_phy_trans_*`、`tgl_combo_phy_trans_*`、`icl_combo_phy_trans_hdmi` |
| T2 DKL の swing | DP の 10 行、HDMI の 5 行 | TGL Vol 12 "DP Voltage Swing Table"、"HDMI Voltage Swing Table" | `adlp_dkl_phy_trans_*`、`tgl_dkl_phy_trans_*` |
| T3 combo DPLL の DP の値 | 8 rate × 2 参照 | TGL Vol 12 "DisplayPort Mode PLL Values" | `icl_dp_combo_pll_*_values` |
| T4 DKL PLL の DP の値 | 1.62/2.7/5.4/8.1 の rate 依存の register | TGL Vol 12 "TypeC PHY PLL Programming for Displayport" | `intel_dpll_mgr.c` の `icl_calc_mg_pll_state` の定数 |
| T5 CDCLK | 38.4 MHz（と 19.2/24）の PLL ratio・CD2X・decimal、squash の window | TGL Vol 12 "CDCLK PLL Ratio and Divider Programming"、ACM Vol 12 の squash の表 | `adlp_cdclk_table` |
| T6 watermark | latency の調整（+3 µs、display 13 の 6 level）、transition WM の式、SAGV の加算、DBUF の割り当ての表（4 slice、pipe の組） | TGL Vol 12 "Watermark Algorithm"、"DBUF Allocation" の表 | `skl_watermark.c`（式）、`adlp_allowed_dbufs` |
| T7 procmon | 0.85/0.95/1.05 V × dot-0/1 の COMP_DW1/9/10 | TGL Vol 12 "Procmon Reference Values" | `icl_procmon_values` |
| T8 timeout | PG0/PG1 fuse 20 µs、PW state 30 µs、DBUF 10 µs、DPLL lock 50 µs（Linux は 1 ms を使う: 大きい方を採る）、DKL lock 900 µs、DDI idle 1 ms/TC 3 ms、TRANS_CONF off 2 frame、AUX 4000 µs、TCCOLD 200/600 µs、uc_health 10 µs、AUX USBC の well 500 ms（Linux） | TGL Vol 12 の各手順 | 今の code の値 |
| W1 power well の表 | ADL-P の well（名前、CTL の instance、index、fuse の PG、domain の集合）、AUX channel → domain | TGL Vol 12 "Functions Within Each Well"（TGL の PG1-5）；ADL-P の index と pipe ごとの well は Linux | `intel_display_power_map.c`（`xelpd_power_wells`、`xelpd_pwdoms_*`） |
| W2 BW_BUDDY の page mask | DRAM の種別と channel 数ごと | TGL Vol 12 "Bandwidth Buddy Registers" の表 | `intel_display_power.c` の `tgl_buddy_page_masks` |
| H1 HPD の pin | DDI A/B → SHOTPLUG_CTL_DDI の nibble、TC1-4 → SHOTPLUG_CTL_TC と TC_HOTPLUG_CTL の nibble、SDE の bit（DDI A/B = 16/17、TC1-4 = 24..27）、DE_HPD の bit（TC = 16+n、TBT = n） | TGL Vol 12 "Hot Plug Detection"、ACM Vol 12 "Hot Plug Pins"。**注意**: ACM Vol 2c の SDE の bit の表（DG2）は TC port 1 を bit 25 に置き ADL-P と違う。ADL-P の bit は `i915_reg.h` の値を採り、m3 の `range-0xC4000.txt`（5330 の実測）で確かめる | `i915_reg.h` の `SDE_DDI_HOTPLUG_ICP`/`SDE_TC_HOTPLUG_ICP`/`GEN11_DE_HPD_*`（MIT）。`intel_hotplug_irq.c`（GPL）は読まない |
| V1 VBT の block の layout | block id、field の offset と幅、version の境 | — | `intel_vbt_defs.h`（MIT） |
| V2 DVO port → DDI | ADL-P の対応 | — | `intel_bios.c`（`dvo_port_to_port`、MIT）の値 |
| O1 OpRegion の layout | header、mailbox の offset、RVDA/RVDS | — | `intel_opregion.c`（MIT）の define |
| D1 DPCD の address と bit | 0x000..、0x100..、0x200..、0x600、0x700..、0xF0000 | VESA DP 1.4（会員のみ） | `drm_dp.h`（MIT） |
| E1 EDID の offset | base block、CEA の tag | VESA E-EDID 1.4（公開） | 今の `output.c`（own） |

### 5.4 PRM の文の扱い

PRM の手順を comment に写さない。step の comment は「何のためにこの register を書くか」を自分の言葉で書き、必要なら「Procedure referenced: IHD-OS-TGL-Vol 12-12.21 "Sequences for DisplayPort" (Enable Sequence)」を block の直前に付ける。Linux の関数は「Procedure referenced:」に書かない（手順の出典は PRM。Linux は値の照合だけ）。

## 6. 書き直しの順序、Phase の分割、切り替え、試験、見積もり

### 6.1 切り替えの方法（選んだ案と選ばなかった案）

**選んだ案: 新しい tree（`xd-*`）を並べて作り、seam で一度に切り替え、最後に旧 code を消す。**

- 旧 code（Linux 由来の 40 file と `internal.h`）は、p00N の切り替えまで**そのまま build に残す**。own の file は p008 で xd の口（§4.2）に付け替え、それまでは旧 code を呼ぶ。
  xd の module は p002〜p007 の間は **host 試験だけ**で育てる（kernel には link するが呼ばれない。`AMD64_I915_SOURCES` に足すのは p008）。
- p008 で `display.c` の段（§3.6）を xd に向け、boot の語 **`i915.display=old`** で旧の段に戻れるようにする（旧 code は p008〜p010 の間だけ残る）。5330 で新しい段が「起動・GOP の引き継ぎ・eDP・backlight・suspend の戻り」を満たすのをユーザーが確かめたら（§6.4 の一覧）、p011 で旧 code と `i915.display=old` を消す。
- 選ばなかった案 A「群ごとに旧 file を置き換える（edid → vbt → aux …）」: 旧 code の型（`intel_dp`・`intel_crtc_state`・`i915_lcd_emit` の hook）が群の境界をまたいで深く結ばれているので、1 群だけ新しくすると adapter（Linux の型 ↔ xd の型）を書く事になり、その adapter 自体が Linux の構造を写す。群ごとの試験は host 試験で同じ事ができる。
- 選ばなかった案 B「own の file の seam を先に xd の口に変え、xd の口を旧 code への adapter で実装する（strangler）」: own の file の付け替えを早くできるが、adapter が 1 つ増え、旧 code の state（power の refcount、world）と新しい module の state を 1 つの display で混ぜる段階が生まれる。power・PLL・DBUF の所有が二重になると実機でしか見つからない不整合を生む。
- 旧 code を残す間の cost: build 時間と `vmunix` の大きさ（display の旧 code は ~1.5 MB の object）。p008 から p011 の間だけ。

### 6.2 順序と Phase（p001 の案 edid → vbt → aux・gmbus → backlight・panel → power・dmc → clock・PLL → watermark → pipe・plane・ddi・dp → takeover・hotplug を、依存と試験の道で組み直した物）

| Phase | 内容 | 受け入れ条件（全部 host で確かめられる物は host、実機の物は 5330） | 依存 | LW |
| --- | --- | --- | --- | ---: |
| p002 | **土台**: `intel/display-regs-xd.h`・`-south.h`・`display-tables-xd.h`（§5 の形で、p003〜p007 が使う register と表を全部）、`xd.h`（§4.3 の型）、`xd-io`（read/write/rmw/poll/trace）、`xd-pcode`、host の試験の枠（`plan/ws245/tests/display-xd-host-lib.sh`: xd の file + fake の register の model + ASan/UBSan）、Gen9 と device.c の 3 つの関数の own への移設（§4.1） | (a) 全 register の offset が Linux の値と一致する表の試験（`tests/display/host-xd-regs-test.c`: 旧 `intel/*.h` の define と比べる。旧 header は試験だけが include する）；(b) xd-io の poll/trace の試験；(c) build warning 0、`git diff --check`；(d) 移設した 3 関数の既存の試験（host-gen9-outputs、host-bootfb、host-native-decide）が通る | — | 3 |
| p003 | **VBT・OpRegion・EDID**: `xd-opregion`、`xd-vbt`、`xd-edid` | 5330 の OpRegion/VBT（`display-ref/` の bytes。無ければ i915_vbt 8704 byte の dump を T1/ユーザーに頼む: §7）と EDID の fixture で、今の parser と同じ値（port の表、panel の DTD、PPS の T、backlight、eDP の bpp/rate/lanes、DDC/AUX）を返す host 試験（`host-xd-vbt-test.c`）；VBT 無しの既定；壊れた VBT（size・offset・block の overrun）の拒否 | p002 | 3 |
| p004 | **AUX・GMBUS**: `xd-aux`、`xd-gmbus`、DPCD の helper | `dp-fake-hw.c` の AUX の model（oracle として残す、bind だけ adapter）で native read/write・I2C-over-AUX・DEFER/NACK/timeout/receive error の再試行が今と同じ回数と結果；`hpd-model.c` の GMBUS で 128/256 byte の EDID 読みと NAK/timeout | p002 | 2 |
| p005 | **power・DMC・clock**: `xd-power`（well の表 W1、init/uninit、DC、DC9、pipe count）、`xd-dmc`、`xd-clock`（CDCLK の読み、combo DPLL、DKL PLL の語、map） | host: (a) init の手順の register の列が §2.2 の順（fake の model に request/state/fuse の挙動を持たせる）；(b) well の refcount の get/put と失敗の巻き戻し；(c) DMC package の parse（`adlp_dmc.bin` の実物で main/pipe A/B を選ぶ）と MMIO pair の範囲検査；(d) DPLL の語: HBR@38.4 → 0x00e001a5/0x88（`display-ref`）、m3 の TC PLL の語（`host-dpext-link.c`・`host-dkl-test.c` の期待値を移す）；(e) CDCLK の読み: m3 の `i915_cdclk_info.txt` の値 | p002 | 4 |
| p006 | **PHY・DP・panel**: `xd-phy`（combo の init、swing の表 T1、lanes、DDI_BUF_CTL）、`xd-dkl`、`xd-dp`（link の方針、訓練、DP_TP）、`xd-pps`、`xd-backlight` | host: (a) `lcd-fake-hw.c` の訓練の model（CR/EQ の挙動、oracle）で eDP HBR×2 の訓練が今と同じ DPCD の列で通り、"CR never"/"EQ never" で fallback；(b) swing の register の語が Linux の dump（`regs-selected.txt` の PORT_TX_DW*）と一致；(c) PPS の T の計算と on/off の順、VDD；(d) backlight の level ↔ duty、FREQ 0x17700 | p003〜p005 | 4 |
| p007 | **transcoder・plane・watermark**: `xd-trans`、`xd-plane`、`xd-wm` | host: (a) eDP 1920×1080@60 の timing・M/N・MSA・DDI_FUNC_CTL（0x8a210002）・PIPE_SRCSZ・PLANE_* の語が `regs-selected.txt` と一致（`host-lcd-test.c` の期待値を移す）；(b) WM の level（PLANE_WM0 0x80004010、BUF_CFG 0x0fdb0000）と LINETIME；(c) DBUF の slice の選択（pipe A のみ / A+B）；(d) flip の arm と FLIP_DONE の model | p002、p005 | 4 |
| p008 | **screen・takeover・display の段・seam**: `xd-screen`（eDP の plan/enable/disable/flip/run）、`xd-takeover`（N0 の読みと規則、GOP の出力の分類、takeover の disable）、`xd-display`（§3.6）、`xd-diag`、own の file の付け替え（§4.2）、`i915.display=old` | host: (a) 一画面の mode set の register の列が §2.3 の順（`host-lcd-modeset-test.c` の family A の順序の検査を移す: PPS → PLL → DPCLKA → DPCD 0x102 → M/N → TRANS_CONF）；(b) N0 の判断の規則（`host-native-decide-test.c` と同じ 14 検査）；(c) takeover の disable の順。**5330（ユーザー）**: 起動して GOP の画面から desktop に切り替わる、backlight の get/set、`i915.display=old` でも起動する。QEMU: Venus の image の起動（boot-test.sh）が今までどおり | p003〜p007 | 5 |
| p009 | **hotplug・Type-C・外部 DP・HDMI**: `xd-hpd`、`xd-tc`、`xd-hdmi`、screen の DP-TC/HDMI の plan と enable、head の付け替え | host: (a) `hpd-model.c` の register で HPD の decode/plug/unplug/retry/EDID の変化（`hpd-ktest` の case を host に移す）；(b) TC の connect/disconnect/abort の順と巻き戻し（`host-tc.c` の検査を移す）；(c) DP-ext の probe と fallback（`host-dpext.c`）；(d) HDMI の mode の選択（`output.c` の pick）。**5330（ユーザー）**: HDMI の plug で 2 画面、USB-C DP（TC2）の plug で 2 画面、unplug で戻る | p008 | 5 |
| p010 | **suspend の戻り・残り**: DC9 の enter/leave、DMC の載せ直し、backlight device と control（refresh・power）の確認、診断の boot の語、log の文言の一覧 | host: DC9 の手順の register の列。**5330（ユーザー）**: suspend → resume で panel が戻る、backlight が戻る | p008 | 2 |
| p011 | **旧 code の削除と出典の監査**: 旧 40 file と `internal.h` の Linux 部分、`intel/` の Gen12 だけが使う header（WS244 と分担: vbt-defs/ref/connector/wm/dp/trans/mreg/plane/phy/dkl/clock/psr/opregion/hotplug*/power*）、`i915.display=old`、旧 host 試験・ktest の整理（§6.4）、`Derived from the Linux kernel` と Linux の MIT の表示を Gen12 の file から消す、`plan/ws244/tests/inventory.py` で C/S = 0 を確かめる | `inventory.py` の Gen12 の file の C と S の行が 0、file の先頭に出典が無い（grep）、block ごとの "Value(s) referenced:"/"Procedure referenced:" の形（複数行 comment）の grep、build warning 0、Venus の image の boot-test | p010、ユーザーの確認 | 2 |
| p012 | **規約の全文の見直し**（conformance-reviewer） | `plan/coding-style.md` の全文の checklist、`git diff --check`、build | p011 | 2 |

LW の合計 **36**（±40 %。1 LW ≈ 担当 1 人の 1〜2 時間。8 LW ≈ 2〜3 h の実時間換算（memory）なら 36 LW ≈ 10〜14 h の実時間）。p005〜p007 は互いに依存しないので、担当が空けば並べられる（host 試験だけなので QEMU を塞がない）。

Phase ごとの出力（p002〜p007）は「新しい file + host 試験」だけで、kernel の振る舞いは変わらない。p008 が最初に kernel の振る舞いを変える Phase（既定を `xd` にするか `old` にするかは §7 の未決 1）。

### 6.3 試験の道

- **host**（各 Phase、`plan/ws245/tests/run-xd-host-tests.sh`、ASan/UBSan、数秒）: fake の register の model に対して「register の語」「書き込みの順」「待ちと timeout」「失敗の巻き戻し」を比べる。oracle は (a) 5330 の Linux の dump（`display-ref/regs-selected.txt`、m3 の `range-0x*.txt`、`intel_reg-dump.txt`）の語、(b) PRM の手順の順、(c) 今の試験の期待値（移す物は §6.4）。
  fake の model は今の `dp-fake-hw.c`・`lcd-fake-hw.c`・`hpd-model.c`（own の試験 code、register の挙動の model）を **oracle として残し**、bind する関数だけを xd の `struct i915_xd_io` の hook（read/write を差し替えられる）に変える。
- **QEMU**: QEMU には i915 が無い（VFIO の passthrough の手順は `plan/ws031/tests/vfio-passthrough-procedure.md` にあるが 5330 を host にする必要があり、ユーザーの機械なので使わない）。QEMU で見る物は「i915 の無い image（Venus）の起動が壊れていない」だけ（T1 に boot-test.sh を依頼、p008 と p011）。
- **5330 の実機**（ユーザーの確かめ、試験の一覧に載せる物。QEMU の証拠と分けて書く）:
  1. p008: 起動 → GOP の画面 → desktop（takeover）。`i915: N0:` と `i915: takeover:` の log。backlight の get/set。`i915.display=old` での起動。
  2. p009: HDMI の plug/unplug（head の 2 画面）、USB-C の DP の plug/unplug（TC2、2 画面）、unplug 中の present が止まらない。
  3. p010: suspend → resume → desktop が戻る、backlight の値が戻る。
  4. p011: 旧 code を消した後の同じ 3 つ。
- 負荷・耐久（hotplug の繰り返し、suspend の繰り返し）は別枠（ユーザーに確かめてから）。

### 6.4 今の試験の扱い（2026-10-06 の基準: 変更に追従する時に、参照されていない物は消す）

| 今の試験 | 扱い |
| --- | --- |
| `tests/display/host-dp-test.c`、`host-lcd-test.c`、`host-lcd-modeset-test.c`、`host-capture-test.c` | 走らせる script が無い（`plan/ws031/tests/run-*-host-test.sh` は削除済み、README だけが名指す）。期待値（register の語、順序）を p006〜p008 の xd の host 試験に**移し**、file は p011 で消す（path を Q1 へ） |
| `tests/display/dp-fake-hw.c`、`lcd-fake-hw.c`、`hpd-model.c` | 残す（oracle）。bind の関数だけ xd の hook に合わせる（p004/p006/p009） |
| `tests/display/edp-ktest.c`、`edp-sync-ktest.c`、`lcd-modeset-ktest.c`、`lcd-show-ktest.c`、`lcdg-ktest.c`、`opregion-ktest.c`、`hpd-ktest.c`（kernel の test build） | Linux の型に依存。p011 で消す。hpd-ktest の case は p009 の host 試験に移す。kernel の test build（`I915_TESTS=y` の display の set）は xd の host 試験で置き換え、`vmunix.mk` の display の ktest の行を消す |
| `tests/display/lcd-run.c`、`lcd-flip.c`、`lcd-gpu.c`、`lcd-opregion.c`、`hdmi-output.c`、`hdmi-hotplug.c`、`aux.c`（実機の scenario） | ユーザーの実機の確かめに置き換わる（§6.3）。p011 で消す |
| `plan/ws051/tests/host-tc.c`、`host-tc-tables.c`、`host-dpext.c`、`host-dpext-link.c`、`host-dkl-test.c`、`host-power-map.c`、`host-vbt-pll.c`、`host-n1-tc.c`、`host-unplug-gop.c`、`plan/bugs/BUG-266/tests/`、`BUG-268/tests/` | 期待値（TC PLL の語、DKL の語、power の表、fallback の列）を p005/p006/p009 に移す。file は WS051 の物なので Q1 に報告して整理（WS051 が complete なら消す候補） |
| `host-native-decide-test.c`、`host-bootfb-test.c`、`host-gen9-*` | 残す（own・Gen9）。native-decide は p008 で xd-takeover に付け替える |
| `plan/ws031/display-ref/`、`plan/ws051/tests/m3-5330-20261007/` | 残す（oracle のデータ） |

### 6.5 大きさの見積もり（新しい xd）

| module | 行（概算） | 根拠 |
| --- | --- | --- |
| regs/tables の header | 2500 | 今の `intel/` の Gen12 の define のうち使う物（~700 register・bit）と表 T1〜T8/W1/V1/O1/D1 |
| io・pcode・diag | 900 | |
| vbt・opregion・edid | 1800 | 今の vbt.c の 4978 行は 16 block の全 field と version の分岐。使う 8 block に絞る |
| aux・gmbus | 900 | |
| power・dmc・clock | 2200 | |
| phy・dkl・dp・pps・backlight | 2400 | |
| trans・plane・wm | 1600 | |
| tc・hpd・hdmi | 1800 | |
| screen・takeover・display | 2200 | |
| 計 | ~14300 | 今の 60k（Linux 環境）の 1/4 |

## 7. 危険と未決の判断（ユーザーに聞く点）

| # | 点 | 既定（この設計） | 聞く理由 |
| --- | --- | --- | --- |
| 1 | p008 の切り替えの既定。`i915.display=xd` を既定にして `old` を逃げ道にするか、`old` を既定にして `xd` を試すか | **xd を既定**（5330 で確かめるまでは commit を merge しない運用も可） | 5330 はユーザーの日常の機械。起動できない版を main に入れない運用との兼ね合い |
| 2 | **Tiger Lake（display 12、Latitude 5320）** を xd で支えるか | **支えない**（ADL-P/display 13 だけ。TGL の差: PG3-5 の well、2 slice の DBUF、cdclk の表、MG PLL の enable、PSR の差）。`device-info` が display 12 を返す node は「display なし（bootfb）」にする | 今の code は display 12 の分岐を持つが実機で確かめた記録が無い。支えるなら表（W1、T5）と分岐が増え、試験の道が無い |
| 3 | HDMI を DVI mode（infoframe・scrambling・audio 無し、≤ 340 MHz）のままにするか | **そのまま**（今の振る舞いを保つのが完了の条件） | HDMI 2.0 の 4K60 や audio は別の WS |
| 4 | Type-C の所有の取り方: PRM（TGL）の DFLEXDPPMS/DFLEXDPCSSS の handshake か、Linux の display 13 の DDI_BUF_CTL の ownership bit + TCSS_DDI_STATUS か | **display 13 の取り方**（今の code と同じ。5330 で動いている事実がある）。DFLEX の lane・pin の読みは PRM どおり | PRM に ADL-P の TC の章が無い。TGL の flow を ADL-P に当てると IOM の handshake が合わない恐れ |
| 5 | cursor plane と scaler を持たない（DDB は plane 1 に全部） | **持たない** | Keiland は software cursor。Linux の DDB の割り当て（cursor 36 block）と register の語が変わるので、oracle の比較は plane 1 の語だけにする |
| 6 | hotplug の storm 検出と poll への切り替え、短い pulse の再訓練 | storm は**持たない**。短い pulse は link status を確かめ、落ちていれば retrain の要求（今と同じ） | 2 分の再有効化や poll の thread は複雑さの割に 5330 で必要になった記録が無い |
| 7 | OpRegion の runtime（ACPI の notify、ASLE の backlight 要求、DIDL/CADL）と ACPI の backlight の key | **持たない**（今も持たない） | 今も `VBT_ONLY`。brightness の key は Keiland が backlight device で扱う |
| 8 | PSR・DC3CO・DC6 の扱い | PSR は off のまま。DC6 は DMC が載った時に許す（今と同じ）。DC3CO は持たない | 5330 の panel は PSR 無し。DC6 は今の挙動を保つ |
| 9 | CDCLK の変更 | 持たない（GOP の値を使い、入らない mode は拒む） | 5330 の GOP は 1920×1080 の panel に足りる CDCLK を設定する。4K の HDMI は別 WS |
| 10 | VBT・OpRegion の layout の出典が Linux（MIT）だけである事 | guardrail の「値の引用」で可 | 公開の仕様書が無い。VBT の block の構造を "data" と見る解釈の確認 |
| 11 | PRM の文の引用の範囲（comment に手順を要約する事は可か） | 自分の言葉で書き、"Procedure referenced:" で章を指す | 「No rights are granted to create modifications or derivatives of this document」 |
| 12 | 5330 の VBT の生の bytes が `display-ref/` に無い（`i915_vbt bytes=8704` と `vbt-decode.txt` のみ）。p003 の fixture に要る | ユーザーに `/sys/kernel/debug/dri/0/i915_vbt` の取得を頼む（試験の一覧）。無ければ `vbt-decode.txt` の値で検査 | fixture 無しでは parser の host 試験が弱い |
| 13 | 旧 code を p008〜p011 の間 tree に残す事（licence の表示も残る） | 残す（§6.1） | WS244 の licence の整理との順序 |
| 14 | eDP 1.4 の link rate の表（DPCD 0x010）を新しく支える事 | 支える（小さい） | 今の code は拒む。5330 では使われない |

危険:
- **ADL-P の PRM が無い**: TGL の手順 + ACM の display 13 の差分 + Linux の値 + 5330 の dump で組み立てる。手順の違い（TC の handshake、pipe count の mailbox、PIPE_MISC2、DBUF の 4 slice）は Linux の値で埋める。実機でしか確かめられない点は p008/p009 のユーザーの確かめに集中する。
- **GOP の引き継ぎ**: takeover の disable の順を間違えると画面が消えたまま戻らない。p008 の host 試験で順序を固定し、実機では `i915.display=old` が逃げ道。
- **Type-C**: TCCOLD と IOM の handshake は実機でしか試せない。p009 の実機の確かめが要る。
- **DMC**: 載せ方を間違えると DC6 で display が止まる。p005 の parse の試験 + p010 の suspend の実機。
- **試験の資産の移行**: 旧試験の期待値を落とすと退行に気づかない。§6.4 の表で移す物を名指した。
- **LW の超過**: p008 と p009 が大きい。p008 は eDP だけに絞り、HDMI/DP は p009 に分けた。

## 8. 敵対的レビューの要約

2026-10-16 P2: design-reviewer を §0〜§7 に対して起動した（対象の節、根拠の file と PRM の章、blocker/major/minor の分類を依頼）が、q991 の run が終わる時点で報告は届いていない。**レビューは未完**。届いた指摘の blocker・major を直すか、直さない理由を書き、この節に要約する作業が残る（Q1 が P2 を再開するか、別の担当が行う）。
