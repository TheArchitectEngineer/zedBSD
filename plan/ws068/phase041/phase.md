<!-- awesome-plan project=zedbsd record=ws068p041 -->

# ws068-p041: GL 4.3 の debug output（GL_KHR_debug）

Phase ID: `ws068-p041`
Parent: [WS068](../ws.md)
Status: cleared（2026-10-29 q1686-i01 P2。Venus の venus-all（build/tmp/q1686-v5）で受け入れを確かめた）
Phase disposition: normal
依存: p037
大きさの見積もり: 3 LW
出どころ: p036 i（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- glDebugMessageCallback・Control・Insert、glGetDebugMessageLog、debug group、object label、GL の誤りを message に

## 受け入れ

- Venus で `sh plan/ws068/tests/venus-all.sh OUTDIR` の glx-p037（glxtest `--gl33`）が PASS し、その中の outcome「debug」が緑（`GLXTEST GL33 check` の行に FAILED が無い）。回帰（venus-all の他の試験）も PASS
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。

## 備考

- 実装: q1682-i01（P2）。記録の本体は [phase036](../phase036/phase.md) の「実装: i. KHR_debug」。
