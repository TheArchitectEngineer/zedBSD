<!-- awesome-plan project=zedbsd record=ws269-p005 -->

# ws269-p005 遅い段: FW の parser・MIPS の page table・FW の起動・health の kick・IRQ

- 親: [ws269](../ws.md)、設計: [design.md](../design.md) 改訂 3.1 の §3.2〜§3.4・§6 の p005 と §6 の末尾の必須項目
- Queue: q1332 / q1332-i01（P2、2026-10-19、前半）
- Status: in-progress（2026-10-19 P2: 前半の実装・build・host 試験まで。実機の確認は WS269 p004 の実機の結果（beta2.md U27 の `ALIAS OK`）の後。D1 の 16 LW はその後の debug だけを数える）
- 前提: p003（cleared）、p004（実装済み・実機待ち）、WS256 p020（判定の操作）。

## 目的と受け入れの条件

1. FW の file の parser（末尾の header・section の表・device info の BRN・ERN・feature と値）。値は mainline v7.3 の FWIF（p002 の記録の blob）。host 試験で試せる形。
2. MIPS の FW の page table（4 KiB の page だけ。BRN71317 は host の定数として持つ）。host 試験で試せる形。
3. FW の image（file）を kernel の memory に保持する（recovery の reset で root を待たずに読み直す）。
4. FWIF の init の構造（connection・OS init・system init・OS data・system data・runtime・trace・HWR・fault page・utilisation）と KCCB・FWCCB。host と共有する物は全部 uncached の別名で CPU が触り、FW には run の PA を渡す。MIPS の PTE も uncached（mainline の `DEVICE_UNCACHED` と同じ）。KCCB の命令の値は mainline の FWIF から。
5. root を待つ遅い段の worker（上限 60 s）と、late register の EBUSY の上限付き再試行（100 ms 間隔 50 回の helper）。
6. 失敗の道: FW を止め（bounded の idle 待ち、soft reset）、IRQ を外し、memory を返し、reset・clock・PD を落として 1 行。EBUSY が尽きた時も同じ道（§6 の必須項目）。
7. FW の起動の試行（MIPS を reset から出し、`firmware_started` を待ち、health check を KCCB に送り、return slot と IRQ を確かめる）の code。実機の確認は p004 の実機の後。
8. firmware は逆アセンブルしない（header・device info の読みだけ）。出典は Guardrail の block 単位の `Value(s) referenced:` / `Procedure referenced:`。HAL の API は変えない。build（VF2・riscv64-virt・amd64）warning 0。QEMU は起動しない。

## 実装（commit fd0701927・a234f293a・050143511、試験 0eaf57a72）

