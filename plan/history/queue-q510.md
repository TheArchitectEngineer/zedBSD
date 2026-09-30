<!-- awesome-plan project=zedbsd record=q510 -->

# q510（finished 2026-09-30）


- Purpose: WS103 の p003（libvulkan: VK_KHR_dedicated_allocation と VK_KHR_get_memory_requirements2、image の capability の import での記述の照合、bind の守り）。
- Timebox: この session。
- Focus: WS103（最優先）。
- Approval: current user, 2026-09-30 夜、「消して作り直してOKです。次に進みましょう。」（Q1 が「独立して進められるのは p003 と p005、進める場合は Queue を作る」と示した後）。
  Q1 は p004 の前提の p003 を選んだ。
- Exact approved scope: [ws103-p003](ws103/phase003/phase.md) だけ（[design](ws103/design.md) §2.3 の libvulkan の側の 1〜5 と §3 の p003 の行）。compositor の変更（p004）、fence（p005）、HAL、toolchain は範囲の外。
- Executor: メインのエージェント（Q1）。

| Attempt | Phase | Status | Dependency | Selection reason |
| --- | --- | --- | --- | --- |
| q510-i01 | [ws103-p003](ws103/phase003/phase.md) | cleared | ws103-p001 cleared（design） | p004 の前提。p002 と独立 |

Dependency graph: `ws103-p001 (cleared) -> q510-i01/ws103-p003 -> (future) ws103-p004`。


## Outcome

- q510-i01 / ws103-p003: **cleared**。libvulkan に VK_KHR_get_memory_requirements2・VK_KHR_dedicated_allocation（公開の header は pinned の宣言から道具で生成）、
  image の capability の dedicated の import での照合（`dedicated.c`）、bind の守り。host の試験 18 件 PASS（通常・ASan/UBSan）、QEMU の C1・C2・boot test PASS、
  5330 の passthrough の smoke PASS。dedicated の無い import を拒む処理は p004 へ移した。
- 準備: 別の checkout を指す CMake の cache を消して作り直した（ユーザー許可）。BUG-126 を記録。GitHub へは未公開。
