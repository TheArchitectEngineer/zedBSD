<!-- awesome-plan project=zedbsd record=ws269-p004 -->

# ws269-p004 PMU（PD_GPUA）・CRG の GPU の行・組み込みの記述・uncached の別名（予備 (i)）・attach の早い段

- 親: [ws269](../ws.md)、設計: [design.md](../design.md) 改訂 3.1 の §3.1・§3.2・§3.6・§6 の p004 と §6 の末尾の必須項目
- Queue: q1330 / q1330-i01（P2、2026-10-19）
- Status: in-progress（2026-10-19 P2: 実装・build・host 試験まで。実機の確認（画面の 1 行）は未実施で、ユーザーが拾える試験の一覧に載せる文面を下に書いた。clearance は Q1 の判定）
- 前提: WS256 p013・p015・p022、p002（cleared）。p003 の引き継ぎ（CONFIG_DRIVER_POWERVR の既定 y）を含む。

## 目的と受け入れの条件

1. 新しい `vf2-pmu.c` で PD_GPUA だけを software の turn-on で入れる（他の PD に触らない）。
2. `vf2-crg` に GPU の行と親の行（ISP_2X・ISP_AXI、gpu_root の mux）。gpu_root を PLL2 に固定し gpu_core を 396 MHz。
3. 順: PD_GPUA → clock（axi → apb・rtc → gpu_root の PLL2・gpu_core の rate → core・sys）→ reset の解除（apb → doma）。reset 27 には触らない。
4. 組み込みの記述（`visionfive2.c`）。DT に enable の node があれば優先。coherency の既定は 0（`drv_fdt_dma_coherent(…, 0)`）、DMA は 32 bit。
5. `src/drivers/gpu/powervr/` の早い段（BVNC の読み出しまで）。36.50.54.182 以外は attach しない。
6. 予備 (i) の uncached の別名（WS256 design-peripherals.md §2.4 の予備 (i) の 1〜4）: 4 KiB の段、unmap で FLUSH64、別名で LR/SC・AMO を使わない。0a（`ALIAS OK`）の陽性の対照。kernel と user（D7 (a)）。
7. attach の失敗の後始末（reset を入れ直し、clock を逆順に落とし、PD_GPUA を off、1 行）。
8. CONFIG_DRIVER_POWERVR の既定を y、label を直し、`config/ci/config-visionfive2.mk` を作り直す（p003 の引き継ぎ）。
9. hal.h（HAL の API）は変えない。build（VF2・riscv64-virt・amd64）warning 0、host 試験。QEMU は起動しない。

## 実装（commit 0c7efd3a4）

