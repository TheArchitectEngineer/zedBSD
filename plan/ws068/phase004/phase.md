<!-- awesome-plan project=zedbsd record=ws068p004 -->

# ws068-p004: GLES 2.0 の残り（GL の向きの built-in、gl_DepthRange、sampler の配列）と試験

Phase ID: `ws068-p004`
Parent: [WS068](../ws.md)
Status: in-progress（2026-10-05 P1 generation17、q747。実装と host の試験まで、T1 待ち）
Phase disposition: normal
Queue: q747（P1、ベータ2。p009 → p004 → p007）

## 範囲の決め方（[guide.md](../guide.md) §4 の「p004 の範囲」）

2026-10-05 に次を調べた。

| 調べたこと | 結果 |
| --- | --- |
| ES 2.0 の 142 の entry point が `libglesv2/exports.map` に在るか | 全部在る |
| stub に近い entry point（`glCompressedTexImage2D`・`glShaderBinary`・`glSampleCoverage`・`glLineWidth`・`glPolygonOffset`・`glValidateProgram`・`glGetShaderPrecisionFormat`・`glHint`・`glCopyTexImage2D`・`glGetUniformfv` ほか） | ES 2.0 の範囲では実装済み（`glCompressedTexImage2D` は `GL_NUM_COMPRESSED_TEXTURE_FORMATS` 0 で、ES 2.0 では許される）。`GL_FIXED` の属性、`GL_LUMINANCE_ALPHA`・16 bit の pack の format、`GL_MAX_*_VECTORS` も在る |
| GLSL ES 1.00 の言語（`#pragma`・`#extension GL_OES_standard_derivatives`・struct の uniform の配列・`mat2` の配列・`invariant`・`gl_FragData`・built-in の定数・`gl_PointSize`・`bool` の配列） | 自前の compiler（`glsl-host` の `glsl-test compile`）で全部 compile できる。struct の uniform は `spirv.c` の `spirv_flatten` で名前ごとに反射される |
| `gl_FragCoord` の y | Vulkan の向き（上から）。framebuffer 0（window も pbuffer も、`framebuffer.c` 211 行の `target->flip = 1`）は `gles_spirv_position` で y を反転して描くので、GL の `gl_FragCoord.y`（下から）と逆になる（[phase019](../phase019/phase.md) の制限の 1 行目）。FBO は反転しないので一致 |
| `gl_PointCoord` | Vulkan も GL（`GL_POINT_SPRITE_COORD_ORIGIN` の既定 UPPER_LEFT）も上を t=0 とする。framebuffer 0（反転して描く）では一致し、FBO（反転しない、image の行は GL の下から）では t が逆になる |
| `gl_DepthRange` | compiler が宣言していない（`'gl_DepthRange' is not declared`）。ES 2.0 の built-in の uniform |
| sampler の配列（`uniform sampler2D tex[2];`） | compiler は通すが、libglesv2 の descriptor の layout は sampler ごとに `descriptorCount = 1`（`program.c` 3330 行付近）で、配列の要素ごとの unit・`glUniform1iv`・descriptor の書き込みが無い。ES 2.0 は定数の添字の sampler の配列を許す |

## この Phase でやること

1. **`gl_FragCoord` を GL の向きに**: framebuffer 0（window と pbuffer、y を反転して描く target）では `y' = H − y`（H は描画先の高さ。pixel の中心の 0.5 を含めて GL と一致する）、FBO（反転しない）では今のまま。
2. **`gl_PointCoord` を GL の向きに**: FBO（反転しない）で `t' = 1 − t`、framebuffer 0 では今のまま。
3. **`gl_DepthRange`**（`struct gl_DepthRangeParameters { highp float near; highp float far; highp float diff; }`）: `glDepthRangef` の値。
4. **sampler の配列**: 反射で配列の大きさを持ち、binding の `descriptorCount` を要素の数に、要素ごとに texture unit（`glUniform1i(loc("tex[1]"))`・`glUniform1iv(loc("tex"), 2, …)`）、draw の descriptor の書き込みを要素ごとに、`glGetActiveUniform` の size。GLSL 3.00 の sampler の配列（定数の添字）も同じ道で動く。`GLES_UNITS`（16）を超える数は link で断る。
5. **試験**: egltest に scene `es2` を足す（下の「試験」）。glsl-host の `pass/` に 1〜4 の shader、spirv-host に sampler の配列の反射。

### 方式（1〜3: 隠れた uniform）

- compiler が、shader が `gl_FragCoord`・`gl_PointCoord`・`gl_DepthRange` を読む時だけ、既定の uniform block（libglesv2 の動的な UBO、`program->uniform_data`）に
  隠れた uniform を足す: `gl_ZedFragment`（vec4: `a, b, c, d`。`gl_FragCoord.y' = a + b·y`、`gl_PointCoord.t' = c + d·t`）と `gl_ZedDepthRange`（vec4: near, far, diff, 0）。
  `gl_` で始まる名前は application が宣言できないので衝突しない。`glGetActiveUniform`・`glGetUniformLocation` からは見えないようにする。