| file | 中身 |
| --- | --- |
| `src/drivers/gpu/powervr/firmware-image.c`・`.h`（新規、host で build 可） | 末尾 4 KiB の header（版 3、header 48 byte、section 24 byte、open source の flag、major 1、BVNC の一致）、section の表（最大 8、種類ごとの allocation の大きさの和、各 section が allocation の中に収まる事）、device info（BRN・ERN の mask、feature の mask 2 word と値の列。値を持つ feature の位置は v7.3 `pvr_device_info.c` の表から 64 bit の mask で持つ）、host の BRN（36.50.54.182 の時だけ BRN71317 = `POWERVR_HOST_QUIRK_SMALL_PAGES`）、ELF の PT_LOAD を code・data に置く（file・section の外は EINVAL、memsz の残りは 0）。byte 単位の little endian の読みだけで struct を被せない |
| `mips-table.c`・`.h`（新規、host で build 可） | heap（MIPS の 0xC000_0000 から 16 MiB、最後の 192 KiB に connection・OS init・system init、予約 1 MiB）、PTE（PFN を 6 bit 上げ、bus が 32 bit 超なら mask 0x3FFFFFC0 と cached = write through、uncached = 2、dirty・valid・global）、map・unmap、boot loader の引数（stack・register・table の 4 page・page の log 12・数 4）、remap の窓の値 |
| `retry.c`・`.h`（新規、host で build 可） | EBUSY の時だけ pause を挟んで上限回まで再試行、他の答えは即座に返す |
| `interface.h`（新規） | FWIF の offset と大きさ（v7.3 `pvr_rogue_fwif_check.h` の OFFSET_CHECK・SIZE_CHECK の値）、命令の型（magic 0x2ABC、HEALTH_CHECK 115）、register（soft reset・SLC・ACE-Lite・MIPS wrapper・remap・MTS・stop の待ちの register）。名前は zedBSD の独自の物 |
| `powervr-private.h`（新規） | `struct powervr_core` を powervr.c から移し、FW の状態（保持した file、image、heap、20 個の block、FWCCB の読み位置、IRQ の数）を足した |
| `firmware-memory.c`（新規） | block = 4 GiB の下の連続の run（`kern_pmem_alloc_limited`）+ `kern_pmem_map_uncached` の別名。0 で埋め、MIPS の table に uncached で写す。固定の位置（code 0、data は private の base、config の 3 つ）と一般の位置（cursor）。CPU の読み書きは別名への plain な 32/64 bit・byte の load/store だけ |
| `firmware-file.c`・`.h`（新規） | `/lib/firmware/powervr/rogue_36.50.54.182_v1.fw` を root を 60 s まで待って全部読む（root が来なければ ETIMEDOUT、無ければ ENOENT） |
| `firmware-boot.c`（新規） | 遅い段の thread: 読む → parse（BVNC は早い段で読んだ物）→ MIPS・bus 幅 33〜40 を確かめる → heap・code・data・program の load・boot の引数 → KCCB（128）・FWCCB（32）・return slot → 記録（v7.3 `pvr_fw.c` の初期値の手順）→ IRQ 82 の handler（MIPS wrapper の status を読み clear して数えるだけ、lock なし）→ start（secure bus を外す、全部 soft reset → Rascal・dust → Garten 以外 → SLC・wrapper（remap 1・2・3、BRN63553 の時 5）・ACE-Lite → 3 us → 全部解除 → 3 us）→ `firmware_started` を 5 s まで待つ → health check（slot に命令、return を 0、barrier、write offset、MTS に 0）を 1 s まで待つ → FWCCB を空ける → 1 行。失敗は stop（data master の解除、BIF・SLC の idle を各 1 s、MARS の時は CPU・SYSARB0 の idle、全部 soft reset）→ IRQ を外す → block・table・保持した file を返す → `powervr_power_down`（register の窓を外し、glue が reset・clock・PD を落とす）→ 1 行 |
| `powervr.c` | struct を private header へ。register の読み書き（32/64）と `powervr_power_down` を公開の関数に。早い段の最後で `ALIAS OK` の時だけ `powervr_late_start` |
| `platform/riscv64/vmunix.mk` | 新しい 7 file を `CONFIG_DRIVER_POWERVR` の時に build |
| `plan/ws269/tests/p005/`（新規） | `run.sh`・`powervr-host-test.c` |
| `plan/ws269/tests/p004/vf2-gpu-host-test.c` | `kern_mmio_write64` と `powervr_late_start` の stub、ALIAS OK の時だけ遅い段が始まる事の確かめ |

### 設計との違い・決めた事

- **late register の EBUSY**: helper（`powervr_retry_busy`、50 回・pause は呼び手が 100 ms を渡す）と host 試験まで。p005 では登録する render の node が無い（ops は p006）ので、worker からの呼び出しは p006 で `drv_gpu_register` に結ぶ。EBUSY が尽きた時は同じ `tear_down`（stop → IRQ → memory → power down）を通す（関数は出来ている）。
- **FWCCB の応答**: health check は FWCCB に何も返さず、KCCB の return slot に EXECUTED（bit 0）が立つ（v7.3 `pvr_ccb.c`）。判定は return slot と IRQ の数。FWCCB は空けて magic の付いた命令の数を数えるだけ（処理は p006）。
- **power-domain b**: host は触らない（design §3.1）。FW が自分で扱うかは実機の boot の結果で分かる（未確認）。
- **gpu_rtc_toggle の rate**（p004 の残件）: FW に渡す clock は core の rate（`clk_get_rate` に当たる、396 MHz）だけで、timer の rate を渡す field は v7.3 の init の構造に無い。実機で `boot` が時間切れになった時の調べる点として残す（未確認）。
- **trace**: buffer（12000 word）と control は作るが log type は 0（mainline の既定）。FW の挙動を調べる時に log type を変えて buffer を読む（逆アセンブルの代わり）。
- **soft reset の 2 番目の register**（XE_TPU2）・PBE2 の mask・secure bus（SYS_BUS_SECURE_RESET）・LAYOUT_MARS（値 1）の stop は FW の file の feature で選ぶ。実 file の値: MIPS、bus 36、LAYOUT_MARS 1、PBE2_IN_XE、XE_TPU2、SYS_BUS_SECURE_RESET、GPU_MULTICORE_SUPPORT、XT_TOP なし、SLC 16 KiB・line 512 bit、cluster 1（host 試験の real の節）。
- **BRN71242**: GPU_MULTICORE_SUPPORT があるので SLC の lazy write-back の上書きはしない（v7.3 `rogue_slc_init` と同じ判定、design §0.4 の推測と一致）。

