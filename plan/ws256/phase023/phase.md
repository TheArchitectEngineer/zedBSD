# ws256-p023 DMA の coherency の確かめ

Parent: [ws256](../ws.md)
Status: in-progress（q1294-i01、P1、2026-10-19。板の前の code・試験の cdev・host 試験は済み。実機の確かめ（ユーザーの目視）は未実施）
Disposition: normal
設計: [design-peripherals.md](../design-peripherals.md) 改訂 4 の §2.4・§3.9

## 範囲（q1294-i01）

- 試験の kernel の config（`plan/ws256/tests/vf2-dmatest/`、通常の image には入らない）と `/dev/vf2-dmatest` の cdev。
- 読むと §2.4 の 0a（別名が同じ DRAM）・0b（stale の検出）・主の試験（64 KiB、offset を 128 KiB ずつ 8 回、両方向）を流し、text で答える。
- 予備 (i)（非 coherent の時の確保ごとの uncached の別名）は作らない（p023 で非 coherent と出た後の段）。

## 作った物

| path | 内容 |
| --- | --- |
| `src/drivers/platform/visionfive2/vf2-dmatest-core.{c,h}`（新規） | §2.4 の手順と判定。memory は callback（cached の load/store、別名の load/store、FLUSH64、bus master の copy）だけで見るので host の model で試せる。終わりに（copy の失敗の時も）2 MiB 全部を flush |
| `src/drivers/platform/visionfive2/vf2-dmatest.{c,h}`（新規） | cdev `/dev/vf2-dmatest`（major 0x0017）。`drv_vf2_dmac_get(…, coherent = 1)` の DMAC の DMA owner から 2 MiB 揃えの 2 MiB を `drv_dma_alloc_coherent` で取り、device の address が物理と同じ事（`kern_pmem_to_kernel` が同じ address を返す事）を確かめ、その 2 MiB だけの別名（物理 + 0x4_0000_0000）を `kern_device_map`（UNCACHED、HAL の既存の `hal_space_map_device` の device の窓）で写す。copy は p019 の `drv_dw_axi_dmac_copy`、flush は `kern_dcache_clean_range` + `kern_dcache_invalidate_range`（VF2 の BSP では FLUSH64）。読む人は 1 人ずつ（2 人目は EBUSY）、offset 0 の read ごとに 1 回流す。答えは kernel の log にも出す |
| `src/kern/platform/visionfive2.c` | `#ifdef CONFIG_VF2_DMA_TEST` の時だけ、pwmdac の attach の後に `drv_vf2_dmatest_publish(&soc_tree)` |
| `platform/riscv64/vmunix.mk` | `CONFIG_VF2_DMA_TEST=y` の時だけ vf2 の source に 2 file を足す |
| `plan/ws256/tests/vf2-dmatest/config-visionfive2-dmatest.mk` | `config/ci/config-visionfive2.mk` ＋ `CONFIG_VF2_DMA_TEST := y`・`ZEDBSD_TEST_CPPFLAGS += -DCONFIG_VF2_DMA_TEST` |
| `plan/ws256/tests/vf2-dmatest/run.sh`・`vf2-dmatest-host-test.c` | host 試験（下） |

HAL: 変更無し（hal.h・src/hal とも）。別名の写像は既存の device の窓（上限 64 GiB、2 MiB の leaf）で足りた。

## 答えの形

3 行。`<master>` は `dmac`。

```
DMATEST dmac ALIAS OK | ALIAS MISMATCH
DMATEST dmac CONTROL OK | CONTROL INCONCLUSIVE | CONTROL SKIPPED（0a が MISMATCH の時）
DMATEST dmac PASS COHERENT runs=8 bytes=65536
  または DMATEST dmac FAIL run=<n> [reverse] offset=0x<x> view=cached NONCOHERENT direction=<cpu-to-device|device-to-cpu|unclassified> mismatches=<回数>
  または DMATEST dmac FAIL copy run=<n> error=<errno>
```

始められなかった時は 1 行: `DMATEST dmac FAIL <channel|alloc|physical|alias> error=<errno>`。

