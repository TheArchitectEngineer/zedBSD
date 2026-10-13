# ws231-p001 review-1（設計 f7e417e36 の敵対的 review、2026-10-14、Q1 が起動した design-reviewer）

ws231-p001（基本の AHCI driver）の設計を敵対的にレビューしました。対象は `git -C /home/awe/zedBSD-worktrees/p2 show f7e417e36:plan/ws231/phase001/phase.md` の「設計の要点」1〜9 と /home/awe/zedBSD-claude1/plan/ws231/ws.md です。

結論として、致命的な誤りは見つかりませんでした。重大が 4 件、中が 9 件、軽微が 10 件、ユーザーの判断が要る点が 4 件あります。実装の前に設計へ書き足すべきなのは、重大の 4 件と中の 1・2・3・4 です。

- source と plan は変更していません。build・QEMU も実行していません。
- AHCI 1.3.1・ATA・QEMU についての記述は記憶によるもので、そう書いた所は推測です。
- 既存コードの記述は、すべて main の checkout（/home/awe/zedBSD-claude1）で確かめました。

## 重大

### R1. bus master（BME）の扱いが書かれておらず、firmware が残した DMA を止める順も無い
- 根拠:
  - 設計 1 は「PCI の enable の状態を保存し memory を有効にする」だけで、bus master に触れていません。
  - NVMe は attach で I/O・memory・master を落とします（src/drivers/pci/pci-nvme.c:1583 の MSI mask、1589 の `nvme_pci_quiesce`）。memory を有効にした後は master が 0 であることを確かめ（1640）、自分の queue を書いてから master を立てます（5444 `drv_pci_device_set_bus_master(…, true)`）。
- 起こる状況:
  - (a) firmware が BME を落としていると、DMA が全部失敗します（HBFS か timeout）。
  - (b) firmware の AHCI driver（UEFI の AtaAtapiPassThru、または OVMF）が ST・FRE=1 のまま port を残すと、PxFB は firmware の memory（ExitBootServices の後に kernel が再利用するページ）を指したままです。BME=1 のままなら、attach から probe まで（USB の 5 秒待ち＋α）の間に、link の事象で HBA が D2H FIS をそこへ書き、memory を壊せます。(b) は今も潜在している問題ですが、driver を足すなら塞ぐべきです。
  - QEMU では BME が立っていれば通るので、試験では見つかりません。
- 直し方:
  - attach を NVMe と同じ順にします。enable 状態の保存 → MSI の mask → `drv_pci_device_config_write16` で I/O・memory・master を落とす → BAR の map → memory だけ有効にする（master が 0 であることを確かめる）。
  - probe では、全 PI の port を停止 → 自分の CLB・FB を書く → `drv_pci_device_set_bus_master(true)` → FRE=1、の順にします。
  - detach・shutdown では `drv_pci_device_restore_enable_state` で元に戻します。

### R2. 「PxCI の bit が消えたら完了」と recovery が PxCI を消すことが衝突し、偽の成功になる
- 根拠: 設計 6 は PxCI の bit が消えたら完了、設計 7 は失敗・timeout の後に ST=0 で port を作り直します。AHCI では ST を 1→0 にすると HBA が PxCI と PxSACT を消します。
- 起こる状況:
  - timeout の後、待つ側が recovery で ST=0 にすると PxCI の bit が消えます。並行する割り込み handler（または自分の service の呼び出し）がそれを見て、TFES 等も無いので「成功」と記録します。
  - read なら bounce の古い内容を成功として返し、write なら書かれていないのに成功になります。
  - timeout と完了がほぼ同時のときの判定も、設計に書かれていません。
- 直し方:
  - command に状態 ACTIVE → DONE / ERROR / ABORTED を持たせ、port の spinlock の下で遷移させます。
  - timeout と決める前に、lock の下で PxCI と PxIS をもう一度読みます（間に合った完了は成功にする）。
  - ABORTED にしてから ST=0 にします。service は ACTIVE の command 以外で、CI の消滅を完了として扱いません。
  - host 試験に、この状態遷移（timeout と完了の競合、ST=0 の後の CI=0）を入れます。

