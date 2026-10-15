<!-- awesome-plan project=zedbsd record=ws241-p001 -->

# ws241-p001: run の 3.8 ms と Terminal＋Files の悪化の解析と直し

Status: in-progress（担当 P1、2026-10-15）
Parent: [WS241](../ws.md)

## 範囲と目標
- run（work channel の push の submit〜fence）の平均 3.8 ms の内訳を決める。
- Terminal＋Files の frame の悪化（T1-695 の 19.5〜20.8 ms → T1-696 の 27.8 ms）の理由を決める。
- Q1 の返事の後に直し、build（warning 0）・host 試験・T1 の依頼（T1-696 と同じ 3 場面＋nvrtx.vk.perf=1）。

## 1. 解析（2026-10-15、code と T1-696 の log。実機は未）

### 1.1 数の整理（T1-696、Terminal＋Files の 5 s 窓）
- runs 440 × 平均 3.8 ms ＝ 1.67 s。worker の node->lock の保持は frames 1870 × 1.6 ms ＝ 3.0 s（60%）。差の約 1.3 s は lock の下の CPU の仕事。
- compositor の 1 frame（5 s に 108）あたり: run 約 4 回（約 15 ms）＋ CPU 約 12 ms ＝ 約 28 ms。frame_ms 27.8 と一致。全 session の仕事が node に 1 本の worker（runtime.c）で直列になる。
- run の時間は投入・待ちの遅れではない: 同じ boot の greeter の時期は runs 11 avg 163 us（kernel.log 213 行）。desktop の run は 2〜4 ms を GPU が実行している（F2 で 2 ms spin の後は tick まで眠るので、観測は 3〜4 ms に寄る。GPU の実時間は 2〜3.5 ms と推定）。

### 1.2 code で見つけた原因の候補
| # | 内容 | 根拠 |
| --- | --- | --- |
| A | 毎 draw の `nvrtx_push_draw_pipeline` の最後に INVALIDATE_SHADER_CACHES（0x021c、0x1011＝instruction・data・constant）。NVK（MIT、`nvk_cmd_buffer.c`）は「暗黙に WFI」と注記し、draw ごとには出さない（NO_WFI 版 0x0da4 を command buffer の始めと barrier だけ）。毎 draw に GR の drain と instruction cache の破棄 | draw-push.c 663、vk-execute.c execute_draw |
| B | lock の下の CPU: command stream を blob（VRAM、BAR1 は NOCACHE）から kern_memcpy で写す。uncached の PCIe の読み（8 byte に 0.5〜1 us、8 byte 境界でなければ byte ごと） | vk-stream.c stream_blob、vram.c 797 |
| C | GPU の clock: nvrtx は P-state の問い合わせ・boost を持たない。RM の control は ogkm（MIT）ctrl2080perf.h に GET_CURRENT_PSTATE 0x20802068・PERF_BOOST 0x2080200a。実行中の RPC は gpu->call/reply を drain の thread と共有するので危険、起動の段（P8c の前）なら安全 | records.c・rpc.c |
| D | Terminal＋Files の悪化の推定: (1) F2 の spin が run ごとに 2 ms 丸ごと CPU を占める（GPU が 2 ms 以上かかるため、5 s に約 0.9 s）、(2) F1 で present が app を止めなくなり、app の frame が 1 本の FIFO に多く入る（rec submit 19.9→21.5、acquire 1.4→10 ms は vblank の待ちが present から acquire へ移った通常の形）。今の数では決められない | T1-695・T1-696 |

### 1.3 Q1 への案（2026-10-15 送付、返事待ち）
- X1（直し）: 毎 draw の INVALIDATE_SHADER_CACHES をやめ、push の始め（execute_start）に NO_WFI 版を 1 回。
- M1（計測）: run の始めと終わりの HOST semaphore の timestamp で GPU の実行時間を分けて出す。run あたりの draw・quad・push の量。
- M2（計測）: stream の複写の回数・KiB・us。
- M3（計測と切り替え）: 起動の時の P-state、boot の語 nvrtx.boost=1 で PERF_BOOST（最大、無期限）。
- F2 はこの回は変えない。次の回は数で決める（state の差分だけ書く、stream を cached な system memory へ、spin の短縮など）。

