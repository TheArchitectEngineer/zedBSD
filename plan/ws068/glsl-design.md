<!-- awesome-plan project=zedbsd record=ws068-glsl-design -->

# WS068 設計: 自前の GLSL compiler（ws068-p015、2026-09-27）

状態: エージェント作成（2026-09-27 のユーザーの判断「方式 A」と「サブエージェントで実装を進めて」による）。
上位の設計: [design.md](design.md) §4（方式 A）・§6（desktop GL）。SPIR-V の約束は [phase008](phase008/phase.md) の「SPIR-V の約束」。

## 1. 目的と範囲

- GLSL の source を、libGLESv2 の変換層（p008）がそのまま受ける SPIR-V にする C の compiler。外部の code（glslang・Mesa）は使わない。
- 最初（p016〜p019）: GLSL ES 1.00 と desktop の GLSL 1.10・1.20・1.30。次（p012）: GLSL 1.40・1.50・3.30 と GLSL ES 3.00
  （`in`/`out` の layout、interface block と UBO、整数の varying、複数の fragment 出力）。その後 4.x（p014）。
- 出力の SPIR-V は **i915 のネイティブ compiler（`src/drivers/gpu/i915/compiler/spirv.c`）の受ける形**を基本にする（§5）。
  i915 の受けない命令（逆三角関数、微分、`OpImageSampleExplicitLod` 等）が要る shader は、Venus で正しく動く SPIR-V を出し、
  i915 で拒まれることを記録する（F-023）。
- 範囲外（後）: desktop の compatibility の built-in の状態（`gl_Vertex`・`gl_ModelViewMatrix`・`gl_FrontColor`・`ftransform` 等。
  desktop GL の p013 で libGL の固定機能の状態と一緒に）、sampler の配列、`gl_DepthRange`、geometry 以降の stage。

## 2. 構成（source）

`userland/base/libglesv2/glsl/`（libGLESv2 と libGL の両方が link する。GL の header に依存しない）:

| file | 中身 |
| --- | --- |
| `glsl.h` | 公開の interface（compile・link・解放・型の記述）。libGLESv2 と host の試験が使う |
| `internal.h` | 内部の型: arena、token、型、AST、symbol、関数、定数、診断 |
| `arena.c` | arena（compile 1 回分の memory。失敗は `longjmp` で compile の入口へ戻る）、文字列、info log |
| `lex.c` | 字句（前処理 token: 識別子・数・区切り・改行） |
| `preprocess.c` | 前処理（`#version`・`#define`（関数形式・`##`）・`#undef`・`#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif`・`defined`・`#error`・`#pragma`・`#extension`・`#line`、`__LINE__`・`__VERSION__`・`GL_ES`） |
| `parse.c` | 構文（再帰下降、AST を作る）。struct の型名は parser の scope で知る |
| `types.c` | 型（scalar・vector・matrix・sampler は静的な表、struct と配列は arena）、型の名前、std140 の配置 |
| `builtins.c` | built-in の関数（署名の表）・変数・定数 |
| `check.c` | 意味解析: scope と symbol、型検査、暗黙の変換、overload の解決、lvalue、使われた global の印 |
| `fold.c` | 定数式の評価（配列の長さ、`const`、case の値、定数の畳み込み） |
| `spirv.c` | SPIR-V の module を組む道具（節ごとの word 列、型と定数の重複の除去、id の払い出し） |
| `emit.c` | AST → SPIR-V（式・文・制御・関数の inline 展開） |
| `emit-builtin.c` | built-in 関数の SPIR-V（GLSL.std.450・texture・関係演算） |
| `link.c` | 2 つの stage の interface の照合、uniform block の配置、attribute・varying の location、各 stage の emit |

名前の接頭辞は `glsl_`（公開）と、file ごとの static。新しい C はすべて coding-style の全文（style-check 0）。

## 3. 流れ