- libglesv2 は draw ごとに、描画先が反転するか（`target->flip`）と高さで `a, b, c, d` を（framebuffer 0: `H, −1, 0, 1`、FBO: `0, 1, 1, −1`）、`glDepthRangef` の値で depth range を、uniform の data に書く（既定の uniform は draw ごとに stream に写るので、窓の大きさが変わっても pipeline は作り直さない）。
- push constant を使わない理由: libglesv2 の pipeline layout に今は無く、i915 の native の実行器（F-023）での扱いが未確認。既定の uniform block は両方の backend で動いている。
- `glShaderBinary` の SPIR-V（application が渡す binary）は変えない（Vulkan の向きのまま。今と同じ）。

### 範囲外（別の Phase か Future Work の候補）

| 項目 | 理由 | 行き先の案 |
| --- | --- | --- |
| ETC2・EAC の圧縮 texture（`glCompressedTexImage2D`） | ES 2.0 では不要だが、libglesv2 が名乗る ES 3.0 では必須（`GL_NUM_COMPRESSED_TEXTURE_FORMATS` ≥ 10）。i915 の Gen12 は hardware に無く、CPU での展開が要る | ws068-p039（2026-10-05 Q1 が planned で新設） |
| struct の中の sampler | ES 2.0 で許されるが、使う application は少ない | Future Work（Q1 が行を足す） |
| desktop の GLSL 1.20 の uniform の初期値、`continue` を含む `switch` | ES 2.0 ではない（phase019 の制限） | p007 で残りとして記録 |
| Khronos の dEQP | 外部 package の取り込みとライセンスの監査が要る（guide §4） | Future Work（Q1 が行を足す） |

## 試験

- host: `glsl-host` の `pass/` に `es2-fragcoord.frag`・`es2-pointcoord.frag`・`es2-depthrange.frag`・`es2-samplers.frag` を足し、compile・link・`spirv-val`・lavapipe の実行（`exec/`）で
  隠れた uniform の値から期待の色になるか。spirv-host に sampler の配列の反射（大きさ 2、binding 1 つ、`descriptorCount` 2）。
- egltest の scene `es2`（window と `--platform=pbuffer` の両方）:
  - `gl_FragCoord.y / H` を赤に描き、`glReadPixels` の下の行が暗く上の行が明るい（GL の向き）。FBO に描いて読んでも同じ。
  - 大きさ 32 の点に `gl_PointCoord` を描き、点の上の端の t が 0、下の端が 1（window と FBO で同じ）。
  - `glDepthRangef(0.25, 0.75)` で `gl_DepthRange.near`・`far`・`diff` を色に描き、0.25・0.75・0.5。
  - 2 枚の texture（赤と緑）を `uniform sampler2D tex[2]` に `glUniform1iv` で unit 0・1 として渡し、2 つの和が黄色。
  - `EGLTEST CHECK scene=es2 … failures=0 glerror=0x0`。
- 回帰（T1、Venus）: p009 と同じ一式（egl-p008〜p030・glx-p013・p031・p033・x11-p004・p005、ws101 の venus.sh、boot test）。
  **注意**: `userland/retro/glxtest/gl32.c` の shader が `gl_FragCoord.xy` で texel を選ぶ。GLX の描画先の pbuffer は framebuffer 0（反転する）なので、`gl_FragCoord.y` の向きが変わる。glxtest の期待の値が今の（Vulkan の）向きに合わせて書かれていれば、GL の向きに直す（host で shader と期待を読んでから実装の時に決める）。glx-p033 で確かめる。

## 触る file（見込み）

- compiler: `userland/desktop/libglesv2/glsl/builtins.c`（`gl_DepthRange` の型と変数）、`emit.c`（`gl_FragCoord`・`gl_PointCoord` を読む所で隠れた uniform の変換、`gl_DepthRange` を隠れた uniform から）、`link.c`（隠れた uniform を既定の block に）。
- libglesv2: `program.c`（反射で隠れた uniform を外し、sampler の配列の大きさと unit、layout の `descriptorCount`）、`draw.c`（隠れた uniform の値、sampler の配列の descriptor）、`spirv.c`（配列の sampler の反射）、`gles.c`（`glDepthRangef` の値を隠れた uniform に）。
- 試験: `userland/tests/egltest/`（scene `es2`）、`plan/ws068/tests/glsl-host/pass/`・`exec/`、`plan/ws068/tests/spirv-host/`、`plan/ws068/tests/egl-p004.sh`（新、egl-p030.sh の形）。

## 完了の条件

