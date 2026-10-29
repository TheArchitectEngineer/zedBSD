<!-- awesome-plan project=zedbsd record=ws068p060 -->

# ws068-p060: GL 4.3 の desktop の compute と SSBO（全ての stage）

Phase ID: `ws068-p060`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
依存: p037
大きさの見積もり: 6 LW
出どころ: p035 3・4（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- p035 の実装（desktop GLSL 4.30 の compute・SSBO、libGL の glDispatchCompute 等）の試験、vertex・fragment の stage の storage block（graphics の layout に storage の descriptor）、GL_ARB_compute_shader・GL_ARB_shader_storage_buffer_object を 3.3 の context に出すかの仕様の確認（出さないなら 4.3 の名乗り（p062）の後に試験）、glxtest の outcome「compute」。

## 受け入れ

- Venus で `sh plan/ws068/tests/venus-all.sh OUTDIR` の glx-p037（glxtest `--gl33`）が PASS し、その中の outcome「compute」が緑（`GLXTEST GL33 check` の行に FAILED が無い）。回帰（venus-all の他の試験）も PASS（compute shader が SSBO に書いた値と、fragment shader の SSBO の書き込みを読み戻す）。仕様で 3.3 に出せない時は p062 の glx-p062 の中で同じ判定
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。

## 備考

- p035 の実装（2026-10-29 q1682-i01）の記録は [phase035](../phase035/phase.md)。