### R3. HBA が止まらないとき（CR・FR が消えない）の DMA の安全が無い
- 根拠: 設計 7 は「ST=0、CR が消えるのを待つ…」で、待ちが失敗したときの経路がありません。NVMe はこの場合を quarantine にし、DMA を解放しません（pci-nvme.c の `nvme_io_quarantine(…, dma_unsafe)`、`nvme_cleanup` の失敗で quarantined）。
- 起こる状況: CR が 500 ms で消えない HBA が、なお bounce や command table へ DMA しているのに、再送で bounce を再利用したり解放したりします。HBA が後から書けば memory が壊れます。
- 直し方:
  - CR・FR が消えなければ、その port を「DMA 不安全」の quarantine にします。以後の I/O は EIO、DMA の memory は解放しない（leak を許す）、BME を落とせるなら落とします。
  - 2 回目の手段として、GHC.HR（HBA reset）の採否を決めます。
  - disk は `disk_media_revoke` か `disk_media_gone`（include/kern/disk.h）で閉じます。

### R4. suspend の hook が無いと、AHCI が bind した機械では S0 idle 全体が失敗する
- 根拠:
  - src/drivers/pci/pci-power.c:21「A bound driver without a suspend cannot be suspended and fails the same way」と、799-800 の `driver->suspend == NULL` で ENOTSUP。
  - 呼び出し元は src/kern/sleep.c:308 と src/drivers/generic/system-device.c:352。
  - 設計 8 と ws.md は suspend/resume を「後」に回しています。
- 起こる状況:
  - QEMU の q35 は ich9-ahci（00:1f.2）を常に持つため、amd64 の既定を y にすると、disk の有無に関係なく q35 の全 VM で S0 idle が ENOTSUP になります。
  - SATA controller が AHCI mode の laptop でも同じです（5330 に有るかどうかは未確認で、推測です）。
  - 既存の回帰（boot-test.sh:144 の `-machine q35`）は suspend を試さないので、気づけません。
- 直し方（どれかを選ぶ）:
  - (a) 最小の suspend・resume を入れる。suspend は新しい I/O を止めて idle を待ち、FLUSH CACHE EXT、各 port の ST→FRE を落とし、GHC.IE=0 にする。resume は AE・CLB・FB を書き直し、COMRESET、ST=1。
  - (b) 既定を n にして、試験の config でだけ y にする。
  - (c) ws052 の S0 idle がベータ2 の対象機で使われないことを確かめて、既知の制限として受け入れる。
- どれを選ぶかはユーザーの判断です（下の U1）。

## 中

1. **ST と FRE を同時に落としている（仕様違反）**
   - 根拠: 設計 2「ST・FRE を落として CR・FR が消えるのを 500 ms まで待つ」。仕様（記憶）では、ST・CR が 1 の間に FRE を消してはいけません。正しくは ST=0 → CR=0 を待つ（500 ms）→ FRE=0 → FR=0 を待つ（500 ms）です。
   - 起こる状況: QEMU は寛容なので通りますが、実機では FR が消えない、FIS の書き込みが中途で止まる、などの未定義の動作になります。設計 7 と 8 の停止も同じです。
   - 直し方: 2 段に分けて書きます。

2. **空の port ごとに 1 s 待つので、q35 の全 boot が約 6 s 遅れる**
   - 根拠: 設計 2 は COMRESET の後に「PxSSTS.DET=3 を 1 s まで待つ」を port ごとに直列で行います。ich9-ahci は PI=0x3f の 6 port です（記憶）。probe は src/kern/platform/pcat.c:422-438 の USB の 5 秒待ちの後、NVMe の probe の前に同期で走ります。
   - 起こる状況: 全ての q35 の回帰と、空き port のある実機で boot が遅れ、NVMe の発見も遅れます。
   - 直し方: COMRESET を全 port に出してからまとめて待ちます。DET=0 が約 50〜100 ms 続けば「無し」とします（Linux の debounce と同じ考え方、記憶）。DET=1 なら最大 1 s 待ちます。

