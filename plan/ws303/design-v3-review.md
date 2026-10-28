<!-- awesome-plan project=zedbsd record=ws303-design-v3-review -->

# WS303 設計の敵対的レビュー（design.md 第 3 版）

日付: 2026-10-29（host の `date`、03:43 JST）。担当: design-reviewer（P5 の依頼）。対象: [design.md](design.md) の「第 3 版の改訂」（ユーザーの回答、解釈 1〜8、T1〜T7、第 3 版の Phase の表、Q303-9〜14）と、それが「第 2 版の改訂」・本文と食い違わないか。[ws.md](ws.md)。関係: [WS301 design.md](../ws301/design.md)（第 1 版の草稿、`kern_cpuperf_limit`）、[design-review.md](design-review.md)（第 1 版の review）。

照合した物: worktree `p5-design-b`（HEAD 51fc7a9c7、`plan/ws301/design.md` は未 commit）。読んだ source:
`src/drivers/acpi/acpi-thermal.c`（全部）、`src/drivers/acpi/acpi-event.c`（`drv_acpi_poweroff` 1010〜1100）、`src/kern/sysctl.c`（hw.thermal の registry 140〜245）、`include/uapi/sysctl.h`（236〜313）、
`src/drivers/platform/rpi5/rpi5-fan.c`（1〜340、1000〜1245）、`src/drivers/generic/system-device.c`（100〜175、320〜345、480〜592）、`src/kern/shutdown.c`（1〜140）、`include/kern/platform.h`、`src/kern/platform/{pcat.c,rpi5.c}` の poweroff、
`src/kern/system-event.c`、`include/uapi/system.h`（311〜368）、`src/kern/devfs.c`（570〜605）、`src/kern/vfs.c`（455〜470）、`src/kern/signal.c`（関数の一覧）、`src/kern/sleep.c`（頭の comment）、`include/uapi/ioctl.h`、`include/uapi/*.h` の ioctl の group、
`userland/desktop/sessiond/{power-rules.c,power.c,session.c,greeter.c,main.c,Makefile}`、`userland/base/{poweroff,halt}/Makefile`、`userland/base/shutdown-control/main.c`、`userland/base/init/main.c`（signal と最後の system action）、`userland/desktop/libkeiland-backend-zedbsd/monitor-zedbsd.c`（650〜675）、`plan/guardrail.md`（112〜130）。
firmware の事実: scratchpad の `ws303-acpi/out-ssdt3.dsl`（DPTF の participant の `_PSV`・`_HOT`・`_CRT`・`_TMP`・`KDRT`）と `dsdt.dsl`（`\_SB.DIAG` 94054〜）。ACPI 6.5 11 章の保存の text（`ws303-fable/acpi65_thermal.txt`）。観測: `ws303-5330/survey.txt`。
GPL・LGPL の source は読んでいない。web は使っていない。何も build・実行していない（試験は未実施）。

重大度: **blocker**（このまま実装すると目的を壊す・安全の道が成り立たない）、**major**（実装の前に設計を直す要がある）、**minor**（実装の中で直せる・文書の整合）。「推測」は source・表で確かめられなかった事。

## 要点

- **blocker 2 件**: (B1) 5330 の DPTF の participant の実の値では、T4（hot・critical）と T5（passive）が段にならない。TCPU は `_PSV` = `_HOT` = `_CRT` = 110 °C（同じ 1 つの値）、TMEM・TSKN・NGFF の `_HOT`・`_CRT` は 127 °C、効くのは AMBF（ambient）の `_CRT` 80 °C だけ。p007 の受け入れ（5330 で TCPU が passive を超える）は届かない見込み。(B2) 非常の停止の道（kernel → init → poweroff、60 秒で直に切る）が今の code の口で成り立たない。init に poweroff の signal は無く（SIGTERM は halt、SIGINT は reboot）、`KERN_SYSTEM_POWEROFF` は pid 1 だけで、直の切りは AML の interpreter の lock（`_PTS`）と shutdown の準備の join に依る。sensor の読みが止まった時も気づかない。
- **major 9 件**: Pi 5 の quiet（trip +5 °C）がユーザーの「下回らせない」に反する（ユーザーの回答どうしの食い違いを聞いていない）、hot = critical − 5 °C と「hot で boost を最大」は原文に無いのに問いにしていない、strategy → WS301 の policy の持ち主・起動の時の値・部分の失敗、`kern_cpuperf_limit` の契約（hybrid の単位、下限、WS301 p003 の HAL の承認への隠れた依存）、boost の DIAG は `_WDG` が Method で R3 の「Name だけ」に外れる、2 つの registry（hot の欄が無い、周期のずれで「2 回続けて」が同じ読みを 2 回数える、名前の衝突）、5330 の boost の「下回らせない」を判定する試験が無い、非常の停止の道を端から端まで試す計画が無い、`/dev/thermal` の UAPI の定義が無く本文 §5（`KERN_FAN_MODE_MANUAL` など）が第 3 版と食い違ったまま。
- **minor 15 件**（下）。
- 確かめて正しかった主張は末尾の表（sessiond の root か wheel の規則、`struct system_event` 128 byte、group 'T' の空き、12 の `vmunix.mk`、Pi 5 の段 0〜4、Phase の LW の合計 29 など）。

## 解釈と原文の一致（依頼の (1)）