```
glShaderSource ─► glCompileShader: glsl_compile(stage, source, 既定の版)
                    前処理 → 字句 → 構文 → 意味解析 → （成功）struct glsl_shader を shader に保つ / （失敗）info log
glLinkProgram  ─► glsl_link(vertex, fragment, glBindAttribLocation の組)
                    interface の照合 → uniform・sampler・attribute・varying の配置 → 各 stage を emit → SPIR-V 2 つ
                  ─► 既存の link（p008: 反射、gl_Position の書き換え、uniform の併合、VkShaderModule と layout）
```

- compile で型の誤りまで見つけ、`GL_COMPILE_STATUS` と info log（`0:行: error: …`）で返す。SPIR-V は link で作る
  （uniform block の offset を 2 つの stage で揃えるため。GL の実装の普通の形）。
- link の結果の SPIR-V は p008 の約束の形なので、既存の `program_link` の後段（反射・書き換え・併合・layout）をそのまま通る。
  `glShaderBinary`（SPIR-V）の道は変えない。GLSL と SPIR-V の stage の混在は link の誤り。
- 既定の版: `#version` が無い shader は、libGLESv2 では 100（ES）、libGL（desktop）では 110。

## 4. データ構造

- **arena**: compile ごとに 1 つ。AST・型・symbol・定数はすべて arena に置き、shader の削除で一度に解放する。確保の失敗は
  `longjmp` で `glsl_compile`／`glsl_link` の入口へ戻り、「out of memory」の log で失敗する（呼び出しごとの NULL の検査を無くし、
  式の組み立てを読みやすく保つ）。
- **token**: 種類（識別子・整数・符号無し整数・浮動小数・区切り・改行・EOF）、区切りの種類、綴り、行、前の空白。
  keyword は parser が識別子の綴りから版に応じて分ける（予約語は誤り）。
- **型** `struct glsl_type`: kind（void・bool・int・uint・float・vector・matrix・sampler・struct・array）、基本の scalar、
  成分数、列数、sampler の次元・shadow・配列・結果の型、struct の名前と member、配列の要素と長さ。scalar・vector・matrix・
  sampler は静的な const の表（thread の間で共有して書かない）。
- **AST** `struct glsl_node`: kind、行、演算子、子（最大 4）と `next`（並び）、名前、型（検査の後）、symbol、定数値、
  lvalue・副作用の印。
- **symbol** `struct glsl_symbol`: 名前、種類（変数・関数・型）、型、記憶（local・global・const・uniform・attribute/in・
  varying/out・引数 in/out/inout・built-in）、補間（flat・noperspective・centroid）、`invariant`、定数値、使われたか、emit 中の id。
- **関数** `struct glsl_function`: 名前、戻りの型、引数、本体、定義済みか、built-in の記述、overload の次、再帰の検出の印。
- **定数** `struct glsl_constant`: 型と、平らにした scalar の値（float・int・uint・bool を 32 bit で）。

## 5. SPIR-V の出し方（i915 の受ける形）

i915 の compiler（`spirv.c` の冒頭の説明と opcode の表）から決めた制約と、その出し方:

