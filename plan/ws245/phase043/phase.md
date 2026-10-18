<!-- awesome-plan project=zedbsd record=ws245-p043 -->

# ws245-p043: 旧 code の残りの削除・試験の整理・出典の監査

Status: in-progress（2026-10-17 q1229-i15: p043a 実装・host 試験・build 済み（35b24c912・7de79d444）、p043b の帯域済み（d16688474）。q1229-i16: p043b の power（段の境 2）済み（57967dfb0）。5330・SV7 は全部の段の後に 1 回）
Parent: [WS245](../ws.md)
Queue: q1229-i15（P1）、q1229-i16（P1、p043b の power）
設計: [design.md](../design.md) §6.2 の p043 の行、§5.4（引用の規則）、§6.4（今の試験の扱い）、guardrail「MIT・GPL の出典のデータの引用」

## 段の分け方（2026-10-17 Q1 の判断）

P1 の調べ: "Derived from the Linux kernel" が残るのは power.c（2605 行、旧 power domains の殻）・watermark.c（976 行、DRAM の検出・QGV/PSF の帯域・SAGV の強制 off、xd に代わりが無い）・clock.c（xd-clock の殻）。design の p043（4 LW）はこれらが前の段で消えている前提だった。Q1（2026-10-17）:「p043 は既定の案どおり p043a → p043b の 2 段で進めてよい。…p043b（watermark.c の帯域を xd の部品に、power.c の domain 層を xd-power に、Derived の表示を消す）も 5330 の試験の前に実装する。順は p043a → p043b → p046。p043b は 1 段で重ければ帯域と power を別の commit・段の境にして SHA の表に分けて記録」。

- **p043a**: 試験の整理、intel/ の Gen12 だけの header、display.c の PCH の判定、clock.c、§5.4 の監査。
- **p043b**: watermark.c の帯域を xd の部品に（段の境 1）、power.c の domain 層を xd-power に付け替え（段の境 2）、internal.h・watermark.h の Linux の記述、Derived の表示の削除。

## p043a（2026-10-17、base d52337e50、commit 35b24c912・7de79d444）

- **clock.c** は xd-clock の殻で Linux の code は無かった: 出典の表示を消し、関数を own の名前に（`drv_i915_cdclk_prepare/_read/_refresh/_start/_stop/_max`、旧 init_cdclk_hooks・bxt_get_cdclk・update_cdclk・cdclk_init_hw・cdclk_uninit_hw・max_cdclk_freq）、Linux の文言の log を `display: CD clock: …` に。internal.h の CDCLK の節の Linux の記述も直した。
- **PCH の判定**（display.c）: Linux の intel_pch.c の写し（`drv_i915_pch_type` の switch と `Found … PCH` の log、`drv_i915_is_virt_pch`）を消し、own の `drv_i915_detect_pch`（ISA bridge を歩き、id の種類は xd の表 `drv_i915_xd_pch_kind`）と `i915_display_pch_emulated`・`i915_display_pch_guess` に。xd の PCH の表に display 9 の SPT・SPT-LP・KBP・CNP・CNP-LP の 5 行を足した（Gen9 の backlight の layout が読む。Values referenced で）。支えていない platform の PCH（IBX・CPT・PPT・LPT・WPT・MCC・JSP、display 20 の LNL）は判定しない（I915_PCH_NONE）。
- **intel/ の Gen12 だけの header**: connector.h・ddi.h・dkl.h・dp.h・hotplug.h・mreg.h・phy.h・plane.h・power-set.h・power.h・psr.h・ref.h・ref-inlines.h・trans.h・vbt.h・wm-dbuf.h・wm.h と、参照の無い fixed.h・fourcc.h を空にした（本番の include は 0。読んでいたのは host-xd-regs-test だけ）。
- **host-xd-regs-test**: 旧 header と比べる試験は旧 header と一緒に空にし、runner から外した（試験自身の注記のとおり。値の照合は `check-xd-regs-linux.sh` の Linux の tree との比べが残る）。design の「dump の名前の一覧と比べる」は、5330 の dump（`plan/ws031/display-ref/regs-dump.txt`・`regs-selected.txt`、igt の名前）と xd-regs-map.h の Linux の名前の重なりが 5 組しか無く意味が薄いので作らなかった（既定の案）。
- **color.h** は include されていなかった: 空にした。
- **§5.4 の監査**: 新しい `plan/ws245/tests/check-sources.py`（display の gen9 以外の file と intel/ の xd の header: file の説明に出典が無い、Derived・許諾文が無い、referenced: が 1 行の comment に無い、referenced: に出典の版と path）。p043a の後の残りは power.c・watermark.c・internal.h の Derived と watermark.h の説明（p043b）だけ。inventory の C の行のうち referenced: の段落の外で Linux の関数を名指す物（29）を、Procedure referenced: が既にある物は本文の「(the Linux foo())」を消して直した（xd-dp.c 20、xd-power.c 2、xd-tc.c 1、interrupts.c 1、present.c 1。残りは power.c 3・watermark.c 1 で p043b）。present.h の「Linux stop path」も直した。
- R11: 触った file の開発の管理情報は 0（present.h の BUG-268・ws051-p005b と xd-regs-map.h の plan/ の path を消した）。

