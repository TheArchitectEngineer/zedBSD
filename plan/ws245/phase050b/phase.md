<!-- awesome-plan project=zedbsd record=ws245-p050b -->

# ws245-p050b: eDP の step の本体を xd の部品の直の呼びに

Status: in-progress（2026-10-17 P1: q1229-i07 で実装・host 試験・build は済み（aa4374975）。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない）
Parent: [WS245](../ws.md)
Queue: q1229-i07（P1）
設計: [design.md](../design.md) §6.2 の p050 の行、§3.3、§4.2

## 範囲

xd-screen の light・dark の step（power・dbuf・setup・pipe dmc・link power・pll・port・pipe・output・check・plane・settle と dark の各 step）の中身を、eDP（内蔵 panel）について xd の部品（xd-panel の PPS と backlight、xd-aux の DPCD、xd-power の DDI IO と AUX の well の参照、xd-dp の訓練、xd-dbuf の WM/DBUF の書き込み、xd-pipe、xd-clock、xd-ddi、vblank の event）を直に呼ぶ形にする。旧の modeset.c・state.c・ddi.c・dp-sink.c・pipe.c・plane.c・aux.c・panel.c・panel-backlight.c・vblank.c の eDP の道の state（Linux の型の PPS・backlight・wakeref・link）は使わなくなる。

## 受け入れ

- host 試験（run-xd-host-tests.sh、新しい check は xd の試験へ）、build（既定と I915_TESTS=y の 5 組）warning 0、段の前後の比べ（旧 host 試験が残る間は refcmp.sh）、R11 の数。
- 5330 の回帰は全部の段の後に 1 回（ユーザーの決定 2026-10-16）。

## 着手の手がかり（2026-10-16 P1、p050a の後の引き継ぎ）

- 今の step の中身: `modeset.c` の `i915_modeset_step_*`（light 12・dark 9、`i915_modeset_light_hooks`/`_dark_hooks`）→ `pipe.c` の `drv_i915_lcd_ms_crtc_enable_part/_disable_part` → `i915_hsw_crtc_enable_part/_disable_part`（DMC・LINK・PLL・PORT・PIPE・OUTPUT）→ `ddi.c` の encoder の段（eDP は `i915_tgl_ddi_pre_enable_dp`（ddi.c:894、pps_on・enable_clock・DDI IO の wakeref・mg dp mode・transcoder clock・func config・signal levels・lanes・MSO・D0・link train の start/stop・FEC）、`i915_enable_ddi_dp`、`i915_disable_ddi_dp`、`i915_ddi_post_disable_dp`、`i915_ddi_post_pll_disable`）と `pipe.c` の source size・misc・transcoder・colour・linetime・chicken、`clock.c` の shared DPLL の enable/disable、`watermark.c` の dbuf pre/post plane update と MBUS DBOX、`dmc.c` の pipe DMC、`modeset.c` の crtc の power domain の get/put。
- 旧の host 試験 `host-lcd-modeset-test`（120 check）の詳細の出力（`build/p050a/old-new/lcd.txt`、`oldtests.sh` が作る）は、commit ごとの全部の trace（register の read/write/rmw、`pw-get`（backend の power の get、domain の番号）、`panel`（backend の PPS の op）、`dbuf`、DPCD、`decide`、observer の点）を持つ。refcmp の SAME はこの trace の一致。新しい step の中身が backend の hook（read32/write32/rmw/usleep、power_get/put/put_async、panel、dpcd_read/write、dbuf_slices_update、step）を同じ順で呼べば SAME を保てる（p028 の形: backend の read32/write32 を hook にした xd の window）。
- 案（既定）: 新しい xd の file（例 `xd-output.c`: 1 つの出力の step）に、eDP の step の中身を xd の部品で書く。環境は hook の構造体（register の window、power の get/put（機能の名前で）、panel の PPS の op、DPCD の accessor、vblank の event の待ち）。殻（modeset.c）は今の backend の hook をその環境に渡し、plan（p038 の `struct i915_xd_plan`。数は旧の check と一致を確かめ済み）・PPS と backlight の設定・plane の語から step を呼ぶ。殻が更新する旧の写し（`ms->crtc.active`・`ms->dig_port.dp.link_rate/lane_count/train_set/link_trained`・`ms->pll.on`・`ddi_io_wakeref`・`aux_wakeref`・backlight の level・`stop_unconfirmed`・`dc_off_held`）は status・confirm・backlight.c が読む間（p050d まで）保つ。
- 先に読む物: `ddi.c` の DP の段（上の関数と `i915_ddi_enable_transcoder_clock`・`i915_ddi_config_transcoder_func`・`i915_ddi_power_up_lanes`・`i915_ddi_init_dp_buf_reg`・`i915_icl_ddi_combo_enable_clock`・`i915_ddi_set_dp_msa`・`i915_disable_ddi_buf`・`i915_ddi_disable_transcoder_func`）、`panel.c` の pps_on/off/vdd、`panel-backlight.c` の enable/disable、`dp.c` の link train の glue、`pipe.c` の PIPE の part、`watermark.c` の `drv_i915_dbuf_pre/post_plane_update`・`drv_i915_mbus_dbox_update`、`clock.c` の `drv_i915_enable/disable_shared_dpll`、`modeset.c` の `drv_i915_modeset_get/put_crtc_power_domains`。

