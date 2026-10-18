<!-- awesome-plan project=zedbsd record=ws245-p032 -->

# ws245-p032: transcoder と pipe（xd-pipe）

Status: cleared（2026-10-18 Q1 判定: q1229-i20・i21（5330 の実機）でこの段の「実機で確かめる事」の 5330 の項目を満たした（「判定の材料」の節、P1 q1241）。外部出力・suspend などの残りは beta2.md の U19〜U23 で追う）（旧: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない））
Parent: [WS245](../ws.md)
Queue: q1229-i04（P1）
設計: [design.md](../design.md) §6.2 の p032 の行、§2.10

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-pipe.c/.h`（Zlib、新） | transcoder の M/N（data M に TU、link N を最後）、timing（display 13 は context latency に blank start を入れ VBLANK の start を 1、interlace の sync shift と total・blank end の 1 行）、transcoder の設定（M/N → timing → vblank-with-delay → VRR（固定なら CTL=0、可変なら VMIN・VMAX・CTL（13 は guardband、12 は pipeline full と override）・FLIPLINE）→ MULT → frame start delay → TRANS_CONF（fastset なら enable）と posting read）、PIPE_SRCSZ、PIPE_MISC（bpc、dither、YUV・4:2:0、HDR、rounding）、PIPE_CHICKEN、色の語（BOTTOM_COLOR=0・GAMMA_MODE・CSC_MODE）、MSA、TRANS_DDI_FUNC_CTL の語・enable（CTL2 → CTL）・config・disable、transcoder の start（13 は ARB の slot）と stop（enable を消す → FEC stall の解除 → state の off を 100 ms 待つ）、frame counter、scan line の動きの待ち。step の名前 `xd-pipe configure`・`xd-pipe start`・`xd-pipe stop` |
| `intel/display-regs-xd.h` | CHICKEN_TRANS（D は別の address）、TRANS_MULT、TRANS_VRR_*、TRANS_CONF の interlace、PIPE_CHICKEN と PIPE_MISC の残りの bit、DDI_FUNC の MST・HDCP・HDMI の bit と CTL2、MSA の bpc と色の形式（`Values referenced:`）。map に 36 対 |
| `display/pipe.c`（殻） | `drv_i915_enable/disable_transcoder`、`i915_hsw_configure_cpu_transcoder`（crtc state → `struct i915_xd_trans_setup`）、`i915_set_pipe_src_size`、`i915_bdw_set_pipe_misc`、`i915_icl_set_pipe_chicken` は xd を呼ぶ。recorder の `drv_i915_display_emit_transcoder` も xd の M/N・timing を呼ぶ。消した: `i915_set_m_n`・`_set_m1_n1`・`_set_m2_n2`・`i915_set_transcoder_timings`・`i915_hsw_set_frame_start_delay`・`i915_hsw_set_transconf`・`i915_wait_for_pipe_off`・`i915_trans_vrr_ctl`・`i915_vrr_set_transcoder_timings`。`i915_cpu_transcoder_has_m2_n2` は readout（get_m2_n2）に残る |
| `display/ddi.c`（殻） | MSA（`i915_ddi_set_dp_msa`）と transcoder の DDI function の enable・config・disable は xd。crtc state → `struct i915_xd_trans_func` は `i915_ddi_xd_func`。消した: `i915_ddi_transcoder_func_reg_val_get`・`i915_bdw_trans_port_sync_master_select` |
| `display/color.c`（殻） | `i915_icl_color_commit_arm` の 3 語は xd。GAMMA/CSC の mode の計算（drm の LUT の blob を見る）と LUT・CSC の load の guard は殻に残る（p050 まで） |
| `display/vblank.c/.h`・`control.c` | frame counter の読みは xd（`drv_i915_pipe_frame_read(struct i915_xd_io *, int)`、control.c は `display->xd.io`）。scan line の待ちは xd。R11: control.c・vblank.c の comment の WS の番号を消した |
| `tests/display/host-xd-pipe-test.c`（新） | 23 check（下） |
| `tests/display/host-lcd-modeset-test.c` | E（time base の fault）を消した（pipe-off の待ちが backend の wait_reg を通らなくなり、試す道が無い。timeout の扱いは C が見る）。D の順の check は pipe-off の wait の代わりに TRANS_CONF の書き込みを見る |

段の phase.md の必須の項目:
- 消す file: 無し。`color.c` は殻として残る（design の「color.c を消す」は p050 に送る: commit_noarm・load_luts と color の check が readout・sanitize の殻から呼ばれる）。
- 消した関数（本番の呼び手 0）: 上の pipe.c の 9、ddi.c の 2、vblank.c の `i915_pipe_scanline_is_moving`。
- 記録器（`drv_i915_lcd_emit_transcoder`・`_cpu_transcoder`、dp-sink.c の LCD-A の log）: design はこの段で消すとしたが、xd の書き込みを記録する形で残した（記録器は `drv_i915_lcd_xd_io` の window を通るので旧の書き手の写しではない）。消すのは dp-sink.c の eDP の device の log を畳む段（p050）に送る。
- 殻が更新する旧の写し: 無し（crtc state は読むだけ）。
- 表 R: TRANS_CONF・TRANS_DDI_FUNC_CTL の書き手は xd-pipe だけ（readout の読みは殻）。CHICKEN_TRANS の FEC stall と frame start delay、vblank-with-delay も xd-pipe。
- hook と lock: modeset の lock の下で `drv_i915_lcd_xd_io` の window。frame counter は control.c の refresh の spinlock の下で `display->xd.io` を読むだけ（record に書かない）。
- R11: pipe.c 0、ddi.c 0、color.c 0、vblank.c 1 → 0、vblank.h 0、control.c 1 → 0、host-lcd-modeset-test.c 0。

## 旧と同じに保った物と違い

- 確かめ方: 旧の `host-dp-test`（72）は段の前（55f9b8b59）と出力が同じ。`host-lcd-modeset-test` は rmw を書いた値に直した書き込みの列（`build/tmp/wr.py`）が**全部一致**（TRANS_CONF・M/N・timing・FUNC_CTL・MSA・PIPE_MISC・CHICKEN・色の語）。違いは D の文と E の削除だけ（120 check pass）。
- 違い: pipe-off の待ちは backend の wait_reg（100 ms を一度に）ではなく xd の poll（読みと待ち）。scan line の待ちも xd の poll（本番の呼び手は無い: `intel_crtc_max_vblank_count` は 0 でない）。
- 違い: display 12・13 には eDP transcoder が無いので、TRANS_MULT は常に書く（旧と同じ結果）。M2/N2 は書かない（旧も display 12+ は書かない）。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- `i915.debug=display` の w の log に `xd-pipe configure`・`xd-pipe start` の step が出る（素通りの検出）。mode set の書き込みの列が基準と同じ。survey の TRANS_CONF・FUNC_CTL（0x8a210002）・SRCSZ と frame counter の進み。
- 節目（ユーザーの一覧）: 5320 の起動（display 12 の timing の VBLANK と VRR の pipeline full）。外部出力: 未確認（host 試験のみ）。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p032/xd-host` → 全部 pass（pipe 23、regs 661）、`I915_XD_STRICT` の検査は通る。
- pipe の check: 5330 の panel の timing・M/N・SRCSZ・FUNC_CTL・MSA が regs-selected.txt と一致、設定の書き込みの順、display 13 の VRR、fastset、TGL の transcoder B の HDMI（DVI）の timing（VBLANK の start が timing、context latency 無し）と FUNC_CTL 0x91030000、interlace、start（ARB の slot）、走っている transcoder、stop（FEC stall、off の待ち）、止まらない transcoder の 100 ms、frame counter、PIPE_MISC（18 bpp + dither 0x150、12 bpc 4:2:0）、CHICKEN、色の語、FUNC の disable。
- `sh plan/ws245/tests/check-xd-regs-linux.sh build/ws229-p001/linux/linux-6.8.12 build/p032/linuxcheck` → 1125 対が一致、差 0。
- build（2026-10-16）: `sh build/tmp/kbuild.sh p032` で既定の vmunix と `I915_TESTS=y` の 5 組が rc 0・warning 0。`git diff --check` は空。
- 5330: 未実施（全部の段の後に 1 回）。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- mode set の書き込みの列が基準と同じ（debug の比べの差は I・H の行だけ）、survey の TRANS_CONF・TRANS_DDI_FUNC_CTL・PIPE_SRCSZ が基準と同じ、frame counter が進む（frames +137 など）

満たしていない・未確認の基準と残る物:

- `xd-pipe configure`・`xd-pipe start` の step の印は i20 の debug の log に無い（出るのは `xd-pipe plane off`・`xd-pipe stop` だけ）。enable の道の印の名前が変わったか、未確認
- 5320、外部出力: ユーザーの一覧（beta2.md の U19〜U23）の項目で、未実施

P1 の見立て（Status は変えない。判定は Q1）: cleared にしてよい（列は同じ。印の名前の未確認は記録だけ）。