| i915 の制約 | 出し方 |
| --- | --- |
| `OpFunctionCall`・`OpReturnValue` が無い | 利用者の関数はすべて `main` に inline 展開する（GLSL は再帰を許さない）。引数は Function の変数に copy-in、`out`/`inout` は戻りで copy-out。途中の `return` がある関数は 1 回だけ回る loop で包み、`return` は戻り値を変数に書いて loop を抜ける（入れ子の loop からは「戻った」印で順に抜ける） |
| Private の global が無い | global の変数（uniform・in・out 以外）は `main` の Function の変数にし、`main` の最初で初期化する |
| struct・配列の local は拒む | GLSL のとおりに出す（Venus で動く）。i915 で拒まれる shader として記録 |
| 一度も store していない成分の load を拒む | 初期化の無い local・`out` 引数・戻り値の変数は宣言の所で 0 を store する |
| `OpSwitch` が無い | `switch`（1.30）は 1 回だけ回る loop と、「落ちてきた」印の if の並びにする。`break` は loop を抜ける |
| `OpCompositeInsert` が無い | 成分への代入は成分への access chain と store、swizzle の代入は load・`OpVectorShuffle`・store |
| `OpPhi` は使わない | `&&`・`||`・`?:` は右辺（両辺）に副作用が無く安い時は `OpLogicalAnd`/`OpLogicalOr`/`OpSelect`、そうでなければ変数と分岐 |
| `OpAny`・`OpAll` が無い | `any`・`all`・vector の `==` は成分を取り出して `OpLogicalOr`/`OpLogicalAnd` の鎖 |
| `OpImageSampleProj*` が無い | `texture2DProj` 等は座標を q で割ってから `OpImageSampleImplicitLod` |
| 出力は location 付きと Position だけ | `gl_Position` は Block でない単独の Output 変数（BuiltIn Position）。`gl_PointSize` は shader が書く時だけ出す |
| Flat を拒む | `flat` と書かれた時だけ出す（1.30 の整数の varying は flat が要る） |
| struct の member ≤ 16 | uniform block の member はその stage で使う uniform だけにする（16 を越える shader は i915 で拒まれる。記録） |
| built-in の入力（`gl_FragCoord` 等）を持たない | GLSL のとおり出す（i915 で拒まれる。記録） |

その他の約束（p008 の変換層と Vulkan のため）:

- module: SPIR-V 1.0、`Shader` capability（sampler の種類に応じて `Sampled1D`・`ImageQuery`）、`GLSL.std.450`、Logical GLSL450、
  fragment は `OriginUpperLeft`（`gl_FragDepth` を書く時は `DepthReplacing`）。
- default の uniform block: set 0 binding 0 の Block の struct `gl_DefaultUniformBlock`（変数名も同じ）、std140、member 名 = uniform 名
  （OpMemberName）。uniform の struct は入れ子の struct、配列は ArrayStride 付きの型（local の型とは別の id。Vulkan の
  「Function の変数に明示の layout の型を使わない」規則のため）。block から struct・配列を丸ごと読むときは葉ごとに load して
  local の型で組み直す。bool の uniform は block では uint で、読むときに `!= 0`。
- sampler: set 0 の UniformConstant、binding は 2 つの stage を併せた sampler の順に 1 から。
- attribute: `glBindAttribLocation` の値、残りは宣言の順に空いている location。varying: vertex の宣言の順に location、fragment は
  同じ名前の vertex の出力の location。名前は OpName（p008 の反射が名前で照合するため）。
- uniform・attribute は **使われたものだけ**（`main` から届く code が参照するもの）を出す（GL の active の意味）。
- `gl_Position` の y と z の書き換えは p008 の link（`gles_spirv_position`）が行う。compiler は GL の clip 座標のまま書く。
- 定数: `const` の変数と定数式は畳み込んで `OpConstant`・`OpConstantComposite` にする。
- 制御: if は `OpSelectionMerge`、loop は header・条件・本体・continue・merge の構造化した形（`OpLoopMerge`）。`discard` は `OpKill`、
  `main` の `return` は `OpReturn`。終わった block の後の文は出さない。

## 6. 型検査の規則（主なもの）

- 版ごとの keyword と予約語、ES の `precision`（ES の fragment shader は float の既定の精度が要る）、`gl_` で始まる名前の禁止。
- 暗黙の変換は desktop 1.20 以上だけ（int → float、1.30 は uint → float も）。ES には無い。
- 演算子: 算術は同じ基本型、scalar と vector、vector と vector（同じ大きさ）、matrix の積（線形代数の積）、matrix と scalar。
  `%`・bit 演算・shift は 1.30 から（ES 1.00 は予約）。比較 `<` 等は scalar だけ、`==` は配列（1.20 から）・struct も。
- lvalue: 変数・成分・swizzle（重複の無いもの）・配列の要素・struct の member。uniform・attribute・fragment の varying・const・
  `in` 引数は書けない（`in` 引数は関数の中では書ける）。
