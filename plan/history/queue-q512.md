<!-- awesome-plan project=zedbsd record=q512 -->

# q512（finished 2026-09-30）


- Purpose: WS103 の p005（libvulkan の WSI が、Wayland の target の present ごとに新しい fence を作って送る）。
- Timebox: この session。
- Focus: WS103（最優先）。
- Approval: current user, 2026-09-30 夜、「ws103完了まで自走してください。phaseごとにコミットしてください。」（p005・p006・p007 を順に実行する指示。
  Queue は Phase ごとに 1 つ作る）。
- Exact approved scope: [ws103-p005](ws103/phase005/phase.md) だけ（[design](ws103/design.md) §2.4 の WSI の側）。compositor（p006）、HAL、toolchain、kernel は範囲の外。
- Executor: メインのエージェント（Q1）。

| Attempt | Phase | Status | Dependency | Selection reason |
| --- | --- | --- | --- | --- |
| q512-i01 | [ws103-p005](ws103/phase005/phase.md) | cleared | ws103-p001 cleared（design） | 設計の順。ユーザーの自走の指示 |

Dependency graph: `ws103-p001 (cleared) -> q512-i01/ws103-p005 -> (future) ws103-p006`。


## Outcome

- q512-i01 / ws103-p005: **cleared**。libvulkan の WSI は Wayland の target の present ごとに新しい fence を作って送る（slot の `sent` で display の job と分ける）。
  新しい fence の最初の submit の host の reset を省く。QEMU の Venus で fence 600 個が全て世代 1（前は 598 まで進んだ）、時間は同じ。p054・C1・C2・boot test・5330 PASS。GitHub へは未公開。
