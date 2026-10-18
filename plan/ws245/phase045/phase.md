<!-- awesome-plan project=zedbsd record=ws245-p045 -->

# ws245-p045: display 11（Ice Lake）

Status: in-progress（2026-10-17 P6、q1236-i02: 下の「この attempt で作った物」は実装・host 試験・build 済み。受け入れの「eDP（transcoder EDP）・HDMI（combo B）・DP-TC（MG）の**列**」は、enable/disable の順を組む xd-output（P1 の p050b・p050c）に display 11 の分岐が入るまで作れないので残る。実機は無い（「未確認」））
Parent: [WS245](../ws.md)
Queue: q1236-i02（P6。P1 の p050b〜d と並行: 2026-10-16 ユーザー「P1,P3の処理で、並列にオフロードできる実装があれば、それぞれ1エージェントずつ追加してほしいです。…並列な実装は完璧でなくてもよく、あとでマージするときに整合が必要でもOKです。」）。q1236-i01 は p040・p044 の後にコンテキストの都合で未着手のまま返した
Branch: agent/p6-ws245（p044 の aa14ff623 の上: 711f64b33、31cd4eaaf）
設計: [design.md](../design.md) §6.2 の p045 の行（改訂 3a の p014 の範囲）、§1.1 の表 P1 の ICL の行、§1.2 の表 P2 の display 11 の列、§2.6（MG PLL）、§2.8（流れ B の ICL、MG PHY）、§3.1（`device.c` に ICL の ID を足さない、§7 #30）

## 受け入れ（design §6.2）

ICL の記述で eDP（transcoder EDP）・HDMI（combo B）・DP-TC（MG）の列が Linux の手順と表の値に一致、DMC v1 の parse、ICL の TC の connect の列。host だけ（実機は無い:「未確認」）。

## この attempt で作った物

