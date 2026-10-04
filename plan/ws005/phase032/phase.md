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

## 2026-10-04 19時50分の状態と委譲（Q1）

- 実装済み・main に統合: 5ecc43b（panic の可視化・kernel.log）、48cf8f6（scan の失敗からの restart、DMA の release の遅延）、97543a4（bus master off を quiesce・mmio_stop の後へ、PCI の MSI/MSI-X で INTx Disable、quiesce の TRANSPORT_FAILED を stop の失敗にしない）。main 85efbd4。
- 済んだ確認: host 試験（AX211 boot・core・runtime-start、PCI msi、host-wlan-retire）、build warning 0、T1-089（QEMU で kernel.log の永続化 PASS）。
- 未了: 5330 の AX211 passthrough での 10 分放置（queue の q697）、firmware の SW_ERROR の解析（q698）、実機の UAT（手順 (B): boot の行から `login=graphical`・`kmsg=quiet` を外し text console で放置、`/var/log/kernel.log.old`）。
- q697・q698 は 22 時まで別の session の P4 が実行する（queue.md の「2026-10-04 19時50分〜22時の委譲」）。

## q697 の確認（2026-10-04、委譲の session / P4）

- Attempt: q697-i01、in-progress。承認は Queue の「2026-10-04 19時50分〜22時の委譲」と本 session の user「P4 として実行してください」。
- 開始: 19:56 JST。worktree `/home/awe/zedBSD-worktrees/p4`、branch `agent/p4`、base `62de7d3`（`97543a4`・`85efbd4` を含む）、開始時 clean。Q1・他担当の再開は行わない。
- 指定の `plan/tools/guest/test-image.sh plan/uat/config-uat.mk build/q697` は exit 0、native image check OK（19:58 JST）。build log は `build/q697-build.log`。外部 Noct の return-type warning と submake jobserver warning があり、image 全体を warning 0 とは扱わない。
- 5330 の事前確認: SSH 可、AX211 の driver 無し、iwlwifi/iwlmvm blacklist 保持、SSH は USB Ethernet `enx6c1ff706148a`。QEMU 既存 process 無し。
- 試験は AX211 のみ、std VGA、gdbstub と host の ping/SSH で観測。原本は `plan/ws004/temp/q697/`（git 対象外）。結果は完了時に追記。

### q697 の条件の調整（2026-10-04 20時台、user の回答）

std VGA では `/dev/gpu0` が無く sessiond が console に戻るため、desktop の条件について本 session で確認した。user は「console での10分確認を採用する」と回答。q697 は AX211 のみ・std VGA の console で、保存済み WiFi profile 無しの scan を10分間観測し、元の (a) host 応答・(b) drain/mmio_stop の帰還・(c) restart と scan 再開・(d) vCPU の正常な待ちで判定する。q698 の範囲は不変。共有 Queue への投影は Q1 の統合時に依頼する。

補正: 開始直後は tracer にまだ AX211 event が無かったが、64 秒以降に scan の SW_ERROR と recovery/restart を観測した。harness の net.conf は wired 用だが、networking の起動で WiFi も enable される。初回の「scan 未開始」は結果でなく開始直後の観測だった。画面は `plan/ws004/temp/q697/desktop-start.png`。

### q697 の結果（2026-10-04 20:11 JST）

Attempt q697-i01: **cleared**（user が採用した console 条件）。whole ws005-p032 は実機 UAT 待ちのため in-progress を維持。