### 確かめた事（p043a）

- build 3 組（35b24c912 の tree、`build/tmp/kbuild.sh p043a`）: default warn=0（vmunix sha256 の先頭 aea593c5c589dc35）、vke2 warn=0、execution warn=0。7de79d444 は comment だけ（5 file の構文の確認。build は p043b の時に通す）。
- run-xd-host-tests（`build/p043a-host`）all passed（28 本、0 failures、段の順の check）。WS075 host-output-test 80/0、WS237 gen9 flip 48/0・mode 71/0・backlight 21/0、WS229 bootfb 20/0。

## p043b の 1 つ目: 帯域（段の境 1、2026-10-17、commit d16688474）

- 新しい `xd-bandwidth.c/.h`（xd の型と xd-io だけ）: `drv_i915_xd_bw_read_dram`（mailbox の memory の記述: 種類・channel・QGV・PSF の数）、`drv_i915_xd_bw_read`（QGV の点の clock と timing、PSF の点、group ごとの plane の数と点ごとの display の取り分・peak）、`drv_i915_xd_bw_hold_sagv_off`（display 11〜13 で SAGV を display が持つ時、帯域の最大の点だけを残す mask を 1 ms + 50 ms の間 safe まで頼む）、`drv_i915_xd_bw_point_mbps`（plane の数の group: display 12 以降と display 11 の歩き方）、`drv_i915_xd_bw_allowed_points`。結果は `struct i915_xd` の `bandwidth`。値（mailbox の field、DRAM の種類ごとの burst・channel・幅・deinterleave、system agent の数）は Values referenced、手順は Procedure referenced（intel_dram.c・intel_bw.c・intel_pcode.c）。
- 付け替え: display.c の read_tables（旧 dram_detect + bw_init_hw）、bandwidth_start（旧 bw_init の SAGV の強制 off）、memory_and_clocks の SAGV の判断、core の DRAM の種類（`dcore->dram_type` は xd の I915_XD_DRAM_*、power.c の写しの関数を消した）、resident.c の QGV の帯域（`rlcd->bandwidth`・`rlcd->platform`、旧 `dstate`・`bw`）。internal.h の DRAM・帯域・SAGV の型と field、watermark.c・watermark.h（空、vmunix.mk から外した）を消した。xd-platform.h に DDR3・LPDDR3、display-regs-xd.h に PSF の点の mailbox の語を足した。
- 振る舞いは前と同じに保った: system agent の数は Alder Lake-P の物を全 platform で（Linux は TGL・RKL・ADL-S で別の値。前の code と同じ。直すなら p044 の platform の行で）、memory の記述が読めない時は点が 0 で SAGV は「切り替わる」扱い、点が読めない時は帯域を不明のまま（SAGV の強制 off は走る）。
- 試験: ktest-sync.c の DRAM・帯域の ktest 3 つを消し、新しい `host-xd-bandwidth-test`（28 checks: 同じ firmware の語で、DDR4・2 channel・4 QGV・3 PSF、group 1 が 2 plane、取り分は 38000 MB/s の上限・peak 266688、PSF の帯域、mask 0x30e と 3 回目の safe・51 ms で ETIMEDOUT、QGV 1 点は何も頼まない、拒みと不明の種類、display 11 と 13 の group）を run-xd-host-tests に入れた。
- 確かめた事: build 3 組（`kbuild.sh p043b1`）default warn=0（sha256 の先頭 f0156cd50774f276）、vke2 warn=0、execution warn=0。run-xd-host-tests（`build/p043b1-host`）all passed 29 本。WS075 80/0、WS237 flip 48/0・mode 71/0・backlight 21/0、WS229 bootfb 20/0。
- 実機で見る事: 5330 の log の `display: memory: raw=… type=…`・`QGV n: … MB/s for one plane`・`display: bandwidth: restricted=1 … answer=0 sagv=1`（前の `Forcing SAGV disable: mask` と同じ mask）、resident の `QGV mask 0x… -> allowed 0x… bw … MB/s` が前と同じ値。