## 変えた物（2026-10-17 P1、aa4374975）

| file | 中身 |
| --- | --- |
| `display/xd-output.c/.h`（新） | 1 つの出力の light・dark の step の中身（内蔵 panel）。xd の部品を直に呼ぶ: `dc_hold`（DC off）、`power`（core・pipe・transcoder・lanes の順）、`dbuf`（MBUS の join と両方の slice、DBOX）、`backlight_setup`（VBT の controller・周波数・最低値、firmware の period と duty）、`dmc`（pipe DMC の on/off）、`link_power`（AUX）、`pll`（DPLL の words、pipe の参加）、`port`（PPS on、DPCLKA の map、DDI IO、TRANS_CLK_SEL、func config、signal levels、lane、splitter off、sink の OUI と D0、訓練と終わり、MSA）、`pipe`（source、misc、transcoder、colours、linetime、chicken）、`start`（func enable、transcoder start、backlight on、DIP off）、`settle`、dark の `dark_power`・`stop`（backlight off）・`port_off`（vblank の event、transcoder stop、func off、scaler、DIP、sink D3、DDI stop、clock sel none、VDD→panel off、DDI IO、DPCLKA gate）・`pll_off`・`link_power_off`・`dark_settle`、`dc_allow`（17 ms 後）。brightness（user・ACPI）、user level、backlight on/off。環境 `struct i915_xd_output_env`: io・platform、power の get/put/put_later（用途 `I915_XD_OUTPUT_POWER_*` と index）、PPS の step、DPCD の sink、DBUF の slices、lock（PLL・backlight）、vblank の event、link の失敗、観測点、error/debug |
| `display/output-shell.c`（新、殻: p050d で消す） | modeset の object から出力の記述（`drv_i915_lcd_ms_xo_describe`）、DBUF の前後（`_xo_dbuf`）、環境（`_xo_env`: backend の hook）、旧の写しの更新（`_xo_sync`） |
| `display/modeset.c`・`modeset-internal.h`（殻） | light・dark の step は panel（`!output_hdmi && !output_dp_ext`）の時 xd-output を呼ぶ。HDMI・DP-ext は旧の crtc の part のまま（p050c）。`struct i915_lcd_modeset` に `xo` |
| `display/panel-backlight.c/.h` | Linux 由来の backlight の本体（setup・enable・disable・scale・invert・cnp_*、`drv_i915_backlight_enable/_disable/_set_acpi`、`drv_i915_lcd_ms_backlight_setup`）を消し、modeset object の hook（set_brightness・set_acpi・backlight_power・user_level）は xd-output を呼ぶ。file の先頭の Linux の表示も消した（残りは own の wrapper） |
| `display/dp.c/.h`・`ddi.c` | `drv_i915_edp_backlight_on/_off` を消した（eDP だけの物、eDP は xd-output）。`drv_i915_dp_sink_bind`・`drv_i915_dp_link_failed` を足した（殻が使う、p050d で消す） |
| `display/pipe.c` | `drv_i915_lcd_xd_timing`（殻が使う） |
| `display/xd-ddi.c/.h` | `drv_i915_xd_combo_level_row`（sink の level byte → swing 表の行）、`drv_i915_xd_combo_swing_max` |
| `display/xd-dmc.c/.h` | `drv_i915_xd_dmc_pipe_switch`（program があると分かっている pipe の DMC の enable） |
| `display/xd-pipe.c/.h`・`xd-takeover.c` | `drv_i915_xd_pipe_scalers_off`。xd-takeover の局所の scaler・DIP の define を `display-regs-xd.h` の物に |
| `intel/display-regs-xd.h` | `I915_XD_PS_CTRL/_WIN_POS/_WIN_SZ`、`I915_XD_DIP_CTL` と packet の bit、`I915_XD_PIPE_DSS_CTL1`（pipe A は B・C の 1 つ下: Linux の macro のまま）、`I915_XD_TRANS_CLK_SEL_SHIFT` |
| `tests/display/host-xd-output-test.c`（新） | 47 check: level の行、light（power の順、DBUF、backlight の範囲、DMC、AUX→PLL、PLL の words・lock、PPS→DPCLKA→DDI IO、clock sel、func の語、OUI→D0→訓練、訓練の結果、pipe の語、start の順、settle）、dark（逆順と全部の返却）、backlight の値（user・ACPI・off/on・反転・VBT 無し・period 無し）、fault（PLL の lock、訓練の失敗、running の pipe、power の失敗、DMC program 無し） |
| `plan/ws245/tests/run-xd-host-tests.sh`・`platform/amd64/vmunix.mk` | 試験と file を足した |

