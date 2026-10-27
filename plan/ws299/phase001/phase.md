<!-- awesome-plan project=zedbsd record=ws299-p001 -->

# ws299-p001 5330 の NVMe の読み書きの測定と崖の原因の確かめ

- 親: [WS299](../ws.md)。Queue: q1616 / q1616-i01（担当 P1、2026-10-27）。
- Status: cleared（2026-10-28 Q1。受け入れの数は 2026-10-27 10:28 ユーザー「Debianの95%以上をゴールにします。」、HMB は「やる」（推しの「行わない」は採らない、p006 を残す））
- Disposition: normal

## 受け入れ（ws.md の p001 の行）

- 5330 の Debian と zedBSD で同じ大きさ・位置の読み書きを 1 本の script で測る（counter を前後で取る）。
- 崖の仮説 H1〜H4 を数で確かめ、直しの順と受け入れの数の案、HMB の要否を出す。

## 実施（2026-10-27、全部が素の 5330 の証拠。QEMU は使っていない）

### 測定の道具（commit c35b10cf0、以後の直しは p002 の commit）

| file | 役目 |
| --- | --- |
| `plan/ws299/tests/nvme-bench-5330.sh` | centris から 1 回の測定（`/tmp/i915-hw.lock` の下）。`debian NAME [read] [write]`、`zedbsd NAME VMUNIX\|- [read] [write]`（kernel を p4 に置き、mode ごとに NVMe から 1 回起動して測り Debian に戻す）。write の前に Debian で p6 を保存し、後で書き戻して `cmp` |
| `plan/ws299/tests/bench-zedbsd.sh` | zedBSD の側（root）。`/dev/zero` の read(2)、冷えた 128 MiB を bs 64k・1m・4m、100 GiB の位置から 256 MiB ×16 の連続（各段の後に `vfs.bufcache.*`・`vfs.cache_memory.stats`・`vfs.io.stats`・`hw.cputimes`）、最後の段の再読み。write は root の UFS の file と p6（swapoff の後）|
| `plan/ws299/tests/bench-debian.sh` | Debian の側（root）。dd 直（`iflag=direct`）と page cache 経由、fio（seq 128k QD1・QD32、rand 4k QD1・QD32）を HMB の on・off で。書きは p6 と ext4 の 1 file だけ |
| `plan/ws299/tests/summary.py` | log を表に（MB/s は 10^6 byte/s） |
| `plan/ws299/tests/config-5330-cache128.mk` | 確かめ用の kernel（buffer cache の上限 128 MiB） |

- 5330 の Debian に `fio` と `nvme-cli` を apt で入れた（測定用）。Debian の書きは p6（zedBSD の swap、保存→書き戻し→`cmp` 一致）と `~/ws299/w.bin`（1 GiB、毎回上書き）だけ。zedBSD の書きは root の UFS の `/home/kei/ws299-w.bin`（768 MiB が残っている、消すのは Q1）と p6。
- 要約: `plan/ws299/evidence/{deb1,zb-main,zb-c128,zb-p002a}-summary.md`。log の全文は P1 の worktree の `build/hw5330/ws299/`（git の外）。

### BG5 と Linux の値（`nvme id-ctrl`、Debian の dmesg）

- MDTS 9（4 KiB の page で 2 MiB）、HMB: hmpre 16384（64 MiB）・hmmin 3584（14 MiB）・hmmaxd 1024、Linux は「allocated 64 MiB host memory buffer (1 segment)」、`12/0/0 default/read/poll queues`、`max_sectors_kb` 128、`read_ahead_kb` 128。

### 結果の表（MB/s）

