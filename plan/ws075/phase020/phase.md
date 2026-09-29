<!-- awesome-plan project=zedbsd record=ws075-p020 -->

# ws075-p020: i915 の RPS（GT の周波数）の up/down の割込みと boost（F-054）

<!-- awesome-plan-current:start -->
Status: in-progress（2026-09-29、サブエージェント、worktree `.claude/worktrees/ws075-rps`、branch `wt/ws075-rps`）
Disposition: normal
Parent: [WS075](../ws.md)
Queue: main の依頼（2026-09-29「F-054 を WS075 の新しい Phase p020 として」）。Queue の ID は main が記録する
Resume point: 実装・host 試験・build・QEMU の boot test は済み（下の「経過」）。残り: 実機の passthrough（`build/p020/base.img` と `rps.img`）
<!-- awesome-plan-current:end -->

## 背景と目的

素の 5330 で GT が最低の周波数に張り付き約 8 fps だったので、今は `gt-power.c` の `drv_i915_rps_enable` が RP0（最大）を要求したまま（ws084-p002）。
デモで丸 1 日動かすと発熱と電池が問題になる。Linux の `intel_rps`（割込み・`rps_work`・up/down の閾値・boost）に沿って、負荷に応じて上げ下げする。

## 受け入れ

1. GT の PM の割込み（GEN11 の GTPM、`GEN6_PM_RP_UP_THRESHOLD`・`DOWN_THRESHOLD`）を受け、Linux の `rps_work` の式で周波数を上げ下げする（`RPNSWREQ`）。
2. up/down の閾値（`rps_set_power` の LOW・BETWEEN・HIGH の 3 段、EI と %）、`RP_CONTROL`、`RP_INTERRUPT_LIMITS`、`PMINTRMSK`（`rps_pm_mask`）を Linux どおりに書く。
3. boost: 始まっていない request を client が待つとき（Linux の `intel_rps_boost`、`!i915_request_started`）、boost の周波数（RP0）へ上げる。
4. 確認: host の試験、build（warning 0）、QEMU の boot test、実機の passthrough（Phase の終わりだけ、`flock /tmp/i915-hw.lock`）。
   - `h4-ctl.py latency A 10`・`rate A 10` で、RP0 固定と比べて fps が下がらない。
   - GT の周波数（`RPNSWREQ`・CAGF）が負荷で上がり、idle で下がる。

## 範囲の外

- irq.c の engine の部分（WS075 の別のエージェントの p009）。変えるのは GT の PM（OTHER class の GTPM の identity）の分配だけ。
- Linux の gen12 の既定（busy stats の timer、`intel_rps_set_timer`）と park・unpark（GT の wakeref が無い）。下の「Linux との差」。
- SLPC（GuC）。

## 設計（Linux 6.x の `gt/intel_rps.c`・`gt/intel_gt_irq.c`・`gt/gen6_ppgtt.c` の該当）

- `struct i915_rps` に実行時の状態: `cur_freq`・`last_freq`・`min_softlimit`・`max_softlimit`・`boost_freq`・`last_adj`・`power_mode`・閾値・`pm_events`・
  割込みの `pm_iir`・`pm_imr`・`pm_ier`・`waiters`、work queue と work、数（up・down・boost・変更）。
- `drv_i915_rps_enable`（GT の resume、`gen9_rps_enable` → `rps_reset`）: idle hysteresis、`pm_events = UP_THRESHOLD | DOWN_THRESHOLD`、最低の周波数を要求し
  閾値を書く（`rps_set(min, update)`）。
- `drv_i915_rps_start`（node の公開の前、forcewake を持った後）: work queue を作り、irq に GTPM の handler を登録し、`rps_enable_interrupts`
  （pending の identity の解消、IER・IMR、`PMINTRMSK`、`RP_INTERRUPT_LIMITS`）。
- `drv_i915_rps_stop`（device の停止）: 割込みを止め（`rps_disable_interrupts`）、handler を外し、work を待って queue を壊す。
- 割込み（`gen11_rps_irq_handler`）: `events = pm_iir & pm_events` を mask し、`pm_iir` に足して work を queue。
- work（`rps_work`）: `pm_iir` を取り unmask、`client_boost = waiters`、adj の倍々の式、`DOWN_TIMEOUT` の扱い、`power_mode` による adj の打ち消し、
  softlimit で clamp、`intel_rps_set`（`RPNSWREQ`、閾値、`RP_INTERRUPT_LIMITS`、`PMINTRMSK`）。
- boost: `drv_i915_rps_boost_begin/_end`。request の worker の同期の待ち（`i915_worker_queue_sync`）で、queue に先の item がある（始まっていない）とき begin、
  終わりで end。

## Linux との差（記録）

- Linux は graphics version 12 で busy stats の timer（`rps_timer`）を使い、割込みは 6〜11。このドライバは engine の busy の計測を持たないので、
  11 までの割込みの方式を使う（GTPM の割込みの block は Alder Lake-P にもある）。
- park・unpark（GT の idle で idle の周波数へ）は無い。idle では down の閾値の割込みが倍々で最低まで下げる（forcewake を node の公開の間ずっと持つので、
  GT は RC6 に入らず、RP の評価は続く）。

## 経過（2026-09-29）

- 実装: gt-power.c・gt-power.h・intel/gt-power.h（RPS の register）、irq.c・irq.h（GTPM の分配と `drv_i915_gt_pm_reset_iir`）、device.c（start・stop）、
  engine.c（enable に clock）、worker.c（同期の batch の boost）、tests/contracts（rps の contract test）、tests/execution/ktest-gt.c（期待値）。
- 実機用の image: `build/p020/base.img`（ws075-i915 の worktree の p008 の `irq.img` の複写、RP0 固定、kernel は main 4e95147f と同じ i915）と
  `build/p020/rps.img`（同じ image の ESP の vmunix を、この tree を同じ引数（demo の config、`I915_TEST_VBT=y`、`ZEDBSD_GRAPHICAL_BOOT=n`、
  `display=edp login=graphical`）で build した `build/p020-pt/vmunix` に替えたもの）。
- h4-ctl.py に `freq SECONDS MS [move]`（RPNSWREQ と RPSTAT1 の CAGF を xp で読む）。
