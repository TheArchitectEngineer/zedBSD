<!-- awesome-plan project=zedbsd record=ws245-p050d4b -->

# ws245-p050d4b: 走らせ方の口（resident run・show run・kernel の binding）

Status: cleared（2026-10-18 Q1 判定: q1229-i20・i21（5330 の実機）でこの段の「実機で確かめる事」の 5330 の項目を満たした（「判定の材料」の節、P1 q1241）。外部出力・suspend などの残りは beta2.md の U19〜U23 で追う）（旧: in-progress（2026-10-17 q1229-i12 で実装・host 試験・build 済み（9915b53dc）、5330 は全部の段の後に 1 回））
Parent: [WS245](../ws.md)
Queue: q1229-i11（P1）→ q1229-i12（P1、実行）
設計: [design.md](../design.md) §6.2 の p050 の行、§3.6、§4.2。分け方は [phase050d4](../phase050d4/phase.md)

## 範囲

- resident run・show run（ktest の道）・kernel の binding（bind_ops・fill_cfg・preflight・locks_init・output params/cfg/link fallback）と diagnostics の retained の 3 つを xd-display へ。fill_cfg が `struct i915_xd_picture_config` を直に作り、p050d4a の変換 `drv_i915_lcd_picture_config` を消す。
- output.c の `drv_i915_lcd_compute_hdmi/_dp_ext` を xd-screen の plan_link に（state.c の compute と `i915_state_from_plan` を消す）。
- ktest（lcd-run・hdmi-output・lcd-flip・lcd-gpu・lcdg・lcd-show）は p050d4a で組を外した物の file の削除を Q1 に（R11 と §6.4 の基準）。

## 受け入れ

- 消した関数の本番の呼び手が 0、build（既定と I915_TESTS=y の vke2・execution）warning 0、`I915_XD_STRICT`、R11。比べは付け替えた旧 host 試験（host-lcd-modeset-test）と xd の試験。5330 は全部の段の後に 1 回。

## 再開の手順（2026-10-17、p050d4a の後の P1 の調べ。source は未変更）

1. 起点は p050d4a の記録の commit（d089058b7）。今の modeset.c（2636 行）に残る public は world（create/destroy・set_display_ver/xd_platform・xd_io・backend_fault・debug）、変換 `drv_i915_lcd_picture_config`（:263）、show（`_show_prepared`・`_show_retained`・`_show_gpu_retained`）、resident run（`drv_i915_lcd_kernel_resident_run` :660）、kernel の binding（`_output_params/_cfg/_link_fallback`・`_kernel_locks_init/_bind_ops/_preflight/_fill_cfg/_at_stage` :945〜1390）と static の resident/show/kernel の hook（`i915_kernel_*`・`i915_resident_*`・`i915_show_*`）。これを xd-display（または新しい own の file。xd の file は internal.h を include できない: kernel の device（`struct i915_lcd_kernel_deps`・mmio・gm・irq・edp）を触る部分は own の display 側に置き、xd 側は picture の config と backend の口だけにする）へ。
2. 入力の型: fill_cfg（:1239）と output_cfg・resident の hdmi/dp_ext cfg（`i915_resident_hdmi_cfg/_dp_ext_cfg`）が `struct i915_lcd_modeset_cfg` を作り、`struct i915_display_output.state`（`struct i915_lcd_state`）と合わせて変換している。fill_cfg が `struct i915_xd_picture_config` を直に作る形にし、head.c の `head->cfg`・`head->output.state`、`display->output.state` の型を own に（`struct i915_display_output` は display-internal.h？ internal.h:3000 付近を確かめる）。
3. compute: state.c の `drv_i915_lcd_compute`（dp-sink.c:3313 の eDP の device が呼ぶ: d4c）、`_compute_hdmi`（output.c:992）、`_compute_dp_ext`（output.c:1470、modeset.c:2575 の dp_ext fallback）を xd-screen の `drv_i915_xd_screen_plan_link` に。state.c の word recorder（`drv_i915_lcd_emit_plane/_cpu_transcoder/_ddi/_transcoder`・`_words_*`）は試験だけが使う記録器（R7）: 呼び手を数えて 0 なら消す。`drv_i915_mode_config_init`〜`_atomic_global_obj_init` は display の start の Linux の段（d4c の nogem と一緒）。
4. 比べ: `sh build/tmp/stagecmp.sh d089058b7 <名前> out50c-picture.c`（base 側も out50c-picture.c が通る: d4a 以降の tree）。host-lcd-modeset-test は `drv_i915_lcd_compute` と変換を使うので、d4b で型が変わるならこの試験も付け替える（d4d で消える）。`host-xd-picture-test` に resident run の規則の check を足すかは d4b で判断。
5. build は `sh build/tmp/kbuild.sh <名前>`（既定・vke2・execution の 3 組。display の組は無くなった）。dead code は `sh build/tmp/killdead.sh build/tmp/d4a/dead-base.txt <build log>`（base の一覧は bfd44d75f の物: 新しい段では新しい base で作り直す: `python3 -I build/tmp/deadexp2.py build/tmp/ref-<sha>/src/drivers/gpu/i915/display build/tmp/ref-<sha>`）、古い宣言は `staledecl.py` の差分を `rmdecl.py` に、static の古い前方宣言は `build/tmp/d4a/rmstaticdecl.py`。