- built-in の関数の overload は署名の表で、genType（float と vec2〜4）、genIType、genUType、genBType と固定の型で解く。
- 定数式: 配列の長さと `const` の初期値は定数式（built-in の関数の一部も畳む）。

## 7. libGLESv2 への接続

- `struct gles_shader` に compile の結果（`struct glsl_shader *`）を持たせる。`glShaderSource` は source を保ち、`glCompileShader` が
  `glsl_compile` を呼ぶ。`GL_COMPILE_STATUS`・`GL_INFO_LOG_LENGTH`・`glGetShaderInfoLog` は結果と log。`glShaderBinary` は GLSL の
  結果を捨てる（逆も同じ）。
- `glLinkProgram`: 両方が GLSL なら `glsl_link` で SPIR-V を作り、既存の link の後段へ渡す。反射の後に uniform の GL の型
  （bool・sampler の種類）を compiler の記述で直す。
- `glGetShaderPrecisionFormat`・`glReleaseShaderCompiler` は今のまま。`GL_SHADER_COMPILER` は `GL_TRUE`。
- libGL（desktop）の既定の版は 110。`GL_SHADING_LANGUAGE_VERSION` と `GL_VERSION` を上げるのは desktop GL の Phase（p013）で、
  必須の機能が揃った所で（正直に名乗る）。

## 8. 試験

| 試験 | 中身 | 場所 |
| --- | --- | --- |
| 前処理と構文 | 正しい shader と誤りの shader の組（誤りは log の行と文言） | `plan/ws068/tests/glsl-host/` |
| 型検査 | 正と負の例（暗黙の変換、overload、lvalue、版の差、精度） | 同上 |
| SPIR-V | shader の組を compile・link し、p008 の書き換えを通した SPIR-V を `spirv-val --target-env vulkan1.0`、`spirv-dis` で要所を確認 | 同上 |
| 実行 | host の Vulkan（lavapipe）で全画面の四角を描き、fragment の色を読んで期待の値と比べる（算術・built-in・制御・inline・uniform の配置） | 同上 |
| i915 | i915 の compiler（host）で代表の shader が受けられること（`plan/ws068/tests/i915-shader-check/` の道具を使う） | 同上 |
| Venus | egltest の `--scene=glsl`（GLSL の source で p008 の場面を描き glReadPixels と画面で確かめる） | `plan/ws068/tests/egl-p019.sh` |
| 回帰 | `egl-p008.sh`、`plan/ws069/tests/x11-p005.sh`、`spirv-host/run.sh`、boot test | 既存 |

## 9. Phase の分け方（p003 を分ける）

| Phase | 内容 | 受け入れの要点 |
| --- | --- | --- |
| p015 | この設計 | この文書と Phase の表 |
| p016 | 前処理・字句・構文（AST） | host の試験で正の shader が通り、誤りが行つきで報告される |
| p017 | 型・意味解析・定数・built-in の宣言 | host の試験の正と負の例 |
| p018 | SPIR-V の出力と link | spirv-val、lavapipe での実行の試験、i915 の host の検査 |
| p019 | libGLESv2 への接続、egltest の GLSL の場面、Venus の試験と回帰 | egl-p019 PASS、回帰 PASS |

p012（GLSL 3.30・ES 3.00）は p019 の後に同じ核へ足す（interface block・UBO（set 0 の binding を uniform block ごとに）・layout の
location・整数の varying の flat・複数の出力・`texture` の新しい形の残り）。

## 10. 既知の制限（p003 の範囲で残すもの）

- sampler の配列、`gl_DepthRange`、compatibility の built-in の状態、`#include`、`continue` を含む `switch`（検査で誤り）。
- `gl_FragCoord` の y は Vulkan の向き（上から）。GL の向き（下から）には framebuffer の高さの隠れた uniform が要る（p019 で判断、
  または p004）。
- i915: §5 の表の「記録」の行。
