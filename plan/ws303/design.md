<!-- awesome-plan project=zedbsd record=ws303-design -->

# WS303 設計: 熱と fan（温度の読み・fan の回転数・strategy・非常の停止）

- 版: **第 5 版**（2026-10-29 P5、q1666-i03）。第 4 版までの「改訂の節を本文の上に重ねる」形をやめ、**一つの本文**にまとめた（第 4 版の review [design-v4-review.md](design-v4-review.md) の B1）。前の版は git の履歴にあり、各版の改訂（第 2 版 R1〜R13、第 3 版 T1〜T7、第 4 版 V1〜V10）の行き先は §11。
  review: 第 1 版 [design-review.md](design-review.md)、第 3 版 [design-v3-review.md](design-v3-review.md)、第 4 版 [design-v4-review.md](design-v4-review.md)。第 5 版の review は design-v5-review.md（未実施）。
- 基点: worktree `p5-design-c`（main d94ceac00）。第 4 版までの file:行 は `p5-design-b`（63386d8a4）の物で、引用の source はこの間に変わっていない（第 4 版の review が `git diff --stat` で確かめた）。code は書いていない。
- 調べた物: zedBSD の source（§1）、5330 の ACPI の表（`scratchpad/ws303-acpi/`、git に入れない）、5330 の Debian の読み（2026-10-29 02:42 と、WS301 の読みの TjMax、§2.3）、Fable の調査（公開の資料だけ、§2.4）。

## 0. ユーザーの決定（原文）と解釈

| 日付・問い | 原文 | 解釈（P5） |
| --- | --- | --- |
| 2026-10-27 | 「DVFSとP-core/E-coreは、/dev/systemに入れるのが適切ならそれでもいいですとお伝えください。ファン制御もあるといいですね。」、（クリック）fan は WS301 とは別の WS | この WS の発端 |
| Q303-1 | 「/dev/thermal」 | 新しい device `/dev/thermal`。温度・fan・strategy・boost・事象を 1 つに |
| Q303-2 | 「rootだけが変えられるが、sessiond経由でも変えられる。/sbin/thermalからDVFSストラテジを選択。強制的にファンを大きくする（何段階かあり）ことはできるが、実際の温度に対して必要なファン回転数を下回らせるオーバーライドは不能。」 | 変更は root と sessiond 経由。`/sbin/thermal` が strategy を選ぶ。fan は上げる段（boost）だけ、実際の段 = max(自動, boost) |
| Q303-3 | 「さっき書いた/sbin/thermal」 | 道具は `/sbin/thermal` |
| Q303-4 | 「profile を作る」 | Pi 5 にも strategy の受け手（§4.9） |
| Q303-5 | 「温度に対して必要な回転数を下回らせることはできず、回転を上げる。非常水準に達したとき、電源を落とす。コントロールできないときはファームウェアにまかせたり、ストラテジだけファームウェアに伝える。」 | 非常の水準で電源を落とす（§4.6）。OS が fan を制御できない機械（5330）は firmware に任せ、strategy だけを firmware に伝える（Dell の thermal mode） |
| Q303-6 | 「入れる」 | passive（WS301 の上限を下げる）を入れる（§4.7） |
| Q303-7 | 「Debian で読む (推し)」 | Dell の mode の符号は zedBSD で set → Debian で読みだけ（§6.3） |
| Q303-8 | 「よい (推し)」 | 5330 の BIOS の Thermal Management を試験で書き換えてよい、終わりに performance へ戻す |
| Q303-9/10（Q1 の要約の形） | 「/dev/thermal に移す・root/wheel」（Q1 の注: monitor も /dev/thermal に移し hw.thermal の sysctl は後で外す、sessiond 経由は root か wheel） | `hw.thermal` は monitor と sysctl の道具を移した後に外す（p009）。「後で」を同じ WS の p009 と読んだのは解釈（原文が要約の形なので Q1 に確かめる）。sessiond 経由は root か wheel の user |
| Q303-11 | 「ストラテジーはかなり抽象化されたものとして、トータルの「performance or balanced or power save or manual」と、マニュアルなら実行速度、騒音、細粒度パワーゲートの3項目を0-100で設定可能に。具体的なプラットフォームへのマッピングはドライバが決める。」 | strategy は 4 つ。manual の時だけ 3 つの軸（各 0〜100）。各 driver（受け手）が自分の対応を決める（§4.4） |
| Q303-12 | 「GPL の値を出典つきで (推し)」 | 5330 の SMM の fan の state の値は Linux の hwmon の driver（GPL）から block ごとに出典つきで取る。手順は公開の文書と AML から組む（§8） |
| Q303-13/14（Q1 の要約の形） | 「推しの組 (推し)」（第 4 版の Q303-13 の A、Q303-14 の A） | 非常の停止の水準は ACPI の thermal zone の `_CRT` と DT の critical だけ（5330 は firmware、Pi 5 は 110 °C）。passive は仕組みと host 試験まで、実機の受け入れは passive の水準が critical より下の機械が来た時 |
| Q1 の決定 | AML の harness は p002 で `plan/tools/aml-host/` に移す。`/dev/acpi` は BUG-351 | |
| WS301 Q301-7 | 「持たせない (推し)」（Q1 の注: 利用者が選ぶのは WS303 の strategy だけ、CPU ごとの直の値は root の診断用だけ） | WS301 は自分の policy を持たず strategy の受け手。WS301 の直の設定は root の診断（§4.4 の CUSTOM） |

解釈で確かめたい所: Q303-9 の「後で」（上）、Dell の Quiet（firmware が fan を遅くする mode）を noise の軸で選んでよいか（Q303-19、§9。原文 Q303-5「ストラテジだけファームウェアに伝える」で支えられると読んだ）。

## 1. 今の振る舞い（code の事実）

- 温度: `src/drivers/acpi/acpi-thermal.c`。thermal zone と `_TMP` を持つ device を集め（`thermal_visitor` 134〜190）、5 秒ごとに `_TMP`・`_PSV`・`_CRT` を読み（`THERMAL_PERIOD_MS` 38、`thermal_thread` 235〜270）、`kern_thermal_register` で `hw.thermal` に出す（112）。`_PSV`・`_CRT` は読むだけ。sensor の上限 16（37）。
- Pi 5: `src/drivers/platform/rpi5/rpi5-fan.c`。自分の thread（既定 1 秒、68）で温度を読み（`record_temperature` 1004〜1029、読めない時は `time_ns = 0` と有効の flag を落とす）、device tree の trip と cooling map から level を選び RP1 の PWM に duty を書く（`poll_once` 1155〜1195）、回転数は `read_rpm`（1139〜1152、log だけ）。`hw.thermal` に 1 つ（330、`THERMAL_KIND_ZONE`・`THERMAL_FLAG_CPU` 391〜393）。level・rpm を外へ出す口は無い。
- `hw.thermal`: UAPI `include/uapi/sysctl.h:240〜313`（`HW_THERMAL 9`、`thermal_header` 24 byte、`thermal_entry` 64 byte）、registry `src/kern/sysctl.c:146〜245`（source 4、1 source 16 sensor）、`include/kern/sysctl.h:82〜99`。
  **読み手は 2 つだけ**: `userland/base/sysctl/main.c`（32・99・222〜223・697〜745）と `userland/desktop/libkeiland-backend-zedbsd/monitor-zedbsd.c`（643〜681、`sizeof(struct thermal_entry)` で進む 672）。試験・道具・文書からの参照は無い（第 4 版の review の m6 の grep）。
- ACPI: 自前の AML の interpreter（`src/drivers/acpi/aml-*.c`）。引数つきの評価（`drv_acpi_evaluate`）、Notify の handler、SystemMemory・SystemIO の handler、EC（`acpi-ec.c`、取引は poll の上限つき 708〜735）。全ての AML は 1 つの interpreter の lock（`mutex_lock`、`acpi-kern.c:421〜426`）で直列、Sleep・Acquire・Wait の間は lock を離す（`aml-sync.c:13〜16`）。時間つきの取得の口は無い（`mutex_trylock` は kernel にある、`include/kern/lock.h:118`）。
  電源断 `drv_acpi_poweroff` は `\_PTS` を評価してから（`acpi-event.c:1041〜1048`）event の lock（1056）を取り SLP_TYP・SLP_EN を書く。5330 の `_PTS` は EC の読み書きと SMI を走らせる（`dsdt.dsl:27540〜`）。