| 解釈・設計の記述 | 原文 | 判定 |
| --- | --- | --- |
| 解釈 1・T1「`/dev/thermal` に温度・fan・strategy・boost・事象」 | Q303-1「/dev/thermal」（第 1 版 Q303-1 の C は「温度・fan・profile を新しい device に」） | 合う |
| 解釈 2・T6「sessiond 経由は POWER と同じ root か wheel」 | Q303-2「rootだけが変えられるが、sessiond経由でも変えられる」 | wheel への限定は原文に無い。Q303-10 で聞いているので可。原文の字面は B（session の user）に近い（minor m2） |
| 解釈 3・T2「strategy は WS301 の policy・Dell の mode・Pi 5 の trip の組」 | Q303-2「/sbin/thermalからDVFSストラテジを選択」、Q303-5「ストラテジだけファームウェアに伝える」、Q303-4「profile を作る」 | 組にする読みは原文で支えられる。ただし WS301 の側の決定（root だけ、Q301-6 の推し A「連動しない」）と食い違う（major M3）。Q303-11 の B は「DVFS の strategy が DVFS を変えない」ので原文と合わない（minor m14） |
| 解釈 4・T3「実際の段 = max(自動, boost)」 | Q303-2「強制的にファンを大きくする（何段階かあり）ことはできるが、実際の温度に対して必要なファン回転数を下回らせるオーバーライドは不能。」 | 合う。ただし T7 の quiet（trip +5 °C）と本文 §5.1 の `KERN_FAN_MODE_MANUAL` がこの規則に反する（major M1・M9） |
| 解釈 5「critical に近づいたら回転を上げる」、T4「hot = critical − 5 °C」「hot で boost を最大」 | Q303-5「温度に対して必要な回転数を下回らせることはできず、回転を上げる。」 | 原文の「回転を上げる」は「下回らせない、上げる方向だけ」の言い直しとも読め、「critical の手前で最大にする」は飛躍。−5 °C は原文に無い。どちらも Q303-9〜14 に無い（major M2） |
| 解釈 5・T4「非常の水準 = `_CRT`、秩序だった停止、60 秒で直に切る」 | Q303-5「非常水準に達したとき、電源を落とす。」 | 「秩序だった停止」「60 秒」「2 回続けて」は P5 の足し。Q303-13 で聞いているので可。ただし T4 の本文は決定の形で書いている（Q303-13 の答えまで「推し」と書く） |
| 解釈 6 | Q303-5「コントロールできないときはファームウェアにまかせたり、ストラテジだけファームウェアに伝える。」 | 合う |
| 解釈 7・T5「passive を入れる、`_PSV` で WS301 の上限」 | Q303-6「入れる」（第 1 版 Q303-6 の B「WS301 p002 の後に、`_PSV` で CPU の上限を下げる Phase を足す」） | 合う。ただし 5330 の `_PSV` の値では動かない（blocker B1） |
| 解釈 8「第 2 版の R の判断を保つ」 | — | R3（Name の `_WDG` だけ）と T3 の DIAG の boost が食い違う（major M5） |
| Q303-3 の読み（boost を `/sbin/thermal` から） | Q303-3「さっき書いた/sbin/thermal」 | 第 1 版 Q303-3 は「5330 の fan の手動の制御を入れるか」。答えは「`/sbin/thermal` で（上げる段を）入れる」と読め、Q303-12 で SMM の手段を聞き直すのは妥当 |

## Blocker

### B1. 5330 の participant の値では T4・T5 が段にならない（T4、T5、p007 の受け入れ、Q303-13・14）

- 根拠（`out-ssdt3.dsl`、値は `CTOK(x) = x × 10 + 0x0AAC` の 0.1 K、564〜567）:
  - TCPU: `Name (TJMX, 0x6E)`（1390）、`_PSV`・`_CRT`・`_CR3`・`_HOT` が全部 `CTOK(TJMX)`（1466〜1484）。→ passive = hot = critical = **110 °C**。
  - TMEM（2021〜2025）: `_PSV` 70 °C、`_CRT`・`_HOT` 127 °C（0x7F）。TSKN（2146〜2150）: `_PSV` 56 °C、`_CRT`・`_HOT` 127 °C。NGFF（2271〜2275）: `_PSV` 50 °C、`_CRT`・`_HOT` 127 °C。
  - AMBF（2396〜2402）: `_PSV` 65 °C、`_CR3` 70 °C、`_HOT` 75 °C、`_CRT` **80 °C**。
  - どれも Name で、AML の中で書き換える store は無い（`grep` で Name と Return だけ）。zedBSD は thermal zone が無く、全部 `THERMAL_KIND_DEVICE`（`acpi-thermal.c:10〜13`、visitor 152〜157）。
  - ACPI 6.5 §11.4.4（保存の text 885〜890）: `_CRT` が device の下にある時は「その device の driver が device の critical の trip として使う」。system を落とす `_CRT` は thermal zone の物（§11.1.6、410〜）。§11.1（172〜175）も device の埋め込みの trip は「device を落とすか system を落とすかを OSPM が決める」。
  - i5-1245U の TjMax は 100 °C（推測、Intel の公開の仕様の記憶、未確認）。CPU は TjMax で自分で throttle し、TCPU の `_TMP`（EC の `KDRT(0)`、1162〜1172）は 110 °C に届かない見込み（推測）。
