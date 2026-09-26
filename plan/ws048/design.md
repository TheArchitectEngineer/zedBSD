<!-- awesome-plan project=zedbsd record=ws048-design -->

# WS048 設計: Raspberry Pi 4 の PCIe・VL805 の xHCI・USB キーボード

Parent: [WS048](ws.md)。ws048-p001（2026-09-27）で書いた。

この文書は振る舞いと番地の事実を書く。Linux の `pcie-brcmstb.c` などの code は写さない。register の番地・bit の位置・
手順の順序はハードウェアの interface の事実であり、実装は我々が一から書く。確かでない点は「未確認」と書き、実機で確かめる。

## 0. 前提と範囲

- 対象は Raspberry Pi 4 Model B（BCM2711、VL805 は PCIe の bus 1 の device 0）。CM4 と Pi 400 も同じ PCIe を持つが、
  VL805 の有無と firmware の扱いが違う（CM4 は VL805 が無い）。この WS は Pi 4 B で確かめ、他は FDT の内容に従って動く範囲に留める。
- QEMU 10.0 の raspi4b は PCIe を模倣しない。QEMU は `-dtb` で渡した DTB の `brcm,bcm2711-pcie`・`genet`・`rng200`・
  `thermal` の node に `status = "disabled"` を書き込む（QEMU の binary の文字列 `bcm2711 dtc: %s has been disabled!` で確認）。
  driver はこの `status` を見て何もしない。QEMU で確かめられるのは「PCIe が無いときに起動を壊さない」ことと host 試験だけで、
  PCIe・VL805・USB の動作の確認は実機だけになる。このリポジトリに実機の試験の仕組み（リモートの電源・シリアルの自動化）は無い。
  実機の確認はユーザーに頼み、行うまで「未実施」と書く。

## 1. 実機の構成（`vendor/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb` から）

| 項目 | 値（DT） | CPU から見た値 |
| --- | --- | --- |
| PCIe controller の register | `scb` の `0x7d500000`、大きさ `0x9310` | `scb` の `ranges`（`0x7c000000` → `0xfc000000`）で `0xfd500000` |
| outbound window（CPU → PCIe の memory） | `ranges = <0x02000000 0x0 0xc0000000  0x6 0x0  0x0 0x40000000>` | CPU `0x6_0000_0000` 〜 1 GiB が PCIe の `0xc0000000` 〜 |
| inbound window（device の DMA） | `dma-ranges = <0x02000000 0x0 0x0  0x0 0x0  0x0 0xc0000000>` | PCIe の `0` 〜 3 GiB が CPU の `0` 〜。DMA の番地 = 物理番地、3 GiB 未満 |
| INTx | `interrupt-map`: INTA〜INTD → GIC SPI 143〜146（level high） | GIC の INTID は SPI + 32 → INTA は 175 |
| controller の割り込み | `interrupts`: "pcie" SPI 147、"msi" SPI 148 | 179、180 |
| `brcm,enable-ssc` | あり | spread spectrum の指定（§2.4） |
| VL805 | `pcie/pci@0,0/usb@0,0`、`resets = <&reset 0>` | bus 1、device 0、function 0。reset は firmware の reset controller（§7） |
| mailbox | `soc` の `0x7e00b880`、大きさ `0x40` | `0xfe00b880` |
| `dma-coherent` | pcie の node に無い | **PCIe の DMA は CPU の cache と coherent でない**（§6） |

FDT は firmware が起動時に書き換える（memory の大きさ、8 GB 版の `dma-ranges` ほか）。driver は file の DTB ではなく、HAL が
`rpi4_boot_handoff.fdt_phys` で渡す実行時の FDT を読む（§11）。読めないときに既定値で動くことはしない（番地を推測して触ると
SError で止まる）。

## 2. root complex（brcmstb）の初期化

### 2.1 register（controller の base からの offset）

