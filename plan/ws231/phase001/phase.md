<!-- awesome-plan project=zedbsd record=ws231-p001 -->
# ws231-p001: 基本の AHCI の driver

Status: cleared（2026-10-14 Q1: T1-610（QEMU q35、main f41c0c12b）で poll の数え 0、dd の close の後の flush、poweroff の flush（sata0 +2）、throttle、BUG-304 の wr_bytes 32 MiB ちょうど・部分 sector の 3 byte が PASS。T1-609 の残りの項目（partition・EIO の回復・2 つの HBA・S0 idle・AHCI からの root・既存の boot-test）も PASS。実機は未実施。Intel の RAID mode の device ID の表は実機で未確認）
Disposition: normal
Parent: [WS231](../ws.md)
Queue: q981
依存: 無し

## 範囲

[ws.md](../ws.md) の「基本（今日）」。設計の要点を phase.md に短く書いてから実装する（Q1 が design-reviewer を通す。期限があるので実装と並行）。
- NVMe（`src/drivers/pci/pci-nvme.c`）の attach・disk の登録・bio の処理・DMA・MSI の書き方に揃える。USB storage の error の扱いも参考に。
- 既存の root の選び方（boot の root の指定、disklabel）で AHCI の disk が選べること。
- HAL の API は変えない。`plan/coding-style.md` の全文を適用。Makefile（`platform/*/vmunix.mk` の driver の一覧）に足す。

## 確かめ

- amd64 の kernel の build warning 0。host で試せる部分（FIS・command の組み立て、PRDT の分割）の host 試験。
- T1 への依頼の文: QEMU q35 の ich9-ahci に disk image を付け、boot の log でなく SSH で読み書き（dd と cmp）、partition の認識、AHCI の disk からの root の mount（できれば）、boot-test.sh の PNG。

## 設計の要点（2026-10-14 P2）

file: `include/drivers/pci/pci-ahci-protocol.h`（register の offset・bit、FIS・command header・PRDT の組み立てと IDENTIFY の解釈を、kernel に依らない inline 関数で。host 試験はこれを include する）、`include/drivers/pci/pci-ahci.h`（`drv_pci_ahci_driver_register()`・`drv_pci_ahci_probe_ports()`）、`src/drivers/pci/pci-ahci.c`。config は `CONFIG_DRIVER_PCI_AHCI`（amd64 で既定 y）、`src/kern/platform/pcat.c` で NVMe と並べて登録・probe する。

