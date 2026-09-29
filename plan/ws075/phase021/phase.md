<!-- awesome-plan project=zedbsd record=ws075p021 -->

# ws075-p021: 性能: compiler が どの channel も走らない block を飛ぶ（提案）

Phase ID: `ws075-p021`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-29。分岐の中の texture の send を IF で飛ぶ。10 app の desktop で compositor の 1 run の engine の時間 14.8 → 8.5 ms、flip の率 5.5 → 8.1/s、入力から flip まで中央値 132 → 32 ms。vkx・vke1・vke2・vkc PASS、画面は変更の前と画素で一致）
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

## 検証

- host: `plan/ws075/tests/guard/run.sh`（新規）: panel.frag・quad.frag・browser の display.frag・switch.frag・loop.frag（loop の中の guard、新規）を
  compile、Mesa の brw_disasm で読み brw_asm で戻して同じ byte、IF と ENDIF の対、分岐の中の send が全て IF の中、loop の中の ENDIF の JIP が
  WHILE。PASS。IF・ENDIF の encoding は Mesa の assembler と bit まで一致（試しの program）。
- host: `run-vk-host-tests.sh` の spirv・lower・eu・compile・pipe・resdispatch PASS、`run-vk-gentool-test.sh`（BRW_TOOLS=Mesa 25.0 の build）PASS。
- 規約: `plan/tools/style-check.py` で変更の前後を比べ、足した行の指摘（条件の中の呼出し 2・段落の comment 2）を直した。
- QEMU の boot test（GPU なし）: PASS（`build/ws075-p021/boot-test/login.png`）。
- host: shader の survey（`plan/ws075/tests/shader-survey/run.sh`、99 module）: compiler の拒否は前からの 2 つ（samplerBuffer の textureSize・
  sampler2DMS の fetch、p007 の範囲）だけ。

### 実機（5330 の VFIO passthrough の QEMU、demo の passthrough の image、kei の session）

`plan/ws075/tests/hdmi/measure-apps.sh IMAGE VMUNIX OUTDIR`（新規: lock の下で desktop だけの物差し → App Home の 10 app を開く
（`apps8.sh`、main が Settings・Image Viewer を足した後の App Home）→ 同じ物差し → context ごとの engine の時間（`engine-gdb.sh`）→ 撮影 → 返す）。
比較の image は同じ tree の p021 の前（`build/ws075-p021/noguard.img`: compiler の file だけ merge の時点 4e17c34f に戻して build）と後（`guard.img`）。

| 物差し | p021 の前（m-noguard） | p021 の後（m-guard2） |
| --- | --- | --- |
| desktop だけ `rate A 10` | 55.2/s | 56.2/s |
| desktop だけ `latency A 10` の中央値 | 16.1 ms | 15.8 ms |
| 10 app（Gears が回る）`rate A 10` | 5.5/s（engine の測りの間 5.4/s） | **8.1/s**（8.2/s） |
| 10 app `latency A 10` の中央値（範囲） | 131.8 ms（48〜200） | **31.5 ms**（7.4〜99） |
| 入力なしの 3 秒の flip（Gears の frame） | 17 | 24 |
| compositor の engine の占有・1 run | 56.4%・**14.8 ms**（38 run/s） | 49.1%・**8.5 ms**（58 run/s） |
| engine 全体の占有 | 64.0% | 57.0% |

- 画面: 10 app の各段の撮影を前後で画素で比べ、上の bar（時計）を除いて一致（Notes の title の file 名の時刻だけ違う）。
  `build/ws075-p021/m-noguard/shots/`・`m-guard2/shots/`、並べた図 `build/ws075-p021/m-*-sheet.png`。
- 実行器の回帰（i915 の試験の build、`test-hw.sh`）: vkx 9/9・vke1 6/6・vke2 17/17・vkc 9/9 PASS（試験の場面の verdict の行）。
- 1 回目の p021 の後の測り（m-guard）は Gears の窓が出なかった（App Home の Gears を押した後、窓なし）。2 回目（m-guard2）と手の run（hw-guard）では出た。
  compiler の拒否は無い（survey）ので、[BUG-058](../../bugs/BUG-058.md) の形（App Home の zgears が最初の frame の前に終わる、間欠）と見る（推測）。
  main に BUG-058 への追記を依頼する。

### 変えた file

- `src/drivers/gpu/i915/compiler/ir.h`・`spirv.c`・`compile.c`・`eu.c`・`eu.h`、`src/drivers/gpu/i915/intel/eu-encoding-gen12.h`。
- `plan/ws075/tests/guard/`（`shader-dump.c`・`run.sh`・`loop.frag` と spv）、`plan/ws075/tests/hdmi/`（`engine-gdb.sh`・`measure-apps.sh`・
  `apps8.sh` の 10 app の配置）。

### 残り・次

- 段 2（分岐の中の ALU も飛ぶ）は未実施。compositor はまだ engine の約 50%（1 run 8.5 ms）。今の encoder は send ごとに結果を待つ
  （scoreboard の直列）ので、その緩和も候補（どちらも新しい Phase）。
- p018（非同期の実行器）の要否: 10 app で engine は 57% 塞がり、CPU は約 60% が idle（gdb の 30 標本、halted 72/120）。compositor の frame は
  自分の GPU の時間でほぼ決まる。非同期化の効果は小さい見込みのまま。
- 素の 5330: 未実施。
