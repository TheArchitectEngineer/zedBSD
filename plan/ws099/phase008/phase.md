<!-- awesome-plan project=zedbsd record=ws099p008 -->

# ws099-p008: Shut Down で電源が切れない（BUG-119）の確認と直し方の案

Phase ID: `ws099-p008`
Parent: [WS099](../ws.md)
Status: cleared（2026-09-30、サブエージェント、worktree `wt/ws035`。範囲は確認と案まで、実装は main が別に割り当てる）
Phase disposition: normal
Bug: [BUG-119](../../bugs/BUG-119.md)
Queue: なし（2026-09-30 main の割り当て「p008: QEMU の monitor で電源断まで届いたかを確かめ、kernel の経路に原因があれば切り分けて報告。
直し方の案（どの file に何を足すか、UAPI、`include/hal/hal.h`・HAL の責務に触れるか）と、greeter の「Shutting down…」の案を phase.md に。
HAL の API の変更はユーザーの事前の承認が要るので、要るなら差分の案を plan に置くだけ」）

## 確認（QEMU の Venus、`build/ws099-p007-after.img`、`build/ws099-p008.sh`）

guest に SSH で `/sbin/poweroff`（greeter の Shut Down → sessiond → `/sbin/poweroff` と同じ）を送り、25 秒後に QMP（`human-monitor-command`）で見た:

| 見たもの | 結果 |
| --- | --- |
| `info status`（前・後） | `VM status: running` のまま（ACPI の S5 なら QEMU は終わるか `shutdown` になる） |
| QEMU の process | 生きている |
| `info registers -a` の RIP と HLT（vmunix の symbol に照らす） | CPU0 `amd64_lapic_panic_all+0x49`、CPU1〜3 `hal_cpu_park+0x30`、全て HLT=1 |

→ guest は電源断まで届いていない。CPU を全て止めて（halt）いるだけ。

## 原因（source の読み）

1. **init** `userland/base/init/main.c`（最後の system action、1199 行付近）: `INIT_ACTION_POWEROFF` も `KERN_SYSTEM_HALT` を送る
   （`system_action = action == INIT_ACTION_REBOOT ? KERN_SYSTEM_REBOOT : KERN_SYSTEM_HALT`）。
2. **UAPI** `include/uapi/system.h`: `/dev/system` の ioctl は `KERN_SYSTEM_HALT`（4）と `KERN_SYSTEM_REBOOT`（5）だけで、電源断が無い。
3. **kernel** `src/drivers/generic/system-device.c` → `kern_platform_halt()`、PC は `src/kern/platform/pcat.c` の `kern_platform_halt` が
   `hal_cpu_panic_all()`（全 CPU の停止）だけ。ACPI の S5（`\_S5` の SLP_TYP と PM1a/b_CNT の SLP_EN）を書く処理が kernel のどこにも無い
   （`src/drivers/acpi/` に SLP_TYP・`_S5` の扱いが無い。`acpi-event.c` は PM1 の control を SLP_EN を立てずに書くだけ）。
4. `include/hal/hal.h` の `hal_poweroff()` は宣言があるが、x86・amd64 の HAL に実装は無い（arm64・sparcv9・m68k だけ）。

## 直し方の案（実装は別の担当）

HAL の API（`include/hal/hal.h`・HAL の責務）は**変えない**案。ACPI は driver（`src/drivers/acpi/`）が持ち、電源断は kernel の platform の層で
ACPI の driver を呼ぶ（HAL に ACPI の知識を入れない）。

| 層 | file | 足すもの |
| --- | --- | --- |
| UAPI | `include/uapi/system.h` | `#define KERN_SYSTEM_POWEROFF _IO(KERN_SYSTEM_IOC_GROUP, 6)`（kernel の ABI の追加。HAL ではないが、UAPI の追加の手続きが Guardrail にあれば従う） |
| kernel（/dev/system） | `src/drivers/generic/system-device.c` | `case KERN_SYSTEM_POWEROFF`: HALT と同じく pid 1 だけ、`system_shutdown_prepare()` の後に `kern_platform_poweroff()` |
| kernel（platform） | `include/kern/platform.h`、`src/kern/platform/pcat.c`（と pc98・rpi4・x68k・sun4u） | `kern_platform_poweroff(void) __attribute__((noreturn))`。pcat は `drv_acpi_poweroff()` を呼び、戻れば（ACPI が無い・失敗）`kern_platform_halt()`。他の platform は `hal_poweroff()` がある所（arm64・x68k）はそれ、無い所は halt |
| ACPI の driver | `src/drivers/acpi/acpi-event.c`（PM1 の block を持つ所）、`include/drivers/acpi/acpi.h` | `drv_acpi_poweroff()`: 起動時（`drv_acpi_events_init` の後）に `\_S5` を `drv_acpi_evaluate` で評価して SLP_TYPa・SLP_TYPb を覚えておく（電源断の時は他の CPU が止まり AML を走らせたくないため）。電源断では、あれば `\_PTS(5)` を評価、割り込みを止め、PM1a_CNT に `(値 & ~SLP_TYP の mask) \| (SLP_TYPa << 10) \| SLP_EN`、PM1b があれば SLP_TYPb で同じ。書いた後は戻らない前提で待ち、一定時間で戻る（失敗） |
| init | `userland/base/init/main.c` | `INIT_ACTION_POWEROFF` は `KERN_SYSTEM_POWEROFF` を送る。`EOPNOTSUPP`（古い kernel）なら `KERN_SYSTEM_HALT` に戻す |

確かめ方（案）: QEMU で `/sbin/poweroff` の後に QEMU が終わる（`info status` が取れなくなる、guest.py の process が消える）。
`plan/ws099/tests/c1-boot-shutdown.sh` は「SSH が応じない」に加えて「QEMU が終わる」を見るように直す。実機（5330）は firmware の `\_S5`・`_PTS` が
違うので、実機で電源が切れることをユーザーの目視で確かめる（デモの台本 S14）。

HAL の差分の案: 不要（上の案は HAL の API に触れない）。もし x86 の `hal_poweroff()` を実装する方針にするなら、HAL が ACPI の表を読む責務を
持つことになるので、その場合は差分の案をユーザーの承認に回す。

## 電源を切る途中の表示（「Shutting down…」）の案

- 今: greeter の Shut Down・Restart を押すと `POWER poweroff|reboot` を sessiond に送るだけで、画面は変わらない。compositor が終わっても Venus の
  driver が最後の絵を保つので、電源が切れるまで greeter の絵がそのまま残る（押したことが分からない）。
- 案（`userland/desktop/wayland/greeter.c`、WS099 の compositor の source）: `greeter_power()` で状態を「電源を切る途中」にし、その frame から
  card の中身（利用者・password の欄・button）を消して「Shutting down…」（Restart なら「Restarting…」）と spinner を描き、Restart・Shut Down の
  button を消す。入力は受けない。次の frame を描いてから（present を待ってから）`POWER` を送ると、保たれる最後の絵が「Shutting down…」になる。
- セッションの側: App Home には Shut Down が無い（Log Out だけ）。付けるなら同じ表示を compositor の session の側にも作る（別の案件）。
- 文言は英語（UI の既存の言葉に合わせる）。「Kei」の名前は出さない。

## 記録

BUG-119 の ticket と Bug Board は main の担当（この Phase では変えていない）。

## Resume point

2026-09-30: cleared（確認と案）。実装は main が案を見て割り当てる: kernel・init・UAPI（High）、greeter の表示（WS099）。