| file | 中身 |
| --- | --- |
| `src/hal/riscv64/window.c`・`window.h`（新規） | `riscv64_window_fill_pages`: device window の 1 つの megapage の page の表に、範囲の page だけ leaf を書く（他の page は無効のまま）。hardware に触らない純粋な関数で host 試験できる |
| `src/hal/riscv64/space.c`・`space.h` | `riscv64_space_map_alias(physical, size)`: 範囲が触る megapage ごとに、初回だけ第 3 段の表を HAL の page から取り（lock の外で取り、lock の中で未設定なら結ぶ。取れなければ `HAL_ERR_NOMEM`）、範囲の page の leaf だけを書いて `riscv64_tlb_flush_everywhere()`。既に 2 MiB の leaf の megapage はそのまま。leaf は外さない。`map_device_blocks`: page の表を持つ megapage に MMIO の写像（`hal_space_map_device`）が来たら、表の 512 個を全部埋める（＝その megapage 全体。WS256 の dmatest が 2 MiB の別名を `hal_space_map_device` で写すのと両立させるため。設計の「別名の範囲への MMIO の要求は EINVAL」は採らなかった、下の「設計との違い」）。user の `HAL_SPACE_NOCACHE`（D7 (a)）: board に別名がある時だけ受け、EXEC との組は断る。`hal_space_map` は範囲を FLUSH64 してから leaf を別名（pa + offset）に向ける。`hal_space_prot_query` は page の cache の性質を保ち（leaf の PPN は変えない）、uncached の page を EXEC にする要求だけ断る |
| `src/hal/riscv64/page.c` | `hal_pmem_map_uncached`: board の別名の offset が 0 なら `HAL_ERR_UNSUPPORTED`（virt は従来どおり）。page 境界・managed RAM・window の範囲を確かめ、範囲を FLUSH64 してから `riscv64_space_map_alias`、`RISCV64_DEVICE_BASE + pa + offset` を返す。`hal_pmem_unmap_uncached`: 別名から物理を逆算して FLUSH64（leaf は残す、shootdown 不要）。関数の注に「別名には plain の load/store だけ、LR/SC・AMO を使わない」 |
| `src/hal/riscv64/bsp.h`、`bsp-visionfive2/board.c`、`bsp-virt/board.c` | BSP の内部の口 `riscv64_board_uncached_offset()`（VF2 は ccache がある時だけ 0x4_0000_0000、TRM Table 2-4 の memory port / system port。virt は 0）と `riscv64_board_flush_physical()`（VF2 は既存の FLUSH64、virt は何もしない）。hal.h は不変 |
| `src/drivers/platform/visionfive2/vf2-crg.c`・`.h` | SYS の行: 3 gpu_root（mux、PLL2/PLL1、非 shared）、20 isp_2x（mux＋divider 2〜8、shared）、21 isp_axi（divider 2〜4、shared）、45 gpu_core（divider 3〜7、settable）、46〜48・50 の gate、49 gpu_rtc_toggle（gate＋divider 12 固定、osc）。値は TRM Table 2-53・2-70・2-71・2-95〜2-100。新しい `drv_vf2_clock_set_pll()`（mux を PLL に向ける。既存の `set_parent` は clock の親しか扱えないため） |
| `src/drivers/platform/visionfive2/vf2-pmu.c`・`.h`（新規） | `drv_vf2_pmu_init`（`starfive,jh7110-pmu` を写す）、`drv_vf2_pmu_set_power`（`DRV_VF2_PD_GPU` だけ受け、他は EPERM。GPU の hw event（bit 7）を turn-on・turn-off の mask で止めてから、SOFT_ON/OFF に domain を書き、encourage に 0xff→0x05→0x50 / 0xff→0x0a→0xa0、0x80 の状態を上限付きで待つ）、`drv_vf2_pmu_powered` |
| `src/drivers/platform/visionfive2/vf2-gpu.c`・`.h`（新規） | glue。組み込みの記述を出発点に、DT が enable する node（`img,img-bxe-4-32`→`img,img-rogue`→`img-gpu`）があれば reg・interrupts を取り、coherency を `drv_fdt_dma_coherent(fdt, node, 0)`。dma_limit 0xffffffff。power_up: PD_GPUA on → 50 axi → 48 apb → 49 rtc → 3 を PLL2 → 45 を 396 MHz → 46 core → 47 sys → reset 21 → 22 → 45 の rate を読んで返す。どこで失敗しても stage を記録して power_down（22・21 を assert、47・46・49・48・50 を止め、PD off）。reset 27 には触らない |
| `src/drivers/gpu/powervr/powervr.c`・`powervr.h`（新規） | 早い段: glue の power_up → 1 MiB の register を写す → 0x20 の 64 bit（branch 63:48・version 47:32・units 31:16・config 15:0、v7.3 `pvr_rogue_cr_defs.h`）を読み、36.50.54.182 以外は power_down して 1 行・ENODEV → 0a の確かめ（64 KiB を 4 GiB の下に取り、uncached の別名を作り、A を cacheable で書いて FLUSH64 → 別名で A、B を別名で書いて FLUSH64 → cacheable で B）→ 1 行 `powervr: BXE 36.50.54.182 at <rate> Hz, ALIAS OK`（MISMATCH・UNAVAILABLE も同じ形）。成功の時は電源を入れたまま p005 に渡す |
| `src/kern/platform/visionfive2.c` | 組み込みの記述 `vf2_gpu_builtin`（0x1800_0000、1 MiB、IRQ 82）と `probe_soc` の最後で `drv_vf2_gpu_attach`（`#if CONFIG_DRIVER_POWERVR`） |
| `Makefile`、`platform/riscv64/vmunix.mk` | `CONFIG_DRIVER_POWERVR ?=`（visionfive2 で y）と `-DCONFIG_DRIVER_POWERVR`、y の時に vf2-pmu・vf2-gpu・powervr を build。`window.c` を HAL に |
| `config/drivers/architecture/riscv64.drivers` | 既定 y、label「PowerVR BXE-4-32 GPU (VisionFive 2, early stage; selects its firmware)」 |
| `config/ci/config-visionfive2.mk` | `CONFIG_DRIVER_POWERVR := y` と `powervr-firmware` を ZEDBSD_USER_PROGRAMS に（menuconfig で driver を on にした時と同じ結果を手で書いた。下の注） |
| `plan/ws269/tests/p004/` | host 試験（`run.sh`、`window-host-test.c`、`vf2-gpu-host-test.c`、`vf2-gpu.dts`） |

