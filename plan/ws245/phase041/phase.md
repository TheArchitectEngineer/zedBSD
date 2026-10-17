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