- 起こる状況:
  1. T5 の passive は TCPU（`THERMAL_FLAG_CPU` は TCPU だけ、`acpi-thermal.c:211〜231`）で 110 °C からしか動かず、CPU が TjMax で自分で絞るので**一度も動かない**見込み。p007 の受け入れ「5330 で TCPU が passive を超えた時に上限が下がる記録」は取れない。Q303-14 の B（hot を超えた時だけ）も TCPU では 110 °C。
  2. T4 の「hot（無ければ critical − 5 °C）」は TCPU に `_HOT` があるので −5 °C が当たらず、hot と critical が同じ 110 °C。「hot で boost を最大 → critical で停止」の段が無く、同じ周期で両方が起きる。
  3. 非常の停止を実際に決めるのは AMBF の 80 °C だけ。DTT（Windows）がこの device の `_CRT` を system の停止に使うかは公開の資料に無い（推測）。DTT の無い OS で ambient の 80 °C を system の停止の閾値にしてよいかは不明。
  4. 127 °C（0x7F）は「定めない」の印と見られる（推測）。zedBSD の範囲の検査は 200 °C まで通す（`acpi-thermal.c:44〜49`）ので、EC の byte（`KDRT` は `EXRW` の 1 byte、174〜178）が 0x80〜0xC8 の誤りの値（例 0x80 を「無い」の印に使う EC、推測）を 2 周期返すと 128 °C ≥ 127 °C で**誤って停止**する。
  5. design の T4 の最後「5330 の `_CRT` は p004 で値を読んで確かめる（未確認）」は、表の静的な値なので今読める（上の値）。
- 直し方の案:
  - 水準の決め方を sensor の種類で分ける: thermal zone の `_CRT` だけを system の非常の停止に使い、device（DPTF の participant）の `_CRT` は既定では事象だけ（停止には使わない）にする。5330 の停止は CPU の hardware（TjMax の throttle、THERMTRIP）と EC に任せる、を Q303-13 の選択肢に足す（ユーザーの「非常水準」を何で決めるかの聞き直し）。
  - 値の検め: `_CRT` が 127 °C 以上（0x7F 由来）は「定めない」と扱う。1 周期で 20 °C 以上跳ねた値・0x80 以上の生の byte 由来の値は「読めない」と扱う（plausibility）。
  - passive は 5330 では TCPU の `_PSV` が使えないので、(a) 5330 では passive は効かない（受け入れから外す）、(b) TSKN（skin、56 °C）・TMEM（70 °C）の `_PSV` も CPU の上限の引き金にする、(c) 固定の値（例 TjMax − 10 °C）、を Q303-14 に足す。p007 の受け入れは選んだ方に合わせて書き直す。
  - hot は `min(_HOT, _CRT − 5 °C)` のように「critical より必ず下」にする（−5 °C 自体は M2 で聞く）。

### B2. 非常の停止の道が今の口で成り立たない（T4、Q303-13、p003）

- 根拠:
  - init の signal は SIGHUP = 読み直し、SIGINT = **reboot**、SIGTERM = **halt**（`userland/base/init/main.c:293〜296`、2470〜2480）。poweroff に当たる signal は無い。`/sbin/poweroff` は init の ZSV1 の socket で頼む（`userland/base/shutdown-control/main.c:69`、init 側 2265〜2270）ので、kernel からは使えない。T4 の「`/sbin/poweroff` と同じ道、kernel から init への signal の形は p003 で既存の口を確かめる」の既存の口は無い。
  - `KERN_SYSTEM_POWEROFF` は pid 1 だけ（`system-device.c:552〜555`）で、`system_shutdown_prepare()` → `kern_platform_poweroff()`。T4 の「kernel が直に電源を切る（`KERN_SYSTEM_POWEROFF` の中の処理）」は ioctl の中の処理を kernel の thread から呼ぶ形になる。
  - `system_shutdown_prepare` は先の呼び手が終わるまで後の呼び手を `sched_yield` で回して待つ（`src/kern/shutdown.c:65〜72`）。init が先に入って disk の flush で止まっていると、60 秒の後の kernel の切りも**永く待つ**。
  - pcat の `kern_platform_poweroff` → `drv_acpi_poweroff` は先に `\_PTS(5)` を AML で評価する（`acpi-event.c:1038〜1048`）。全ての AML は 1 つの interpreter の lock で直列（design §1、`aml-sync.c:13`）。DDV の EC の取引が返らない時（第 2 版 R4・§4.10 の `Acquire(ECM1, 0xFFFF)`）は `_PTS` も**返らない**。
  - 同じ AML の止まりで acpi-thermal の thread も `thermal_refresh` の中で止まり、写しは古い値のまま `time_ns` だけが古くなる（`acpi-thermal.c:247〜270、286〜311`）。T4 は写しの古さを見ないので、温度が上がっても非常の判定に届かない。
  - init の停止は service ごとに SIGTERM の後 5 秒・SIGKILL の後 5 秒を待つ（`STOP_GRACE_MS`、`init/main.c:64〜70`、1930〜1966）を後ろから順に回す。service の数によっては 60 秒の中に sync（1867）まで届かず、60 秒の切りが sync の前に入る。
- 起こる状況: (1) kernel が init に頼む手段が無いので p003 で発明する事になる（SIGTERM を使えば halt で、pcat の halt は電源を切らない）。(2) 60 秒の直の切りが AML の止まり・shutdown の準備の待ちで永く止まり、温度が上がり続ける。(3) 秩序だった停止が進んでいるのに 60 秒で sync の前に切る。(4) sensor の読みが止まると、停止が始まりもしない。
- 直し方の案:
  - init に poweroff の signal を足す（例 SIGUSR2 → `INIT_ACTION_POWEROFF`）。`userland/base/init/main.c` の変更を p003 の内容と受け入れに書く（init の挙動の変更なので Q1 に範囲を確かめる）。kernel は `signal_send_process`（`src/kern/signal.c:435`）で pid 1 に送る。init がまだ無い（起動の直後）時の扱いも決める。
  - 直の切りは ioctl の道を通さない専用の関数にする: `system_shutdown_prepare` を呼ばない（または `shutdown_preparation_state` が 1 の時は待たない）、`_PTS` は interpreter の lock を時間つきで取れた時だけ（取れなければ飛ばす。lock の try の口は p003 で既存を確かめる、未確認）、その後 `kern_platform_poweroff`、返れば halt。
  - 60 秒は「init が `KERN_SYSTEM_POWEROFF` に入ったか」で延ばす（入った後は prepare の flush を待つ上限を別に持つ）。温度だけで止めない（minor m9）。
  - 非常の判定に写しの古さを入れる: CPU の sensor が 3 周期（15 秒）以上新しくならなければ「読めない」として事象と boost を最大（停止はしない）。