| 名前（この文書での呼び名） | offset | 使う field |
| --- | --- | --- |
| RC の config 空間（type 1 header） | `0x0000`〜`0x0fff` | bus 0 の device 0 の config 空間そのもの。PCIe capability は `0x00ac` |
| VENDOR_SPECIFIC_REG1 | `0x0188` | bit 3:2 = inbound（BAR2）の endian。0 = little |
| PRIV1_ID_VAL3 | `0x043c` | bit 23:0 = RC が名乗る class code。`0x060400`（PCI-PCI bridge）にする |
| MDIO_ADDR / WR_DATA / RD_DATA | `0x1100` / `0x1104` / `0x1108` | SerDes の MDIO（§2.4） |
| MISC_CTRL | `0x4008` | bit 12 SCB_ACCESS_EN、bit 13 CFG_READ_UR_MODE、bit 21:20 MAX_BURST_SIZE（0 = 128 byte）、bit 31:27 SCB0_SIZE |
| MEM_WIN0_LO / HI | `0x400c` / `0x4010` | outbound window 0 の PCIe 側の番地 |
| RC_BAR1_CONFIG_LO | `0x402c` | bit 4:0 = 大きさ。0 で無効 |
| RC_BAR2_CONFIG_LO / HI | `0x4034` / `0x4038` | inbound window の PCIe 側の番地と大きさ（bit 4:0） |
| RC_BAR3_CONFIG_LO | `0x403c` | 0 で無効 |
| PCIE_STATUS | `0x4068` | bit 4 PHYLINKUP、bit 5 DL_ACTIVE、bit 7 PORT（1 = RC mode） |
| REVISION | `0x406c` | 記録用 |
| MEM_WIN0_BASE_LIMIT | `0x4070` | bit 15:4 = CPU 側の開始の MB 単位の下位 12 bit、bit 31:20 = 終わり（含む）の MB 単位の下位 12 bit |
| MEM_WIN0_BASE_HI / LIMIT_HI | `0x4080` / `0x4084` | bit 7:0 = 開始・終わりの MB 単位の番地の bit 12 以上 |
| HARD_DEBUG | `0x4204` | bit 1 CLKREQ_DEBUG_ENABLE、bit 27 SERDES_IDDQ（1 = SerDes の電源断） |
| INTR2_CPU | `0x4300` | +0 status、+8 clear、+0x10 mask set、+0x14 mask clear。link の状態変化・MSI の要約 |
| EXT_CFG_DATA | `0x8000` | index で選んだ function の config 空間の窓（4 KiB） |
| EXT_CFG_INDEX | `0x9000` | `bus << 20 | device << 15 | function << 12` |
| RGR1_SW_INIT_1 | `0x9210` | bit 0 PERST（1 = PERST# を assert）、bit 1 INIT（1 = bridge を reset） |

inbound の大きさの符号（RC_BAR2 の bit 4:0）: 大きさを 2 の冪 2^n として、n が 12〜15 なら `n - 12 + 0x1c`、16〜37 なら `n - 15`。
SCB0_SIZE は `n - 15`。window の大きさは `dma-ranges` の合計を 2 の冪へ切り上げたもの（3 GiB → 4 GiB、n = 32、符号 17）。

### 2.2 手順

firmware（起動の EEPROM と start4.elf）が PCIe を起こしていることがある（USB boot のとき）。driver は状態を仮定せず、reset から始める。

1. bridge と端点を reset に入れる: RGR1_SW_INIT_1 の INIT と PERST を 1。100〜200 µs 待つ。
2. bridge の reset を解く: INIT を 0（PERST は 1 のまま）。
3. SerDes の電源を入れる: HARD_DEBUG の SERDES_IDDQ を 0。100〜200 µs 待つ。
4. MISC_CTRL: SCB_ACCESS_EN = 1（RC が system bus へ出られる）、CFG_READ_UR_MODE = 1（存在しない function の config の読みが
   abort でなく all-ones になる。これが無いと空の slot の読みで CPU が SError で止まる）、MAX_BURST_SIZE = 128 byte（BCM2711 の値）、
   SCB0_SIZE = inbound window の符号。
5. inbound: RC_BAR2 = PCIe の 0 から 4 GiB、CPU の 0 へ。RC_BAR1・RC_BAR3 は無効。VENDOR_SPECIFIC_REG1 の endian を little に。
6. controller の割り込みを全て mask し、溜まった状態を消す（INTR2_CPU の mask set と clear に all-ones）。MSI は使わない（§8）。
7. PERST を 0（端点の reset を解く）。PCIe の規格（CEM）で、PERST# の解除から config の要求まで 100 ms 待つ。
   その 100 ms の中で 5 ms ごとに PCIE_STATUS を読み、PHYLINKUP と DL_ACTIVE が両方 1 になれば link up。100 ms で上がらなければ
   失敗（端点が無い、または壊れている）。**link が上がっていない間は bus 1 以下の config を読まない**（SError の元）。
