<!-- awesome-plan project=zedbsd record=ws073-p043 -->

# ws073-p043: BUG-119（Shut Down で電源が切れない）の修正: ACPI の S5 の電源断

Status: cleared（2026-09-30、サブエージェント、worktree `wt/ws073`。QEMU で電源断を確認、5330 の実機は未実施）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-119](../../bugs/BUG-119.md)
Queue: main の依頼（2026-09-30「BUG-119 をなるべく短時間で直して」）。Queue の ID は main が記録する
Resume point: なし（cleared）。残課題は下の「残課題」

## 範囲

[ws099-p008](../../ws099/phase008/phase.md) の確認（`/sbin/poweroff` の後も QEMU は `VM status: running`、全 CPU が HLT）と案に従い、
kernel に ACPI の S5 の電源断を足し、init が電源断を頼むようにする。HAL の API（`include/hal/hal.h`）は変えない。
greeter の「Shutting down…」の表示は ws099-p009（本 Phase の範囲外）。

## 原因

- init は poweroff でも `KERN_SYSTEM_HALT` を送り、`/dev/system` の ioctl は HALT と REBOOT だけだった。
- pcat の `kern_platform_halt()` は `hal_cpu_panic_all()` で CPU を止めるだけで、ACPI の S5（`\_S5` の SLP_TYP を PM1a/b_CNT に SLP_EN と共に書く）
  が kernel のどこにも無かった。`hal_poweroff()` は x86・amd64 の HAL に実装が無い。

## 修正

| 層 | file | 内容 |
| --- | --- | --- |
| UAPI | `include/uapi/system.h` | `KERN_SYSTEM_POWEROFF`（`_IO('s', 16)`。案の 6 は `KERN_SYSTEM_GET_RESOURCES` の番号なので避けた）。platform が電源を切れなければ `EOPNOTSUPP` |
| /dev/system | `src/drivers/generic/system-device.c` | `KERN_SYSTEM_POWEROFF`: pid 1 だけ、`system_shutdown_prepare()` の後に `kern_platform_poweroff()`。platform の失敗（ENODEV・ETIMEDOUT）は log に書いて `EOPNOTSUPP` に丸める（init が halt に切り替えられるように。準備の失敗はこれまでどおりその errno で、init が 5 秒後に再試行） |
| platform | `include/kern/platform.h`、`src/kern/platform/pcat.c`・`pc98.c`・`rpi4.c`・`sun4u.c`・`x68k.c` | `int kern_platform_poweroff(void)`（失敗のときだけ戻る）。pcat は `drv_acpi_poweroff()`。rpi4・x68k は `hal_poweroff()`（既存の HAL の実装）、pc98・sun4u は `EOPNOTSUPP`（sparcv9 の `hal_poweroff()` は fatal の stub なので呼ばない） |
| ACPI | `include/drivers/acpi/acpi.h`、`src/drivers/acpi/acpi-event.c` | `drv_acpi_events_init()` が起動時に `\_S5` を評価して SLP_TYPa・SLP_TYPb を `soft_off` に覚える（HW_REDUCED なら FADT の SLEEP_CONTROL_REG（I/O 空間のときだけ）も）。`drv_acpi_poweroff()`: `\_PTS(5)` を評価（無ければ省く、失敗は log だけ）→ event lock（SCI の thread を締め出し、この CPU の割り込みを止める）→ 固定 hardware なら PM1_EN と GPE を全て mask、WAK_STS を clear、PM1a/b_CNT に SLP_TYP を書き、次に SLP_TYP\|SLP_EN を書く（ACPICA と同じ 2 段）。HW_REDUCED なら sleep control register に 1 byte。monotonic の timer で 3 秒待って戻れば ETIMEDOUT |
| init | `userland/base/init/main.c` | `INIT_ACTION_POWEROFF` は `KERN_SYSTEM_POWEROFF` を送り、`EOPNOTSUPP`（古い kernel、電源を切れない機械）なら `KERN_SYSTEM_HALT` に切り替える |

案からの変更・判断:

- `\_PTS` は他の CPU を止める前（普通の thread の文脈、AML の interpreter が使える状態）で評価する。Linux・ACPICA も `_PTS` の後に CPU を止める。
  他の CPU は止めずに SLP_EN を書く: `system_shutdown_prepare()` が既に net・USB・PCI を止めており、chipset は CPU の状態に関わらず電源を切る。
