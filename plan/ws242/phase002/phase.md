<!-- awesome-plan project=zedbsd record=ws242-p002 -->

# ws242-p002: shader の compiler を SM86（RTX 30）・SM89（RTX 40）にも

Status: in-progress（2026-10-16 P4: 実装・host 試験・build まで。実機（RTX 30/40）の確かめはユーザーの指示で無し。driver が SM を渡す口は P2（ws242-p001））
Disposition: normal
Parent: [WS242](../ws.md)
Queue: Q1 の割当（2026-10-16、P4）。ユーザー「nvrtxドライバは、RTX 20/30/40/50までのSMで対応できるようにしたいです。20/30/40はSPIR-V loweringのみで対応できそうですか？」「20/30/40は、30/40の動作確認なしで、とりあえず対応しておいてください。」「20/30/40対応も独立したWSにして、今から作業します。」
依存: なし（host で作って確かめる）。driver の呼び手が chip の SM を渡すのは ws242-p001（P2）。
実行者: P4（phase-runner）
元の記録: compiler の全体は [ws037-p008](../../ws037/phase008/phase.md)（SM75）。

## 1. 範囲と受け入れ

- compiler（`src/drivers/gpu/nvrtx/compiler/`）が SM75・SM86・SM89 の code を出せる。SM75 の出力は byte 単位で今と同じ（RTX 2070 で desktop が描けている code を変えない）。
- host 試験: desktop の全 shader と compiler の試験の module を 3 つの SM で compile し、interpreter（SM ごとの依存と memory order の検査）と IR の参照で比べる。encode の欄の試験も SM ごと。
- SM120（RTX 50）は WS240 で後。SM80（GA100、RTX ではない）・SM90 は拒む。

## 2. SM75・SM86・SM89 の差（Mesa 25.3.6 の NAK、MIT。P1 の `plan/ws037/temp/mesa/`）

出典（sha256）: `nak/sm70_encode.rs`（111b841c…6695）、`nak/sm70.rs`（50486955…de27）、`nak/sm75_instr_latencies.rs`（419b8b1d…4878）、`nak/sm80_instr_latencies.rs`（© 2025 Red Hat、3aeeb5b8…7049797）、`nak/ir.rs`（bb34c049…b09e: is_turing 73..79、is_ampere 80..88、is_ada 89）、`nak/calc_instr_deps.rs`。open-gpu-doc には命令の資料が無い（QMD の class の header だけ、driver の側）。

NAK の encoder は SM70 以降で 1 本（SM の番号の比較で分岐）、latency の表は SM75・SM80（Ampere と Ada で共有）・SM120 の 3 本。

