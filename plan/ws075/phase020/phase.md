<!-- awesome-plan project=zedbsd record=ws075-p020 -->

# ws075-p020: i915 の RPS（GT の周波数）の up/down の割込みと boost（F-054）

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-29、サブエージェント、worktree `.claude/worktrees/ws075-rps`、branch `wt/ws075-rps`。実機の passthrough で RP0 固定より fps が上がり、idle で最低の周波数に下がることを確認。素の 5330 は未実施）
Disposition: normal
Parent: [WS075](../ws.md)
Queue: main の依頼（2026-09-29「F-054 を WS075 の新しい Phase p020 として」）。Queue の ID は main が記録する
Resume point: なし（素の 5330 での確認はユーザーの作業。下の「未実施」）
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

## 実機で分かったこと（設計の改め）

最初の実装は受け入れの 1 のとおり GT の PM の割込み（Linux の graphics version 6〜11 の方式）で追ったが、実機の passthrough で次が分かった。

- **up/down の割込みは Alder Lake-P で来ない**。RPS の割込みを有効にし mask を外しても（`GPM_WGBOXPERF` の enable 0x00300000、mask 0xffcf0000）、
  負荷の間も up・down の event は 0 回だった（`hw-rps2` の log の `up=0 down=0`）。`PMINTRMSK` の読み返しは書いた `~0x30` ではなく `0x80000388`。
  Linux も graphics version 12 ではこの割込みを使わず、engine の busy stats の timer（`rps_timer`）で追う（`intel_rps_enable()`: `has_busy_stats` → timer、
  6〜11 だけ割込み）。
- そこで Linux の gen12 の方式に改めた（割込みの経路は `use_timer = 0` として残し、contract test で試す）:
  - **busy の時間の評価**（`rps_timer` + `rps_work` の判定）: worker が request を engine に出してから終わりを見るまでを busy の時間として数え
    （`drv_i915_rps_busy_begin/_end`。Linux の engine の busy stats の代わり）、1 ms（変化の後）から 20 ms まで倍々の間隔で、前回からの busy の割合を
    閾値（95 %・85 %）と比べて up・down の event にし、`rps_work` と同じ式（倍々の step、soft limit）で周波数を決める。
  - **park・unpark**: 最低の周波数で idle（busy 0、待つ client なし）なら評価を止め（park）、次の request で RPe から評価を始める（unpark、Linux の
    `intel_rps_unpark()`）。
  - boost（始まっていない batch を待つ client）はそのまま。
- baseline（RP0 固定）の実機の CAGF は **200 MHz** だった（RPNSWREQ は 1200 MHz を要求、`RP_CONTROL` は 0）。p020 の後は CAGF が要求に従う（負荷で 1150 MHz）。
  `RP_CONTROL` を書くこと（`rps_set_power`）が要求を効かせていると見る（推測。素の 5330（ws084）では RP0 固定で fps が上がっていたので、passthrough と
  素の機械で違う可能性がある）。

## 実装（最終）

| file | 内容 |
| --- | --- |
| `src/drivers/gpu/i915/gt-power.h`・`gt-power.c` | RPS の実行時の状態、enable（最低の周波数と LOW_POWER の閾値、`RP_CONTROL`）、start（work queue、RPe への引き上げ、busy の評価の timer、または割込み）、stop、割込みの handler と work（`rps_work`）、busy の評価（`i915_rps_tick`）、park・unpark、boost、判定の関数（`drv_i915_rps_next_freq`・`_pm_mask`・`_limits`・`_pm_interval`）、変化の log（最初の 24 回） |
| `src/drivers/gpu/i915/intel/gt-power.h` | RP の register（`RP_INTERRUPT_LIMITS`・`RP_CONTROL`・閾値・EI・`RPSTAT1`） |
| `src/drivers/gpu/i915/irq.c`・`irq.h` | OTHER class の GTPM（instance 1）の identity を RPS の handler へ（`pm_handler`）、`drv_i915_gt_pm_reset_iir`。engine の部分は変えていない |
| `src/drivers/gpu/i915/device.c` | node の公開の前に `drv_i915_rps_start`、停止で forcewake を返す前に `drv_i915_rps_stop` |
| `src/drivers/gpu/i915/engine.c` | `drv_i915_rps_enable` に GT の clock を渡す |
| `src/drivers/gpu/i915/worker.c` | request を出してから終わりを見るまでを busy に数える（`i915_worker_run`）。同期の batch が先の work の後ろに並ぶとき boost（`i915_worker_queue_sync`） |
| `src/drivers/gpu/i915/tests/contracts/rps_contract_test.c`（新）・`run.sh`・`README.md`・`host_kernel.c`・`host_unreached.c` | RPS の contract test（下）。`kern_deadline_after` を host の stand-in に移した（delayed work が使う） |
| `src/drivers/gpu/i915/tests/execution/ktest-gt.c` | 期待値を RPNSWREQ=最低・LOW_POWER の閾値・判定の関数に |
| `plan/ws075/tests/hdmi/h4-ctl.py` | `freq SECONDS MS [move]`（RPNSWREQ と RPSTAT1 の CAGF を xp で読む） |