## 結果（2026-10-17 q1229-i12、P1。base ec4951551、段の commit 9915b53dc）

Q1 の判断（kernel の device の型は既定の案: own の型を display-internal.h へ移し、run の code は own の file に）に従った。

形:
- 新しい own の file `display/resident.c`・`resident.h`（Zlib）: modeset.c の show run・resident run・kernel の binding を移した。口は `drv_i915_resident_run`（旧 `drv_i915_lcd_kernel_resident_run`）、`_retained`（旧 `drv_i915_lcd_show_retained`）、`_abandoned`・`_gpu_retained`（旧 diagnostics.c の `drv_i915_lcd_kernel_abandoned/_gpu_retained` と `drv_i915_lcd_show_gpu_retained`）、`_output_params`、`_bind`（旧 locks_init と bind_ops。run は常に `k->display` を持つ: container_of を消した）、`_preflight`、`_config`（旧 fill_cfg と output_cfg: `struct i915_xd_picture_config` を直に作る）、`_link_fallback`。show の本体は static（`resident_show`）。
- 型: `display-internal.h` に own の `struct i915_display_link`（新、出力の link: rate・lanes・bpp・M/N・PLL の reference と combo の語）、`struct i915_lcd_kernel_deps` と `struct i915_lcd_run_params`（kind・port・pipe だけに縮めた）を移した。`struct i915_display_output.state`（`i915_lcd_state`）は `mode` と `link` に。`struct i915_lcd_show_env.cfg` と `head.cfg` は `struct i915_xd_picture_config`。`struct i915_lcd_modeset_cfg` と変換 `drv_i915_lcd_picture_config` を消した。show の env・report・run の使われない field（pattern・readback・bl_freq0・draws_ok ほか）を消した。
- output.c（own）: `drv_i915_output_link_hdmi`・`drv_i915_output_link_dp` が xd-screen の `drv_i915_xd_screen_plan_link` を直に呼ぶ（platform は `display->xd.choice.platform`）。state.c の `drv_i915_lcd_compute_hdmi/_dp_ext` と `i915_state_dkl_words`、clock.c の `drv_i915_dkl_pll_calc`（呼び手 0）を消した。panel の `drv_i915_lcd_compute` と `i915_state_from_plan` は dp-sink.c の eDP の device が呼ぶので d4c まで残る（resident の panel の config は `edp->lcd` から写す）。
- modeset.c は world（create/destroy・display_ver・xd_platform・xd_io・backend_fault・debug）だけ（2636 → 282 行）。d4c で消える。
- log: `i915: LCD-B preflight:`・`LCD-B input:` は段の符号（R11）なので `i915: resident display: preflight:`・`inputs:` に、`LCD-B PICTURE UP`・`observation window over` の行は捨てた。h4-ctl.py が読む `resident display: buffers …`・`picture up (buffer A surf …)` は同じ。
- 振る舞いの差（実機の確かめは 5330 の 1 回で）: (1) DP-ext の出力の選びで旧は DKL の TC PLL の語を計算して失敗を返せた。今は link の plan だけで、TC PLL は picture の prepare が作る（旧も picture は自前の語を使っていた: d4a）。(2) HDMI の link の `ref_khz` を 38400 で持つ（旧は 0。picture は Type-C の時だけ読むので書き込みは同じ）。(3) 出力の kind が点けられない時の EIO を takeover の前に返す（旧は後。kind は panel・HDMI・DP-ext の 3 つだけなので起きない）。