- 観測: 19:59:32〜20:10:43 JST、guest 走行 670.822 秒。console 到達時の画面は20:00台、終了まで10分以上。AX211 のみ、std VGA、12 vCPU・4 GiB、保存 WiFi profile 無し。
- (a) host: ping 成功 67 回、SSH 成功 67 回（約10秒間隔）、失敗記録無し。
- (b) gdb: `ax211_pci_interrupt_drain returned eax=0x0` 30 回、`drv_intel_ax211_mmio_stop returned eax=0x0` 30 回。以前 host が停止した bus master off の手前から全て帰還した。
- (c) klog の guest メモリ観測: SW_ERROR（hw=02000000）10 回、`restarted after recovery attempt=1 error=0` 10 回。その後また scan 中の次の SW_ERROR に到達しており scan 再開を確認。約66秒の周期は残る。
- (d) gdb の670.053秒の sample: **12 vCPU 全て `sched_idle`**。panic/fatal breakpoint hit 無し。
- stop の最初の回は `global stop deferred error=5 runtime-state=3 irq=0/0 dma-retained=1` を記録し、後続 cleanup の後に restart が成功する。即座の stop 成功とは報告しない。
- 終了: QEMU monitor の `quit`、runner exit 0、`restored 0000:00:14.3 to none`。host の WiFi blacklist は保持。
- image SHA256: `53256f6fc08a9e581a1f8d02196bae77eff1f45e46d6d76cdb4f5dca91dcd22d`。vmunix SHA256: `3c14a5fef10b4c477e154745f4cfb1e51257cc9f40e6f133a84e80d6faddc59e`。base `62de7d3`。
- 原本（git 対象外）: `plan/ws004/temp/q697/run1-trace.log`（SHA256 `57edacdd938e61dd2c0260f9ec93541f6f12471b1facdb5d4f77d42222e1a3ff`）、`run1-host-liveness.log`、`run1-final-monitor.txt`、`run1-klog-end-memory.txt`、`console-end.png`。klog は gdb/monitor でメモリから取得し、console/serial log を判定に使っていない。
- 制限: passthrough の証拠であり、素の実機 UAT・desktop・接続済み通信・長時間耐久の証拠ではない。q698 は残る SW_ERROR の解析。GitHub 公開・共有 Queue/WS/Master の投影は Q1 の統合時に行う。

## q698 の開始（2026-10-04 20:12 JST、P4）

q697 の結果 commit `c61b1c8` を前提に q698-i01 を開始。原因の特定と file:line・試験を含む修正計画までで、製品修正は行わない。調査用 source は temp に baseline と差分を保存し、最後に戻す。まず recovery の停止前に既存の LMAC/UMAC SRAM error table reader を呼び、失敗した scan の channel・flags・command ring の位置を記録する（DWARF 付き調査 build）。次に C1（discrete 用 LTR bootstrap）の除外比較。各 run は最大180秒を基本とし、観測を変えずに同じ条件を繰り返さない。q697 で firmware error の再現は10回確認済み。

q698 の最初の診断起動は59.5秒で中止（firmware error 前）。DWARF があると gdb の関数名 breakpoint が prologue 後へ移り、既存 tracer のレジスタ引数と `rsp` 上の return address の前提が崩れることを発見した。tool を symbol の正確な entry address に合わせ、DWARF の `info address` の表記にも対応させる。q697 は DWARF 無しで entry に置かれており影響無し。

### q698 の途中の観測（2026-10-04 20:23 JST）

- baseline 診断 run（20:15:24〜20:18:24、179.003秒）で同じ SW_ERROR を2回採取。両方 `channel=56 / phase=WAIT_START_ACK / token=0 / flags=0886 / channel-flags=00000000 / command-ring=1/0/1 / next-generation=258`。UMAC `id=20100247 / data=00001000/81000001/deadbeef / hcmd=00ff010d`、LMAC `id=00000071`（UMAC fatal の伝播）。各回 restart 成功。
- C1 除外 run（20:19:40〜20:20:59、78.401秒）も同じ channel・token・UMAC error で再現し restart 成功。LTR bootstrap の削除だけでは今回の SW_ERROR を防げない。
- code と OpenBSD `if_iwx.c` rev 1.230 の照合: AX210 以降の hardware pointer は65536で周回し、256 slot の index と分かれる。現 `intel-ax211-transport.c:1369` は slot 用の `command_ring.head` を doorbell に使い、256件目に0へ戻す。data TX は既に16 bit `write_sequence` を別に持つ。
- 同一 baseline image の gdb で CSR write 直前の edx だけを16 bitの連番に補正する原因確認を実施中。最初の `0xff → 0x100 → 0x101` を越えて scan 継続を観測した。製品の修正はしていない。複数周回と、補正を外す対照で因果を確かめてから結論にする。
- source の一時診断2ファイルは baseline に復元済み（`git diff --exit-code`）。image と診断差分は `plan/ws004/temp/q698/` の hash・diff と `build/q698-{diag,no-ltr}/` に保持。
- tracer の DWARF 対応は独立 WIP commit `2615f9d`。Python 3.13.5 syntax、GNU gdb 16.3 で DWARF 有り・無し両 kernel の exact function entry / klog symbol の gdb 検証が PASS。C の製品 source は含まない。LTO が引数を変形した関数の raw register は、disassembly と照合して読む。