## 2. X1・M1〜M3 の実装（2026-10-15、Q1 の承認「X1 と M1〜M3 を案の通り、F2 はこの回は触らない」）

| 項 | 中身 | file |
| --- | --- | --- |
| X1 | 毎 draw の INVALIDATE_SHADER_CACHES（0x021c、暗黙の WFI）をやめ、新しい `nvrtx_push_draw_invalidate()`（NO_WFI 0x0da4、instruction・global data・constant）を push の始めに 1 回。vk の executor は execute_start、P9g（draw.c）の 2 つの push も pipeline の前に呼ぶ | draw-push.c・nvrtx-draw.h・vk-execute.c・draw.c |
| M1 | nvrtx.vk.perf=1 の時、work channel は 1 回の投入を 3 entry（始めの stamp＝timestamp 付きの release で WFI なし、segment、timestamp 付きの fence）にし、`nvrtx_work_run_time()` が GPU の timer で run の時間を出す。perf の行に `perf: gpu N avg Uus max Mus, run max Rus` と `perf: draws D quads Q push KKiB` | objects.c（`nvrtx_push_fence_timed`・`nvrtx_push_stamp`）・work.c・vk-node.c・vk-execute.c・node.c・nvrtx-node.h |
| M2 | blob からの command stream の複写を node の `nvrtx_vk_blob_read()` に移し、回数・KiB・us を数える。`perf: streams N KKiB Uus` | vk-stream.c・vk-node.c・nvrtx-vk.h |
| M3 | 新しい perf.c（段 P9p、P9w の最後から呼ぶ）: GET_CURRENT_PSTATE で `P9p: performance state Pn`、boot の語 nvrtx.boost=1（既定 off）で PERF_BOOST（最大、無期限）と `P9p: boosted to performance state Pn`。drain の前なので RPC は共有しない。失敗は log だけ | perf.c・work.c・nvrtx-gpu.h・platform/amd64/vmunix.mk（1 行）。stage.c・gsp.c は触らない |

確認:

| コマンド | 結果 |
| --- | --- |
| `make -j16 BUILD=build/p1perf/nvrtx ZEDBSD_CONFIG=plan/ws037/tests/config-amd64-nvrtx.mk build/p1perf/nvrtx/vmunix` | rc 0・warning 0・vmunix check PASS（cksum 3200698666 3607808） |
| `make -j16 BUILD=build/p1perf/ci ZEDBSD_CONFIG=config/ci/config-amd64.mk build/p1perf/ci/vmunix` | rc 0・warning 0・vmunix check PASS |
| `sh plan/ws037/tests/nvrtx-vk-d3-host-test.sh` | PASS 122（新: 0x021c なし、0x0da4＝0x1011 が run ごとに 1 回、draw＋quad の計数 4） |
| `sh plan/ws037/tests/nvrtx-vk-host-test.sh` / `nvrtx-gsp-host-test.sh` | PASS 60 / PASS 285（firmware の directory 無しの skip を除く） |
| `sh plan/ws037/tests/nvrtx-sm75-host-test.sh` / `nvrtx-sm75-desktop-test.sh` | 170 checks 0 failures（0x021c なし、0x0da4＝0x1011）/ 46 compiled 0 disagreed |
| 実機 | **未実施**（T1-697 へ依頼） |

T1-697 の依頼（Q1 経由）: T1-696 と同じ 3 場面（静止 20 s・Files 20 s・Terminal の無限 echo＋Files 20 s）を nvrtx.vk.perf=1 で、(a) nvrtx.boost=1 なし、(b) あり の 2 回。見る物: KWL PERF（frame_ms・rec submit・acquire）、kernel.log の perf の 5 行（runs・frames/flips・gpu・draws・streams）、`P9p` の行、`D3 fail`・`a command buffer did not run`・`rm: control` の有無、画面の崩れ（X1 で shader の cache の扱いを変えたので、Terminal・Files の文字と窓の絵）。