8. PCIE_STATUS の PORT が 1（RC mode）であることを確かめる。0 なら失敗。
9. outbound window 0: MEM_WIN0_LO/HI = PCIe 側の開始（`0xc0000000`）。BASE_LIMIT・BASE_HI・LIMIT_HI = CPU 側の開始と終わりを MB
   単位で（`0x6_0000_0000` → 開始 `0x6000` MB、終わり `0x63ff` MB → BASE_LIMIT = `0x3ff0_0000 | 0x0000`、HI = 6）。
10. PRIV1_ID_VAL3 の class code を `0x060400` に（RC を PCI-PCI bridge として列挙させる）。
11. HARD_DEBUG の CLKREQ_DEBUG_ENABLE を 1（ASPM の L0s・L1 で refclk を CLKREQ# で止められるように。ASPM の設定自体は firmware の既定のまま触らない）。
12. RC の PCIe capability（config `0x00ac`）の Link Status から速度と幅を読み、記録する。

失敗の扱い: 1〜12 のどこで失敗しても、controller を reset に戻さず（PERST を assert するだけ）PCI の bus を作らずに戻る。
起動は続く（USB が無いだけ）。待ちは全て上限つき。

### 2.3 config 空間の access

- bus 0: device 0 function 0 だけ（RC 自身）。controller の base + offset を直接読む。他の device は「無い」と答える（hardware に触れない）。
- bus 1 以下: link が上がっているときだけ。EXT_CFG_INDEX に `bus << 20 | device << 15 | function << 12` を書き、EXT_CFG_DATA + offset を読む。
  index と data の組は一つなので spinlock（割り込みも止める）で守る。
- root port の直下の bus（secondary bus）は device 0 しか持てない（PCIe の link は point-to-point）。device 1〜31 は「無い」と答える。
- 幅: BCM2711 は 8・16・32 bit の access を受け付ける（Linux の BCM2711 の設定は任意幅の generic access を使う）。読みも書きもその幅で行う。
  32 bit の read-modify-write で 16 bit を書くと隣の W1C の status を消すので使わない。

### 2.4 spread spectrum（`brcm,enable-ssc`）

DT は SSC を求めている。SerDes の MDIO で SSC の override を立て、PLL の lock を確かめる手順がある。EMI の規格のためのもので、
link と USB の動作には要らない（Linux 5.10 より前は行っていなかった）。確かめられない実機の SerDes に誤った MDIO を書くと link を
失う危険があるので、**最初の版では行わない**。実機で link と USB を確かめた後の別の Phase（Future Work）にする。

## 3. 資源の割り当て（bus 番号・BAR・bridge window）

PC では firmware が bus 番号と BAR を割り当て、PCI の層（`src/drivers/pci/pci.c`）はそれを読むだけ。Pi 4 では driver が割り当てる。
構成は固定（RC と端点 1 つ）なので、host driver の中に小さな割り当てを置く。PCI の層の変更は要らない。

1. root port（bus 0、device 0）: primary 0、secondary 1、subordinate 1。I/O window と prefetchable window は無効（base > limit）。
2. bus 1 の各 function（device 0、multi-function なら function 0〜7）の BAR を大きさで測る（all-ones を書いて読み戻す。測る間は
   command の memory decode を切る）。memory BAR だけを outbound window の PCIe 側の先頭から、大きさで揃えて並べる。I/O BAR は
   割り当てない（brcmstb に I/O window は無い）。64 bit BAR は上位 0。expansion ROM は割り当てない。
3. root port の memory window（config `0x20`）を割り当てた範囲を 1 MiB で囲む大きさに。command に memory と bus master。
4. 端点の command は触らない（driver の attach が有効にする）。bus 1 に bridge があれば（Pi 4 には無い）割り当てずに記録だけする。

VL805 の BAR0 は 64 bit、4 KiB（未確認。Linux の起動の記録では 4 KiB）。割り当ては PCIe `0xc0000000`、CPU `0x6_0000_0000`。

## 4. BAR の対応付け（map_bar）

