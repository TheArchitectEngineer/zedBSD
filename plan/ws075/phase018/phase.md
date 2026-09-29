<!-- awesome-plan project=zedbsd record=ws075p018 -->

# ws075-p018: 性能 2: 非同期の実行器（ws031-p045）

Phase ID: `ws075-p018`
Parent: [WS075](../ws.md)
Status: planning（2026-09-29、[ws075-p008](../phase008/phase.md) から分けた）
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
