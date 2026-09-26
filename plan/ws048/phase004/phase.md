<!-- awesome-plan project=zedbsd record=ws048p004 -->

# ws048-p004: 非 coherent な DMA（uncached の対応付け）

Phase ID: `ws048-p004`
Parent: [WS048](../ws.md)
Status: uncleared（2026-09-27。HAL 以外の部分は済み。**hal.h の差分の承認待ち**）
Queue: 2026-09-27 ユーザー指示のサブエージェントの実行（worktree の branch）
HAL の承認: **要る**。hal.h に `hal_pmem_map_uncached`・`hal_pmem_unmap_uncached` を足す（design.md §6、差分 [proposed/hal-pmem-uncached.diff](../proposed/hal-pmem-uncached.diff)）
依存: p001

## 範囲（承認の後）

- hal.h の 2 関数と arm64 の実装（MAIR の Attr3 = Normal non-cacheable と uncached の窓）。他の port は実装しない（実行で決めた。kernel は weak で参照する）。
- `kern_pmem_map_uncached`・`kern_pmem_unmap_uncached`（arm64 だけの `src/kern/uncached.c`。実行で決めた）。
- `src/drivers/generic/dma.c`: 非 coherent な device の `alloc_coherent`・`free_coherent`・`drv_dma_map`・vector。

## 受け入れ条件

1. 承認（ユーザーの差分ごとの事前承認）。
2. host 試験: 非 coherent な device で uncached の番地を返し、`drv_dma_map`・vector が動く。coherent な device の振る舞いは変わらない。
3. QEMU raspi4b の kernel の自己試験か gdbstub で、uncached の窓に書いた値が direct map から（cache を invalidate した後）見えること。
4. rpi4 と amd64 の `make -j16` が warning 0、`BOOT_MODE=raspi4b plan/tools/boot-test.sh` と amd64 の `boot-test.sh` で login prompt。

## 2026-09-27 の実行

### 当てたもの（HAL ではない。承認不要）

| file | 内容 |
| --- | --- |
| `include/kern/pmem.h` | `kern_pmem_map_uncached()`・`kern_pmem_unmap_uncached()` の宣言（実装は HAL の承認の後、arm64 だけの `src/kern/uncached.c`） |
| `src/drivers/generic/dma.c` | allocation ごとに CPU の view（`address`）を持つ。coherent な device は従来どおり direct map。非 coherent な device は `kern_pmem_map_uncached()` の view を返し、free で外す（外せなければ allocation を残し、呼び手が retry できる）。二つは weak の参照で、無い kernel では非 coherent な device の coherent な確保が ENOTSUP になる（cache の効いた memory を黙って渡さない）。`drv_dma_map()` は view で探す。vector は非 coherent でも許す（中身が coherent な確保なので）。規約の件数 51 → 49 |
| `plan/ws048/tests/dma-host-test.c` | 下の host 試験 |

amd64 の振る舞いは変わらない（coherent な device の view は `kern_pmem_to_kernel()` と同じ番地）。

### 承認を待つもの（**未適用**）

差分 [../proposed/hal-pmem-uncached.diff](../proposed/hal-pmem-uncached.diff)（412 行、SHA256 `42901901603571540b9faef9477c8b115768d050f82cd65fdaaebd4f86657f36`）。`patch -p1 --dry-run` で今の branch に当たることを確認した。

| file | 内容 |
| --- | --- |
| `include/hal/hal.h` | `hal_pmem_map_uncached()`・`hal_pmem_unmap_uncached()` の宣言と契約（design.md §6） |
| `src/hal/arm64/locore.S` | MAIR の Attr3 = `0x44`（Normal non-cacheable）。Attr0〜2 は不変 |
| `src/hal/arm64/space.c` | system half の L0 の entry 1（`0xffff_0080_0000_0000` から 512 GiB）を uncached の窓にし、page ごとに Normal-NC・実行不可で対応付ける（table は必要に応じて確保）。対応付けの前に direct map の範囲を `dc civac`。外すときは entry を消し、TLB を消し、direct map の line を捨てる。番地は増やすだけで再利用しない。RAM でない範囲・揃っていない範囲は拒む |
| `src/kern/uncached.c`（新規、arm64 だけ） | `kern_pmem_map_uncached()`・`kern_pmem_unmap_uncached()`（HAL の状態を errno へ） |
| `platform/arm64/vmunix.mk` | `src/kern/uncached.c` |

amd64・i386・sparcv9・m68k の HAL は変えない（非 coherent な bus が無く、kernel は weak で参照するので link も要らない）。

## 検証

| 検証 | 結果 |
| --- | --- |
| host 試験 `dma-host-test`（uncached が無い kernel） | 46 checks 通過: coherent な device は direct map・`drv_dma_map`・vector が従来どおり。非 coherent な device の coherent な確保と vector は ENOTSUP、物理 memory を残さない |
| host 試験 `dma-uncached-host-test`（uncached がある kernel） | 81 checks 通過: view は uncached の窓、device の番地は物理、`drv_dma_map` は view で探し direct map の番地は ENOTSUP、unmap の拒否で buffer が残り retry で外れる、vector は uncached の memory、destroy 中の device は EBUSY、最後に物理・view の残りが 0 |
| rpi4 の `make -j16 vmunix`（当てたものだけ） | 成功、warning 0 |
| amd64 の `make -j16 vmunix`（`config-amd64.mk`） | 成功、warning 0、`check-amd64-vmunix` PASS。image は sysroot（`make toolchain`）が要るので作っていない。amd64 の boot test は未実施 |
| QEMU raspi4b の boot test（当てたものだけ） | login prompt |
| 差分の検証（worktree の外の scratch の copy に当てた。worktree には当てていない） | rpi4 の vmunix の build が warning 0。QEMU raspi4b で、一時的な probe（`proposed/scratch-probe.py` で scratch だけに入れた。差分には含まない）が非 coherent な DMA device を作り、8 KiB と 4 KiB の coherent な確保をして後者を返した。QEMU の monitor で: probe の記録が全段の成功、view `0xffff008000000000` の 2 page が `gva2gpa` で物理 `0x950000`・`0x951000`、view から書いた値が物理側（`xp`）に見える、返した view `0xffff008000002000` は `Unmapped`。L3 の entry は `0x006000000095070f`（valid・page・AttrIndx 3・inner shareable・AF・PXN・UXN）。QEMU は cache を模倣しないので、非 cache の効果そのものは確かめられない（実機だけ）。同じ kernel で boot test は login prompt（[qemu-login-proposal.png](qemu-login-proposal.png)） |
| `style-check.py` | `dma.c` 51 → 49、`pmem.h` 0。差分: `uncached.c` 0、`space.c` 29（前と同じ）、`hal.h` 4（前と同じ） |
| 実機 | 未実施 |

## 再開の条件

ユーザーが差分 `proposed/hal-pmem-uncached.diff` を承認したら: 差分を当て、rpi4 の build・boot test をし、Guardrail の承認済みの表に載せ（main session）、この Phase を clear する。承認されない場合は design.md §6 の案 B（xHCI に cache の操作を入れる）を計画し直す。
