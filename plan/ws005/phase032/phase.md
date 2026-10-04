<!-- awesome-plan project=zedbsd record=ws005-p032 -->
# ws005-p032: BUG-158 — panic を見える・残る形にし、AX211 の scan の失敗から off→on なしに戻る

Status: in-progress（q684-i01: 実装・T1-089 PASS、UAT 待ち。q684-i03（P3 generation9、2026-10-04）: P4 の原因の確定を受けた修正 (1)(2)(4) を実装・host 試験済み、5330 の AX211 passthrough の確認と UAT 待ち）
Disposition: normal
Parent: [WS005](../ws.md)
Bug: [BUG-158](../../bugs/BUG-158.md)（解析は同 ticket の「解析（2026-10-04、P3 / q684-i01）」と「割り込みの観点」）
Queue: q684（2026-10-04 user「BUG-158は…実装はOpus 5.5 Mid,テストはT1です。」「バグ修正はP3に移管します。」）。q684-i03: 2026-10-04 user「P4には、原因確定後、どのソースコードのどこを直すかまで計画してもらい、チケットに記録した上で、実装はOpus 5.5 Midの通常のバグ修正サブエージェントに回して、順番が回ってきたときに修正しましょう。」

## 範囲

BUG-158 の解析の直し方 (1)〜(4) を実装する。フリーズの直接の原因は未確定（実機の判定は次の UAT の (B)。QEMU の passthrough の gdbstub は P4 の q684-i02）。
この Phase は「次の UAT で panic か deadlock かを分けられる」ことと「scan の 1 回の失敗で WiFi が ENETDOWN のまま戻らない」機能の不具合の直しまで。

1. panic・fatal・未処理の supervisor fault を klog の ring に残し、graphical（`/dev/graphics` を取った後）でも text を画面に戻す。syslogd が kernel log を
   `/var/log/kernel.log` に差分で追記して `fsync` する（前回の起動の分は `kernel.log.old`）。
2. AX211: `ax211_radio_scan_stop` の失敗の分岐で network worker の中で `session_stop` を呼ばず、poll の recovery に回す。recovery の後に、net device が
   open のままなら、WLAN の退役の thread（network worker でない）で driver が自分で再 open する（off→on なし）。poisoned の command transaction は
   既存の runtime stop の device reset の後（`drv_intel_ax211_command_after_device_reset`）に解け、再 open で新しい transaction になる。
3. AX211: runtime stop で master-disable の表示が timeout した時は、その回は DMA を release せず STOP_REQUIRED に留め、次の close の retry で release する。
4. 割り込みの mask の順の確認（HAL は変えない）。

範囲外: HAL（`include/hal/hal.h`・`src/hal/`）、i915 の scanout を firmware の plane に戻す panic の hook（残り）、ISR での cause の claim（ticket の「割り込みの観点」の案）。

## 受け入れ

- kernel の build（`config/ci/config-amd64.mk`、AX211 が有効）が warning 0。syslogd の build が warning 0。
- host 試験: AX211 の core 試験（既存）と、共通 WLAN の退役の thread が restart を呼ぶ試験・command の poisoned → reset → 再利用の試験が PASS。
- style（`plan/ws073/tests/style-diff.py`）の変えた行の findings 0、`git diff --check` 空。
- QEMU（T1、Q1 経由）: 起動の回帰と syslogd の `kernel.log` の追記・前回分の保持（下の依頼）。panic を QEMU で起こす手段は無い。
- 実機（次の UAT、ユーザー）: 下の手順 (B)。AX211 の passthrough は Guardrail で停止中なので使わない。

## 実装

HAL（`include/hal/hal.h`・`src/hal/`）は変えていない。

