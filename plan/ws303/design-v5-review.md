<!-- awesome-plan project=zedbsd record=ws303-design-v5-review -->

# WS303 設計の敵対的レビュー（design.md 第 5 版）

日付: 2026-10-29（host の `date`）。担当: design-reviewer（P5 の依頼）。file への保存は P5（reviewer は本文を返し、P5 がそのまま写した）。**この review は design.md に未反映**（2026-10-29 のラップアップの時点）。
対象: [design.md](design.md) 第 5 版（一つの本文、§0〜§11）、[ws.md](ws.md)、[user-questions.md](user-questions.md)。前の版の review の行き先は [design-v4-review.md](design-v4-review.md) と design.md §11 で照らした。関係: [WS301 design.md](../ws301/design.md)（第 3 版の改訂 2 を見た。改訂 3 で WS301 の §4.10 の lock の順は直した）。
基点: design.md の記す main 68ee65381。照合は worktree `p5-design-d`（HEAD 184a42bb0 = 68ee65381 の上の plan だけの commit）。d94ceac00..68ee65381 で引用の source は変わっていない（`git diff --stat`）。
読んだ物: `src/drivers/acpi/{acpi-thermal.c 全部, acpi-event.c:1000〜1105, acpi-kern.c:237〜254・410〜430, acpi-ec.c:700〜740, aml-sync.c:1〜72, aml-define.c:395〜410}`、`src/drivers/platform/rpi5/rpi5-fan.c`（40〜70、180〜340、385〜396、995〜1245）、`src/kern/{sysctl.c の hw.thermal, signal.c:425〜500・1405〜1432, process.c:2698〜2712, shutdown.c:37〜120, sleep.c:1〜215, clock.c:120〜175, mount.c:940〜960, vfs.c:455〜475, devfs.c:580〜610, platform/{pcat.c:655〜680, rpi5.c:298〜315}}`、`src/drivers/generic/system-device.c`（280〜370、540〜580）、`include/{uapi/sysctl.h:236〜313, uapi/system.h:305〜372, uapi/ioctl.h, kern/cdev.h, kern/lock.h, kern/sched.h:15〜35, kern/boot.h:100〜200, kern/process.h:200〜212, kern/sysctl.h:80〜100}`、`userland/base/init/main.c`（36〜90、285〜360、1835〜1966、2267〜2274、2462〜2480）と `services/`、`userland/desktop/sessiond/power-rules.c`、`config/kernel-options.list`、`Makefile:414・947〜948`、`plan/guardrail.md:112〜125`、`docs/architecture/power-management.md:20〜28`、`plan/master.md:141〜143`。firmware（scratchpad の `ws303-acpi/`）の `dsdt.dsl` と `out-ssdt3.dsl` の該当の行。
**GPL・LGPL の source は読んでいない。web は使っていない。build・実行はしていない（試験は未実施）。**

重大度: **blocker**（実装に入ると目的・安全の道を壊す、または一意に読めない）、**major**（実装の前に設計を直す）、**minor**（実装の中で直せる・文書の整合）。「推測」は source・表で確かめられなかった事。

## 要点

- blocker 0、major 9、minor 25。第 4 版の B1 は解消。§7 が Phase の表の本体で ws.md の写しと合う（35 LW）。
- 第 4 版の review の解消が部分的: M1 の 5（WS301 の ETIMEDOUT の後で効く場合）、M2（WS301 の側の lock の順）、M4（WS301 の古い注記）、M8（init の上書き・停止の途中）、m10・m11 の事実（下の M3・M4）。

## 第 4 版の review の指摘の解消

