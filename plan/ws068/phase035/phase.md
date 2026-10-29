<!-- awesome-plan project=zedbsd record=ws068p035 -->

# ws068-p035: desktop GL の compute、SSBO、image load/store、atomic counter

Phase ID: `ws068-p035`
Parent: [WS068](../ws.md)
Status: in-progress（一部だけ実装・試験未実施。2026-10-29 q1682-i01 P2。cleared にしない）
Phase disposition: normal
依存: p037。GLES 3.1 の compute の部分集合（GLSL ES 3.10 の compute、glDispatchCompute・SSBO・glMemoryBarrier）は WS101 の物（2026-09-30 main D4）。
再開（2026-10-29）: ユーザー「作業の手が空いてしまったら、テストなしで実装だけ進める戦略で、残っているWSのphaseをまんべんなく進めましょう。」
（Q1 の依頼 q1682-i01 の引用）。試験なし、build と host の試験まで。旧: 保留（2026-09-27、p037 と同じ）。

## 範囲

desktop GL の compute shader（GL_ARB_compute_shader、4.3）、shader storage buffer（GL_ARB_shader_storage_buffer_object）、image load/store
（GL_ARB_shader_image_load_store、glBindImageTexture、image 型と imageLoad・imageStore・imageAtomic*）、atomic counter
（GL_ARB_shader_atomic_counters、atomic_uint、GL_ATOMIC_COUNTER_BUFFER）。

## 実装（2026-10-29）: compute と SSBO の desktop の口だけ

- GLSL: desktop 4.30 以上の version mask に `GLSL_IN_ES310`（WS101 の compute・storage block・shared・barrier・atomic*・memoryBarrier*）を入れた。
  compute の stage は GLSL 4.30 か ES 3.10 が要る（`compute shaders need GLSL 4.30 or OpenGL ES 3.10`）、buffer block は `glsl_since(430, ES310)`。
- libGLESv2: `gles_compute_offered` が desktop GL（libGL）の context を、その GLSL の上限が 430 以上の時に受ける（WS101 の device の条件は同じ）。
- libGL: `compute.c` を build に入れ、`glDispatchCompute`・`glDispatchComputeIndirect`・`glMemoryBarrier` を export。`GL/gl.h` に 4.3 の compute の宣言と定数。
- 今の libGL の context の上限は 3.3（GLSL 330）なので、desktop の compute は 4.3 の context（p036）が来るまで使えない（3.3 では GL_COMPUTE_SHADER は
  `GL_INVALID_ENUM` のまま）。GL_ARB_compute_shader（GL 4.2 が要る）は 3.3 の context で名乗らない。
- glsl-host: `pass/compute430.comp`（storage block・shared・barrier・atomicAdd）、`fail/compute-330.comp`、`glsl-test` と run.sh が `.comp` を読む。

## 残り（未実装）

1. image load/store: GLSL の image 型（image2D 等、format の layout、memory 修飾）と imageLoad・imageStore・imageSize・imageAtomic*、SPIR-V の
   OpTypeImage（Sampled 2）・OpImageRead/Write、libGL の glBindImageTexture（STORAGE_IMAGE の descriptor、texture の image の STORAGE の usage と
   GENERAL の layout の遷移）、glMemoryBarrier の image の bit。
2. atomic counter: atomic_uint（layout(binding, offset)）、atomicCounterIncrement・Decrement・atomicCounter を storage buffer の atomic へ、
   GL_ATOMIC_COUNTER_BUFFER の indexed の binding point。
3. vertex・fragment の stage の storage block（WS101 は compute だけ）。
4. 4.3 の context（p036）での compute の試験。

## 判断（推しで進めた）

- compute を 3.3 の context に extension で出さない（GL_ARB_compute_shader は GL 4.2 を前提にする、と読んだ。未確かめ）。

## 検証

- build（2026-10-29）: amd64 の `libGL.so`・`libGLESv2.so`・`libEGL.so` → exit 0、warning 0（sysroot を作り直し）。
- host: `sh plan/ws068/tests/glsl-host/run.sh build/glsl-host-p035` → `glsl-host: PASS`。
- 未実施: arm64・riscv64 の build（p036 の後に一緒に）、desktop compute の実行（4.3 の context が無い）、Venus・i915。
