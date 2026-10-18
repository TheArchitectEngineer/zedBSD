<!-- awesome-plan project=zedbsd record=ws245-p022 -->

# ws245-p022: panel power（PPS、xd-panel）

Status: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない）
Parent: [WS245](../ws.md)
Queue: q1229-i01（P1、ws245-p022〜p046 を design の順に実装して branch に積む）
設計: [design.md](../design.md) §6.2 の p022 の行（改訂 6）、§2.11・§3.2 の xd-panel

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-panel.c/.h`（Zlib） | PPS（新）: `drv_i915_xd_pps_usable`（sequencer の数と、ICP〜ADP の 2 つ目は SOUTH_CHICKEN1 の bit）、`_find`（panel on → VDD → any の順の firmware の選び）、`_powered`・`_vdd_forced`、`_delays_set`、`_read_delays`（PP_ON/OFF_DELAYS と PP_CONTROL の cycle の field）、`_spec`（eDP 1.3 の上限）、`_from_vbt`（VBT の cycle に 100 ms）、`_choose`（大きい方か spec、ms の待ち、sequencer へは backlight の 2 つを 1 と cycle を 100 ms に切り上げ）、`_write_delays`（ON → OFF → cycle は変わる時だけ）、`_vdd_on`（VDD を立て posting read、panel の電源の有無を返す）、`_vdd_off`（書いた control を返す）、`_wait_cycle`（cycle の終わりを 5 s 待つ）、`_power_on`（**delay を毎回書く**（design §2.11、レビュー 2 の m12）→ DPLS の clock gate を外す → POWER_ON|RESET → posting → on かつ idle を 5 s 待つ → gate を戻す）、`_power_off`（ON・RESET・VDD・BLC を一緒に落とし off を 5 s 待つ）、`_backlight`（BLC enable の set/clear）。register は xd-io だけで触る。手順は block ごとに `Procedure referenced:`（Linux v6.8.12 intel_pps.c の intel_pps_is_valid・bxt_initial_pps_idx・pps_has_pp_on・pps_has_vdd_on・pps_any・intel_pps_readout_hw_state・pps_init_delays_vbt・pps_init_delays・pps_init_registers・intel_pps_vdd_on_unlocked・intel_pps_vdd_off_sync_unlocked・wait_panel_power_cycle・wait_panel_status・intel_pps_on_unlocked・wait_panel_on・intel_pps_off_unlocked・wait_panel_off・intel_pps_backlight_on/off）、spec の値は `Values referenced:`（pps_init_delays_spec） |
| `display/panel.c`（殻） | 公開の関数（`drv_i915_pps_*`・`drv_i915_edp_emit_panel`）は名前と型を保ち、register の部分を xd-panel に。殻に残る物: PPS の lock（DISPLAY_CORE の参照と mutex）、VDD の AUX の参照（`vdd_wakeref`）、`want_panel_vdd`、遅延の VDD off の work と schedule、時刻（`panel_power_off_time`・`last_power_on`・`last_backlight_off`）と時刻からの残りの sleep（power cycle の残り、VDD の後の power-up delay、backlight の on/off の delay）、sequencer の選び（VBT → xd の find）、quirk の T12、log の文。削除: VLV/CHV・BXT/GLK・ILK・pre-DDI の死んだ枝（`i915_bxt_power_sequencer_idx`・PP_DIVISOR の枝・unlock key・`force_disable_vdd`）、register の helper（`i915_pps_get_registers`・`i915_pp_ctrl_reg`・`i915_pp_stat_reg`・`i915_ilk_get_pp_control`・`i915_pps_has_pp_on/_has_vdd_on/_any`・`i915_bxt_initial_pps_idx`・`i915_pps_is_valid`・`i915_pps_readout_hw_state`・`i915_pps_delays_valid`・`i915_pps_init_delays_bios/_vbt/_spec`・`i915_pps_pick_delay`・`i915_wait_panel_status/_on/_off`）。2112 → 1468 行 |
| `display/internal.h` | `struct i915_dp_env` の `aux_io` と kernel の `aux_io` の comment を「AUX と PPS の窓、PPS の mutex の下」に（窓は AUX の transfer と共用: AUX の transfer は PPS の lock を取る） |
| `intel/display-regs-xd-south.h` | PP_STATUS の sequence state（3:0、OFF_IDLE 0・ON_IDLE 8）、PP_CONTROL の power cycle（8:4）、SOUTH_DSPCLK_GATE_D の DPLS の bit 29 を足した（`Values referenced:` に名前を足した） |
| `tests/display/xd-regs-map.h` | 上の 5 つの対 |
| `tests/display/host-xd-panel-test.c` | PPS の 26 の check（下） |
| `plan/ws245/tests/run-xd-host-tests.sh` | panel の試験の説明に PPS を足した |

段の phase.md の必須の項目:
- 消す file: 無し（`panel.c` は殻として残る。PPS の呼び手（`dp-sink.c`・`aux.c`）が消える段で消える）。
- 消す関数と残る呼び手: 上の panel.c の static 19 個（呼び手は panel.c の中だけ → 0。`git grep` で panel.c の外に無い）。公開の関数は全部残る。
- 殻が更新する旧の写し: `intel_dp->pps` の `pps_idx`・`pps_delays`・`bios_pps_delays`・`panel_power_up_delay`・`backlight_on_delay`・`backlight_off_delay`・`panel_power_down_delay`・`panel_power_cycle_delay`・`vdd_wakeref`・`want_panel_vdd`・時刻。`dp-sink.c:1772` が `panel_power_up_delay` を読む。旧の host 試験 `host-dp-test`（200 ms の power-up delay・T12 の後の 800 ms の sleep）と `host-lcd-modeset-test` が写しと sleep を見る。
- 表 R: PP_* の書き手は段の後も 1 つ（xd-panel。diagnostics.c・survey は読むだけ）。SOUTH_DSPCLK_GATE_D は旧の書き手が他にもある（init の列の DPMG の bit: p035 まで旧）が、bit が違い、どちらも rmw。表 R に行は足さない。
- hook と lock: 殻が PPS の lock（DISPLAY_CORE の参照 → mutex）を取り、その下で xd を呼ぶ。xd-panel は lock を取らない。窓は env の `aux_io`（kernel は `k->aux_io` = MMIO、host は model）。
- R11: panel.c 0、xd-panel.c/.h 0、internal.h 0、display-regs-xd-south.h 0、xd-regs-map.h 0、host-xd-panel-test.c 0（`grep -cE 'ws[0-9]{3}|WS[0-9]{3}|BUG-[0-9]+|review [A-Z][0-9]|\bP[0-9]+[a-z]? '`）。

## 旧と同じに保った物と違い

- 確かめ方: 旧の host 試験 `host-dp-test`（72）と `host-lcd-modeset-test`（123）を段の前（e58dc6f48）と後で `plan/ws031/display-ref -v` で流した（display-host-lib、`I915_HOST_EXTRA_PRODUCTION="src/drivers/gpu/i915/perf.c src/drivers/gpu/i915/mmio-record.c"`）。どちらも失敗 0。lcd の出力（394 行）は pointer を除いて一致。dp の出力（821 行）の差は 28 行の debug の文だけ: 旧の `PPS 0 mask: … value: … PP_STATUS: … PP_CONTROL: …`（待ちの mask と値）が `PPS 0 waits from PP_STATUS: … PP_CONTROL: …` になった（mask と値は xd の中の事）。
- **違い（意図した物）**: power on の前に PP_ON_DELAYS・PP_OFF_DELAYS・PP_CONTROL の cycle を毎回書く（design §2.11「毎回の enable で書く」）。値は init で書いた物と同じなので、firmware や suspend が変えていなければ register の値は変わらない（PP_CONTROL の cycle は変わる時だけ書く）。
- 違い: 状態の待ち（on・off・cycle）は env の `wait_reg`（kernel: fast 2 µs + slow 5000 ms）ではなく xd-io の poll（1 µs の spin → 10 µs → 100 µs の sleep、上限 5 s）。timeout の記録は xd-io の record に残る。DPLS の rmw は xd-io の rmw（値が同じでも書く。旧は変わる時だけ）。
- 違い: `i915_pps_verify_state`（delay の不一致の log）は待ちの直前（control の書き込みの前）に読む。旧は control の書き込みの後。log だけの事。
- 時刻からの sleep（power cycle の残り、VDD の後の power-up delay、backlight の delay）は殻に残した（xd-io には時刻が無い）ので、env の sleep の数（`host-dp-test` の slept_us の check）は旧と同じ。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- 起動で panel が点く（survey の PP_STATUS on、`PP_CONTROL` が基準と同じ。PP_ON/OFF_DELAYS が基準と同じ値）。
- `i915.debug=display` の起動で、power on の前に PP_ON/OFF_DELAYS の書き込みが増えた（書き込みの列の digest は基準と違う見込み: 意図した差）。
- panel status timeout の log が無い。
- suspend → resume で panel が戻る（ユーザーの一覧）。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p022/xd-host` → regs 498・io 50・platform 89・vbt 137・survey 57・aux 43・gmbus 24・panel 47（PPS の 26 を足した）、失敗 0。`I915_XD_STRICT` の検査は通る。
- PPS の check（host-xd-panel-test）: VBT の delay が無い時は空、spec の待ち（210・50・50・500・610 ms）と sequencer への値（backlight 1/1、cycle 7000）、大きい方を選ぶ事と ms への切り上げ、VBT の cycle +100 ms、delay の書き込みの順（ON → OFF → cycle、VDD は保つ）、cycle が同じなら control を書かない、sequencer 1 は 0x100 上、読み返し、sequencer 1 の muxed の判定、find の 3 つ、VDD の単独 on（1 書き込み）と off、power on の 5 書き込み（delay 2 → DPLS の gate を外す → ON|RESET → gate を戻す）と 30 ms の待ち、backlight の enable の set/clear、power off で 4 bit が一緒に落ちる、cycle の終わりの待ち（0.4 s 後に終わる cycle を追う）、点かない panel で 5 s の timeout と DPLS の gate を戻す事、WA 無しで gate の word に触らない事。
- `sh plan/ws245/tests/check-xd-regs-linux.sh build/ws229-p001/linux/linux-6.8.12 build/p022/linuxcheck` → 1012 対が一致、差 0（新しい 5 対を含む）。
- 旧の host 試験: 上。
- build: 下。
- 5330: 未実施（全部の段の後に 1 回）。

## build

- 2026-10-16: `sh build/tmp/kbuild.sh p021`（`BUILD=build/p021/k-*` の使い回し）で既定の vmunix（sha256 c863ef67d69f5eca…）と `I915_TESTS=y` の `I915_TEST_SET=vke2・display・display_ktest・display_ktest2・execution` の 5 組が rc 0・warning 0。`git diff --check` は空。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- PP_STATUS 0x80000008（on）、PP_ON/OFF_DELAYS の書き込みが power on の前に増えた（I5、意図）、panel status timeout の log 0

満たしていない・未確認の基準と残る物:

- PP_CONTROL は survey 2 で基準 0x6f・段 0x67（H7: VDD の 3 秒後の off。段が Linux の振る舞い、意図と判断済み）
- suspend → resume で panel が戻る: ユーザーの一覧（beta2.md の U19〜U23）の項目で、未実施

P1 の見立て（Status は変えない。判定は Q1）: cleared にしてよい（PP_CONTROL の差は H7 で意図）。