1. **attach**（PCI の列挙の中、割り込みの前）: class 01.06.01 に一致。BAR5（ABAR）を claim・map、PCI の enable の状態を保存し memory を有効にする。GHC.AE=1、GHC.IE=0。CAP・PI・CAP2 を読み、command slot の数 = min(CAP.NCS+1, 32)。MSI を 1 本（無ければ INTx）確保して handler を登録。重い処理（handoff・COMRESET・IDENTIFY）は時計と sched_yield が使える probe に回す（NVMe の namespace の probe と同じ理由）。
2. **probe**（`drv_pci_ahci_probe_ports()`、pcat.c の USB の 5 秒待ちの後、NVMe の probe の隣。理由は NVMe と同じで、AHCI の disk が USB の root の発見を終わったように見せない）: CAP2.BOH なら BIOS/OS handoff（OOS=1、BOS=0 を 25 ms＋BB なら 2 s まで待つ）。PI の各 port で: ST・FRE を落として CR・FR が消えるのを 500 ms まで待つ → port の DMA の memory（command list 1 KiB、受信 FIS 256 B、command table を slot 数ぶん 1 つの coherent buffer に 384 B ずつ（128 B 境界）、bounce 64 KiB）を確保して PxCLB・PxFB に書く → FRE=1 → COMRESET（PxSCTL.DET=1 を 2 tick 以上、DET=0、PxSSTS.DET=3 を 1 s まで待つ）→ PxSERR・PxIS を消す → PxTFD の BSY・DRQ が消えるのを 10 s まで待つ → PxSIG が ATA（0x00000101）でなければ（ATAPI・port multiplier）使わない → ST=1、PxIE を有効、GHC.IE=1 → IDENTIFY DEVICE（PIO in、DMA で bounce へ）。
3. **IDENTIFY の解釈**: word 83 bit 10（LBA48）が無い disk は今は使わない（READ/WRITE DMA EXT だけを使うため）。容量 word 100〜103、論理 sector の大きさ word 106（bit 14=1・bit 15=0 かつ bit 12）なら word 117〜118 の word 数×2、他は 512。512〜64 KiB の 2 の冪だけ受ける。FLUSH CACHE EXT は word 83 bit 13、無ければ FLUSH を何もせず成功にしない（EOPNOTSUPP を返す、ただし write cache が無効（word 85 bit 5=0）なら成功）。model（word 27〜46、byte を入れ替え）を log に出す。
4. **disk の層**: `disk_alloc` → `disk_alloc_sd_name`（PC/AT の IDE と同じく sdN）→ block の大きさ・数、`d_max_transfer_blocks = 64 KiB / block`、`d_flags = 0`（NVMe と同じ、DISK_FLUSH_PROOF は付けない）、stats の種類は ATA の disk として `DISK_STATS_KIND_IDE`（SATA の種類を足すと uapi の変更になるので今はしない）、`ioctl` は GET_GEOMETRY（IDENTIFY の CHS、無ければ 255/63）→ `disk_create`。
5. **I/O**（`submit` は NVMe・PC/AT の IDE と同じく同期で `bio_complete` まで行う）: port ごとの mutex で command を 1 つずつ出す（NCQ でない command は libata と同じく port に 1 つだけ。32 slot の command table は用意し、NCQ（後）で並列にする）。64 KiB ごとに: write なら bounce へ複写 → H2D Register FIS（C=1、READ DMA EXT 0x25 / WRITE DMA EXT 0x35 / FLUSH CACHE EXT 0xEA / IDENTIFY 0xEC、LBA48、device=0x40）→ command header（CFL=5、W、PRDTL、PRDBC=0、CTBA）→ PRDT（bounce の連続した領域を 4 MiB ごとに 1 entry、DBC=bytes−1、最後の entry に I）→ PxCI の bit を立てる → 完了を待つ → read なら bounce から複写。
6. **完了と割り込み**: port の spinlock の下の `ahci_port_service_locked()` が PxIS を読んで消し、事象を port に貯め、PxCI の bit が消えたら完了、TFES・HBFS・HBDS・IFS・(PxTFD.ERR) なら失敗とする。handler は GHC.IS の port ごとにこれを呼んで GHC.IS を消し、waitq を起こす。待つ側は NVMe と同じく、idle の thread（起動の早い VFS の発見）なら lock を外して sched_yield しながら自分で service を呼び、普通の thread なら deadline 付きの waitq_sleep の後に service を呼ぶ（割り込みが来なくても進む）。timeout は 30 s（回転する disk の再試行を見込む）。
7. **回復**: 失敗・timeout の後は port を作り直す（ST=0、CR が消えるのを待つ、PxSERR・PxIS を消す、BSY・DRQ が残れば COMRESET、ST=1）。timeout と HBA・link の失敗は 2 回まで同じ request をやり直す（read・同じ data の write・flush は繰り返して安全）。device が ERR を返した時（TFES、media の error）はやり直さず EIO。
8. **detach・shutdown**: detach は disk を出している間は EBUSY（hotplug は後）。shutdown は GHC.IE=0 と各 port の ST・FRE を落として DMA を止めるだけ（cache の flush は電源断の前の sync が disk の層の BIO_FLUSH で出す）。suspend/resume・NCQ・TRIM・ALPM・ATAPI・hotplug は後（ws.md）。
9. **DMA の address**: PC/AT の PCI の DMA は 32 bit の device なので全部 4 GiB 未満。上位の register（CLBU・FBU・CTBAU・DBAU）にも上位 32 bit を書き、CAP.S64A が無いのに 4 GiB 以上の address が来たら attach を断る。
## review-1 とユーザーの決定（2026-10-14）

