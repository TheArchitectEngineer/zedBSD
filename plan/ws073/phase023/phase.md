<!-- awesome-plan project=zedbsd record=ws073p023 -->

# ws073-p023: amd64 の `AMD64_CURRENT_SPACE` の 2 回の load の監査（BUG-088）

Status: cleared（2026-09-28、監査。code の変更なし）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-088](../../bugs/BUG-088.md)（main の依頼、2026-09-28）

## 目的と受け入れ

`src/hal/amd64/space.c` の `AMD64_CURRENT_SPACE`（`amd64_percpu_current()->current_space`: `%gs:0` の self の load と field の load）を読む・書く
全ての所が、2 つの load の間で preempt されて別の CPU へ移りうるかを調べる。移りうるなら 1 回の `%gs` 相対の load にする（BUG-082 の
`hal_task_get_current()` と同じ。HAL の実装、hal.h は不変）。結果をどちらでも記録する。

## 結果: 全ての使う所は割り込みを禁止した中にあり、preempt されない

| 所 | 操作 | 割り込みの状態 |
| --- | --- | --- |
| `space.c` の page table の初期化の終わり（644 行付近） | store | 起動の途中、scheduler の前で CPU は 1 つ |
| `hal_space_switch()` の先頭の比較（978 行付近） | load | 呼び手が禁止: `hal_task_context_switch()`（`sched.c` の `sched_yield`・`sched_preempt` が `hal_irq_disable()` の後、`spin_unlock` は割り込みを戻さない）と `hal_task_exec_current()`（`exec.c` が直前に `hal_irq_disable()`）。amd64 で他に呼び手は無い（`grep hal_space_switch`） |
| `hal_space_switch()` の system への切り替え（988 行付近） | store | 自分で `hal_irq_disable()` |
| `hal_space_switch()` の user space への切り替え（1010 行付近） | store | `space_lock_enter()` が `hal_irq_disable()` |
| `shootdown()` の送り手の flush の判定（2325 行付近） | load | 関数の先頭で `hal_irq_disable()`（CPU の移動を防ぐための既存の注記あり） |

割り込みを禁止した中では timer の preempt も CPU の移動も起きないので、2 つの load は同じ CPU の state を読む。BUG-082 の `running_task` は割り込みを
許した kernel の code（fork）から読まれていた点が違う。**修正は要らない**ので code は変えていない。

他の CPU の値の読み（`amd64_percpu_get(cpu)->current_space`、`hal_space_destroy()` と `shootdown()` の対象の選択）は CPU を名指しで読むので、この問題に当たらない。

## 残り

- `hal_space_switch()` は hal.h の公開の API で、割り込みを許した呼び手が将来できれば先頭の比較が別の CPU の値を読みうる。hal.h の契約に
  「割り込みを禁止して呼ぶ」は書かれていない。防ぐなら (a) 比較を 1 回の `%gs` 相対の load にする（実装の変更、承認不要）か、(b) 契約を hal.h に書く
  （API の文言の変更、承認が要る）。今の呼び手では不要なので、どちらも行っていない（main の判断）。
- 検証は code の読みだけ（build・QEMU は不要、code を変えていない）。