`drv_pci_bus_ops.map_bar` は PCIe の番地を CPU の番地に直して（`CPU = window の CPU 側 + (PCIe の番地 - window の PCIe 側)`）
`kern_device_map()` で対応付ける。window の外の BAR は拒む。

### 4.1 arm64 の device mapping（HAL の実装の修正。hal.h は不変、承認不要）

今の arm64 の `hal_space_map_device()` は direct map の番地を返すだけで、対応を作らない（中の `hal_space_map(HAL_SPACE_SYS, ...)`
は常に失敗し、結果を捨てている）。boot で FDT から見つけた UART・mailbox・GIC・SDHCI の 2 MiB の block だけが Device として
対応付いている。PCIe の register（`0xfd500000`）と outbound window（`0x6_0000_0000`）は対応が無く、触ると fault する。
さらに、RAM が 4 GiB 以上の Pi では `0xc0000000`〜RAM の終わりが Normal（cache あり）として対応付き、その中の周辺機器の穴
（`0xfc000000`〜`0xffffffff`）も Normal になっている（投機的な読みが周辺機器へ出うる。既存の潜在的な不具合）。

hal.h の契約（「device の物理範囲を kernel 空間へ対応付ける。RAM は direct map 済みで呼ぶ必要が無い」）どおりに実装する:

- 範囲が FDT の memory（RAM）と重なれば拒む（RAM は direct map を使う）。ただし範囲の全てが RAM なら従来どおり direct map の番地を返す。
- 範囲を 2 MiB の block ごとに Device-nGnRE（MAIR の Attr1）・実行不可で direct map に置く。L1 の entry が無ければ（4 GiB を
  超える番地）L2 の table を 1 page 確保して置く。L1 が 1 GiB の block（RAM）なら、その 1 GiB に device の block を混ぜることは
  RAM と重ならない限り起きないので拒む。
- 既に有効な entry の属性を変えるときは break-before-make（無効にし、TLB を消し、新しい entry を書く）。同じ属性なら何もしない。
- 1 CPU だけの実装（`space.c` は UP 専用）。割り込みを止めて行う。

直すのは `src/hal/arm64/space.c` の `hal_space_map_device()` だけ。

## 5. DMA の番地

inbound window は PCIe の 0 から CPU の 0 へ。DMA の番地 = 物理番地。ただし 3 GiB（`0xc0000000`）以上は outbound window と重なる
ので使えない。`drv_dma_constraints.address_bits = 31`（2 GiB 未満）にする。3 GiB の上限をそのまま表す field は無く、2 GiB で
足りる（USB の DMA は小さい）。RAM が 1 GiB の版は全てが 2 GiB 未満。

## 6. 非 coherent な DMA（**HAL の API の追加。承認が要る**）

BCM2711 の PCIe の DMA は CPU の cache を snoop しない（DT に `dma-coherent` が無い）。xHCI は ring・context・DCBAA・ERST・
bounce buffer を `drv_dma_alloc_coherent()` と `drv_dma_vector_create()` で取り、CPU と controller の両方がそこへ直接読み書きする。
今の `src/drivers/generic/dma.c` は coherent な bus（x86）だけを前提にしている:

- `drv_dma_alloc_coherent()` は direct map（arm64 では Normal の write-back cache）の番地を返す。
- `drv_dma_sync_for_cpu()`・`drv_dma_sync_for_device()` は何もしない。
- `drv_dma_vector_create()` は非 coherent の device を `EOPNOTSUPP` で拒む。

選択肢:

| 案 | 内容 | HAL | xHCI の変更 |
| --- | --- | --- | --- |
| **A（推す）** | 非 coherent な DMA device の `alloc_coherent` は、確保した RAM を cache の無い（Normal non-cacheable）別の kernel の番地へ対応付けて返す。Linux の arm64 と同じ方式 | hal.h に 2 関数を足す（下）。承認が要る | 無し |
| B | 全ての DMA の buffer で CPU が触る前後に cache の clean と invalidate。`drv_dma_buffer_sync_*` を足し、xHCI の ring・context・event の読み書きの全ての所へ入れる | 既存の `hal_dcache_*` を kernel の wrapper から使うだけ。承認不要 | 大きい（数十か所）。cache line を共有する TRB の扱いが難しく、誤りが実機でしか出ない |
| C | device に coherent と偽る | 不要 | 不要だが実機で壊れる。採らない |