## Major

### M1. Pi 5 の quiet（trip +5 °C）がユーザーの「下回らせない」に反する（T7、Q303-11）

- 根拠: Q303-2「実際の温度に対して必要なファン回転数を下回らせるオーバーライドは不能」、Q303-5「温度に対して必要な回転数を下回らせることはできず」。一方 Q303-4 の答え「profile を作る」が選んだ第 1 版の C は「profile（cool・quiet・balanced を trip の上下で作る）」。T7 は quiet = trip +5 °C（Pi 5 の既定の trip 50・60・67.5・75 °C、`rpi5-fan.c:52〜67、234〜247`）で、同じ温度で fan を device tree の値より遅くする。
- 起こる状況: device tree の trip を「必要な回転数」と読むなら、quiet は OS 自身が「必要な回転数を下回らせる」上書きになる。5330 の Dell の Quiet は firmware の判断（原文の「ストラテジだけファームウェアに伝える」）だが、Pi 5 は OS が fan を回すので同じ言い訳が立たない。ユーザーの回答どうしが食い違っているのに、P5 は片方（+5 °C）を推しに置き、食い違いを聞いていない（AGENTS.md §8「曖昧な時・既存の事実と食い違う時は記録の前に聞く」）。
- 直し方の案: Q303-11 か新しい問いで「Pi 5 の quiet は (A) trip を上げない（fan は device tree のまま、CPU だけ WS301 の powersave）、(B) trip を上げる（ユーザーが Q303-4 の C で選んだ形、必要な回転数を device tree より下にする事をユーザーが受け入れる）」を、2 つの回答の食い違いを明記して聞く。答えまで T7 の quiet は A で書く。

### M2. hot = critical − 5 °C と「hot で boost を最大」は原文に無く、問いにもしていない（解釈 5、T4、T1 の `THERMAL_GET_SENSOR`）

- 根拠: 原文 Q303-5 は「回転を上げる」「非常水準に達したとき、電源を落とす」まで。hot の水準・−5 °C・hot で boost を最大は Q303-9〜14 のどれにも無い。ACPI の `_HOT` は「S4 に移ってよい温度」（ACPI 6.5 §11.4.7、保存の text 935〜940）で、fan を上げる水準ではない（`_ACx` が active cooling）。
- 起こる状況: P5 の足しが決定のように実装される（第 2 版 R12 と同じ種類の誤り）。5330 では boost が無い見込み（Q303-12）、Pi 5 は 75 °C で既に最大の段 4（`rpi5-fan.c:61〜62`）なので hot の boost は空の動作。−5 °C は B1 のとおり 5330 の TCPU では当たらない。
- 直し方の案: Q303-13 に「hot の水準と動作（`_HOT` があればそれ、無ければ critical − 5 °C、超えたら boost を最大と事象）」を推しとして足す。T4・解釈 5 は「推し（Q303-13）」と書く。`_HOT` を S4 の意味で使わない事も書く。

### M3. strategy → WS301 の policy の持ち主・起動の時の値・部分の失敗が決まっていない（T2、Q303-11、Phase の依存）

- 根拠:
  - WS301 の設計: policy は root が `/dev/system` で変え、起動の引数 `cpuperf=` で既定を変え、既定は BALANCED（`plan/ws301/design.md` §4.4、94〜97 行）。Q301-6 の推し A は「WS303 の profile と連動しない」（同 §8）。WS301 の ws.md のユーザーの決定「設定できるのは root だけ」。
  - T2: kernel は strategy を全部の受け手に渡す。起動の時の strategy は firmware の今の値（5330 は `\ODV0`、Debian の観測で performance、design §2.3）。
  - R2: Dell の set は 1 秒に 1 回まで（EBUSY）、失敗の後は今の mode を「不明」。T2「受けられない物は無視する」。
  - 第 3 版の Phase の表: p003 が registry と strategy を作るのに、WS301 への依存は p007 だけ。
- 起こる状況:
  1. 5330 の起動で strategy = performance を全部の受け手に渡すなら、WS301 の policy は起動ごとに PERFORMANCE（EPP 0、min = guaranteed）になり、WS301 の既定 BALANCED と `cpuperf=` を上書きする（電池の消費）。渡さないなら `THERMAL_GET_STRATEGY` は performance、WS301 は balanced で食い違う。
  2. WS301 の道具（`cpuperf`）と `/sbin/thermal` の 2 つが同じ policy を書き、後の方が勝つ。`THERMAL_GET_STRATEGY` は古い名前を返す。
  3. SET_STRATEGY の途中で WS301 は受け、Dell は EBUSY・EIO の時、strategy の値・返す errno・事象が決まっていない（状態遷移の抜け）。
  4. sessiond の道は wheel の user に WS301 の policy を変えさせる。WS301 の「root だけ」と字面で食い違う（sessiond は root で書くので形式は守るが、決定の意味は記録で揃える）。
  5. p003 で WS301 の受け手を作るなら WS301 p002（`kern_cpuperf_policy_set`）が要るのに、依存に無い。
