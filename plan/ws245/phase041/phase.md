<!-- awesome-plan project=zedbsd record=ws245-p041 -->

# ws245-p041: suspend の戻り

Status: in-progress（2026-10-17 q1229-i14 で実装・host 試験・build 済み（4045c6b0b）。5330 は全部の段の後に 1 回（T1）。節目: suspend → resume → desktop、backlight の値、resume の後の HDMI の plug はユーザーの一覧）
Parent: [WS245](../ws.md)
Queue: q1229-i14（P1）
設計: [design.md](../design.md) §6.2 の p041 の行、§2.14（suspend・resume の列）、§3.6（段の表の suspend_begin/end・resume_begin/end）

## 範囲

- suspend: hotplug を止める → screen を暗くする → TC の link を返し disconnect → DC off を保持 → uninit → DC9。
- resume: DC9 を出る → init → DMC の載せ直し → VGA → HPD の再 arm と live の読み直し。PPS/PWM は次の点灯で書き直す。
- 消す旧 code: 無し（design の行の「—」）。

## 実装（2026-10-17 q1229-i14、P1。base 75cd8f58a、commit 4045c6b0b）

今の列（`device.c` の `drv_i915_device_suspend_hw/_resume_hw` から `display.c` の 4 つの口）は、DC off の保持（INIT の参照）、core の uninit/init（xd-core、p035）、DC9（xd-power、p025）、DMC の載せ直し（xd-dmc、p026）を既に xd の部品で行っていた。足りなかった物を足した:

- `display.c` の `drv_i915_display_suspend_begin`: 先に `drv_i915_hpd_suspend`（新、hpd.c: 割り込みの入口を閉じ、中の入口を待ち、pending の pulse・event・retry を捨て、work を cancel して走っている detection を待つ。queue は残す）、`drv_i915_tc_port_suspend`（新、tc-port.c → xd-tc-port.c の `drv_i915_xd_tc_ports_suspend`: port ごとに lock の下で links を落とし（残っていれば log）、流れの disconnect。port は宣言のまま live）、それから INIT の参照（今のまま）。screen の dark は request worker の park（display window を抜ける）で今のまま。
- `drv_i915_display_resume_begin`: DMC の後に、start の front が済んでいれば `i915_display_resume_front`（新）: VGA plane を止め直す（`drv_i915_vga_disable`）、display の WA（`drv_i915_xd_display_wa`）、GMBUS の reset（`drv_i915_xd_gmbus_reset`）。
- `drv_i915_display_resume_end`（割り込みが戻った後）: INIT を返した後に `drv_i915_tc_port_resume`（xd の `drv_i915_xd_tc_ports_resume` = start の readout と同じ読み（legacy の PHY の ready を待つ）、log の why は "resume"、UCSI に報告し直す）と `drv_i915_hpd_resume`（新: pin を arm し直す（hotplug の register は core の down で消える）、全 connector の pin に event を立てて detection を即 queue、入口を開く）。resume の間に抜かれた connector はこの detection で disconnected（ABSENT）になる。
- PPS の delay と backlight の PWM の FREQ は、次の表示の点灯で `drv_i915_xd_pps_power_on`（delay を書き直す）と `drv_i915_xd_backlight_enable`（FREQ を書く）が書く（保存する register は無い: §2.14）。
- 出典: 新しい関数の block に Procedure referenced（Linux v6.8.12 i915_driver.c・intel_display_driver.c・intel_vga.c・i915_suspend.c・intel_hotplug.c・intel_tc.c）。

## 確かめた事

- build 3 組（既定・vke2・execution）warning 0。
- `run-xd-host-tests.sh` all passed（host-xd-tc-port-test 145 checks: suspend が firmware の出力の port を返し port は live のまま、2 回目は何も書かない、resume の読み直し（空の port は何も持たない、firmware の出力が残れば links 1）、lock と電源の釣り合い）。WS075 host-output-test 80/0、WS237 gen9 flip 48/0・mode 71/0、WS229 bootfb 20/0。
- 触った file の R11 の数 0、`git diff --check` 0。
- host で確かめていない事: display.c・hpd.c の suspend/resume の継ぎ目（kernel の workqueue と割り込みの順。host の stand-in に suspend の道は無い）。実機の suspend で見る。

## 実機で確かめる事（全部の段の後、T1・ユーザーの一覧）

- 5330: suspend → resume → desktop が戻る、backlight の値が戻る、dmesg の `i915: hpd suspend`・`i915: TC<n> resume: …`・`i915: resume: vga …`・`i915: hpd resume: pins=…` の行、resume の後の HDMI・USB-C DP の plug、寝ている間に抜いた monitor が ABSENT になる事。

## 削除と掃除

- 無し。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- suspend の戻りの code は起動では通らない。5330 の起動の基準（i20・i21）は PASS で、起動の道を壊していない事だけが分かる

満たしていない・未確認の基準と残る物:

- Phase の受け入れの全部（suspend → resume → desktop、backlight、`hpd suspend`・`TC<n> resume`・`resume: vga`・`hpd resume`、resume 後の plug、寝ている間の unplug）: ユーザーの一覧（beta2.md の U19〜U23）の項目で、未実施

P1 の見立て（Status は変えない。判定は Q1）: cleared にしない（実機の証拠が無い。ユーザーの suspend の試験か、Q1 の判断で ユーザーの一覧へ移す決定が要る）。