案 A の hal.h の差分（**未適用。承認待ち**。全文は [proposed/hal-pmem-uncached.diff](proposed/hal-pmem-uncached.diff)）:

```c
/*
 * Map managed RAM into kernel space a second time, without caching.
 *
 * This is for memory that a device which does not snoop the CPU caches
 * reads and writes.  The range must be page-aligned managed RAM that the
 * caller owns.  Before returning the new address the HAL writes back and
 * discards every cached line of the range, so no dirty line of the direct
 * map can later overwrite what the device or the uncached mapping stored.
 * While the uncached mapping exists the caller must not touch the range
 * through its direct-map address.  A port whose devices snoop the caches
 * may return the direct-map address itself.
 */
int
hal_pmem_map_uncached(
	hal_physaddr_t paddr,
	size_t size,
	void **vaddr);

/*
 * Remove a mapping made by hal_pmem_map_uncached().
 *
 * The range is afterwards reachable through the direct map only.
 */
int
hal_pmem_unmap_uncached(
	void *vaddr,
	size_t size);
```

HAL の実装（同じ差分に含める）:

- arm64: MAIR の Attr3 を Normal non-cacheable（`0x44`）に（`locore.S`）。kernel の L0 の 1 entry（direct map と別の 512 GiB）を
  uncached の窓にし、要求ごとに page 単位で対応を作る（L1〜L3 の table は必要に応じて確保）。対応を作る前に direct map の範囲を
  `dc civac` で書き出して捨てる。窓の番地は増やすだけで再利用しない（DMA の buffer は attach で取り detach で返す程度で、512 GiB を使い切らない）。
  direct map の cache のある別名は残る（投機的な読みで clean な line が入りうるが、書き出されない。Linux の arm64 と同じ前提）。
- amd64・i386・sparcv9・m68k: 呼ばれない（その bus は coherent で、kernel は coherent な device に対して呼ばない）が、link のために
  `HAL_ERR_UNSUPPORTED` を返す実装を置く。

kernel の側（HAL の承認の後、driver の変更として。hal.h ではない）:

- `kern_pmem_map_uncached()`・`kern_pmem_unmap_uncached()`（`src/kern/pmem.c`）。
- `dma.c`: `constraints.coherent == 0` の device の `alloc_coherent` は uncached の番地を返し、`free_coherent` で外す。
  `drv_dma_map()` の探索は返した番地で行う。`drv_dma_vector_create()` は非 coherent でも許す（中身が `alloc_coherent` なので）。
- `drv_dma_sync_*` は非 coherent で `drv_dma_map()` の範囲が coherent の確保の中にしか無いので、何もしなくてよいまま（uncached）。

## 7. VL805 の firmware（mailbox）

Pi 4 B の VL805 の多く（2019 年後半以降の基板）は firmware の EEPROM を持たず、VideoCore が PCIe の reset の後に VL805 へ
firmware を読み込む。ARM 側は VideoCore の firmware の property の mailbox で「xHCI の reset の通知」（tag `0x00030058`）を送る。
DT では VL805 の node の `resets = <&firmware_reset 0>`（firmware の reset controller の USB の id）として表れる。

- 値: 32 bit 1 つ = VL805 の PCIe の番地 `bus << 20 | device << 15 | function << 12`（bus 1 → `0x00100000`）。
- 時機: link が上がり、root port の bus 番号と BAR の割り当てが済んだ後、xHCI の driver が register に触る前。VideoCore は
  PCIe の controller を通して VL805 に触るので、bus 番号が要る。
- 通知が返った後、VL805 が起きるまで 200 µs〜1 ms 待つ（Linux の記述による値。未確認）。
- 古い firmware は tag を知らず失敗を返す。EEPROM を持つ基板は通知が要らない。どちらも起動を止めず、記録して続ける。

mailbox の使い方（property channel 8）:

- 送る buffer: 16 byte 境界、VideoCore が読める 1 GiB 未満の RAM。VideoCore の bus 番地 = 物理 | `0xc0000000`（`soc` の
  `dma-ranges` の cache を通らない別名）。書いた後に data cache を clean、答えを読む前に invalidate。
