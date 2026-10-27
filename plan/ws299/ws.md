<!-- awesome-plan project=zedbsd record=ws299 -->

# WS299 5330 の NVMe の読み書きを速くする

- Master: [plan/master.md](../master.md)（2026-10-27 作成、q1615 P1 の計画）。Primary Milestone: MG004。Related: MG003（[WS298](../ws298/ws.md) VMD の driver、q1612 NVMe から起動）。
- Status: planning（2026-10-27 新設。計画だけ、実装は Queue の承認の後）
- 出典: 2026-10-27 ユーザー「5330でNVMeの最適化（速度向上）をWSを立ててやってほしいです。（あとで順番にやってほしいです）」「新規WSはまず計画を広く進めて、全体の依存関係を見ましょう。」
- 全体の依存と着手の順: [plan/agents/P1/pm-plan.md](../agents/P1/pm-plan.md)。

- 2026-10-27 09:39 ユーザー（クリック）: 受け入れの速さの数は p001 の測定の後に決める、HMB は p001 で要否を判断する。

## 単一の到達目標

Dell Latitude 5330（Intel VMD 8086:467f の下の KIOXIA BG5 KBG50ZNS256G、DRAM-less、PCIe）で、zedBSD の NVMe の連続の読み書きが、量に依らず一定の速さで、
同じ機械の Debian の測定に対して決めた割合（p001 で数を決める）に届く。今の「512 MiB を超えると 10〜3.6 MB/s に落ちる」崖を無くす。

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
  - H4: BG5 は DRAM-less で HMB（Host Memory Buffer）の対応が無い。遠い LBA の読みに効くが、量に依る遅さは説明しない。
- 数える口: `vfs.bufcache.{max_bytes,current_bytes,stats}`・`vfs.io.stats`（`IO_BUF_SINGLE_MEMORY` など）・`vfs.readahead.stats`（`src/kern/sysctl.c:68-79`）。

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

## 受け入れ（案、数は p001 で決める）

1. 5330 で `/dev/nvme0n1` の連続の読み 4 GiB（cache の上限の 4 倍）の速さが、最初の 256 MiB と最後の 256 MiB で大きく違わない（例: 比が 0.8 以上）。
2. 同じ 5330 の Debian の `dd iflag=direct bs=1M`（と fio、入っていれば）の値に対し、zedBSD の raw の読みが p001 で決めた割合（案: 50% 以上）、UFS の file の連続の読み・書きも p001 で決めた値。
3. 崖の原因を数で示す（仮説の確かめの記録）。直した後に `vfs.io.stats` の single の落ち込みが 0 に近い。
4. host 試験 PASS、amd64 と buffer cache を使う全 platform の build warning 0、`boot-test.sh` の回帰（QEMU、T）、5330 の素の起動（`5330-run.sh` PASS）。
5. 規約の全文の見直し。

## Phase の案と見積もり（LW、±50%）

| Phase | 内容 | 依存 | LW |
| --- | --- | --- | --- |
| p001 | 調査と設計: 測定の script（5330 の Debian と zedBSD で同じ大きさ・位置、counter を前後で取る）、Debian の基準、H1〜H4 の確かめ（`CONFIG_BUF_CACHE_KIB` を小さくして崖が動くか、`alloc_metadata` の時間）、直しの順と受け入れの数、design-reviewer | WS298（済） | 4 |
| p002 | buffer cache: header の確保を O(1)（空きの list）にし、irq を止める区間を短く。evict を run 単位に。host 試験で計算量 | p001 | 4 |
| p003 | NVMe の大きな転送: MDTS に合わせた command、PRP list（bounce を使わず page に直接、使う所だけ bounce）、BIO の上限を上げる（`d_max_transfer_blocks`） | p001 | 6 |
| p004 | NVMe の queue と割り込み: 深さを上げ、BIO を待たずに続ける（非同期の BIO）、queue を CPU ごとか数本、VMD の MSI-X の vector を子に配る | p003 | 6 |
| p005 | 連続の読み書き: readahead の窓（raw と UFS）、UFS の cluster の読み書き、書きの合流 | p002 | 4 |
| p006 | HMB（DRAM-less の BG5、p001 で効果が見込める時だけ。見込めなければ取り消し） | p003 | 4 |
| p007 | 5330 の測定の受け入れと回帰（QEMU の nvme・USB の起動は T、5330 は 1 本の script） | p002〜p006 | 2 |
| p008 | 規約の全文の見直し | p007 | 2 |

合計 約 32 LW（p006 を取り消せば 28）。

## 判断が要る点

- 受け入れの数（Debian に対する割合、file の速さ）: p001 の測定の後にユーザーへ。
- HMB をやるか（p001 の数で）。
- 1 queue を CPU ごとの queue にする時の割り込みの配り方（VMD の vector の数、IRQ の affinity）が HAL の口で足りない時は HAL の差分の案（承認）。