| 直し | file | 変更 |
| --- | --- | --- |
| (1) | `src/kern/klog.c`・`include/kern/klog.h` | `kern_log_record_fatal()`: 停止の文を ring にだけ書く（console に出さない）。この CPU が `klog_lock` を持ったまま停止した時は lock なしで書く（`spin_trylock` の再帰の trap を避ける）。他の CPU が持つ時は上限つきで待つ |
| (1) | `src/kern/panic.c` | `__libc_panic`・`kern_fatal`: 割り込みを止め、`kernel panic: …`／`fatal: file:line: …` を ring に残してから `kern_text_reveal_fatal()` |
| (1) | `src/kern/user-probe.c` | `kernel_sys_fault_handler`（未処理の supervisor fault、`__builtin_trap` の ud2 も）: HAL が止める前に `fatal: supervisor fault vector= pc= address= error=` を ring に残し、console を戻す。返り値は今まで通り FAILED |
| (1) | `include/kern/text-display.h`・`src/kern/text-display.c` | `kern_text_ops` に任意の `reveal_fatal`、`kern_text_reveal_fatal()`（無い board は従来の `reveal`） |
| (1) | `src/drivers/platform/pcat/graphics/text.c`・`text.h` | `drv_pcat_text_reveal_fatal()`: `/dev/graphics` の enter が `text_ready = 0` にした後でも、surface が残っていれば `text_ready = 1` に戻し、framebuffer を黒にして保持中の cell を描く。`text_lock` は自 CPU 保持なら取らない |
| (1) | `userland/base/syslogd/main.c` | 起動時に `/var/log/kernel.log` を `kernel.log.old` へ移し、ring の全部を書く。以後 `poll` の 2 秒ごとに `kern.msgbuf_dropped`＋保持量の差分を追記して `fsync`（ring が先に捨てた分は「lost」の行）。`/var/log/messages` も 2 秒ごとに `fsync` |
| (2) | `src/drivers/wifi/intel-ax211/intel-ax211.c` `ax211_radio_scan_stop` | 失敗（COMMAND/TIMEOUT など）の分岐で `ax211_pci_session_stop` を呼ばず、`ax211_pci_recovery_latch_locked(error)` で poll の recovery に回し、scan の errno を返す（common は scan を失敗にして stop を retry。recovery の前は ENETDOWN、stop の後は runtime が止まって 0） |
| (2) | 同 `ax211_pci_recovery_run_locked` の末尾、`ax211_pci_reopen_request_locked` | net device が open（`net_opened`）なら `reopen_pending` を立て `wlan_station_restart_request()`。連続 `AX211_REOPEN_ATTEMPTS_MAX`（3）回まで。scan が最後まで完了したら回数を 0 に戻す |
| (2) | 同 `ax211_net_open` → `ax211_pci_open_locked(controller, restarting)` | open の本体を切り出し（停止済みの epoch の close → boot → runtime → tx ring → scan init → `wlan_station_open`）。restart の時は close の間に利用者が close したら ECANCELED |
| (2) | 同 `ax211_radio_restart`（新しい radio op）・`ax211_net_close` | WLAN の退役の thread で `open_locked(…, 1)`。失敗は上限の中で再要求。`net_close` は `net_opened`・`reopen_pending` を落とす |
| (2) | `include/kern/net/wlan.h`・`src/kern/net/wifi/wlan.c` | radio op `restart`（任意）と `wlan_station_restart_request()`。`wlan_retirement_run` が stop の後に、stop が残っておらず teardown（`stop_retry_disabled`）でなく active・control・lifecycle が空の station で 1 回呼ぶ。呼ぶ間は `stop_work_active` の pin で detach を締め出す |
| (2) | poisoned | 新しい code は無い。runtime stop の device reset の後の既存の `drv_intel_ax211_command_after_device_reset` が解き、再 open で新しい transaction になる（host 試験で確認） |
| (3) | `src/drivers/wifi/intel-ax211/intel-ax211-runtime-start.c`・`.h` | `ax211_runtime_start_stop_and_release`: mmio_stop の前に `master_disable_timed_out` を 0 にし、timeout したら最初の 1 回は DMA を release せず STOP_REQUIRED（`dma_release_deferred`）。次の cleanup（close の retry、bus master は off のまま、もう一度 reset）で release |
| (4) | 確認だけ | `ax211_pci_interrupt_drain` は `receive_enabled = 0` → CSR の mask（`transport_disable_interrupts`）→ MSI-X の disestablish → free → bus master off の順で、runtime stop の中で `mmio_stop`（SW reset）より前に呼ばれる。DMA の無い分岐（`!dma_prepared`、`!dma_exposed && !transport_bound`）は transport の bind（MSI-X の establish）より前なので handler は無い。`session_stop` の 2 度目の drain は no-op。code の変更は不要 |

### user のヒント（割り込み）の結果