- 書く: mailbox 1 の status（`+0x38`）の FULL（bit 31）が落ちるのを待ち、`+0x20` に「bus 番地 | 8」を書く。
- 読む: mailbox 0 の status（`+0x18`）の EMPTY（bit 30）が落ちるのを待ち、`+0x00` を読み、送った値と同じなら完了。上限 1 秒。
- 答え: buffer の word 1 が `0x80000000`（成功）、tag の word の bit 31 が立つ。

**所有**: HAL の mailbox（`src/hal/arm64/bsp-rpi4/mailbox.c`）は起動の framebuffer の設定だけに使い、kernel へ移った後は
使わない（呼び出し元は `cmain.c` の `rpi4_framebuffer_init()` だけ）。driver は自分の mailbox の client
（`src/drivers/platform/rpi4/rpi4-firmware.c`）を持ち、`kern_device_map()` で mailbox を対応付ける。hal.h は変えない。
二者が同時に使うことは無い。後で HAL が kernel の実行中に mailbox を使うようになれば、所有を一つにする（その時は HAL の口の
提案が要る）。

cache の操作: driver は HAL を直接呼ばない（`include/kern/device-io.h` の方針）。既存の `hal_dcache_clean_range()`・
`hal_dcache_invalidate_range()`（hal.h に既にある。amd64 には実装が無い）を包む kernel の関数
`kern_dcache_clean_range()`・`kern_dcache_invalidate_range()` を `src/kern/dcache.c` に置き、実装のある arch（arm64）だけで build する。

注: HAL の mailbox は FULL の確認に mailbox 0 の status（`+0x18`）を読んでいる。書き込み先は mailbox 1 なので、正しくは `+0x38`。
実害は出ていない（ARM→VC の mailbox が満ちることは稀）。WS048 の範囲外なので記録だけする。

## 8. 割り込み

- INTx: VL805 の INTA → GIC SPI 143 → INTID 175、level high。GIC の初期化（`gic.c`）は SPI を level に設定済み。
  PCI の層の INTx の経路（`kern_irq_register(175)` → `hal_irq_register`）で足りる。host driver の `allocate_irqs` は
  device の interrupt pin（config `0x3d`）を読み、root port の直下なので swizzle 無しで INTA〜INTD → FDT の `interrupt-map` の INTID を返す。
- MSI・MSI-X: brcmstb は内部に MSI の受け口（SPI 148）を持つが、HAL の MSI の口（`hal_irq_register_msi` ほか）は x86 の
  LAPIC 向け。この WS では使わず `ENOTSUP` を返す（xHCI は MSI-X → MSI → INTx の順に試すので INTx に落ちる）。Future Work。
- controller の割り込み（SPI 147: link の状態変化）: 使わない（mask したまま）。

## 9. xHCI と USB の driver の再利用

`src/drivers/pci/pci-xhci.c`・`src/drivers/usb/*` は arch に依存する code を持たない（port I/O・x86 の asm・64 bit の MMIO を使わない）。
arm64 の build に入れるのに要るもの:

- `src/drivers/pci/pci.c`・`src/drivers/generic/dma.c`・`src/drivers/usb/usb.c`・`pci-xhci.c`・`usb-hub.c`・`usb-hid.c` を
  `platform/arm64/vmunix.mk` に（config で選ぶ）。
- config の表（`config/drivers/pci.drivers`・`usb.drivers`）の platforms に `rpi4` を足し、`config/ci/config-rpi4.mk` を menuconfig で作り直す。
- `src/kern/platform/rpi4.c`: PCI・USB の core と driver の登録、PCIe の host の起動、`kern_platform_refresh_devices()` で
  `drv_pci_xhci_probe_roots()`（pcat.c と同じ順序）。
- console の入力: `usb-hid.c` は `drv_input_device_*` へ出し、`src/drivers/generic/console.c`（arm64 の build に既にある）が
  console の tty へ渡す。rpi4 の console（`rpi4-console.c`）はシリアルの入力を `tty_console_input_byte()` に入れており、両方が
  同じ console へ入る（p006 で確かめる）。

VL805 について公開の情報で知られている振る舞い（実機で確かめる。code は写さない）:

- TRB の先読み: ring の segment の終わりを越えて次の TRB を読むことがある。IOMMU の無い Pi 4 では次の物理 page を読むだけで
  害は無い（RAM の外にかからないよう、ring の page の直後が RAM であることは確保の仕方で満たされる）。