[review-1](review-1.md)（致命的 0、重大 R1〜R4、中 1〜9、軽微 1〜10）。ユーザーの決定（クリック）:
- U1（R4: suspend の hook が無いと S0 idle が全体で失敗）:「既定を n にする」→ CONFIG_DRIVER_PCI_AHCI の既定は n。試験の config でだけ y。suspend・resume は後。
- U2:「Intel の RAID mode にも bind」→ class 01.06.01 に加え、Intel の RAID mode（class 01.04.00）の controller を Intel の device ID の表で AHCI として bind（Linux と同じ）。RST が NVMe を AHCI の BAR に remap している機種は対象外（記録だけ）。
- U3・U4: 推しどおり（shutdown では FLUSH CACHE（EXT）だけ、STANDBY IMMEDIATE は出さない。hw.diskstats の「IDE」は受け入れ、UAPI を増やさない）。
技術判断（Q1）: R1〜R3 と中 1〜9、軽微は review の直し方の案どおり。試験の案 (a)〜(f) を T1 の依頼に入れる。

- 2026-10-14 ユーザー（U1 の変更）:「最小の suspend・resumeだけでいいんじゃないですか？linkの省電力は割と、相性問題がひどいです」→ 最小の suspend・resume を入れ、既定を y に戻す（18 時までに T1 の QEMU の試験が PASS しなければ n）。ALPM・DevSleep などの link の省電力は入れない（相性の問題）。

## 実装（2026-10-14 P2、review-1 と U1〜U4、suspend の決定を反映）

