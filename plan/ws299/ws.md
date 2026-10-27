<!-- awesome-plan project=zedbsd record=ws299 -->

# WS299 5330 の NVMe の読み書きを速くする

- Master: [plan/master.md](../master.md)（2026-10-27 作成、q1615 P1 の計画）。Primary Milestone: MG004。Related: MG003（[WS298](../ws298/ws.md) VMD の driver、q1612 NVMe から起動）。
- Status: incomplete（p001・p002・p003・p009 cleared。p004 は実装と試験が済み Q1 の判定待ち、p005 は調べと案（q1634、P1）。現在の状態は下の「現在の状態」）
- 出典: 2026-10-27 ユーザー「5330でNVMeの最適化（速度向上）をWSを立ててやってほしいです。（あとで順番にやってほしいです）」「新規WSはまず計画を広く進めて、全体の依存関係を見ましょう。」
- 全体の依存と着手の順: [plan/agents/P1/pm-plan.md](../agents/P1/pm-plan.md)。

- 2026-10-27 09:39 ユーザー（クリック）: 受け入れの速さの数は p001 の測定の後に決める、HMB は p001 で要否を判断する。

- 2026-10-27 10:28 ユーザー（Q1 の受け入れの案 (1)〜(5)、Debian 比 50%・40% などへの回答）「Debianの95%以上をゴールにします。」→ 受け入れ: 連続の読み・書き（raw と UFS の file）と乱読みが、同じ条件の Debian の 95% 以上、他の機種で遅くならない。HMB は「やる」（p006 を残す）。5330 の NVMe の root は、書く時に p5 いっぱいに広げる（ユーザーのクリック）。

## 単一の到達目標

Dell Latitude 5330（Intel VMD 8086:467f の下の KIOXIA BG5 KBG50ZNS256G、DRAM-less、PCIe）で、zedBSD の NVMe の連続の読み書きが、量に依らず一定の速さで、
同じ機械の Debian の測定の 95% 以上に届く（2026-10-27 ユーザー）。今の「512 MiB を超えると 10〜3.6 MB/s に落ちる」崖を無くす。

## 今の事実（2026-10-27 の調べ、根拠は file:行）

- 測定（[ws298 p002](../ws298/phase002/phase.md):46-51、kernel f0effe7ad、`/dev/nvme0n1` を dd）: 1・16・64・256 MiB は 0〜1 秒、512 MiB は 5 秒、1024 MiB は 52 秒、
  195 GiB の位置の 256 MiB は 71 秒（約 3.6 MB/s）。遅さは位置ではなく boot からの読みの量に依る。nvme の timeout・回復の行は無い。dd の script は git に無い（`5330-run.sh` の EXTRA）。
- 経路: devfs の `block_pread_data`（`src/kern/devfs.c:1625-1716`）→ `disk_read`（`src/kern/disk.c:2416-2431`）→ buffer cache の `buf_read`・`transfer_run`
  （`src/kern/buf.c:673-731`、`2339-2480`、1 run は 4 KiB の line ×16 = 64 KiB）→ `disk_transfer_direct`（`disk.c:3923-4015`、BIO を 1 つずつ待つ）→ NVMe。
- NVMe の driver（`src/drivers/pci/pci-nvme.c`）: 1 command は 4 KiB（`NVME_IO_BOUNCE_SIZE`、:38、MDTS を読んでも :5128-5130 で 4 KiB に切る）、slot ごとの bounce buffer に
  1 つの PRP（PRP list・SGL 無し）、読みは `kern_memcpy` で写す（:4894-4897）、1 BIO の中で同時に 4 command（:47-52）、I/O queue は 1 本（depth 64）、割り込みは 1 command に 1 回。