- endpoint context の DCS が停止の後に正しくないことがある（Set TR Dequeue で DCS を context から取らず、driver の記録から取る）。
  今の xHCI の driver がどちらを使っているかを p005 で確かめる。
- USB 2.0 の port（黒）は VL805 の中の USB 2.0 hub（VIA Labs 2109:3431）を経る。low/full speed のキーボードは HS hub の
  TT を経るので、hub の driver と xHCI の slot context の TT の field（既にある）が要る。USB 3.0 の port（青）は root port 直結。

## 10. 起動の順序（`src/kern/platform/rpi4.c`）

`kern_platform_init()`（割り込みの前）:

1. console、SD（今のまま）。
2. FDT から PCIe の node を読む（§11）。無い・`status` が okay でない → PCIe を使わず続ける（QEMU）。
3. PCI の core（`drv_pci_init`）、USB の core と driver の登録（p005 以降）。
4. brcmstb の起動（§2）と資源の割り当て（§3）。失敗 → 記録して続ける。
5. bus 1 の VL805（1106:3483）に firmware の通知（§7、p003 以降）。
6. root bus を作り列挙（`drv_pci_bus_create_root`・`drv_pci_scan_all`）。driver の attach はここで起きる。

`kern_platform_refresh_devices()`（割り込みの後）: `drv_pci_xhci_probe_roots()`（p005 以降）。

## 11. FDT の読み取り

HAL の `bsp-rpi4/fdt.c` は HAL の内部で、決まった node の番地だけを HAL の struct に写す。driver 側に読み取り専用の小さな
FDT の reader を置く（`src/drivers/generic/fdt.c`、`include/drivers/generic/fdt.h`）。将来の arm64 の driver（GENET ほか）も使える:

- header の検査（magic、版、大きさ、struct・strings の block が blob の中にあること）。
- node の走査（path で、または `compatible` の一致で）、property の取得、親の `#address-cells`・`#size-cells`、
  `status`（無い・"okay"・"ok" を有効とする）。
- 番地の変換: node の `reg` を親の bus の `ranges` を順にたどって CPU の番地へ（`scb` の `ranges` の 1 段で足りるが一般に書く）。
- `ranges`・`dma-ranges`・`interrupt-map` の entry の読み出し（PCIe の 3 cell の番地を含む）。

FDT の blob は HAL が物理番地 `fdt_phys` で渡し、page allocator が予約している（`page.c`）。`kern_pmem_to_kernel()` で読む。

## 12. source の配置と config

| file | 内容 | Phase |
| --- | --- | --- |
| `include/drivers/generic/fdt.h`・`src/drivers/generic/fdt.c` | FDT の reader | p002 |
| `include/drivers/pci/pci-brcmstb.h`・`src/drivers/pci/pci-brcmstb.c` | BCM2711 の PCIe の host bridge（`drv_pci_bus_ops`） | p002 |
| `src/hal/arm64/space.c` | `hal_space_map_device()` の実装の修正（§4.1） | p002 |
| `src/kern/platform/rpi4.c` | 起動の順序（§10） | p002〜p006 |
| `platform/arm64/vmunix.mk`・`config/drivers/architecture/arm64.drivers` | build（PCIe の host は SDHCI と同じ fixed） | p002〜 |
| `include/kern/dcache.h`・`src/kern/dcache.c` | cache の操作の kernel の wrapper（arm64） | p003 |
| `src/drivers/platform/rpi4/rpi4-firmware.c`・`.h` | mailbox の property の client と VL805 の通知 | p003 |
| `include/hal/hal.h`・`src/hal/*`（提案） | `hal_pmem_map_uncached` ほか（§6） | p004（承認後） |
| `src/kern/pmem.c`・`src/drivers/generic/dma.c` | 非 coherent な DMA | p004（承認後） |
| config の表と `config-rpi4.mk` | xHCI・hub・HID を rpi4 で | p005・p006 |

新しい file には `plan/coding-style.md` の全文を当て、`python3 plan/tools/style-check.py` で 0 件にする。既存の file
（`space.c`・`rpi4.c`・`dma.c`・`pci.c`）は件数を増やさない。

## 13. 試験

### 13.1 host 試験（guest を起動しない。`plan/ws048/tests/`）

