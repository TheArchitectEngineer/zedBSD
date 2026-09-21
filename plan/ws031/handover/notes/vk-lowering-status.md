# SPIR-V lowering の項目別状態（vkdemo 固定 shader 基準）

WS031 E-110（2026-09-19）。`vkdemo-dependency-table.md` §2 の続き。対象は **出荷版のまま**の `userland/base/vkdemo/shaders/cuboid.{vert,frag}.spv`（glslc `-O0`、SPIR-V 1.0）。shader の簡略化・`-O` での再生成・hash による既知 binary への置換はしていない。path は `agent-1:~/zedBSD/` 基点。

## 0. 状態の定義（三段階。上の段は下の段を含まない）
| 状態 | 意味 | 根拠になる試験 |
|---|---|---|
| 拒否 | parser か compiler が ENOTSUP（理由と opcode／word offset つき）で shader 全体を断る。読み飛ばしはしない | `i915-vk-lower-test.c` の refusals、`i915-vk-spirv-test.c` の body classification、`i915-vk-pipe-test.c`（VkResult −8、残留 object なし） |
| lowering済み | SPIR-V → scalar IR。IR を interpreter で実行した出力が、**独立に書いた式**の値と一致 | `plan/ws031/tests/i915-vk-lower-test.c` |
| EU生成済み | IR → Gen12 EU word。**生成された word を bit field から decode して 8 channel で実行する model** の出力が同じ独立式と一致。operand の位置・negate・scalar region・register 再利用の誤りはここで落ちる | `plan/ws031/tests/i915-vk-compile-test.c`（`test_vertex_shader_eu_computes_the_shader` ほか） |
| GPU検証済み | 実機で実行して結果を確認 | **該当なし（0 件）** |

**EU生成済みが保証しないこと**: payload の register 配置（push constant→入力の順、入力 1 location = 4 register）、出力の staging register（r112〜）、終端 SEND と sampler SEND の message descriptor、URB／render target への渡し方。これらは `compile.c` の header に「hardware 未検証の baseline」と明記した仮の規約で、E-101〜E-105 の実機描画は手組み kernel を使っており compiler 出力は一度も GPU で走っていない。VK-3 以降で prog_data 相当（E-103 で確定した PS DW3／DW7／PS_EXTRA、GRF start）と突き合わせる。

## 1. 設計の要点（なぜ複雑な pass が要らないか）
- IR を **scalar** にした: IR の値 1 個 = float 1 個。SIMD8 dispatch では「channel ごとの float 1 個」= GRF 1 本なので、hardware の register model とそのまま対応する。
- 成分を並べ替えるだけの命令（OpCompositeConstruct／OpCompositeExtract／OpVectorShuffle／成分 OpAccessChain／local の OpStore・OpLoad）は **IR を出さない**。parser が「どの scalar がどの成分か」の名前づけだけを行う。成分の欠落・重複が構造的に起きない。
- Function storage の local は memory にしない。受理する shader は basic block 1 個（分岐は拒否）なので、load は「その成分へ最後に store された値」を読む（store→load forwarding）。成分単位で管理し、**未 store の成分を load したら拒否**。
- compiler の register 割当ては「定義から最後の読み手まで」。直線 SSA なので後ろ向きの 1 走査で最後の読み手が決まる。足りなければ拒否（誤った spill はしない）。vkdemo VS は 50 IR 命令・44 値が r16〜r23 の 8 本に収まる。

## 2. 項目別（固定 shader の使用箇所・条件・状態・試験）
使用箇所は `spirv-dis` の実数（VS = cuboid.vert.spv、FS = cuboid.frag.spv）。

