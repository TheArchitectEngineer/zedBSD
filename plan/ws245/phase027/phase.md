<!-- awesome-plan project=zedbsd record=ws245-p027 -->

# ws245-p027: CDCLK（xd-clock）

Status: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない）
Parent: [WS245](../ws.md)
Queue: q1229-i02（P1、ws245-p024〜p046 を design の順に実装して branch に積む）
設計: [design.md](../design.md) §6.2 の p027 の行、§2.6、§6.7（保存した `xd-clock.h` と patch の CD2X の divider の code）

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-clock.c/.h`（新、Zlib） | CD clock: `struct i915_xd_cdclk`（ref・vco・cdclk・bypass・voltage、kHz）、`struct i915_xd_cdclk_report`（始めに見た state と保った／替えた、求めた state、firmware の prepare と voltage の答え、書いたか、PLL の待ちの遅れ）。`drv_i915_xd_cdclk_read`（DSSM の reference、PLL が on かつ lock なら ratio × ref、bypass は ref/2、CD2X の 1/1.5/2/4、voltage は platform の段）・`_voltage`・`_lowest`（表の ref の最低の cdclk ≥ min）・`_vco`・`_legal`（PLL on、bypass でない、表の cdclk と vco、CDCLK_CTL の divider と decimal（pipe の field は見ない））・`_set`（firmware の prepare（3 ms は 10 µs おき、さらに 50 ms）→ PLL（display 13 は crawl、他と停止・不明の PLL は off → on）→ CDCLK_CTL（pipe 無し）→ voltage の通知 → 読み直し）・`_init`（保てれば保ち、だめなら表の最低に）・`_uninit`（bypass、PLL off）・`_max`（24 MHz は 648000、他 652800）。firmware の mailbox は xd-io の `drv_i915_xd_io_mailbox`（GT と同じ sideband の lock）。手順は block ごとに `Procedure referenced:`（intel_cdclk.c の bxt_get_cdclk・bxt_de_pll_readout・icl_readout_refclk・tgl_calc_voltage_level・calc_voltage_level・bxt_calc_cdclk・bxt_calc_cdclk_pll_vco・bxt_sanitize_cdclk・bxt_set_cdclk・_bxt_set_cdclk・icl_cdclk_pll_update・icl_cdclk_pll_disable・icl_cdclk_pll_enable・adlp_cdclk_pll_crawl・bxt_cdclk_cd2x_div_sel・skl_cdclk_decimal・bxt_cdclk_init_hw・bxt_cdclk_uninit_hw・intel_update_max_cdclk、intel_pcode.c の skl_pcode_request・skl_pcode_try_request）、待ちの時間と最大の cdclk は `Values referenced:` |
| `display/clock.c`（殻） | CDCLK の公開の関数（`drv_i915_init_cdclk_hooks`・`_bxt_calc_cdclk`・`_bxt_calc_cdclk_pll_vco`・`_bxt_get_cdclk`・`_update_cdclk`・`_bxt_set_cdclk`・`_cdclk_init_hw`・`_cdclk_uninit_hw`・`_max_cdclk_freq`）の中身を xd-clock に。殻に残る物: 旧の `struct i915_cdclk_config` と xd の state の変換、report を今と同じ文の log と `cd->diag_*` に写す `i915_cdclk_report`。削除: `drv_i915_adlp_cdclk_table`・`_icl_cdclk_table`・`_tgl_calc_voltage_level`・`drv_i915_bxt_sanitize_cdclk`（呼び手は clock.c の中だけ）、static の 16 個（i915_divrc・i915_hweight16・i915_cd_calc_voltage_level・i915_cdclk_calc_voltage_level・i915_skl_cdclk_decimal・i915_bxt_cdclk_cd2x_div_sel・i915_cdclk_squash_waveform・i915_cdclk_pll_is_unknown・i915_icl_readout_refclk・i915_bxt_de_pll_readout・i915_icl_cdclk_pll_disable/_enable/_update・i915_adlp_cdclk_pll_crawl・i915_bxt_program_cdclk・i915_bxt_cdclk_is_legal・i915_bxt_cdclk_init_hw）と CDCLK の define。4076 → 3519 行 |
| `display/clock.h` | 上の宣言を整理、`drv_i915_init_cdclk_hooks(cd, xd)` |
| `intel/clock.h` | core の CDCLK の表（`i915_icl_cdclk_table`・`i915_adlp_cdclk_table`）を削除（mode set の device の表 `icl_cdclk_table`・`adlp_cdclk_table` は p038 まで残る）。R11: `ws051-p003` を消した |
| `display/internal.h` | `struct i915_cdclk_vals` と `enum i915_cdclk_funcs` を削除、`struct i915_cdclk_dev` の `table`・`funcs` を `struct i915_xd *xd` に |
| `display/display.c` | core init の stepping の計算をやめ、`drv_i915_init_cdclk_hooks(&display->cdclk, &display->xd)` |
| `intel/display-regs-xd.h`・`tests/display/xd-regs-map.h` | CD2X の divider の code 1.5（1）と 4（3）（`Values referenced:` BXT_CDCLK_CD2X_DIV_SEL_1_5・_4。§6.7 の patch と同じ値）と 2 つの対 |
| `platform/amd64/vmunix.mk` | `xd-clock.c` |
| `tests/display/host-xd-clock-test.c`（新） | 18 の check（下） |
| `plan/ws245/tests/run-xd-host-tests.sh` | `host-xd-clock-test` を足した |

段の phase.md の必須の項目:
- 消す file: 無し（`clock.c` は DPLL の部分が p028・p040 まで残る）。
- 消す関数と残る呼び手: 上の公開 4・static 16（`git grep` で clock.c の外に呼び手 0）。残る殻の関数の呼び手: `drv_i915_cdclk_init_hw`・`_uninit_hw`（power.c の core init/uninit）、`drv_i915_bxt_get_cdclk`（power.c の DC_off の on）、`drv_i915_update_cdclk`（takeover.c:1132）、`drv_i915_max_cdclk_freq`（modeset.c:2452）、`drv_i915_init_cdclk_hooks`（display.c）。
- 殻が更新する旧の写し: `display->cdclk.hw`（ref・vco・cdclk・bypass・voltage_level）、`diag_observed_before`・`diag_sanitized`・`diag_no_change`・`diag_requested`・`diag_prepare_status`・`diag_notify_status`・`diag_hw_sequence_reached`。`display_ver`・`has_cdclk_crawl`（xd の platform から）。
- 表 R: CDCLK_CTL・CDCLK の PLL の書き手は xd-clock だけ。mode set の state.c の cdclk の global obj は計算だけで書かない（p038 まで殻）。GT driver mailbox は xd-io（GT の pcode と同じ `gt->sb_lock`）。
- hook と lock: 呼び手は旧のまま（core init・suspend の段）。mailbox は 1 命令ごとに sideband の lock を取る（旧の `drv_i915_skl_pcode_request` は再試行の全体で lock を持っていた: GT の RPS の命令が間に入り得る。各命令は完結するので問題は無い見込み）。
- R11: 触った file は全部 0（intel/clock.h は 1 → 0）。

## 旧と同じに保った物と違い

- 確かめ方 1: 旧の host 試験 `host-dp-test`（72）・`host-lcd-modeset-test`（123）の出力は p024 の後と同じ。
- 確かめ方 2（scratch の harness、git に入れない: `plan/ws245/temp/harness/wells27.c`・`run-wells27.sh`。旧の tree は `PCW=",--wrap=drv_i915_skl_pcode_request,--wrap=drv_i915_snb_pcode_write"` で pcode を数えるだけにする）: 5330 の GOP の state（38.4 MHz、PLL ratio 10、192 MHz）での init（保つ）、decimal を壊した init（替える）、307.2 MHz への set、uninit、PLL off からの init を、段の前（76fefb632 の `git archive`）と後で比べた。**ADL-P・TGL とも CDCLK の PLL と CDCLK_CTL の書き込みの列が全部一致**、firmware への要求は旧 8 回（pcode）と新 8 回（mailbox の命令）で同じ。
- 違い（診断だけ）: `diag_no_change` は init ごとに決まる（旧は一度 1 になると残っていた）。
- 違い: firmware の prepare の再試行は 10 µs おきに 3 ms + 50 ms の時間を数えて行う（旧は時刻で 3 ms、その後 50 ms を preemption を止めて）。preemption は止めない。
- 違い: RPL-U（480 MHz の行）は xd の platform の表で正しく扱う（旧は ADL-P の表）。
- 違い: CD2X の divider の不明な値は 1 として読む（旧は早く返して cdclk を前の値のまま残した。2 bit の field に不明な値は無い）。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- `cdclk=192000 vco=…` の log が同じ（GOP の値を保つ。`sanitizing cdclk programmed by pre-os` が出ない）、`PCU prepare failed`・`PCODE freq set failed` が無い。
- 節目（ユーザーの一覧）: suspend → resume（p025〜p027 の DC・DMC・CDCLK の uninit/init の道）、5320 の起動（TGL の relock）。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p027/xd-host` → regs 498・io 50・platform 89・vbt 137・survey 57・aux 43・gmbus 24・panel 47・power 65・dmc 32・clock 18、失敗 0。`I915_XD_STRICT` の検査は通る。
- clock の check: 5330 の GOP の 192 MHz を読み保つ、pipe B を待った変更でも保つ、decimal の誤りは替える、1.5 の divider の 179.2 MHz、lock の無い PLL は bypass で替える、24 MHz の reference。init: 保つ時は書かず firmware にも聞かない、替える時は PLL off → ratio → on → CDCLK_CTL 0x00780164 と prepare 1 回・voltage 1 回（level 0）。ADL-P の crawl（0x80000010 → 0x80800010 → 0x80000010 → CDCLK_CTL 0x00380264）、TGL の relock（0x4000000a → 0x10 → 0x80000010 → 0x00380264）、uninit（PLL off → 0x00380024）。拒み: ready にならない firmware に 5301 回聞いて何も書かない（ETIMEDOUT）、voltage の拒みで hardware は変わり cdclk は読み直さない。voltage の段（TGL 312000→0、326400→1、556800→2、652800→3、上は 3）、表の最低と vco、最大（38.4 で 652800、24 で 648000）。
- `sh plan/ws245/tests/check-xd-regs-linux.sh build/ws229-p001/linux/linux-6.8.12 build/p027/linuxcheck` → 1019 対が一致、差 0。
- 旧の host 試験と harness: 上。
- build: 下。
- 5330: 未実施（全部の段の後に 1 回）。

## build

- 2026-10-16: `sh build/tmp/kbuild.sh p021` で既定の vmunix と `I915_TESTS=y` の 5 組が rc 0・warning 0。`git diff --check` は空。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- `display: core started: cdclk=179200 vco=537600` と `CD clock: cdclk=179200` が基準（`cdclk=179200 vco=537600`）と同じ（基準の文の 192000 は例の誤り、5330 の GOP は 179200）、`sanitizing cdclk`・`PCU prepare failed`・`PCODE freq set failed` 0

満たしていない・未確認の基準と残る物:

- suspend → resume、5320 の relock: ユーザーの一覧（beta2.md の U19〜U23）の項目で、未実施

P1 の見立て（Status は変えない。判定は Q1）: cleared にしてよい。