| 試験 | 内容 |
| --- | --- |
| FDT | 実物の `bcm2711-rpi-4-b.dtb` から PCIe の register（`0xfd500000`、`0x9310`）・window（CPU `0x6_0000_0000`・PCIe `0xc0000000`・1 GiB）・`dma-ranges`（0・3 GiB）・INTA〜D（175〜178）・mailbox（`0xfe00b880`）を読む。QEMU と同じく `status = "disabled"` を書いた DTB（`dtc` で作る）で「無効」と答える。壊れた header を拒む |
| brcmstb の register model | `pci-brcmstb.c` を本物のまま compile し、`kern_mmio_*`・`kern_device_map`・待ち・PCI の core の口を試験の側で置き換える。model は RGR1・HARD_DEBUG・MISC_CTRL・STATUS・EXT_CFG と VL805 の config 空間（BAR0 64 bit 4 KiB、INTA）を持ち、PERST の解除の後に何回目かの読みで link を上げる。確かめること: 手順の順序（INIT・PERST・IDDQ・MISC_CTRL・PERST の解除・link の確認・window）、書いた値（SCB0_SIZE 17、RC_BAR2 の符号、window の BASE_LIMIT・HI）、link が上がらないときに bus 1 に触れないこと、config の index の値、bus 0 の device 1 以上と bus 1 の device 1 以上が「無い」こと、BAR の割り当て（`0xc0000000`）と root port の bus 番号・memory window、map_bar の CPU 番地、INTx の INTID |
| mailbox | message の組み立て（大きさ・tag・値・境界）、VideoCore の bus 番地、register の手順（FULL・EMPTY の待ち、上限）、答えの判定（成功・tag の不明・timeout）を model で |
| 非 coherent な DMA（p004） | `dma.c` が非 coherent な device で uncached の番地を返し、`drv_dma_map` と vector が動くこと（HAL の関数は試験の側で置き換え） |

### 13.2 build と QEMU

- `make -j16`（rpi4 の config、warning 0）。
- `BOOT_MODE=raspi4b plan/tools/boot-test.sh`（画面の login prompt）。QEMU の DTB では PCIe が disabled なので、driver は何も
  しない。「PCIe が無くても起動が壊れない」ことの確認。
- amd64 の build（`pci.c`・`dma.c`・`pci-xhci.c` を変える Phase だけ）。

### 13.3 実機（ユーザー）

QEMU では確かめられない。**このリポジトリに実機の試験の仕組みは無い**。確認はユーザーに頼み、結果を Phase に書く。

| Phase | 実機で見ること |
| --- | --- |
| p002 | 起動が止まらない。`lspci` で 00:00.0（14e4:2711、bridge）と 01:00.0（1106:3483、xHCI） |
| p003 | 通知が成功と記録される（`dmesg`） |
| p005 | xHCI が起動し、`lsusb` で hub（2109:3431）が見える |
| p006 | USB キーボードで login できる |

## 14. Phase と承認

| Phase | 内容 | HAL の承認 | 依存 |
| --- | --- | --- | --- |
| p001 | 調査と設計（この文書） | 不要 | — |
| p002 | FDT の reader、arm64 の device mapping の実装の修正、brcmstb の host bridge と PCI の backend。VL805 が列挙される | 不要（hal.h は不変） | p001 |
| p003 | mailbox の client と VL805 の firmware の通知 | 不要 | p002 |
| p004 | 非 coherent な DMA（`hal_pmem_map_uncached` と `dma.c`） | **要る**（hal.h の追加） | p001 |
| p005 | xHCI を rpi4 で（build、INTx、VL805 の振る舞いの確認） | 不要 | p002・p003・p004 |
| p006 | hub と HID キーボードを rpi4 で有効にし、console に入力 | 不要 | p005 |
| p007 | 変えた source の全文の規約確認と、QEMU・amd64 の回帰。実機の確認の取りまとめ | 不要 | p002〜p006 |

## 15. ユーザーの判断が要ること

1. **§6 の HAL の差分（案 A）の承認**。承認が無ければ p004 以降（USB の動作）は進まない。案 B（HAL を変えず xHCI に cache の操作を
   入れる）を選ぶこともできるが、変更が大きく、誤りが実機でしか出ない。
2. mailbox を driver が持つこと（§7。hal.h は変えない）。HAL に持たせたい場合は hal.h の提案に変える。
3. 実機の確認（§13.3）。
