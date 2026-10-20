<!-- awesome-plan project=zedbsd record=ws269-p012 -->

# ws269-p012: 規約の全文の見直し

- ID: ws269-p012（親 [ws269](../ws.md)）
- Status: in-progress（q1396-i01 で P1 の file の前半、q1397-i01 で P2 の file の後半。clearance は Q1）
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

## q1397-i01（P2、2026-10-20）: P2 の file の後半

### 範囲

P2 が書いた kernel の file（`.c`・`.h`）:
- compiler: shader-spirv・shader-spirv-interface・shader-spirv-control・shader-spirv-instruction・shader-backend・shader（`.h`）・compute-program・usc-encode・pds-encode
- descriptor: descriptor-set・data-program
- 固定の program: render-program・render-load-program・transfer-program
- render: render-pass・render-draw・render-depth
- vk: vk-pass・vk-draw・vk-memory・vk-device

userland は vkstage と gpu-selftest の P2 の分。書き方だけを直し、動作は変えない。clang-format は host に無いので、手で直した。

照らした規則は前半と同じ（plan/coding-style.md §14 と plan/guardrail.md の出典の書き方）。grep と読みで、次の物を探した:
- `return f(…)`
- 条件の中の関数の呼び出し
- file の先頭の `Procedure/Value(s) referenced:`
- comment の中の開発の管理情報（plan/・design-p010.md・WS・Phase の番号）

### 直した物

| file | 直し |
| --- | --- |
| shader-spirv.c | `return shader_spirv_value_operand(…)` 2 か所を、局所の operand を返す形に |
| shader-spirv-interface.c | `blend_factor` の `return` 2 か所。条件の中の `color_format_known()`・`shader_spirv_scalar_count()` |
| shader-spirv-instruction.c | 命令の振り分けの `return reader_*(…)` 6 か所を、error を受けて失敗と成功の return に。`sine` の `return` |
| shader-spirv-control.c | `reader_arm` の `return reader_phi_moves(…)`・`return reader_region(…)` |
| shader-backend.c | `powervr_shader_compile`・`select_instruction`・`build_group_fetch` の `return f(…)`。register を作る関数（`temp_register`・`destination_of`・`operand_register` の 5 か所・`scratch_register` の 2 か所）を、局所の register を返す形に（段落の空行と comment を付けた） |
| render-draw.c | 条件の中の `round_units()`（2 つ）・`powervr_format_find()`・`rational_from_float()` |
| render-pass.c | 条件の中の `area_covers()`。comment の design-p010.md・p011 の参照を外した |
| transfer-program.c | 条件の中の `powervr_transfer_needs_rows()`。comment の design-p010.md を外した |
| vk-draw.c | pipeline の state の presence word の条件の中の `drv_i915_wire_read_u64()` 9 か所（`present` で受け、閉じ括弧の後に空行）。条件の中の `powervr_vk_object_retain()` 2 か所（NULL の判定の後で、短絡の順を保った）。comment の design-p010.md を外した |
| vk-pass.c | 条件の中の `powervr_vk_object_retain()` 2 か所。comment の design-p010.md を外した |
| vk-device.c | 条件の中の `usage_allowed()`。file の先頭の `Procedure referenced:` を route の block の直前へ。先頭の説明の古い文（「graphics は p010 で」「format に features が無い」）を今の形に。BVNC の出典の plan/ws269/design.md を、core の識別の register の値という書き方に |
| vk-memory.c | file の先頭の `Procedure referenced:` を route の block の直前へ |
| render-pass.h・render-draw.h・render-depth.h・render-program.c | comment の design-p010.md・p010g の参照を外した |
| userland/tests/vkstage/main.c | comment の ws269・design-p010.md の参照を外した。条件の中の `vkstage_check_pixel()` 8 か所と `strcmp()`。`stage_depth` の最後の文が成功の return になるように、snprintf を前へ |

- 上の grep の型は、照らした file では 0 件になった（`return f(` と、`if`・`while`・`for`・`else if` の条件の中の呼び出し）。
- shader-backend.c の「Phase 0/1/2」は ALU の phase で、hardware の用語なので残した。