### 設計との違い・p004 で確かめた事

- **gpu_rtc_toggle の rate**: 設計は「OSC ÷ 3 = 8 MHz（推測）、p004 で TRM と照合」。TRM Table 2-99 は divider の Min・Max・Default が全部 12（24 MHz ÷ 12 = 2 MHz）。vendor の DT の `current-clock = <8000000>` とは合わない。**rate は設定せず、reset の値（12）のまま gate だけ開ける**。FW に渡す timer の値が要るのは p005 で、そこで FW の応答と合わせて確かめる（未確認）。
- **別名の範囲への MMIO の写像**: 設計（WS256 §2.4 予備 (i) の 2）は「別名の範囲に MMIO の写像の要求が来たら EINVAL」。WS256 の dmatest（`vf2-dmatest.c`）は試験のために 2 MiB の別名を `hal_space_map_device` で写すので、EINVAL にすると dmatest が壊れる。そこで「page の表を持つ megapage に MMIO の要求が来たら、表を全部埋める」にした（leaf の megapage は従来どおり）。dmatest は自分が allocator から取った 2 MiB の別名だけを写すので、OpenSBI・予約の別名に届かない性質は変わらない。
- **DT の node がある時の clock・reset・PD**: DT からは reg・interrupts・coherency だけを取り、clock・reset・PD の番号は SoC の固定の値（組み込み）を使う（vendor の DT の clock-names は vendor の名前で、番号は同じ）。
- **user の NOCACHE の prot**: 設計に書いていない細部。`hal_space_prot` は VM（fork の COW、reclaim）が cache の属性を付けずに呼ぶので、属性の食い違いで断らず page の cache の性質を保つ形にした。
- **PMU の turn-off の mask（0x08）**: TRM の register の表に無く（Table 3-12 は 0x04 の turn-on の mask だけ）、vendor の `jh71xx-pmu.c` の offset を Value referenced にした。GPU の event（bit 7）だけを 1（mask）にし、他の bit は変えない（vendor の「全部 0 / 全部 1」は写さない、§3.1）。
- **menuconfig での作り直し**: `tools/menuconfig.py` の load/save を script から呼ぶと `sh` が消え `CONFIG_AMDRDNA_CONTROL` が足される（TUI の外の差）ので使わず、driver を on にした時の `follow_driver_firmware` と同じ結果（`CONFIG_DRIVER_POWERVR := y`、`powervr-firmware`）を手で書いた。`make menuconfig-host-test` は PASS。
- **0a の位置**: 0a は GPU を使わない CPU だけの確かめなので、powervr の早い段で 1 回流す（WS256 p023 が要れば同じ HAL の口を使う）。0b・主の試験は p006（MIPS の道）・p008（data master の道）。

