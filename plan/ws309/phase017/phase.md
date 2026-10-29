<!-- awesome-plan project=zedbsd record=ws309-p017 -->

# ws309-p017 QEMU の回帰と遅さの注入の host 試験

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §10（試験）。Queue: q1662-i10（P1）。
- 状態: in-progress（2026-10-30 q1662-i10 P1）。
- 依存: p003〜p014（cleared）、p016（同じ branch の source で流す）。

## 手順

1. host: WS309 の host 試験の全部（`plan/ws309/tests/*/run.sh`、ASan+UBSan と TSan）と、新しい遅さの注入の試験 `plan/ws309/tests/slow/run.sh`
   （lane の io-file・io-device・present・daemon・hand の pool に 2 s の flush・80 ms の open ×3・2 s の present・accept しない daemon・500 ms の照合 ×4 を
   与えたまま、main の側の give と take が 5 ms 未満で返る事、全ての job が戻る事）。既存の遅さの注入: `io-file`（fsync を遅くする）、
   `input-find`（open 80 ms・EIO）、`daemon-link`（volumed が accept しない間の event loop の呼び出し 5 ms）。
2. QEMU（Venus）: この branch の tree から作った image（`plan/tools/guest/test-image.sh plan/ws309/tests/config-amd64-p1.mk build/ws309/img16 --file …`）で、
   起動（`plan/tools/boot-test.sh`）、WS309 の guest の試験（grave・resize・hand・scanout・rotate・spawn・io-device2・daemon・font・task・io-file・io-read）、
   GPU の境界（`plan/tools/gpu-boundary/forge-guest.sh`・`fence-guest.sh`）、`KWL THREADS` の行と終わりの `KWL PERF` の pass の最大。
   WS099 の基準の C1・C2・C9 は基準の image（`plan/ws099/tests/build-criteria-image.sh`）で `plan/ws099/tests/criteria.sh IMAGE OUT C1 C2 C9`。
3. design §10 の thread の検査（`KWL_THREAD_CHECKS`、main の thread で `vkQueueSubmit`・present・fsync・open・fork を assert）は、compositor を host で
   link する試験が無いので、この Phase では作らない（理由と代わりを「結果」に書く）。