| file | 中身 |
| --- | --- |
| `intel/display-tables-xd.h` | ICL の 2 行（「Ice Lake」= port F のある ID、「Ice Lake without port F」= 0x8a51・0x8a5d。Linux は port F を `IS_ICL_WITH_PORT_F` で足す）を表の**最後**に（modeset.c の `drv_i915_lcd_xd_platform` は版の行が無い時に行 0 を使うので、行 0 は TGL のまま）。表: PCI ID、stepping（revid 7 = D0）、port（A・B combo、C〜F = TC1〜4 の MG PHY C〜F、HPD は TC の pin）、well（`icl_power_wells` の 21 個: PW_1、DC_off、PW_2、PW_3、DDI IO A〜F、AUX A〜F、AUX TBT1〜4（index 8〜11）、PW_4）、PLL（DPLL0/1、TBT、MG PLL 1〜4 = 0x46030+4n）、CDCLK（TGL と同じ `icl_cdclk_table`）、voltage 3 段（312/556.8/652.8 MHz）、DBUF（`icl_allowed_dbufs`、1 pipe 1 slice）、DBOX（A 2、BW 1、B 8、B2B 無し）、abox 0 だけ・buddy 無し・tracker 無し、WM level 8・read latency 2 µs、DMC `i915/icl_dmc_ver1_09.bin`・pipe DMC 無し、swing（combo の DP は全 rate で `icl_combo_phy_trans_dp_hbr2_edp_hbr3`、eDP の HOBL 無し、MG の DP 2 表と HDMI 表）、TBT PLL の語（分周も使う: `icl_tbt_pll_*`）、`tc_flow` FIA・`tccold` AUX_WELL_EXIT・`fia` SINGLE、WA の bit（combo AUX の lane、WA #1178）、VBT の番号は TGL の表（`port_mapping`・`icp_ddc_pin_map`・`direct_aux_ch_map`）。既存の 9 行には新しい field（`fia` READ_MODULAR、TBT の行 NULL）と swing の MG の 3 つ（NULL）を足しただけ |
| `display/xd-platform.h` | `I915_XD_PHY_MG`、`I915_XD_TCCOLD_AUX_WELL_EXIT`、`I915_XD_FIA_READ_MODULAR/_SINGLE`、`I915_XD_KIND_ICL`（log だけ）、`I915_XD_WA_COMBO_AUX_LANE`・`I915_XD_WA_AUX_ANAOVRD_1178`、`struct i915_xd_mg_swing(_table)`、swing set の `mg_dp_low/mg_dp_high/mg_hdmi`、platform の `fia`・`tbt_pll_19p2`・`tbt_pll_24`（末尾に足した） |
| `intel/display-regs-xd.h` | MG PHY の register（`I915_XD_MG_PHY(tc)` = 0x168000+0x1000·tc、lane は 0x400 おき: TX1/TX2 の LINK_PARAMS・PISO_READLOAD・SWINGCTRL・DRVCTRL・DCC、CLKHUB、DP_MODE、PLL の 10 語）と field、ICL の TBT AUX well の index（8+tc）、DPLL の CFGCR の ICL の場所（0x164000+0x80·pll: TGL の 0x164284 と違う）、AUX_ANAOVRD1、display 11 の transcoder（`I915_XD_TRANS_EDP` = 15: 0xF000 上なので既存の TRANS_* の macro がそのまま当たる、CHICKEN_TRANS の EDP 0x420cc、DDI select（bits 30..28）、EDP input、TRANS_CLK_SEL の field（bits 31..29））、DP_TP の名前（`I915_XD_DP_TP_PORT(ddi)` なら port の 0x64040+0x100·port、それ以外は今までの transcoder の場所） |
| `display/xd-clock.c/.h` | MG PLL: `drv_i915_xd_mg_pll_words`（Dekel と同じ分周の探索と clock top（line driver 2）、FBPREDIV は 2、8 bit に入らなければ 4、DIV0/DIV1/LF/FRAC_LOCK/SSC/BIAS/TDC、38.4 MHz は BIAS の mask 0・TDC は COLDSTART だけ）、`_port_khz`、`_enable`（power → clock top 3 語は field だけ rmw → DIV0〜SSC は write → BIAS・TDC は mask の rmw → posting read → enable → lock: `mg_pll_enable`・`icl_mg_pll_write` の順）、`_read`。TBT PLL の ICL の語（分周つき）。CDCLK の bypass は display 11 で 50 MHz。DPLL の DIV0 は display 12 以降だけ |
| `display/xd-tc.c/.h` | `drv_i915_xd_tc_mg_dp_mode`（Dekel と同じ mode の選び（static に分けた）を MG の DP_MODE に直に）、`drv_i915_xd_tc_mg_swing_table`、`drv_i915_xd_tc_mg_signal_levels`（`icl_mg_phy_set_signal_levels` の順: FS32 を消す → SWINGCTRL → DRVCTRL（override）→ CLKHUB（300 MHz 未満で low-rate lock）→ DCC（500 MHz 超で AMI の分周 1）→ PISO の CALCINIT）。FIA: ICL は FIA1 だけで modular の bit を読まない。TC cold: ICL は legacy だけ port の AUX の well、DP-alt・TBT は無し |
| `display/xd-power.c/.h` | AUX の well の Type-C の port を MG も含めて引く（TBT は well の powers の bit から: ICL の index 8〜11 も TGL/ADL-P の 9〜 も同じ道）。Dekel の uc_health は Dekel の port だけ。ICL の legacy の TC AUX の well は request の後に EXIT_TCCOLD（mailbox 0x12、busy なら 3 回 1 ms おき、受けたら 1 ms 待つ、失敗は state_late に記録して続ける: `icl_tc_cold_exit`）。ICL の combo AUX の well: request の後に PORT_CL_DW12 の AUX lane、state の後に AUX A/B の eDP でない port は AUX_ANAOVRD1（WA #1178）。disable は CL_DW12 を先に消す。hooks に `tc_legacy`・`port_edp` を足した（NULL は「いいえ」） |
| `display/power.c`（殻） | hooks の `tc_legacy`・`port_edp` を NULL に（旧の display は ICL の port を扱わない） |
| `display/xd-pipe.c/.h` | `drv_i915_xd_pipe_func_word_11`・`_func_enable_11`（display 11 の DDI select と EDP の input: pipe A は A_ON、B/C は ONOFF）、`drv_i915_xd_pipe_clock_on/_off`（display 11 の field、EDP は書かない）。configure の CHICKEN_TRANS の VBLANK_DELAY は display 12 以降だけ（Linux `IS_DISPLAY_VER(12, 13)`）、CHICKEN_TRANS の場所は EDP なら 0x420cc |
| `display/xd-ddi.c` | eDP の HOBL の表が無い platform（ICL）は HOBL を見ない |
| `display/xd-dbuf.c` | DBOX の B2B の regulate は B2B の最大が 0 でない時だけ（ICL は 0。TGL・ADL-P は 16 で今と同じ） |
| `display/xd-core.c` | CHICKEN_DCPR_2（Wa_14011508470）は display 12 以降だけ。ICL の init の WA は他に無い（`icl_display_core_init`・`intel_display_wa_apply` で ICL に当たる物は無い） |
| `display/xd-dmc.c` | event handler を消すのは display 12 以降だけ（Linux `disable_all_event_handlers` も < 12 は何もしない）。DMC v1 の parse（header v1、program は 0x80000、mmio ≤ 8、最大 0x6000）は前からあった物をそのまま使う |
| `display/xd-takeover.c`・`xd-vbt.c` | 「Type-C の port」を `phy_kind == DEKEL` から `tc != 0` に（MG も Type-C） |
| `tests/display/host-xd-icl-test.c`（新） | 48 check（下）。`run-xd-host-tests.sh` に足した |
| `tests/display/host-xd-platform-test.c`・`host-xd-cover-test.c`・`host-xd-power-test.c` | 行の数 9 → 11、ICL の行の check、shape の check に MG、cover の Type-C の口の MG PLL、未知の ID から ICL を外し CFL に。power-test の hooks の新しい 2 つを NULL に |
| `tests/display/xd-regs-map.h`、`plan/ws245/tests/check-xd-regs-linux.c` | MG・ICL の TBT AUX・AUX_ANAOVRD1・ICL の DPLL の CFGCR・EDP の transcoder・port の DP_TP・display 11 の select の対を 141 足した（EDP の transcoder の対は Linux の比べだけ: `I915_XD_MAP_EDP_TRANSCODER`） |
| `plan/ws245/tests/check-xd-tables-linux.py` | ICL の voltage・TBT PLL・stepping・PCI ID（port F の有無の 2 つ）・MG の swing 3 表・DBUF を足した |