## 確認（実行したもの）

| 確認 | 結果 |
| --- | --- |
| `src/drivers/gpu/i915/tests/contracts/run.sh`（全部、plain と ASan/UBSan） | PASS（mmio dma pci rpm pte sync rps）。rps は 74 checks、0 failures |
| rps の contract test の中身 | caps、enable（RPNSWREQ=最低、UP_EI 19200・UP_THRESHOLD 18240・DOWN_EI 38400・DOWN_THRESHOLD 32640、RP_CONTROL 0x592）。割込みの経路: start の enable・unmask・RPe、handler の mask と work の queue、倍々の up・down、power mode の切替、PMINTRMSK と LIMITS、boost、stop。busy の経路: 忙しい間は倍々で RP0 へ、RP0 で間隔 20 ms、半分の busy は下がる、idle で最低まで下がって park、request で unpark して RPe、client が待つ間は RP0 を保ち park しない、stop |
| kernel の build（main の config、demo の passthrough の config） | rc=0、warning 0 |
| ktest（`ktest-gt.c`） | 未実施（test の kernel の build の道具が無い。ws084 と同じ） |
| 規約（`plan/tools/style-check.py`） | 新しい指摘は critical section の本体と unlock の行の paragraph-comment（既存と同じ形）と contract test の `contract_section`（既存の contract test と同じ形）だけ。irq.c の指摘の数は変更の前と同じ |
| QEMU の boot test（GPU なし、main の `build/amd64/hdd-image.img` の複写の ESP の vmunix をこの tree の kernel に替えた `build/p020/hdd-image.img`） | PASS（`build/p020/boot-test/login.png`） |

実機（5330 の VFIO passthrough の QEMU guest、`plan/ws075/tests/hdmi-h4-hw.sh`、`flock /tmp/i915-hw.lock`）。image は `build/p020/base.img`
（ws075-i915 の p008 の `irq.img` の複写、i915 は main 4e95147f と同じ、RP0 固定）と `build/p020/rps.img`（同じ image の ESP の vmunix だけを
この tree の同じ引数の kernel に替えた）。desktop の起動の約 130 秒後から測った:

| 物差し | RP0 固定（hw-base） | p020（hw-rps3） |
| --- | --- | --- |
| `latency A 10` | 10/10、中央値 49.3 ms（48.7〜115.4） | 10/10、中央値 32.5 ms（16.0〜78.4） |
| 入力なしの 3 秒の flip | 0 | 0 |
| `rate A 10` | 19.8/s・19.6/s | 55.9/s・53.7/s |
| `freq 5 100`（idle） | 要求 1200 MHz、CAGF 200 MHz | 要求 100 MHz（最低）、CAGF 100 MHz |
| `freq 5 20 move`（pointer の移動の間） | 要求 1200 MHz、CAGF 200 MHz | 要求 平均 657 MHz（100〜1200）、CAGF 平均 646 MHz（100〜1150） |
| 画面 | desktop（`build/ws075-shots/ws075-p020-base-desktop.png`） | desktop（`build/ws075-shots/ws075-p020-rps-desktop.png`） |

- kernel の log（hw-rps3）: `i915: rps: started timer=1 min=6 RPe=24 RP0=72 freq=24`、その後 busy の down（24→23→21→17→9→6）、park、request での unpark
  （6→24）、up（9→10）のくり返し。
- 割込みの版（hw-rps2、idle の検査つき）: up・down の割込みは 0 回。boost（6→72）と idle の下げだけが動いた（上の「実機で分かったこと」）。
- 最初の p020 の計測（hw-rps、20:35〜20:44）は、WS075 の別のエージェントの pointer の操作が 20:37〜20:40 に届いた可能性があるので使わない（main の連絡）。
  上の表の hw-rps3 は 21:09 からで、lock を持っていることを確かめて測った。

## 未実施・制限

- 素の 5330（USB の image）: 未実施（ユーザーの作業）。確かめる点: desktop の操作中の present の率が WS084 の 24.5/s より下がらないこと、idle で
  RPNSWREQ が最低に下がること、発熱。
- 消費電力・温度の数値は測っていない（passthrough の guest からは読めない）。
- busy の時間は worker が request の終わりを見るまでで数える（CPU の側の時間。engine の busy stats の register ではない）。worker が 1 tick の期限で
  起きる分の遅れを含む。
- boost は同期の batch の queue だけ（非同期の request の待ちは GPU core の側で、i915 に見えない）。
- 割込みの経路は Alder Lake-P で event が来ない。原因（`PMINTRMSK` の読み返し `0x80000388`、GuC への redirect の bit か）は調べていない。
- ktest は未実施。
