<!-- awesome-plan project=zedbsd record=ws068p054 -->

# ws068-p054: GL 4.1 の separate shader objects（2）: pipeline の描画

Phase ID: `ws068-p054`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
依存: p053
大きさの見積もり: 8 LW
出どころ: p036 f（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- separable な program の stage を組み合わせた描画: stage ごとの module・descriptor の layout の合成、varying の location による照合（名前ではない）、gl_Position の書き換えを最後の stage に、pipeline の cache の key に stage の組。GL_ARB_separate_shader_objects の名乗り。

## 受け入れ

- Venus で `sh plan/ws068/tests/venus-all.sh OUTDIR` の glx-p037（glxtest `--gl33`）が PASS し、その中の outcome「separate-shaders」が緑（`GLXTEST GL33 check` の行に FAILED が無い）。回帰（venus-all の他の試験）も PASS（vertex と fragment を別の program にした描画が緑）
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。
