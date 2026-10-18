<!-- awesome-plan project=zedbsd record=ws245-p025 -->

# ws245-p025: DC state と DC9（xd-power）

Status: cleared（2026-10-19 Q1 判定: 5330 の i20・i21 で基準どおり（判定の材料の節）。DC6 に入る事の未観測は書き直しの前からの性質で、2026-10-18 ユーザーの決定「Bug に移して cleared」で BUG-334 へ移した。DC6 の直しは主張しない）（旧: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない。dc9.c・dc9.h の削除は Q1 待ち））
Parent: [WS245](../ws.md)
Queue: q1229-i02（P1、ws245-p024〜p046 を design の順に実装して branch に積む）
設計: [design.md](../design.md) §6.2 の p025 の行、§2.5 の DC5/DC6・DC9、§6.7（south.h の SOUTH_CHICKEN1 の bit）

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-power.c/.h`（Zlib） | DC state（新）: `struct i915_xd_power` に方針（`dc_allowed`・`dc_target`・`dc_firmware`）、最後に書いた DC の bit（`dc_state`）、書き込みと書き直しの数。`drv_i915_xd_power_dc_policy`、`_dc_write`（許された state に絞り、書く前の DC の bit が最後に書いた物と違えば report、他の bit を保って書き、読み返すまで最大 100 回書き直し、6 回続けて読めたら終わり、読めなければ EIO と report）、`_dc_leave`（DC off の on: state 0）、`_dc_enter`（DC off の off: firmware が無ければ何もしない、DC6 の前に utility pin の PWM と DC6 の既の要求を report、DC5 も）、`_dc_off_held`（DC5・DC6（許されていれば DC3CO）の bit が無い）、`_dc_clear`（core の始めの rmw の clear、記録は変えない）、`_dc9_enter`（DC9 既に → EALREADY、DC5/6 → EBUSY、PW_2 on → EBUSY、割り込み on → EINVAL、DC9 を書いてから SOUTH_CHICKEN1 の reference clock の bit）、`_dc9_leave`（bit を戻してから、PW_2 on・DC5/6 → EBUSY、state 0）。DC3CO の target は持たない（design「DC3CO は持たない」）。手順は block ごとに `Procedure referenced:`（intel_display_power_well.c の gen9_set_dc_state・gen9_write_dc_state・gen9_disable_dc_states・gen9_dc_off_power_well_disable・skl_enable_dc6・assert_can_enable_dc6・gen9_enable_dc5・gen9_dc_off_power_well_enabled・bxt_enable_dc9・assert_can_enable_dc9・bxt_disable_dc9・assert_can_disable_dc9、intel_display_power.c の icl_display_core_init・icl_display_core_uninit）、書き直しの数は `Values referenced:`（gen9_write_dc_state） |
| `display/power.c`（殻） | DC_off の well の enable は `i915_pw_dc_off_on`（xd の `_dc_leave` に、CDCLK と DBUF の比べと combo PHY の立て直しは殻に残る: p027・p034・p029）、disable は `i915_pw_dc_off_off`（xd の `_dc_enter`）、is_enabled は xd の `_dc_off_held`。`drv_i915_display_dc9_enter/_leave` は xd の `_dc9_*` を呼んで log。core init は xd の `_dc_clear`、uninit は `_dc_leave`。殻の `i915_pw_dc_policy` が `pwc` の allowed・target・`dmc_has_payload` を xd に渡し、`i915_pw_dc_log` が report を今と同じ文の log にし、`pwc` の `dc_state`・`dc_state_writes`・`dc_state_rewrites` を xd から写す。削除: 公開の `drv_i915_gen9_set_dc_state`・`drv_i915_dc_off_enable`（power.c の外に呼び手は無い）、static の `i915_gen9_dc_mask`・`i915_gen9_write_dc_state`・`i915_gen9_set_dc_state_disable`・`i915_pw2_enabled`、`DC_STATE_EN*`・`I915_SOUTH_CHICKEN1`・`UTIL_PIN_*` の define、`dc9.h` の include。2962 → 2868 行 |
| `display/power.h` | 上の 2 つの公開の宣言を削除 |
| `intel/display-regs-xd.h` | UTIL_PIN_CTL・ENABLE・MODE_MASK・MODE_PWM（`Values referenced:` intel_backlight_regs.h）。MODE_MASK は Linux の 0xf << 24（旧の power.c は 0x1f << 24: Linux を優先、log の判定だけ） |
| `intel/display-regs-xd-south.h` | SOUTH_CHICKEN1 の reference clock の bit（`Values referenced:` i915_reg.h SBCLK_RUN_REFCLK_DIS。§6.7 の p006 の差分と同じ値） |
| `tests/display/xd-regs-map.h` | 上の 5 つの対 |
| `platform/amd64/vmunix.mk` | `display/dc9.c` を外した |
| `tests/execution/ktest-gt.c` | DC6 の ktest の private な display に xd の状態（ADL-P の記述、model の窓、数え）を与えた（DC の書き込みが xd を通るので）。R11: comment の `ws035-p066`・`T1-334`・`ws075-p020` と check の名前の `p7: P7-` を消した |
| `tests/display/host-xd-power-test.c` | DC の 16 の check（下）。49 → 65 |

段の phase.md の必須の項目:
- **消す file（Q1 が削除）**: `src/drivers/gpu/i915/display/dc9.c`、`src/drivers/gpu/i915/display/dc9.h`（本番の呼び手は 0: `git grep -n 'drv_i915_dc9_\|dc9.h' src` は dc9.c・dc9.h の中だけ。vmunix.mk から外した）。これらを compile する試験は WS052（incomplete、p009 は実機の UAT 待ち）の `plan/ws052/tests/host-i915-dc9.c` と `plan/ws052/tests/Makefile` の `i915-dc9` の規則で、dc9.c が消えると build できない。判断の期待値（入る: EALREADY・EBUSY（DC5/6、PW_2）・EINVAL、出る: EBUSY、PCH の bit の立て下ろしと他の bit の保持）は `host-xd-power-test.c` の DC9 の check に移した。WS052 の file は担当の範囲の外なので、`host-i915-dc9.c` の削除と Makefile の規則（66〜76 行）の除去を Q1 に頼む。
- 消す関数と残る呼び手: 上の公開 2・static 4（`git grep` で power.c の外に 0）。`drv_i915_dc9_enter_check`・`_leave_check`・`_south_chicken`（dc9.c）は本番の呼び手 0。
- 殻が更新する旧の写し: `pwc->dc_state`・`dc_state_writes`・`dc_state_rewrites`（display.c の well の log、`drv_i915_power_domains_enable` の `probe->dc_state_after`、ktest-gt が読む）、DC_off の写しの `hw_enabled`。
- 表 R: DC_STATE_EN の書き手はこの段から xd-power だけ（殻は xd を呼ぶ）。DC_STATE_DEBUG は p026 まで旧 `dmc.c` だけが書く（変えていない）。SOUTH_CHICKEN1 は xd-power（DC9 の bit）と xd-panel（2 つ目の PPS の IO select の読み）: bit が違い、xd-power は rmw。
- hook と lock: xd の DC の関数は lock を取らない。呼び手は旧のまま（DC_off の well は power domain の lock の下の xd の数えの switch から、DC9 は suspend/resume の段から）。DC_off の on の後の combo PHY の立て直しは殻の `i915_pw_dc_off_on` が続けて行う。
- R11: power.c 0、power.h 0、xd-power.c/.h 0、display-regs-xd.h 0、display-regs-xd-south.h 0、xd-regs-map.h 0、host-xd-power-test.c 0、ktest-gt.c 2 → 0（と check の名前の `p7: P7-` 14 個）。

## 旧と同じに保った物と違い

- 確かめ方 1: 旧の host 試験 `host-dp-test`（72）・`host-lcd-modeset-test`（123）の出力は p024 の後と同じ。
- 確かめ方 2（scratch の harness、git に入れない: `plan/ws245/temp/harness/wells25.c`・`run-wells25.sh`）: DC_off の enable → disable（firmware 無し）→ disable（firmware あり、DC6）→ enable → utility pin の PWM と DC6 の既の要求の下の disable → DC の bit の食い違いの下の enable → DC9 の enter（割り込み on で拒む）→ enter → 再びの enter（EALREADY）→ leave → PW_2 on の enter と leave（EBUSY）を、書き込みの列・`pwc` の `dc_state`・書き込みの数・`hw_enabled`・is_enabled で段の前（a249461a9 の `git archive`）と後で比べた。**ADL-P・TGL とも全部一致**（89 行）。
- 違い（log だけ）: DC6 の前の log の順は utility pin → DC6 既 → 食い違い → 書けない、の順のまま。utility pin の mode の mask は Linux の 4 bit（旧 5 bit）。
- 違い: DC3CO の target の枝（旧は `target == DC3CO` で DC3CO を書いた）を持たない。target は init で DC6 に決まり（`i915_sanitize_target_dc_state`）、DC3CO にはならないので、振る舞いは変わらない。
- core の始めの clear は旧と同じ記録しない rmw のまま（Linux は検証つきの書き込み: 5330 で動いている形を保った）。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- 起動、idle で DC6 に入る事（survey の DC_STATE_EN が基準と同じ。`i915: DC state mismatch`・`Writing dc state … failed` の log が増えない）。
- suspend → resume（DC9 の enter/leave、`i915: DC9: entered`・`left`）: 節目（p027）でユーザーの一覧。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p025/xd-host` → regs 498・io 50・platform 89・vbt 137・survey 57・aux 43・gmbus 24・panel 47・power 65、失敗 0。`I915_XD_STRICT` の検査は通る。
- DC の check: DC の leave が DC6 を消して他の bit を保ち記録する、書いていない DC の bit の report、読み返さない state の 100 回の書き直しと report、許されない state を書かない、firmware 無しで何も書かず held、firmware ありで DC6 と not held、utility pin の PWM と DC6 既の report、DC5 の target、core の clear が DC5・DC6・許された DC3CO だけを落とし記録を変えない。DC9: DC9 → SOUTH_CHICKEN1（他の bit を保つ）の順、既に → EALREADY、leave は bit を戻してから state 0、DC6 で EBUSY（読んだ値の report）、PW_2 on で enter・leave とも EBUSY、割り込み on で EINVAL（読みだけで書かない）。
- `sh plan/ws245/tests/check-xd-regs-linux.sh build/ws229-p001/linux/linux-6.8.12 build/p025/linuxcheck` → 1017 対が一致、差 0（新しい 5 対を含む）。
- 旧の host 試験と harness: 上。
- build: 下。
- 5330: 未実施（全部の段の後に 1 回）。

## build

- 2026-10-16: `sh build/tmp/kbuild.sh p021` で既定の vmunix（sha256 15a3d1dae08c3434…）と `I915_TESTS=y` の 5 組が rc 0・warning 0。ktest-gt の名前の変更の後に `execution` の組を作り直して rc 0・warning 0。`git diff --check` は空。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- DC_STATE_EN が基準と同じ（survey 1・2 とも 0x0、基準も 0x0）、`DC state mismatch`・`Writing dc state … failed` 0

満たしていない・未確認の基準と残る物:

- idle で DC6 に入る事は観測していない（基準・段とも DC_STATE_EN 0: DMC の program が serve の後に載ったため。phase046 の H6、main からの性質）。H6 の Q1・ユーザーの判断待ち
- suspend → resume の DC9: ユーザーの一覧（beta2.md の U19〜U23）の項目で、未実施

P1 の見立て（Status は変えない。判定は Q1）: 保留（基準どおりは満たしたが、Phase の目的の DC6 の入りは未観測。H6 の判断で決める）。