段の phase.md の必須の項目:
- 消す file: 無し（output-shell.c は p050d で消す殻）。
- 消す関数とまだ残る呼び手: panel-backlight.c の Linux 由来の関数全部と dp.c の `drv_i915_edp_backlight_on/_off` を消した。ddi.c・dp.c・pipe.c・clock.c・watermark.c・dmc-shell.c の eDP の道の関数は HDMI・DP-ext（p050c）が同じ関数を使うので残る（`build/tmp/deadexp2.py` で前の段 5342e0844 と比べ、新しく呼び手 0 になった export は無い）。
- 殻が更新する旧の写し（`drv_i915_lcd_ms_xo_sync`）: `crtc.active`、`dp.link_trained/train_set/DP/hobl_*`、`dp.dpcd`・LTTPR の caps、shared DPLL の `on/active_mask`、`ddi_io_wakeref`・`aux_wakeref`、`dc_off_held/_wakeref`、crtc の power domain の set（4 つ）、panel の backlight（present・enabled・min・max・level・pwm_level_*・controller・active_low）。読む側: status、confirm、brightness の入口、flip・plane（crtc.active）。p050d まで。
- 表 R: panel の light・dark の書き手は xd-output（旧の crtc の part は HDMI・DP-ext だけ）。PWM の書き手は xd-output（xd-panel を直に）と takeover（xd-display）。PPS の op（power・VDD・backlight の bit）は backend の panel hook を通る（kernel は panel.c: p050d で xd に）。
- R11: 触った file の開発の管理情報は全部 0（grep）。

## 旧と同じに保った物と違い

- 確かめ方: 旧の host 試験 `host-lcd-modeset-test`（120 check）は全部 pass。trace を `build/tmp/trnorm.py`（read と decide を除き、rmw を書いた値に、put の cookie を除く）で前の段（5342e0844 の木）と比べると、違いは error の文言の 2 行だけ（`PLL %d not locked` → `xd-output: the port PLL is not locked`、`pipe_off wait timed out` → `xd-output: the transcoder did not stop`）。rmw は read と write になり（pipe DMC、DSS_CTL1）、posting read が増えた（DIP_CTL）。`host-dp-test` の出力は SAME。
- 違い: (1) crtc の domain の put は保持した cookie で（旧は -1。kernel の put は cookie を見ない）。(2) `intel_crtc_vblank_on` の decide の記録は無い（kernel の振る舞いは同じ）。(3) power の get が失敗したら error を数えて step を失敗にする（旧は 0 の wakeref のまま続けた。殻は step の戻り値を見ず旧と同じく続ける）。(4) 訓練の失敗で HOBL が使われていたら xd-output が `hobl_failed` を立てる（旧は fallback の中で sink が接続している時だけ）。
- 設計との差: design §3.2 では enable/disable は xd-screen の責務。step の中身を 1 つの出力の file（xd-output）に分け、xd-screen は順と失敗の扱いを持つまま（phase050b の手がかりの案）。p050d で xd-display の screen が xd-output を直に呼ぶ。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- 内蔵 LCD が点く（survey の PP_STATUS on、PWM、frame counter、SURFLIVE、CRC）。backlight の明るさの変更（backlight-probe）と off/on。
- digest の差は rmw → read+write と DIP_CTL の posting read だけの見込み（書き込みの値と順は同じ）。

## 確かめた事

- host（2026-10-17）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p050b/xd-host` → 全部 pass（output 47 を含む）、`I915_XD_STRICT` は通る。
- 旧の host 試験: `sh plan/ws245/temp/harness/oldtests.sh <tree> build/p050b/old-new4` → dp 72・lcd 120 pass、`trnorm.py` の比べは上の 2 行だけ。
- build（2026-10-17）: `sh build/tmp/kbuild.sh p050b` で 6 組が rc 0・warning 0。`git diff --check` は空。
- 5330: 未実施（全部の段の後に 1 回）。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- 内蔵 LCD が点く（PP_STATUS on、PWM の duty、frame counter、SURFLIVE、CRC）、backlight-probe の set 100、digest の差は I・H の行だけ

満たしていない・未確認の基準と残る物:

- backlight の off/on は T1 の項目に無く未確認

P1 の見立て（Status は変えない。判定は Q1）: cleared にしてよい（off/on は ユーザーの一覧へ）。