commit: b89e52067（初版）→ 28301c8f4（review-1 の反映と suspend）→ 8919876ec（FRE の再確認）。上の「設計の要点」との違い:
- **R1**: attach は NVMe と同じ順（enable の状態の保存 → MSI の mask → I/O・memory・master を落とす → ABAR の map → memory だけ戻し master=0 を確かめる）。probe で全 port を ST→CR、FRE→FR の 2 段で止め、自分の CLB・FB を書いてから BME を立てる。止まらない port があれば GHC.HR を 1 回試し、それでも止まらなければ BME を立てずにその HBA を使わない。
- **R2**: port ごとに command の状態（IDLE・ACTIVE・DONE・DEVICE_ERROR・LINK_ERROR・ABORTED、`drv_ahci_command_advance`・`drv_ahci_command_expire`、protocol header）を state lock の下で進める。timeout は lock の下で最後に PxIS・PxCI・PxTFD を読み直してから ABORTED にし、その後で ST=0。ACTIVE 以外は CI の消滅で完了にならない。発行の前に残りの status を吸い取る。
- **R3**: recovery で CR が消えない・COMRESET で戻らない port は quarantine（PxIE=0、以後 ENXIO、DMA の memory は解放しない、`disk_media_revoke`）。
- **R4/U1 → suspend の決定**: 最小の suspend・resume を入れた（`ahci_suspend`・`ahci_resume`）。suspend は全 port の command mutex を取って（走っている command を待ち、新しい request を止める）、FLUSH CACHE (EXT)、GHC.IE=0、各 port を ST→CR、FRE→FR、BME を落とす。失敗したら resume の手順で戻して error を返す。resume は AE、CAP・CAP2・PI の書き戻し（Linux と同じ）、CLB・FB の書き直し、BME、全 port 一括の COMRESET、ATA の disk が戻った port だけ ST=1（戻らない disk は退役と `disk_media_revoke`）、GHC.IE=1、mutex を返す。ALPM・DevSleep は入れない（ALPE・ASP・ADSE は落とす）。既定は y（Makefile は amd64 で y、`config/ci/config-amd64.mk` の `CONFIG_DRIVER_PCI_AHCI := y` の 1 行で切り替え）。
- **中 1〜9**: 停止は 2 段。COMRESET は全 port に一括（0x301 → 2 ms → 0x300、SPD は保つ）、DET=0 が 100 ms 続けば空、DET=1 は 1 s まで、BSY・DRQ も全 port 一括で 10 s。attach で全 PI の port の記録と lock を作り、PxIE=0、PxSERR→PxIS→IS を消し、CCC を切る。GHC.IE は全 port の設定の後に 1 回。PxIE は DHRS・PSS・DSS・SDBS・UFS・DPS・OFS・INFS・IFS・HBDS・HBFS・TFES（PCS・PRCS は外す、PRCS は poll で見えれば link の失敗）。SUD・POD・ICC=Active、ALPE・ASP を落とす、FBS.EN・DEVSLP.ADSE を落とす。待ちは 100 ms ごとに自分で port を見て、割り込みが来ずに poll で完了を見つけた回数を `ahci%u: port %u: completion found by polling (n)` に出す（1、2、4、8…回目）。shutdown は FLUSH CACHE (EXT) だけ（trylock、5 s、U3）の後に port を止め、PCI の enable を戻す。PxSIG は上位 16 bit で分類。空・ATA 以外・失敗の port は停止して記憶域は残す（CLB・FB が指したまま）。bounce は ATA の disk の port だけ。
- **軽微**: S64A の判定は probe の port ごと。FLUSH CACHE EXT が無ければ FLUSH CACHE（0xE7）。recovery の COMRESET と resume で `disk_persistence_forget`。PxCI の前に write barrier、完了の後に read barrier。PRDBC が長さと違えば link の失敗として扱う（再試行あり）。IDENTIFY と shutdown・suspend の flush は 5 s、再試行なし。command table は slot 0 の 1 つだけ（NCQ まで 32 slot は使わない）。
- **U2**: class 01.04.00 の Intel の RAID mode を device ID の表（0x2822・0x282a・6〜9 series・0xa105〜0xa10f・0x9d05・0x9d07・0x02d7・0x06d6）で bind。表は Linux の ahci.c の記憶で、実機の ID は未確認（5330・5320 の `lspci -nn` で確かめる）。RST が NVMe を ABAR に remap している時は数を log に出すだけ。attach で VS≥1.0 と PI≠0 を確かめる。
- **U4**: hw.diskstats は IDE。

確かめ（P2、host）:
- `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p2-ahci build/p2-ahci/vmunix -j16`: exit 0、warning 0、`amd64 vmunix check: PASS`（8919876ec の時点）。
- `plan/ws231/tests/run-ahci-protocol-test.sh`: `-std=c89 -pedantic` と ASan・UBSan で PASS。中身: FIS（READ/WRITE DMA EXT・FLUSH・IDENTIFY、48 bit の端、拒否）、command header、PRDT（64 KiB・9 MiB の分割・奇数の拒否）、IDENTIFY（LBA48・4Kn・512e・word 106 の無効・奇数の大きさ・LBA48 無し・容量 0）、完了の分類、chunk、状態遷移（timeout と完了の競合、ST=0 の後の CI=0 が ABORTED のまま、error が残る）、PxSIG の分類。
- QEMU・実機は未実施（T1 に依頼する、下）。

残り: NCQ・TRIM・ALPM・DevSleep・hotplug・ATAPI・port multiplier（ws.md の「後」）。Intel の RAID の ID 表の実機の確認。`plan/coding-style.md` の全文の見直しは WS の規約の Phase で。

## T1 への依頼の文（ws231-p001）

image: branch agent/p2-ws215 の 8919876ec 以降（Q1 の merge の後は main）。`plan/tools/guest/test-image.sh plan/ws231/tests/config-amd64-ahci.mk build/ws231-ahci`（SSH guest に sleepctl、AHCI は y）。QEMU は q35（ich9-ahci が 00:1f.2 に常にある）。boot は guest.py の既定（NVMe）。判定は SSH と QMP と boot-test.sh の PNG だけ（console・serial の log は読まない）。kernel の log は SSH の `dmesg` で読む。