- 直し方の案: (a) strategy の受け手を「kernel の中の登録の口」（`kern_thermal_strategy_receiver_register`）にし、WS301 の側が自分の受け手を登録する（向きは WS301 → WS303 の登録の口、呼びは WS303 → WS301。WS303 は WS301 の symbol を呼ばない）。WS301 の受け手は p007 か WS301 の Phase で足し、p003 は WS301 に依存しない。(b) 起動の時は strategy を受け手に**渡さない**、firmware の値は「Dell の受け手の今の値」としてだけ示す。`THERMAL_GET_STRATEGY` は受け手ごとの今の値と「揃っていない」の flag を返す。(c) SET の成否: 全部の受け手を試し、1 つでも失敗なら strategy は「揃っていない」、errno は最初の失敗、事象は成功した受け手の分。(d) WS301 の Q301-6 と ws.md の「root だけ」を、WS303 の回答（strategy から policy を選ぶ）に合わせて直す事を Q1 に頼む（WS301 の記録は WS303 の担当が書けない）。

### M4. `kern_cpuperf_limit` の契約が T5 の「highest の 10%」を表せない（T5、p007、WS301 §4.6・§5.1）

- 根拠: WS301 の口は `kern_cpuperf_limit(uint32_t source, uint32_t domain, uint8_t maximum)`（domain は `KERN_CPUPERF_ALL` も可、source ごとに 1 つ、最小を採る。`plan/ws301/design.md` 109〜110・166 行）。WS301 §4.5: hybrid では P-core と E-core の HWP の Highest は単位が違う（106 行）。表の provider（JH7110 の OPP、Pi）は 0〜255 の連続でなく step の表（92 行）。WS301 p004（x86 の provider）は p003（HAL の差分、ユーザーの承認）に依る（WS301 §7）。
- 起こる状況: (1) T5 が domain = ALL に 1 つの uint8 を渡すと、P-core の highest の 90% が E-core では別の意味になる（E-core の highest より上なら効かない、下なら E-core を強く絞る）。(2) 1 段ずつ下げる下限が無く、0（lowest 未満）まで下がる。WS301 の範囲の検査（lowest ≤ max、§4.3）が拒むか切るかで WS303 の段の数え方が崩れる。(3) `source` の値（「thermal」）の定数がどちらの WS の header にも無い。(4) p007 は WS301 p004 に、p004 は WS301 p003（HAL の承認）に依るので、WS303 の完了（p008 は p007 に依る）がユーザーの HAL の承認に縛られるが、表に出ていない。
- 直し方の案: WS301 に「domain ごとの highest に対する千分率で上限を渡す」口（例 `kern_cpuperf_limit_permille(source, permille)`、下限は各 domain の lowest か efficient に切る）を頼む（WS301 の設計の review に送る）。source の定数は WS301 の header に置く。Phase の表の p007 の依存に「WS301 p003（HAL の承認）」を書き、p008 を p007 に依らせない（p007 が待つ間も規約の見直しと文書を進め、p007 の分は後で足す）か、待つ事を ws.md に明記する。

### M5. boost の DIAG は `_WDG` が Method で、R3（Name の `_WDG` だけ）と p002 の内容から外れる（T3、Q303-12、p002・p004）

- 根拠: `dsdt.dsl` 94054〜94071: `\_SB.DIAG` の `_WDG` は `Method (_WDG) { Return (XWDG) }`（中身は Name の buffer で、SMI は起こさない）。第 2 版 R3 は「`_WDG` が Method の 5 つ（… DIAG 94066 …）は評価しない（この WS の provider は使わない）」。第 3 版の p002 は「ACPI WMI の層（Name の `_WDG`）」、p004 は「SMM の boost の確かめ」。
- 起こる状況: p002 のとおりに作ると DIAG の GUID（F1DDEE52-063C-…）が WMI の表に載らず、p004 で boost を試せない。逆に Method の `_WDG` を全部評価すると R3 が避けた AMW2・AMW5 の SMI（`DM05` → `GENS(0x21)`）が起動ごとに起きる。
- 直し方の案: p002 に「Method の `_WDG` は、AML を読んで Name の buffer を返すだけの形（`Return (Name)` の 1 文）の時に限り評価する」か「DIAG の `_UID` "WMDG" だけを許す表」を足し、harness の SMI の模擬で attach の SMI が 0 のままである事を数える（R3 の試験をこの場合に広げる）。Q303-12 が B（boost 無し）なら不要なので、p002 の分岐として書く。

### M6. 2 つの registry（`hw.thermal` と `/dev/thermal`）の一貫性と非常の判定の標本（T1、T4、Q303-9）

- 根拠:
  - `hw.thermal` の registry は `sysctl.c` の `kern_thermal_register(read, context)`（186〜229）で、source の写しの関数を呼ぶ pull の形。`struct thermal_entry` は温度・passive・critical だけで hot の欄が無い（`include/uapi/sysctl.h:290〜299`）。acpi-thermal は `_TMP`・`_PSV`・`_CRT` だけを読み `_HOT` を読まない（`acpi-thermal.c:286〜301`）。Pi 5 は passive も hot も持たない（`rpi5-fan.c:1012〜1028`、critical だけ）。
  - Keiland の monitor は entry を `element_size` でなく `sizeof(*entry)` で進める（`monitor-zedbsd.c:672`）。`sysctl` の道具は `element_size` を使う（`userland/base/sysctl/main.c:745`）。
  - acpi-thermal の周期は 5 秒（`acpi-thermal.c:38`）、Pi 5 は既定 1 秒（`rpi5-fan.c:68`）。T4 は「5 秒の周期で 2 回」。
  - 名前: `include/uapi/sysctl.h` に接頭辞なしの `THERMAL_VERSION`・`THERMAL_KIND_*`・`THERMAL_HAVE_*`・`THERMAL_FLAG_CPU`・`struct thermal_header`・`struct thermal_entry` が既にある。T1 は新しい `include/uapi/thermal.h` に `THERMAL_GET_SENSOR` などを置く。kernel にも `kern_thermal_register` が既にある（`include/kern/sysctl.h`）。