3. **firmware が残した PxIE・PxIS・IS と、port の状態の初期化の順**
   - 根拠: 設計 1 では attach で handler を登録して GHC.IE=0、設計 2 では port ごとに「ST=1、PxIE を有効、GHC.IE=1」とします。この順だと、まだ初期化していない port の PxIE・PxIS が firmware の値のまま、GHC.IE=1 になります。
   - 起こる状況: 未初期化の port で割り込みが起き、handler が未初期化の port の構造（lock）に触れます。INTx なら level の storm になります。
   - 直し方:
     - attach で、全 port の構造と lock を初期化します。PI の全 port で PxIE=0 にし、PxSERR→PxIS→IS の順に消します。
     - handler は、未開始の port でも PxIS を消すだけにします。
     - GHC.IE=1 は全 port を設定した後に 1 回だけ立てます。

4. **PCS と PRCS は PxIS では消えないため、storm になる**
   - 根拠: 設計 6 の service は PxIS を消すだけです。PxIS.PCS は PxSERR.DIAG.X を、PRCS は PxSERR.DIAG.N を映しており、PxSERR を消さないと残ります（仕様、記憶）。
   - 起こる状況: PxIE に PCS・PRCS を入れると、link の事象（ALPM・hotplug・ケーブル）で INTx なら止まらない割り込み、MSI でも繰り返しになります。
   - 直し方: hotplug は後なので、PxIE からは PCS・PRCS を外します。PxIE に入れる bit を設計に列挙します（DHRS、PSS、DPS、SDBS、TFES、HBFS、HBDS、IFS、INFS、OFS、UFS）。

5. **staggered spin-up・POD・ICC・ALPM の扱いが無い**
   - 根拠: 設計 2 に PxCMD.SUD、POD、ICC と、PxSCTL.IPM の記述がありません。
   - 起こる状況: CAP.SSS=1 で SUD=0 の port は OOB をせず、DET=0 のままになり disk が見えません（Intel の mobile PCH で起こり得る、推測）。firmware が ALPE・ASP・DevSleep を有効にしていると、driver の知らない link 省電力が動きます。
   - 直し方:
     - CAP.SSS なら SUD=1、CPD なら POD=1、ICC=Active にします。
     - COMRESET は SPD を保ったまま IPM=3 で書きます（0x301、次に 0x300）。
     - ALPE・ASP を落とします。FBS.EN と CCC_CTL.EN も落とします。

6. **割り込みが失われると、各 I/O が 30 s かかり、試験でも見つからない**
   - 根拠: 設計 6 は「deadline 付きの waitq_sleep の後に service を呼ぶ」で、timeout は 30 s です。NVMe の待ち（pci-nvme.c:4050-4085）も同じ形です。MSI で IS を消す時機と PxIS の再発生の扱いには機種差があります（推測）。
   - 起こる状況: MSI が来ない・IS の消し方が誤っている、という不具合が、dd と cmp では「遅いが成功」として通ります。
   - 直し方:
     - 待ちを短い間隔（例 100 ms）で繰り返して service を呼び、全体は 30 s で切ります。
     - 「poll で完了を見つけた回数」を数えて log に出し、T1 の合否に使います。

7. **shutdown で FLUSH をしていない**
   - 根拠: 設計 8 は「cache の flush は disk の層の BIO_FLUSH で出す」とします。しかし src/kern/shutdown.c:116 の `mount_sync_all()` が flush するのは mount された volume だけです。157 の `if (network_stopped)` が偽なら `drv_pci_shutdown` 自体が飛ばされます。NVMe は CC.SHN の normal shutdown（cache の書き戻しを含む）を自分で行います。
   - 起こる状況: raw device（試験の dd）や mount していない disk の write cache が、電源断で失われ得ます。HDD では emergency の head 退避も起こります。
   - 直し方: shutdown hook で port が使えるなら FLUSH CACHE EXT を出します。STANDBY IMMEDIATE も出すかどうかを判断します。

8. **PxSIG を 32 bit 全体で 0x00000101 と比べている**
   - 根拠: 設計 2。
   - 起こる状況: sector count や LBA low が 1 でない ATA の device を捨てます（推測。Linux・FreeBSD は上位 16 bit で分類している、記憶）。
   - 直し方: `sig >> 16` で分類します。0x0000 は ATA、0xEB14 は ATAPI、0x9669 は PM、0xC33C は SEMB です。