| # | 項目 | 固定 shader の使用箇所 | 受理する条件（型・storage class・operand） | 状態 | 小試験 |
|---|---|---|---|---|---|
| 1 | 型: void／bool／int／float／vector／image／sampler／sampled image／array／struct／pointer／function | VS: float 1、vector 3（vec2/3/4）、int 2、array 1（ClipDistance）、struct 2、pointer 10。FS: image／sampled image 各 1 | 値として扱うのは float32 と float32 の 2〜4 成分 vector だけ。それ以外の型は宣言は受理、値としての load／store／演算は拒否。matrix・runtime array・64 bit は宣言の時点で拒否（未解釈の module-level 命令） | lowering済み／EU生成済み | lower: 全試験の前提。refusals「size mismatch」 |
| 2 | float OpConstant を operand に | VS 9 個（0.3、0.43、0.4、0.7、3.0、1.2、−1.6、10/9.9、1/9.9） | 32 bit float。初回使用時に IR `CONST` を 1 個出す。EU は即値 MOV。OpConstantComposite／Spec 定数は拒否 | lowering済み／EU生成済み | lower: `test_dot_negate_scale`（70−5=65 と 5−70=−65 で operand 位置も確認）。compile: `test_register_lifetime` |
| 3 | int OpConstant（access chain の index） | VS 4 個 | 32 bit。index としてだけ使う | lowering済み | lower: `test_component_access` |
| 4 | Function storage の OpVariable／OpStore／OpLoad | VS: local 8 個（float 6、vec3 2）、OpStore 10 のうち 8、OpLoad 29 の大半 | float か float vector。initializer つき・struct／array の local は拒否。未 store 成分の load は拒否 | lowering済み（IR なし）／EU生成済み | lower: `test_local_store_load_overwrite`（5 を store→load、2 で上書き→load、(5, 2, 3, −3)）。refusals「unset local」「partial local」「initializer」 |
| 5 | 成分 OpAccessChain | VS 17 個（`position.x/y/z`、`rotated_x.x/y/z`、`view.x/y/z`、push constant の member 0、`gl_PerVertex` の member 0） | index は定数。struct member →（任意で）vector 成分の 2 段まで。動的 index・array・matrix・入れ子は拒否 | lowering済み／EU生成済み | lower: `test_component_access`（local の 1 成分だけ上書き、入力・出力を成分単位・順不同で）。refusals「dynamic index」 |
| 6 | OpCompositeConstruct（2〜4 成分） | VS 3 個（vec3×2、vec4×1） | 結果は float vector。構成要素は float scalar／vector の混在可、合計が成分数と一致しなければ EINVAL | lowering済み（IR なし） | lower: `test_vector_construct_extract_shuffle`（(1,2,4,8) を逆順に、vec2＋scalar＋scalar） |
| 7 | OpCompositeExtract | 固定 shader では 0 個（`-O0` は access chain を使う） | float vector から定数 index 1 段 | lowering済み（IR なし） | 同上（4 成分を個別に取り出し、どれも欠落・重複しない） |
| 8 | OpVectorShuffle | 0 個 | float vector 2 本、undef 成分（0xFFFFFFFF）は拒否 | lowering済み（IR なし） | 同上（(1,2,4,8) と (5,6,7,8) から成分 1,6,3,4 → (2,7,8,5)） |
| 9 | OpFAdd／OpFSub／OpFMul | VS: FAdd 6、FSub 2、FMul 13 | 同じ成分数の float scalar／vector。成分ごとに IR 1 個。FSub は EU では「ADD、第 2 source に negate」（E-109 の修正を保持） | lowering済み／EU生成済み | lower: 5−2=3 と 2−5=−3。compile: `test_fsub_is_add_with_second_source_negated`（operand 番号と negate bit） |
| 10 | OpFNegate | VS 1 個（`-sy`。`-1.6` は glslang が定数に畳む） | float scalar／vector。IR `FNEG`、EU は source negate つき MOV | lowering済み／EU生成済み | lower: dot=70 → −70、vector の negate。compile: `test_fneg_and_push_operands` |
| 11 | OpDot | 0 個 | 同じ成分数の float vector 2 本。FMUL n 個＋FADD n−1 個へ展開（旧実装は MUL 1 個に化けていた → E-109 で拒否 → 今回実装） | lowering済み／EU生成は FMUL・FADD と同じ経路 | lower: dot((1,2,3,4),(5,6,7,8)) = 70 |
| 12 | OpVectorTimesScalar | 0 個 | vector × scalar | lowering済み | lower: (1,2,3,4)×2 → negate → ×(5,6,7,8) = (−10,−24,−42,−64) |
| 13 | OpExtInst GLSL.std.450 Sin／Cos／InverseSqrt | VS: Sin 2、Cos 2 | float scalar／vector、成分ごと。ほかの拡張命令（Tan など）は拒否 | lowering済み／EU生成済み（MATH の function field を model が decode） | lower／compile: 出荷版 VS の全体一致 |
| 14 | 入力（Input、Location つき） | VS: location 0 = vec3、location 1 = vec2。FS: location 0 = vec2 | float／float vector。IR `LOAD_INPUT(location, component)`。**属性に無い成分は読まない**（試験で junk を置いて確認）。BuiltIn の入力（gl_FragCoord など）は拒否 | lowering済み／EU生成済み（payload 規約は未検証） | lower: `test_vkdemo_vertex_shader` |
| 15 | 出力（Output、Location つき／`gl_PerVertex.Position`） | VS: Position（OpMemberDecorate BuiltIn を解釈）、location 0 = vec2。FS: location 0 = vec4 | IR `STORE_OUTPUT(location または POSITION, component)`。Position 以外の builtin 出力（PointSize など）への store は拒否。各成分ちょうど 1 回の store を試験で確認 | lowering済み／EU生成済み（staging register と URB 渡しは未検証） | lower: VS で Position 4 成分＋location 0 の 2 成分が各 1 回 |
| 16 | push constant | VS: `animation.seconds`（member 0、Offset 0）、2 回 load | struct member、OpMemberDecorate Offset 必須。IR `LOAD_PUSH(byte offset)`。EU は `<0;1,0>` の scalar region（register = 2 + offset/32、subnr = offset%32）。旧実装の「先頭 member 以外は拒否」を解消 | lowering済み／EU生成済み（model で push register の残り 7 float に junk を置き、vector として読む誤りを検出） | compile: `test_fneg_and_push_operands`（2 番目の float、subnr 4） |
| 17 | sampled image（UniformConstant、Binding／DescriptorSet） | FS 1 個（set 0 binding 0） | `texture(sampler2D, vec2)` で image operand なしだけ。IR `SAMPLE`（結果 4 scalar、u・v の順、set／binding を保持） | lowering済み。**EU は形だけ**（u,v を連続 2 register へ置き SEND、応答 4 register。descriptor = 0 で binding table index／message type／SIMD mode 未設定） | lower: `test_vkdemo_fragment_shader`（u と v を取り違えると一致しない擬似 texture） |
| 18 | decoration | VS: Block 2、Location 3、Offset 1、BuiltIn 4。FS: Location 2、Binding 1、DescriptorSet 1 | §3 の分類表 | — | lower: refusals「Flat」、`test_harmless_decoration` |

