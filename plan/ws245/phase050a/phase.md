<!-- awesome-plan project=zedbsd record=ws245-p050a -->

# ws245-p050a: takeover の readout と sanitize を xd-takeover の record と stop に（takeover.c の registry の削除）

Status: cleared（2026-10-18 Q1 判定: q1229-i20・i21（5330 の実機）でこの段の「実機で確かめる事」の 5330 の項目を満たした（「判定の材料」の節、P1 q1241）。外部出力・suspend などの残りは beta2.md の U19〜U23 で追う）（旧: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない））
Parent: [WS245](../ws.md)
Queue: q1229-i06（P1）
設計: [design.md](../design.md) §6.2 の p050 の行（Q1 の判断 2026-10-16 23:0x で p050 を p050a〜d の 4 段に分けた: p050a takeover の readout/sanitize、p050b eDP の step、p050c HDMI と DP-TC の step、p050d 口 xd.h と旧 file の削除）、§2.14

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-display.c`・`xd.h` | `drv_i915_xd_takeover`: firmware の record の lit の pipe を順に `drv_i915_xd_takeover_stop` で止める。hook は xd の部品を直に呼ぶ: backlight（PP_CONTROL の backlight の enable を消し、firmware の T9 を待ち、PWM を duty 0 で止める）、DP の sink を D3（port の AUX の DPCD 0x600）、panel の電源（VDD を立ててから power・reset・VDD・backlight を一緒に落とし off を待つ）。HDMI の sink の scrambling は sink が自分で解くので触らない（旧も registry に HDMI の encoder が無く hook は空だった）。port・link の参照の返しは無い（INIT の参照が takeover の間すべての well と DC off を持つ: 参照を取らない）。panel の sequencer は VBT の backlight controller（無ければ powered の sequencer、それも無ければ 0）。`struct i915_xd_takeover_outcome` |
| `display/xd-aux.c/.h`、`intel/display-dpcd-xd.h` | native の DPCD の read/write（`drv_i915_xd_dpcd_read/_write`: header、16 byte ごと、ACK 以外は 32 回まで（timeout 以外は 500 µs 休む）、read は先に revision の 1 byte を読み捨てる）。DPCD の SET_POWER と native の AUX の request・reply の値（Values referenced） |
| `display/xd-takeover.c/.h`、`intel/display-regs-xd.h` | `drv_i915_xd_firmware_sanitize`: lit の pipe の pipe DMC（DMC がある時）、FBC A の off、lit の pipe が使わない port の clock の gate、lit の pipe が使わない PLL の off（lit の port の PLL が読めなければ PLL は残す）。DPFC_CONTROL の値 |
| `display/modeset.c`（殻） | `i915_resident_takeover` は `drv_i915_xd_takeover` を呼ぶだけ（旧の prepare・registry の readout・takeover・release を消した）。log: `i915: takeover: lit pipes 0x1, output panel (pipe A, DDI 0)`、`i915: takeover: pipe A stopped: DDI 0 mode 2, PLL 0, late 0x0 (rc 0)`、`i915: takeover: rc=0, crtcs stopped 1, still active 0x0` |
| `display/display.c` | init_nogem の readout と sanitize（旧 nogem の crtc・plane・encoder・DPLL の readout と sanitize）を `i915_display_sanitize`（xd の sanitize と、lit の pipe が無い時の power well の sanitize）に。`dprobe.active_crtcs` は xd の record の lit の pipe の数。log `i915: sanitize: lit pipes …, dmc pipes …, fbc off …, clocks gated …, PLLs off …, wells off …` |
| `display/power.c/.h` | `drv_i915_power_wells_sanitize`（旧 takeover.c の static を移した: 参照の無い well を落とす） |
| `display/takeover.c`・`takeover.h`・`takeover-internal.h` | 消した: registry（`struct i915_n1_registry`、world の `n1`・`tc_ms`）、`drv_i915_n1_*`（readout・takeover・release・walk の accessor・power の accessor・read_plane・check_ggtt・mirror_console（呼び手 0 だった））、`i915_modeset_setup_hw_state` と readout・sanitize・noatomic disable の static 全部、nogem の readout と sanitize（`drv_i915_modeset_readout_hw_state`・`_sanitize_hw_state`・`drv_i915_dpll_readout`・`drv_i915_nogem_dpll_sanitize_state`・`drv_i915_ddi_get_hw_state`・`_is_clock_enabled`・`_disable_clock`・`drv_i915_hsw_enabled_transcoders` と static）。takeover-internal.h は world（display を名指すだけ）と clock.c の宣言だけに。5699 → 1662 行 |
| `display/clock.c`・`ddi.c`・`pipe.c`・`plane.c`・`watermark.c`（殻）と header | 本番の呼び手が 0 になった readout の関数を消した: PLL の get_hw_state（`dpll_get_hw_state`・`get_freq`・ops 表の `.get_hw_state`）、`drv_i915_n1_dpll_readout_hw_state`、`drv_i915_dpll_sanitize_state`、DDI の get_hw_state・get_config・sync_state・get_power_domains・connector の hw state・`lcd_ms_bind_readout`・`bound_encoder/_connector`・`hsw_chicken_trans_reg`・`sanitize_encoder_pll_mapping`、pipe の get_pipe_config・transcoder の readout・m/n の readout・dotclock・`crtc_state_reset`・`encoder_get_config`・`set_plane_visible`・`plane_fixup_bitmasks`・`plane_disable_noatomic`・`skl_scaler_get_config`・`crtc_wait_for_next_vblank`・`lcd_ms_display_funcs`、plane の `skl_plane_get_hw_state`・`lcd_plane_disable_arm`、watermark の `skl_wm_get_hw_state`・sanitize・`bw_crtc_update`・`skl_ddb_*`・`lcd_wm_get_hw_state`。`clock.c` の `i915_planes_min_cdclk`（registry の plane の walk: registry の外では常に 0）を消し、`drv_i915_crtc_compute_min_cdclk`・`drv_i915_lcd_ms_cdclk_check`・`i915_skl_crtc_allocate_ddb` の takeover の引数を外した（crtc state は watermark の context の物） |
| `display/internal.h` | `struct i915_n1_report` を消した |
| `tests/display/host-xd-takeover-test.c` | +15 check（sanitize: lit の DDI A 以外の port の clock、DPLL1 の off と DPLL0 の保持、FBC、2 回目は何もしない、lit の TC の PLL が読めない時は PLL を残す。display の takeover: PWM の duty 0、sink の D3、panel の off、transcoder と DPLL0 の off、2 回目は何もしない、記述の無い device は ENODEV） |
| `tests/display/host-xd-aux-test.c` | +5 check（DPCD: 起こしの read と 2 byte、20 byte を 16+4、write の header、DEFER と NACK の再送、5 回答えない message の再送） |
| `plan/ws245/tests/run-xd-host-tests.sh` | 説明の行 |

段の phase.md の必須の項目:
- 消す file: 無し（takeover.c・takeover-internal.h は殻として残り p050d で消す。中身は 5699 → 1662 行、763 → 99 行）。
- 消す関数とまだ残る呼び手: 上の表の関数は本番の呼び手 0（`build/tmp/deadexp.py` で前の段（3429a6af6）の木と比べ、新しく呼び手 0 になった export を全部消した。残った物は前の段から呼び手 0 だった物で p050d で file ごと消える）。
- 殻が更新する旧の写し: 無し（registry が無くなった。PLL の pool と DBUF の状態は takeover の後に空に戻す: 今のまま）。
- 表 R: GOP の出力の止め方の書き手は xd-takeover と xd-display の hook だけ（旧の registry の encoder の hook は無くなった）。init の sanitize の書き手は xd-takeover（pipe DMC・FBC・clock・PLL）と power.c（well）。
- R11: 触った file の開発の管理情報は全部 0。

## 旧と同じに保った物と違い

- 確かめ方: `sh build/tmp/refcmp.sh 3429a6af6 p050a` で旧の host 試験（dp 72・lcd 120）の出力が同じ（どちらも takeover を通らない）。takeover と sanitize の列は host-xd-takeover-test。
- takeover の違い: (1) 旧は registry の readout が encoder の状態（PPS・link）を読んでから hook で旧の state を通して止めた。今は xd の部品を直に呼ぶ（書く register は同じ: PP_CONTROL の backlight の bit、PWM の CTL と DUTY、AUX の DPCD 0x600、PP_CONTROL の VDD と power）。(2) 旧は readout が取った power の参照（DDI IO・AUX・pipe）を返した。今は取らない（INIT の参照が持つ）。(3) backlight の off の待ちは firmware が PP の register に入れた T9（旧は readout が選んだ値: VBT と firmware の大きい方）。(4) 旧の readout が走らせた sanitize（registry の crtc の）は無い。init の sanitize が同じ事をする。
- sanitize の違い: (1) pipe DMC の enable は DMC の payload がある pipe だけ（旧は無条件に PIPEDMC_CONTROL_A を rmw。Linux は payload がある時だけ）。init の時は DMC がまだ載っていないので、書かない見込み → digest に差（PIPEDMC_CONTROL_A の 1 書き込み減）。(2) clock の gate は platform の port の全部（旧は VBT の output の encoder だけ）。5330 では起動時に灯っていない port の clock は gate 済みの見込みで書き込みは同じ（実機で確かめる）。(3) ADL-P A0 の CMTG の WA は無い（A0 は支えない: §7 #14）。(4) 旧の readout の log（`[CRTC:A] hw state readout`・`[ENCODER port A] …`・`DPLL 0 hw state readout`・`i915: readout: …`）は無くなり、`i915: sanitize:` の 1 行に。
- 設計との差: p050 の「readout を扱う段はこの段だけ」のうち、`setup_outputs`（nogem の encoder の表: hotplug.c・tc-kern.c が使う）・nogem の front（WM の latency・SAGV・crtc の表・GMBUS の pin・VGA）・VGA の client は p050d（口と旧 file の削除）か、使う側の段（hotplug は p039、TC は p052）まで残す。takeover world は display を名指すだけの殻として p050d まで残す（旧の modeset の関数の引数）。

## 実機で確かめる事（5330、全部の段の後の 1 回で。ユーザーが居る時間に）

- `i915: takeover: lit pipes 0x1, output panel (pipe A, DDI 0)`、`i915: takeover: pipe A stopped: DDI 0 mode 2, PLL 0, late 0x0 (rc 0)`、`i915: takeover: rc=0, crtcs stopped 1, still active 0x0`。sink の D3 の失敗の行（`the sink did not go to sleep`）・panel の off の失敗の行が無い。
- `i915: sanitize: lit pipes 0x1, dmc pipes 0x0, fbc off 0, clocks gated 0x0, PLLs off 0x0, wells off 0`（GOP が DDI A だけを灯している時の見込み。違えばその値）。
- GOP の画面 → desktop の切り替え（黒の時間が延びない、目視はユーザーの一覧）。panel が点く（survey の PP_STATUS・PWM・frame counter）。LCD-B の preflight が通る（`DDI / PLL idle=1`）。
- digest の差は takeover と sanitize の部分（PIPEDMC_CONTROL の 1 書き込み、PPS・PWM の書き込みの順）。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p050a/xd-host` → 全部 pass（takeover 71、aux 49）、`I915_XD_STRICT` の検査は通る。
- 旧の host 試験: `sh build/tmp/refcmp.sh 3429a6af6 p050a` → dp・lcd とも SAME。
- build（2026-10-16）: `sh build/tmp/kbuild.sh p050a` で 6 組が rc 0・warning 0。`git diff --check` は空。
- 5330: 未実施（全部の段の後に 1 回）。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- `takeover: lit pipes 0x1, output panel (pipe A, DDI 0)`、`pipe A stopped: … PLL 1 …(rc 0)`（例の PLL 0 は誤り）、`rc=0, crtcs stopped 1, still active 0x0`、`the sink did not go to sleep` 0（q1229-i19 の直し 26b307bd9 の後）、`sanitize: lit pipes 0x1, dmc pipes 0x0, fbc off 0, clocks gated 0x0, PLLs off 0x0, wells off 0`（見込みどおり）、preflight の `DDI / PLL idle=1`、digest の差は I2〜I4

満たしていない・未確認の基準と残る物:

- GOP → desktop の黒の時間の目視: ユーザーの一覧（beta2.md の U19〜U23）の項目で、未実施

P1 の見立て（Status は変えない。判定は Q1）: cleared にしてよい。