9. **IDENTIFY・probe の失敗と、空の port の DMA の後始末が無い**
   - 根拠: NVMe は probe の失敗で `nvme_cleanup` と quarantine を行います（pci-nvme.c:540-575）。設計には、port ごとの失敗の後始末（FRE を落として FR=0 を待ってから DMA を解放する）が書かれていません。
   - 直し方:
     - 空の port に bounce は確保しません。CLB・FB を残すか、FRE→FR を待ってから解放します。
     - controller の registry と probe の claim を、NVMe の `probe_started`・`detach_busy` と同じ形にします。

## 軽微

1. 設計 9「CAP.S64A が無いのに 4 GiB 以上の address が来たら attach を断る」: DMA の確保は probe にあるので、時機が合いません。src/drivers/pci/pci-pcat.c:93-97 は address_bits=32、coherent=1 なので実害はありません。「probe で port を断る」と書き直します。
2. FLUSH CACHE EXT が無い disk には、FLUSH CACHE（0xE7、word 83 bit 12）へ落とします。EOPNOTSUPP を返すと、fsync が失敗として上がります（src/kern/disk.c:2180-2215）。
3. recovery の COMRESET は「locally initiated transport reset」です。USB と同じく `disk_persistence_forget` を呼びます（src/drivers/usb/usb-storage.c:627、include/kern/disk.h の注記）。FLUSH_PROOF を付けないので、今の実害はありません。
4. memory barrier を明記します。command header・FIS・PRDT を書いてから、PxCI の書き込みの前に write barrier を入れます。CI の消滅を見てから、bounce と FIS 領域を読む前に read barrier を入れます。volatile でない store がコンパイラに MMIO の後ろへ動かされ得るためです。
5. read では PRDBC を確かめ、短い転送を EIO にします（IDENTIFY も同じ）。
6. timeout 30 s × (1+2 回) を boot の idle thread で行うと、壊れた disk 1 台で boot が約 90 秒以上止まります。IDENTIFY には probe 用の短い timeout を使います。
7. command header の C bit（Clear Busy upon R_OK）と FIS の C bit の書き分け。設計 5 の「C=1」は FIS の bit であることを明記します。
8. `GET_GEOMETRY` に IDENTIFY の CHS（16/63）を返すことの意味は小さいです。使い手は src/kern/vfs.c:1308 の log だけです。
9. hw.diskstats に SATA が「IDE」と出ます。UAPI を増やさない判断は妥当です。
10. 32 slot ぶんの command table（12 KiB/port）は、slot 0 しか使いません。NCQ までは不要です（容量だけの話）。

## 確かめて問題が無かった点

- d_max_transfer_blocks は 64 KiB 単位で、disk の層が分割します（disk.c:3971）。async では E2BIG・EINVAL で上限を守ります（disk.c:2852）。
- `drv_dma_alloc_coherent` は物理的に連続で、max_segment_size（16 MiB）以下です（src/drivers/generic/dma.c:283 以降）。64 KiB の bounce は PRDT の 1 entry です。
- 128 B・256 B・1 KiB の整列は、384 B 単位と page 単位の確保で満たせます。PRDT の DBC は bytes−1 で、偶数長の規則も満たします。
- word 106・117-118 の解釈、LBA48 の count、PxSERR→PxIS→IS の消す順、BIOS/OS handoff の 25 ms と 2 s、COMRESET の手順は正しいです。
- 32 bit DMA、`b_mapped_block` の扱い（設計に明記を推奨）も問題ありません。
- HAL の変更は不要です。UAPI も変えません。

## 試験が誤りを見つけられない所と、追加の案

- 設計の「dd と cmp」では、上の R1（BME）、R2（偽成功）、R3、中の 1・3・4・5・6・7 のどれも見つかりません。
- QEMU の ich9-ahci の特性（記憶・推測）:
  - CAP2.BOH が無いので、handoff は試せません。
  - SSS が無いので、SUD の欠落も見つかりません。
  - FRE の順序に寛容です。
  - ide-hd は logical_block_size=512 しか受けないので、4Kn は QEMU で試せません（512e は physical_block_size=4096 で試せる）。
