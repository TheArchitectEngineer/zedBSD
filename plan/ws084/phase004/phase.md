<!-- awesome-plan project=zedbsd record=ws084p004 -->

# ws084-p004: parity の N1 との乖離 1〜3 の整理（調べて記録し、直すかを main が決める）

Phase ID: `ws084-p004`
Parent: [WS084](../ws.md)
Status: in-progress（2026-10-07 P2: 完了の条件 1〜3 を済ませ、4 の判断を Q1 に求めた。p003 の dmesg の入力は未（5330 に届かない））（旧: planned、2026-10-01 作成）
Phase disposition: normal
承認: なし（Queue に入れるときに main が承認を記録する）
依存: p003 の結果を先に見る（p003 で失敗が出れば、その dmesg の行がこの Phase の入力になる）。

## 目的

ws.md の「parity の N1 との照合」の乖離 1〜3 を、今の code の位置・危険・実機で見える印・直すなら何を変えるかの形にまとめる。
**source は変えない**（今は実害が無く、10-10 以降は bug の修正だけの期間に入る）。直す差分は `exp/` に patch として置き、適用は main の判断。
p003 で乖離に結び付く失敗が出たときは、その失敗を直す Phase を main が別に立てる。

| # | 乖離 | 今の code |
| --- | --- | --- |
| 1 | takeover の後の preflight（parity は readout の前に 1 回、takeover の後は無し） | `src/drivers/gpu/i915/display/modeset.c:1734`（takeover）→ `:1768`（`drv_i915_lcd_kernel_preflight`、定義 `:1955`、拒む log `:2011`） |
| 2 | probe 時の sanitize（parity は firmware の画面があるとき encoder の PLL の対応付け・crtc・DPLL・未使用の well の 4 つを外した、今は well だけ） | `display/takeover.c:2141`（`drv_i915_modeset_sanitize_hw_state`、`:2154` の XXX の注記）、`i915_nogem_sanitize_encoder_pll_mapping`（`:5552`）・`i915_nogem_sanitize_crtc`（`:5577`）・`drv_i915_nogem_dpll_sanitize_state`（`:2256`）・`i915_nogem_power_domains_sanitize_state`（`:5652`） |
| 3 | INIT の参照の返却（parity は N1 の run の後、今は takeover の直後・再点灯の前） | `display/modeset.c:1745-1749`（`drv_i915_power_domains_enable`、`wells_on` の log）、遅らせる印は `display/display.c:2278-2279`、`display/power.c:1455-1465` |

## 完了の条件

1. 乖離ごとに、今の code の位置（file:line）、parity の該当（`git show 8022d26f:<path>` の file と行）、Linux の参照（`plan/ws031/linux-parity/linux-reference/i915-src/display/intel_modeset_setup.c`・
   `intel_display_power.c`・`intel_ddi.c` の関数）、実機で起こり得る症状と dmesg の印、直すなら変える所を表にして、この phase.md の「結果」に書く。
2. 乖離 1・3 の直しの案を `plan/ws084/phase004/exp/*.patch` に置く（適用しない）。kernel の build（warning 0）だけ確かめる: worktree で patch を当て
   `make -j64 BUILD=build/ws084-p004 ZEDBSD_CONFIG=plan/ws075/demo/config-demo-hdmi.mk build/ws084-p004/vmunix`、終わったら戻す。
3. host の試験 `src/drivers/gpu/i915/tests/display/host-native-decide-test.c` を走らせる道具を戻す（下の手順 3）。走れば結果を、走らなければ link の不足を記録する。
4. main に「直す・直さない」の判断を求める（判断の表を ws.md に足すのは main）。

## 手順

repo の root から。手引きは [guide.md](../guide.md)。

1. parity の tree を読む（file を作らず表示だけ）:
   ```
   git show 8022d26f:src/drivers/gpu/i915/parity/lcd/parity_lcd_kernel.c | grep -n 'n1_run\|preflight\|takeover\|release\|power_domains'
   git show 8022d26f:src/drivers/gpu/i915/parity/lcd/parity_modeset_setup_glue.inc | grep -n 'sanitize'
   git show 4ab09939:plan/ws031/handover/notes/n1-implementation-state.md | sed -n '1,200p'
   ```
   n1-implementation-state.md の「停止要因」1〜7 と ws.md の乖離 1〜5 を照らす。
2. 今の code を読む（上の表の行）。乖離 1 は「takeover の後の pipe が TRANSCONF の state の bit を残すときに preflight が拒む」場合で、p002 の 1・2 回目の失敗の直接の理由
   （ws.md）。今は DPLL を読むので止まり切るが、止まり切らない pipe（例: 別の firmware の版、外付けの画面）で再発し得る。案: takeover が `still_active == 0` を返したときは
   preflight の「idle」の検査を TRANSCONF の enable の bit だけにする、または preflight を takeover の前に移す（parity の順）。