- 起こる状況: (1) hot・emergency を `/dev/thermal` で出すには provider の口を変える要がある。`thermal_entry` に欄を足すと Keiland の monitor が壊れる（sizeof で進むため）。(2) registry の thread と source の thread の周期が独立なので、「2 回続けて」が同じ読み（同じ `time_ns`）を 2 回数え、実質 1 回の読みで停止しうる（逆に読みを飛ばす）。(3) 2 つの header を同じ file で include すると `THERMAL_*` の名前がぶつかる恐れ。
- 直し方の案: provider の口は kernel の中の新しい struct（hot・emergency・`time_ns` を持つ）にし、`hw.thermal` はそこから今の 64 byte の entry を作って返す（ABI を変えない）。非常の判定は「`time_ns` が前と違う読み」だけを数える。新しい UAPI の名前は `KERN_THERMAL_*`・`struct thermal_dev_*` のように今の物とぶつからない接頭辞にする（他の header の `KERN_SYSTEM_*`・`KERN_FPGA_*` に揃う）。どちらが本体の registry かを T1 に書く。

### M7. 5330 の boost の「下回らせない」を判定する試験が無い（T3、Q303-12、p004 の受け入れ）

- 根拠: T3「EC がそれより上げる自由を失わない事を p004 で確かめた時だけ」。p004 の受け入れは「第 2 版 R10 に boost と非常の水準の値の**記録**を足す」で、合否の条件が無い。5330 の自動の段は EC の中で OS は知らない（T3）ので、`THERMAL_GET_FAN` の「自動の段」は 5330 では出せない。Linux の `dell_smm` は `pwm1_enable` = 1（手動の印と見られる）を出している（design §2.3、推測）。design §2.4・§4.13: SMM の fan の口は自動の制御の切り替えに重い副作用、BIOS が数秒ごとに戻す機種、1 回 2〜4 s の停止の報告。
- 起こる状況: SMM の state を書くと EC が自動の制御を止める機種なら、boost = 1（低い段）で負荷をかけると EC は段 2 に上げず、ユーザーの「下回らせない」が破れる。記録だけの受け入れではこれを見落とす。
- 直し方の案: p004 の受け入れに判定を書く: 「boost = 1 にし CPU の負荷（R10 の形）で TCPU が上がった時、rpm が boost なしの同じ負荷の rpm を下回らない（3 回、差の許容を決める）。下回れば boost は無しで登録」。試験の間は TCPU を見て一定（例 95 °C）で boost を最大か 0 に戻す安全の止めを script に入れる。UAPI の「自動の段」には「不明」の値を定める。

### M8. 非常の停止を端から端まで試す計画が無い（p003 の受け入れ、§6）

- 根拠: p003 の受け入れは host 試験（水準・2 回の判定・停止の求め・boost の max）と QEMU（ssh で `/sbin/thermal`）。kernel → init の signal → init の停止 → `KERN_SYSTEM_POWEROFF`、60 秒の直の切り（B2 の道）を通す試験が無い。q35 には温度の sensor が無い（design §6.2）。p005 の受け入れ「QEMU で session の user から strategy を変える」は、QEMU に受け手が無く（PNP0C14 無し、HWP 無し）何を変えても成功するので誤りを見つけない。session の socket は compositor が持つ fd 3（`session.c:33〜35`）で、試験の client が無い。
- 起こる状況: B2 の誤り（signal が無い、prepare の待ち、`_PTS` の lock）は host 試験では見つからず、最初の実の発動（過熱）で初めて分かる。
- 直し方の案: QEMU 用に root だけが使える試験の sensor（起動の引数で有効、値を `/sbin/thermal` の隠しの命令か sysctl で書く。既定の kernel では無効）を p003 に足し、(1) critical を 2 回 → init が停止 → QEMU の process が終わる（QMP の `query-status` か process の終わりで判定、console の log では判定しない）、(2) init を止めた状態（試験の service で SIGUSR2 を無視させる、など）で 60 秒の後に切れる、(3) 停止の求めの後に温度を下げても init の停止が続く、を確かめる。p005 は session の script を試験の物（`--file /etc/keiland/session=…`）に替え、fd 3 に `THERMAL …` を書いて答えを ssh で読む。QEMU の受け手の無い場合は「受け手 0 → EOPNOTSUPP か『受け手なし』」を期待の値にする。

### M9. `/dev/thermal` の UAPI の定義が無く、本文 §5 が第 3 版と食い違ったまま（T1、§4・§5）

