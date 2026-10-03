<!-- awesome-plan project=zedbsd record=ws073-p046 -->
# ws073-p046: BUG-053 — RAM を超える anonymous memory（残り: RAM + swap の 9 割の 1300 MiB）

Status: cleared（2026-10-04。Q1 判定）
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

## T1-049 の読みと再開の条件（P1 generation12 のラップアップ、2026-10-04。修正は未実装）

T1-049（b65fd40、512 MiB、swap 1 GiB、NVMe）: 900 MiB PASS（33.4 s）、450 は 1 回目に t=10 で SSH が切れ再試行 PASS（13.6 s）、1300 MiB は 602 s で打ち切り
（t=30 で 263271/332800 page、以後 30 s に約 1300〜1600 page、io_err=0、ps は全て残る）。証拠 `/home/awe/zedBSD-worktrees/t1/build/t1-049/`。

読み（コード、未検証）:
- **page の出し入れの繰り返しではない。** STAT の page_out・page_in は system の起動からの累計で、450・900 の run の分を含む。1300 の run の中では
  page_out の増え（605396 → 798113 = 192717）は swapped の増え（657 → 193229 = 192572）とほぼ同じ、page_in の増えは 145。1 page を 1 回出しているだけ。
- **遅くなる境が 262144 page（1 GiB）**: 書いた page が 263271 を越えた所から 1 page に約 20 ms。region の page の索引（`src/kern/vmspace.c` の
  `region_page_index_rebuild`・`region_page_index_insert`）が原因の第一候補:
  - 索引は「page 数の 2 倍以上の 2 の冪」の bucket で、page 数が bucket 数を越えるたびに作り直す。262145 page 目で 1048576 bucket（8 MiB を 1 つの
    `kern_calloc`）を求める。空きが 1 MiB の時は物理の連続 8 MiB が取れず、`region_page_index_rebuild` は**古い索引を先に解放してから**確保に失敗し、
    region は索引無しになる（`find_page` は region の list を全部たどる）。
  - さらに `region_page_index_insert` は「索引が無く page 数が MINIMUM 以上」で**毎回 rebuild を呼ぶ**ので、以後の fault ごとに 26 万 page の list を
    2 回たどり（数える・`find_page`）8 MiB の確保を試みて失敗する。fault 1 回が O(n) になり、観測の「1 page 約 20 ms」と合う。
- 直し方（案）: rebuild は新しい索引を先に確保し、失敗したら古い索引を残して使い続ける（chain が長くなるだけ）。失敗の後は page 数が倍になるまで
  作り直さない（`page_index_retry` の閾値を足す）。bucket 数に上限（例 2^18 = 2 MiB）を置く。host 試験は vmspace の索引の部分を切り出して、確保の失敗を
  注入して insert・find・remove が正しいことと rebuild の回数が O(log n) であることを確かめる。
- 450 の 1 回目の SSH の切断（t=10、page out が始まる頃）は未調査。sshd・cron が fault で待つ時間か、別の原因（BUG-053 の前の記録に同じ形の症状の
  記録あり）。
- 再開の条件: 上の直しを実装し、host 試験、kernel の build、T1 に swaphog 450・900・1300（`plan/ws073/tests/swaphog.c`、10 分の上限）を再依頼。
  1300 が終われば受け入れの残り（1300 MiB の未確認）が解ける見込み。

## 直し（q656 の後、P2 generation7、2026-10-04。P1 の読みと案のとおり）

- `src/kern/vmspace.c`・`include/kern/vmspace.h`:
  - `region_page_index_rebuild(region, keep)`: **新しい bucket を先に確保**し、取れた時だけ古い索引を解放して作り直す。取れない時は `page_index_retry` を
    page 数の 2 倍にし、`keep` なら古い索引を残す（chain が長くなるだけで全 page を持つ）、`keep` 無し（split の後、古い索引が list と合わない）なら索引を
    除いて list に任せる。戻り値は作り直したか。
  - bucket の数に上限 `VM_REGION_INDEX_MAXIMUM` = 2^18（64 bit で 2 MiB）。上限の後は作り直さず chain が伸びる（1300 MiB = 332800 page でも平均 1.3）。
  - `region_page_index_insert`: 索引が無い時も、上限の前に page 数が索引を越えた時も、`page_index_retry` に達するまで作り直さない。作り直しに失敗したら
    新しい page を古い索引に chain する（以前は失敗の後に索引が無くなり、以後の fault ごとに list の全部を 2 回たどって 8 MiB の確保を試みていた）。
  - split（`region_split`）の両半分は `keep` 無しで作り直し、右半分の `page_index_retry` を 0 に。
- host: `sh plan/ws073/tests/region-index-host.sh`（新、関数の本文を切り出して実物の struct で compile、ASan/UBSan、確保の失敗を注入）→
  **PASS**: 60 万 page を乱順に入れて全部見つかり bucket は上限、1/3 を除いても残りが見つかる、確保が失敗している間は古い索引が残り全 page を持ち、
  64000 page まで足しても作り直しの試みは 8 回以下（O(log n)）、memory が戻り page が倍になると大きな索引、split の後の作り直しの失敗は list で全部見つかる。
- build: `make -j16 ZEDBSD_CONFIG=plan/ws135/tests/config-amd64-settings.mk BUILD=build/p2-q652 build/p2-q652/vmunix` → exit 0、warning 0、
  `amd64 vmunix check: PASS`。`plan/tools/style-check.py src/kern/vmspace.c` の変えた範囲に違反なし（範囲の外の既存の指摘は残る）。
- QEMU（T に依頼）: 512 MiB の guest（native の image、swap 1 GiB）で `swaphog 450`・`900`（回帰）、`swaphog 1300 30`（10 分の上限）。未実施。
- 450 の 1 回目の SSH の切断（T1-049）は未調査のまま。

## 結果（Q1、2026-10-04）

cleared。T2-018（QEMU KVM、512 MiB guest、088b21d の kernel）: swaphog 900 bad=0（32.3 s）、450 bad=0（13.3 s）、1300 bad=0・total_s=53.3（直す前の T1-049 は 600 s で 288885/332800 page）、io_err=0、終わりの ps に init・cron・sshd。450 の 1 回目の SSH の切断（T1-049）は今回は起きず、原因は未調査。実機は未実施。
