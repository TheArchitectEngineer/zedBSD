<!-- awesome-plan project=zedbsd record=ws068p056 -->

# ws068-p056: GL 4.2 の image load/store（1）: GLSL の image

Phase ID: `ws068-p056`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
依存: p037
大きさの見積もり: 8 LW
出どころ: p035 1（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- image1D〜imageCubeArray・image2DMS 等の型（i・u も）、layout の format（rgba8・r32f・r32ui 等）、memory の修飾（coherent・volatile・restrict・readonly・writeonly）、imageLoad・imageStore・imageSize・imageAtomicAdd 等、`early_fragment_tests`、SPIR-V の OpTypeImage（Sampled 2）・OpImageRead・OpImageWrite・OpImageTexelPointer と atomic、capability（StorageImage*）。1.30〜3.30 は `#extension GL_ARB_shader_image_load_store`。

## 受け入れ

- glsl-host が PASS（image の pass/fail、spirv-val、lavapipe の compute か fragment の exec で imageStore → imageLoad の値）
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。