| 測り方 | Debian 6.19 | zedBSD main（7ceb026de） | zedBSD cache 128 MiB | zedBSD p002 |
| --- | --- | --- | --- | --- |
| `/dev/zero` の read(2)、bs 64k・1m・4m | — | （未測定） | 3355・4474・4130 | 3355・4474・4474 |
| 冷えた 128 MiB、bs 64k・1m・4m | 直 697・1500・2100 | 305・163・91 | 172・105・115 | 344・384・363 |
| 連続 256 MiB ×16、最初・最後の段 | 直 1600・1600、page cache 3700・1100 | 56・15.0 | 125・127 | 384・363 |
| 連続、16 段の幅 | 直 1400〜2000 | 3.3〜56（上限の後 3.3〜21） | 105〜130 | 320〜384 |
| cache の再読み（256 MiB） | — | 3355 | — | 2237 |
| fio seq 128k QD1・QD32（HMB on） | 1123・3097 | — | — | — |
| fio rand 4k QD1・QD32（HMB on / off） | 36.0・340 / 29.8・286 | — | — | — |
| 書き: raw 1 GiB | 直 dd 1500、fio seq 128k QD1・QD32 1633・1903、rand 4k QD1・QD32 341・1322 | p6 4.1〜14.3 | — | p6 597〜655 |
| 書き: file | ext4 1 GiB＋fsync 1600 | UFS 268→77→53 | — | UFS 433・479（＋sync 0.09 s） |

- zedBSD の上限の前後: main の kernel は cache が 985 MiB（RAM/8）に達した段から evictions = misses で、各段 25〜83 秒。全段で dd の sys ≈ real（kernel の CPU、device を待っていない）。`IO_BUF_SINGLE_MEMORY`（確保の失敗）と `capacity_failures` は 0。
- 1_geom（単独の line）の段ごとの数は main の上限の後に 1 万〜4 万へ増えた。driver の読み（drv_rd）は段ごとに 4096（64 KiB の BIO）で変わらないので、これは dd 以外（測定の sysctl・dd の exec など）が cache から追い出された自分の file を読み直した分で、遅さの原因ではない。

### 仮説の確かめ

| 仮説 | 結果 | 根拠 |
| --- | --- | --- |
| H1 `alloc_metadata` の slab の線形の walk（irq を止めて） | **確か（崖の原因）** | ① 崖は cache が上限に達した段から（main: chunk2、cache 985 MiB）。② 上限を 128 MiB にすると崖が消え 4 GiB の間 105〜130 MB/s で一定（slab の数が 1/8、上限の後の walk も 1/8）。③ 上限の前も cache が育つほど遅い（head の slab が埋まる度に全 slab を歩く。1 line 約 60 µs と合う）。④ sys ≈ real。⑤ p002（O(1) の index）で 320〜384 MB/s が一定、最初と最後の段の比 0.95 |
| H2 上限の後の確保の失敗で 4 KiB・深さ 1 に落ちる | 否 | `IO_BUF_SINGLE_MEMORY`・`capacity_failures`・`physical_failures` が 0、drv_rd は段ごとに 4096 の 64 KiB の BIO のまま |
| H3 NVMe の driver の天井 | **確か（次の天井）** | p002 の後、256 MiB に 0.70〜0.84 秒のうち sys は 0.18〜0.39 秒。残りの約 0.45 秒は driver の待ち（64 KiB の BIO ＝ 4 KiB の command 16 個を深さ 4 で同期）。Debian の直の dd bs=1M の 24%、fio QD1 128k の 34% |
| H4 HMB | 連続には効かない | Debian で HMB を切っても seq 128k は変わらない（QD1 1123→1242、QD32 3097→3134）。rand 4k は QD1 -17%、QD32 -16% |
| H5（途中で立てた）大きい read(2) の user buffer の pin が超線形 | 否 | `/dev/zero` の read(2) は bs に依らず 3.4〜4.5 GB/s。main の bs の差は測った順に cache が育った分（H1） |

- 書きも同じ: main の kernel では UFS の file の書きが cache の育ちと共に 268→53 MB/s、p6 の raw の書きは上限の後 4〜14 MB/s。p002 の後は UFS 433〜479、raw 597〜655 MB/s（Debian の直の 1500 の約 40%）。

### 他の天井の寄与（p002 の後の数から）

