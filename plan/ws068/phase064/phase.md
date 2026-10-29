<!-- awesome-plan project=zedbsd record=ws068p064 -->

# ws068-p064: GL 4.5 の direct state access（1）: buffer・texture・framebuffer

Phase ID: `ws068-p064`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
依存: p037
大きさの見積もり: 8 LW
出どころ: p036 j（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- glCreateBuffers・glNamedBuffer*、glCreateTextures・glTexture*・glBindTextureUnit、glCreateFramebuffers・glNamedFramebuffer*（GL_ARB_direct_state_access の前半。既存の bind する関数を名前で呼ぶ形に）。

## 受け入れ

- Venus で `sh plan/ws068/tests/venus-all.sh OUTDIR` の glx-p037（glxtest `--gl33`）が PASS し、その中の outcome「dsa-objects」が緑（`GLXTEST GL33 check` の行に FAILED が無い）。回帰（venus-all の他の試験）も PASS
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。
