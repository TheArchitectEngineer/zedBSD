<!-- awesome-plan project=zedbsd record=ws229-p003 -->
# ws229-p003: EU の命令の符号化を Gen9 に

Status: in-progress（2026-10-14 P1: 実装と host の受け入れを終えた。clearance は Q1 の判定待ち）
Disposition: normal
Parent: [WS229](../ws.md)
Queue: q978（P1）
依存: [p001](../phase001/phase.md)（設計 §3・§5、review-1 の F2・M5・M6・中 4）

## 範囲（p001 §5 の p003 の行）

- `compiler/eu.[ch]` を版ごとの命令の layout（`struct i915_eu_format`）で引く形にし、Gen9 の layout を足す。
- `intel/eu-encoding-gen9.h`（新）: Mesa 25.0.7 の Gen9（9+ の列）の field・opcode・type の値。
- generator（`compile.c`・`compile-compute.inc`）の Gen9 の差。
- `drv_i915_shader_compile_gen()` を足し、古い名前（`drv_i915_shader_compile()`・`_stage()`）は Gen12 の wrapper として残す（§5 の決定）。呼び手（`render/*`）は触らない。

## 受け入れ（§5）

- 既存の host の shader 試験の module を Gen9 でも compile し、Mesa の `brw_disasm --gen=skl` → `brw_asm --gen=skl` の往復で byte が一致。拒否すべき module は同じく拒否。
- Gen12 の byte は前後で同一（review-1 M6）。
- build warning 0。

## 実装（2026-10-14 P1）

- **encoder**: `struct i915_eu_format`（field の HI/LO、opcode の表、register と immediate の type の表、SWSB の有無）を Gen12 と Gen9 の 2 つの定数で持つ。`drv_i915_eu_init()` は Gen12、`drv_i915_eu_init_gen(buf, 9|12)` が版を選ぶ（他の版は buffer を poison）。generator が使う type の名前（`EU_TYPE_*`、Gen12 の符号）は Gen9 の表で訳す（UW 2・UD 0・D 1・F 7・HF 10/imm 11・V imm 6）。
- Gen9 だけの規則: SWSB と sync.nop を出さない（hardware の scoreboard）。immediate の src0 は src1 の file を ARF・type を src0 の type に（`brw_set_src0()`）。IF・WHILE・ENDIF は src0 に immediate D 0（IF は UIP を 0 に）。barrier の待ち（`EU_SYNC_BAR`）は `wait(1) n0`（他の sync の関数は poison）。
- SEND（review-1 F2 で設計 §3.4 の「1 payload に並べ直す」は撤回）: ex_desc が 0 なら SEND/SENDC（dst UW、src0 UD、src1 = descriptor の UD immediate、EOT bit 127、SIMD1 の message の src0 は `<0;1,0>`、ex_desc の重なり 94:91・88:85・83:80・67:64 は最後に 0 を書く = `brw_set_desc_ex()`、review-1 M6）。ex_desc が 0 でなければ SENDS/SENDSC（opcode 51・52、src1 の reg 51:44・file bit 36、dst の file bit 35、ex_desc[31:16] → 95:80、ex_mlen → 67:64、ex_desc の 15:10 と 5:0 が立っていれば poison）。generator の約 18 か所の 2 payload の SEND は変えずに済む。
- **generator**（`state->graphics_ver`）:
  1. compute の終了: descriptor に bit 4（"do not dereference URB"、`emit_cs_terminate()`）。
  2. barrier: r0.2 の mask を 0x8f000000（`emit_barrier()`）、待ちは WAIT（encoder）。
  3. fence: data cache の fence を BTI 0・commit で 1 本（SLM も含む）、続けて reply を `mov(1) null:UW reply:UW` で読んで stall（review-1 M5、`FS_OPCODE_SCHEDULING_FENCE`）。
  4. sampler の header: r0 の複写＋dword 2 に offset（`i915_compile_sampler_header()`、Gen12 は今のまま）。
  5. render target の header（review-1 中 4）: 位置 0 だけ・単一 source は今と同じ header 無しの SENDC。それ以外（位置 1 以上、複数の位置）は 2 register の header（r0・r1 の複写、dword 2 に RT の index、discard する shader は word 15 に f1.0）を src0、色を src1 の SENDSC。EOT の書き込みの header は r124（位置 0 の register、送信済みか未使用）、途中の書き込みは r96（fragment の値の register の上）。dual source は 2 色目を r116..r119 に移し、header を r122..r123 に置いて src0 = header＋1 色目（mlen 6）、src1 = 2 色目（ex_mlen 4）。新しい emitter `drv_i915_eu_flag_store_word()`。
  6. 2 payload の SEND: encoder の SENDS（上）。