| 項目 | SM75（Turing） | SM86（Ampere）・SM89（Ada） | compiler の扱い |
| --- | --- | --- | --- |
| 命令の形（128 bit、opcode・dst・src・form・述語の欄） | 同じ | 同じ | 変えない |
| compiler が使う命令の opcode・修飾（MOV・SEL・FMNMX・FSETP・ISETP・IADD3・LOP3・SHF・FMUL・FADD・FFMA・IMAD・IMAD.WIDE・F2I・I2F・FRND・MUFU・S2R・LDC・LDG・STG・LDS・STS・ATOMG・ATOMS・MEMBAR・BAR・BSSY・BSYNC・BRA・EXIT・NOP・ALD・AST・IPA・TEX・TLD・KILL） | — | sm70_encode.rs の分岐は `sm >= 90`・`>= 100`・`>= 120` だけ（uniform の R2UR の opcode、texture の bindless、atom の float の型など）。SM80 で変わるのは下の memory order だけ | 変えない |
| global の memory order（LDG・STG・ATOMG・ATOMG.CAS） | scope 77..78（GPU = 2）、order 79..80（strong = 2） | 77..80 の 1 つの欄（strong・GPU = 7、weak 0、constant 4、strong CTA 5、strong system 0xa） | **SM ごとに encode**（`nvrtx_sm75_memory_order()`） |
| `suld.constant`（image の load） | 無い（weak に落とす） | ある | 使っていない |
| 新しい命令（REDUX、fp16 の min/max、uniform 述語の分岐 BRA.U、SM86 の F2FP の pack） | 無い | ある | 使っていない（無くても足りる） |
| 制御の bit（delay 105..108、yield 109、scoreboard 110..121、reuse 122..125） | 同じ | 同じ（変わるのは SM120） | 変えない |
| register（汎用 253 + RZ、hw の予約 2、述語 7、PT） | 同じ | 同じ | 変えない |
| uniform の datapath（UGPR 63、UPred 7、SM73+） | ある | ある（SM100 で 80） | 使っていない |
| scoreboard の数・待ちの 2 cycle の規則（calc_instr_deps.rs） | 6・2 | 6・2 | 変えない |
| 固定 latency の書き → 可変 latency の読み（RAW） | 6 | Decoupled 4、DecoupledAgu（LDG・STG・LDS・STS・atomic・ALD・AST・IPA・BAR）5 | **SM ごと** |
| IMAD.WIDE の書き → 次の IMAD.WIDE の加数の読み | 2（IMADWideLower） | 4（IMADWideReadCL は CoupledFMA と同じ行） | **SM ごと** |
| その他の固定 → 固定の RAW（ALU→ALU 4、→FMA・IMAD 5、FMA→FMA 4、IMAD.WIDE の因数 ← IMAD.WIDE 6 など） | 表のとおり | 同じ値 | 変えない |
| 比較の述語 → guard・分岐・可変の読み | 12 | 13 | **SM ごと** |
| 比較の述語 → ALU 4、FMA・IMAD 5 | 同じ | 同じ | 変えない |
| WAW（固定の後の固定 1〜2、固定の後の可変 6）、WAR（固定の読みの後の可変の書き 2）、述語の WAW・WAR | 表の最大 2・6・2 | 固定の後の可変は 5（述語付き 6）、他は同じか小さい | compiler は両方とも大きい方（2・6・2）で、変えない |
| 結果の最悪（block の終わりで待つ: 汎用 6、述語 13） | 6・13 | 6・13 | 変えない |
| BAR・MEMBAR の実行 latency | 5 | 6 | compiler の最小 delay は前から 6 で、変えない |
| SPH（version 4、128 byte） | SM73 以上で同じ | 同じ | 変えない |
| QMD | V02_02 | V03_00 | driver の側（ws242-p001） |
| SM あたりの thread（register の方針の目安） | 1024 | 1536 | register の pool（62 + 2）は SM75 のまま。速さの目安だけで、正しさには効かない |

## 3. 実装（2026-10-16 P4）

| file | 変更 |
| --- | --- |
| `sm75-isa.h` | `NVRTX_SM_TURING`（75）・`NVRTX_SM_AMPERE`（86）・`NVRTX_SM_ADA`（89）・`NVRTX_SM_AMPERE_FIRST`（80）、SM80 の memory order の欄（`NVRTX_SM80_MEM_ORDER_LOW` 77、4 bit、strong GPU 7）。出典の表に sm70.rs と ir.rs の sha256 |
| `sm75-encode.[ch]` | `nvrtx_sm75_memory_order(word, sm)`: global の access の欄を SM の形で書く（SM75 は今までと同じ値） |
| `sm75-schedule.[ch]` | `nvrtx_sm75_reorder()`・`nvrtx_sm75_schedule()` に `sm`。read の分類 `NVRTX_SM75_CLASS_AGU`（address unit の読み、SM75 では 6 で今までと同じ）。RAW を `nvrtx_sm75_raw_turing()`・`nvrtx_sm75_raw_ampere()` に分け、述語の RAW も SM ごと。Red Hat の MIT の notice と sm80_instr_latencies.rs の出典を header に |
| `sm75-compile.[ch]` | `nvrtx_sm75_compile_stage_sm(words, count, stage, sm, …)`・`nvrtx_sm75_compile_ir_sm(ir, sm, …)`。75・86・89 以外は ENOTSUP と理由。結果に `shader_model`。emit の時に global の access に SM の memory order。今の関数（sm 無し）は SM75 のまま |

driver との口（Q1 経由で P2 に渡した、2026-10-16）: driver の呼び手 4 か所（`vk-pipeline.c`・`draw.c`・`compute.c`・`vk-execute.c`）が chip の表の SM（TU10x・TU11x → 75、GA10x → 86、AD10x → 89）で `_sm` の関数を呼ぶ。呼び手を変えるまでは今と同じ SM75 の code。

### 3.1 出典の block ごとの注記（2026-10-16、Q1 経由のユーザーの指示「その2つのファイルは、まずブロックごとに引用元を示してください。」「レイテンシの表は、データなので、引用で済むように思います。引用元を明示すればいいと思います。」）