- WMI（PNP0C14）の driver・SMBIOS の表の読みは無い。
- `/dev/system`: `src/drivers/generic/system-device.c`（登録は `src/kern/vfs.c:466`、root の判定は `cred_is_superuser` 333〜337）。devfs の node の mode は名前で決まり既定 0666（`devfs.c:589〜601`、`input-inject` は 0600 の前例 596〜597）。使用中の device の番号は 0x00010000〜0x00010007・0x00010010〜・0x00010020（第 4 版の review の m7）。
  事象は `src/kern/system-event.c`（class の bit は 1 つの名前空間 `KERN_SYSTEM_EVENT_CLASSES 0xff`、ring は `KERN_SYSTEM_EVENT_QUEUE` の 64、`system.h:315〜338`、record `struct system_event` 128 byte 356〜368）。
- init: SIGHUP・SIGINT（reboot）・SIGTERM（halt）だけに handler（`userland/base/init/main.c:293〜296`、2470〜2480）。service は最大 32（38）、1 service の停止の猶予 `STOP_GRACE_MS` 5 秒 × 2（69）、base の service は 7（`userland/base/init/services`）。
  SIGUSR2 の既定の動作は終了（既定で無視は SIGCHLD・SIGCONT・SIGURG・SIGWINCH だけ、`src/kern/signal.c:1414〜1428`）。kernel に pid 1 を既定の終了から守る code は無い（`process.c:2705` は reaper の選びだけ）。kernel から送る `signal_send_process` は `struct process *` を取る（`signal.c:435`、`process_find_ref(1)` が要る `include/kern/process.h:207`）。
- sessiond の POWER の要求は root か wheel（`userland/desktop/sessiond/power-rules.c` の `sessiond_power_decide`）。
- 試験の注入の前例: `CONFIG_INPUT_TEST_INJECT`（`config/kernel-options.list:4`、`Makefile:414・947〜948`）。
- AML の host 試験: `plan/ws049/tests/`（`aml-host`、5330 の表、EC と SMI の port の模擬）。p002 で `plan/tools/aml-host/` に移す（Q1 の決定）。

## 2. 調べた事実

### 2.1 5330 の ACPI（BIOS 1.31.1、逆アセンブルの行は `dsdt.dsl` の物）

PNP0C14 は DSDT に 9 つ（`grep -c` 9。62928・63129・91147・91730・93072・93750・94056・94625・100951 行）。fan と熱に関わる物:

| device（_UID） | GUID（object id） | 中身（AML で読んだ事） |
| --- | --- | --- |
| `\_SB.AMW0`（0） | A80593CE-A997-11DA-B012-B622A1EF5492（"BA"、method） | `WMBA`（91256〜）: Arg2 の buffer の 0x28 の dword を長さとし、全体 = 長さ + 0x2C（0x8000 まで）。word 0 = class、word 2 = select、dword 4 = input0、dword 8 = input1。**class 0x11・select 0x13・input0 = 1 の時、input1 の 1・2・4・8 で `ODV0` を 0・1・2・3 にし `Notify(IETM, 0x88)`**。その後 buffer を `GENS(3, …)` → `SMBF`（91090〜）が firmware の共有の記憶（`SMBA`、SystemMemory）へ dword ずつ写し、`ASMI` が SMI の port（`PSMI`）を書き、結果を写し戻して返す |
| 同 | 8D9DDCBC-A997-11DA-B012-B622A1EF5492（"AA"、data） | `WQAA`: "DELL"・" WMI"・1・0x8000・1 を返す（Dell の WMI の記述子、buffer の大きさ 0x8000） |
| 同 | 9DBB5994-A997-11DA-B012-B622A1EF5492（event 0xD0） | `_WED(0xD0)`（94532〜）が事象の buffer を返す（hotkey など） |
| `\_SB.AMWV`（"ADDV"） | 8A42EA14-4F2A-FD45-6422-0087F7A7E608（"DV"、method） | `WMDV`（94631〜95000）: method id 1〜0x11 は電池の EC の値、**0x12 は 3 を返す**（interface の版）、**0x20 は fan の情報**（EC 0x31 に 0 を書き 0x6F で数、fan ごとに 0x6F の種類の byte、0x39 に index を書き 0x70・0x71 の 2 byte、最後に 0xFF。`{長さ, buffer}` の package）、**0x22 は温度の sensor の情報**（EC 0x6D で数、sensor ごとに 5 byte、最後に 0xFF）。EC は AML の mutex `ECM1` の下で読む |
| `\_SB.DIAG`（"WMDG"） | F1DDEE52-063C-4784-A11E-8A06684B9B01（"DM"、method） | `WMDM`: buffer の EAX〜EDX を取り出して `GENS(0x23, …)` で SMI に渡す（Dell の SMM の register の呼び出しを WMI で包んだ物と見られる、推定）。`_STA` は無い（`GENS(0x22)` の `_STA` は別の device `\_SB.VDGS` の物、第 2 版 m1） |
| `\_SB.AMW2`（"WBAT"） | F1DDEE52-…-9BF5 ほか | BIOS の設定（属性）の WMI。`_WDG` が method で、実行時に instance の数を書き換える |
| `\_SB.WFDE`・`WFTE` | 2BC49DEF…・1F13AB7F… | Intel の参照 code の試験用の device（"DSarDev"・"TestDev"）。関係しない |

DPTF（SSDT3 "DptfTabl"）: `\_SB.IETM`（INTC1041）の `ODVP` が `ODV0`〜`ODV5` を返す（`out-ssdt3.dsl:481〜498`）、participant は TCPU・TMEM・TSKN・NGFF・AMBF（`_AC0`・`_PSV`・`_CRT` つき）。
`GHID` の表は TFN1〜TFN3（INTC1048、fan の participant）の HID を返せるが（`out-ssdt3.dsl:280〜292`）、**fan の device はどの表にも定義が無い**（Intel の参照 code の表を Dell が使わなかった形）。
ACPI の fan（PNP0C0B、`_FIF`・`_FPS`・`_FSL`・`_FST`）も無い。SSDT9 の `\_SB.PTID`（INT340E）の `OSDD`（fan の duty と回転数）は空の package を返す（`out-ssdt9.dsl` の OSDD）。
→ fan は EC が自分で回し、OS の口は WMI（DDV の読み、SMBIOS の thermal mode、SMM の呼び出し）だけ。

SMBIOS: type 218（0xDA）が 2 つ（Dell の呼び出しの表、command の I/O の番地 0x00B2 と思われる byte 列、§2.3）。type 11 の OEM の文字列 "Dell System"・"1[0B02]"。

### 2.2 5330 の DPTF とこの WS

Windows では Intel の DTT（DPTF）の service が `IETM` の表で policy を回し、`ODVP`（OEM の設計の変数）で Dell の mode を受ける。zedBSD に DTT は無く、この WS でも作らない（§4.13）。
`Notify(IETM, 0x88)` の受け手が居なくても、`WMBA` は SMI へ進む（AML の順）。IETM の `_OSC` は DTT の driver の有無を EC に伝える（`DPST(1)`、§2.4）。zedBSD は `_OSC` を呼ばないので EC は DTT の無い状態で自分の制御を続ける（推定）。DTT が無い時に thermal mode が何に効くか（EC の fan の曲線、BIOS の電力の上限 PL1・PL2）は公開の資料に無く（§2.4）、p004 の実機で測る（未確認）。

### 2.3 5330 の Debian の観測（2026-10-29 02:42、Linux 6.19.13、読みだけ、Q1 の割当、記録 `scratchpad/ws303-5330/survey.txt`・`survey2.txt`）

Linux を黒箱として読んだ結果（Linux の source は読んでいない）:

- `platform_profile_choices` = `cool quiet balanced performance`、今の `platform_profile` = `performance`。→ class 0x11・select 0x13 の 4 つの mode がこの機械にある。今の設定は performance（BIOS の NVRAM に残る見込み、未確認）。
- DDV（`dell_ddv` の hwmon）: `fan1_input` = 3741（rpm）、label "CPU Fan"。debugfs の生の buffer `fan_sensor_information` = `00 39 0f ff`（fan 1 つ、種類 0x00、回転数 0x0f39 = 3897 rpm の little endian、終わり 0xFF。読んだ時刻が hwmon と違うので値が違う）。
- DDV の温度: 生の `thermal_sensor_information` = `00 32 00 00 00 | 33 2f 00 00 00 | 33 21 00 00 00 | 52 2c 00 00 00 | 44 26 00 00 00 | ff 22 00 00 00 | ff`。
  5 byte の 1 byte 目が種類、2 byte 目が摂氏（50・47・33・44・38・34）、3〜5 byte 目は 0。Linux の label は 0x00 "CPU"、0x33 "Other"、0x52 "SODIMM"、0x44 "Ambient"、0xff "Unknown"。
  同じ時の ACPI の thermal zone は TMEM 33.05・TSKN 47.05・NGFF 44.05・AMBF 38.05・TCPU 55.05 °C で、**DDV の温度は ACPI の `_TMP` と同じ値**（TCPU だけ別の読み）。→ DDV の温度は `hw.thermal` に足さない（§4.13）。