| 指摘 | 判定 |
| --- | --- |
| B1 層の形 | 解消 |
| M1 strategy の UAPI・受け手の口 | ほぼ解消。WS301 の ETIMEDOUT（要求は箱に残り後で効く）を失敗に数え、後で効いても結果を直さない。Dell の EBUSY との組は新しい M1 |
| M2 lock の順 | WS303 の側は解消。WS301 design.md:224（改訂 2）は逆の順のまま（新しい M2） |
| M3 受け手の持ち主 | 解消（WS301 p004）。WS301 design.md:382 に古い注記 |
| M4 WS301 の記録 | ほぼ解消。WS301 design.md:146 が「WS303 design の V5」を指す（m16） |
| M5・M6・M7・M11 | 解消（M11 は p003a の review の待ちが残る、m23） |
| M8 handler の無い init・温度で切らない | (a)(b) 解消、(c) は 2+2 秒。init の上書き・停止の途中・60 秒の門は新しい M4 |
| M9 `_PTS` | 解消 |
| M10 QEMU の 2 本 | ほぼ解消。記録の file の仕様が無い（m13） |
| m1〜m9・m12〜m16 | 解消 |
| m10 S0 idle | 事実が誤り（M3） |
| m11 120 秒の見込み | 数が誤り（M4） |

## Major

### M1. strategy の世代の追い越しで、前の受け手が古い世代のまま残る（design.md:192〜194、161）

- 根拠: SET の (4)「途中で新しい世代が来たら、残りの受け手には新しい世代を渡す（古い世代は捨てる）」。既に g を渡した受け手に g+1 を渡し直す手順が無い。g を待つ ioctl に返す値も無い。Dell は「set は 1 秒に 1 回まで（速い求めは EBUSY）」。
- 起こる状況: 順が WS301 → Dell で、SET(POWERSAVE) の途中に SET(PERFORMANCE)。WS301 は POWERSAVE、Dell は PERFORMANCE、写しは PERFORMANCE。渡し直しを足しても Dell が 1 秒以内の 2 回目で EBUSY → PARTIAL で Dell の mode が古いまま。§6.1 の「世代の追い越し」の試験は書かれた振る舞いを確かめるだけで見つけない。
- 直し方の案: 1 周の後に最新の世代が今の周と違えば全部にもう 1 周（最新が勝つ）。受け手ごとに「適用済みの世代」を持ち GET に出す。古い世代の ioctl は「上書きされた」の flag で 0。Dell の 1 秒の制限は EBUSY でなく受け手の中で次の窓まで眠る。WS301 の ETIMEDOUT は「受け付けたが未確認」の flag にして失敗に数えない。

### M2. WS301 design との契約が食い違う（WS301 design.md:146〜147・224・274〜275・382、WS303 design.md:256〜257・353）

1. lock の順: WS301 :224「WS303 の strategy の mutex の下から呼ばれる」と WS303 §4.11「受け手を呼ぶ間は持たない」（WS301 の改訂 3 で WS301 の側を直した）。
2. 上限の口: WS301 `kern_cpuperf_limit(source, domain, permille)` と WS303 :353 `int (*limit)(uint32_t source, uint32_t permille)`（domain が無い）。WS303 §4.7 は 3 つの引数で書く。
3. WS301 :274 `kern_cpuperf_strategy(...) /* the receiver WS303 calls */` は直に呼ぶ symbol に読める。WS303 は登録の口だけ。
4. WS301 §4.4「受け手は結果（適用・一部・対応外）を返す」と WS303 の apply は int だけ。「対応外」の errno も決めていない。
5. WS301 §7:382「WS303 の p007（strategy の受け手の登録と passive）」は古い（登録は WS301 p004）。
- 直し方の案: WS303 §5.1 の上限の口を `(source, domain, permille)` にする（WS303 は ALL を渡す）。apply の戻りを「0・EOPNOTSUPP（対応外）・他の errno」と部分の flag の組にする。WS301 :224・274・382・147 を直す。

### M3. S0 idle の時計の事実が誤り（design.md:210・215、§6.1:376）