## p043b の 2 つ目: power（段の境 2、次の世代へ。2026-10-17 P1 の調べ）

power.c（約 2570 行）は既に xd-power の殻: domain（Linux の enum i915_power_domain、`I915_PW_DOMAIN_*`）を `i915_pw_functions(display_ver, domain)` で xd の function の bit（`I915_XD_POWERS_*`）に写して `drv_i915_xd_power_get/_put/_put_later` を呼び、Linux 形の well の写し（`struct i915_power_well` の name・refcount・hw_enabled・ops）を数え直している。残る Linux の形: domain の enum と写し、well の写しの配列、DC の許す mask（`i915_get_allowed_dc_mask`・`i915_sanitize_target_dc_state`・`i915_sanitize_disable_power_well`）、`struct i915_display_core`・`struct i915_pw_ctx`・`struct i915_power_domains`（internal.h）、`drv_i915_lcd_power_*`（resident の run の hook、domain の番号で）。

呼び手（2026-10-17 の grep）:

| file | 使う物 |
| --- | --- |
| display.c | `power_domains_init/_cleanup/_init_hw/_driver_remove/_enable/_disable/_suspend`、`dc9_enter/_leave`、`power_well_is_enabled`（wells_on・log_wells）、`power_wells_sanitize`、`pwc`・`dcore` の用意 |
| resident.c | `lcd_power_get/_get_if_enabled/_put/_put_async`（run の hook、DC_OFF・DISPLAY_CORE・PIPE_A・TRANSCODER_A・PORT_DDI_IO/LANES_A/TC1）、`power_domains_enable`（takeover の後の start の参照の返し）、`gen9_dbuf_slices_update` |
| edp-device.c | `display_power_get/_put/_put_async`（DISPLAY_CORE）、`async_bind/_work/_flush_work_sync` |
| dmc-shell.c | `display_power_get/_put`（INIT） |
| dp-ext-kern.c | `display_power_get/_put/_put_async`（DISPLAY_CORE） |
| hpd.c | `display_power_get/_put`（DISPLAY_CORE・GMBUS） |
| tc-port.c | `display_power_get_set/_put_set`（function の集合を直に） |
| interrupts.c | `display_power_is_enabled`（PIPE_x・TRANSCODER_x） |
| head.c | `I915_PW_DOMAIN_NUM` |
| tests/execution/ktest-sync.c・ktest-gt.c | `power_domains_init`・`power_well_by_id/_enable/_disable/_is_enabled`（execution の組の ktest。DC_off の well の ktest は host-xd-power-test が同じ事を見ているか確かめ、見ていれば ktest を消す） |

