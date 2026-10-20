<!-- awesome-plan project=zedbsd record=ws269-p012 -->

# ws269-p012: 規約の全文の見直し

- ID: ws269-p012（親 [ws269](../ws.md)）
- Status: in-progress（q1396-i01 で P1 の file の前半。P2 の file は後で P2）
- Disposition: normal
- Queue: q1396 / q1396-i01（2026-10-20 P1）

## q1396-i01 の範囲

P1 が書いた kernel の file: render-queue・render-context・render-sr-program・render-kick-layout・render-command・render-job・transfer-context・transfer-job・render-check・vk-queue・vk-transfer・render-target・render-target-layout・stage・stage-log・vk-image・fixed-area（.c・.h）と、P1 が触った userland の gpu-selftest。書き方だけを直す（動作は変えない）。clang-format は host に無いので使っていない（手で）。

## 照らした規則（plan/coding-style.md §14 の checklist と plan/guardrail.md の出典の書き方）

grep と読みで次を探した: 関数の結果を直に返す `return f(…)`、条件の中の関数の呼び出し、file の先頭の `Procedure/Value(s) referenced:`（使う block の直前に置く規則）、source の中の開発の管理情報（plan/・WS・Phase の番号）、1 行の `/* … referenced … */`。

## 直した物（19 か所）

| file | 直し |
| --- | --- |
| render-kick-layout.c | `return kick_data(…)` 2 か所を error を受けて失敗と成功の return に |
| render-queue.c | `return powervr_render_frame(…)`、`take_memory` の `return kern_calloc(…)`、条件の中の `is_faulted()` 4 か所（stop_begin・stop_poll・isolate・service_pass）、条件の中の `sched_ticks()`（stop_poll） |
| render-target.c | cleanup の待ちの条件の中の `sched_ticks()` |
| render-target-layout.c | 条件の中の `next_power_of_two()` |
| stage.c | `return powervr_stage_note_fail_value(…)` |
| transfer-job.c | 条件の中の `powervr_transfer_needs_rows()`（blit は呼ばない順を保った） |
| vk-transfer.c | 条件の中の `powervr_vk_object_retain()` |
| vk-queue.c | file の先頭の `Procedure referenced:` を route と run の block の直前へ、comment の「plan/ws269 p008」を外した |
| vk-image.c | file の先頭の `Procedure referenced:` を route の block の直前へ |
| userland/tests/gpu-selftest/main.c | comment の「plan/ws269/phase010k2」を外した、`return run_program_list(…)` |

## 残した例外・未了

- 一斉の読みは grep の型に当たる物まで。全部の文の段落の comment・宣言の順・空行の規則を 1 文ずつは見ていない（後半か、変更の時に）。
- 出典の 1 行目が `Procedure referenced: zedBSD src/…` の形（同じ行に path）が vk-transfer.c・render-check.c に残る（複数行の comment の中なので規則の「1 行の comment にしない」には当たる。改行の位置だけの違い）。
- host 試験の file（plan/ws269/tests/p010/*.c）は試験の道具として短い形（1 行の stand-in の関数）を残した。

## 確かめ（2026-10-20）

| 確かめ | 結果 |
| --- | --- |
| VF2・riscv64-virt・amd64 の vmunix | rc=0、warning 0 |
| 試験の image の config（vmunix と gpu-selftest） | rc=0、warning 0 |
| p009 run.sh | 175 passed, 0 failed |
| p010 run.sh | 全部 0 failed |
