<!-- awesome-plan project=zedbsd record=ws245-p035 -->

# ws245-p035: display の init/uninit の列（xd-core、司令）

Status: cleared（2026-10-18 Q1 判定: q1229-i20・i21（5330 の実機）でこの段の「実機で確かめる事」の 5330 の項目を満たした（「判定の材料」の節、P1 q1241）。外部出力・suspend などの残りは beta2.md の U19〜U23 で追う）（旧: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない。design は「ユーザーが居る時間に流す」段））
Parent: [WS245](../ws.md)
Queue: q1229-i04（P1）
設計: [design.md](../design.md) §6.2 の p035 の行、§2.2

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-core.c/.h`（Zlib、新。design の「xd-power の init」を独立の file にした: xd-power・xd-ddi・xd-dbuf を順に呼ぶ司令なので） | start: DC の clear → TGP/ADP の south で DPMG の clock gate off → PCH の reset handshake → combo PHY → fault の検査 → power well 1（hook）→ CDCLK（hook）→ fault の検査 → display 12 の DBUF の tracker service 8 → DBUF（firmware の slice と S1、hook で電源）→ display 12 の ABOX の credit（ABOX0 を含む）→ BW_BUDDY（memory の種類と channel、未知なら disable、display 12 は TLB timer）→ CHICKEN_DCPR_2 の 4 bit → display 13 の ERR_FATAL_MASK。stop: DC を出る → DBUF off → CDCLK down → PW1 off → combo PHY down。step `xd-core start`・`xd-core stop`。`Values referenced:`（tracker、ABOX の credit、TLB timer）と `Procedure referenced:`（intel_display_power.c の icl_display_core_init/uninit・intel_pch_reset_handshake・gen9_dbuf_enable） |
| `display/power.c`（殻） | `i915_icl_display_core_init/uninit` は xd-core を呼ぶ（hook: PW1 の on/off は domains の lock の下の well の enable/disable、CDCLK は `drv_i915_cdclk_init/uninit_hw`、DBUF は `i915_gen9_dbuf_slices_update`、fault は `drv_i915_time_base_faulted`）。memory の種類は own の enum から xd の値に。消した: `i915_pch_reset_handshake`・`i915_gen12_dbuf_slices_config`・`i915_gen9_dbuf_enable`・`i915_icl_mbus_init`・`i915_tgl_bw_buddy_init`・`i915_power_rmw`、own の buddy の表と 28 の macro |
| `tests/display/host-xd-core-test.c`（新） | 8 check（下） |

段の phase.md の必須の項目:
- 消す file: 無し。
- 消した関数（本番の呼び手 0）: 上の 6 と表。`i915_combo_phys_up` は DC_off の後の PHY の戻しに残る。
- 殻が更新する旧の写し: `dc->dbuf_enabled_slices`（slice の hook）、`dc->last_child`・`fault_where`（止まった所）、trace の ring の PHY の数。
- 表 R: DBUF_CTL の tracker service は core の start（xd-core）と mode set の MIN_TRACKER（xd-dbuf）で field が別。
- hook と lock: core の start は domains の lock の外で呼ばれ、PW1 と DBUF の hook が lock を取る（旧と同じ）。
- R11: power.c 0。

## 旧と同じに保った物と違い

- 確かめ方: 旧の host 試験（`host-dp-test` 72、`host-lcd-modeset-test` 120）は core の init を通らない（lcd 試験の DBUF は hook の model）。順は旧の `i915_icl_display_core_init` と同じ（PHY → PW1 → CDCLK → tracker → DBUF → ABOX → buddy → DCPR_2 → ERR_FATAL）で、host の xd-core 試験で見る。
- 違い（Linux を優先）: ADP/TGP の south で SOUTH_DSPCLK_GATE_D の DPMG の clock gate を off（Wa_14011294188。旧は書かなかった。**5330 でも書く**）。ERR_FATAL_MASK は display 13 だけ（旧は TGL でも書いた）。BW_BUDDY の表は xd の表（ADL-S・RKL A0 の Wa_1409767108 の表を含む。ADL-P・TGL は旧と同じ値）。DBUF の slice は platform の slice だけを読む・書く（TGL で S3・S4 を触らない）。
- 違い: tracker・ABOX・buddy の log の行（`gen12_dbuf_slices_config:`・`icl_mbus_init:`・`BW_BUDDY:`）は出さない。
- 残り（design の p035 の行のうち、この段で動かさなかった物）: `takeover.c` の `drv_i915_display_wa_apply` と `drv_i915_display_nogem_front` の hardware の部分（max cdclk、HTI、GMBUS の reset）、raw clock、DDI A の lane の読み、WM の latency の mailbox: takeover の段（p036・p037）で。`display.c` の `i915_display_core_init` は power.c の init_hw を呼ぶだけで変えていない。

## 実機で確かめる事（5330、全部の段の後の 1 回で。ユーザーが居る時間に）

- 起動の書き込みの列（digest、差があれば w の列の順）が §2.2: `xd-core start` の step、SOUTH_DSPCLK_GATE_D の bit 15 が立つ（新しい差）、BW_BUDDY の page mask、DCPR_2、ERR_FATAL_MASK。
- 節目: suspend → resume（init/uninit は resume の道）、5320 の起動（TGL の tracker 8・ABOX・buddy の TLB timer、ERR_FATAL を書かない、DBUF の S3/S4 を触らない）。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p035/xd-host` → 全部 pass（core 8）、`I915_XD_STRICT` の検査は通る。
- core の check: ADL-P の start の hook の順（PW1 → CDCLK → DBUF の slice 0x3）、ADP の clock gate・handshake・LPDDR4 x4 の buddy 0x38（0・1）・DCPR_2・ERR_FATAL、display 13 の tracker と ABOX 無し、TGL（ICP: clock gate 無し、tracker 8 を 2 slice、ABOX 0〜2 の 0x00111010、DDR4 x2 の buddy 0x1f と TLB timer、ERR_FATAL 無し）、未知の memory で buddy の disable、PHY の後の fault、PW1 の拒み、stop の順。
- build（2026-10-16）: `sh build/tmp/kbuild.sh p035` で 6 組が rc 0・warning 0。`git diff --check` は空。
- 5330: 未実施（全部の段の後に 1 回）。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- debug の log に `w step xd-core start`、SOUTH_DSPCLK_GATE_D の bit 15（c2020=00008000、I1 で意図）、他の差は I・H の行だけ

満たしていない・未確認の基準と残る物:

- suspend → resume、5320: ユーザーの一覧（beta2.md の U19〜U23）の項目で、未実施

P1 の見立て（Status は変えない。判定は Q1）: cleared にしてよい。