1. **基本の読み書き**: 64 MiB の raw image を `-drive if=none,id=sata0,file=<img>,format=raw -device ide-hd,bus=ide.0,drive=sata0` で付けて起動。`dmesg | grep ahci` に `ahci0: ... ports=0000003f ... MSI`、`ahci0: port 0: QEMU HARDDISK sectors=00000000:00020000 sector-size=512 write-cache`、`/dev/sdN` があること。`dd if=/dev/urandom of=/tmp/a bs=1M count=32; dd if=/tmp/a of=/dev/sdN bs=64k; sync; dd if=/dev/sdN of=/tmp/b bs=64k count=512; cmp /tmp/a /tmp/b` が一致。`completion found by polling` が 0 行。合否: 一致し、polling の行が無い。
2. **(a) flush と経路**: 1 の前後で QMP `query-blockstats` の sata0 の rd_bytes・wr_bytes・flush_operations を取る。wr_bytes が 32 MiB 以上増え、flush_operations が sync で増えること。`poweroff` の後にも flush_operations が増えること（shutdown の FLUSH）。
3. **partition**: host で image に MBR か GPT の partition を 2 つ作って付ける（中身は空でよい）。`ls /dev/sdN*` に partition が 2 つ出ること。
4. **(b) error の回復**: `-drive driver=blkdebug,config=<cfg>,image.filename=<img>,id=sata0` で read の EIO を 1 回（`[inject-error] event = "read_aio" errno = "5" once = "on"`）。対象の dd が EIO で失敗し、直後の dd が成功すること。`dmesg` に `device error` か `link error` の行があり `quarantined` が無いこと。write でも同じ（`write_aio`）。
5. **(c) 遅延**: QMP `block_set_io_throttle`（例 bps=2097152）で遅くして 1 の dd。30 s 未満の遅れで timeout にならず一致し、polling の行が 0 行のこと。
6. **(d) 複数と boot 時間**: (i) disk 無しの q35（ich9-ahci の 6 port が空）、(ii) `-device ahci,id=ahci1` を足して 2 つ目の HBA の port 0・1 に disk、の 2 構成で boot-test.sh の PNG と、AHCI 無しの kernel（`CONFIG_DRIVER_PCI_AHCI := n` で同じ config）との boot から login までの時間の差（目安 +0.3 s 以内）。(ii) で `ahci1` の disk が 2 つ出て、1 の dd・cmp が両方で通ること。
7. **(g) S0 idle**: ws052 の `plan/ws052/tests/p004-guest.sh roundtrip` と同じ手順（sleepctl devices）を AHCI の disk を付けた状態で。`sleep result=0 resume=0 device=-` が出て、`dmesg` に `ahci0: suspended`・`ahci0: resumed` があり、resume の後に 1 の dd・cmp が通ること。q35 の ich9-ahci が ENOTSUP の原因にならないこと。
8. **AHCI からの root（できれば）**: guest.py に `--disk sata`（`-device ide-hd,bus=ide.0,drive=boot,bootindex=1`）が無ければ `--qemu-extra` で boot disk を ich9-ahci に付け、NVMe 無しで login まで（boot-test.sh の PNG）。guest.py の変更が要るなら T1・Q1 の判断。
9. **既存の回帰**: q35 の既定の boot-test（uefi-nvme、uefi-usb）が遅れずに通ること（AHCI の bind で S0 idle・boot が壊れないこと）。

結果（PASS/FAIL、PNG、dmesg の抜き出しの場所）を P2 と Q1 に。実機（5330・5320 の SATA）は別。

## 2026-10-14 T1-609（QEMU q35、tree a25b73a3f）

