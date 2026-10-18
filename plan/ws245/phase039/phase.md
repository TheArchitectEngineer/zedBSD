<!-- awesome-plan project=zedbsd record=ws245-p039 -->

# ws245-p039: hotplug（xd-hpd）

Status: in-progress（2026-10-17 q1229-i13 で実装・host 試験・build 済み（02dc4b5c8）、5330 は全部の段の後に 1 回。節目: HDMI の plug で 2 画面はユーザーの一覧）
Parent: [WS245](../ws.md)
Queue: q1229-i13（P1）
設計: [design.md](../design.md) §6.2 の p039 の行、§3.2 の xd-hpd の行、§3.4（lock）、§3.5（connector の遷移）、表 H1・表 R の HPD の mask の行

## 範囲

- xd-hpd: pin の表 H1（platform の port の記述の north_pin・south_pin から）、HPD の arm（north の DE_HPD_IMR・TC/TBT_HOTPLUG_CTL、south の SHPD_FILTER_CNT・SDEIMR・SHOTPLUG_CTL_DDI/TC）、割り込みの ack と pulse の長さ、pulse と検出の event への振り分け、storm（1 秒に 50、long は 10、2 分後に再 arm）、connector の検出（HDMI: live と GMBUS の EDID、Type-C DP: hook）、retry（1 回、Type-C は 5 回）、pulse（eDP は無視、long は sink の限界を戻して検出へ、short は sink の確認）。
- own の殻 hpd.c: 割り込みの入口と門、lock（pin と pending の spinlock、検出の mutex、topology の spinlock）、work（pulse・検出・storm の再 arm・Type-C の link の 2 秒後の確認）、hook（電源の domain、tc.c の live、dp-ext の probe・pulse・link reset・link check、present の retrain・unplugged・partner gone）、topology と表示の inventory の口。
- 旧 file を空に: hotplug.c・hotplug.h・hotplug-internal.h・hdmi.c・hdmi.h（HDMI の検出は xd-hpd に入ったので p051 を待たない）・gmbus.c・gmbus.h（GMBUS の adapter は HDMI の検出だけが使っていた）・intel/hotplug-types.h（intel/hotplug.h は host-xd-regs-test の比べが読むので p043 まで残す）。
- TC の flow と DP-ext の probe は p052 まで旧（tc.c・tc-kern.c・dp-ext*.c）を hook で呼ぶ（design §3.4 の例外）。

## 実装（2026-10-17 q1229-i13、P1。base c3dea0b0e、commit 02dc4b5c8）

- 新しい `display/xd-hpd.c/.h`（xd、`I915_XD_STRICT` で通る）: pin は own の番号（DDI A〜D が 0〜3、TC1〜6 が 4〜9）、bit は display-regs-xd*.h の define（north の `I915_XD_HPD_INT_TC/_TBT`、south の `I915_XD_SDE_DDI_HPD/_TC_HPD`、control の nibble の enable・long）。値と手順の出典は block ごとに（hpd_gen11・hpd_icp、icp_hpd_irq_setup の filter、intel_hotplug.c の閾値、intel_ddi_hotplug の retry の数、drm_edid の DDC の番地）。
- 新しい `display/hpd.c/.h`（own）: 旧 hotplug.c の公開の口（start・stop・pch_irq・de_irq・probe_connector・summary・topology_sequence/_touch・output・output_count・output_take）を同じ名前で持ち、`drv_i915_hpd_arm(display, interrupts)` が display の driver probe の pin の初期化・hpd_init・poll_disable を置き換えた（connector は xd の port の表から 1 回だけ作る）。`drv_i915_hpd_edid_bytes` は display を取る形に（output.c）。summary は started・num_connectors・hdmi_connector だけ。
- display.c: start は `drv_i915_hpd_start(display)`、driver probe は `drv_i915_hpd_arm`、`struct i915_hotplug` とその log、`kms_poll_inited` を消した。internal.h から hotplug の型（`struct i915_hotplug`、`I915_HPD_*` の pin と register の define、記録の型）を消し、`struct i915_hpd_output`・`i915_hpd_summary` は hpd.h へ。
- ktest-gt.c の HPD の case（`drv_i915_hpd_init` の register の oracle: A・B の SHOTPLUG_CTL_DDI 0x88、SDEIMR 0xfffcffff、filter 250、DE の IMR は書かない、interrupts off で SDEIMR を書かない）を host-xd-hpd-test に移して消した。
- 旧との違い（意図した物）: HDMI の EDID の bit-banging の再試行（旧でも未移植で -EIO）は無い。HDMI が live でない時は EDID を忘れる（旧は前の EDID を残したが、使われるのは connected の時だけ）。DDC の電源の参照が取れない時は見ずに disconnected（旧は参照無しで続けた）。log の文言は `i915: hpd …` の接頭辞のまま変えた（`HPD-EVENT` → `hpd event`、`DE HPD` → `hpd north`。読む道具・試験は無い: grep は bug の log だけ）。
- 呼び手 0 になった `drv_i915_power_domain_wells`（power.c、削除を承認された WS051 の host-power-map だけが使っていた）を消した。