**固定 shader 全体**: VS・FS とも通常の parser→compiler 経路で lowering済み＋EU生成済み。VS は 50 IR 命令 → 51 EU 命令（終端 SEND 込み）。lowering は 4 頂点 × 3 時刻、EU model は 8 頂点を 8 channel 同時に実行し、`gl_Position` 4 成分と `texture_coordinate` 2 成分が GLSL 原式（C で独立に記述、相対 2e-6 以内）と一致。

## 3. 無視してよいもの／意味を持つもの／拒否するもの（明示の分類表）
| 種類 | 扱い | 対象 |
|---|---|---|
| 意味を持つ → 解釈する | 値・配置に反映 | OpDecorate: Location、Binding、DescriptorSet、BuiltIn、Block。OpMemberDecorate: Offset、BuiltIn。OpEntryPoint の execution model |
| 実行意味を持たない → 名前を挙げて無視 | 読み飛ばしてよい理由つき | OpDecorate／OpMemberDecorate の RelaxedPrecision（精度を落としてよいという許可で、この lowering は使わない）。module-level: OpNop、OpSource、OpSourceContinued、OpSourceExtension、OpName、OpMemberName、OpString、OpLine、OpNoLine、OpModuleProcessed、OpCapability、OpExtension、OpMemoryModel、OpExecutionMode。body: OpNop、OpLine、OpNoLine、OpLabel、OpReturn |
| 上のどちらにも無い → 拒否（ENOTSUP、opcode と word offset を報告） | 一括許可しない | それ以外の decoration 全部（ArrayStride、MatrixStride、Component、Flat、NoPerspective、Centroid、Index、InputAttachmentIndex、SpecId …）。それ以外の module-level 命令（OpTypeMatrix、OpTypeRuntimeArray、OpConstantComposite、OpConstantTrue／False、OpSpecConstant*、OpUndef …）。それ以外の body 命令全部（OpFDiv、OpSelect、比較、変換、分岐・merge・phi、OpFunctionCall、2 個目の function、OpKill …） |