## 確かめ（2026-10-19、worktree /home/awe/zedBSD-worktrees/p2、host）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| VF2 の kernel | `timeout 1800 make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/ws269-p004-vf2 vmunix` | rc=0、`warning:`・`error:` 0 件。`-DCONFIG_DRIVER_POWERVR=1`、powervr.o・vf2-gpu.o・vf2-pmu.o が出来、vmunix に `powervr: BXE` の文字列 |
| riscv64-virt の kernel | `timeout 1800 make ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk BUILD=build/ws269-p004-virt vmunix` | rc=0、0 件 |
| amd64 の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws269-p004-amd64 vmunix` | rc=0、0 件（HAL の最後の 2 つの修正（page.c の順、space.c の prot）の前に build。amd64 は riscv64 の HAL を build しないので影響なし） |
| host 試験 | `timeout 120 sh plan/ws269/tests/p004/run.sh <out>` | PASS 65・FAIL 0（window 16、vf2-gpu の ok・unknown・mismatch の 3 通り）。PD の bit・mask・mux = PLL2・divider 3（1188/3 = 396 MHz）・gate 5 個・reset 21/22 の解除と 27 の不変・順（axi < apb < core < reset 21 < reset 22）・1 行の文面・unknown の後始末（PD off、reset 再 assert、clock 停止）・MISMATCH の行 |
| WS256 の SoC の host 試験（CRG を変えたので） | `timeout 120 sh plan/ws256/tests/vf2-soc/run.sh <out>` | `PASS: 0 failure(s)` |
| menuconfig（6 target） | `timeout 600 make menuconfig-host-test` | PASS（firmware licenses、round-trip、「the drivers' firmware selected with them」） |
| 空白 | `git diff --check` | 問題なし |

未実施: QEMU（起動しない指示）。実機（下の試験の一覧の文面）。VF2 の disk image の build（firmware の license の同意が要る。p003 で確かめ済みの道）。

## T1 への依頼（riscv64-virt の回帰、HAL の space.c・page.c を変えたため）

riscv64-virt の image で `plan/tools/boot-test.sh` の login prompt の確認 1 回（GPU なし、framebuffer）。virt の board は別名の offset が 0 なので `hal_pmem_map_uncached`・user の NOCACHE は従来どおり UNSUPPORTED、変わるのは `map_device_blocks` の分岐（virt では page の表を持つ megapage が出来ないので通らない）と `window.c` の追加だけ。config は `config/ci/config-riscv64-virt.mk`、commit 0c7efd3a4 以降。

## ユーザーが拾える試験の一覧への文面（今は頼まない）

- **WS269 p004（VisionFive 2、GPU の早い段）**: main の `config/ci/config-visionfive2.mk` で作った image を SD card で起動し（初回の build で LICENSE.powervr の同意が出る）、画面の boot の log に次の 1 行が出るかを見る: `powervr: BXE 36.50.54.182 at 396000000 Hz, ALIAS OK`。見る点は 3 つ: (1) `BXE 36.50.54.182`（違う番号なら `unknown core …; powered down`）、(2) 周波数（396000000 から外れていたら値を控える）、(3) 末尾が `ALIAS OK`（`ALIAS MISMATCH` か `ALIAS UNAVAILABLE (n)` なら控える）。`visionfive2: GPU unavailable (n)` や `vf2-gpu: no power management unit (n)` が出たらその行を控える。その後 login prompt まで起動が進む事も見る。

## 残件

- 実機の判定（上の文面、Q1 が一覧に載せる）。結果が `ALIAS MISMATCH` なら §3.2 の fallback（FLUSH64 だけで保つ道の再設計、p005 に入らず Q1 に返す）。
- gpu_rtc_toggle の rate（TRM の 12 固定 = 2 MHz と vendor の DT の 8 MHz の食い違い）を p005 で FW の応答と合わせて確かめる。
- 594 MHz は範囲外（design §3.1、Future Work の候補は Q1 に依頼済みの扱い）。
- 片付け（Q1 に依頼）: `/home/awe/zedBSD-worktrees/p2/build/ws269-p004-vf2/`、`build/ws269-p004-virt/`、`build/ws269-p004-amd64/`、`build/ws269-p004-vf2.log`、`build/ws269-p004-virt.log`、`build/ws269-p004-amd64.log`、`build/ws269-p004-menu.log`（いずれも worktree /home/awe/zedBSD-worktrees/p2 の中）。
