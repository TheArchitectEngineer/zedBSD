# WS298 design.md 版 1 の design-reviewer の指摘（2026-10-27）

design-reviewer（agent a69fb2559c68721b4）が worktree の 89cea113b で読んだ結果の要約。原文は P1 の会話にあり、ここには指摘と design.md 版 2 での扱いだけを残す。

| # | 重さ | 指摘 | 版 2 での扱い |
| --- | --- | --- | --- |
| 1 | 重大 | NVMe の区画（q1612 の p5 `zedBSD-root`・p6 `zedBSD-swap`）と USB が同じ PARTLABEL を持ち、NVMe が見えると `block_identity_resolve()` が EEXIST（src/kern/block-identity.c:188）を返し USB 起動の root の mount が失敗する。今の USB は SystemRescue | §12 D1（ユーザーの判断）。p002 の merge の前に決める |
| 2 | 中 | 使わない root port の bus 番号が残り core がその下を列挙する（pci.c:471-481） | §5.1 の 4・5: 使わない port は bus 番号を 0 に |
| 3 | 中 | WS の目標（NVMe の区画から root を mount）を満たす Phase が無い | §12 D2、ws.md に p004 の案 |
| 4 | 中 | 受け入れが割り込みの付け替えを証明しない | §9.3 に連続読み・timeout 0・handler の回数・Debian への reboot |
| 5 | 中 | 実機の試験は T1、止まった時の回復 | §9.3 は T1 への依頼、`pci.vmd=off` の起動の token、§10 に止め方 |
| 6 | 中〜軽 | VMD の MSI-X の他の entry が firmware のまま、entry の数の確認が core に無い | §6.1: table を map し数え、他の entry を mask |
| 7 | 軽 | CFGBAR の map の前に claim が要る（pci.c:1645） | §7.1 |
| 8 | 軽 | driver の登録の順の理由が誤り | §3 を直した |
| 9 | 軽 | pci-window.c は ACPI の時だけ link（vmunix.mk:353-355） | §8 |
| 10 | 軽 | 窓を閉じる書き込みの順と幅、Values referenced | §5.1 の 3 |
| 11 | 軽 | Linux の MEMBAR_SHADOW_VSCAP（"SHDW"）を書き落とし、他の client の ID | §1.2 |
| 12 | 軽 | root port 自身の BAR | §5.1 の 6 |
| 13 | 軽 | 止める範囲（endpoint・root port の MSI/hot-plug/PME・PMCSR） | §5.1 の 2・5 |
| 14 | 軽 | suspend は深い物から: NVMe が suspend された後で VMD が EBUSY | §7.3 を直した |
| 15 | 軽 | pcat_map_bar が BAR を移しうる（pci-pcat.c:478-505） | §6.1: establish の後に BAR を読み直す |
| 16 | 軽 | 割り込みの表と lock の初期化の順 | §6.2 |
| 17 | 軽 | host 試験の不足 | §9.1 |
| 18 | 軽 | VMD の bus が root_buses の先頭: 列挙の順・survey の参照が変わる | §3、§9.3 |

U1〜U9 には全部「賛成」（条件: U1 は指摘 2、U7 は指摘 14 の記述の訂正、U8 は `irq.index` の使い方の理由を code に書く）。
