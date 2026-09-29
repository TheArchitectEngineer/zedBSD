<!-- awesome-plan project=zedbsd record=ws075p021 -->

# ws075-p021: 性能: compiler が どの channel も走らない block を飛ぶ（提案）

Phase ID: `ws075-p021`
Parent: [WS075](../ws.md)
Status: in-progress（2026-09-29 main の判断「p021 を先に進めてください」）
Phase disposition: normal

## 動機

8 app の desktop で GPU の 90% が compositor の合成（1 batch 約 100 ms）。compositor の `panel.frag` は mode（push constant、draw の中で一様）で
7 つの分岐を持つが、i915 の compiler は分岐を if 変換して全ての分岐を全 pixel で実行する（texture の sample 1 pixel 8 回、image の draw でも）。

## 範囲（案）

1. 段 1: IR の texture の sample（SAMPLE・LD 等の send）に、その block の predicate を持たせ、EU では predicate に立つ channel が 1 つも無い
   thread は send を飛ぶ（flag の any で jmpi）。block の中の値は predicate の立たない channel では使われない（SELECT・phi・store は predicate で
   選ぶ）ので、飛んだ send の結果の register の中身は観測されない。
2. 段 2（要れば）: 算術の多い block も同じく飛ぶ（block 単位の jump。loop 変数・store の SELECT は飛ばさない所に置く）。
3. 受け入れ: host の lower・compile の試験（飛ぶ場合と飛ばない場合の結果の一致）、実機の vkx・vke1・vke2・zdesktop の capture、
   `apps8.sh` の 8 app の desktop の `rate`・`latency` と compositor の engine の占有（`engine_ns`）が変更の前より良くなる。

## 依存

なし（p008・p009 の後の tree）。p018 は p021 の後に計り直して要否を決める。

## 設計（2026-09-29、段 1 を実装）

- IR（`compiler/ir.h`）: `struct i915_shader_ir_inst` に `guard`（texture の命令の結果を使う channel の Boolean の値 + 1、0 は全 channel）。
  parser（`spirv.c` の `i915_spirv_guard()`）が SAMPLE・SAMPLE_BIAS・SAMPLE_LOD・TEXTURE を emit するとき、block の predicate が
  PREDICATE_ALWAYS でなければ付ける。predicate の外の channel はその結果を predicate による選択（store・phi・後の block の edge）でしか
  見ないので、message は predicate の channel にだけ要る。
- code generator（`compile.c` の `i915_compile_guarded_texture()`）: `cmp.nz f0.0 guard, 0` → `(+f0.0) if(8)` → message（payload・send・
  sync.nop）→ `endif(8)`。guard は source として liveness と spill の fill に数える（`i915_compile_sources()` の最後の source）。
  loop の中の ENDIF の JIP は loop の WHILE（Mesa の `brw_set_uip_jip()`）、外は次の命令。1 shader の guard は最大 256（超えた分は今までどおり）。
- encoder（`eu.c`）: `drv_i915_eu_if()`・`drv_i915_eu_endif()`・`drv_i915_eu_patch_if()`・`drv_i915_eu_patch_endif()`（Mesa の brw_IF・
  brw_ENDIF・patch_IF_ELSE の Gen12 の形）。`intel/eu-encoding-gen12.h` に IF=34・ENDIF=37 と UIP の bit（95:64）。
- 算術（全ての mode の ALU）は今までどおり全 pixel で走る（段 2 は要否を測ってから）。

効果の見込み: compositor の `panel.frag` は 8 つの sampler の send が全て mode の分岐の中にあり、mode は draw の中で一様。1 つの draw では
1 つ（blur は 5）の send だけが走り、残りは IF で飛ぶ。今の encoder は send ごとに結果を待つ（`eu.c` の scoreboard: send の後の sync.nop）ので、
send の数がそのまま thread の待ちの長さになる。

## 検証（実施中）

- host: `plan/ws075/tests/guard/run.sh`（新規）: panel.frag・quad.frag・browser の display.frag・switch.frag・loop.frag（loop の中の guard、新規）を
  compile、Mesa の brw_disasm で読み brw_asm で戻して同じ byte、IF と ENDIF の対、分岐の中の send が全て IF の中、loop の中の ENDIF の JIP が
  WHILE。PASS。IF・ENDIF の encoding は Mesa の assembler と bit まで一致（試しの program）。
- host: `run-vk-host-tests.sh` の spirv・lower・eu・compile・pipe・resdispatch PASS、`run-vk-gentool-test.sh`（BRW_TOOLS=Mesa 25.0 の build）PASS。