前の generation の読み（[BUG-158](../../bugs/BUG-158.md) の「割り込みの観点」）を引き継いだ: AX211 は MSI-X 1 本・automask・ISR は latch と poll の予約だけで、
cause は worker が読んだ値のまま W1C する。未接続の間に上がる cause（RX、FH/SW/HW_ERROR、RF_KILL・CT_KILL・PERIODIC など）は ack されるか、error なら
rearm されずに automask のまま recovery に回る。ISR が取る lock は全部 irqsave の spin で、`lifecycle_lock`・`station->lock` は取らない。join・close_wait・
recovery・drain は spinlock を持たずに回る。**割り込みの嵐・ISR 起点の deadlock は読みでは起こせない。** この Phase の変更で増えた待ちは restart（退役の
thread が `lifecycle_lock` を持って firmware を起こす間、network worker が AX211 の op で mutex を待つ）で、利用者の open と同じ形（spinlock ではない）。

## 検証（2026-10-04、P3）

- build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p3-q684/ci build/p3-q684/ci/vmunix`（AX211・i915 が有効）→ exit 0、warning 0、
  `kernel include check: PASS`、`amd64 vmunix check: PASS`。`… build/p3-q684/ci/bin/syslogd` → exit 0、warning 0（sysroot は main の `build/amd64/sysroot` の複写）。
- host: `sh plan/ws005/tests/host-wlan-retire.sh` → `host-wlan-retire: PASS`（新しい case 5: restart の要求 2 回で 1 回、stop が残る間は呼ばない、stop の完了後に呼ぶ、
  teardown の後は呼ばない）。
- host: `sh plan/ws004/tests/run-intel-ax211-boot-test.sh` → PASS（新しい `test_command_poison_reset`: timeout → poisoned → submit 拒否 → 同じ epoch・transport の
  reset 前は解けない → quiesce と transport の reset → 新しい epoch で解けて submit が通る）。この script は main の時点で `-std=c89` の `vsnprintf` の未宣言で
  compile できなかった（変更前の tree で確認）ので `-D_DEFAULT_SOURCE` を足して直した。
- host: `sh plan/ws004/tests/run-intel-ax211-core-test.sh` → PASS（既存）。
- style: `python3 plan/ws073/tests/style-diff.py`（変えた C の全 file）→ findings on changed lines: 0。`git diff --check` 空。
- driver の段の「scan の timeout → recovery → 再 open → 次の scan」は host 試験に無い（PCI driver 本体は host で組めない。AX211 は QEMU に無く、5330 の passthrough は
  Guardrail で停止中）。読みで確かめた順: scan_stop が latch → common が stop を 1 tick ごとに retry（ENETDOWN）→ poll の recovery が session_stop
  （runtime 停止、device reset で poison が解ける）→ 次の retry は 0 → recovery が restart を要求 → 退役の thread（1 秒ごと）が close → boot → `wlan_station_open`。
- QEMU（T1）: 未実施（Q1 に依頼）。実機: 未実施（次の UAT）。

## QEMU の試験の依頼（T1、Q1 経由）

- image: `plan/ws005/tests/config-bug158.mk`（`config/ci/config-amd64.mk` の複写）での build だけ。`--file` の複写は無い。
- 試験と合否:
  1. `plan/tools/boot-test.sh` で login の画面まで（PNG）。
  2. guest で（serial か SSH）`ls -l /var/log/kernel.log` が在る。`dmesg | tail -n 5` の 5 行が、`sleep 3` の後の `tail -n 5 /var/log/kernel.log` と同じ。
  3. `reboot` の後、`/var/log/kernel.log.old` が在り、その先頭の行が 1 回目の起動の `kernel.log` の先頭の行と同じ。新しい `kernel.log` も在る。
- AX211 の経路（(2)・(3)）は QEMU に device が無いので対象外。panic の表示は QEMU で起こす手段が無いので対象外（未実施）。

## 実機の手順（次の UAT、ユーザー、5330）

- (B) text の console で放置: boot の行から `login=graphical`・`kmsg=quiet`・`logo=` を外して起動し、保存の profile 無しで `net wifi enable`、5 分放置。
  panic なら画面に `kernel panic:`／`fatal:` の行が残る。画面が凍るだけなら deadlock／停止側。
- graphical のまま再現した時: 電源の長押しで切り、再起動の後に `/var/log/kernel.log.old` の末尾を読む（最後の 2 秒以内の行は失われ得る）。見る行:
  `intel-ax211: scan stop … ; recovering`、`intel-ax211: recovery latched`、`intel-ax211: stopping connection`、`intel-ax211: restarted after recovery attempt=`、
  `intel-ax211: recovery left the interface down`、`fatal:`／`kernel panic:`（panic の行は停止の後は disk に届かないので、ここに無くても panic を否定しない）。
- 機能（(2)）: WiFi 未接続のまま放置した後、AP の在る所で off→on をせずに自動接続されること。`net wifi` の状態が ENETDOWN のまま残らないこと。
- 5330 の graphical（i915）では i915 が別の buffer を scanout しているので、text を戻して描いても画面に出ない可能性がある（i915 の hook は範囲外）。
  **graphical での panic の文字が実機で見えるかは未確認**。実機の判定の主は (B) と `kernel.log` の永続化。

## 残り

- i915 の panic の hook（panic の時に primary plane を firmware の framebuffer、または text を写した buffer に向ける）は未実装。graphical の実機で panic を
  画面で見るには要る（Q1 の判断で別の Phase）。
- フリーズの直接の原因は未確定（P4 の q684-i02 の gdbstub と、次の UAT の (B)・`kernel.log`）。
- ticket の「割り込みの観点」の案（ISR で cause の claim、RF_KILL の扱い）は未実装（原因が決まるまで後）。
- QEMU（T1）と実機（UAT）の確認が終わるまで、この Phase は cleared にしない。

## q684-i03: 原因の確定を受けた修正（2026-10-04、P3 generation9）

原因（[BUG-158](../../bugs/BUG-158.md) の P4 の節、5330 の passthrough と gdbstub）: 未接続の scan で firmware が assert（SW_ERROR）→ recovery の stop の
`ax211_pci_interrupt_drain` が RX DMA の進行中に PCI の Bus Master Enable を落とし、PCH 内蔵の CNVi で platform ごと止まる。

### 範囲

ticket の「修正の計画」(1) 必須・(4) recovery の EIO・(2) 推奨（PCI 層の INTx Disable）。(3)（stop で MSI-X を解放しない）は計画で任意・別 Phase とされており、入れない。
HAL（`include/hal/hal.h`・`src/hal/`）・toolchain は変えていない。PCI 層（`src/drivers/pci/pci.c`）の変更は Q1 の委任（起動の指示）。

### 受け入れ

- CI の kernel の build（AX211・i915 有効）が warning 0。
- host: AX211 の boot・core 試験、新しい runtime-start の stop の table 試験、PCI の MSI 試験が PASS。style の変えた行の findings 0、`git diff --check` 空。
- QEMU passthrough（5330、計画 (5)、Q1 経由）: 修正後の UAT 構成の image を profile 無しで 10 分放置し、host が落ちない、tracer に `ax211_pci_interrupt_drain returned`・
  `drv_intel_ax211_mmio_stop returned eax=0x0` が出る、klog に `restarted after recovery attempt=1 error=0` が出て scan が再び回る、10 分後に全 vCPU が正常な待ち。
- 実機（UAT）: WiFi 未接続のまま 10 分放置して固まらない。

### 実装

| 直し | file | 変更 |
| --- | --- | --- |
| (1) | `src/drivers/wifi/intel-ax211/intel-ax211-boot.h`・`intel-ax211-boot.c`・`intel-ax211-runtime-start.c` | boot ops に `bus_master_disable` を追加（ops の検査に NULL を足す）。runtime と boot の stop は drain → quiesce → `mmio_stop` が成功した後に `bus_master_disable`、その後に command の reset と DMA の release。drain/reset の失敗・master-disable の timeout の最初の stop・bus master off の失敗は bus master と DMA を残して STOP_REQUIRED（次の cleanup で reset → bus master off → release） |
| (1) | `src/drivers/wifi/intel-ax211/intel-ax211.c` | `ax211_pci_interrupt_drain` から bus master off を外す。`ax211_pci_bus_master_disable` を足して `ax211_runtime_start_ops.boot` に登録。`ax211_pci_session_stop` の coordinator の居ない IRQ の drain（firmware は走っていない）は drain の後に bus master off を続ける（前と同じ）。DMA barrier の comment を「quiesce・STOP_MASTER・SW_RESET の後の bus master off」に書き直した。attach・detach（`release_resources`）・`quarantine`・bind の巻き戻しの bus master off は firmware 未起動か reset 後なので変えていない（`quarantine` は attach の失敗だけから呼ばれ、recovery の `quarantined` は flag だけ） |
| (4) | `intel-ax211-runtime-start.c` | quiesce が `TRANSPORT_FAILED`（RX DMA は idle、command が残っていた。transport の quiesce でこの値を返すのはこの場合だけ）なら stop の失敗にしない。reset 後の `command_after_device_reset` が捨てる。run 6 の「global stop deferred error=5」を経ずに `session_stopped` になり、7e2225a の restart が働く |
| (2) | `src/drivers/pci/pci.c` | `drv_pci_device_establish_irq` が MSI/MSI-X の成功の後に INTx Disable を立てる（前から立っていれば触らない、cookie の `intx_disable_set` に印）。`drv_pci_device_disestablish_irq_checked` は capability を戻した後に、その cookie が立てた bit だけを戻す。config の access の失敗は guard を飛ばすだけ |
| 試験 | `plan/ws004/tests/intel-ax211-boot-test.c` | fake の `bus_master_disable`（trace `M`）。成功・部分失敗の unwind で `s` < `M` < `f`、drain/reset 失敗の stop で `M` が出ない、新しい `test_bus_master_after_reset`（bus master off の失敗で DMA を残し、cleanup で reset → `M` → release） |
| 試験 | `plan/ws004/tests/intel-ax211-runtime-start-test.c`・`run-intel-ax211-runtime-start-test.sh`（新） | running の session を直接作り、stop と cleanup の trace の table 7 行（健全 `iqsMzf`、command の残り → OK、RX 非 idle → TRANSPORT だが `M` の後に release、drain 失敗・reset 失敗・master-disable の timeout → `iqs` で保持し cleanup で `iqsMzf`、bus master off の失敗 → `iqsM` で保持） |
| 試験 | `plan/ws004/tests/pci-msi-test.c`・`run-pci-msi-test.sh`（新） | stale だった（hal_* の stub、kern_* の移行の後に link できない）ので kern_* に直した。MSI・MSI-X の establish で INTx Disable が立ち disestablish で戻る、前から立っていた bit は残る、を assert |

7e2225a（recovery → restart）との整合: restart の `ax211_pci_open_locked` → `ax211_pci_transport_bind` が bus master を立て直し、stop は reset の後に落とすので、
restart ごとの stop も同じ安全な順になる。(4) で recovery の stop が OK で終わるので、restart は遅れた close を待たずに要求される。

他の driver への影響（(2)、読み）: hda（MSI|INTX）・nvme（MSIX|MSI）・xhci（MSIX|MSI|INTX）・venus（MSIX|INTX）は全部 1 vector。INTx に fallback した時は
INTX cookie なので bit に触らない。command register を書く他の箇所（`restore_enable_state`・`pci_command_quiesce`・BAR の割り当て）は IO/MEM/MASTER の外の bit を
保つので INTx Disable を壊さない。

### 検証（2026-10-04、P3）

- build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p3-q684/ci build/p3-q684/ci/vmunix` → exit 0、`warning:` 0、`kernel include check: PASS`、`amd64 vmunix check: PASS`。
- host: `sh plan/ws004/tests/run-intel-ax211-boot-test.sh` → PASS（ordinary・ASan/UBSan・analyzer・amd64/i386 syntax）。`run-intel-ax211-core-test.sh` → PASS。
  `run-intel-ax211-runtime-start-test.sh` → `intel ax211 runtime stop: ordinary, ASan/UBSan PASS`。`run-pci-msi-test.sh` → `pci msi: ordinary, ASan/UBSan PASS`。
- style: `python3 plan/ws073/tests/style-diff.py`（変えた C の file）→ findings on changed lines: 0。`git diff --check` 空。
- QEMU passthrough: 未実施（Q1 に依頼）。実機: 未実施（UAT）。

### 残り

- (3) stop で MSI-X を解放しない（Linux と同じ形）は未実装（任意、別 Phase の候補）。
- firmware の SW_ERROR（hw cause 0x02000000）が起動から約 64 s で毎回起きる件は ticket の残課題のまま（(1) の後は recovery → restart で戻る見込み、約 1 分ごとに WiFi が途切れ得る）。
- `pci-shared-intx-test.c`・`pci-hcd-irq-teardown-test.c` も hal_* の stub のままで今の tree では link できない（この Phase で触っていない。直すなら別に）。