host-xd-icl-test の中身: 行（D0・ICP・port C は MG PHY C の TC1・port F・MG PLL 4 の enable・well の並び・TBT AUX 1 の index 8）、MG PLL（RBR@38.4・HDMI 148.5@24 の語が Linux の値、enable の書き込みの順と 38.4 の mask、read、TBT PLL を拒む）、MG PHY（DP_MODE の 2 例、HBR と HBR2 の swing・CLKHUB・DCC・PISO、HDMI の既定の行）、TC（FIA1 を読まない、TC cold の機能、legacy と DP-alt の connect/disconnect）、AUX の well（legacy の EXIT_TCCOLD 1 回・health を呼ばない、DP-alt は呼ばない、TC2 の TBT AUX の TBT IO、AUX A（eDP）の AUX lane の順と WA 無し、AUX B の WA #1178 は 0x6c398、disable は lane が先）、transcoder（EDP の function の語（pipe A・B）、transcoder B の select、EDP の timing 0x6f000・conf 0x7f008・chicken 0x420cc、clock select の field と EDP は書かない、DDI B の DP_TP は 0x64140）、clock（bypass 50 MHz、voltage 3 段、TBT PLL の語 2 つ、DPLL0 は 0x164000/0x164004 で DIV0 無し、DBOX 0x4802）、DMC v1（parse、load は handler 無し・program・register・debug mask）。期待の値は Linux の define と式から手で組んだ（MG PLL の 2 例は Linux の text を host で動かした値）。

