<!-- awesome-plan project=zedbsd record=ws245-p052 -->

# ws245-p052: Type-C の状態機械（流れ A）と USB-C の DP

Status: in-progress（2026-10-17 q1229-i14 で残りの 1〜4 を実装・host 試験・build 済み（e92b2bee0・e466cb254）。5 は p043。5330・SV7・5320 は全部の段の後に 1 回（T1）。節目: USB-C DP の plug はユーザーの一覧）
Parent: [WS245](../ws.md)
Queue: q1229-i13（P1、1 つ目）、q1229-i14（P1、残り）
設計: [design.md](../design.md) §6.2 の p052 の行、§3.4（DDI_BUF_CTL の所有 bit）、§3.5（TC port の遷移）

## 範囲

- xd-tc の状態機械（§3.5: live/ready/所有/TCCOLD、links の refcount、legacy の印の補正、readout）、UCSI への報告（`drv_typec_display_report/_forget`）、DDI_BUF_CTL の所有 bit の規則、screen の DP-TC の TC の部分、DP-ext の probe と fallback。
- 消す: tc.c・tc-kern.c・dp-ext.c・dp-ext-kern.c、intel/phy.h。旧 Linux の DP の環境（aux.c・dp-sink.c・edid-read.c・dp-internal.h）。

## 1 つ目の実装（2026-10-17 q1229-i13、P1。base fbf1fcda9、commit 200128cc0）

- dp-ext-kern.c を Linux の DP の world（intel_dp・drm_dp_aux・aux.c の transfer・dp-sink.c の DPCD helper・edid-read.c）無しに書き直した: port ごとに `struct i915_xd_aux`（channel は tc-kern の aux_ch）、DPCD は `drv_i915_xd_dpcd_read/_write`、受信の capability は拡張 capability の規則（Procedure referenced: drm_dp_read_dpcd_caps）、EDID は I2C over AUX（segment pointer・offset・read）と `drv_i915_xd_edid_read`、DDC の probe は 1 byte の read。各 access は Type-C の lock（`drv_i915_tc_lock`、DP-alt/legacy でなければ・partner がいなければ -ENXIO）、AUX の電源の domain（`drv_i915_aux_legacy_power_domain`）と display core、外部 port の共有の mutex を取る（旧 aux.c の message ごとの順と同じ物を access ごとに）。外部 port の状態は own の `struct i915_dp_ext_world`（`display->dp_ext`、start で確保、`drv_i915_dp_ext_stop/_destroy`）。eDP の device の backend を外部 port のために start する呼びは要らなくなった。
- 空にした（vmunix.mk から外した）: aux.c・aux.h（d4c で空）、dp-sink.c・dp-sink.h、edid-read.c、dp-internal.h、vbt.h（Linux の環境、読み手 0）。display.c の DP world の create/destroy は `drv_i915_dp_ext_destroy` に。edp-device.c の終わりは `drv_i915_dp_ext_stop(display)`。internal.h の環境の説明と eDP の device の説明、使われない define（`I915_PORT_B`・`I915_DP_WORK_VDD_OFF`・`I915_EDP_E2BIG/EPROTO/EREMOTEIO`）を直した。
- 旧との違い: I2C の nack・defer の数の log は無い（xd-aux の tries・no_answers を出す）。AUX の電源と lock は DPCD の 1 回の access（16 byte を超える read も）ごと（旧は AUX の message ごと）。

## 残りの実装（2026-10-17 q1229-i14、P1。base 421e5ca1c、commit e92b2bee0・e466cb254）