- VMD（`src/drivers/pci/pci-vmd.c`）: 下の device に MSI-X の entry を 1 つだけ（`VMD_CHILD_VECTOR`、:80、`host_allocate_irqs` :573-617）、`vmd_interrupt`（:1318-1366）が子の handler を全部呼ぶ。
- cache の大きさ: buffer cache の上限は RAM/8（`buf.c:350-360`、`CONFIG_BUF_CACHE_KIB` は既定 0、5330 は 8 GiB で約 1 GiB）。
- 崖の仮説（p001 で確かめる。数は調べの見積もり）:
  - **H1（最有力）**: buffer の header の slab の確保 `alloc_metadata`（`buf.c:1934-1957`）が slab の list を頭から線形に、`spin_lock_irqsave` の下で歩く。読みの量の 2 乗で増え
    （256→512 MiB で約 4 倍＝1 秒→5 秒に合う）、上限に達した後は 1 line ごとに約 1.7 万 slab を歩く（約 1 ms/4 KiB ＝約 4 MB/s に合う）。割り込みも止める。
  - H2: 上限の後の line ごとの evict と、確保の失敗で 4 KiB・深さ 1 の同期の読み（`buf.c:2383-2389 → 2110`）に落ちる。
  - H3: NVMe の driver の天井（4 KiB・bounce の写し・深さ 4・1 queue・1 vector）。量に依らない上限（約 250 MB/s）で、崖の説明ではない。
  - H4: BG5 は DRAM-less で、zedBSD は HMB（Host Memory Buffer）を渡していない（BG5 は HMB に対応、Linux は 64 MiB を渡す。p001 で確かめた）。遠い LBA の読みに効くが、量に依る遅さは説明しない。
- 数える口: `vfs.bufcache.{max_bytes,current_bytes,stats}`・`vfs.io.stats`（`IO_BUF_SINGLE_MEMORY` など）・`vfs.readahead.stats`（`src/kern/sysctl.c:68-79`）。

## 現在の状態（2026-10-27 10:30、P1）

- p001（測定）: H1（`alloc_metadata` の slab の線形の walk）が崖の原因と確かめた（cache 128 MiB の kernel で崖が消える、sys ≈ real）。H2・H5 は否、H3（driver）が次の天井、H4（HMB）は連続には効かない。[phase001](phase001/phase.md)。cleared（2026-10-28 Q1。数は Debian の 95%、HMB はやる）。
- p002（直し）: header の確保を O(1) の index に。5330 の連続の読み 4 GiB が 320〜384 MB/s で一定（前は上限の後 3.3〜21）、raw の書き 597〜655 MB/s（前 4〜14）。QEMU の回帰も PASS（2026-10-27 q1617、P1）。cleared。
- p009（root を広げる）: `ufs-grow.py` で 5330 の root を 15104 MiB に。大きな file の後の reboot が止まる、もとからある不具合（`vm_object_sync_range_buffer` の 2 乗の探し）を直した。QEMU・5330 で確かめた。cleared（2026-10-28 Q1）。
- 再開点（2026-10-27 ラップアップ）: p004 の設計（[phase004](phase004/phase.md)）の design-reviewer から（範囲は 2026-10-28 Q1 が決定）。測る口で 256 KiB の command は device 約 270 µs・起床 3 µs・CPU 約 160 µs が直列と分かった（重ねる＝先読みが次の手）。