- 根拠: T1 は ioctl の名前と欄の言葉だけで、struct・大きさ・`_Static_assert`・番号・errno・事象の action と subject を定めていない（第 2 版 m7 の「`offsetof` の `_Static_assert`、SET の他の欄は 0」も `/dev/system` の物）。本文で第 3 版と食い違う所: §0 の 2・5（`/dev/system`、SMM を入れない）、§3 の UAPI の行、§4.3「profile の provider は 1 つだけ（2 つ目は EBUSY）」（T2 は複数の受け手）、§4.4・§5.2（`/dev/system` の ioctl 22〜24・事象の class 0x100）、§4.11（WS301 との連動は WS301 が決める）、§4.13（`/dev/thermal` を選ばない）、§5.1 の `KERN_FAN_MODE_MANUAL`「held at a level a user set」と `set_level(context, mode, level)`（user の指定の段に固定する = ユーザーの「下回らせない」に反する）、§6.2（profile は EOPNOTSUPP）。
- 起こる状況: p003 の実装者が唯一の具体の定義である §5 を使い、MANUAL の段（温度より下に固定）を作る。UAPI の layout を review できないまま実装に入る（UAPI はユーザーの決定の対象、`plan/master-design-policy.md`）。
- 直し方の案: 第 3 版に §5 の置き換え（`include/uapi/thermal.h` の全文の案: sensor・fan・strategy・boost・事象の struct と大きさ、ioctl の番号、errno、事象の種類の bit）を書き、`KERN_FAN_MODE_MANUAL` と `set_level` の mode を消し `set_boost(context, level)` にする。本文の食い違う節の頭に「第 3 版の T で置き換え」と 1 行ずつ書く。

## Minor

- **m1 sessiond は amd64 だけ**: `userland/desktop/sessiond/Makefile` の package の arch は `amd64`。Pi 5 には sessiond が無く、Pi 5 の strategy は `/sbin/thermal`（root）だけ。T6・ws.md に書く。
- **m2 Q303-10 の推し**: 原文「rootだけが変えられるが、sessiond経由でも変えられる」は session の user に開く読み（B）に近い。今の sessiond も `POWER suspend` は誰でも（`power.c:83〜91`）、poweroff・reboot と `SERVICE` は root か wheel（`service-rules.c:86〜88`）。Dell の set は BIOS の設定（再起動の後も残る見込み）を書くので wheel に絞る理由はあるが、推しの理由として書く。
- **m3 cdev の登録**: `cdev_register` の device の番号（`system` は 0x00010002、`graphics` と `ptmx` は 0x00010001 を共有、`system-device.c:162` ほか）と、登録の呼び場所（今は `src/kern/vfs.c:466` が `drv_system_device_register` を呼ぶ）を T1 に書く。devfs の既定の mode は 0666（`devfs.c:600〜601`）なので、SET の root の判定は ioctl の中で `cred_is_superuser`（`system-device.c:333〜337` と同じ形）。
- **m4 事象の溢れ**: subscriber ごとの ring が溢れると古い物から捨てる（`system.h:315〜318`、`system-event.c:377〜379`）。passive の上下を hysteresis なしで事象にすると、5 秒ごとの跨ぎで ring が埋まり「非常の停止の開始」を捨てうる。跨ぎに hysteresis（passive − 3 °C と同じ）を付ける。`system-event.c` の ring（spinlock、割り当て無し）を class の bit で使い回し、複写を作らない案も比べる。
- **m5 T7 の cool**: 「全 trip −5 °C」に critical（110 °C、`rpi5-fan.c:70`）が入るかが曖昧。入ると cool で非常の停止が 105 °C になる。quiet と同じく「critical は変えない」と書く。
- **m6 起動の時の「読めなければ balanced」**: R1 で `ODV0` を BIOS が埋めない時は「今の mode は不明」。strategy を balanced と答えると firmware の実（Debian では performance）と食い違う。「不明」の値を UAPI に持つ。
- **m7 S0 idle**: sleep は user の process を凍らせる（`src/kern/sleep.c:12〜13、173`）。停止の求めが sleep の直前・間に出ると init は応えず、tick は suspend の後に進められる（同 comment）ので 60 秒の timer が resume の直後に切れる。非常の停止の開始の後は `KERN_SYSTEM_SLEEP` を EBUSY にする。`_CR3`（ACPI 6.5 §11.4.5、S0 idle の間の水準、5330 の AMBF に 70 °C）は使わない事を書く。
- **m8 Q303-13 の C の例**: 「CPU 100 °C」は i5-1245U の TjMax（100 °C、推測）と同じで、普通の重い負荷で CPU が throttle の温度に張り付くと停止する恐れ。例の値を TjMax より上（または「CPU の hardware に任せる」）にする。
- **m9 60 秒の判定を温度だけで決めない**: T4「60 秒たっても温度が critical 以上なら」は、init が止まっていて温度が critical の少し下（例 −1 °C）を行き来すると、永く電源が入ったままになる。停止を求めた後は「init が終わらない」だけで切る（温度の条件は外すか hysteresis を大きくする）。
- **m10 ws.md の到達目標**: 「単一の到達目標」に passive（Q303-6）と Pi 5 の profile（Q303-4）が無い。p007・p006 の受け入れと揃える。
- **m11 BUG-351**: この tree に `plan/bugs/BUG-351.md` は無く、master.md の「次の番号」が BUG-351（未確認: main で作られた見込み）。design・ws.md の参照は main に merge した後に確かめる。
- **m12 `THERMAL_GET_SENSOR` の欄**: T4 で critical = 非常なのに、T1 は critical と emergency の両方を返す。B1 の直し（device の `_CRT` は停止に使わない）を採るなら欄を分ける意味はあるので、どちらかに揃えて意味を書く。
- **m13 Dell の set の頻度**: strategy の SET は SMI（数十 µs〜数 s の停止、design §2.4・§4.9）と BIOS の設定の書き（NVRAM、推測）。UI からの切り替えで 1 秒に 1 回（R2）まで許すと NVRAM の書きが重なる恐れ（推測）。同じ値の SET は firmware に渡さない、と書く。
- **m14 Q303-11 の B**: 「Dell の mode だけを変え、WS301 の policy は別に選ぶ」は、原文の「DVFSストラテジ」（DVFS を選ぶ物）と合わない。選択肢に残すなら「原文の strategy から DVFS を外す」と明記する。
- **m15 LW**: p003 の 7 LW は registry・cdev と事象・UAPI・水準と非常の停止・`hw.thermal` の作り直し・`/sbin/thermal`・12 の `vmunix.mk`・host 試験・QEMU に加え、B2 の init の変更と M8 の試験の sensor が入る。9〜10 LW の見込み（推測）。p002 は harness の移し（Q1 の決定）と M5 の分岐を足す。

