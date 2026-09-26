<!-- awesome-plan project=zedbsd record=ws048p002 -->

# ws048-p002: PCIe の root complex と PCI の rpi4 の backend

Phase ID: `ws048-p002`
Parent: [WS048](../ws.md)
Status: cleared（2026-09-27。受け入れ 1〜4。実機（5）は未実施）
Queue: 2026-09-27 ユーザー指示のサブエージェントの実行（worktree の branch）
HAL の承認: 不要（hal.h は変えない。`src/hal/arm64/space.c` の実装の修正だけ）

## 範囲

- FDT の reader（`src/drivers/generic/fdt.c`、`include/drivers/generic/fdt.h`）。design.md §11。
- arm64 の `hal_space_map_device()` を契約どおりに（design.md §4.1）。
- brcmstb の host bridge（`src/drivers/pci/pci-brcmstb.c`、`include/drivers/pci/pci-brcmstb.h`）: reset・link・window・
  config の access・資源の割り当て・map_bar・INTx（design.md §2〜§4、§8）。
- `src/kern/platform/rpi4.c` から起動（FDT の node が有効なときだけ）。`pci.c`・`dma.c` を arm64 の build に。
- `platform/arm64/vmunix.mk` の未解決 symbol の検査の awk の quote（p001 の付随の発見）。

範囲外: SSC（design.md §2.4）、MSI、VL805 の firmware（p003）、xHCI（p005）。

## 受け入れ条件

1. host 試験: FDT（実物の DTB と `status = "disabled"` の DTB と壊れた blob）、brcmstb の register model（design.md §13.1）が通る。
2. rpi4 の `make -j16` が warning 0。amd64 の vmunix の build も通る（`pci.c`・`dma.c` を変えた場合）。
3. `BOOT_MODE=raspi4b plan/tools/boot-test.sh` で login prompt（QEMU は PCIe が disabled なので driver は何もしない）。
4. 新しい file の `style-check.py` が 0 件。既存の file は件数が増えない。
5. 実機: 起動が止まらず、`lspci` に 01:00.0 1106:3483 が出る。**ユーザーの確認。行うまで未実施**（受け入れ 1〜4 で Phase を clear し、実機の結果は後で追記する）。

## 変更

| file | 内容 |
| --- | --- |
| `include/drivers/generic/fdt.h`・`src/drivers/generic/fdt.c`（新規） | FDT の reader: header の検査、`compatible`・phandle・親の探索、property、`status`、cell の数、`ranges` を root までたどる番地の変換、`reg` |
| `include/drivers/pci/pci-brcmstb.h`・`src/drivers/pci/pci-brcmstb.c`（新規） | BCM2711 の root complex: FDT からの記述（`reg`・`ranges`・`dma-ranges`・`interrupt-map`）、reset・SerDes・inbound（RC_BAR2、SCB0_SIZE）・PERST の解除と link の待ち（100 ms の settle の後、さらに最大 100 ms）・outbound・class code・CLKREQ、config の access（bus 0 は直接、下は index と data の窓、spinlock）、bus 番号と BAR の割り当て、root port の window、`map_bar`、INTx（swizzle と interrupt map）。MSI は `ENOTSUP` |
| `src/drivers/platform/rpi4/rpi4-pcie.c`・`.h`（新規） | PCI の core の起動、FDT の node を探し、無効・無い・link down なら何もせず続ける。有効なら start と publish |
| `src/hal/arm64/space.c` | `hal_space_map_device()` を契約どおりに（design.md §4.1）: RAM は従来どおり direct map を返す。RAM と重なる範囲は拒む。device は 2 MiB ごとに Device-nGnRE・実行不可の block を置き、L1 が空なら L2 の table を確保、属性の違う有効な entry は break-before-make。hal.h は不変 |
| `src/kern/platform/rpi4.c` | SD の後に `drv_rpi4_pcie_init(fdt_phys)` |
| `platform/arm64/vmunix.mk` | `fdt.c`・`dma.c`・`pci.c`・`pci-brcmstb.c`・`rpi4-pcie.c` を build に。未解決 symbol の検査の awk の quote を直した（`'$$1 == \"U\"'` → `'$$1 == "U"'`。以前は awk が構文の誤りで何も出さず、検査が常に通っていた） |
| `config/drivers/architecture/arm64.drivers` | `BCM2711 PCIe root complex and PCI core`（fixed、rpi4） |