- driver（H3）が今の上限: 1 BIO（64 KiB）に約 110 µs の待ち。1 command 4 KiB・bounce の写し・同期の深さ 4・queue 1 本が一つの塊で、分けて測る口（driver の中の時間）は無い。p003（MDTS に合わせた大きな command と PRP list）で command の数が 1/32、p004（非同期の BIO・深さ）で待ちの重なり、と分けて直しながら測る。
- buffer cache の CPU: 256 MiB に sys 0.18〜0.39 秒（約 700〜1400 MB/s 相当の上限）。line ごとの確保・写し（driver の bounce→呼び手の buffer→line の 3 回）。p005 の後に残れば写しを減らす。
- readahead・UFS の cluster: raw の dd は readahead を使わない（`vfs.readahead.stats` は見ていない）。p005 で UFS の file の読みを測る（今の root の UFS は 988 MiB で余り 28 MiB しかなく、file の読みの測定には別の区画か大きな root が要る、下の残り）。
- VMD の vector 1 つ: 今の同期の 1 queue では効いていない。p004 で queue を増やす時に要る。

## 受け入れの数の案（ユーザーへ、Q1 経由）

Debian の値は同じ 5330・同じ位置の測定（上の表）。

1. 量に依らない（p002 で達成見込み、p007 で確かめ）: `/dev/nvme0n1` の連続の読み 4 GiB（cache の上限の 4 倍）で、最後の 256 MiB の速さが最初の 256 MiB の 0.8 以上。p002 の kernel で 0.95。
2. raw の連続の読み（dd bs=1M、cache 経由）が Debian の直の dd bs=1M（1.5 GB/s）の **50% 以上（750 MB/s）**。今 384（24%）。p003・p004 の後に届く見込み（Debian の fio QD1 128k が 1.1 GB/s なので、128 KiB の command を同期で出すだけでも近づく）。
3. raw の連続の書き（dd bs=1M）が Debian の直の 1.5 GB/s の **50% 以上**。今 597〜655（40〜44%）。
4. UFS の file の連続の読み書き（1 GiB 以上、sync を含む）が Debian の ext4 の 1 GiB＋fsync（1.6 GB/s）の **40% 以上**。今の書き 433〜479（27〜30%、ただし 256 MiB の file で、root が小さく 1 GiB は書けない）。
5. 回帰: 他の機種（QEMU の nvme・virtio・USB）で遅くならない（T の回帰）。

## HMB の要否（案）

- 推し: **WS299 では行わない（p006 を取り消し、Future Work へ）**。理由: 連続の読み書き（WS299 の目標）には効かない（H4）。効くのは乱読みの約 17%（Debian、disk 全体の 4k）で、今の zedBSD の乱読みの上限は driver の同期（p003・p004）にあり HMB の差は測れない。64 MiB の DMA の memory と、電源の遷移（WS300 の APST・D3）で HMB を保つ手順が増える。
- 戻す条件: p004 の後に zedBSD の rand 4k が Debian の HMB 無しの値（QD1 29.8 MB/s）に届き、乱読みが目標になった時。

## 未実施・残り

- zedBSD の乱読み（rand 4k）は測っていない（zedBSD に fio が無い。p004 で深さを変える時に測る道具を作る）。
- UFS の file の読み・1 GiB 以上の file の書き: 5330 の zedBSD の root の UFS は image の大きさ（988 MiB、98% 使用）のままで、p5 の 15 GiB を使っていない（`5330-nvme-write.sh` が image の UFS を写すだけ）。file の測定の前に root を広げるか、測定用の UFS を作るかを Q1 に相談。
- `bench-zedbsd.sh` の write の UFS の file は `UFS_MIB`（既定 512）に下げた（最初の回は 1 GiB で 4 段目が空き不足、dd の message を出すように直した）。
- design-reviewer は p002 の直しが小さく（index の付け替えだけ）設計文書を作っていないので呼んでいない。p003 以降（driver の設計）で呼ぶ。