- `dell_smm`（DIAG の WMI 経由の SMM）: `fan1_input` = 3741、`fan1_max` = 4850、`fan1_target` = 3000、`pwm1` = 128、`pwm1_enable` = 1、cooling device `dell-smm-fan1` の state 1/2。
  → SMM の口は fan の state（0〜2）と公称の回転数を持ち、Linux はそれを書ける形で出している（書けて効くかは未確認、§4.13）。
- DMI: BIOS 1.31.1（2025-07-04）、SKU 0B02、CPU i5-1245U。SMBIOS type 218 の先頭 `DA FB 00 DA B2 00 7C FF 1F B6 …`。

### 2.4 公開の資料（Fable の調査）

Fable（2026-10-29、GPL・LGPL の source は読まない条件。取った頁は `scratchpad/ws303-fable/`）の結果。「確か」は資料で読んだ事、「推定」は組み合わせた推論。
検索の要約に GPL の driver の comment の言い換えが 2 つ混じった（Dell の mode の bit の割り当て・Linux の名前の対応）。**それは使わない**（下の表で「使わない」）。

| 事実 | 出典（種類） | 程度 |
| --- | --- | --- |
| PNP0C14、`_WDG` の 20 byte の entry（GUID 16、object id 2 か notify の番号、instance の数、flags: EXPENSIVE 1・METHOD 2・STRING 4・EVENT 8）、`WMxx(instance, method id, buffer)`、`WQxx`・`WSxx`・`WExx`・`WCxx`、`_WED` | Microsoft「Windows Instrumentation: WMI and ACPI」（learn.microsoft.com の previous-versions、dn614028） | 確か |
| SMBIOS の呼び出しの buffer: class・select・input の後に output が同じ buffer で返る | Linux の ABI の文書 `Documentation/ABI/testing/dell-smbios-wmi`（文書の散文、概念だけ） | 確か（概念） |
| output は 0x14〜0x20 の 4 dword（input の 4 dword の後） | AML の位置（input0 が 4、input1 が 8、延長の長さが 0x28）からの推論 | 推定 |
| output の status の値、class 0x11・select 0x13 の get の形と答えの位置 | GPL でない公開の資料は無い | 不明（p004 で読む） |
| Dell が SMBIOS の type 0xDA の SMI の直の道（物理の buffer）を廃止の方向とし、WMI を使う | Dell の技術者の LKML の投稿（2017-09-25、散文） | 確か |
| BIOS の設定「Thermal Management」（Power の頁、既定 Optimized）、Dell Command \| Configure の `--ThermalManagement` の値 Optimized・Cool・Quiet・UltraPerformance | Latitude 5330 Service Manual（System setup options）、DCC 5.x の参照（Dell の文書） | 確か |
| BIOS の Thermal Management が software の thermal profile を上書き・衝突し得る、Cool は fan を上げ CPU を下げる | Dell KB 000130867・000133195 | 確か |
| 5330 の fan は 1 つ | Service Manual（fan の取り外し） | 確か |
| 旧い SMM の fan の口（port 0xB2 と EAX の命令）は Dell の文書が無く試行で作られ、自動の fan の制御の切り替えは多くの機械で重い副作用、BIOS が数秒ごとに fan を戻す機種がある | Linux の hwmon の文書 `dell-smm-hwmon`（散文） | 確か（散文） |
| 11 世代以降の Dell で SMM の fan の乗っ取りが効かない報告、SMM の呼び出し 1 回に 2〜4 s の停止の報告 | 利用者の issue・forum（散文） | 確か（逸話） |
| DDV: method 1〜17 は電池、18（0x12）は版、32（0x20）は fan（u8 種類・u16 LE の rpm、0xFF で終わり）、34（0x22）は温度（u8 種類・s8 今・s8 最小・s8 最大・u8 不明、0xFF で終わり）。種類の符号と温度の単位は公開されていない | Linux の文書 `wmi/devices/dell-wmi-ddv`（散文、Dell の文書は無い） | 確か（散文）。§2.1 の AML・§2.3 の観測と合う |
| BIOS の属性の WMI（`root/dcim/sysman/biosattributes`、`SetAttribute` に admin の password の扱い） | Dell「Agentless Client Manageability」（Dell の文書、検索の要約まで） | 確か（要約） |
| ACPI の fan（PNP0C0B）、`_FIF`・`_FPS`・`_FSL`・`_FST`、Notify 0x80、`_ACx`・`_ALx`・`_ART`・`_CRT`・`_PSV`・`_TRT` | ACPI 6.5 §11.2.3・§11.3・§11.4 | 確か。5330 には無い |
| DTT（DPTF の後継）は OEM が振る舞いを決め、EC と firmware に依る | Intel 000102775 | 確か |
| IETM の `_OSC` は DTT の driver が有る事を `\_SB.PC00.LPCB.ECDV.DPST(1)` で EC に伝える（SSDT3 363〜） | 5330 の SSDT3 | 確か（AML）。zedBSD は IETM の `_OSC` を呼ばないので、EC は「OS の DTT 無し」の状態で自分の制御を続ける（推定） |
| Notify(IETM, 0x88) は「OEM の変数が変わった、ODVP を読み直せ」 | coreboot の DPTF の文書と commit の散文 | 確か（散文） |
| Linux の `platform_profile` の名前 low-power・cool・quiet・balanced・balanced-performance・performance | Linux の ABI の文書 `sysfs-platform_profile`（散文、概念の名前だけ） | 確か |
| Dell の mode の bit と名前の対応、get の input0 = 0 の意味 | GPL の comment の言い換え（検索の要約） | **使わない**。p004 の実機で決める |

### 2.5 5330 の熱の水準（第 4 版 V1 と WS301 の読み）

- `out-ssdt3.dsl` から（第 3 版の review が計算）: TCPU の `_PSV` = `_HOT` = `_CRT` = TJMX（110 °C）、TMEM・TSKN・NGFF の `_HOT`・`_CRT` 127 °C、AMBF の `_CRT` 80 °C・`_HOT` 75 °C。これらは DPTF の participant（device）の値で、ACPI の thermal zone の `_CRT`（system を落とす閾値）ではない（ACPI 6.5 §11.4.4、Fable の写しの 11 章）。5330 に thermal zone は無い。
- WS301 の Debian の読み（2026-10-29、[WS301 design.md](../ws301/design.md) §2.3）: MSR 0x1A2 の **TjMax は 100 °C**。CPU は 100 °C で自分で絞る（PROCHOT）ので、TCPU の 110 °C より前に hardware が守る。PL1 25 W・PL2 55 W（BIOS の Thermal Management が performance の時）。
- → 5330 の非常の停止は firmware と CPU の hardware に任せる（Q303-13 A）。passive も 5330 では動く場面が無い（Q303-14 A）。

## 3. 責務と境界

| 層 | 責務 | 持たない物 |
| --- | --- | --- |
| ACPI WMI（`src/drivers/acpi/acpi-wmi.c`、新規、p002） | PNP0C14 を見つけ `_WDG` が Name の device だけを attach の時に解析（SMI を起こさない）。GUID から block を引き、method（`WMxx`）・query（`WQxx`）を呼ぶ口。provider が path を名指した device だけ Method の `_WDG` を評価する口 | vendor の意味。事象（`_WED`）の口（使い手が無い）。user への生の WMI の口 |
| kernel の熱の registry（`src/kern/thermal.c`、`include/kern/thermal.h`、新規、全 platform、p003a） | sensor の写し（源が push）、fan の写し（provider を周期で読む）、strategy と受け手、boost、`/dev/thermal` の cdev・ioctl・事象、ioctl の範囲の登録の口（WS301 用）、`hw.thermal` を p009 まで同じ写しから | hardware の操作、CPU の register |
| 熱の見張り（`thermal.c` の中の専用の thread、p003b） | passive・critical の判定、非常の停止（init への知らせ、直の切り）。provider を呼ばず写しだけを読む | provider の呼び（止まりうる物） |
| 非常の電源断（`src/drivers/acpi` に `drv_acpi_poweroff_emergency`、platform ごとの `kern_platform_poweroff_emergency`、p003b） | `_PTS` を評価せずに S5 に入る（pcat）、`hal_poweroff`（rpi5） | 秩序だった停止（init の物） |
| init（`userland/base/init`、p003b） | SIGUSR2 で非常の poweroff（短い猶予） | 温度の判定 |
| Dell の provider（`src/drivers/platform/pcat/dell-wmi.c`、新規、p004） | DDV の fan の読み、thermal mode を strategy の受け手として、SMM の fan の state を boost として（確かめられれば） | Dell の他の機能 |
| Pi 5 の provider（`rpi5-fan.c` に足す、p006） | 温度の push、fan の level・rpm、strategy の受け手（trip の組）、boost（level の下限） | — |
| WS301 の受け手（WS301 p004 が作る、WS301 の source） | strategy → CPU の要求 | WS303 の lock |
| UAPI（`include/uapi/thermal.h`、新規、p003a。WS301 の struct も同じ file） | sensor・fan・strategy・boost・状態・事象 | — |
| `/sbin/thermal`（`userland/base/thermal/`、p003a） | 表示、strategy・boost の設定（root） | — |
| sessiond（p005） | session の user（root か wheel）からの THERMAL の要求を `/dev/thermal` に | — |