- `sm75-isa.h`・`sm75-schedule.c` の Mesa の値を持つ block の上に、出典（Mesa 25.3.6 の path、関数・表の名前、行の範囲）を 1 つずつ書いた。zedBSD の物（cbuf の割り当て、追跡の数、並べ替えの見積り）は「zedBSD's own」。EXIT の delay 5 は Mesa ではなく CUDA の出す語から。
- `sm75-isa.h` は #define の値だけなので、Collabora の MIT の notice を外し、header を Zlib（SPDX）に、出典の一覧（sha256）は残した。`sm75-schedule.c` の notice は、表の入れ子の形（reader → writer の if の並び）と scoreboard の選び方が NAK に沿うので、Q1 の判断まで残した。
- 動作は不変: SM75 の 55 module の出力は byte 単位で同じ、host 試験 287 checks 0 failures、vmunix は warning 0。

## 4. 確かめ（host と build、2026-10-16 P4）

| コマンド | 結果 |
| --- | --- |
| 変更前後の SM75 の出力の比較（使い捨ての dump、desktop 44 と試験の 11 の module、code・register 数・SPH・binding） | 55 module 全て byte 単位で同じ（sm 無しの関数と `_sm(75)` の両方） |
| `sh plan/ws037/tests/nvrtx-sm75-host-test.sh` | `287 checks, 0 failures`（compile して走らせる部分を 75・86・89 で回す。新しい check: LDG・STG・ATOMG の memory order の欄を SM ごと、SM89 の add.comp の global の access が全て 7、SM80・SM120 の拒否） |
| `sh plan/ws037/tests/nvrtx-sm75-desktop-test.sh --strict` | `138 compiled, 0 refused, 0 disagreed with the IR`（46 module × 3 SM、各 200 入力。SM75 は sm 無しの関数の出力と同じことも確かめる） |
| `make -j16 ZEDBSD_CONFIG=plan/ws037/tests/config-amd64-nvrtx.mk BUILD=build/ws242-p002 build/ws242-p002/vmunix` | rc 0、`warning:` 0、`kernel include check: PASS`、`amd64 vmunix check: PASS` |
| `git diff --check` | 出力なし |

- interpreter（`plan/ws037/tests/sm75-machine.c`）は program の `shader_model` で表を選ぶ（SM80 の表は NAK から試験の側で別に書いた）。global の access の memory order の欄も SM ごとに調べる。
- 感度（schedule.c の Ampere の値を Turing の値に変えて流し、戻した）: 述語 13→12 は host 試験が 12 件、可変の読み 4→3 は host 14 件・desktop 42 module、AGU の読み 5→4 は host 22 件・desktop 19 module で検出。IMAD.WIDE→加数 4→2 は検出されない（その組を作る program が無い。今の lowering の加数は LDC の結果か RZ）。
- SM86 と SM89 の出力は同じ（表が同じ）。SM75 との差は制御の bit・global の memory order と、latency の違いによる block の中の並びの違い。

### 4.1 既存の試験の穴（直した）

desktop の試験を 3 つの SM で回すと乱数の入力の並びが変わり、libGL の `fixed_vert_8` が 1 つの入力で IR と食い違った（SM75 でも同じ入力で再現、compiler の変更とは関係ない）。調べ: 光源 4 の `normalize(direction + vec3(0,0,1))` で direction がほぼ (0,0,-1)。IR の参照は正確な除算で和がちょうど 0 になり normalize が NaN、SM の interpreter は MUFU.RCP の近似で和が 6e-8 程度残り有限の向き。悪条件の入力（GLSL で未定義）。→ 参照の interpreter（`sm75-reference.c`）で GLSL の未定義（inversesqrt・log2 の x <= 0、sqrt の x < 0、float の 0 除算）に当たった入力を「未定義」として数えない（整数の 0 除算と同じ扱い）。libGL の 7 module の有効な入力が 200 から約 130〜145 に減り、他の module は 200 のまま。

## 5. 未実施・残り

- 実機（RTX 30・40）: ユーザーの指示で確かめない。RTX 2070（SM75）は出力が同じなので今までの実機の結果のまま。
- driver が SM を渡すこと（P2、ws242-p001）。それまでは RTX 30/40 でも SM75 の code が出る（memory order の欄と latency が違うので、そのままでは正しくない）。
- IMAD.WIDE→加数の SM86 の値を試験で踏む program が無い。
- 並べ替えの見積り（可変 latency の推定）は SM で変えていない（速さだけに効く）。
- register の方針（64 で SM75 の 1024 thread）は SM86・89（1536 thread）でも同じ。