- 割り込みを止めた後の待ちに `drv_acpi_os_stall()` は使えない（`kern_usleep_range()` は scheduler で寝る）ので、`drv_acpi_os_timer()` を読む spin にした。
- PM1b が無い機械は PM1a だけ（`pm1b_control.length == 0` で省く）。
- HW_REDUCED は SLEEP_CONTROL_REG が I/O 空間のときだけ対応（memory 空間なら ENODEV で halt）。5330（Alder Lake）は PCH の固定の ACPI hardware
  （PM1a_CNT）を持つ普通の PC で、HW_REDUCED ではないと見ている（実機の FADT は未確認）。

## 確認（QEMU、host の証拠。全て `build/ws073-p043/`）

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| amd64 の kernel の build | `make ZEDBSD_CONFIG=plan/ws073/tests/config-amd64-zdesktop-noclang.mk BUILD=build/ws073-p043 vmunix` | rc 0、warning 0（`kernel.log`・`kernel2.log`） |
| rpi4・pc98 の kernel の build | `make ZEDBSD_CONFIG=config/ci/config-{rpi4,pc98}.mk BUILD=build/ws073-p043-{rpi4,pc98} vmunix` | rc 0、warning 0（`others.rc`） |
| image の build（clang 無し） | `plan/ws073/tests/build-image-noclang.sh build/ws073-p043`、`plan/ws099/tests/build-criteria-image.sh build/ws073-p043-crit` | rc 0。`image.log` の warning 478 行は perl の locale・openssh・openssl・noct の既存のもので、変えた source の warning は 0 |
| `_S5` の読み取り | guest の `dmesg` | `ACPI: S5 is SLP_TYP 0/0`（QEMU の q35 の `\_S5` は {0,0,0,0}） |
| `/sbin/poweroff`（SSH） | `action-test.sh poweroff`（QMP の event を聞きながら SSH で送る） | 6 秒後に QEMU の process が終了。QMP の event `SHUTDOWN {'guest': True, 'reason': 'guest-shutdown'}`（ACPI の S5 で QEMU が終わった）。修正前（ws099-p008）は `VM status: running` のまま |
| `/sbin/reboot`（SSH） | `action-test.sh reboot` | 3 秒後に QEMU が終了、event `SHUTDOWN reason=guest-reset`（guest.py の `-no-reboot` で reset が終了になる。従来どおり） |
| greeter の Shut Down | `C1_REQUIRE_QEMU_EXIT=1 plan/ws099/tests/c1-boot-shutdown.sh build/ws073-p043-crit/hdd-image.img build/ws073-p043/c1-shots`（基準の image、自分の runtime `build/ws073-p043-run`） | `C1: PASS`、`C1-shutdown-qemu: PASS (QEMU ended)`、最後の絵は「Shutting down…」の card（`c1-shots/shutdown/seen-010.png`）。`C1-shutdown-order: WARN` は「greeter の log を機械が落ちる前に読めなかった」（電源が早く切れるようになったため。表示は ws099 の範囲） |
| boot test | `OUTPUT=build/ws073-p043/boot-test bash plan/tools/boot-test.sh build/ws073-p043/hdd-image.img` | PASS（`boot-test/login.png`、login prompt） |
| `git diff --check` | | 問題なし |

最初の C1（`build/ws073-p043/hdd-image.img`、console を残す zdesktop の config）は「kei の desktop に届かない」で FAIL したが、これは C1 が基準の image
（graphical の boot、kei の自動 login）を前提にするためで、基準の image で通った。

## 未実施

- 5330 の実機での電源断（ユーザーの目視）。実機の firmware の `\_S5`・`\_PTS` は QEMU と違う。
- x68k・sun4u の kernel の build（config が無い）。pc98・rpi4 は build だけ。

## 残課題

- 5330 の実機での電源断の確認（ユーザーの目視）。実機で電源が切れず halt に戻るなら、kernel の log の `ACPI: no _S5`・`no sleep control register`・
  `the platform did not turn off`・`system: the power-off failed` のどれかが出る。
- x68k・sun4u の platform の file は build していない（config が無い）。
- HW_REDUCED で sleep control register が memory 空間の機械は未対応（ENODEV → halt）。