段の phase.md の必須の項目:
- 消す file: 無し。消す関数: 無し（`i915_xd_tc_pll_clock_tops` と `i915_xd_tc_pll_iref` は static の引数を足しただけ）。
- 殻が更新する旧の写し: 無し。表 R: 新しい書き手は MG PHY の register（ICL の行だけ。0x168000〜 の番地は TGL/ADL-P では Dekel の窓だが、MG の関数は ICL の port からしか呼ばれない: 今は誰も呼ばない）、EXIT_TCCOLD の mailbox（ICL の legacy だけ）、CL_DW12・AUX_ANAOVRD1（WA の bit の platform = ICL だけ）。TGL・ADL-P の書き込みは変わらない（下の「確かめた事」）。
- 殻が xd に渡す hook と取る lock: power.c の hooks の `tc_legacy`・`port_edp` は NULL（lock は今まで通り power domains の mutex の下）。
- R11: 触った source の file（power.c・xd-*.c/.h・display-regs-xd.h・display-tables-xd.h・tests/display の 5 つ）の数は全部 0。
- spinlock の下から呼ばれる関数: 無し（MG の関数は窓も lock も使わない: 直の register）。

## 設計との差（Q1 の確認が要る所）

1. **行は 10 ではなく 11**: design §3.1 は「ICL を足して 10 行」だが、port F の有無（Linux `IS_ICL_WITH_PORT_F`、ID で決まる）で port の表が違うので 2 行にした。
2. **display 11 の DDI function と transcoder clock は新しい関数（`_11`、`clock_on/_off`）で**: 既存の `drv_i915_xd_pipe_func_word/_enable` は P1 の xd-output が呼ぶので引数（platform・pipe）を変えなかった。merge の後に display 11 の分岐を xd-output から呼ぶか、関数を 1 つにまとめるかは xd-output の持ち主（P1）が決める。xd-output の TRANS_CLK_SEL の直の書き込み（`I915_XD_TRANS_CLK_SEL_SHIFT` 28）も display 11 では `drv_i915_xd_pipe_clock_on` に。
3. **DP_TP は名前で場所を分ける**: `link->trans`（と `drv_i915_xd_ddi_pattern/_idle_patterns` の `trans`）に display 11 では `I915_XD_DP_TP_PORT(ddi)` を渡す。既存の呼び（transcoder の番号）はそのまま当たる。
4. **power の hooks に 2 つ足した**（`tc_legacy`・`port_edp`）: legacy かどうか・eDP かどうかは xd-power が知らないので、Linux の `intel_tc_cold_requires_aux_pw`・`intel_port_is_edp` の答えを持ち主から聞く。NULL は「いいえ」（WA #1178 は eDP でないとして当てる）。

## 残り（この Phase の受け入れのうち、まだ無い物）

- **enable/disable の列**（eDP = transcoder EDP、HDMI = combo B、DP-TC = MG）を Linux の手順と比べる事。display 11 の順の差（DP は訓練の後に TRANS_CLK_SEL（hsw の形）、HDMI の disable は TRANS_CLK_SEL none を DDI_BUF_CTL off の前に、TC の DP_MODE と swing は MG の関数、PLL は MG PLL、transcoder EDP は `_11` の関数）は列を組む xd-output（P1 の p050b・p050c で入った物）に分岐を足す所。部品と部品ごとの書き込みの順は host-xd-icl-test で固定した。p044 の「9 行 × 列」と同じ時に、ICL の 2 行も cover の列に足す。
- **ICL の takeover の readout**: GOP が eDP を灯すのは transcoder EDP（`TRANS_DDI_FUNC_CTL(EDP)` を読む）で、DDI select の field も display 11 は違う（bits 30..28）。xd-takeover の readout は display 12 の field と transcoder A〜D だけを読む。ICL の attach は §7 #30 の別の判断なので、attach の前に直す（takeover の持ち主に）。
- **WM の read latency +2 µs**: 記述の `wm_read_latency_us` = 2 は入れたが、xd に latency を読む部品はまだ無い（今は旧 `watermark.c` が読む）。
- `drv_i915_xd_pipe_stop` の FEC stall の bit（Linux は display 12 以降だけ）は platform を受けないので display 11 でも 0 にする rmw が残る（0 の bit を 0 にするだけの見込み。関数の引数を変える時に直す）。

## 確かめた事