## 検証

| 検証 | 結果 |
| --- | --- |
| host 試験 `make -f plan/ws048/tests/host-test.mk run DTB=<firmware の bcm2711-rpi-4-b.dtb>`（ASan・UBSan） | FDT 74 checks、brcmstb 1116 checks（model 内の確認を含む）通過 |
| brcmstb の model で確かめたこと | reset（INIT と PERST を同時に）→ 100 µs 後に bridge だけ解除 → SerDes の電源 → MISC_CTRL（SCB_ACCESS・UR_MODE・128 byte・SCB0_SIZE 17）・RC_BAR2（PCI 0、4 GiB、符号 17）・RC_BAR1/3 の閉鎖・little-endian・INTR2 の mask → PERST の解除。bus 1 への config は PERST の解除から 100 ms 以上後で link up の間だけ。outbound（WIN0_LO `0xc0000000`、BASE_LIMIT `0x3ff00000`、BASE_HI・LIMIT_HI 6）、class `0x060400`、CLKREQ。root port の bus 番号 `0x00010100`、memory window `0xc000c000`、prefetchable と I/O の閉鎖、command `0x0006`。VL805 の BAR0 `0xc0000000`、command は触らない。publish は 31 bit・非 coherent の DMA。config: bus 0 の device 1 と bus 1 の device 1 は hardware に触れず all-ones、幅ごとの読み書き、不正な access の拒否。map_bar の CPU 番地 `0x6_0000_0000`、window の外・I/O・空の拒否。INTx: 1:00.0 の INTA → 175、bridge の後ろの slot 1 の INTA → INTB 176、pin 0 → ENODEV、MSI → ENOTSUP。失敗: link が上がらない → ETIMEDOUT（約 200 ms、PERST を戻し、bus 1 に触れず、確保を返す）、endpoint mode → ENODEV、window に入らない BAR → ENOSPC、表せない構成 → EINVAL。multi-function の 2 つ目の function の BAR `0xc0001000` |
| FDT（実物の DTB） | PCIe の register `0xfd500000`/`0x9310`、outbound CPU `0x6_0000_0000`・PCI `0xc0000000`・1 GiB、inbound 0・3 GiB、INTA〜D = 175〜178、mailbox `0xfe00b880`/`0x40`。QEMU と同じく `status = "disabled"` を足した DTB で describe が ENODEV |
| rpi4 の `make -j16 vmunix` | 成功、warning 0（awk の検査も動く） |
| rpi4 の image の `make -j16` | **失敗（WS048 と無関係）**: この branch の起点（`3f0b7c67`）で `src/rtld/rtld.c` が `include/libc/elf.h` と `src/rtld/elf.h` の macro の再定義（`PF_X` ほか）で止まる。userland は WS048 の範囲外なので直していない |
| QEMU raspi4b の boot test | `BOOT_MODE=raspi4b KERNEL=build/arm64/vmunix plan/tools/boot-test.sh /home/awe/zedBSD-rpi4/build/ws053-rpi4-full/hdd-image.img`（SD の image は main の 2026-09-25 の build。kernel はこの Phase の build）で login prompt。[qemu-login.png](qemu-login.png)。QEMU は DTB の PCIe を disabled にするので、driver は何もしない経路を通る（その経路は host 試験で確認。QEMU の中の経路の確認は、aarch64 の gdb が無く未実施） |
| `style-check.py` | 新しい file 6 個は 0 件。`space.c` 29 件（前と同じ）、`rpi4.c` 6 件（前と同じ） |
| 差分の空白の検査（`diff --check`） | 問題なし |
| amd64 | 影響なし（`pci.c`・`dma.c` は変えていない。新しい file は arm64 の build だけ）。build は未実施 |
| 実機 | **未実施**（受け入れ 5。`lspci` で 00:00.0 14e4:2711 と 01:00.0 1106:3483） |

## 残り・注意

- 実機で確かめること: link が上がること（`pcie: brcmstb link up, generation 2, x1` の記録）、`lspci`。上がらなければ SSC（design.md §2.4）と待ち時間を疑う。
- `hal_space_unmap_device()` は arm64 で今も失敗を返す（`hal_space_unmap(HAL_SPACE_SYS, ...)`）。PCIe は対応を外さないので影響は無い。