- 根拠: 「S0 idle の間に 120 秒の timer は進まない」。実際は CPU 0 が suspend-to-idle の前後で tick の数を眠った時間だけ進める（`src/kern/sleep.c:27〜28`、`src/kern/clock.c:138〜174`）。S0 idle は user process を凍らせ（`sleep.c:12〜13・173`）、その後 device を suspend（`sleep.c:176`）。KERN_SYSTEM_SLEEP の EBUSY は入口の判定だけ。
- 起こる状況: (1) 凍らせた後の非常では SIGUSR2 が init に届かず、60 秒が resume で一度に過ぎ、起きた直後に sync なしの直の切り。(2) 30 秒の古さが resume の直後に全 sensor で真になり、最後の値が critical 以上なら新しい読みを待たず非常。resume を知る口を名指していない。(3) host 試験は模擬の時計で前提を確かめるだけ。
- 直し方の案: timer と古さは sched_ticks（跳ぶ時計）で数えると明記し、resume の口（`sleep.c` の post か新しい callback）で古さの基準を置き直す。非常の判定と S0 idle の入口を 1 つの atomic の flag で排他にし、sleep の途中の非常は sleep を中止。少なくとも「凍る前の非常は sleep を EBUSY、凍った後は直の切り」と決める。

### M4. init の道の状態の穴（design.md:211〜213・244〜247、`userland/base/init/main.c:2267〜2274・2470〜2480・1844〜1866・1957〜1963`）

1. init の `action_requested` は SIGINT（REBOOT）・SIGTERM（HALT）・制御の要求（2267〜2274）で上書きされる。
2. 通常の停止の途中の SIGUSR2 は見られず、猶予は 5+5 秒（`STOP_GRACE_MS`）、終わりの action は前の要求。
3. kernel の状態遷移は「init が POWEROFF に入る」だけを見る。REBOOT・HALT を呼ぶと熱い機械が reboot か、HALT で見張りの thread が走れない（推測）。
4. 60 秒の門は init が全 service を止めて sync した後にしか越えない。service は順に止める（1860〜1861）。「base の service 7」は `services/` の数で、`/etc/service.d` に入る他の物で 14 前後（推測）。14 × 4 + 4 ≈ 60 秒で門に当たる。213 の「132 秒で 120 秒を超えうる」は 60 秒の門が先に来る事と食い違う。
- 直し方の案: (a) init の非常の要求は上書きされない（停止の途中なら猶予を 2+2 秒に縮め、終わりを POWEROFF に）。(b) kernel は非常の間 REBOOT・HALT を POWEROFF として扱う（`system-device.c:540〜580`）。(c) 60 秒の門を「init が非常の停止を始めた」の印（ioctl か ack）にするか、90 秒にして見込みを image の service の数で書き直す。(d) host 試験に REBOOT との競合・停止の途中の SIGUSR2。

### M5. 直の切りの sync が止まりうる。電源断の失敗の後の道が無い（design.md:214、§10.5、`acpi-event.c:1025〜1036・1099〜1104`、`src/kern/platform/pcat.c:666〜676`）

- 根拠: sync の口は今ある `mount_sync_all`（`src/kern/mount.c:946`）で、呼び手の thread で時間の上限なしに走る。`drv_acpi_poweroff` は ENODEV・ETIMEDOUT を返す。
- 起こる状況: 見張りの thread が直に呼ぶと、高温で storage が止まった時に返らず電源断に届かない。電源断の失敗の後は何もしない。
- 直し方の案: sync は別の kernel の thread に頼み、見張りは 10 秒だけ待って進む。失敗の後は `kern_platform_halt`、GET_STATUS に「切れなかった」。§10.5 を直す。

### M6. 誤った非常の停止を止める口が無い（design.md:179〜180・208、§6）

- 根拠: 値の規則は 127 °C 以上と −40〜150 °C の外だけ。`_CRT` が低すぎる firmware（推測）では起動のたびに電源が落ちる。止める parameter が無い。ACPI の zone を持つ amd64 の機械での確かめが無い（SV7・5320 に zone があるかは未確認）。
- 直し方の案: (a) 水準の検査（critical が 50 °C 未満か起動の時の温度 + 5 °C 未満なら「無し」と log）、(b) 止める起動の parameter か loader の設定、(c) p003b の受け入れに「zone を持つ amd64 の機械で ZONE と critical を示し非常にならない」。(a)(b) は p003b の UAPI の review に含める。

### M7. Phase の依存の誤り（design.md:283・402〜408、`rpi5-fan.c:330`）

