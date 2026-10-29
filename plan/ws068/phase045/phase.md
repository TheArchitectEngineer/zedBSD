<!-- awesome-plan project=zedbsd record=ws068p045 -->

# ws068-p045: GL 4.0 の sample shading（GL_ARB_sample_shading）の Venus の試験

Phase ID: `ws068-p045`
Parent: [WS068](../ws.md)
Status: in-progress（実装済み。残りは下の「範囲」の Venus の試験）
Phase disposition: normal
依存: p037
大きさの見積もり: 2 LW
出どころ: p036 c（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- 実装済み（q1682-i01: gl_SampleID・gl_SamplePosition・gl_SampleMask(In)、glMinSampleShading、GL_SAMPLE_SHADING、pipeline の sampleShadingEnable）。
- 残り: glxtest `--gl33` に outcome「sample-shading」を足す（4 sample の FBO に gl_SampleID ごとに色を変えて書き、resolve した色が sample の平均になる。device に sampleRateShading が無ければ skipped で緑）。

## 受け入れ

- Venus で `sh plan/ws068/tests/venus-all.sh OUTDIR` の glx-p037（glxtest `--gl33`）が PASS し、その中の outcome「sample-shading」が緑（`GLXTEST GL33 check` の行に FAILED が無い）。回帰（venus-all の他の試験）も PASS
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。

## 備考

- 実装の記録は [phase036](../phase036/phase.md) の「実装: c. sample shading」。