## 4. 設計

### 4.1 ACPI WMI の層（p002）

- attach: `drv_acpi_wmi_attach()` を `acpi-kern.c` の thermal の後・LPS0 の前（EC が要る）。namespace を歩いて `_HID` "PNP0C14" を集め、**`_WDG` が Name の device だけ**を解析する（5330 で要る AMW0・AMWV は Name、91150・94628）。`_WDG` が Method の 5 つ（AMW2・AMW5・AMW4・DIAG・ECMO、AMW2・AMW5 は SMI を起こす）は評価しない。
  provider が path を名指した device（`\_SB.DIAG`、Q303-12 で使う）だけ Method の `_WDG` を評価する口 `drv_acpi_wmi_attach_path(path)` を持つ（DIAG の `_WDG` は Name を返すだけで SMI 無し、`dsdt.dsl` 94054〜94071）。
- 20 byte の entry（GUID 16、object id 2 か notify の番号、instance の数、flags）。GUID は `_WDG` の生の byte の順で持ち、文字列の表記から変える関数を 1 つ持つ（前 3 欄が逆順）。表は最大 48 block。同じ GUID は `drv_acpi_wmi_find_nth` で n 番目。
- `drv_acpi_wmi_method(block, instance, method_id, input, size, &result)`（`WMxx`、結果は AML の object、呼び手が解放）、`drv_acpi_wmi_query(block, instance, &result)`（`WQxx`）。method は Arg2 を書き換えて返す事があるので返り値だけを見る。
- 事象（`_WED`）の口は作らない（使い手が無く、Notify の handler の中の評価は handler の契約 `aml-sync.c:62〜67` に反する。要る WS が評価の thread つきで作る）。
- 生の WMI を user に出さない。`/dev/acpi` の 0666 の評価の口は BUG-351（Q1）。
- harness の拡張（第 2 版 R9）: 引数つきの評価、EC の register ごとの読みの列、SMI の時に SystemMemory を書く模擬。`plan/tools/aml-host/` に移して tools.md に登録。

### 4.2 Dell の provider（p004）

**見分け**: DDV（8A42EA14…）か SMBIOS の呼び出し（A80593CE…）の block があり、記述子（8D9DDCBC…）の `WQAA` が "DELL"・" WMI" を返す時。機種の表は持たない。DDV の版（method 0x12）が 3 でなければ fan の読みを止めて log 1 行。

**fan の読み（DDV method 0x20）**: 読む前に `\ECRD` = 1 を確かめる（0 だと EC の 1 byte ごとに SMI、96389・97597〜。0 なら STALE）。入力は 4 byte の 0（AML は byte 0〜2 を `CreateByteField`、zedBSD は buffer の外の field を拒む `aml-define.c:403〜406`）。
解析は package の長さ（要素 0）→ 3 byte の組 `[種類, rpm LSB, rpm MSB]` → 0xFF の順（`{0, Buffer(1){0}}` を種類 0 の fan と読まない）。fan は 4 まで。method 0x20 は `ECM1` を取らず `Sleep(3)` の間は interpreter の lock を離す（EC の取引 2 + 4n と Sleep 1 回）。registry の worker の thread から 5 秒ごと。

**今の mode（SMI を起こさない）**: GNVS の `\ODV0`（`dsdt.dsl` 1236・2466）を AML の評価で読む。`WMBA` の Case が input1 の bit（1・2・4・8）と `ODV0`（0〜3）の 1 対 1 を与える。読むのは attach・set の後・resume の後だけ（周期の読みはしない）。
BIOS が起動の時に `ODV0` を埋めるかは p004 の最初の確かめ。埋めなければ「今の mode は不明（set の後だけ分かる）」で受け手を登録する。input0 = 0 の get の SMI は使わない。

**mode の set（SMBIOS の呼び出し class 0x11・select 0x13・input0 = 1・input1 = bit）**: buffer は「延長の長さ 0 + 0x2C」に固定する 1 つの関数で作り、0x14〜0x23 を sentinel 0xFFFFFFFF で埋める。返りで 16 byte が全部 sentinel のままなら「SMI が起きなかった」（`SMBF` は buffer が短いと SMI を飛ばす、91087〜）として EIO。延長 0 を firmware が受けなければ受け手を登録しない。
成否は「SMI が起きた」と `ODV0` の読みまで（status の意味は不明）。失敗の後は今の mode を「不明」。set は 1 秒に 1 回まで（速い求めは EBUSY）。BIOS の admin の password で拒まれる時は区別できず EIO と log。

**strategy の受け手としての対応**（初期の案、p004 で 5330 の測りと Debian の `platform_profile` の名前の対で決めて記録する）:

| strategy | Dell の mode（BIOS の名前） |
| --- | --- |
| PERFORMANCE | UltraPerformance |
| BALANCED | Optimized |
| POWERSAVE | Cool（CPU を下げ fan を上げる。Quiet は fan を遅くするので既定の写しにしない） |
| MANUAL | noise ≥ 67 かつ speed ≥ 67 → UltraPerformance、noise ≤ 33 → Quiet（Q303-19）、他は Optimized |
| FIRMWARE（起動の時） | 書かない |

**boost（SMM の fan の state、DIAG の WMI）**: Q303-12 で値は GPL の file から出典つきで取る。p004 で確かめる: (1) state を上げた時に rpm が上がる、(2) さらに温度が上がった時に EC が state より上げる（EC が上げる自由を失わない）。どちらかが成り立たなければ boost は「無し」と答える。
boost を上げた後は 60 秒ごとに書き直さない（BIOS が戻す機種の報告がある、§2.4。戻ったら boost を「無効」の flag にして log）。SMM の呼び 1 回に数秒の停止の報告（§2.4）があるので、boost の set は worker の thread で、1 秒に 1 回まで。

### 4.3 registry（p003a）

- **sensor**: 源（acpi-thermal、rpi5-fan、試験の注入）が `kern_thermal_sensor_register(name, kind, flags, &handle)` で登録し、読むたびに `kern_thermal_sensor_update(handle, millicelsius, valid)` で push する（registry が CLOCK_MONOTONIC の時刻を付ける）。水準（passive・critical、0 は「無し」）は源が登録の時と変わった時に `kern_thermal_sensor_levels(handle, passive, critical)` で渡す。
  源の周期は源のまま（ACPI 5 秒、Pi 5 1 秒）。`kind` は ZONE（ACPI の thermal zone、DT の thermal zone）と DEVICE（ACPI の device、DPTF の participant）。**非常の停止の判定に使うのは ZONE の critical だけ**（Q303-13 A）。sensor は全体で 32 まで、外さない。
- **値の規則**（第 4 版の review の m9）: 読みの値が −40〜150 °C の外なら無効。最後に採った値から 20 °C 以上跳ねた値は 1 回だけ捨て、次の読みは跳ねていても採る（本当の段差を捨て続けない）。水準の値が 127 °C 以上（0x7F 以上）なら「無し」。
- **fan**: provider は `kern_fan_register(name, rpm_max, ops, context)`（8 本まで、外さない）。`ops->read(context, &state)` は rpm（`HAS_RPM`）・自動の段・段の数（`HAS_LEVEL`）・boost の段の数を返す。registry の worker の thread が 5 秒ごとに全部の `read` を呼び写しに置く（ioctl は写しを返す）。
  STALLED は `HAS_RPM` と `HAS_LEVEL` の両方があり、自動の段 > 0 で rpm 0 が 3 周期の時だけ（Dell の fan と tachometer の無い Pi 5 には出さない）。
- **boost**: `ops->boost(context, level)`（NULL なら boost 無し）。実際の段 = max(自動の段, boost)。boost は provider が「自動の制御より下にしない」形でだけ実装する（Pi 5 は level の下限、Dell は SMM の state で EC の上げる自由を保つ時だけ）。fan を手で固定する mode・段を下げる口は無い（ユーザーの「下回らせるオーバーライドは不能」）。
- **worker の thread**（`thermal-work`、1 本）: fan の周期の読み、strategy の受け手の呼び、boost の set、passive の上限の書き（WS301）。止まりうる物（AML・SMI・WS301 の 1 秒の待ち）は全部ここ。
- **見張りの thread**（`thermal-guard`、1 本、p003b）: 1 秒ごとに sensor の写しだけを読み（spinlock の下）、passive・critical を判定して worker に依頼か非常の停止を始める。provider を呼ばない（第 4 版の review の M6）。