- 判定は cached の view だけ。別名は向きの分類だけに使い、0a・0b の両方が OK の時だけ（それ以外は `direction=unclassified`、§2.4 の 0）。
- `mismatches` は 16 回（8 回 × 両方向）のうち食い違った回数。
- 注意: copy の descriptor も coherent の前提の memory にある。CPU→device が非 coherent なら DMAC が古い descriptor を読み、`FAIL copy … error=60`（ETIMEDOUT）か `error=5`（EIO）になり得る。その時も非 coherent の疑いとして記録する。

## 確かめ（板の前）

| 確かめ | command | 結果 |
| --- | --- | --- |
| host 試験 | `plan/ws256/tests/vf2-dmatest/run.sh build/p023-host` | ALL PASS（10 件: coherent、非 coherent の両方向・CPU→device だけ・device→CPU だけ、別名の書き込みが cache を落とす（INCONCLUSIVE、coherent と非 coherent）、別名が別の memory（MISMATCH、coherent と非 coherent）、copy の失敗、text の切り詰め。各件で最後に region の line が cache に残らない事と、region 全体の flush が 2 回以上ある事） |
| VF2 の試験の kernel | `make ZEDBSD_CONFIG=plan/ws256/tests/vf2-dmatest/config-visionfive2-dmatest.mk BUILD=build/p023-vf2 -j16 vmunix` | exit 0、warning・error 0（build/tmp/p023-vf2.log）。vmunix に `drv_vf2_dmatest_*` がある |
| VF2 の通常の kernel | `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p023-vf2plain -j16 vmunix` | exit 0、warning 0。vmunix に dmatest の symbol は無い（nm で 0 件） |
| virt | `make ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk BUILD=build/p023-virt -j16 vmunix` | exit 0、warning 0 |

QEMU は使っていない（vf2 の vmunix は virt では起動しない）。

## 実機の確かめ（未実施。ユーザーの目視、#15 (A) の答えの文字列で判定）

前提: p022（clock・reset の実機の確かめ）の後。

1. 試験の image を作る: `make ZEDBSD_CONFIG=plan/ws256/tests/vf2-dmatest/config-visionfive2-dmatest.mk BUILD=build/p023-vf2 -j16 build/p023-vf2/vmunix build/p023-vf2/root.img build/p023-vf2/uefi/BOOTRISCV64.EFI build/p023-vf2/disk.img`（p011 と同じ target）。
2. `build/p023-vf2/disk.img` を SD に書き、VF2 を SD で起動する。起動の log に `vf2-dmatest:` で始まる行（`no AXI DMA controller`・`unavailable`・`not published`）が出ない事を見る。
3. root で login し、他の操作をせずに `cat /dev/vf2-dmatest` を打つ（1 回、数秒）。続けてもう 1 回打つ（2 回の答えが同じか）。
4. 画面の 3 行をそのまま phase.md に写す（写真でよい）。判定:
   - `PASS COHERENT` → coherent。`VF2_DMA_DEFAULT_COHERENT` は 1 のまま。`CONTROL INCONCLUSIVE` の時は「陽性の対照は示せていない」と添える。
   - `FAIL … NONCOHERENT` か `FAIL copy` → 非 coherent（の疑い）。向き・run・offset を記録して Q1 へ。次の段は予備 (i)（`ALIAS OK` の時だけ採れる）、`ALIAS MISMATCH` なら (i) の代わりを設計し直す（§2.4 の 0）。
   - `FAIL alloc|physical|alias|channel` → 試験が始まらない。error を記録して Q1 へ。
5. 結果は bus master ごとに記録する（この試験は AXI DMAC。SDIO・GMAC・PCIe・表示は各 Phase の短い版）。

## 残り

- 実機の確かめ（上）。結果で `VF2_DMA_DEFAULT_COHERENT` を決める。
- 非 coherent なら予備 (i)（§2.4、この依頼の範囲外）。
- p019 の短い版（DMAC の cyclic）・各 bus master の短い版は、`vf2-dmatest-core.c` の手順を共有して各 Phase で足す（Minor 12）。
- 片付け（Q1 へ）: `build/p023-vf2`・`build/p023-vf2plain`・`build/p023-virt`・`build/p023-host`・`build/tmp/p023-*.log`（worktree `/home/awe/zedBSD-worktrees/p1` の中）。
