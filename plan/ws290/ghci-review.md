# GHCI レビュー 第 21 版（回答 第 20.0 版の C01・C02 の直しの確かめの依頼と、occlusion query の counter の疑問 N81、zedBSD main 9b210811d、ghci.md c74accc、2026-10-28）

- 読んだ回答: `git -C ~/amaterrace-gpu show cabb926:docs/ghci-review-reply.md`（1 行目「# GHCI Review Reply - Version 20.0 (2026-10-28) - Commit: c74accc」、CONDITIONAL APPROVE、重大 C01・C02）と `git -C ~/amaterrace-gpu diff dd68e87 cabb926 -- docs/ghci.md`（N80: §3.3・§10 の MEM_READY の待ち）。
- driver の正本は ghci.md **c74accc**（288c459 からの差は N80 だけと確かめた）。
- 直した code は `~/zedBSD-claude1`（branch main、**9b210811d**）に入っている。

## C01・C02 の直し（確かめてほしい）

| ID | 直し | file と行（9b210811d） |
| --- | --- | --- |
| C01 | `ghci_fence_read()` の末（64 bit の fence の語の 2 つの半分を読んだ後、値を返す前）に `kern_io_read_barrier()` を置いた。fence の語が過ぎたと見た後の CPU の読み（GPU が書いた image・query など）が fence より前に投機で読まれない。ticket の判定・query の結果・event の語の読みは全部この関数か、`kern_io_read_barrier()` の後の読みを通る | `~/zedBSD-claude1/src/drivers/gpu/ghci/ghci.c` 320〜352 行（barrier は 348 行） |
| C02 | 回復の `reset_and_check()` で、`CTRL.RESET` の自己クリアと `AXI_STUCK` の確かめの直後、register を書き直す前・`CTRL.RUN` の前・liveness の fence の前に、`STATUS.MEM_READY == 1` を有界（1000 ms）に待つ。来なければ reset の失敗として扱い、device を LOST にする（N80 の「Bypassing … risks … RECOVERY_LOST」の逆の側に倒す）。attach の初めの reset は前から MEM_READY を待っている | `~/zedBSD-claude1/src/drivers/gpu/ghci/recover.c` 779〜846 行の `reset_and_check`（待ちは 809〜813 行）、attach の側は `~/zedBSD-claude1/src/drivers/gpu/ghci/ghci.c` の `reset_device` |

- host 試験: 回復の中の MEM_READY が遅い時は待って回復し、来ない時は LOST になる場面を足した（`~/zedBSD-claude1/plan/ws290/tests/ghci-driver-host-test.c` の `scene_reset_mem_ready`）。全 PASS（driver 1028 ほか）。amd64・riscv64 の kernel の build の warning 0。
- C03〜C06 の承認・追認を確かめた。

## 新しい疑問

| ID | 重さ | 疑問 | driver の扱い（仮） | 求める答え |
| --- | --- | --- | --- | --- |
| N81 | M | occlusion query（`CMD_BEGIN_QUERY`・`CMD_END_QUERY`、ghci.md c74accc の 1135〜1143 行付近）の sample の counter は session ごとか、device に 1 つか。driver は WaitEvents の保留（p016）で 1 つの submission を ring の上で分けて置く事があり、分けた間に他の session の draw が入り得る | render pass の外で始めた query が有効な間は、driver は submission を分けない（event の待ちで避けられない時だけ分ける）。counter が device に 1 つなら、分けた間の他の session の sample が数えられ得る | (1) counter は session ごとか device に 1 つか。(2) device に 1 つなら、BEGIN と END の間に他の session の draw が入った時の値（含まれるか）。(3) driver が避けるべき並べ方があるか |

## 回答の形

- C01・C02 の確かめ（承認か、直しの要求）と、N81 への答え（今までの取り決めの形）を書いてほしい。

DONE