3. host-native-decide-test の道具を戻す（WS031 の片付けで削除、`1e867fbf` で消えた）:
   ```
   mkdir -p plan/ws084/tests
   git show 1e867fbf^:plan/ws031/tests/display-host-lib.sh > plan/ws084/tests/display-host-lib.sh
   git show 1e867fbf^:plan/ws031/tests/native-decide-host-test.c | head -40
   git show 1e867fbf^:plan/ws031/tests/run-native-decide-host-test.sh > plan/ws084/tests/run-native-decide-host-test.sh
   sed -i 's|plan/ws031/tests/display-host-lib.sh|plan/ws084/tests/display-host-lib.sh|' plan/ws084/tests/run-native-decide-host-test.sh
   timeout 120 sh plan/ws084/tests/run-native-decide-host-test.sh; echo "exit=$?"
   ```
   （runner は `cd "$(dirname "$0")/../../.."` で root に移るので、plan/ws084/tests/ に置いても同じ形で動く。出力は `${TMPDIR:-/tmp}/ws031-native-decide-host-test`。）
   p001 の記録では `present.c` の `drv_i915_perf_*` が解決できず link できない。`src/drivers/gpu/i915/perf.c` を link の list に足して試す
   （display-host-lib.sh は `display/*.c` と `trace.c` だけを compile する）。host の試験は 2 分以内に終わる範囲で。
4. 乖離 1・3 の案を patch にする（worktree で編集 → `git diff > plan/ws084/phase004/exp/<名前>.patch` → `git checkout -- <file>`）。kernel の build だけ（完了の条件 2）。
5. 結果を書き、main に判断を求める。

## 未知

- 乖離 3: INIT の参照を早く返すと DC state（DMC）が再点灯の前に入り得る（p002 の 2 回目で `wells_on 8 -> 3`、`power well DC_off state mismatch`）。今の demo-lcd3 では
  実害が無いが、毎回の起動で同じかは p003 の 10 回の dmesg（`takeover: power_domains_enable: wells_on` の行）で見る。
- 乖離 2: parity が外した crtc・DPLL の sanitize を今の code が走らせることの影響。takeover の readout（`drv_i915_n1_readout`、`takeover.c:806`）より前に走るので、
  readout が読む状態を変えていないかを、p003 の dmesg の `takeover: readout:` の行（`modeset.c:3703`、DPLL と clock が 0 でない）で確かめる。

## 結果

### 2026-10-07 P2（Q1 の ACK「範囲 1・2 で ACK、修正の案の patch は当てない、fix するかの判断は案ができたら Q1 へ」）

入力の制限: p003 の素の 5330 の dmesg は無い（5330 に届かない）。下の「実機で起こり得る症状」は code と parity の記録（`4ab09939` の
`plan/ws031/handover/notes/n1-implementation-state.md` の停止要因 1〜7）からの見立てで、観測ではない。parity の tree（`8022d26f`）・`4ab09939`・`1e867fbf` は
local の git に有った（`git show` で表示だけ、file は作らない）。