- **1 状態機械**（e92b2bee0）: 新しい `xd-tc-port.c/.h`（`struct i915_xd_tc_ports`、port ごとに present・legacy・aux・desc・`struct i915_xd_tc_connection`・links・数）。tc.c の規則をそのまま持つ: target の mode（legacy > DP-alt > TBT、何も無ければ legacy は LEGACY・receptacle は TBT）、legacy の印の補正（live が 1 種だけの時）、lock の時に links 0 なら live の mode に合わせ直す（拒まれたら port の既定の mode に落とす）、unlock で links 0 なら即 disconnect、readout（ready かつ owned なら live の mode か port の種類、DDI_BUF_CTL の enable と TC cold の block で links 1、legacy は ready を 500 ms 待つ）、link_needs_reset、connected(_locked)、FIA の lane（1・2・4 以外は log して 0）・pin、dp_sample、log_state、stop。connect/disconnect は xd-tc の `drv_i915_xd_tc_connect/_disconnect`（流れ A・B）。env は lock・now・sleep・log、power は xd の function の組（`struct i915_xd_tc_power`）。FIA 流れ（TGL・ICL）は live・readout・log の読みの間 TC cold を off に保つ（`drv_i915_xd_tc_cold_functions` の port の種類の物）。
- **3 kernel の橋**（e92b2bee0）: own の新しい `tc-port.c/.h`（`struct i915_tc_port_world` は tc-port.c の中、`display->tc_ports` は pointer。start・stop・destroy、UCSI への報告 `drv_i915_tc_port_report`、resident の TC hook `drv_i915_lcd_tc_*`（mode は I915_XD_BACKEND_TC_* に写す）・DKL hook、port の番号 `drv_i915_tc_port_of(display, ddi)`、AUX `drv_i915_tc_port_aux`）。宣言は xd の port の表（`display->xd.ports`、AUX は表の aux）と xd-vbt の child（type_c・tbt が無ければ legacy）。power.c に function の組の `drv_i915_display_power_get_set/_put_set`。`struct i915_tc_kern` は internal.h から消した。tc.c・tc.h・tc-kern.c・tc-kern.h は中身を空にし vmunix.mk から外した。
- **2 試験**（e92b2bee0）: WS051 の host-tc（112）の検査を新しい `src/drivers/gpu/i915/tests/display/host-xd-tc-port-test.c`（137 checks、run-xd-host-tests.sh に登録）に移した。TGL の FIA 流れ（live の読みの TC cold、connect・unlock・readout・空の receptacle）を足した。host-tc-tables（AUX の domain・DKL の window・HPD の long pulse）は表の元（dkl-phy.c・hotplug.c）が p040・p039 で既に空で、base で壊れていた。
- **4 DP-ext の probe**（e466cb254）: dp-ext.c の probe の核を xd-dp.c に `drv_i915_xd_dp_probe_*`（型 `struct i915_xd_dp_probe`・`_env`・`_source`・`_desc`、enum `i915_xd_dp_probe_status/_step/_mode_status`、定数 `I915_XD_DP_PROBE_*`）として移した（中身は名前の付け替えだけ）。`struct i915_xd_sink`（xd-screen）・`struct i915_xd_dp_sink`（xd-dp の訓練の sink）と名前が当たるので phase の既定の案の `drv_i915_xd_sink_*` ではなく `_dp_probe_` にした。file の上部の出典の文は消し、define の組に Values referenced、関数に Procedure referenced を付けた。dp-ext-kern.c は own の殻のまま（hpd・output・resident の口）。dp-ext.c・dp-ext.h は中身を空にし vmunix.mk から外した。
- 5（intel/ の読み手 0 の header）は p043。

## 旧との違い（実機で見る事）

- **display 12（Tiger Lake、5320）と display 11 の Type-C port も driven になった**（旧 tc-kern は display 13 だけ）。design の p052（流れ A）と p044 の「流れ B の配線（p052）」に従った既定の案。TGL の起動で `i915: TC<n>: declared`・readout の行が出る。TC_COLD_OFF の well（firmware の mailbox）を lock・unlock・live の読みごとに取る（Linux と同じ）。
- legacy の port の PHY が ready でない時: 旧 tc.c は connect を拒んだ、xd の connect は Linux のとおり進める（user の決定 4「Linux を優先」）。
- port の電源は domain（PORT_DDI_LANES・AUX_USBC）ではなく同じ well の function（LANES(ddi)・AUX(aux)）で取る。DISPLAY_CORE（well 無し）は取らない。power の domain の使用数（`domain_use_count`）に TC の分は数えない。
- 宣言の AUX channel は xd の port の表（VBT の aux か platform の既定）から。log の行 `i915: TC%u <why>: … | TCSS_DDI_STATUS=…`（FIA 流れは `DFLEXDPPMS=`）、`live` は FIA が読めない時 0。

## 確かめた事（q1229-i14）

- build 3 組（既定・vke2・execution、`build/tmp/kbuild.sh`）warning 0（1〜3 の後と 4 の後の 2 回）。
- `run-xd-host-tests.sh` all passed（28 本、host-xd-tc-port-test 137 checks 0 failures）、WS075 `hdmi/host-output-test.sh` 80/0、WS237 gen9 flip 48/0・mode 71/0、WS229 bootfb 20/0（2 回とも）。
- 触った file の R11 の数 0、`git diff --check` 0。
- 未実施: 5330・5320・SV7・QEMU（全部の段の後に T1）。WS051 の host-tc.sh は tc.c が空になり build できない（下の削除）。

## 削除と掃除（Q1、q1229-i14 の分）

- `src/drivers/gpu/i915/display/` の `tc.c`・`tc.h`・`tc-kern.c`・`tc-kern.h`・`dp-ext.c`・`dp-ext.h`（中身を空にした）。
- `plan/ws051/tests/host-tc.sh`・`host-tc.c`・`host-tc-tables.c`（検査は host-xd-tc-port-test へ。master の Tools の行 `ws051/tests/host-tc.sh` を run-xd-host-tests の host-xd-tc-port-test に替える）。
- `plan/ws051/tests/host-dpext.sh`・`host-dpext.c`（空にした dp-ext.h を読む、base で既に link error、参照なし: 判断は Q1）。

