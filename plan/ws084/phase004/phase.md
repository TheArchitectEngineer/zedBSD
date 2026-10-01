<!-- awesome-plan project=zedbsd record=ws084p004 -->

# ws084-p004: parity の N1 との乖離 1〜3 の整理（調べて記録し、直すかを main が決める）

Phase ID: `ws084-p004`
Parent: [WS084](../ws.md)
Status: planned（2026-10-01 作成。ws.md の Resume point「残り: parity との乖離 1〜3 の整理（今は実害なし）」と「parity の N1 との照合」の節から）
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

（未実施）
