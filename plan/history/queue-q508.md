<!-- awesome-plan project=zedbsd record=q508 -->

# q508（finished 2026-09-30）


- Purpose: WS103（compositor を libvulkan だけにする）の調査と設計。
- Timebox: この session。
- Focus: WS103（2026-09-30 夜 ユーザーが最優先に）。
- Approval: current user, 2026-09-30 夜、「では、このqueueをメインエージェントで実行してください。」（Q1 が示した q508 の案に対して）。
- Exact approved scope: [ws103-p001](ws103/phase001/phase.md) だけ。調査と設計文書（ioctl ごとの置き換え、fence の世代の照合と i915 の native の external fence、
  OS の backend の境界の形、macro を外した build の確かめ方、buffer ごとの import の費用、p002 以降の Phase の分け方）と design-reviewer のレビュー。
  code の変更、HAL（`hal.h`）、toolchain、Linux・FreeBSD の backend、evdev は範囲の外。libvulkan への追加は設計として書くだけ。
- Executor: メインのエージェント（Q1）。

| Attempt | Phase | Status | Dependency | Selection reason |
| --- | --- | --- | --- | --- |
| q508-i01 | [ws103-p001](ws103/phase001/phase.md) | cleared | なし（D1〜D3 は 2026-09-30 夜に決定） | 最優先の WS103 の最初の Phase。ユーザーが実行を指示 |

Dependency graph: `q508-i01/ws103-p001`（外部の前提なし）。


## Outcome

- q508-i01 / ws103-p001: **cleared**。[design.md](../ws103/design.md)（改訂 3、design-reviewer 2 回）。p002〜p007 を planned にした。
- 決定の具体化: D3 は dedicated の import で照らし bind でも守る形に。fence は WSI が present ごとに新しい fence を送り、compositor は poll だけ（protocol・kernel は不変）。
- ユーザーの確認待ち: ws.md の V1・V4 の言い回しの改訂（module の入れ替え、前後の比較）。
- 未実施: code・build・QEMU・実機（設計の Phase）。GitHub へは未公開。