### q698 の結果（2026-10-04、P4）

Attempt q698-i01: **cleared（原因解析と修正計画の範囲）**。[BUG-158「SW_ERROR の解析（q698）」](../../bugs/BUG-158.md#sw_error-の解析q698) に実測・source行・試験計画・raw hash・制限を保存した。

- 原因: 256個のDMA slot indexを16 bitのhardware command write pointerとして使い、`intel-ax211-transport.c:1369-1371` が256件目でdoorbellを0へ戻す。AX211は65536で周回する。
- baseline診断179.003秒でSW_ERROR2回、LTR除外78.401秒で同一SW_ERROR1回。いずれもUMAC `0x20100247`、channel56、pending token0 / next_generation258。C1除外だけでは解消しない。
- 同じbaseline imageで、CSR callback直前のedxだけをgdbで16 bit連番へ補正した199.088秒では、256・512の境界を越え、SW_ERROR/restartとも0、最終12 vCPU全てidle。
- 同じgdb観測で512件目から補正を外すと135.847秒のdoorbell0の後に同じUMAC assertが再現。今回はchannel8、pending token255 / next_generation513。channel固有でもgdbのpauseによる改善でもないことを確認した。
- 対照は152.084秒で終了。意図したassertの後のrestart attempt1に続き、scan stop command timeout(error42、error table valid0)を1回観測し、attempt2のrestart成功。この追加観測もticketに残した。回復全体の製品検証を済ませたとは扱わない。
- 修正計画: transportに16 bit command_write_sequenceを持ち、publish時だけ進める。slot/wire indexは256のまま。hardware resetで連番を初期化し、prepare/abortで消費せず、CSR write失敗は巻き戻さず既存のreset必須を維持する。実transportをリンクするhost試験（256/512/65536境界、abort、曖昧なwrite、reset）と、値の補正無しの修正imageで10分passthroughを計画。既存boot fixtureはpublishがstubである点に注意。
- 製品修正は未実装。調査用 `intel-ax211.c` と `intel-ax211-mmio.c` は元へ復元しdiff無し。tracer改善だけ `2615f9d`。HAL・toolchain変更無し、main編集・merge・push無し。
- 最終host確認20:28:02 JST: QEMU無し、AX211 driver無し、driver_override `(null)`、iwlwifi/iwlmvm blacklist保持、USB有線 `enx6c1ff706148a` でSSH応答。host電源再投入は今回不要だった。
- whole ws005-p032 は **in-progress**、BUG-158は **tracking**。次は通常のバグ修正QueueによるSW_ERROR修正と実機UAT。P4から次Queueを開始しない。共有Queue/WS/MasterとGitHubの反映はQ1への引き継ぎ事項。

## 委譲の session の引き継ぎ（2026-10-04 20:30 JST、P4 → Q1）

P4単独の委譲を終了。q697-i01・q698-i01は**ともにcleared（各Queueの限定範囲）**。P4は以後実行を続けず、次のQueueは選ばない。worktree `/home/awe/zedBSD-worktrees/p4`、branch `agent/p4`。mainのcheckoutは編集していない。merge・push・GitHub公開はしていない。

| commit | 内容 |
| --- | --- |
| `c61b1c8`（WIP） | q697のconsole10分確認。670.822秒、host ping/SSH各67回成功、drain/mmio_stop帰還各30回、SW_ERROR/restart各10回、最後の12 vCPUすべてidle。userの明示回答「console での10分確認を採用する」をPhaseへ記録 |
| `2615f9d`（WIP） | `plan/ws004/tests/ax211-gdb-trace.py` のDWARF対応。関数の正確なentryにbreakpointを置き、名前を別に保持。DWARF有り・無しのsymbol解決とentry一致、Python syntaxを確認 |
| `76556be`（WIP） | q698の結果・BUG-158の原因とfile:line/検証計画・Bug Boardの該当行。hardware command pointerを256で巻き戻す不具合を確定。LTR除外でも再現し、16 bit値へのgdb補正で199秒エラー無し、2周目で補正を外すと同じassertが別channelで再現 |

本節を含む最後のWIPは引き継ぎ記録だけ。上の3 commitはbase `62de7d3` から順に積んである。製品sourceの診断差分は復元済みで、`src/`・`include/` の差分無し。恒久の変更は担当の計画3ファイルとtracerだけ。最終の `git diff --check` はPASS。

**5330の残した状態**（20:28:02 JST確認）: QEMU停止、`0000:00:14.3` にdriver無し、driver_override `(null)`。`/etc/modprobe.d/vfio-ax211.conf` の `blacklist iwlwifi` / `blacklist iwlmvm` とinitramfsは維持。iGPUは渡していない。SSHはUSB有線 `enx6c1ff706148a`（10.0.30.3）、応答正常。今回hostの電源再投入は不要。remoteの `~/zedbsd-q697-p4/`・`~/zedbsd-q698-p4/` にimage/ELF/runner/trace、従前の `~/zedbsd-q684-p4/` も残る。起動中の作業は無い。localのrawは `plan/ws004/temp/q697/`・`q698/`（git対象外）、再現可能な要点とhashはticketに保存した。

**未了・Q1へ渡すもの**:

1. 製品のSW_ERROR修正は**未実装**。BUG-158「SW_ERROR の解析（q698）」の修正計画を、user指定の通常のバグ修正エージェント（Opus 5.5 Mid）の次の承認Queueへ選定する。gdb補正のrunを製品修正後の合格に流用しない。
2. 実機UATは未実施。ws005-p032全体はin-progress、BUG-158はtrackingを維持。対照runでassert後に1回観測したcommand timeout（error42、table valid0、restart attempt2成功）もticketに記録し、修正後のreset/再利用の検証へ渡す。
3. mainのQueueでq697/q698の結果、q697のconsole条件へのuser承認、T1-095と重なる確認の扱いを反映する。WS/Master/Past Log/必要なGitHub投影はQ1が統合する。P4は共有Boardを書き換えていない。


## q699 の開始（2026-10-04 20:35 JST、P4）

user「では、修正してください。」を、直前に提示したq698のcommand write pointer修正計画への実装指示として記録。[P4 lane](../../agents/P4/queue.md)にq699-i01をactive/in-progressで予約し、q698の過去の解析のみのscopeは保持した。P4単独で修正・host検証・既存の承認済みAX211 passthrough手順の10分確認まで行い、製品修正のcommitと結果をQ1へ渡す。Guardrail、全文coding-style、実sourceを再読。mainの状態はcleanで他の担当は停止中、P4のbaseはf1fa284。whole Phaseはin-progressを維持する。

### q699 の実装・host検証（2026-10-04 20:43 JST、P4）

- 製品修正と回帰試験を WIP `acb4afa` に保存。transport の command_write_sequence は16 bit、publish時だけ前進、init/確認済みdevice resetで0。DMA slot・wire indexは256のまま。曖昧なCSR失敗は連番を戻さずreset必須、prepare/abortは連番未消費。HAL・toolchain変更無し。
- `plan/ws004/tests/run-intel-ax211-transport-test.sh`: 実transportと抽出した実coreをリンクし、65537件のinline/external交互publish、256/512/65536の境界、wire index、invalid/stale/duplicate publish、abort、DMA sync失敗、CSR書込み失敗、active reset拒否、reset後の再利用を確認。ordinary/ASan/UBSanともPASS（GCC 14.2.0、C89、-Wall -Wextra -Werror）。旧transport（f1fa284）で同じfixtureを動かす対照は256件目のdoorbell=0 / expected=256で失敗し、回帰検知を確認した。
- 全文coding-styleを読み、変更したpublish・init/resetの段落・state・新fixtureを手で確認。C89宣言、実slotとhardware連番の分離、失敗時の所有権、呼出し/条件の分離、static宣言、tab引数、コメントを確認した。encode失敗は、prepareがpayload上限とnullを先に拒否するため有効な固定256 slotの入力では到達しない。fake DMA syncの失敗でprepare rollbackを実行確認した。
- clang-format 19.1.7を製品の編集範囲と新fixtureに適用し、規約のtab引数を保持。`style-diff.py --base f1fa284`（製品2ファイル）はchanged-line findings 0、新fixtureの`style-check.py --summary`はtotal 0、`sh -n`と`git diff --check`はPASS。
- `plan/tools/guest/test-image.sh plan/uat/config-uat.mk build/q699`: UAT image作成・image check PASS。image全体はsubmakeのjobserver警告1件、compiler警告/エラー0。最終ソースで同wrapperのtarget `build/q699/vmunix`を再ビルドし、kernel/include check PASS、警告0、実行中ELFのSHA256と一致。Clang 23.1.0（repo toolchain）、既存toolchainのみ使用。
- 補正無しの製品imageを5330で確認中。gdbのELF専用probeはCSR callback直前の値を読むだけで、register/guest dataを書き換えない。256・512・768件の実doorbell境界を通過、SW_ERROR/restart無し、host応答継続。console10分の最終結果は次の節に記録する。

### q699 の結果・引き継ぎ追補（2026-10-04 20:51 JST、P4 → Q1）

**q699-i01: cleared（承認済みcommand pointer修正と限定検証）**。製品/試験 WIP `acb4afa`、承認・checkpoint WIP `999eb8b`。原因と過去のq698証拠を保持したまま、[BUG-158「SW_ERROR の修正」](../../bugs/BUG-158.md#sw_error-の修正q6992026-10-04-p4)へ実装・最終結果・raw hashを追記した。Bug Boardの該当行も更新。

- 補正無しのUAT image、AX211単独/std VGA/12 vCPU/4GiB/profile無しで688.531秒。console capture間624.389秒（10分24秒）。実doorbell15→2582を2568件観測、256境界10回、飛び/巻戻り0。
- SW_ERROR/restart/recovery/panicは0。688.051秒の最終sampleで全12 vCPUがsched_idle。host ping/SSHは各70回成功・失敗0。console/serial logを判定に使用せず、guest memoryとread-only breakpointで確認。
- host最終確認20:49:54 JST: QEMU停止、AX211 driver無し、override(null)、blacklist保持、USB有線で応答。電源再投入不要。local rawはplan/ws004/temp/q699、remoteは~/zedbsd-q699-p4。試験・monitorは終了済み。
- q699のscopeは実装と今回の確認で満たした。whole Phaseはin-progress、BUG-158はtrackingを維持。実機UAT（未接続10分・接続通信・desktop/入力）は未了。q698のassert後error42の原因解明・実deviceへのreset注入試験を済ませたとはしない。
- 本追補は20:30の引き継ぎ後にuserが追加した実装指示の結果。以前の「未実装」は当時の履歴であり、現在はacb4afaで修正済み。main変更・merge・push・GitHub公開無し。Q1はbase f1fa284以降を統合し、共有Queueでq699の承認/結果、WS/Master/Past Logを投影する。次のQueueは開始しない。

## 実機の受け入れ（2026-10-04 夜、Q1）

ad-hoc UAT-3（uat-3 の image、main efe846a）で、ユーザー「WiFiは問題ないです。解決。」。Phase は **cleared**（Q1 の判定）。BUG-158 は resolved。i915 の graphical での panic の文字の表示（hook）は未実装のまま残り（別件）。
