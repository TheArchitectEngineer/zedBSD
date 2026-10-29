<!-- awesome-plan project=zedbsd record=ws068p057 -->

# ws068-p057: GL 4.2 の image load/store（2）: GL の API

Phase ID: `ws068-p057`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
依存: p056
大きさの見積もり: 8 LW
出どころ: p035 1（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- glBindImageTexture（image unit 8、level・layered・layer・access・format）、texture の image に VK_IMAGE_USAGE_STORAGE_BIT（format が許す時）、STORAGE_IMAGE の descriptor（graphics と compute の layout）、GENERAL の layout の遷移、glMemoryBarrier の image の bit、device の fragmentStoresAndAtomics・vertexPipelineStoresAndAtomics の条件、GL_ARB_shader_image_load_store の名乗り。

## 受け入れ

- Venus で `sh plan/ws068/tests/venus-all.sh OUTDIR` の glx-p037（glxtest `--gl33`）が PASS し、その中の outcome「image-load-store」が緑（`GLXTEST GL33 check` の行に FAILED が無い）。回帰（venus-all の他の試験）も PASS（fragment shader の imageStore を次の draw の imageLoad で読む）
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。