- 上の 1〜4 が実装され、host の試験（glsl-host・spirv-host・ws101 の gles と glsl の host）が PASS、build の warning 0、style-check の新しい指摘 0。
- T1 の Venus で `egl-p004.sh`（scene `es2` の window と pbuffer）が exit 0、p009 と同じ回帰が全部 exit 0、boot test PASS。
- i915 の実機は p038 の範囲（未実施なら「未実施」と書く）。

## 結果

### 実装（2026-10-05、P1 generation17、commit b3797e23）

| 部分 | 内容 | 場所 |
| --- | --- | --- |
| `gl_DepthRange` | struct `gl_DepthRangeParameters { near; far; diff; }` の built-in の uniform（compute 以外の stage）。libGLESv2 が draw ごとに `glDepthRangef` の値を書く。`glGetUniformLocation` は -1（`glGetActiveUniform` には出る） | `glsl/builtins.c`（`builtins_uniform`・`builtins_depth_range`）、`program.c`（`program_builtin_uniform`）、`draw.c`（`draw_builtin_uniforms`） |
| GL の向き | fragment shader の隠れた uniform `gl_ZedFragment`（vec4 a, b, c, d）。`gl_FragCoord`・`gl_PointCoord` を読む shader だけ使い（`check_use`）、main の始めに input を読んで `y' = a + b·y`・`t' = c + d·t` にした変数を作り、code はその変数を読む（`emit_directions`・`emit_turned`）。値は framebuffer 0（`target->flip`）で `(H, −1, 0, 1)`、FBO で `(0, 1, 1, −1)`。`glGetActiveUniform` に出さず location も無い | `glsl/internal.h`・`builtins.c`・`check.c`・`emit.c`、`program.c`、`draw.c` |
| sampler の配列 | 型の要素の数（`glsl_type_sampler_units`）を link の `sampler` に持ち、1 つの binding（16 unit の上限は要素で数える）。反射は大きさを `size` に、layout の `descriptorCount` に。uniform は要素ごとの unit（`units[GLES_UNITS]`、`glUniform1i(loc("tex[1]"))`・`glUniform1iv`・`glGetUniformiv`）。draw は要素ごとに texture を決め `dstArrayElement` で書く | `glsl/types.c`・`link.c`・`emit.c`、`spirv.c`、`gles.h`、`program.c`、`draw.c`、`compute.c`（`gles_draw_descriptors` に target を渡す、dispatch は NULL） |
| 試験 | egltest の scene `es2`（`userland/tests/egltest/es2.c`・`es2.h`、`--scene=es2`）、`plan/ws068/tests/egl-p004.sh`（display・zdesktop の窓・pbuffer）、glsl-host の `exec/10〜12-es2-*.frag`・`pass/es2-builtins.*` と反射の検査（run.sh 3c）、vk-run の sampler の配列 | |

### 確認

| 確認 | 結果 |
| --- | --- |
| `make -j16 ZEDBSD_CONFIG=plan/ws068/tests/config-amd64-glsl.mk BUILD=build/ws068-p009-lib …/libEGL.so …/libGLESv2.so …/libGL.so …/bin/egltest` | rc=0、warning 0 |
| `sh plan/ws068/tests/glsl-host/run.sh build/ws068-p004/glsl-host` | `glsl-host: PASS`（vk-run 24 試験、新しい 3 つを含む。`es2: done`） |
| `sh plan/ws068/tests/spirv-host/run.sh build/ws068-p004/spirv-host` | `spirv-host: PASS` |
| `sh plan/ws101/tests/gles/run.sh build/ws068-p004/ws101-gles` | PASS |
| `sh plan/ws101/tests/glsl/run.sh …` | FAIL（`re-assembled` の 10 件）。変更の前の source でも同じ 10 件で FAIL（`build/ws068-p004-ws101s-base.log`）: host の `brw_asm` の再アセンブルの不一致で、この Phase の変更によらない（既存。Q1 に報告） |
| style-check | 新しい file（`es2.c`・`es2.h`）は 0、変えた file は base との比較で新しい指摘 0 |
| `sh -n plan/ws068/tests/egl-p004.sh` | OK |
| Venus（T1）・実機 | **未実施**（T1 に依頼） |

### 残り

- T1: `egl-p004.sh` と p009 と同じ回帰（特に glx-p033: glxtest gl32 の `gl_FragCoord`）、ws101 の venus.sh、boot test。

## Q1 の承認（2026-10-05）

範囲を案のとおり承認（技術の裁量）: (1)〜(3) は shader が読む時だけの隠れた uniform（gl_ZedFragment・gl_ZedDepthRange、glGetActiveUniform から隠す）、(4) は反映した配列の大きさを descriptorCount に。ETC2/EAC は新しい Phase p039（planned、ES 3.0 の主張に要る、i915 Gen12 は CPU の復号）として WS068 に足す（実行は別の Queue）。struct の中の sampler・dEQP は Future Work、desktop だけの GLSL の残りは p007。