## 確かめ（2026-10-19、worktree /home/awe/zedBSD-worktrees/p2、host）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| VF2 の kernel | `timeout 1800 make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/ws269-p005-vf2 vmunix` | rc=0、`warning:`・`error:` 0 件。新しい 7 file の .o が出来、vmunix に `powervr: firmware %s failed` と `... running, health check ok` の文字列 |
| riscv64-virt の kernel | `timeout 1800 make ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk BUILD=build/ws269-p005-virt vmunix` | rc=0、0 件 |
| amd64 の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws269-p005-amd64 vmunix` | rc=0、0 件 |
| host 試験（p005） | `timeout 120 sh plan/ws269/tests/p005/run.sh build/ws269-p005-host build/ws269-p002/fw/rogue_36.50.54.182_v1.fw` | `PASS 99 FAIL 0`。合成の file（parse、版・section・BRN・feature と値、host の BRN、load の配置と bss の 0、拒否 14 通り）、実 file（131072 byte、v1.1 build 6976702、section 6、code 102400・data 20480、BRN mask 0xa00、上の feature、data の heap offset 0x32000、boot code の offset 98304、load 成功）、MIPS の table（heap の配置、PTE の bit、map・unmap・拒否 5 通り、boot の引数の offset、remap）、retry（4 通り） |
| host 試験（p004、変更の追従） | `timeout 120 sh plan/ws269/tests/p004/run.sh build/ws269-p005-p004host` | PASS 67・FAIL 0（ALIAS OK の時だけ遅い段が始まる事を足した） |
| 空白 | `git diff --check` | 問題なし |

実 file は p002 が取った物（sha256 が S6・mirror の値と同じ）。firmware は逆アセンブルしていない（header・section の表・device info・ELF の program header だけを読む）。

未実施: QEMU（起動しない指示、QEMU に PowerVR は無い）。実機（下の文面）。

## 必須項目: `hw.gpu.attaching`（2026-10-20 P1、q1356、design.md 末尾の Q1 の追記）

- `powervr.c` `drv_powervr_attach`: ALIAS OK の後、`powervr_late_start` の直前に `kern_gpu_attach_begin()`。start の失敗では `kern_gpu_attach_end()` してから power down。ALIAS が OK でない時・未知の core・power up・map の失敗では数えない（遅い段が始まらない）。
- `firmware-boot.c` `late_thread`: FW の失敗（file・bring_up）、render の node の失敗（`powervr_render_start`、EBUSY の再試行が尽きた時を含む）、render の node の登録の成功、の 3 つの出口すべてで `kern_gpu_attach_end()` を 1 回。i915（settled）・nvrtx（worker の終わり）と同じ「登録か、登録されない事が決まった時」に -1。
- 試す: `timeout 120 sh plan/ws269/tests/p004/run.sh build/q1356-p004host` → PASS 70・FAIL 0（ok で begin 1・end 0、mismatch と unknown で 0・0 の 3 件を足した。early の段を compile するのは p004 の試験なので、数の case はそこに置いた。late_thread は host で build しない）。build は [ws270 p006a](../../ws270/phase006a/phase.md) の表（VF2 rc=0・warning 0、firmware-boot.c・powervr.c を compile）。
- sessiond 側（gpu0 があっても attaching の間は待つ）は WS270 p006a。

## 実機の試験の文面（今は頼まない。p004 の実機で `ALIAS OK` が出た後）

- **WS269 p005（VisionFive 2、GPU の firmware の起動）**: main の `config/ci/config-visionfive2.mk` で作った image を SD card で起動し、画面の boot の log で、p004 の行 `powervr: BXE 36.50.54.182 at 396000000 Hz, ALIAS OK` の後（root の mount の後、数秒以内）に次の 1 行が出るかを見る: `powervr: firmware 1.1 build 6976702 running, health check ok, <n> interrupts, <m> replies`。見る点: (1) `running, health check ok` が出るか、(2) `<n>`（1 以上なら IRQ 82 が届いている）と `<m>` の数を控える。代わりに `powervr: firmware <段> failed (<番号>); powered down` が出たら、その行をそのまま控える（段は file・image・memory・interrupt・start・boot・health check、番号は 42 = 時間切れ、6 = file が無い、22 = core・版の違い、3 = file の形の誤り、4 = memory 不足）。その後 login prompt まで起動が進む事も見る。

## 残件

- 実機の判定（上の文面、Q1 が一覧に載せる）。`boot` の時間切れなら D1 の debug（FW の trace の log type を上げて buffer を読む、MIPS wrapper の register、gpu_rtc_toggle の rate）。D1 の 16 LW はここから数える。
- late register の EBUSY の helper を `drv_gpu_register` に結ぶのは p006（render の node の ops と一緒に）。
- 片付け（Q1 に依頼）: worktree /home/awe/zedBSD-worktrees/p2 の `build/ws269-p005-vf2/`・`build/ws269-p005-virt/`・`build/ws269-p005-amd64/`・`build/ws269-p005-host/`・`build/ws269-p005-p004host/`・`build/ws269-p005/`（v7.3 の参照用の取得物）・`build/ws269-p005-vf2.log`・`build/ws269-p005-virt.log`・`build/ws269-p005-amd64.log`。