1. p009（`hw.thermal` を外す）は p003a だけに依るが、rpi5-fan.c を push に変えるのは p006。p009 が先だと arm64 の build が落ちる。
2. p005 の QEMU の受け入れは注入の build（p003b）に依るのに、依存は p003a だけ。
- 直し方の案: p009 の依存に p006 を足すか、p003a で rpi5-fan.c も push に。p005 の依存に p003b を足すか、模擬の受け手を host 試験に移す。ws.md の写しも同じ commit で。

### M8. Dell の mode の確かめが働かない（design.md:157〜161・249・391、§10.1、`dsdt.dsl:91264〜91297・1236〜1237`）

1. `WMBA` は SMI より前に AML の中で `ODV0` を書く（91264〜91291 で ODV0 と Notify、91295〜91297 で SMI）。SMI が飛ばされても `ODV0` は新しい値。「SMI が起き ODV0 が合う --> READY」は firmware が受けた証拠にならない。resume の後の `ODV0` の読みも誤った mode を出しうる。
2. 試験の 3（zedBSD で set → Debian で `platform_profile`）は mode が再起動で残る事（未確認）と Debian が profile を書き直さない事（power-profiles-daemon など、未確認）に依る。
- 直し方の案: (1) 成否は sentinel の書き換えだけ、`ODV0` は表示の補い。resume の後は kernel が覚えた値を使う。(2) 試験の 3 の前に「Debian で set → 再起動 → 読む」で残る事、profile の daemon が無い事を読みだけで確かめる。残らなければ Q303-7 の方法が使えないので Q1 へ。

### M9. SMM の boost の手順の出典が根拠に無い（design.md:173・419〜420、§2.4:105〜106）

- 根拠: 「手順は公開の `dell-smm-hwmon` の文書と 5330 の AML から組む」。文書の散文は命令の符号・引数・失敗の印を書いていない（推測、全文は読んでいない）。`WMDM` は EAX〜EDX を SMI に渡す包みだけ。Q1 の答えで GPL の file から手順を取らない。
- 起こる状況: p004 の担当が失敗の印（返った EAX の比べ方）を値か手順か決めきれず、規則に触れるか uncleared になる。
- 直し方の案: p004 の前に GPL の file から取る値の一覧を決める（命令の符号、fan の番号の符号、state の範囲、失敗を示す返りの値）。「失敗の印の値」を値として扱ってよいかを Q1 に確かめる。手順は「書いた後に get と DDV の rpm で読み直して確かめる」と自分で組み、確かめられなければ boost は「無し」。

## Minor

