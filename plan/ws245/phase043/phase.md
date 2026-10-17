<!-- awesome-plan project=zedbsd record=ws245-p043 -->

# ws245-p043: 旧 code の残りの削除・試験の整理・出典の監査

Status: in-progress（2026-10-17 q1229-i15: p043a 実装・host 試験・build 済み（35b24c912・7de79d444）、p043b の帯域済み（d16688474）、power は実装中）
Parent: [WS245](../ws.md)
Queue: q1229-i15（P1）
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

## p043b（残りの計画）

2. power（段の境 2）: power.c の domain 層（`drv_i915_display_power_get/_put/_put_async/_is_enabled`・wells の写し・core の init/uninit・DC9）を xd-power・xd-core の直の呼びに付け替え（呼び手 dmc-shell・dp-ext-kern・hpd・edp-device・tc-port・interrupts・resident・display.c、ktest-sync.c・ktest-gt.c）、power.c・power.h を空に。
3. internal.h の Derived の表示と Linux の記述、check-sources.py を run-xd-host-tests に入れる。

## 実機で確かめる事（T1、全部の段の後に 1 回）

- 5330: 起動の log の `display: PCH type=8 … source=1`（ADP、以前の `intel_detect_pch: type=` と同じ種類と id）、`display: CD clock: …`、survey の判定。
- SV7（Gen9）: backlight（PCH の SPT/CNP の判定が xd の表に移った: `gen9 … backlight` の行）。

## 削除と掃除（merge の時に Q1）

- `src/drivers/gpu/i915/intel/` の connector.h・ddi.h・dkl.h・dp.h・hotplug.h・mreg.h・phy.h・plane.h・power-set.h・power.h・psr.h・ref.h・ref-inlines.h・trans.h・vbt.h・wm-dbuf.h・wm.h・fixed.h・fourcc.h（空）
- `src/drivers/gpu/i915/tests/display/host-xd-regs-test.c`（空）、`src/drivers/gpu/i915/display/color.h`（空）
- `src/drivers/gpu/i915/display/watermark.c`・`watermark.h`（空、p043b の帯域）