### 4.4 strategy（p003a、Q303-11）

- **値**: `KERN_THERMAL_STRATEGY_FIRMWARE`（0、起動の時・まだ選ばれていない。受け手は何も書かない）、`PERFORMANCE`、`BALANCED`、`POWERSAVE`、`MANUAL`。
- **manual の 3 つの軸**（各 0〜100、uint8）。**向きは「100 = その物が多い」**（Q303-18 で確かめる）: speed 100 = 一番速く、noise 100 = 一番うるさくてよい（fan の音を許す）、gating 100 = 一番細かく電源を切る（省電力）。manual でない SET では 3 軸は 0 でなければ EINVAL、GET は manual でない時 0 を返す。
- **CUSTOM の印**: WS301 の root の直の設定が効いている間、GET は strategy の値を変えずに `CUSTOM` の flag を立てる（WS301 の受け手の `is_custom` の callback を lock なしで読む、§4.11）。次に strategy が SET されると WS301 の直の設定は消える（WS301 §4.5、Q303-17 A）。
- **SET の手順**（ioctl の中）: (1) 引数の検査（root、値、軸）。(2) strategy の mutex を取り、世代の番号を 1 進め、今の strategy・軸・世代を写しに置き、mutex を離す。(3) worker に「世代 g を全部の受け手に渡す」を依頼し、完了を 5 秒まで待つ。
  (4) worker は受け手を登録の順に **WS303 の lock を持たずに**呼ぶ。途中で新しい世代が来たら、残りの受け手には新しい世代を渡す（古い世代は捨てる）。(5) 結果: 受け手ごとの errno と「適用済み」の flag を写しに置く。
  ioctl の戻り: 受け手 0 → EOPNOTSUPP（strategy は記録する）。全部失敗 → 最初の errno。一部 → 0 と `PARTIAL` の flag（事象を出す）。5 秒で終わらない → 0 と `PENDING` の flag（後で結果の事象）。
- **後から登録された受け手**: 登録の時に今の strategy が FIRMWARE でなければ、worker が今の世代をその受け手に渡す。
- **受け手の口の契約**（`kern_thermal_strategy_receiver_register(name, ops, context)`、16 まで、外さない）: `ops->apply(context, strategy, axes)` は worker の thread から呼ばれ、眠ってよい、2 秒を目安に返す（超えても止めない、結果に時間を記録）。`ops->is_custom(context)`（任意、NULL 可）は lock なしで読める値を返す。受け手は WS303 の関数を apply の中から呼ばない（事象の発行を除く）。
- **起動の時**（Q303-15、推し A）: FIRMWARE のまま。GET は FIRMWARE と、受け手が出せれば firmware の今の mode の名前（Dell は `ODV0` の名前、16 byte の文字列）を返す。
- WS301 の受け手（CPU の要求への対応）は WS301 p004 が作り登録する（第 4 版の review の M3）。WS303 の受け入れは host 試験の模擬の受け手で確かめる。

### 4.5 fan と boost の振る舞い

- 5330: 自動は EC。OS は rpm を読む（DDV）。boost は §4.2 の確かめが通った時だけ。
- Pi 5: 自動は `rpi5-fan.c` の DT の trip。boost = level の下限（0〜段の数）。実際の level = max(trip からの level, boost)。温度が読めない時は今の通り全開。
- boost の SET（root、fan ごとか全部）は worker で。boost は strategy と独立に保つ（strategy を替えても boost は残る）。起動の時は 0。

### 4.6 水準と非常の停止（p003b、Q303-5・Q303-13 A）

- **判定**（見張りの thread）: ZONE の sensor の critical に「時刻が違う 2 つの有効な値」が続けて達したら非常。写しが 30 秒以上更新されない ZONE の sensor で、最後の有効な値が critical 以上なら、それも非常。
- **非常の手順**（一度始めたら温度が下がっても取り消さない。原文「非常水準に達したとき、電源を落とす」）:
  1. 事象 EMERGENCY を出し、状態を GET_STATUS の固定の欄に置く（ring の溢れで失わない）。以後 `KERN_SYSTEM_SLEEP`（S0 idle）は EBUSY。
  2. pid 1 の SIGUSR2 の disposition が handler である事を確かめる（signal の lock の下で読む小さな口を `src/kern/signal.c` に足す）。handler なら `process_find_ref(1)` → `signal_send_process(SIGUSR2)`。handler でなければ（古い init、起動の直後）4 へ。
  3. init の SIGUSR2 の道: init は全部の service に短い猶予（SIGTERM の後 2 秒、SIGKILL の後 2 秒）で止め、sync、`KERN_SYSTEM_POWEROFF`。kernel は SIGUSR2 から 60 秒で init が `KERN_SYSTEM_POWEROFF` に入らなければ 4 へ。入った後も SIGUSR2 から 120 秒で電源が落ちていなければ 4 へ。
     見込み: base の service 7 × 4 秒 + 他の process 4 秒 ≈ 32 秒。service が 32 の最悪でも 132 秒で 120 秒を超えうるので、超えたら sync の前でも切る（「電源を落とす」を優先する解釈）。
  4. 直の切り: kernel の sync の口があれば 10 秒まで待って呼び（p003b で確かめる、無ければ飛ばす）、`kern_platform_poweroff_emergency()`。pcat は `drv_acpi_poweroff_emergency()`（`_PTS` を評価しない、event の lock は取る、SLP_TYP・SLP_EN の書き）、rpi5 は `hal_poweroff`（`rpi5.c:303〜312`、`_PTS` 無し）。init が `system_shutdown_prepare` の中に居ても切る（電源断を優先）。
- S0 idle との関係: 見張りの thread は S0 idle の間は走らない（sensor も読まれない）。resume の後に最初の値から判定し直す。S0 idle の間に 120 秒の timer は進まない（monotonic の時計が止まる間は数えない）。
- **5330**: thermal zone が無いので非常の停止は働かず、firmware（EC）と CPU（TjMax 100 °C の PROCHOT、THERMTRIP）に任せる。**Pi 5**: DT の critical 110 °C で働く（実際に効く唯一の今の機械）。

### 4.7 passive（p007、Q303-6・Q303-14 A）

- CPU の sensor（`THERMAL_FLAG_CPU`）が passive の水準を超えたら、見張りの thread が worker に依頼し、worker が 5 秒ごとに WS301 の `kern_cpuperf_limit(KERN_CPUPERF_LIMIT_THERMAL, ALL, permille)` を 100‰ ずつ下げる（下限は guaranteed、千分率は呼んだ時の lowest・highest に対する値で、STEPS の provider は切り下げで近い段）。水準 − 3 °C を下回ったら 100‰ ずつ戻す。
- `CONFIG_CPU_PERF=n` の kernel は passive の上限の口が無いので何もしない（WS301 は `kern_thermal_limit_receiver_register` の形で WS303 に口を登録する。WS303 から WS301 の symbol を直に呼ばない、§4.8 の M5 と同じ向き）。
- 5330（TCPU の `_PSV` 110 °C、TjMax 100 °C）と Pi 5（passive の trip 無し）では動く場面が無い。受け入れは host 試験まで（Q303-14 A）。

### 4.8 UAPI `/dev/thermal`（p003a。案はユーザーの review に出してから実装）

- device: 新しい cdev `/dev/thermal`（番号は次の空き、今は 0x00010008 の見込み。登録は `vfs.c:466` の並び）。node の mode は 0666（読みは誰でも）、SET の root の判定は ioctl の中で `cred_is_superuser`。試験の注入は別の node `/dev/thermal-inject`（0600、`CONFIG_THERMAL_TEST_INJECT` の build だけ、`input-inject` と同じ形）。
- ioctl の group 'T'（空き、第 4 版の review が確かめた）。番号の範囲: WS303 が 1〜31、WS301 が 32〜63。`thermal.c` は `kern_thermal_ioctl_range_register(first, last, handler)` を持ち、WS301 の `cpuperf.c` が自分の範囲を登録する（登録の無い範囲は ENOTTY。`CONFIG_CPU_PERF=n` でも link が通る。weak の symbol は使わない）。
- WS303 の ioctl: `GET_STATUS`（sensor の数・fan の数・strategy の要約・非常の状態）、`GET_SENSOR`（index）、`GET_FAN`（index）、`GET_STRATEGY`、`SET_STRATEGY`（root）、`SET_BOOST`（root）、`SUBSCRIBE`（事象の種類の mask）。
- 事象: `read`・`poll` で 128 byte の record（`struct system_event` と同じ大きさと頭の形、種類は `/dev/thermal` の自分の名前空間）。種類: STRATEGY（変化・PARTIAL・PENDING の結果）、BOOST、PASSIVE（跨ぎ、2 °C の hysteresis）、CRITICAL（跨ぎ）、EMERGENCY、FAN_STALLED、CPU（WS301 の変化）。open ごとに 64 の ring、溢れたら古い物を捨てる（EMERGENCY は GET_STATUS の固定の欄でも読める）。`system-event.c` の ring は使わない（class の名前空間を `/dev/system` と分ける）。
- 大きさは `_Static_assert` と `offsetof` で固め、ILP32 と LP64 で同じ（§5.2）。