## 確かめた事

- 新しい host 試験 `host-xd-hpd-test`（85 checks 0 failures）: pin（ADL-P・TGL・RKL、ICP 前の south 無し）、5330 の port の arm（SHOTPLUG_CTL_DDI 0x88、_TC 0x8888、TC/TBT_CTL 0x8888、SDEIMR 0xf0fcffff、DE の IMR 0xfff0fff0、filter 250）、A・B だけの arm（ktest の oracle）と interrupts off、TGP の filter、south と north の ack と long/short、振り分け、storm と再 arm、HDMI の検出（GMBUS の EDID、別の monitor、抜去、応答なし、電源の拒否）、Type-C DP の検出と 5 回の retry、pulse。`run-xd-host-tests.sh` に足した（27 本 all passed）。
- build 3 組 warning 0。WS237 の gen9 の 5 本・WS229 bootfb・WS075 hdmi/host-output-test（80）・WS051 host-tc（112）が 0 failures。
- R11: 触った file の数は全部 0。`git diff --check` 0。

## lock（design §3.4 の殻の段の規則）

- 割り込み（device の IRQ lock の下）: hpd の spinlock を取って xd-hpd の ack・振り分け・storm の arm（xd-io の read/write だけ。mutex を取らない）。
- 検出の mutex（probe_lock）の下で呼ぶ hook: `drv_i915_tc_connected`・`drv_i915_tc_kern_report`（tc の port の lock）、`drv_i915_dp_ext_probe`（AUX と dp-ext の lock）、`drv_i915_tc_link_needs_reset`、`drv_i915_dp_ext_link_check`、`drv_i915_present_retrain_request`・`_unplugged`（present の lock）、電源の domain の lock。旧 hotplug.c が mode_config.mutex の下で呼んでいたのと同じ物。
- 未解決（既存の性質、5330 では問題になっていない）: xd-io の書き込みの記録の ring は lock 無しで、割り込みの ack と worker の書き込みが同時だと記録の 1 行が崩れ得る（hardware の書き込みには影響しない）。

## 実機で確かめる事（全部の段の後の T1 の 1 回と、ユーザーの一覧）

- 起動の `i915: hpd arm:` の行（5330 の pin の register の値が旧の `hpd_init:` の行と同じか）と `i915: hpd start: connectors=6 hdmi=HDMI-A-1`。
- 節目（ユーザーの一覧）: HDMI の plug で 2 画面、unplug で戻る、plug したまま起動、USB-C DP の plug（TC の hook の道）。

## 削除と掃除（Q1）

- `src/drivers/gpu/i915/display/` の `hotplug.c`・`hotplug.h`・`hotplug-internal.h`・`hdmi.c`・`hdmi.h`・`gmbus.c`・`gmbus.h`、`src/drivers/gpu/i915/intel/hotplug-types.h`（中身を空にした）。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- `hpd arm: connectors=4 | north used=0x30003 …`、`hpd start: connectors=4 hdmi=HDMI-A-1`（基準も connectors=4。基準の文の 6 は例の誤り）、`hotplug path: started`、hpd の書き込みは debug の比べで基準と同じ（I6 の位置だけ）

満たしていない・未確認の基準と残る物:

- 旧の `hpd_init:` の行は基準の dmesg に無く、pin の register の値の行どうしの比べはできない（書き込みの列で代えた）
- HDMI の plug・unplug、plug したまま起動、USB-C DP の plug: ユーザーの一覧（beta2.md の U19〜U23）の項目で、未実施

P1 の見立て（Status は変えない。判定は Q1）: cleared にしてよい。
