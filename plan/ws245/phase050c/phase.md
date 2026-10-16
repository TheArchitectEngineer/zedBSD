<!-- awesome-plan project=zedbsd record=ws245-p050c -->

# ws245-p050c: HDMI と DP-TC の step の本体

Status: in-progress（2026-10-17 P1: q1229-i07 で実装・host 試験・build は済み（81332cf8e）。5330 の回帰は全部の段の後に 1 回（ユーザーの決定 2026-10-16）。それまで cleared にしない）
Parent: [WS245](../ws.md)
Queue: q1229-i07（P1）
設計: [design.md](../design.md) §6.2 の p050 の行、§3.3、§4.2

## 範囲

p050b と同じ形で、HDMI（combo の DDI B、GMBUS の DDC、SCDC）と DP-TC（Type-C の DP、TC の link の参照は旧 tc.c の殻の hook: p052 まで）の step の中身を xd の部品の直の呼びに。

## 受け入れ

- host 試験（run-xd-host-tests.sh、新しい check は xd の試験へ）、build（既定と I915_TESTS=y の 5 組）warning 0、段の前後の比べ（旧 host 試験が残る間は refcmp.sh）、R11 の数。
- 5330 の回帰は全部の段の後に 1 回（ユーザーの決定 2026-10-16）。

## 変えた物（2026-10-17 P1、81332cf8e）

| file | 中身 |
| --- | --- |
| `display/xd-output.c/.h` | 3 種の出力（panel・HDMI・Type-C の DP）の step。HDMI: AUX の power 無し、port の step は DDI clock・DDI IO・TRANS_CLK_SEL・DIP off、pipe の step は M/N 無し、start は func（DVI）・transcoder start・HDMI の swing（VBT の level か表の既定）・lane・`drv_i915_xd_ddi_hdmi_start`、dark は sink の D3 無し・DDI stop（dp=0）。Type-C の DP: `struct i915_xd_output_tc` の hook（link の get/put、mode、FIA の lane、DP mode、DKL の levels、PLL（pool）、PHY の reset の WA）。link_power で link → mode → AUX → lane、clock は `drv_i915_xd_tc_clock_on/_off`（自前の PLL か TBT の rate）、DP mode、TBT では DDI IO・lane・ownership 無し、ADL-P の DP-alt/legacy では buffer の前に PHY の WA と ownership bit、repeater を探す、converter の hook、link_power_off で AUX → link を返す |
| `display/xd-clock.c/.h` | `drv_i915_xd_tc_clock_select/_on/_off`（xd-takeover の TC の clock off もこれに） |
| `display/output-shell.c`（殻） | 3 種の記述（kind、TC の port と swing の上限、HDMI の level）、DP-ext の converter と Type-C の hook（旧の tc の macro、`drv_i915_icl_update_active_dpll`、PLL の pool `drv_i915_enable/_disable_shared_dpll`、ddi.c の DKL の DP mode・levels・WA）。TC の PLL は pool の記録のまま（写さない） |
| `display/modeset.c` | light・dark の step は全部 xd-output（旧の crtc の part の分岐を消した）。commit の `put_domains`・`off_state` と world の off state を消した |
| `display/ddi.c`・`pipe.c`・`watermark.c`・`power.c`・`dmc-shell.c`・`color.c`・`clock.c`・`dp.c`・`plane.c`・`vblank.c` とその header | 呼び手 0 になった物を消した: crtc の part、encoder の段（`drv_i915_encoders_*` と pre/post の hook の本体、enable/disable の encoder の hook の結び付け）、DDI の clock・transcoder clock・func・lane・MSO・buffer の旧の書き手、`drv_i915_enable/_disable_transcoder`、crtc の power domain の get/put と in_set、DBUF の pre/post と DBOX、pipe DMC の on/off、colour の load/commit、`drv_i915_dpll_map/_unmap/_ddi_mapped`、DP の set_power・link params・start/stop link train・infoframes・is_edp、`drv_i915_phy_is_combo`、`drv_i915_icl_hdr_plane_mask`、`drv_i915_wait_for_pipe_scanline_moving`。ddi.c は 2358 行減。TC の DKL の DP mode と WA は `drv_i915_lcd_ms_tc_dp_mode/_phy_wa`（殻の hook、p040 で xd-tc に） |
| `display/hdmi-mode.c/.h` | 中身を空にして vmunix.mk から外した（infoframe・scrambling・DP++ の切り替えは呼び手 0）。file の削除は merge の時に Q1 |
| `tests/display/host-xd-output-test.c` | +11 check（HDMI on DDI B、Type-C の DP-alt と TBT） |

段の phase.md の必須の項目:
- 消す file: `display/hdmi-mode.c`・`hdmi-mode.h`（中身を空にした。削除は Q1）。
- 消す関数とまだ残る呼び手: 上の表（`build/tmp/killdead.sh` で前の段 c569cd427 の dead の一覧と比べ、新しく呼び手 0 になった export を全部消し、static も続けて消した。`staledecl.py` で古い宣言も消した）。残る旧の物: TC の DKL の levels・DP mode・WA、TC PLL の pool（p040・p052）、plane・flip・vblank・status（p050d）。
- 殻が更新する旧の写し: p050b と同じ（TC の PLL は pool が持つので写さない）。
- 表 R: 3 種とも light・dark の書き手は xd-output。DKL PHY の DP mode・levels・WA と TC/TBT の PLL の enable は殻の hook を通る旧の code（p040 で xd-tc・xd-clock に）。TC の link の参照は旧 tc.c（p052 まで）。
- R11: 触った file の開発の管理情報は全部 0。

## 旧と同じに保った物と違い

- 確かめ方: 旧の host 試験（lcd 120・dp 72）は pass、lcd の trace の差は p050b と同じ 2 行の文言だけ。HDMI と DP-TC は旧の lcd 試験に無いので、harness `plan/ws245/temp/harness/out50c.c`（`run-out50c.sh <tree> <out>`: lcd の model で HDMI（DDI B、VBT の level 無しと 3）、DP-TC（TC1、model の mode は none → TBT の PLL）、DP-alt（tc の hook を model に付けて DP-alt、TC PLL 1）の enable と disable の trace を出す）を前の段の木（c569cd427）と今の木で走らせ、`trnorm.py` で比べた（`STEP intel_tc_port_in_mode` の問い合わせの行は除く）。差は error の文言（DDI の idle・idle pattern）と、DP-TC の disable の `STEP intel_tc_port_link_cancel_reset_work (no reset work is queued)`（取り消す reset の仕事は無い: 呼ばない）だけ。
- 違い: (1) HDMI の DIP の書き込みの前の func の読み（旧の assert）は無い。(2) TC の port の mode の問い合わせの回数（読みだけ）。(3) TC の reset work の取り消しの記録が無い。
- 設計との差: HDMI の sink は DVI mode で scrambling を使わないので、SCDC と DP++ の切り替えの hook は持たない（旧も実際には何もしていなかった: sink の SCDC の能力が無い、DP++ の type 2 は未移植の step）。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- 外部出力（HDMI・USB-C DP）は節目だけユーザーの一覧（常設できない）。内蔵 LCD は p050b と同じ。

## 確かめた事

- host（2026-10-17）: run-xd-host-tests.sh 全部 pass（output 58）。旧の host 試験 pass。harness の比べは上のとおり。
- build: `sh build/tmp/kbuild.sh p050c` で 6 組 rc 0・warning 0。`git diff --check` 空。
- 5330: 未実施。