### 4.9 Pi 5（p006、Q303-4）

- 温度は `rpi5-fan.c` の thread が registry に push（ZONE、critical は DT の critical の trip 110 °C、passive は DT に無いので 0）。fan は level・段の数・rpm（poll の中で `read_rpm` を毎回読み写しに置く）を registry に出す。今の `rpi5_fan.lock`（185〜221）の範囲を level・rpm の写しまで広げる。
- strategy の受け手: trip の組を選ぶ。**fan を DT より遅くしない**（trip を DT より上げない）: PERFORMANCE・BALANCED・POWERSAVE・FIRMWARE は DT の trip、MANUAL は noise ≥ 67 の時だけ全 trip −5 °C（早く回す、critical は変えない）、他は DT の trip。CPU の違いは WS301 の受け手（Pi の provider は後の WS）。
- boost は level の下限（§4.5）。

### 4.10 状態遷移

```
strategy:  FIRMWARE --SET(s)--> s（世代 g、受け手の結果: 全部／PARTIAL／PENDING）--SET(s')--> s'
           s --WS301 の直の設定--> s + CUSTOM の印 --SET--> 印は消える
非常:      NORMAL --ZONE の critical に 2 回--> EMERGENCY_SIGNALED（init へ SIGUSR2）
           NORMAL --init に handler 無し--> EMERGENCY_CUTTING
           EMERGENCY_SIGNALED --init が POWEROFF に入る--> EMERGENCY_INIT --電源断--> (終)
           EMERGENCY_SIGNALED --60 秒で入らない--> EMERGENCY_CUTTING
           EMERGENCY_INIT --SIGUSR2 から 120 秒--> EMERGENCY_CUTTING --直の切り--> (終)
Dell の mode:  UNPROBED --記述子と GUID と延長 0 の受け--> READY(今の mode = ODV0 か「不明」)
               READY --apply(s)--> SETTING --SMI が起き ODV0 が合う--> READY / --失敗--> READY(「不明」、errno)
fan の写し:  UNKNOWN --read 成功--> OK --read 失敗--> STALE --read 成功--> OK
```

### 4.11 資源・並行性・lock

- 表（sensor 32・fan 8・受け手 16・ioctl の範囲 4・WMI の block 48）は static で kernel の寿命。AML の object は呼んだ関数の中で解放。
- lock: 写し（sensor・fan・strategy の写し・非常の状態）は 1 つの spinlock（`LOCK_RANK_DEVICE`、irqsave）。strategy の mutex は SET の直列と世代の番号だけに使い、**受け手を呼ぶ間は持たない**。boost の mutex も同じ。
- 全体の順: strategy の mutex（短く、受け手を呼ばない）→ 写しの spinlock。worker の thread → 受け手の lock（WS301 の `cpuperf_mutex`、Dell の provider の mutex）→ AML の interpreter の lock。逆の道は無い: 受け手は WS303 の lock を取らず、WS301 の CUSTOM は WS303 が `is_custom` を lock なしで読む（第 4 版の review の M2）。
- 見張りの thread は写しの spinlock だけを取る。非常の手順の signal の口は signal の lock を短く取る。
- Pi 5: `rpi5_fan.lock` の下で level・rpm の写し、registry への push は lock の外。

### 4.12 割り込みと DMA

- DMA は無い。割り込みは使わない（WMI の事象は扱わない、sensor は周期の読み）。
- SMI: Dell の mode の set と SMM の boost で CPU は SMM に入る（数十 µs〜数 s の報告、§2.4）。どちらも worker の thread、1 秒に 1 回まで。DDV の読みは SMI を使わない（`\ECRD` = 1 の時）。

### 4.13 失敗と回復

| 失敗 | 振る舞い |
| --- | --- |
| PNP0C14 が無い（QEMU・他の機械） | WMI の表が空、Dell の provider は attach しない。fan 0、strategy の受け手 0 なら SET は EOPNOTSUPP |
| `_WDG` の長さが 20 の倍数でない・評価の失敗 | その device を飛ばし log 1 行 |
| DDV の buffer の誤り・`\ECRD` = 0 | read の失敗（STALE） |
| attach の一時の失敗 | 3 回まで 1 秒おきに試し直す |
| Dell の set の失敗・BIOS の password | 受け手の結果 EIO、今の mode は「不明」、log |
| 受け手が 2 秒を超える | 止めずに待つ、結果に時間、ioctl は 5 秒で PENDING |
| 見張りの thread の周期が 5 秒以上遅れる | log と事象（写しの古さの判定は続く） |
| sensor の源が止まる | 30 秒で古い扱い、最後の値が critical 以上なら非常 |
| init が SIGUSR2 に応じない・handler が無い | §4.6 の直の切り |
| `_PTS` が返らない恐れ | 非常の切りは `_PTS` を評価しない |

### 4.14 既存の framework との関係

- `hw.thermal`: p003a で registry の写しから出す（`thermal_entry` の形は変えない）。p009 で sysctl の道具と Keiland の monitor を `/dev/thermal` に移し、`hw.thermal` を外す（`HW_THERMAL 9` は欠番の comment を残し再利用しない）。monitor の file（`userland/desktop/libkeiland-backend-zedbsd/`）の所有は Keiland の WS と Q1 が調整する。
- `/dev/system`: 変えない（事象の class も足さない）。
- WS301: strategy の受け手、passive の上限の口、ioctl の範囲 32〜63（WS301 design.md §4.4・§4.6・§5.2）。
- WS300: gating の軸の受け手は WS300 の設計で決める（Q303-16 の推し C）。
- amdrdna（WS039）の GPU の fan は GPU の telemetry に残す（registry に載せない）。
- Keiland: strategy の表示と切り替えは別の WS。この WS は UAPI・`/sbin/thermal`・sessiond まで。
- docs: `docs/architecture/power-management.md:24`（「thermal management」は対応外）は p008 で WS301 と順を Q1。

### 4.15 選ばなかった案

| 案 | 理由 |
| --- | --- |
| SMBIOS の type 0xDA の SMI の直の道 | firmware の共有の記憶を AML が持ち `WMBA` が使う。直の道は物理の buffer・port・版の差を kernel が持ち、WSMT で拒まれる恐れ |
| DDV の温度を sensor に足す | ACPI の `_TMP` と同じ値（§2.3） |
| Intel DTT の framework | 大きく、到達目標に要らない |
| 生の WMI を user に出す | 任意の SMI を user が起こせる |
| 温度・fan・strategy を `/dev/system` に | ユーザーの決定（Q303-1）で `/dev/thermal` |
| fan を手で固定する mode（第 1 版の MANUAL） | 「下回らせるオーバーライドは不能」に反する |
| DPTF の participant の `_CRT` で停止（第 4 版の Q303-13 C） | ACPI の規格で system を落とす閾値でない。5330 では AMBF の 80 °C が効き誤って落ちる恐れ |
| 非常の判定を worker の thread で（第 4 版まで） | provider の止まりで判定が止まる（M6） |
| `/dev/thermal` の事象に `system-event.c` の ring を使う | class の bit の名前空間が `/dev/system` と共有で、ぶつかる |
| WS301 の symbol を weak で呼ぶ | R7 で禁じた。登録の口で向きを揃える |

## 5. インタフェース（案。名前は実装の Phase で coding-style に合わせる）

### 5.1 kernel の中（`include/kern/thermal.h`）