- **m1** §4.14「/dev/system は変えない」と、§4.6 が要る 2 つの hook（POWEROFF に入った事を知る口 `system-device.c:552〜568`、KERN_SYSTEM_SLEEP の EBUSY `system-device.c:320〜370`）が食い違う。「UAPI は変えない、system-device.c に 2 つの hook」と書き、file の所有を Q1 に。
- **m2** apply の引数が §4.4:196（3 つ）と §5.1:345（2 つ）で違う。§5.1 に揃える。
- **m3** SET(FIRMWARE) の意味が無い。SET(FIRMWARE) は EINVAL、FIRMWARE は起動の時の読みだけと書く。SET の時の GET だけの欄の扱いも。
- **m4** passive の hysteresis が事象（2 °C、§4.8）と制御（3 °C、§4.7）で違う。1 つに。
- **m5** §2.1:60「ECM1 の下」は method 0x20 に当たらない（`dsdt.dsl:94873〜94921` に Acquire が無い）。「0x20 以外は」に。
- **m6** 引用の行のずれ: rpi5 の lock（185〜221 → field 224、struct 201〜226、契約 193〜200）、周期（68 → 66）、`power-management.md:24` → 23、`acpi-event.c` の event の lock 1056 → 1058。他は合う。
- **m7** `include/uapi/sysctl.h` の接頭辞なしの `THERMAL_KIND_*`・`THERMAL_FLAG_CPU`・`THERMAL_HAVE_*` と §5.2 の名前がぶつかる。接頭辞の規則を書く。hw.thermal の 1 source は 16 個まで（`sysctl.c:148`）で registry の 32 を出しきれない。source を 2 つか 16 で切ると書く。
- **m8** 非常の手順の `process_find_ref(1)`・`signal_send_process` は写しの spinlock（125）を離してから（process の lock は 10〜20）。見張りの thread の優先度を高めに（`SCHED_PRIORITY_DEFAULT` 8、`sched.h:25`）。
- **m9** STALLED を「実際の段（max(自動, boost)）> 0」で判定する。
- **m10** BIOS が boost を戻した事の判定に SMM の get が要るなら周期の SMI になる。DDV の rpm で推すか SET の時だけ読む。worker 1 本で SMI と DDV の止まりが passive・strategy を待たせる事も書く。
- **m11** `CONFIG_THERMAL_TEST_INJECT` の platforms を `amd64,arm64`（要れば riscv64）に。
- **m12** `thermal.emergency_test=nosignal` は `include/kern/boot.h:107〜130` の固定の enum に key が要る。注入の ioctl の flag で足りる。
- **m13** init が書く非常の記録の file の場所・形・製品の build でも書くかが無い。前の回の file が残る恐れ、起動の番号か注入の時刻を入れる。
- **m14** §10.5 は `mount_sync_all`（`mount.c:946`）で答えられる。
- **m15** `kern_platform_poweroff_emergency` は pcat と rpi5 だけ。他の 9 つの platform の既定を書く（全 platform の build に要る）。
- **m16** ws.md:36・38 は第 5 版に無い節を指す。WS301 design.md:146「WS303 design の V5」も。
- **m17** 出典の字面は「Value referenced:」か「Values referenced:」（guardrail:122、前例 `rpi5-fan.c:44`）。
- **m18** §6.3 の 2・4 に合否の条件が無い。合否の手順と記録だけの手順を分ける。
- **m19** 注入の sensor は外さないので、同じ名前は同じ sensor に push すると書く（32 の枠）。
- **m20** ioctl の範囲の登録と振り分けの公開の順（release・acquire）、WS301 の SET の root の判定は WS301 の handler、`include/uapi/thermal.h` の所有と p003a の review に WS301 の struct を含めるか。
- **m21** attach の試し直し（1 秒おきに 3 回）は起動を 3 秒延ばす。worker で。
- **m22** 事象の ring の溢れに OVERFLOW の record（`system.h:316〜319` と同じ）。
- **m23** p003a の中のユーザーの review の待ちは Queue の途中で止まる。UAPI の案を p001 の残りか独立の小さな Phase に。p003a の 6 LW は少なめ。
- **m24** §2.3:85 の「ACPI の thermal zone」は Linux の sysfs の名前の事で、§2.5「thermal zone は無い」と食い違う。「Linux の thermal_zone（DPTF の participant）」と書く。
- **m25** §4.10 に、30 秒の古さの道・電源断の失敗・REBOOT/HALT・ETIMEDOUT の後に効く場合が無い。WS301 の p005 は p004 に依らない（WS301 への申し送り）。

## 確かめて正しかった主張（要約）

hw.thermal の UAPI・registry・読み手 2 つ。init の handler と SIGUSR2 の既定、pid 1 を守る code が無い事。`signal.c:435`・`process.h:206〜207` の口。`drv_acpi_poweroff` は `_PTS` の後に event の lock、S5 の値は起動の時に覚えている。devfs の mode。ioctl の group 'T' が空き。`/dev/system` の事象の名前空間と 128 byte。PNP0C14 9 つと `_WDG` の Name 4・Method 5、DIAG の `_WDG`。GUID の byte の順。WMBA・SMBF の振る舞い。method 0x20 の入力と EC の番地、`\ECRD` = 0 の時の EISC。`aml-define.c:403〜406`。Pi 5 の sensor・critical 110 °C・読めない時の全開。sessiond の POWER の判定。注入の option の前例。guardrail:123 の GPL の値の扱い。HAL の API の変更は要らない。判定は console の log に依らない。