- host（2026-10-17）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p045/xd-host` → 22 本全部 pass（icl 48、platform 97、cover 111、regs 639、power 65、tc 114、dmc 32、dpll 30、ほか前の段と同じ数）、`I915_XD_STRICT` と `kind`・`verified` の grep も通る。
- Linux との比べ（tree は P1 の worktree の `build/ws229-p001/linux/linux-6.8.12` を読むだけ）: `check-xd-regs-linux.sh` 1304 対が一致（0 差）、`check-xd-tables-linux.py` 全部の表が一致（75 表。最初の版で MG の swing の行の並びを取り違えていたのをこの比べが見つけて直した）。
- MG PLL の語: Linux v6.8.12 の `icl_mg_pll_find_divisors`・`icl_calc_mg_pll_state`・`icl_ddi_mg_pll_get_freq` の text を display 11 として host で compile し（p040 の `plan/ws051/tests/host-dkl.sh` と同じ抜き出し、scratch で commit しない）、DP 1000 kHz・HDMI 250 kHz の刻みで 20〜700 MHz、参照 19.2/24/38.4 MHz を比べ、7371 件で 12 語（mask を含む）と周波数が全部一致、2835 件は両方が拒む、差 0。2 件（RBR@38.4、HDMI 148.5@24）は host-xd-icl-test に固定した。
- build（2026-10-17）: `sh build/tmp/kbuild.sh p045`（P1 の kbuild.sh を p6 に写した物）で既定と `I915_TESTS=y` の 5 組（vke2・display・display_ktest・display_ktest2・execution）が rc 0・warning 0（既定の vmunix の sha256 の頭 aeb18c49b660e9df）。`git diff --check` は空。
- 旧の host 試験（host-dp-test 72・host-lcd-modeset-test 120）は前の木（aa14ff623）と出力が同じ（dp・lcd とも SAME、`build/tmp/oldtests.sh` は P1 の harness の写し）。
- TGL・ADL-P の振る舞いが変わらない根拠: 変えた分岐は全部 display 11 の field（`display_ver < 12`、`fia`、`tbt_pll_19p2`、WA の bit、`edp_hobl == NULL`、`b2b_max == 0`、`tccold == AUX_WELL_EXIT`、`phy_kind`）で、TGL/ADL-P の行の値では前と同じ道を通る。TBT AUX の port を well の番号から powers の bit に変えた所は host-xd-power-test（ADL-P の TBT AUX）と host-xd-tc-test が前と同じく通る。
- 5330: 未実施（全部の段の後に 1 回。振る舞いの変化は無い見込み: 上の根拠）。ICL: 実機は無い（未確認）。

## 実機で確かめる事

- 5330（全部の段の後の 1 回）: 起動・survey・digest に差が無い事（ADL-P の行の値は変わらない）。
- ICL: 実機は無い。attach（§7 #30）の時に、起動・eDP（transcoder EDP）・HDMI・USB-C（MG、legacy と DP-alt）・suspend。

## P1 の branch との合わせ（2026-10-17 q1229-i10、P1）

- merge: P6 の c85e8569e を agent/p1-ws245 に名指しで merge（c3d27ec57、親 b720306ed（p050d3 の記録）と c85e8569e）。衝突 3 file: `xd-platform.h` の WA の bit（P1 の `I915_XD_WA_TC_RESET_AFTER_TBT` 0x0800 を残し、P6 の `I915_XD_WA_COMBO_AUX_LANE` を 0x1000、`I915_XD_WA_AUX_ANAOVRD_1178` を 0x2000 に付け替え。使うのは名前だけ）、`display-tables-xd.h` の ADL-P 系 4 行（P1 の WA の bit と P6 の新しい 3 field の両方）、`run-xd-host-tests.sh` の試験の列（両方）。merge の後: `run-xd-host-tests.sh` 24 本 all passed（icl 48、cover 111、platform 97、regs 651）、旧 host 試験と out50c は b720306ed と同じ（stagecmp: dp SAME、lcd・out50c の差 0）。
- xd-output の display 11 の分岐（88e99dbe2）: 「設計との差」の 2・3 と「残り」の列のうち xd-output の物を入れた。
  - DP の port: display 11 は transcoder の function を訓練の前に書かず、splitter も無く、transcoder の clock は訓練の後（Linux `hsw_ddi_pre_enable_dp` の順）。display 12 以降は今まで通り。
  - transport（DP_TP）: display 11 は `I915_XD_DP_TP_PORT(ddi)`（`i915_xd_output_transport`、DDI の link・pattern・idle が使う）。
  - function の enable: display 11 は `drv_i915_xd_pipe_func_enable_11`（pipe を渡す。eDP の transcoder は pipe の input）。関数は 1 つにまとめず分岐にした（`_func_config` は display 11 で呼ばない）。
  - transcoder の clock: `drv_i915_xd_pipe_clock_on/_off` を全部の版で使う（display 11 の field、eDP の transcoder は書かない）。stop は display 11 で DDI buffer を止める前（Linux `intel_ddi_post_disable_dp/_hdmi` の `< 12`）。
  - MG PHY: TC の port の PHY が MG（`i915_xd_output_mg`、platform の port の `phy_kind`）なら DP mode は `drv_i915_xd_tc_mg_dp_mode`、signal levels は MG の表と `drv_i915_xd_tc_mg_signal_levels`、訓練の swing の上限は MG の表の行数、PLL は `drv_i915_xd_mg_pll_enable`（記述に `mg_pll_words` を足した。作るのは記述の作り手: ICL を attach する時）。
  - host 試験（`host-xd-output-test` 67 check、+9）: ICL の panel を transcoder EDP で灯す（function の語: DDI A の display 11 の select・pipe A の input・enable、clock select を書かない、port A の transport で訓練して function はその後、transcoder A の transport に触らない、dark で止めて function を消す）、TC1 の MG（MG PLL 1 の lock と DIV0、DP mode と swing、transcoder B の clock select 0x60000000 が訓練の後に 1 回、dark で clock を DDI C の buffer の前に外す）。
- まだ無い物（この Phase の残り）: HDMI の display 11 の列（combo B。HDMI の port の順は display 12 と同じ。clock select は display 11 の field で入った）を Linux の列と比べる試験、ICL の 2 行を cover の列に足す事、xd-output の電源の use（transcoder の index 15）を xd-power の domain にどう写すか（attach の時）、ICL の takeover の readout、WM の read latency。build: 6 組 warning 0（`kbuild.sh d45`）。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 5330 の起動・survey・digest に差が無い（ADL-P の行は不変）

満たしていない・未確認の基準と残る物:

- host の受け入れの残り（ICL の enable/disable の列、ICL の takeover の readout、WM の read latency、FEC stall の bit）は phase.md の「残り」のとおり未実施。実機は無い

P1 の見立て（Status は変えない。判定は Q1）: cleared にしない（host の受け入れが残る）。

## host の受け入れの続き（2026-10-18 P1、q1244）

- 足した物（be2e6c243）: `host-xd-output-test` の `i915_xd_ot_test_cover`。表の全部の行（display 12・13 の 9 行と ICL の 2 行）で、同じ panel（DDI A、DPLL 0）の light と dark を xd-output の step で流す（display 11 は transcoder EDP、1 slice、pipe の DMC 無し）。各行で: light が通る（error・lock 0、lit）、DDI A の buffer と transcoder の function が on、dark でどちらも off、power の get と put（遅らせた put を含む）の数が同じで held 0。22 check（67 → 89）。最初の版で ICL の 2 行が transcoder A の panel で落ちた（transcoder EDP が要る: 期待どおり）ので display 11 の分岐を入れた。
- 確かめた事: `run-xd-host-tests.sh build/p244c-host` all passed（host_xd_output_test 89 checks 0 failures）。build は cebac0006 で 3 組 warning 0（試験だけの変更なので kernel は同じ）。
- まだ無い物: 列を Linux の手順・表の値（ADL-P 系は m3 の dump）と書き込みの 1 つずつで比べる事（足したのは各行で通る事と on/off と参照の釣り合いまで）、HDMI・DP-TC・TBT の 3 種を各行で、RKL・ADL-S の takeover の列、TGL の流れ B の配線（p052）、ICL の takeover の readout、WM の read latency、FEC stall の bit。期待の列を行ごとに Linux から組むのは 1 attempt では収まらない大きさ。

## 判定の材料（2026-10-18 P1、q1244）

- 満たした: 全部の行で eDP の light・dark が xd の部品で通る（host）。5330 の steady 差 0（q1229-i20・i21、前の節）。
- 満たしていない: 上の「まだ無い物」（Linux の列との比べと 3 種の kind）。
- P1 の見立て: cleared にしない。