- PASS: dd+cmp、partition、blkdebug の EIO と回復、2 つの HBA、空の port の boot 時間の差 0、S0 idle、AHCI からの root、既存の q35 の boot-test。
- FAIL: (1) `completion found by polling` が boot の直後から（32 MiB の後 2048）、(2) sync・poweroff で QMP の flush_operations が増えない、(3) wr_bytes が 8〜40 倍。P2 が直す。成果物は T1 の worktree の build/review/t1-609/。

### T1-609 の FAIL の解析と直し（2026-10-14 P2、commit 4a46d836c、QEMU は使わず source と T1 の成果物から）

- **(1) `completion found by polling` は数え方の誤り（割込みは動いている）**: 旧い判定は「待つ側が最初に見た時に `interrupt_status` が 0」を polling と数えた。QEMU では command の多くが PxCI の書き込みから待つ側の最初の PxIS の読みまでの間に終わる（KVM の MMIO exit の間に QEMU の AIO が完了）。待つ側が state lock（割込み禁止）の下で PxIS を読んで消すと、その後に届く MSI の handler は IS=0 を見て何もせず、完了は「polling」と数えられた。証拠: (a) 32 MiB の dd は 512 B の sector ごとの 4 KiB の write（下の (3)）で 65536 個の command、polling の数は 2048〜4095（3〜6%）。(b) 割込みが本当に無ければ各 command が 100 ms の poll を待ち、65536 個で 1.8 時間、throttle の 16384 個で 27 分かかる。実際は dd が通常の時間で終わり、throttle は 33 s（= 64 MiB ÷ 2 MiB/s、(3) の 8 倍と一致）。直し: poll の間隔の sleep が wake（handler の waitq_wake）でなく時間切れで終わり、その後に完了を見つけた時だけ数える（`drv_ahci_completion_by_polling`、protocol header、host 試験 `test_polling`）。最初の一目で終わっていた command は数えない。
- **(2) flush_operations が増えない**: (a) guest の `sync` は `mount_sync_all()`（mount された volume だけ、`src/kern/syscall.c` の KERN_SYS_sync）で、raw の /dev/sdN には BIO_FLUSH を出さない（NVMe も同じ、generic の動作。Linux の sync も raw の block device に cache の flush を出さない）。AHCI の driver の誤りではない。直し（driver）: disk の close の hook（`ahci_disk_close`）で、前の flush の後に write があれば FLUSH CACHE (EXT) を出す（FreeBSD の ada(4) と同じ）。dirty は write の発行で立ち、成功した flush で消える（`drv_ahci_cache_dirty_after`、host 試験 `test_cache`）。これで `dd of=/dev/sdN` の終わり（close）で flush_operations が 1 増える。partition（/dev/sdNsM）の close は disk の層が driver の close を呼ばないので flush しない（mount の sync・shutdown・suspend で出る）。(b) poweroff で増えない: `ahci_shutdown` の FLUSH は `src/kern/shutdown.c` の `drv_pci_shutdown()` の中で、157 行の `network_stopped` が偽（`net_shutdown_for_boot()` が 10 s で止まらない）なら PCI の shutdown 全体が飛ぶ。driver の中には flush を出さない理由が見つからない（trylock の規約・`port->disk`・flush_ext は正しい、suspend では flush が 1 回届いている）。どちらかは T1 の gdbstub で確かめる（下の再試験 2）。generic の shutdown の順序（disk の flush を network の停止の前に出す等）は ws231 の範囲外なので Q1 に提案。
- **(3) wr_bytes の 8 倍は generic の devfs の raw write の作り**: `src/kern/devfs.c` の `block_pwrite_data()` は 1 sector（512 B）ごとに `disk_write(disk, block, 1, …)` を呼び、`buf_write_context()` は write-through の disk で 4 KiB の line を単位に書くので、512 B ごとに line 全体（4 KiB）を `writeback_line_whole()` で書く。32 MiB → 65536 回 × 4 KiB = 256 MiB（stats3 の 268435456 と一致）。最初の部分 write の line の読み込みが rd の 32 MiB。AHCI の PRDT・sector count・bounce は正しい（host 試験と、dd の cmp の一致、throttle の時間が 8 倍の量と一致）。NVMe の /dev/nvme0n1 でも同じになる。直し（範囲外、Q1 に提案）: `block_pwrite_data`・`block_pread_data` で、揃った完全な sector の並びを bounce（4 KiB）に詰めて複数の sector で `disk_write` を呼ぶ（line が full になり読み込み無しで 1 回の write）。8 MiB で ~336 MiB（40 倍）は同じ VM の前の書き込みを含んだ累計と見る（未確認）。
- 確かめ（P2、host）: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p2-ahci build/p2-ahci/vmunix -j16` rc 0、warning 0、`amd64 vmunix check: PASS`。`plan/ws231/tests/run-ahci-protocol-test.sh` PASS（`-std=c89 -pedantic`、ASan・UBSan）。`git diff --check` 無し。QEMU は未実施（T1 へ、下）。

### T1 への再試験の依頼の文（T1-609 の 1・2・3 と throttle、ws231-p001）

image: branch agent/p2-ws215 の 4a46d836c 以降（Q1 の merge の後は main）、`plan/tools/guest/test-image.sh plan/ws231/tests/config-amd64-ahci.mk build/ws231-ahci`。QEMU は T1-609 と同じ（q35、64 MiB の raw を `-device ide-hd,bus=ide.0,drive=sata0`）。判定は SSH・QMP・gdbstub だけ。

1. **polling の数**: boot の後と、`dd if=/dev/urandom of=/tmp/a bs=1M count=32; dd if=/tmp/a of=/dev/sda bs=64k; dd if=/dev/sda of=/tmp/b bs=64k count=512; cmp /tmp/a /tmp/b` の後で `dmesg | grep -c 'completion found by polling'`。合否: cmp が一致し、polling の行が 0（QEMU の割込みが届く限り 0 のはず。1 行以上なら、その時の `dmesg | grep ahci` を付けて FAIL）。
2. **flush**: QMP `query-blockstats` の sata0 の flush_operations を (i) 起動の後、(ii) 上の書き込みの `dd of=/dev/sda` が終わった後、(iii) `sync` の後、(iv) `poweroff` の後（`-action shutdown=pause` か `-no-shutdown`）で取る。合否: (ii) が (i) より 1 以上多い（close の flush）。(iii) は増えなくてよい（raw の sync は flush を出さない、generic）。(iv) は増えることを期待するが、増えなければ FAIL とせず次を記録: gdbstub（`-gdb tcp::<port>`、`vmunix` の symbol）で `break ahci_shutdown`・`break drv_pci_shutdown`・`break net_shutdown_for_boot` を置いて poweroff し、どれに止まったか、`net_shutdown_for_boot` の戻り値（`finish` の後の `$rax`）。ahci_shutdown に止まったなら `break ahci_port_flush` にも止まるか。
3. **wr_bytes**: 1 の書き込みの dd の前後の wr_bytes の差を記録（直しは generic の devfs で範囲外なので、32 MiB で ~256 MiB でも AHCI の FAIL にしない）。比較のため同じ dd を NVMe の空きの disk（`-drive if=none,id=nv1,file=<64 MiB>,format=raw -device nvme,serial=nv1,drive=nv1`、/dev/nvme1n1）にも行い、その wr_bytes の差も記録（同じ 8 倍なら generic の確認）。
4. **throttle と polling**: `block_set_io_throttle`（device sata0、bps=2097152、他は 0）で `dd if=/tmp/a of=/dev/sda bs=64k count=128` と `dd if=/dev/sda of=/tmp/c bs=64k count=128`（8 MiB）、時間を記録し、cmp が一致、`completion found by polling` が 0 行、timeout・link error の行が無いこと。

結果（PASS/FAIL、QMP の値、gdbstub の記録）を P2 と Q1 に。