```c
#define KERN_THERMAL_KIND_ZONE		1U
#define KERN_THERMAL_KIND_DEVICE	2U
#define KERN_THERMAL_FLAG_CPU		0x1U

int kern_thermal_sensor_register(const char *name, uint32_t kind, uint32_t flags, struct kern_thermal_sensor **sensor);
void kern_thermal_sensor_update(struct kern_thermal_sensor *sensor, int32_t millicelsius, bool valid);
void kern_thermal_sensor_levels(struct kern_thermal_sensor *sensor, int32_t passive, int32_t critical);	/* 0: none */

struct kern_fan_state {
	uint32_t flags;			/* KERN_FAN_HAS_RPM, KERN_FAN_HAS_LEVEL */
	uint32_t rpm;
	uint32_t level;			/* the automatic control's level */
	uint32_t level_count;
	uint32_t boost_count;		/* 0: no boost */
};
struct kern_fan_ops {
	int (*read)(void *context, struct kern_fan_state *state);
	int (*boost)(void *context, uint32_t level);	/* NULL: no boost; never below the automatic level */
};
int kern_fan_register(const char *name, uint32_t rpm_max, const struct kern_fan_ops *ops, void *context);

enum kern_thermal_strategy_value {
	KERN_THERMAL_STRATEGY_FIRMWARE = 0,
	KERN_THERMAL_STRATEGY_PERFORMANCE,
	KERN_THERMAL_STRATEGY_BALANCED,
	KERN_THERMAL_STRATEGY_POWERSAVE,
	KERN_THERMAL_STRATEGY_MANUAL
};
struct kern_thermal_strategy {
	uint32_t value;
	uint8_t speed, noise, gating;	/* 0..100, 100 = most of it; 0 unless MANUAL */
	uint8_t reserved;
};
struct kern_thermal_receiver_ops {
	int (*apply)(void *context, const struct kern_thermal_strategy *strategy);	/* worker thread, may sleep */
	bool (*is_custom)(void *context);						/* lock-free read, may be NULL */
	int (*firmware_name)(void *context, char *name, size_t size);			/* may be NULL */
};
int kern_thermal_strategy_receiver_register(const char *name, const struct kern_thermal_receiver_ops *ops, void *context);

typedef int (*kern_thermal_ioctl_t)(struct file *file, unsigned long request, uintptr_t argument);
int kern_thermal_ioctl_range_register(unsigned first, unsigned last, kern_thermal_ioctl_t handler);
int kern_thermal_limit_receiver_register(int (*limit)(uint32_t source, uint32_t permille));	/* WS301 */
```

### 5.2 UAPI（`include/uapi/thermal.h`、欄の一覧。p003a の最初に完全な案を書きユーザーの review に出す）

| struct | 欄 |
| --- | --- |
| `thermal_status` | sensor の数、fan の数、strategy の値と flags（CUSTOM・PARTIAL・PENDING）、非常の状態（NORMAL・SIGNALED・INIT・CUTTING）と始めた時刻と sensor の index、reserved |
| `thermal_sensor_info`（in: index） | 名前 32、kind、flags（CPU・VALID・STALE）、温度（m°C、int32）、passive、critical（0 は無し）、時刻（ns、CLOCK_MONOTONIC）、reserved |
| `thermal_fan_info`（in: index） | 名前 32、flags（HAS_RPM・HAS_LEVEL・STALE・STALLED・BOOST_DISABLED）、rpm、rpm_max、自動の段、boost の段、段の数、boost の段の数、時刻、reserved |
| `thermal_strategy`（GET/SET） | 値、speed・noise・gating（uint8）、flags（CUSTOM・PARTIAL・PENDING、GET だけ）、世代、firmware の今の mode の名前 16、受け手の数、受け手ごとの結果 16 ×（名前 16・errno・flags・時間 ms） |
| `thermal_boost`（SET） | fan の index（0xffffffff は全部）、段 |
| `thermal_inject`（試験の build、`/dev/thermal-inject`） | sensor の名前、kind（ZONE）、温度、critical、passive、valid |
| 事象の record（128 byte） | `struct system_event` と同じ頭（大きさ・種類・時刻）、種類（§4.8）、sensor・fan・受け手の index、値 |

## 6. 試験の計画

### 6.1 host 試験（`plan/ws303/tests/`、harness は `plan/tools/aml-host/`）

- WMI: 5330 の表で Name の `_WDG` の device だけを解析し、attach と周期の処理が SMI の port を書かない（harness の SMI の模擬で数える）。期待の block の数は harness の出力を基準にする。
- DDV: EC の像（0x6F・0x70・0x71・0x6D の列）で「fan 1、種類 0、3897 rpm」、版 3、0xFF の欠け・長さの超過・`{0, Buffer(1){0}}`・`\ECRD` = 0、EC の取引の失敗で STALE。
- SMBIOS の set: sentinel の残り（SMI 無し）で EIO、`ODV0` が bit に合う、延長 0。
- registry: sensor の push と値の規則（範囲・跳ね・127 °C）、fan の写し・STALE・STALLED（誤報の 2 つの場合）、boost の max、strategy の SET の規則（受け手 0・全部失敗・一部・5 秒・世代の追い越し・後から登録・CUSTOM）、ioctl の範囲の登録の無い時の ENOTTY、lock の順（受け手が WS303 を呼ばない）。
- 非常: 判定（2 回・30 秒の古さ・DEVICE の sensor では動かない）、手順の状態遷移（handler 有り・無し、60 秒・120 秒）、provider の read が返らない時も判定が進む（M6）、S0 idle の間の timer。
- UAPI の layout（`_Static_assert`、-m64・-m32）。

### 6.2 QEMU（amd64 q35、`CONFIG_THERMAL_TEST_INJECT=y` の試験の build、判定は console の log でなく ssh・QMP・時間）

- 回帰: `/sbin/thermal` が sensor・fan 0・strategy の受け手 0（SET は EOPNOTSUPP）を出す。`boot-test.sh` PASS。ACPI の無い arch（riscv64 の virt）でも `/sbin/thermal` が動く。
- 非常の init の道: 注入（ZONE、critical 90 °C に 95 °C を 2 回）→ host の monotonic で QEMU の終了までの時間（60 秒未満）と QMP の `SHUTDOWN` 事象 → 再起動して ssh で init が書いた非常の記録の file（sync まで届いた印）を読む。
- 非常の直の切りの道: 試験の起動の parameter `thermal.emergency_test=nosignal`（試験の build だけ、init に送らず直の切りへ）で同じ注入 → 終了までの時間（直の切りの 10 秒の sync 待ちの後）と、init の記録の file が無い事。
- sessiond（p005）: 試験の模擬の受け手（注入の build の option）で、session の user（wheel）から strategy が届き、wheel でない user は拒まれる事を ssh で読む。

### 6.3 実機

- 5330（素の起動、`plan/tools/hw5330/5330-run.sh`、1 本の script `plan/ws303/tests/5330-thermal.sh`、Q1 の割当）:
  1. `/sbin/thermal` の出力を ssh で回収: fan 1 本の rpm（`HAS_RPM`、idle の 0 も可、負荷の後に 0 でない）、strategy の受け手 "dell-wmi"、起動の時は FIRMWARE と firmware の mode の名前（今は performance の見込み）。
  2. strategy を POWERSAVE → PERFORMANCE と PERFORMANCE → POWERSAVE の 2 回、各 60 秒の負荷（CPU の数だけの spin）で rpm・温度を 5 秒ごと。間は温度が戻るのを待つ（5 分か開始の値まで）。set の時間を記録（合否にしない）。
  3. mode の名前の対: zedBSD で 4 つを set → Debian に戻して `platform_profile` を読む（読みだけ）で bit と名前の対を記録。終わりに performance へ戻す（Q303-8）。
  4. boost（SMM、確かめが通った時）: 同じ負荷で boost の段を上げた時の rpm が boost 無し以上、さらに温度が上がった時に EC が boost より上げる。安全の止め: TCPU が 90 °C を超えたら boost を 0 に戻して試験を止める。
  5. WS301 の 5330 の測りと同じ日に重ねない。
- Pi 5（素の起動、Q1 の割当）: `/sbin/thermal` が "rpi5-fan" の level・rpm と温度を出し、負荷で level が上がり、boost で level の下限が上がる。非常の道は試験の build の注入（`/dev/thermal-inject`）で init の道を 1 回（Pi 5 の電源の入れ直しは Q1）。

## 7. Phase と LW（この表が本体。ws.md の表は写し。±50%、1 LW ≈ 実時間 15〜25 分）

