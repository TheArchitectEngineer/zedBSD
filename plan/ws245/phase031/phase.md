<!-- awesome-plan project=zedbsd record=ws245-p031 -->

# ws245-p031: DP の訓練（xd-dp）

Status: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない）
Parent: [WS245](../ws.md)
Queue: q1229-i04（P1、ws245-p031 から残りの段を design の順に実装して branch に積む）
設計: [design.md](../design.md) §6.2 の p031 の行、§2.3

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-dp.c/.h`（Zlib、新） | DP の link: 両端の rate（source の表を platform と VBT の上限まで、sink の標準の rate を自身と repeater の上限まで、共有、無ければ RBR）、eDP 1.4 の rate の表の読み、fallback の次の上限（一つ下の共有の rate、最低なら lane を半分で最高の rate）、mode の link の選び（深い色から、低い rate から、少ない lane から）、データ量（need・carry・stream・symbol）、M/N（固定 N、field に収まるまで半分）、link status（CR・EQ・repeater の 5 byte を sink の形に）、訓練（repeater の検出と非透過への切り替え（link が上がっていれば mode を保つ）、capability の読み直し、source の準備、DOWNSPREAD・BW_SET（rate の index の道は LANE_COUNT_SET・LINK_RATE_SET と eDP の rate の表の読み直し）、repeater を番号の大きい方から、sink を最後に。各 receiver は CR（pattern 1、最低の level から要求に従う。同じ要求 5 回・top・10/80 回で諦める）と EQ（TPS4 > TPS3 > TPS2、5 回）、pattern を外し、source の idle）、訓練の終わり（sink → port の pattern off）。`Values referenced:`（rate の表、効率、M/N の N、待ち、回数）と `Procedure referenced:`（intel_dp.c・intel_dp_link_training.c・drm_dp_helper.c の関数）を block ごとに |
| `intel/display-dpcd-xd.h`（新） | DPCD の address と field（受信の capability、link の設定、level の byte、link status、repeater の範囲）。`Values referenced:` drm_dp.h |
| `display/dp.c`（殻、3287 → 966 行） | `drv_i915_dp_start/stop_link_train` は port と crtc state から `struct i915_xd_dp_link` を作り xd を呼ぶ（sink の hook は環境の DPCD accessor、source の hook は port の set_signal_levels・set_link_train・prepare_link_retrain・set_idle_link_train、`intel_dp->train_set` は hook と訓練の後に写す）。`drv_i915_drm_dp_channel_eq_ok/clock_recovery_ok/dpcd_read_phy_link_status` は xd の殻。fallback の要求（`i915_dp_schedule_fallback_link_training`）は殻に残る。drm_dp_helper.c の出典の表示は消した（その code が残らない） |
| `display/dp.h` | 帯域の 5 関数の宣言を消した |
| `display/pipe.c` | `drv_i915_link_compute_m_n` は xd の M/N の殻。`i915_compute_m_n`・`i915_reduce_m_n_ratio` を消した。dotclock の symbol の bit は xd |
| `display/state.c` | eDP の need/carry と、外部 DP の link の選び（`i915_state_dp_ext_link`）は xd |
| `display/dp-ext.c` | rate の一覧（`i915_dp_ext_set_rates`）と fallback（`drv_i915_dp_ext_fallback_values`）は xd。rate の表 2 つ、`i915_dp_ext_bw_code_to_rate`、BW の定数を消した。R11: comment の `ws051-p005b` を消した |
| `tests/display/host-xd-dp-test.c`（新） | 32 check（下） |
| `tests/display/xd-regs-map.h`・`host-xd-regs-test.c`・`plan/ws245/tests/check-xd-regs-linux.c` | DPCD の 62 対（tree の `intel/dp.h` と Linux の `drm_dp.h` と比べる） |
| `platform/amd64/vmunix.mk`・`plan/ws245/tests/run-xd-host-tests.sh` | xd-dp.c と host-xd-dp-test を足した |

段の phase.md の必須の項目:
- 消す file: 無し（dp.c は p050 で殻ごと消える。`dp-sink.c` の DPCD の転送は AUX の DPCD 層で訓練の物ではないので残る）。
- 消した関数（本番の呼び手 0）: dp.c の訓練・LTTPR・link status の static 約 70、公開の `drv_i915_dp_link_required`・`_link_symbol_size`・`_link_symbol_clock`・`_effective_data_rate`・`_max_data_rate`（呼び手 state.c・pipe.c は xd に）、pipe.c の 2、dp-ext.c の 1。
- 殻が更新する旧の写し: `intel_dp->dpcd`（訓練で読み直す。xd は殻の storage に書く）、`lttpr_common_caps`・`lttpr_phy_caps`、`train_set`、`link_trained`（stop で立てる）。
- 表 R: 該当無し（DPCD は sink の register。DDI_BUF_CTL・DP_TP_CTL は p030 の xd-ddi が port の hook で書く）。
- hook と lock: modeset の lock の下。DPCD は環境の accessor（AUX の lock はその中）。sleep は現在の device の usleep。
- R11: 触った file は dp.c 0、dp.h 0、pipe.c 0、state.c 0、dp-ext.c 1 → 0。

## 旧と同じに保った物と違い

- 確かめ方: 旧の host 試験 `host-dp-test`（72）と `host-lcd-modeset-test`（124）の出力（ptr を正規化）が段の前（f86a468da の `git archive`）と**全部一致**。lcd 試験の trace は DPCD の読み書き（0x000 の caps、0x107・0x100、0x102 の pattern、0x202 の status、0x103 の level）と register の書き込みの列を含むので、訓練の列が同じ事を見ている（B2 の CR の失敗と fallback の要求の error の文も同じ）。
- 違い: 128b/132b（UHBR）の訓練の道は無い（display 12・13 の source の表は 810000 まで）。`drv_i915_dp_is_uhbr` は port clock が 810000 を超えるかで答える。stop の intra-hop の待ちも無い。
- 違い: source が直に drive する receiver の判定は「訓練する repeater の数」で行う（旧は repeater の共通の capability の数の field で判定。probe が失敗した時に前の検出の値が残る場合だけ違う）。
- 違い: 訓練の debug の log の文は xd の独自の文（`xd-dp: …`）。error として数える場面（pattern を取らない、status が読めない、level を取らない、CR の回数切れ）は旧と同じ。
- eDP 1.4 の rate の表（§7 #9）: 読みと訓練の LINK_RATE_SET の道は xd にある。`drv_i915_lcd_compute` は eDP 1.4 の panel を今も EINVAL で断る（rate の表の DPCD 0x010 を読む入口が無い）: p038（screen の plan）で compute の入口を作る時に使う。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- eDP の訓練（HBR x2、TPS2）が通り、`xd-dp: the link is trained` の debug の行、`lease 1 claimed`、再訓練の log が無い。survey の DDI_BUF_CTL・TRANS_CONF と M/N が基準と同じ。
- 節目（ユーザーの一覧）: USB-C DP（TC2）の plug で 2 画面（外部 DP の訓練、repeater の検出）、HDMI の plug で 2 画面（p028〜p030 の HDMI の道）、5320 の起動。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p031/xd-host` → 全部 pass（dp 32、regs 625）、`I915_XD_STRICT` の検査は通る。
- dp の check: rate の共有と上限、RBR の補い、rate の code、eDP 1.4 の表、fallback 3 つ、link の選び 5 つ、5330 の panel の M/N（data 0x4b17e4/0x800000、link 0x42bfe/0x80000 = regs-selected.txt）、link status と repeater の形、5330 の panel（DPCD 1.1・HBR・2 lane）の訓練の DPCD の 9 access の順と source の hook と待ち（100×2 + 400 µs）、CR never（同じ要求 5 回、4 ms × 6）、EQ never（TPS3 を 5 回）、DPCD 1.4 の TPS4、top の level で諦める、全 lane 同じ level の source の上限、repeater 2 つ（transparent → 非透過、capability、2 → 1 → sink の順、source の level は repeater 2 だけ）、切り替えを断られた時、上がっている link の透過の repeater。
- `sh plan/ws245/tests/check-xd-regs-linux.sh build/ws229-p001/linux/linux-6.8.12 build/p031/linuxcheck` → 1089 対が一致、差 0。
- 旧の試験の比べ: `sh plan/ws245/temp/harness/oldtests.sh <tree> <out>` を段の前と後で → dp 72・lcd 124 pass、正規化した出力の diff は空。
- build（2026-10-16）: `sh build/tmp/kbuild.sh p031` で既定の vmunix と `I915_TESTS=y` の 5 組が rc 0・warning 0。`git diff --check` は空。
- 5330: 未実施（全部の段の後に 1 回）。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- i20 の debug の起動で `display run: debug: xd-dp: the link is trained`、lease 1 claimed、再訓練の log 0、survey の DDI_BUF_CTL・TRANS_CONF が基準と同じ

満たしていない・未確認の基準と残る物:

- M/N は survey の register に無い（M/N の書き込みは debug の比べの列で基準と同じ: 差は I・H の行だけ）
- USB-C DP・HDMI の 2 画面、5320: ユーザーの一覧（beta2.md の U19〜U23）の項目で、未実施

P1 の見立て（Status は変えない。判定は Q1）: cleared にしてよい。
