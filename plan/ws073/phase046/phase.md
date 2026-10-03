<!-- awesome-plan project=zedbsd record=ws073-p046 -->
# ws073-p046: BUG-053 — RAM を超える anonymous memory（残り: RAM + swap の 9 割の 1300 MiB）

Status: uncleared（q651-i01、P1 generation12、2026-10-04。T1-049 で 1300 MiB が 10 分で終わらない）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-053](../../bugs/BUG-053.md)
Queue: q651（2026-10-04 user「P1、P2はBUG-143, 053, 052, 162, 103, 129, 033, 157, 139を修正します。直す順序は任せます。」）

## 範囲

BUG-053 は 2026-09-25 の修正（commit 5a32f4e7、古い履歴）で 450・900 MiB が通った（512 MiB の guest、swap 1 GiB）。残りは **1300 MiB が 35 分で
終わらず、進んでいるか止まっているか未確認**。再開の条件（ticket）: 1300 MiB を 1 分ごとの統計を出しながら走らせ、page out が進むか見る。

## 道具（作り直し、2026-10-04）

当時の `swaphog`・`vmstatprobe` は ws062 の Phase の試験で、repository の作り直しで残っていない。
- [swaphog.c](../tests/swaphog.c)（[swaphog-build.sh](../tests/swaphog-build.sh) で guest 用に build）: `swaphog MIB [STAT_SECONDS]` は MIB MiB の
  private anonymous を page ごとに番号の模様で書き、全部を読み戻して検算する。STAT_SECONDS（既定 10）ごとに `/dev/system` の
  `KERN_SYSTEM_GET_VMSTAT` の行（free・resident・swapped・page in/out・reclaim・I/O error・swap の空き・commit）を出す。最後の行
  `SWAPHOG mib= bad= write_s= verify_s= total_s=`、bad=0 で exit 0。`swaphog stat` は統計の 1 行だけ。

## 読みで直したこと（2026-10-04）

- **swap の slot の確保が毎回 bitmap の先頭から 1 bit ずつ探していた**（`src/kern/swap.c` の `swap_alloc_slot`、`swap_lock` の spinlock で割り込みを
  止めたまま）。swap が埋まるほど 1 回の確保が使用中の slot の数だけ歩く（1 GiB の swap は 262144 slot、1300 MiB では 8 割以上が使用中）。
  → source ごとに `next_slot`（前回確保した次、`include/kern/swap.h`）から探し、使用中の 8 slot の byte を 1 歩で飛ばし、端で巻き戻る（next-fit）。
  1300 MiB の停止の原因とは断定していない（測定で確かめる）。

## 検証

- host: `sh plan/ws073/tests/swap-slot-host.sh` → `swap-slot-host: PASS`（swap.c から関数の本文を切り出して実物の struct で compile、ASan/UBSan。
  20000 回の乱数の bitmap・hint・大きさで、hint から巻き戻り順に最初の空きを返す、満杯で ENOSPC）。
- build: `make -j16 BUILD=build/p1-q651/amd64 build/p1-q651/amd64/vmunix` → exit 0、warning 0、`amd64 vmunix check: PASS`。
  `BUILD=build/p1-q651/amd64 sh plan/ws073/tests/swaphog-build.sh build/p1-q651/swaphog` → built。
- style: `style-diff.py src/kern/swap.c include/kern/swap.h` → 0。
- QEMU（T1 に依頼、未実施）: 512 MiB の guest（native の image、swap 1 GiB の partition）で `swaphog 450`・`900`（回帰、bad=0）と `swaphog 1300 30`
  （上限 10 分。STAT の行で page out が進むかを見る）。終わった後の `ps`（init・cron・sshd が生きている）。
- 実機: 未実施。

## 残り

- T1 の 1300 MiB の STAT の行で、進んでいる（遅いだけ）か止まっているかを見て次を決める。止まっているなら gdbstub で止まっている所を取る。

## 結果（Q1、2026-10-04、T1-049、QEMU KVM、512 MiB guest、b65fd40）

uncleared。swap_free=262143/262143 で開始。900 MiB: `SWAPHOG mib=900 bad=0 total_s=33.4`。450 MiB: 1 回目は t=10 の verify の途中で `Connection to 127.0.0.1 closed by remote host.`（SWAPHOG の行なし、14 s）、900 の後の再試行は `bad=0 total_s=13.6`。1300 MiB: 602 s で打ち切り。t=30 で write 263271/332800、以後 30 s ごとに約 1300〜1600 page、t=600 で 288885/332800、page_out=798113、swap_free=68912、io_err=0。後の ps に init・sshd・cron など全て残る（止まりではなく極端に遅い）。証拠 worktrees/t1/build/t1-049/。再開: P1 が 1300 MiB の遅さ（1 page あたりの page_out の多さ、約 2.7 回）と 450 の 1 回目の SSH の切断を調べる。