既定の案（次の世代）:
1. 新しい own の `display-power.c/.h`（Linux の形を持たない）: 呼び手は xd の function の集合（`I915_XD_POWERS_*`）で `drv_i915_display_power_get(display, functions)`・`_put`・`_put_later(delay_ms)`・`_flush`・`_is_on`。xd-power の counts と switch（well の on/off の hook: pipes up/down・VGA reset・TC の PHY の健康）を持つ。well の写しの配列と domain の enum は作らない（log は xd の well の記述の名前と `drv_i915_xd_power_well_powered`）。
2. core の start/stop・suspend・DC9・DC off（start の参照の保持と返し）は `xd-core`・`xd-power` の直の呼びで display.c に（`struct i915_display_core` の dbuf の slice・fault の記録は display.c の小さな own の struct に）。DC の許す mask は xd-power の `drv_i915_xd_power_dc_policy` の入力として display.c で決める（Values referenced）。
3. 呼び手の付け替え（上の表）。resident.c の run の hook（`drv_i915_lcd_power_*` は domain の番号の int を受ける: run の kernel の hook の型 `power_get(ctx, int domain)` は xd-backend.h の口。番号を xd の function の bit の index にするか、hook の型を uint64_t の集合に変える）。
4. power.c・power.h を空にし vmunix.mk から外す。internal.h の power の型（`struct i915_power_domains`・`i915_power_well`・`i915_pw_ctx`・`i915_display_core`・domain の enum）を消す。check-sources.py を run-xd-host-tests に入れ、internal.h の Derived の表示（intel_display_limits.h の型）を直す。
5. build 3 組、run-xd-host-tests・WS075/237/229、`host-xd-power-test` と `host-xd-core-test` が新しい呼びの順を見ているか。段の境 2 の SHA を ws.md の表へ。

## p043b の 2 つ目: 実装（段の境 2、2026-10-17 q1229-i16、base 2c49ef470、commit 57967dfb0）