### 残した例外・未了

- 一斉の読みは grep の型に当たる物まで。compiler（shader-*）は全体が短い comment の書き方なので、すべての文の段落の comment・`Succeeded:` の comment・宣言の順を 1 文ずつは見ていない。変更の時に直す。
- host 試験の file（plan/ws269/tests/p009・p010・p010-compiler）は試験の道具として短い形を残した。

### 前の項目（1）: keiland の frame の期待を main の挙動に

- P1 の 70b88646e（instance_jobs、QUEUE_JOBS_MAX 64）の後の実際の kick の順は、copy・R16・R4c・R16・R1c・clear。B は R16 と R16c の 11 個と R8c の 13 job が順に走り、submit は SUCCESS。
- 期待どおりだったので、試験の期待をこれに直した（7d2c90f9a）。

### 確かめ（2026-10-20）

| 確かめ | 結果 |
| --- | --- |
| p009 run.sh（切り替え 0・1） | どちらも 240 passed、0 failed |
| p010 run.sh | 全部 0 failed |
| p010-compiler run.sh | 396 passed、group 20 passed、0 failed |
| p010 selftest-host.sh | 0 failed |
| VF2 の vmunix | rc=0、warning 0 |
| VF2 の試験の image（config-visionfive2-selftest.mk の disk-image） | rc=0、vkstage・gpu-selftest・powervr の warning 0 |
| `git diff --check` | 出力なし |

- 未実施: riscv64-virt・amd64 の vmunix。
- 片付け（Q1 に依頼）: `build/p012/`。

## q1522-i01（P1、2026-10-21）: 2 回目（N4）、P1 の file

### 範囲

[proposal-next-phases.md](../proposal-next-phases.md) の (3) のうち P1 の担当の file。

| 群 | file |
| --- | --- |
| 1 | compute-program.c/.h、render-program.c/.h、render-command.c/.h、interface.h、gpu-mmu.c/.h、render-sr-program.c/.h。transfer-program.c は P2 が変えている途中なので除いた |
| 2 | firmware-boot.c、commands.c、render.c、render-queue.c、render-runtime.c、render-target.c、render-context.c、render-job.c、render-private.h、powervr-private.h |
| 3 | render-check.c |
| 4 | vk-queue.c、vk-device.c |
| 6 | vf2-gpu.c、vf2-pmu.c/.h、vf2-crg.c |
| 7 | gpu-selftest、vkimage |
| 追加 | vkmatrix、vkcoherency、vkreset |

### 見方

- 照らした規則は `plan/coding-style.md` の全文。
- 1 回目（de99d9964、2026-10-20 20:00 の main）から変わった行を重く見た（30 file、+7053/−218）。
- 機械の検査を 2 つ使った。どちらも scratchpad の使い捨ての script で、試験の道具としては残さない。
  1. 条件の中の呼び出し、`for` の中の宣言、条件演算子、関数の結果の直の return、複数行の注釈の形。
  2. 閉じ括弧の後の空行、注釈の無い制御文、複数行の本体の括弧。
- 検査に出た所は全部、人が読んで判断した。

### 直した物（動作は変えない）

- **関数の結果の直の return**:
  - compute-program.c `powervr_compute_shared_kernel_words`。
  - render.c `resource_make`・`powervr_resource_make_free_list`・`powervr_resource_make_region_headers`。
  - vkmatrix の 20 か所（`vkmatrix_finish`・`case_*` の呼び出し）。
  - それぞれ結果を受けて調べ、成功の return を別にした。
- **render-check.c の末尾の `return error;`**: 失敗と成功の return を分けた。
- **条件の中の呼び出し**:
  - render-queue.c: stop_poll の `powervr_command_wait_executed` は短絡の順を保って分けた。watchdog の `ms_ticks` は前に出した。
  - gpu-selftest: `ppm_header` を 1 段ずつにした。`frame_check` の fseek・fread を分け、説明の部分を `frame_describe` に分けた。
  - vkmatrix: 引数の名前の比較と、`wide_compare` の memcmp 2 か所。
  - vkreset: 引数の比較と clock_gettime。