## 第 2 版の R と第 3 版の strategy の書き込みの関係（依頼の (8)）

| R | 第 3 版との関係 | 判定 |
| --- | --- | --- |
| R1 `\ODV0` を attach・set の後・resume の後だけ読む、周期の読みを止める | T2 の起動の時の strategy が `ODV0` を読む。SET_STRATEGY の後の確かめも `ODV0`。合う | 合う。起動の時に WS301 へ渡すかは M3 |
| R2 延長 0・sentinel・EIO・1 秒に 1 回 | SET_STRATEGY の部分の失敗と errno の扱いが決まっていない（M3） | 要補い |
| R3 Name の `_WDG` だけ | DIAG（Method の `_WDG`）の boost と食い違う（M5） | 食い違い |
| R5 WMI の事象を外す | 第 3 版も firmware 側からの strategy の変化を知らせる事象を使わない。合う | 合う |
| R8 起動で firmware に書かない | T2 が保つ。合う | 合う |
| R13 STALLED | `/dev/thermal` の事象へ移す。Pi 5 の boost で level > 0 の時も条件は同じ。合う | 合う |
| m9 Pi 5 の lock の範囲 | T7 で trip の組を他の thread が変えるので、`rpi5_fan.trips`（今は thread だけが触る、`rpi5-fan.c:195〜198`）も lock の下か、thread が拾う要求の形にする（p006） | 要補い |

## 確かめて正しかった主張

| 主張 | 根拠 |
| --- | --- |
| 今の sessiond の POWER（poweroff・reboot）は root か wheel | `userland/desktop/sessiond/power-rules.c:52〜70`、`power.c:96〜106` |
| sessiond は root で動き、session の要求は 1 行の語で `*-rules.c` が決める形 | `main.c:142〜146`、`session.c:405〜416`、`service-rules.c:86〜88`。T6 の `thermal-rules.c` はこの形に合う。rules の host 試験の前例は `plan/ws052/tests/host-sleep-rules.c` |
| `struct system_event` は 128 byte の固定の record | `include/uapi/system.h:356〜368` |
| ioctl の group 'T' は空き | `include/`・`src/`・`userland/`（packages を除く）の `_IO*('T'` と `*_IOC_GROUP 'T'` が 0。使用は 'A' 'B' 'E' 'F' 'G' 'H' 'L' 'R' 'S' 'W' 'b' 'c' 'f' 'g' 'h' 's' 't' |
| ioctl の size の欄は 13 bit（8191 byte まで） | `include/uapi/ioctl.h:26〜28` |
| `system-device.c` を build する `vmunix.mk` は 12 | `grep -l system-device.c platform/*/vmunix.mk` が 12 |
| `hw.thermal` の今の読み手は `sysctl` の道具と Keiland の monitor | `userland/base/sysctl/main.c:707〜745`、`monitor-zedbsd.c:653〜672` |
| acpi-thermal は 5 秒ごとに `_TMP`・`_PSV`・`_CRT` を読み、何もしない | `acpi-thermal.c:38、247〜311` |
| 起動の直後（EC の `_REG` の前）の 5330 の読みは低い偽の値で、誤って停止しない | TCPU の `_TMP` は `ECRD` = 0 で 0x0BB8（26.85 °C）、他は 0x0ADE（5.05 °C）（`out-ssdt3.dsl:1162〜1172` ほか）。attach は EC の後（design §1） |
| Pi 5 の fan の段は 0〜4 の 5 つ、温度が読めなければ最大 | `rpi5-fan.c:94、234〜236、1170〜1176`。T3 の「boost = level の下限（0〜4）」は表せる |
| Pi 5 の critical は 110 °C（device tree か既定） | `rpi5-fan.c:70、366〜371、662〜666` |
| `KERN_SYSTEM_POWEROFF` は pid 1 だけで、prepare の後に platform の poweroff | `system-device.c:552〜568` |
| init は poweroff を ZSV1 の socket で受け、最後に `/dev/system` の ioctl を呼ぶ | `shutdown-control/main.c:69`、`init/main.c:1871〜1900、2265〜2270` |
| kernel から process に signal を送る関数がある | `src/kern/signal.c:435`（`signal_send_process`） |
| guardrail は GPL の file の値を block ごとの出典で使う事を全 WS に許す（Q303-12 の A は guardrail に合う） | `plan/guardrail.md:123` |
| 第 3 版の Phase の LW の合計は 29（p001 を除く） | 6 + 7 + 7 + 2 + 3 + 2 + 2 |
| ws.md の Phase の表は design.md の第 3 版の表の写しとして合う | `ws.md:24〜33` と `design.md:56〜65` |
| ユーザーの回答の原文は ws.md と design.md で同じ | `ws.md:19〜20`、`design.md:14〜21` |
| 受け入れの判定は ssh・host 試験で、QEMU の console の log を使っていない（AGENTS.md §9） | 第 3 版の Phase の表、R10 |