- **2026-10-28 Q1 の判定と範囲の決定**: p001・p009 は cleared。p004 の範囲は P1 の案のとおり「raw の block device の先読み（buffer cache の worker で device の待ちと CPU の仕事を重ねる）」に変える。複数の queue・VMD の vector の配り方は、起床が 3 µs で 1 本の流れに効かないので後へ（p007 で乱読みの QD を測った後に要否を決める）。UFS の file の先読み・書きの合流は p005。HMB（p006）はユーザーの「やる」のとおり残す。
- 次（q1634、P1、2026-10-28 ユーザー「P1は5330で作業します。NVMeの高速化の続きをお願いします。」）: p004 の design-reviewer → 実装 → host・QEMU・5330 → p005 → p006 → p007 → p008。
- p004（2026-10-28 P1、commit ded5ae2e9・36d0d7b5d・1e0ac531a）: raw の block device の先読み（buffer cache の worker 4 本、窓 2 MiB）と、cache が上限の時の line の付け替え・hit の 16 line ずつの写し（cache_lock の取り合いの直し）。5330 の raw の連続の読み 4 GiB が 1.68〜2.24 GB/s で平ら（中央値 1917 MB/s、p003 の 583 の 3.3 倍、Debian の b-chunk 1.1 GB/s・d-chunk 1.4〜1.6 GB/s を越える）、raw の書き 926〜1032 MB/s（回帰なし）。QEMU 4 本 PASS、build 5 config warning 0。D5（付け替え・hit の写し）は design-reviewer（重大なし）を反映し、128 MiB の cache の負荷試験（`qemu-cache-stress.sh`）PASS。review の後の 5330 の raw の書きは 1 GiB の後半で 440〜560 MB/s に落ちた（sys は同じ、device の側の待ちと見るが未確認、p006・p007）。乱読みと `5330-run.sh` の素の起動は未実施（p007）。[phase004](phase004/phase.md)。
- p005（2026-10-28 P1）: UFS の file の読み書きの道を調べた（1 MiB の読みが 64 KiB の同期の request 16 回・写し 4 回、flusher は 4 KiB ずつ、割当は 64 KiB ごと）。測定の追加と案だけ、設計は未着手。[phase005](phase005/phase.md)。
- 再開点（2026-10-28 P1）: p004 は Q1 の判定待ち。p005 は phase005 の「計画の案」の 1（UFS の file の読みの測定、Debian の ext4 の読み）から。p006（HMB）・p007・p008 は未着手。

## 範囲

- 含む: 崖の原因（buffer cache の確保・evict）、NVMe の大きな転送（MDTS・PRP list、bounce の写しを減らす）、queue の深さと数、割り込み（VMD の vector の配り方）、
  連続の読みの readahead・UFS の cluster、書き（連続の書き・fsync）、HMB（p001 で要否）。
- 含まない: NVMe・VMD の電源（APST・ASPM・D3cold は [WS300](../ws300/ws.md)）、S0ix（[WS052](../ws052/ws.md)）、他の機種の NVMe の調整（回帰しない事だけ確かめる）、file system の形式の変更。

## 制約

- HAL の API（`include/hal/hal.h`）を変えるなら差分ごとの事前承認（[Guardrail](../guardrail.md)）。DMA の確保は既存の `drv_dma_buffer` と `pmem` の口で足りる見込み（p001 で確かめる）。
- NVMe・VMD の値と手順は NVMe の仕様（公開）と Intel の資料を優先。Linux（GPL）は値と手順の参照だけ、code は写さず block ごとに Value(s)/Procedure referenced（Guardrail の出典の規則）。
- 試験: host 試験（buffer cache の確保の計算量・NVMe の PRP list の組み立て）と 5330 の実機（`5330-run.sh --nvme-kernel`、`/tmp/i915-hw.lock`、遠隔の電源は無い）。
  QEMU の nvme は回帰だけ（T に依頼）。実機の測定は 1 本の script（配置 → 起動 → 測定 → 回収 → 要約）。console・serial の log で判定しない。
- 共有の file: `pci-nvme.c`・`pci-vmd.c` は WS300 と同じ。同じ担当が WS299 → WS300 の順に持つか、Q1 が file の所有を分ける。`buf.c` は kernel の共通（全機種に効く）。

## 受け入れ（2026-10-27 10:28 ユーザー「Debianの95%以上をゴールにします。」で数を決めた）

1. 5330 で `/dev/nvme0n1` の連続の読み 4 GiB（cache の上限の 4 倍）の速さが、最初の 256 MiB と最後の 256 MiB で大きく違わない（例: 比が 0.8 以上）。
2. 連続の読み・書き（raw と UFS の file）と乱読みが、同じ 5330・同じ条件の Debian の測定（`plan/ws299/tests/bench-debian.sh`）の 95% 以上。他の機種で遅くならない。
3. 崖の原因を数で示す（仮説の確かめの記録）。直した後に `vfs.io.stats` の single の落ち込みが 0 に近い。
4. host 試験 PASS、amd64 と buffer cache を使う全 platform の build warning 0、`boot-test.sh` の回帰（QEMU、T）、5330 の素の起動（`5330-run.sh` PASS）。
5. 規約の全文の見直し。