確かめた事:
- 前後の比べ（`sh build/tmp/stagecmp.sh ec4951551 d4b/cmp2 out50c-d4b.c out50c-picture.c`、stagecmp.sh に ref の harness の引数を足した）: dp SAME、lcd の trnorm の差 0 行、out50c の差 0 行。旧 host 試験 dp 72・lcd 120 check とも 0 failures（host-lcd-modeset-test は変換を試験の中の helper に移した）。harness `temp/harness/out50c-d4b.c`（output.c の link の口と config を直に）。
- `run-xd-host-tests.sh` 26 本 all passed。新しい `host-output-link-test`（21 check: WS051 の host-dpext-link の oracle（5330 の USB-C の monitor が RBR x4 24 bpp、limits、branch）を output.c の口に移し、HDMI の link と拒みを足した）。`I915_XD_STRICT` の xd の .c は全部通る。
- build（`sh build/tmp/kbuild.sh d4b`、既定・vke2・execution）: 3 組 rc=0 warning 0。`git diff --check` 0。
- 呼び手 0: 消した関数（compute_hdmi/_dp_ext・picture_config・dkl_pll_calc・lcd_kernel_abandoned/_gpu_retained・lcd_show_*・lcd_kernel_*・lcd_output_*）の参照は tree に 0（WS051 の host-dpext-link.c を除く: 下の削除の依頼）。deadexp で新しく死んだ export は 0（dkl_pll_calc を消した後）、古い宣言 0。
- R11（触った file）: resident.c/h・modeset.c/h・internal.h・head.c・present.c・display.c・output.c/h・state.c/h・diagnostics.c/h・clock.c・modeset-internal.h は 0。display-internal.h 10 → 0、dp-ext-kern.c 3 → 0。日付つきの決定の記録を head.c 1・output.c 2・display-internal.h 1 消した。

実機で確かめる事（5330 は全部の段の後に 1 回）: 起動・picture up・preflight と inputs の log、HDMI・USB-C DP の 2 画面（head の config）と DP の link fallback（ユーザーの一覧）。

Q1 に頼む事:
- 削除: `plan/ws051/tests/host-dpext-link.sh`・`host-dpext-link.c`（消した `drv_i915_lcd_compute_dp_ext` を呼ぶ。master の Tools・未完の Phase からの参照なし。oracle は `host-output-link-test` に移した）。
- 他の WS の道具の文言: `plan/ws084/tests/reboot-loop.sh:10,71,82` が `LCD-B preflight: the display is not idle` を grep する → `resident display: preflight: the display is not idle`（R11 の基準で直すか消すかは Q1）。`plan/ws075/tests/hdmi-h1-hw.sh:47`・`hdmi/usb-poll.sh:20` の進みの表示の grep の `LCD-B` は他の語も含むので害なし。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- resident run（`resident display: preflight`・inputs の行）、show run と picture up

満たしていない・未確認の基準と残る物:

- なし（5330 の範囲で）

P1 の見立て（Status は変えない。判定は Q1）: cleared にしてよい。