- 新しい own の `display/display-power.c/.h`（Linux の形を持たない。`struct i915_display_power` が display の中に 1 つ）: 呼び手は xd の function の集合（`I915_XD_POWERS_*`）で `drv_i915_display_power_get/_put/_put_later/_is_on`、遅れた返しの work は `struct i915_xd_power_work` を `_bind_work`、`_work`・`_flush`。well の on/off は xd-power の hook（pipe の割り込みの復元と drain、VGA の書き直し、Type-C PHY の健康）で、DC off の well は DC の離れ（CD clock と DBUF の slice の比べ、combo PHY の上げ直し）と DMC の program がある時の入り。始まりの参照 `I915_DISPLAY_POWER_START`（全 pipe と DC off、旧 INIT）は `_core_start` が core の後に取り well の状態を読む（firmware の要求の引き取り）、`_serve`（旧 power_domains_enable: 前後の well の数・DC の状態・食い違いを probe に）、`_hold`（旧 disable）、`_core_stop`（旧 suspend）、`_dc9_enter/_leave`、`_sanitize`・`_wells_on`・`_check`・`_log`・`_dbuf`、AUX channel の function `drv_i915_display_power_aux(ver, ch, thunderbolt)`。DC の許す mask（DC9|DC3CO|DC6 = 0x4000000a、idle の DC6）は Values referenced。
- 消した Linux の形: domain の enum と `i915_pw_functions` の写し、well の写しの配列（`struct i915_power_well`）、`struct i915_power_domains`・`i915_pw_ctx`・`i915_display_core`、`drv_i915_aux_*_power_domain`、`drv_i915_lcd_power_*`。power.c・power.h は空にし vmunix.mk から外した（vmunix.mk に display-power.c を足した）。dc9.c・dc9.h（p025 で呼び手 0、Q1 の削除待ち）は host の display の lib が compile して壊れるので空にした。
- 呼び手の付け替え: display.c（start・core・serve・stop・suspend・resume・sanitize・log）、resident.c（run の power hook を resident.c の static に移し、番号は function の bit（0〜63）と「well の要らない使い」64、Thunderbolt の無い channel は -1 で拒む。`I915_LCD_POWER_SLOTS`）、edp-device.c（AUX の function、work の bind、fini の log は xd の counts の stats）、dmc-shell.c（`dev->power`・`dev->xd`、START の参照）、dp-ext-kern.c（AUX の function、display core の get/put は well が無いので消し、lock の印を `aux_locked` に）、hpd.c（display core と GMBUS は switched well が無いので何も数えない hook）、tc-port.c（function の集合を直に、Dekel は `power->dkl`）、interrupts.c（`power->irq_ops`・`irqs_enabled`、pipe と transcoder は `_is_on(PIPE/TRANS)`）、head.c。i915_dp_env の使われていない power hook と数（power_refs・power_get_failures など）も消した。
- internal.h: Linux の base（u8〜s64、i915_reg_t・_MMIO・REG_*・BIT・_PICK_EVEN・ARRAY_SIZE・min/max・min_t/max_t・mmio_reg の helper、環境の約束の comment）は display の本番の使い手 0 なので消し（container_of と i915_lcd_mode の別名は残す: WS075 の host-output-test が使う）、Derived の表示（intel_display_limits.h）と Linux の probe の説明、DMC の (F1)〜(F3) の印を直した。display-internal.h の i915_display_irq・i915_lcd_kernel_deps は `struct i915_display_power *power`。
- 振る舞いの差（既定の案、実機で見る）: (1) 遅れた返し（put_later）は旧は domain の最後の参照の時だけ遅らせたが、新は work が bind されていれば常に遅らせる（同じ well の他の使い手がいる間は差が出ない。差は他の使い手が work より先に返した時に well が最長 100 ms 遅れて落ちる事だけ）。(2) display core・GMBUS の get/put（旧も well を数えなかった）は呼ばない。(3) 起動の log の文言: `intel_power_domains_init:`→`display power: N wells …`、`combo PHYs: N initialised`→`display power: combo PHYs up: N`、`DC9: entered/left`→`display power: DC9 entered/left`、`BIOS left unused …`→`display power: well … was left on by the firmware and is unused: turned off`、`well X: refcount=… hw_enabled=…`→`display power: well X: N users, on/off`、`DC_STATE_EN=… DC_off disable_calls=…`→`display power: DC_STATE_EN=…, DC off released N times …`、`display: core started: … init_ref=… sync_hw=…`→`… start reference held`、`edp fini` の async put の数は xd の stats。読む道具は無い（2026-10-17 の grep: plan/tools・各 WS の tests・tests/）。device->stage の fault の名前は `combo PHYs`・`power well 1`・`CD clock`。(4) well の数（wells_on）は旧と同じく mailbox の TC cold を数えない。
- 試験: ktest-sync.c の late fuse（fuse の遅れ）と ktest-gt.c の DC6 の ktest は host-xd-power-test が同じ事を見ている（fuse の遅れ・DMC の有無での DC6）ので消し、fake の fuse と well の model も消した。ktest の 2 file は UNUSED_PARAMETER のため `../../i915.h` を include。新しい `host-display-power-test`（19 checks: 始まりの DC と slice、AUX の function（display 12・13、Thunderbolt）、pipe B の well の get/put、DC off の DMC の有無、遅れた返し（work・flush・unbind）、drain の失敗で後の off を拒む、firmware が残した well の sanitize）を run-xd-host-tests に入れた。`check-sources.py` を run-xd-host-tests の最後に入れた（171 file、問題 0）。
- R11: 触った file の開発の管理情報の数（`grep -cE 'ws[0-9]{3}|WS[0-9]{3}|BUG-[0-9]+|review [A-Z][0-9]|\bP[0-9]+[a-z]? |\(F[0-9]\)'`）は全部 0。

