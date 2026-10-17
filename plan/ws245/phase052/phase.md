<!-- awesome-plan project=zedbsd record=ws245-p052 -->

# ws245-p052: Type-C の状態機械（流れ A）と USB-C の DP

Status: in-progress（2026-10-17 q1229-i13 で 1 つ目（外部 DP の AUX を xd-aux に）を実装・host 試験・build 済み（200128cc0）。残り（tc.c の状態機械を xd-tc に、tc-kern の UCSI の橋、dp-ext.c）は下の「再開の手順」。5330 は全部の段の後に 1 回。節目: USB-C DP の plug はユーザーの一覧）
Parent: [WS245](../ws.md)
Queue: q1229-i13（P1）
設計: [design.md](../design.md) §6.2 の p052 の行、§3.4（DDI_BUF_CTL の所有 bit）、§3.5（TC port の遷移）

## 範囲

- xd-tc の状態機械（§3.5: live/ready/所有/TCCOLD、links の refcount、legacy の印の補正、readout）、UCSI への報告（`drv_typec_display_report/_forget`）、DDI_BUF_CTL の所有 bit の規則、screen の DP-TC の TC の部分、DP-ext の probe と fallback。
- 消す: tc.c・tc-kern.c・dp-ext.c・dp-ext-kern.c、intel/phy.h。旧 Linux の DP の環境（aux.c・dp-sink.c・edid-read.c・dp-internal.h）。

## 1 つ目の実装（2026-10-17 q1229-i13、P1。base fbf1fcda9、commit 200128cc0）

- dp-ext-kern.c を Linux の DP の world（intel_dp・drm_dp_aux・aux.c の transfer・dp-sink.c の DPCD helper・edid-read.c）無しに書き直した: port ごとに `struct i915_xd_aux`（channel は tc-kern の aux_ch）、DPCD は `drv_i915_xd_dpcd_read/_write`、受信の capability は拡張 capability の規則（Procedure referenced: drm_dp_read_dpcd_caps）、EDID は I2C over AUX（segment pointer・offset・read）と `drv_i915_xd_edid_read`、DDC の probe は 1 byte の read。各 access は Type-C の lock（`drv_i915_tc_lock`、DP-alt/legacy でなければ・partner がいなければ -ENXIO）、AUX の電源の domain（`drv_i915_aux_legacy_power_domain`）と display core、外部 port の共有の mutex を取る（旧 aux.c の message ごとの順と同じ物を access ごとに）。外部 port の状態は own の `struct i915_dp_ext_world`（`display->dp_ext`、start で確保、`drv_i915_dp_ext_stop/_destroy`）。eDP の device の backend を外部 port のために start する呼びは要らなくなった。
- 空にした（vmunix.mk から外した）: aux.c・aux.h（d4c で空）、dp-sink.c・dp-sink.h、edid-read.c、dp-internal.h、vbt.h（Linux の環境、読み手 0）。display.c の DP world の create/destroy は `drv_i915_dp_ext_destroy` に。edp-device.c の終わりは `drv_i915_dp_ext_stop(display)`。internal.h の環境の説明と eDP の device の説明、使われない define（`I915_PORT_B`・`I915_DP_WORK_VDD_OFF`・`I915_EDP_E2BIG/EPROTO/EREMOTEIO`）を直した。
- 旧との違い: I2C の nack・defer の数の log は無い（xd-aux の tries・no_answers を出す）。AUX の電源と lock は DPCD の 1 回の access（16 byte を超える read も）ごと（旧は AUX の message ごと）。

## 確かめた事

- build 3 組 warning 0、run-xd-host-tests 27 本 all passed、WS051 host-tc（112）・WS075 hdmi/host-output-test（80）・WS237 gen9 flip/mode PASS。R11 0、`git diff --check` 0。
- 外部 DP の AUX の道（dp-ext-kern.c）を通す host 試験は無い（旧 WS051 の host-dpext は base で既に link error）: 実機の USB-C DP の節目で確かめる。

## 再開の手順（次の世代へ、2026-10-17 P1 の調べ）

1. tc.c（own の code、display 13 だけ、register は直書き）の状態機械を xd-tc に移す: `struct i915_xd_tc_ports`（port ごとに present・legacy・mode・links・connection・数）を xd-tc.c に足し、connect/disconnect は既にある `drv_i915_xd_tc_connect/_disconnect`（流れ A・B、全 platform）を使う。tc.c の規則をそのまま持つ: mode は live の legacy > DP-alt > TBT、何も無ければ legacy の port は LEGACY・receptacle は TBT（`tc_target_mode`）、legacy の印は live が 1 種だけの時に live を信じて直す（`tc_fix_legacy`）、lock の時に links 0 なら live の mode に合わせ直す、unlock で links 0 なら即 disconnect（Linux の 1 秒の遅延は採らない: 他の Type-C port の HPD が更新されない firmware の性質）、readout は ready かつ owned なら live の mode か port の種類で、DDI_BUF_CTL の enable がある port は links 1、legacy の port は ready を 500 ms 待つ、`link_needs_reset`（links>0 の DP-alt で live の mode が違う）、`connected_locked`（mode が受ける live の bit）、FIA の lane（DFLEXDPSP）・pin（DFLEXPA1）・MLE1 の lane の書き込み、`dp_sample`、`log_state`。env は lock・power（CORE・PORT・COLD → xd の I915_XD_POWERS_*）・sleep・now・log の hook。
2. WS051 の `plan/ws051/tests/host-tc.sh`・`host-tc.c`・`host-tc-tables.c`（master の Tools、tc.c の 112 checks）の検査を xd の host 試験（host-xd-tc-test に足す）に移し、host-tc の道具の扱い（直すか消すか）を Q1 に。
3. tc-kern.c（own、UCSI の橋 `drv_typec_display_report/_forget`、power の domain の対応、port の lock、aux_ch の表、resident の TC の hook `drv_i915_lcd_tc_*`・DKL の hook）を own の新しい file（例 `tc-port.c`）か hpd.c・resident.c に分け、xd の状態機械に env を渡す。`struct i915_tc_kern` は internal.h から own の header へ。
4. dp-ext.c（own の probe の core、env 抽象）は xd-dp へ移す（design の「DP-ext の probe と fallback」）か own のまま改名するかを決める（既定の案: xd-dp.c に `drv_i915_xd_sink_*` として移し、dp-ext-kern.c は own の殻のまま hpd・output・resident の口を持つ）。
5. intel/phy.h ほか intel/ の Gen12 だけの header（読み手 0: phy.h・connector.h・ref.h・wm.h・power.h・ddi.h・fixed.h・fourcc.h・psr.h・power-set.h・wm-dbuf.h・ref-inlines.h、vbt.h は display/vbt.h だけが読んでいた。dp.h・mreg.h・trans.h・plane.h・hotplug.h は host-xd-regs-test の比べだけ）の整理は p043 で WS244 と。

## 実機で確かめる事

- USB-C DP の monitor（TC2）の plug で 2 画面・unplug で戻る・抜いている間の present が止まらない・出力の移動（節目、ユーザーの一覧）。起動の `i915: DP-ext TCn: bound (DDI ., AUX channel N, …)` の行。

## 削除と掃除（Q1）

- `src/drivers/gpu/i915/display/` の `aux.c`・`dp-sink.c`・`dp-sink.h`・`edid-read.c`・`dp-internal.h`・`vbt.h`（中身を空にした。aux.h は p050d4c で依頼済み）。