| Phase | 内容 | 依存 | LW | 受け入れ |
| --- | --- | --- | --- | --- |
| p001 | 調査と設計、design-reviewer、ユーザーの判断（Q303-15〜19） | — | 残り 1 | 答え、第 5 版の review の反映 |
| p002 | ACPI WMI の層、harness の `plan/tools/aml-host/` への移しと拡張（tools.md に登録） | p001 | 6 | §6.1 の WMI の host 試験、SMI を起こさない、amd64 の build warning 0 |
| p003a | registry（sensor の push・fan・strategy と受け手・boost・worker）、`/dev/thermal`・ioctl の範囲の口・事象、`include/uapi/thermal.h`（**最初に案をユーザーの review へ**）、`/sbin/thermal`、acpi-thermal を push に、`hw.thermal` を同じ写しから、全 platform の `vmunix.mk` | p001 | 6 | §6.1 の registry の host 試験、全 platform の build warning 0、QEMU の回帰（§6.2） |
| p003b | 見張りの thread と非常の停止（signal の disposition の口、init の SIGUSR2、`drv_acpi_poweroff_emergency`、platform の口）、`/dev/thermal-inject` と `CONFIG_THERMAL_TEST_INJECT` | p003a | 5 | §6.1 の非常の host 試験、QEMU の非常の 2 本（§6.2） |
| p004 | Dell の provider（DDV・`ODV0`・set・strategy の受け手・SMM の boost の確かめ）と 5330 の試験 | p002、p003a | 7 | §6.3 の 5330 の 1〜4、mode の対と boost の結果の記録 |
| p005 | sessiond の THERMAL の要求（strategy と boost、root か wheel） | p003a | 2 | rules の host 試験、QEMU（§6.2 の sessiond） |
| p006 | Pi 5 の provider（push・fan・受け手・boost、lock の範囲） | p003a、p003b | 3 | Pi 5 の §6.3 |
| p007 | passive（worker から WS301 の上限、`kern_thermal_limit_receiver_register`） | p003b、WS301 p002・p004 | 2 | host 試験（模擬の上限の受け手）。実機は passive の水準が critical より下の機械が来た時（Q303-14 A） |
| p009 | sysctl の道具と Keiland の monitor を `/dev/thermal` へ、`hw.thermal` を外す | p003a | 2 | 2 つの読み手の build と表示、`hw.thermal` の grep が 0（欠番の comment を除く） |
| p008 | 規約の全文と文書（WS301 と順を Q1） | p004〜p007、p009 | 2 | 規約、文書 |

合計 約 35 LW（p001 を除く）。p004 は実機の割当を待つ時間を別に持つ。WS301 の受け手は WS301 p004 が作る（この表に無い）。

## 8. 出典と引用（guardrail の「MIT・GPL の出典のデータの引用」）

- 5330 の ACPI の表（Dell の firmware、hardware の事実）: method の名前・offset・GUID・EC の番地は値として、block ごとに「Value(s) referenced: the Latitude 5330's DSDT (BIOS 1.31.1), \_SB.AMWV.WMDV」の形（複数行の comment）。
- Microsoft「Windows Instrumentation: WMI and ACPI」、ACPI 6.5 §11: 「Procedure referenced:」「Value(s) referenced:」で block ごと。
- Dell の公開の文書（Service Manual、DCC、KB）: mode の名前の出典。zedBSD の名前は strategy の抽象で、Dell の名前は provider の表の comment に出典つきで。
- Linux の文書の散文（ABI・hwmon・wmi）は概念の確かめだけ。
- **Q303-12**: SMM の fan の state の命令の値は Linux の `dell-smm-hwmon` の driver（GPL）から「Value(s) referenced:」で block ごとに取る（guardrail:123、前例 `rpi5-fan.c:44〜47`）。実装の担当は値のために GPL の file を読むが、code・名前・構造・comment は写さない。
  呼び出しの手順（どの register に何を入れ、何を失敗の印とするか）は公開の `dell-smm-hwmon` の文書（§2.4）と 5330 の AML（`WMDM` の EAX〜EDX、§2.1）から組む。GPL の file からの「Procedure referenced:」が全 WS で許されるかは guardrail の字面が WS039 の節だけなので Q1 に確かめる（それまでは手順は GPL の file から取らない）。
- 5330 の観測の値は試験の期待の値として `plan/ws303/tests/` に置き、source の定数にしない。

## 9. ユーザーに決めてほしい点（推しつき）

- **Q303-15 起動の時の strategy**: (A, 推し) firmware のまま（受け手は何も書かない。表示は FIRMWARE と firmware の今の mode の名前）。(B) 前回選んだ strategy を `/etc` に保存し、起動の時に rc で `/sbin/thermal` が渡す（同じ値なら firmware に書かない。Dell は起動ごとに書くと BIOS の設定の書き・SMI になる）。
- **Q303-16 細粒度パワーゲート（gating）の軸が何に効くか**: (A) この WS の中で CPU の C-state の上限・PCIe の ASPM・NVMe の APST・GPU の power gating の受け手を作る（今は C-state の governor が無く、ASPM・APST は WS300、GPU は各 driver で、この WS に作業が無い）。(B) CPU だけ。(C, 推し) 軸は UAPI に持ち保存して示すだけ、受け手は各 WS（WS300 など）が後で足す。
- **Q303-17 WS301 の直の設定と strategy**: (A, 推し) 直の設定は strategy より優先し CUSTOM の印、次に strategy を選ぶと直の設定は消える（WS301 の Q301-7 の答え「CPU ごとの直の値は root の診断用だけ」に合う）。(B) 直の設定は MANUAL の時だけ許す。
- **Q303-18（新）3 つの軸の向き**: (A, 推し) どれも「100 = その物が多い」: speed 100 = 一番速い、noise 100 = 一番うるさくてよい、gating 100 = 一番省電力。(B) どれも「100 = 好ましい方」: speed 100 = 速い、noise 100 = 一番静か、gating 100 = 一番省電力。
- **Q303-19（新）firmware が fan を遅くする mode（Dell の Quiet）を選んでよいか**: (A, 推し) よい。MANUAL で noise ≤ 33 の時だけ Quiet を選ぶ（POWERSAVE は Cool）。OS は firmware の fan の曲線を変えられず、原文「ストラテジだけファームウェアに伝える」の形。(B) Quiet は選ばない（firmware の mode でも fan を遅くする物は使わない）。

## 10. 未確認（実装の前・中で確かめる）

1. Dell の mode の bit と名前の対、BIOS が起動の時に `ODV0` を埋めるか、mode が NVRAM に残るか、DTT の無い OS で mode が fan と電力（PL1・PL2）に効くか（p004）。
2. 延長の長さ 0 を firmware が受けるか（p004）。
3. SMM の fan の state が効くか、EC が上げる自由を保つか、BIOS が戻すか（p004）。
4. `IETM._STA` の `DDDR` が Debian の後の warm reboot で残るか（p004 で `\DDDR` を読む）。
5. kernel の中に非常の時に呼べる sync の口があるか（p003b）。
6. `/dev/thermal` の device の番号の空き（p003a）。
7. Q303-9 の「後で外す」の原文（Q1）。GPL の file からの手順の参照の可否（Q1、§8）。

## 11. 経緯（前の版の改訂の行き先）

- 第 2 版（R1〜R13）: R1 → §4.2 の今の mode。R2 → §4.2 の set。R3 → §4.1。R4 → §4.2 の fan の読み。R5 → §4.1（事象の口を外す）。R6 → BUG-351。R7 → §4.8（weak を使わない、範囲の登録）。R8 → §4.4（起動で書かない）。R9 → §4.1 の harness。R10 → §6.3。R11 → §8。R12 → §0。R13 → §4.3 の STALLED。minor → §4.1〜§4.3・§4.13。
- 第 3 版（T1〜T7）: T1 → §4.8。T2 → §4.4（名前は Q303-11 で 4 つに）。T3 → §4.3・§4.5。T4 → §4.6（hot の規則は第 4 版 V2 で外した）。T5 → §4.7（単位は千分率）。T6 → p005。T7 → §4.9（第 4 版 V4 と Q303-11 で「遅くしない」に）。
- 第 4 版（V1〜V10）: V1 → §2.5・§4.6。V2 → §4.3（boost の max、hot の規則なし）。V3 → §4.6（第 4 版の review の M8・M9 で改めた）。V4 → §4.9。V5 → §4.4（受け手の登録は WS301 p004、M3）。V6 → §4.7。V7 → §4.1。V8 → §4.3（源ごとの push、M7）・§4.14。V9 → §6.2・§6.3（M10）。V10 → §5.2。
- 第 4 版の review: B1 → この版の形。M1 → §4.4・§5。M2 → §4.11。M3 → §4.4。M4 → WS301 の第 3 版の改訂 1（Q301-7 は答えを受けて取り下げ、`/dev/thermal`、千分率）と Q1 への申し送り（master.md の写し）。M5 → §4.8。M6 → §4.3・§4.6。M7 → §4.3・§4.9。M8・M9 → §4.6。M10 → §6.2。M11 → §7 の p003a・p003b。m1〜m16 → 版の行、ws.md、§4.8 の事象、§4.7、§0、§1、§4.8 の cdev、§4.8 の ring、§4.3 の値の規則、§4.6 の S0 idle、§4.6 の見込み、Q303-16 の C、Q303-19、Q303-15 の B、§8、§6.3。