## 確かめた事（q1229-i13）

- build 3 組 warning 0、run-xd-host-tests 27 本 all passed、WS051 host-tc（112）・WS075 hdmi/host-output-test（80）・WS237 gen9 flip/mode PASS。R11 0、`git diff --check` 0。
- 外部 DP の AUX の道（dp-ext-kern.c）を通す host 試験は無い（旧 WS051 の host-dpext は base で既に link error）: 実機の USB-C DP の節目で確かめる。

## 再開の手順（2026-10-17 P1 の調べ。1〜4 は q1229-i14 で済み、5 は p043）

1. tc.c（own の code、display 13 だけ、register は直書き）の状態機械を xd-tc に移す: `struct i915_xd_tc_ports`（port ごとに present・legacy・mode・links・connection・数）を xd-tc.c に足し、connect/disconnect は既にある `drv_i915_xd_tc_connect/_disconnect`（流れ A・B、全 platform）を使う。tc.c の規則をそのまま持つ: mode は live の legacy > DP-alt > TBT、何も無ければ legacy の port は LEGACY・receptacle は TBT（`tc_target_mode`）、legacy の印は live が 1 種だけの時に live を信じて直す（`tc_fix_legacy`）、lock の時に links 0 なら live の mode に合わせ直す、unlock で links 0 なら即 disconnect（Linux の 1 秒の遅延は採らない: 他の Type-C port の HPD が更新されない firmware の性質）、readout は ready かつ owned なら live の mode か port の種類で、DDI_BUF_CTL の enable がある port は links 1、legacy の port は ready を 500 ms 待つ、`link_needs_reset`（links>0 の DP-alt で live の mode が違う）、`connected_locked`（mode が受ける live の bit）、FIA の lane（DFLEXDPSP）・pin（DFLEXPA1）・MLE1 の lane の書き込み、`dp_sample`、`log_state`。env は lock・power（CORE・PORT・COLD → xd の I915_XD_POWERS_*）・sleep・now・log の hook。
2. WS051 の `plan/ws051/tests/host-tc.sh`・`host-tc.c`・`host-tc-tables.c`（master の Tools、tc.c の 112 checks）の検査を xd の host 試験（host-xd-tc-test に足す）に移し、host-tc の道具の扱い（直すか消すか）を Q1 に。
3. tc-kern.c（own、UCSI の橋 `drv_typec_display_report/_forget`、power の domain の対応、port の lock、aux_ch の表、resident の TC の hook `drv_i915_lcd_tc_*`・DKL の hook）を own の新しい file（例 `tc-port.c`）か hpd.c・resident.c に分け、xd の状態機械に env を渡す。`struct i915_tc_kern` は internal.h から own の header へ。
4. dp-ext.c（own の probe の core、env 抽象）は xd-dp へ移す（design の「DP-ext の probe と fallback」）か own のまま改名するかを決める（既定の案: xd-dp.c に `drv_i915_xd_sink_*` として移し、dp-ext-kern.c は own の殻のまま hpd・output・resident の口を持つ）。
5. intel/phy.h ほか intel/ の Gen12 だけの header（読み手 0: phy.h・connector.h・ref.h・wm.h・power.h・ddi.h・fixed.h・fourcc.h・psr.h・power-set.h・wm-dbuf.h・ref-inlines.h、vbt.h は display/vbt.h だけが読んでいた。dp.h・mreg.h・trans.h・plane.h・hotplug.h は host-xd-regs-test の比べだけ）の整理は p043 で WS244 と。

## 実機で確かめる事

- 起動の `i915: TC2: declared`・`i915: TC2 readout: …` の行（xd-tc-port に替えた後も同じ mode・links）。5320（Tiger Lake）の起動で TC の宣言と readout が出て、GOP の出力と内蔵 LCD が変わらない事（display 12 の TC を driven にした）。
- USB-C DP の monitor（TC2）の plug で 2 画面・unplug で戻る・抜いている間の present が止まらない・出力の移動（節目、ユーザーの一覧）。起動の `i915: DP-ext TCn: bound (DDI ., AUX channel N, …)` の行。

## 削除と掃除（Q1、q1229-i13 の分）

- `src/drivers/gpu/i915/display/` の `aux.c`・`dp-sink.c`・`dp-sink.h`・`edid-read.c`・`dp-internal.h`・`vbt.h`（中身を空にした。aux.h は p050d4c で依頼済み）。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- `TC2: declared (DDI E, legacy 0, AUX channel 4)`・`TC2 readout: mode=none links=0 …` が基準と同じ、`DP-ext TC1/TC2: bound (…)`

満たしていない・未確認の基準と残る物:

- 5320 の TC の宣言と readout、USB-C DP の plug・unplug・出力の移動: ユーザーの一覧（beta2.md の U19〜U23）の項目で、未実施

P1 の見立て（Status は変えない。判定は Q1）: cleared にしてよい。
