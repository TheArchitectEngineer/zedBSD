<!-- awesome-plan project=zedbsd record=ws245-p050d2 -->

# ws245-p050d2: prepare の check と WM/DBUF を xd に

Status: planned（2026-10-17 Q1 の判断で p050d を 4 段に分けた 2 段目。p050d1 の後）
Parent: [WS245](../ws.md)
Queue: q1229-i08（P1）
設計: [design.md](../design.md) §6.2 の p038・p050 の行、§2.6、§2.10、§3.3。着手の手がかりは [phase050d](../phase050d/phase.md) の 4

## 範囲

- prepare の判断を xd-screen の plan（p038 の影）を本物にする: `state.c` の compute（mode・link・M/N・PLL の語・CDCLK の下限・voltage）と `modeset.c` の `i915_modeset_prepare_state` の拒む条件、`clock.c` の CDCLK の check を xd-screen の plan と xd-clock に。旧の check との比べの 1 行（p038）を外す。
- WM/DBUF: `watermark.c` の DDB の割り当てと WM の計算・書き込みを xd-dbuf の allocate と level の書き込みに（p034 の影を本物に）。xd-output の記述（PLL の語、M/N、WM、DBUF）は plan から作る（殻の記述の写しを減らす）。
- 残す物: plane・flip・vblank（p050d3）、own の file の口（p050d4）。

## 受け入れ

- xd の host 試験（plan の値が旧の check の値と同じ: 5330 の panel、HDMI、DP-TC。WM/DBUF の語）、build 6 組 warning 0、旧 host 試験の前後の比べ（trnorm）、R11 の数。5330 は全部の段の後に 1 回。
