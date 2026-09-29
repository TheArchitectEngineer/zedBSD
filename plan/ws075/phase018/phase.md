<!-- awesome-plan project=zedbsd record=ws075p018 -->

# ws075-p018: 性能 2: 非同期の実行器（ws031-p045）

Phase ID: `ws075-p018`
Parent: [WS075](../ws.md)
Status: uncleared（2026-09-29 の試み: 着手前の計測で、受け入れの構成の遅さは実行器の同期ではなく compositor の GPU の描画の重さと分かった。実装は未着手。計画の判断待ち、下の「計測」と [ws075-p021](../phase021/phase.md)）
Phase disposition: normal

## 範囲（正本は WS031 の [phase018](../../ws031/phase018/phase.md) の 1）

vkQueueSubmit を queue して待たずに返し、fence・semaphore は GPU の完了で signal する。state・batch の heap の多重化、
command buffer と資源の寿命の管理。1 frame の直列の GPU の往復（render・libvulkan の present の copy・compositor の copy）を
重ねられるようにする。

今の形（2026-09-29 の調べ）: `render/draw.c` の `drv_i915_gfx_flush()` が `drv_i915_worker_run_sync()` で batch の終わりまで
呼んだ thread を眠らせる。batch・state の object は session に 1 つ（`struct i915_gfx_session`）なので、次の submit は前の
batch の終わりを待たないと書けない。

## 着手前に決めること

- batch・state の object の数（2 か 3 の ring）と、寿命（command buffer・image・buffer・descriptor が GPU の実行中に消されたとき）。
- fence・semaphore の signal の所（`render/fence.c`・`render/sync.c`、request の完了の配達 `drv_i915_request_complete_list`）。
- UAPI（`include/drivers/gpu.h`）の変更が要るか。要るなら事前に提示する。

## 依存

[ws075-p008](../phase008/phase.md)（完了を割込みで知る）。

## 設計に生かす所見（p008・p009）

- p008: desktop だけのとき入力から flip まで約 49 ms・約 20 flip/s。worker の待ち方（busy-wait → 割込み）では変わらない。frame の周期は
  submit の GPU の往復 2 回と present（copy と flip の待ち）の直列の和と見る（推測）。
- p009: App Home の 8 app を開き Gears が回ると 3.1 flip/s・中央値 166 ms。全 client の submit と present が 1 つの worker で直列に待つ。
- 物差し（受け入れ）: `h4-ctl.py rate A 10`・`latency A 10` を (a) desktop だけ、(b) 8 app と Gears、で変更の前後に。

## 計測（2026-09-29、着手前、実機の passthrough）

main（p009 の merge の後、a6e4970d 系 + 5e93e06f）の demo の passthrough の image（`build/ws075-p018/base.img`）。
main の USB HID・PS/2 の変更の後、desktop だけの物差しは大きく良くなった（p008 の 19.6 flip/s・49 ms → **56.4 flip/s・15.8 ms**）。

8 app を開いた desktop（`plan/ws075/tests/hdmi/apps8.sh`）: **7.4 flip/s、latency 中央値 31.6 ms（7.3〜115）**、入力なしで 8 flip/s（Gears）。

gdbstub で読んだ内訳（`plan/ws075/tests/hdmi/perf-gdb.sh`: 5 秒の窓の i915 の perf の合計、kernel の log を読まない）:
5 秒に batch 1085、engine（submit から CSB の完了まで）3363 ms = **GPU 67% が塞がる**。CPU は 4 vCPU の 75% が idle（gdb の `info threads` の 30 回の標本、halted 90/120）。

どの process が GPU を使うか（flush の入口の hardware breakpoint で current の process の名前と session、`worker.c` に足した
context ごとの engine の時間 `engine_ns`・`engine_runs` を gdb で 10 秒の差で読む）:

| session | process | 1 秒の run | engine の占有 | 1 run |
| --- | --- | --- | --- | --- |
| a4d130 | /bin/wayland（compositor の描画） | 9.1 | **90.5%** | **99.6 ms** |
| b9a8b0 | /bin/xserver | 20.3 | 2.4% | 1.2 ms |
| b98110 | /bin/zgears | 3.9 | 1.6% | 4.0 ms |
| a60c20 | /bin/wayland（display の present の copy） | 0.9 | 0.7% | 7.3 ms |

（この表の run は手違いで app が重複して開いた状態 = 窓が約 12。そのとき 1 flip/s。compositor の 1 frame に約 9 batch × 100 ms。
Model viewer・Browser を閉じても変わらない、pointer の位置にも依らない。）compositor 自身の CPU の側の数（zterm で `/run/user/*/session.log` の
`ZWL PERF` を表示）: 8 app のとき compose 8 frame/s、1 frame の `submit+present` 77.5 ms。

**結論**: 窓が多い desktop の遅さは compositor の合成の GPU の時間（1 batch 約 100 ms、窓の数に比例）で、GPU は既に 70〜95% 塞がっている。
同期の往復を非同期にしても GPU の仕事は減らないので、この物差しはほとんど良くならない（見込み）。desktop だけのときは既に 56 flip/s で
60 Hz に近い。

原因の見立て（コード）: compositor の `panel.frag` は 1 つの shader に 7 つの mode（glass・shadow・image・solid・ring・text・blur）を持ち、
mode は push constant（draw の中で一様）。i915 の compiler は分岐を if 変換し、**全ての mode の命令を全ての pixel で実行する**
（`compiler/spirv.c` の説明: every block runs for every channel）。texture の sample は 1 pixel に 8 回（glass 1・image 1・text 1・blur 5）で、
image の draw でも 8 回の sampler の message を出す。Venus（host の GPU の driver）は一様な分岐を飛ぶ。

提案: [ws075-p021](../phase021/phase.md)（compiler: どの channel も走らない block を飛ぶ）を p018 の前に。p018 は p021 の後に計り直して要否を決める。

## 変えたもの（この試み）

- `src/drivers/gpu/i915/worker.c`: `struct i915_worker_context` に `engine_ns`・`engine_runs`（context ごとの engine の時間。worker だけが足し、
  debugger で読む）。
- `plan/ws075/tests/hdmi/apps8.sh`（8 app を順に開く）、`perf-gdb.sh`（i915 の perf の窓を gdb で）、`h4-ctl.py`（`rate PIPE SECONDS [X Y]`、
  `keys` の `* "`）、`hdmi-h4-hw.sh`（下）。

### 実機の harness の事故と対策（2026-09-29）

`hdmi-h4-hw.sh start` を `timeout 300` の下で走らせたため、lock（RPS の agent の run が持っていた）を待つ間に start が殺され、その後の
`ctl`（apps8 の click）が **RPS の agent の QEMU**（共有の `~/bigbang/h4`）に届いた（20:37〜20:40 頃、App Home の app を開いた）。
対策: start は lock を得たら OUTDIR を `/tmp/i915-h4-owner` に書き、`ctl`・`fetch`・`stop` はこの tree の run が lock を持つときだけ動く
（持たない `stop` は自分の待ちだけを終える）。start を timeout で括らない（script の説明に書いた）。