- **条件演算子**:
  - 入れ子の物: firmware-boot.c の trace の量の macro 3 つを `#if` に、vkmatrix の clamp を `unit_clamp` に、render-job.c の load の probe の形を `LOAD_USC_PROBE_FORM` に。
  - 長い・意味のある選択: compute-program.c の workgroup の数と probe の index、render-command.c の single core の 3 か所、render.c の window の 2 か所、render-job.c の prefix の 2 か所、vk-queue.c の executor の 7 か所（`QUEUE_KERNEL_*` の macro と `if` に）、vkmatrix の band。
  - 残した物: 短い対称の選択（`"reset" : "kept"`、`x != NULL ? x->address : 0` の診断の行など）は §6 の許す範囲として残した。
- **まとめて確保して後で調べる形**:
  - vk-queue.c の `build_dispatch` を 1 つずつ確保・検査する形にし、compile と kernel の追加を `dispatch_kernels` に分けた。
  - render-job.c の `load_graphics` の compile を `load_graphics_compile`・`load_graphics_shaders` に分けた。
- **入れ子の block の中の宣言**: render-job.c の 3 か所を関数の先頭へ移した。
- **複数行の本体に括弧が無い**: render-target.c 3、render-check.c 2、vk-queue.c 2、firmware-boot.c 1、render-job.c 1、vkmatrix 1。
- **閉じ括弧の後の空行**: 69 か所に足した。
- **段落の注釈**: render-job.c・render-check.c・vk-queue.c・compute-program.c・render-queue.c・render-target.c・firmware-boot.c・vkreset の、注釈の無い制御文と段落に足した。
- **goto の後始末の label**: render-target.c と render-context.c で、label の後に注釈を足した。
- **source の中の plan/ と WS の参照**: 6 か所を外した。
  - compute-program.c/.h と vk-queue.c の `plan/ws269/tests/p008/…` は「Mesa 26.2.5 を host で drm shim で動かした時の stream」と書き直した。
  - render-job.c・vkimage・vkcoherency の `ws269-p0xx` は消した。

### 確かめ（2026-10-21、worktree /home/awe/zedBSD-worktrees/p1、QEMU と板は使っていない）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| VF2 の kernel と userland | `timeout 3000 make ZEDBSD_CONFIG=plan/ws269/tests/p010/config-visionfive2-selftest.mk BUILD=build/q1522 -j16 vmunix build/q1522/bin/{vkreset,vkmatrix,vkcoherency,gpu-selftest,vkimage}` | rc=0、warning 0 |
| p009 の host 試験 | `sh plan/ws269/tests/p009/run.sh build/q1522-host/p009` | 505 passed、0 failed |
| p010 の host 試験 | `sh plan/ws269/tests/p010/run.sh build/q1522-host/p010` | rc=0、15 本とも 0 failed |
| gpu-selftest の host 試験 | `sh plan/ws269/tests/p010/selftest-host.sh build/q1522-host/st` | 0 failed |
| 空白 | `git diff --check` | 出力なし |

### 残した物・未了

- **vkmatrix・vkcoherency の段落の粒度**: 試験の program で、合わせて約 3,700 行ある。次の形が多く残る。
  - `if (status == VK_SUCCESS) status = …` の連鎖。
  - 1 つの段落に複数の操作を入れた書き方。
  - 後始末の `if (x != NULL) destroy` の並び。
  
  検査に出た「注釈の無い制御文」約 80 か所のうち、直したのは明らかな物（最後の return、結果の判定）だけ。全部を段落に分けるには書き直しになるので、ここでは行わなかった。続けるなら別の Queue にする。
- **compute-program.c の probe の program**: `if (error == 0) error = …group(&groups[count++], …)` の連鎖が残る。診断の probe で、動作を変えずに書き直すと長くなるので残した。
- **群 5（compiler）と compute-check.c**: P2 の担当。transfer-program.c も P2 が変えている途中なので見ていない。
- **commit**: 5960d5a97 と 4d6e01797（main 987679cba の上）。板での確認はしていない（style だけの変更で、host 試験と build で確かめた）。