- half の pack・unpack（review-1 M5 の mixed mode）: `brw_disasm` の validator（`special_restrictions_for_mixed_float_mode()`）が Gen9 の matfn.frag・vformat.vert の `mov(8) g<2>HF g<8,8,1>F` などを受け入れた。符号の変更は無し。

## 確かめ（2026-10-14、host だけ）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| Gen12 の byte が前後で同一 | 変更前（d1e743394）と後で、encoder の試験・vkdemo・mview・compiler/generality/feature-shaders の 70 kernel と ws101・vkcs の compute 18 kernel（計 88）を作り `cmp`（作業用の script は scratchpad、出力は `build/ws229-p003/`） | 88 個すべて同一（最後の commit 4891934b5 の後でも） |
| Gen12 の往復（既存） | `BRW_TOOLS=build/mesa-tools/build-asm/src/intel/compiler sh plan/ws031/tests/run-vk-gentool-test.sh` | WS031 gentool test PASS |
| Gen9 の往復と意図 | `plan/ws229/tests/eu-gen9-host.sh`（新） | PASS: encoder・全 vertex/fragment/geometry module（拒否すべき 7 個は拒否）・ws229 の MRT の 2 module（Gen12 でも往復）・compute 18 個の計 96 kernel が `--gen=skl` で byte 一致。encoder の byte は人が呼び出しと照らした期待の listing `eu-gen9-expect.asm` を brw_asm で組んだ byte と一致（review-1 M6）。generator の Gen9 の形 9 点（barrier の mask と WAIT、fence と reply の MOV、終了の bit 4、sampler の header、MRT の header の index と word 15、dual の mlen 6/ex_mlen 4）を listing で確認 |
| kernel の build | `make -j32 build/amd64/vmunix`（worktree の build/amd64、clang -Werror） | rc 0、warning 0、`amd64 vmunix check: PASS` |
| host の厳しい flag | `cc -std=gnu11 -Wall -Wextra -Werror -Wdeclaration-after-statement -pedantic` で eu.c・compile.c | warning 0 |

未実施:
- `plan/ws101/tests/host/run.sh` と `plan/ws075/tests/guard/run.sh` は走らせていない（どちらも script の中で `rm -rf` する。2026-10-06 の規則で担当は rm を走らせない）。両者が compile する kernel の byte は上の 88 個の比較で変更前と同一なので、Gen12 の結果は変わらない。Gen9 の分は eu-gen9-host.sh が同じ module を扱う。
- 実機・QEMU: なし（Gen9 の GT は QEMU に無い。message の意味（header の中身、fence の待ち、SENDS の payload の再利用の安全）は p005 の実機でしか確かめられない）。

## 残課題

- `tests/execution/eu-test.c` の Gen9 の kernel: GT の版（p002 の `gt->graphics_ver`）で選ぶ必要があり、p002 の契約が main に入ってから p005 で足す。
- 呼び手（`render/pipeline-prepare.c`・`render/blit.c`）は Gen12 の名前のまま。Gen9 で `drv_i915_shader_compile_gen(..., gt->graphics_ver, ...)` を呼ぶ 1 行ずつの差分は p004（呼び手の所有者）か p005。
- geometry shader の Gen9 の payload・URB の意味は符号化（往復）しか見ていない（設計 §3.1 の「同じ」の判断のまま）。
- `plan/ws229/tests/eu-gen9-host.sh` は回帰の試験として残す価値がある（Gen9 の encoder の唯一の試験）。Q1 が master.md の Tools・試験の一覧に登録するか判断する。
- 規約の全文の見直しは p007。