- 案:
  - **(a) flush:** QMP の `query-blockstats` で flush_operations、wr_bytes、rd_bytes を前後で比べ、sync と shutdown で flush が出ることと、I/O が AHCI を通ったことを確かめます（console log を読まない方法）。
  - **(b) error 回復:** `blkdebug` の inject-error（once=on、errno=5）で read・write の EIO を起こし、EIO が返ることと、次の I/O が回復した port で通ることを確かめます。
  - **(c) 遅延:** QMP の `block_set_io_throttle` で遅くし、30 s 未満の遅延を偽 timeout にしないことと、poll で完了を見つけた回数が 0 であること（中の 6 の数え）を確かめます。
  - **(d) 複数:** `-device ahci,id=ahci1` で 2 つ目の HBA、複数の port、空の port だけの構成で、boot 時間の差を測ります。
  - **(e) 4Kn:** IDENTIFY の解釈の host 試験に、4Kn・512e・word 106 が無効、の各 vector を入れます。
  - **(f) 競合:** R2 の command の状態遷移の host 試験。
  - **(g) suspend:** ws052 に S0 idle の試験があれば、q35 で ENOTSUP にならないこと（R4）。
- 実機の証拠と QEMU の証拠は分けて記録し、実機は未実施と明記します。

## ユーザーの判断が要る点

- **U1（R4）:** 最小の suspend・resume を今回の範囲に足すか。既定を n にするか。S0 idle の失敗を既知の制限として受け入れるか。今日 18 時の freeze との兼ね合いがあります。
- **U2:** Intel の RAID（RST）mode の class 01.04.00 にも bind するか。Linux は Intel の device ID の表で bind します（記憶）。
  - BIOS で RAID から AHCI へ切り替えると、dual boot の Windows が起動しなくなる危険があります。
  - RST が NVMe を AHCI の BAR に remap している機種では、NVMe 自体が PCI に見えません（推測）。
  - ws.md は「AHCI に切り替える」前提ですが、その危険をユーザーに示すべきです。
- **U3（中の 7）:** shutdown で STANDBY IMMEDIATE まで出すか。
- **U4（軽微の 9）:** hw.diskstats に「IDE」と出ることを受け入れるか（SATA の種類を足すと UAPI の変更になる）。

## Phase・計画について

- 依存「無し」は妥当です。
- 一方で、R4 と中の 2 により、既定 y にした時点で全ての q35 の試験（boot の時間）と S0 idle に影響が出ます。p001 の確かめに「既存の q35 の boot-test（uefi-nvme・uefi-usb）が遅れず通ること」を足す必要があります。
- T1 の依頼文に上の試験の案 (a)〜(d) を入れないと、cleared の判定の根拠が弱いままです。

## 参照した file

- /home/awe/zedBSD-claude1/src/drivers/pci/pci-nvme.c（1468-1835 attach、1583・1589・1640・5444、4050-4085 の待ち、4761-4835 の submit、4907 以降の probe）
- /home/awe/zedBSD-claude1/include/kern/disk.h
- /home/awe/zedBSD-claude1/src/kern/disk.c（2150-2215、2852、3971）
- /home/awe/zedBSD-claude1/include/drivers/pci/pci.h
- /home/awe/zedBSD-claude1/include/drivers/generic/dma.h
- /home/awe/zedBSD-claude1/src/drivers/generic/dma.c
- /home/awe/zedBSD-claude1/src/drivers/pci/pci-pcat.c:93-97
- /home/awe/zedBSD-claude1/src/kern/platform/pcat.c:255-260、417-438
- /home/awe/zedBSD-claude1/src/drivers/pci/pci-power.c:21、797-803
- /home/awe/zedBSD-claude1/src/kern/shutdown.c:113-162
- /home/awe/zedBSD-claude1/src/drivers/usb/usb-storage.c:627、1680-1700
- /home/awe/zedBSD-claude1/plan/tools/boot-test.sh:144