| # | 今の code（2026-10-07 の行） | parity（8022d26f） | Linux の参照（`plan/ws031/linux-parity/linux-reference/i915-src/display/`） | 実機で起こり得る症状と dmesg の印 | 直すなら |
| --- | --- | --- | --- | --- | --- |
| 1 takeover の後の preflight | `modeset.c:1737`（`i915_resident_takeover` を呼ぶ）→ `:1770`（`drv_i915_lcd_kernel_preflight`、定義 `:1962`、TRANSCONF の bit 31・30、PLANE・DPLL・DDI の enable を見て拒む log `:2018`）。takeover（`:3676`）は `still_active == 0` を返すが、pipe の state の bit（TRANSCONF bit 30）の落ちるのを待たない | `parity/lcd/parity_lcd_kernel.c:2886`: preflight は readout の前に 1 回（`:829-832` で RUNNING を正常として通す）、takeover の後は preflight 無し | `intel_display.c:286` `intel_wait_for_pipe_off`（state の bit が落ちるのを 100 ms 待つ）、`intel_modeset_setup.c:266` `intel_crtc_disable_noatomic` | `i915: LCD-B preflight: the display is not idle (TRANSCONF 0x40000000 …)` → `resident display: not started (preflight: nothing was written)`（p002 の 1・2 回目。そのときの原因は PLL を失って止まれない pipe で、p002 の 2・4 回目で直った）。今残る危険は state の bit が遅れて落ちる場合と、止まり切らない pipe（前者は案で救え、後者は正しく拒む） | 案 `exp/d1-pipe-off-wait.patch`: takeover の成功の後、TRANSCONF の on の bit が落ちるのを 1 ms ごとに最大 100 ms 待ち、結果を `i915: takeover: pipe state after the stop: TRANSCONF … after N us` と log。preflight は変えない（落ちなければ今と同じく拒む） |
| 2 probe の sanitize | `takeover.c:2148` `drv_i915_modeset_sanitize_hw_state`: active な pipe があっても encoder の clock の gate（`:2218` → `:5569`、`crtc_linked == 0` だけ）、crtc（`:2228` → `:5594`、log だけで何も止めない）、DPLL（`:2232` → `:2263`、`active_mask == 0` かつ readout が完全なものだけ）を走らせ、未使用の well（`:2249`）だけ active な pipe の時に飛ばす | `parity/display_nogem.c:1558` `keep_firmware_display`: active な pipe がある N1 の build は 4 つとも飛ばす | `intel_modeset_setup.c:934` `intel_modeset_setup_hw_state`（4 つとも走らせる。readout が動いている pipe の encoder・PLL を数えるので安全） | parity が 4 つとも飛ばしたのは、当時の readout が encoder を pipe に結ばず PLL も数えなかったから（停止要因 1）。今の readout（`takeover.c:2003-2025` の `drv_i915_ddi_get_hw_state` で `crtc_linked`、`:2056` の `drv_i915_dpll_readout` で combo PHY の PLL を `ICL_DPCLKA_CFGCR0` から `active_mask` に）は Linux と同じく数えるので、firmware の encoder の clock と DPLL は「使用中」で残る。危険は readout が port A を結べない時: `i915: [ENCODER port A] is disabled with an ungated DDI clock, gate it`・`i915: DPLLn enabled but not in use, disabling` が出て画が消える／pipe が止まれない。p003 の dmesg で `P5c [ENCODER port A] hw state readout: enabled` と上の 2 行が無いことを見る | 直さない案（Linux どおり。parity の飛ばしは古い readout への回避）。p003 の dmesg で上の印が出たら、その時に直す Phase を立てる |
| 3 INIT の参照の返却 | `modeset.c:1747-1751`: takeover の直後、再点灯（preflight・buffer・`drv_i915_lcd_show_prepared`）の前に `drv_i915_power_domains_enable`。遅らせる印は `display.c:2459`、返却は `power.c:1491` | `parity/probe.c:1940`: N1 の run（readout・takeover・自前の modeset での再点灯・45 秒の console の mirror）が終わってから返す | `intel_display_power.c:2035` `intel_power_domains_enable`（`intel_initial_commit` が firmware の画面を引き継いで well を参照した後に呼ばれる） | 再点灯の commit が pipe の domain を取る前に誰も参照しない well が落ち、DC state（DMC）に入り得る: `power well DC_off state mismatch`、`takeover: power_domains_enable: wells_on 8 -> 3`（p002 の 2 回目）。demo-lcd3 では実害無し（回ごとに同じかは p003） | 案 `exp/d3-init-after-picture.patch`: 返却を `i915_resident_power_enable(display, when)` にまとめ、`i915_resident_window` の「picture up」（commit が panel の参照を持った後）で返す。picture up に届かない経路（takeover の失敗、preflight・buffer の失敗、show が window の前に終わった）でも 1 回だけ返す。log は `i915: takeover: power_domains_enable (picture up): wells_on A -> B` |

- 案の build（完了の条件 2）: 2 つの patch を当てた tree で `make -j16 BUILD=build/p2-ws084-p004 ZEDBSD_CONFIG=plan/ws075/demo/config-demo-hdmi.mk build/p2-ws084-p004/vmunix`
  → warning 0、vmunix の check PASS（d1 はその後に注の 1 行と空行だけ足した: style-check の blank-after-brace）。build の後 `git checkout -- modeset.c` で戻した。2 つの patch は
  どちらの順でも `git apply --check` が通る（hunk が重ならない）。source は変えていない。
- host-native-decide-test（完了の条件 3）: `plan/ws084/tests/display-host-lib.sh`・`run-native-decide-host-test.sh` に戻した（`1e867fbf^` の WS031 の道具から。元の `rm -rf` は
  `plan/tools/fresh-out.sh` の新しい directory に置き換え、削除の command は無い）。link の不足は `drv_i915_perf_*`（`perf.c` を production として link）と
  `sched_sleep`・`drv_pci_device_address`・`drv_i915_worker_sync_backlight`（`plan/ws084/tests/native-decide-stubs.c` の stand-in、呼ばれたら abort）。
  流すと 14 checks のうち 2 つが古い期待（p001 の前の「active な pipe は STOP」）で FAIL → `host-native-decide-test.c` の NATIVE-1（PROCEED・takeover・stop 無し）と
  TWO-WALLS（VT-d の translation が stop、active な pipe は記録だけ）を 2026-09-29 の判断に合わせて直した。結果 `sh plan/ws084/tests/run-native-decide-host-test.sh build/p2-ws084-nd4/test`
  → `native_decide_host_test: 14 checks, 0 failures`。
- 判断を Q1 に求める（完了の条件 4）: 乖離 1・3 を直すか（案はどちらも小さく、実機の確認は p003 の 10 回と一緒に要る）、乖離 2 は直さない案でよいか。