### 確かめた事（p043b の power）

- build 3 組（57967dfb0 の tree、`build/tmp/kbuild.sh p043b2`）: default rc=0 warn=0（vmunix sha256 の先頭 539c8f4ecddac882）、vke2 rc=0 warn=0、execution rc=0 warn=0。
- run-xd-host-tests（`build/p043b2-host`）all passed: 30 本 0 failures（host-display-power-test 19 checks を含む）、段の順、check-sources 171 file。WS075 host-output-test 80/0、WS237 gen9 flip 48/0・mode 71/0・backlight 21/0、WS229 bootfb 20/0。`git diff --check` 0。
- 未実施: 5330・SV7・5320（全部の段の後に T1）。

## 実機で確かめる事（T1、全部の段の後に 1 回）

- 5330: 起動の log の `display: PCH type=8 … source=1`（ADP、以前の `intel_detect_pch: type=` と同じ種類と id）、`display: CD clock: …`、survey の判定。
- 5330（power）: `i915: display power: <数> wells (Alder Lake-P), DC states allowed 0x4000000a, entered when idle 0x2`、`display: core started: … start reference held`、`display: serving: wells_on A -> B dc_state=… verify_mismatches=0`（前の kernel と同じ A・B）、`display power: well …: N users, on` の一覧、`DC_STATE_EN=0x00000002`（DMC の program がある時、表示が止まっている間）、takeover の後の `the start's power reference went back: wells on A -> B`。suspend → resume で `display power: DC9 entered`・`DC9 left` と core の上げ直し（ユーザーの一覧）。USB-C の DP（TC の AUX の well、Thunderbolt の AUX）で `TC: power functions … did not come` が出ない。
- SV7（Gen9）: backlight（PCH の SPT/CNP の判定が xd の表に移った: `gen9 … backlight` の行）。

## 削除と掃除（merge の時に Q1）

- `src/drivers/gpu/i915/intel/` の connector.h・ddi.h・dkl.h・dp.h・hotplug.h・mreg.h・phy.h・plane.h・power-set.h・power.h・psr.h・ref.h・ref-inlines.h・trans.h・vbt.h・wm-dbuf.h・wm.h・fixed.h・fourcc.h（空）
- `src/drivers/gpu/i915/tests/display/host-xd-regs-test.c`（空）、`src/drivers/gpu/i915/display/color.h`（空）
- `src/drivers/gpu/i915/display/watermark.c`・`watermark.h`（空、p043b の帯域）
- `src/drivers/gpu/i915/display/power.c`・`power.h`（空、p043b の power）、`src/drivers/gpu/i915/display/dc9.c`・`dc9.h`（p025 の削除の行と同じ。この段で空にした）

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- p043a: `display: PCH type=8 id=0x5180 source=1`（ADP）、`display: CD clock: cdclk=179200 …`、survey lit。p043b: `display power: 29 wells (Alder Lake-P), DC states allowed 0x4000000a, entered when idle 0x2`、`core started: … start reference held`、`serving: wells_on 9 -> 9 … verify_mismatches=0`、well の users の一覧、`TC: power functions … did not come` 0

満たしていない・未確認の基準と残る物:

- `DC_STATE_EN=0x00000002` は未観測（survey 0x0、H6）
- takeover の後の `wells on 7 -> 2` は基準 `8 -> 3` と 1 つ違う（基準は DDI IO A の request を firmware の後に数えていた見込みだが、理由は未確認。survey の PWR_WELL_CTL は同じ）。基準の `serving` は 0 -> 0（旧の log の欠陥、phase046）なので比べられない
- SV7 の backlight、suspend の DC9: 未実施・ユーザーの一覧（beta2.md の U19〜U23）の項目で、未実施

P1 の見立て（Status は変えない。判定は Q1）: p043a は cleared にしてよい。p043b は保留（DC6 の H6、well の数の 1 の差の説明、SV7）。