## Phase の案と見積もり（LW、±50%）

| Phase | 内容 | 依存 | LW | 状態 |
| --- | --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調査と設計: 測定の script（5330 の Debian と zedBSD で同じ大きさ・位置、counter を前後で取る）、Debian の基準、H1〜H4 の確かめ（`CONFIG_BUF_CACHE_KIB` を小さくして崖が動くか、`alloc_metadata` の時間）、直しの順と受け入れの数、design-reviewer | WS298（済） | 4 | cleared（2026-10-28 Q1 判定。H1 を確かめ、数はユーザー「Debianの95%以上」、HMB は「やる」） |
| [p002](phase002/phase.md) | buffer cache: header の確保を O(1)（空きの list）にし、irq を止める区間を短く。evict を run 単位に。host 試験で計算量 | p001 | 4 | cleared（2026-10-27 11:41） |
| [p003](phase003/phase.md) | NVMe の大きな転送: MDTS に合わせた command、PRP list（bounce を使わず page に直接、使う所だけ bounce）、BIO の上限を上げる（`d_max_transfer_blocks`） | p001・p002 | 6 | cleared（2026-10-27、Q1 判定。raw の読み中央値 583 MB/s = p002 の後の 1.52 倍、書き 1.5 倍） |
| [p004](phase004/phase.md) | raw の block device の先読み（buffer cache の worker で device の待ちと CPU の仕事を重ねる）。複数の queue・VMD の vector は p007 の乱読みの測定の後に要否（2026-10-28 Q1） | p003 | 6 | in-progress（実装・QEMU・5330 済み、raw の読み 4 GiB 中央値 1917 MB/s。Q1 の判定待ち、q1634） |
| [p005](phase005/phase.md) | 連続の読み書き: UFS の file の readahead の窓と cluster の読み書き、書きの合流（raw の先読みは p004 へ、2026-10-28 Q1） | p002 | 4 | planning（調べと案、2026-10-28 P1） |
| p006 | HMB（DRAM-less の BG5、p001 で効果が見込める時だけ。見込めなければ取り消し） | p003 | 4 | planned |
| p007 | 5330 の測定の受け入れと回帰（QEMU の nvme・USB の起動は T、5330 は 1 本の script） | p002〜p006 | 2 | planned |
| p008 | 規約の全文の見直し | p007 | 2 | planned |
| [p009](phase009/phase.md) | 5330 の NVMe の root を p5 いっぱいに（`ufs-grow.py`、UFS の群の探しを O(1)）と、大きな file の sync の 2 乗の直し（reboot が止まった） | p002 | 2 | cleared（2026-10-28 Q1 判定: 受け入れ 3 点を QEMU・5330 で確かめた。xattr・msync の道を狙う試験は未実施のまま） |

合計 約 32 LW（p006 を取り消せば 28）。

## 判断が要る点

- 1 queue を CPU ごとの queue にする時の割り込みの配り方（VMD の vector の数、IRQ の affinity）が HAL の口で足りない時は HAL の差分の案（承認）。

## 2026-10-28 測り直しと継続（q1640、P1）

- 2026-10-28 ユーザー「P1はfstrimを使った上で、SSDの消去の影響をなるべく排除して、再度目標達成まで継続してください。」
- 解釈（Q1）: 測る前に 5330 の Debian で `fstrim -av`、書きの試験の先の p6（zedBSD の swap、1 GiB）だけ `blkdiscard`。p1〜p5 は discard しない。測定の間に休みを置き、同じ条件で Debian の基準も取り直す。
- 2026-10-28 ユーザー「P1は、UFSのパーティションが非常に小さいため、そこへの読み書きでは正確な計測ができないです。32GBくらいの空のパーティションを作って、それに対する読み書きをDebianとzedBSDで比較する必要がありそうです。」→ 約 32 GiB の測定用の区画を作り、raw と file system（Debian は ext4、zedBSD は UFS）の読み書きを同じ区画で比べる。空きが足りず ext4 を縮める（live OS の起動が要る）時はユーザーに確かめる。