注: OpCapability／OpExecutionMode を「無視」に置いているのは現状の固定 shader（Shader、OriginUpperLeft）に対して意味が変わらないためで、値の検査はしていない（DepthReplacing などが来ても拒否しない）。ここは次の増分で「値が既知の集合に入ること」を検査する候補。

## 4. 同時に直した「誤って実装されていたもの」
- `i915_vk_eu_mad` は operand を一切 encode していなかった（opcode と dst だけの MAD）。IR の FMAD を出す parser 経路は無かったが、呼べば誤った命令になる。→ IR から FMAD を削除、encoder は呼ばれたら buffer を error にする（誤った word を出荷できない）。
- parser の IR 配列が一杯のとき **黙って命令を捨てていた** → 容量は body 命令数から算出し、超過は error。
- 旧 IR の `DOT`／`COMPOSE`／`EXTRACT` と `swizzle` field は廃止（scalar IR では存在し得ない）。

## 5. 残り（VK-2 の続き、順序つき）
1. sampler SEND の descriptor（binding table index、message type、SIMD mode）と、終端 SEND（VS = URB write、FS = render target write）の message。値は Mesa の定義から出典つきで `vk/linux/eu-encoding-gen12.inc` へ転記する（記憶から書かない）。
2. payload／出力の register 規約を E-103 で確定した 3DSTATE 側の値（dispatch GRF start、URB entry 読取り、PS の barycentric 入力）と突合せ。FS の入力は実際には plane 方程式＋barycentric からの補間で、`LOAD_INPUT` = payload register の MOV という今の規約は FS では成り立たない見込み（未検証と明記した範囲）。
3. そのうえで最初の GPU 実行試験（parity の draw 経路に compiler 出力の kernel を載せ、readback を独立式と比較）→ ここで初めて「GPU検証済み」を付ける。

## 6. E-128 更新（2026-09-21）: GPU 検証済み

§0 の表の「GPU検証済み = 該当なし」は過去の状態。E-128 で出荷版 `cuboid.{vert,frag}.spv` を executor の compiler に通した kernel が Latitude 5330 の実 GPU で動き、time_ms = 0 と 2500 のフレームが Mesa 参照 kernel のフレームと **同一 SHA-256**、独立オラクル mismatch 0 だった（`results-ws031.md` E-128）。したがって §2 の項目 2・4〜6・9・10・13〜17 は固定 shader の範囲で「GPU検証済み」。
その過程で §0 の「EU生成済み」の根拠だった model が、**同じ誤った encoding table を encoder と共有していた**ことが分かった（float の型番号、region の width、math selector、即値の表し方が Gen12 と違っていた。model は自分の table で decode するので気付けない）。いまの判定者は Mesa の assembler / disassembler（`tests/run-vk-gentool-test.sh`）で、model は命令の意味の検算として残している。§5 の 1〜3 は E-128 で実施済み（規約は `compile.c` 冒頭）。
