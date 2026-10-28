<!-- awesome-plan project=zedbsd record=ws303-design -->

# WS303 設計: 熱と fan（温度の読み・fan の回転数・thermal mode）

- 版: 第 3 版（2026-10-29 P5、q1666-i02。第 1 版 → design-reviewer（[design-review.md](design-review.md)）→「第 2 版の改訂」→ ユーザーの回答 →「第 3 版の改訂」。第 3 版の review は [design-v3-review.md](design-v3-review.md)）。p001（調査と設計）の成果。code は書いていない。
- 基点: worktree `p5-design-b`（main 63386d8a4）。根拠の file:行 はこの tree の物。
- 調べた物: zedBSD の source（下の §1）、5330 の ACPI の表（前の P5 が取った逆アセンブル、`scratchpad/ws303-acpi/`、git に入れない）、
  5330 の Debian の読みだけの観測（2026-10-29 02:42、Q1 の割当、§2.3）、Fable の調査（公開の資料だけ、GPL の source は読まない、§2.4）。

## 第 3 版の改訂（2026-10-29 P5。ユーザーの回答を受けた改訂で、この節が第 2 版の改訂と本文に優先する）

### ユーザーの回答（2026-10-29、Q1 の中継、原文。Q303-2・3・5 は自由記述）

- Q303-1「/dev/thermal」（推しでない C）
- Q303-2「rootだけが変えられるが、sessiond経由でも変えられる。/sbin/thermalからDVFSストラテジを選択。強制的にファンを大きくする（何段階かあり）ことはできるが、実際の温度に対して必要なファン回転数を下回らせるオーバーライドは不能。」
- Q303-3「さっき書いた/sbin/thermal」
- Q303-4「profile を作る」（Pi 5 にも profile）
- Q303-5「温度に対して必要な回転数を下回らせることはできず、回転を上げる。非常水準に達したとき、電源を落とす。コントロールできないときはファームウェアにまかせたり、ストラテジだけファームウェアに伝える。」
- Q303-6「入れる」（WS301 の passive cooling を入れる、WS301 p002 に依存）
- Q303-7「Debian で読む (推し)」
- Q303-8「よい (推し)」（終わりに performance へ戻す）
- Q1 の決定（2026-10-29）: AML の host の harness は、WS303 p002 の実装の時に `plan/ws049/tests/` から `plan/tools/aml-host/` へ移す（tools.md に登録）。`/dev/acpi` の件は BUG-351。

### 解釈（P5 の読み。確かめが要る所は下の Q303-9〜14 で聞き直す）

1. UAPI は新しい device **`/dev/thermal`**。温度・fan・strategy（profile）・fan の上げの段・事象を 1 つにまとめる。`/dev/system` の ioctl・事象の class（第 1 版 §4.4・§5.2）は作らない。
2. 変えられるのは root。session の user は **sessiond 経由**で変えられる（sessiond が root として `/dev/thermal` を書く。今の POWER の要求と同じ形、`userland/desktop/sessiond/power-rules.c` の `sessiond_power_decide`: root か wheel の user）。
3. **`/sbin/thermal`**（root の道具、`userland/base/thermal/` を /sbin に置く）が **DVFS の strategy** を選ぶ。strategy は WS301 の policy（performance・balanced・powersave）と firmware の thermal mode（Dell）と Pi 5 の trip の組を 1 つの名前で選ぶ物（対応の表は Q303-11）。
4. fan は「**強制的に上げる**」段（boost、何段か）だけを指定できる。温度から決まる回転数（自動の制御の値）を下回らせる指定はできない: 実際の段 = max(自動の段, boost の段)。
   OS が段を持たない機械（5330 は EC が回す）は、boost を OS の手段（5330 は SMM の fan の state、Q303-12）で上げられる時だけ boost を出し、出せない時は boost を「無し」と答える。
5. 危ない温度: critical に近づいたら回転を上げる（自動の制御が最大へ）、**非常の水準に達したら電源を落とす**（秩序だった停止、Q303-13）。
6. OS が fan を制御できない機械は firmware に任せる（5330 の EC）か、strategy だけ firmware に伝える（Dell の thermal mode）。
7. passive cooling を入れる（Q303-6）: CPU の温度が passive の水準を超えたら WS301 の熱の上限の口（`kern_cpuperf_limit`、WS301 p002）で CPU の上限を下げる。**WS303 は WS301 に依存する**（p002 の枠組みと p004 の x86 の provider）。
8. 第 2 版の R1〜R13 の技術の判断（`\ODV0` の読み、sentinel、Name の `_WDG` だけ、DDV の `\ECRD` の確かめ、事象の WMI の口を外す）は保つ。STALLED などの fan の事象は `/dev/thermal` の事象へ移す。

### 設計の改訂

- **T1 `/dev/thermal`**（新しい cdev、`src/kern/thermal.c` が持つ。全 platform、`system-device.c` と同じく `vmunix.mk` に名指し）:
  - `read`/`poll`: 事象（strategy の変化、boost の変化、温度の水準の跨ぎ（passive・hot・critical）、非常の停止の開始、fan の止まり）。形は `struct system_event` に倣った 128 byte の固定の record（`/dev/thermal` の UAPI に別の struct として定義）。
  - ioctl（`include/uapi/thermal.h`、新規、group 'T' は他と衝突を確かめて p003 で決める）: `THERMAL_GET_SENSOR`（index、温度・passive・hot・critical・emergency の水準、源）、`THERMAL_GET_FAN`（第 1 版の `system_fan_info` に boost の段・段の数・自動の段を足す）、
    `THERMAL_GET_STRATEGY`／`THERMAL_SET_STRATEGY`（root）、`THERMAL_SET_BOOST`（root、fan ごとか全部）、`THERMAL_SUBSCRIBE`（事象の種類の mask）。
  - `hw.thermal`（sysctl）は残す（今の読み手: sysctl の道具、Keiland の monitor）。中身は同じ registry から出す。新しい読み手は `/dev/thermal` を使う（Q303-9）。
- **T2 strategy**: 名前は cool・quiet・balanced・performance（Dell の 4 つと Linux の platform_profile の名前にも合う）。kernel は strategy を選ぶと、登録された全部の「strategy の受け手」に渡す: WS301 の policy、Dell の thermal mode（`WMBA` の set）、Pi 5 の trip の組。受け手は自分の解釈で受け、受けられない物は無視する。対応の表（推し）は Q303-11。
  起動の時の strategy: firmware の今の値（5330 は `\ODV0` から、第 2 版 R1）を読み、読めなければ balanced。起動で firmware に書かない（第 2 版 R8 を保つ）。
- **T3 fan の自動と boost**: fan の provider は「自動の段」（Pi 5 は trip から、5330 は EC の物で OS は知らない）と「boost の段の数」を出す。kernel は max(自動, boost) を provider に求める。
  5330: 自動は EC。boost を出すのは SMM の fan の state（DIAG の WMI）で上げられて、かつ EC がそれより上げる自由を失わない事を p004 で確かめた時だけ（失うなら boost は無し、Q303-12）。Pi 5: boost の段 = level の下限（0〜4）。
- **T4 critical と非常の停止**（Q303-13）: 水準は sensor ごと: passive（ACPI `_PSV`、DT の passive の trip）、hot（ACPI `_HOT`、DT の hot。無ければ critical − 5 °C）、critical = 非常（ACPI `_CRT`、DT の critical）。
  hot を超えたら boost を最大にし（自動の制御の機械は自動に任せたうえで）事象。critical に 2 回続けて達したら（5 秒の周期で 2 回）事象を出し、init に秩序だった停止を求める（`/sbin/poweroff` と同じ道、kernel から init への signal の形は p003 で既存の口を確かめる）。停止を求めて 60 秒たっても温度が critical 以上なら kernel が直に電源を切る（ACPI S5、`KERN_SYSTEM_POWEROFF` の中の処理）。
  5330 の participant の `_CRT` は DPTF の値で、DTT の無い OS の停止の閾値として妥当かは p004 で値を読んで確かめる（未確認）。
- **T5 passive**（Q303-6）: CPU の sensor（`THERMAL_FLAG_CPU`、5330 の TCPU、Pi 5 の cpu-thermal）が passive の水準を超えたら、5 秒ごとに WS301 の上限を 1 段（HWP の単位で highest の 10%）下げ、水準 − 3 °C を下回ったら 1 段戻す。上限の source は「thermal」の 1 つ。WS301 の provider の無い機械は何もしない。
- **T6 sessiond**: sessiond に THERMAL の要求を足す（strategy の名前と boost の段だけ、`thermal-rules.c` で語を検め、POWER と同じ root か wheel の規則（Q303-10））。sessiond が `/sbin/thermal` を root で走らせるか、`/dev/thermal` を直に書くかは p005 で決める（推し: `/dev/thermal` を直に。program の起動を避ける）。
- **T7 Pi 5 の profile**（Q303-4）: strategy の受け手として trip の組を持つ: balanced = device tree の trip、cool = 全 trip −5 °C、quiet = trip +5 °C（critical は変えない、hot までに限る）、performance = balanced の trip（fan は同じ、CPU は WS301 の performance）。boost は level の下限。値は推しで、Q303-11 で確かめる。

### 第 3 版の Phase の表（第 2 版の表を置き換える。LW は ±50%）

| Phase | 内容 | 依存 | LW | 受け入れ |
| --- | --- | --- | --- | --- |
| p001 | 調査と設計、design-reviewer、ユーザーの判断 | — | 残り 1 | Q303-9〜14 の答え |
| p002 | ACPI WMI の層（Name の `_WDG`）、harness の `plan/tools/aml-host/` への移しと拡張（tools.md に登録） | p001 | 6 | WMI の host 試験、attach が SMI を起こさない |
| p003 | registry・`/dev/thermal`・`include/uapi/thermal.h`・水準と非常の停止・`hw.thermal` を同じ registry から・`/sbin/thermal` | p001 | 7 | host 試験（水準・2 回の判定・停止の求め・boost の max）、全 platform の build warning 0、QEMU（ssh で `/sbin/thermal`） |
| p004 | Dell の provider（DDV、`ODV0`、strategy の受け手、SMM の boost の確かめ）と 5330 の試験 | p002、p003 | 7 | 5330 の試験（第 2 版 R10 に boost と非常の水準の値の記録を足す） |
| p005 | sessiond の THERMAL の要求（rules の host 試験） | p003 | 2 | host 試験、QEMU で session の user から strategy を変える |
| p006 | Pi 5 の provider（profile と boost、lock の範囲の変更） | p003 | 3 | Pi 5 で ssh、負荷で level |
| p007 | passive（WS301 の上限の口を使う） | p003、WS301 p002・p004 | 2 | host 試験、5330 で TCPU が passive を超えた時に上限が下がる記録 |
| p008 | 規約の全文、文書（WS301 と順を Q1） | p004〜p007 | 2 | 規約、文書 |

合計 約 29 LW（p001 を除く）。p007 は WS301 の p002・p004 の後。

### ユーザーに聞き直す点（推しつき）

- **Q303-9 `hw.thermal` の sysctl**: (A, 推し) 残す（monitor と sysctl の道具はそのまま、中身は同じ registry）。(B) Keiland の monitor も `/dev/thermal` に移し、`hw.thermal` は後で外す。
- **Q303-10 sessiond 経由で変えられる人**: (A, 推し) 今の POWER と同じ「root か wheel の user」。(B) 今 seat に居る session の user なら誰でも。
- **Q303-11 strategy の対応の表**: (A, 推し) performance = WS301 performance・Dell UltraPerformance・Pi 5 の DT の trip、balanced = balanced・Optimized・DT の trip、quiet = powersave・Quiet・trip +5 °C、cool = powersave・Cool・trip −5 °C。(B) Dell の mode だけを変え、WS301 の policy は別に選ぶ（strategy と policy を分ける）。
- **Q303-12 5330 の fan の boost**: (A, 推し) SMM の fan の state を使うかを p004 で確かめる。命令の値は公開の資料に無いので、guardrail の「GPL の値は引用で可」で Linux の hwmon の driver の値を block ごとに出典つきで使う（GPL の source を読む事になる）。EC が上げる自由を失うなら boost は無し。(B) GPL の値を使わず、5330 の boost は無し（strategy だけ）。
- **Q303-13 非常の停止の水準と手順**: (A, 推し) ACPI の `_CRT`（DT の critical）に 2 回続けて達したら秩序だった停止、60 秒で下がらなければ直に電源を切る。(B) 秩序だった停止だけ（直に切らない）。(C) 固定の値（例 CPU 100 °C）。
- **Q303-14 passive の手順**: (A, 推し) T5 の段の上げ下げ（5 秒ごと 10%、−3 °C の戻り）。(B) passive は入れるが、上限を下げるのは hot を超えた時だけ。

## 第 2 版の改訂（2026-10-29 P5、[design-review.md](design-review.md) の blocker 1・major 13・minor 21 を反映。この節が下の本文の該当の所に優先する）

- **R1（B1）今の mode は SMI で get しない**: 今の mode は GNVS の `\ODV0`（`dsdt.dsl` 1236 の GNVS の SystemMemory、2466 の field）を AML の評価で読む。SMI を起こさない。
  `WMBA` の Case が input1 の bit（1・2・4・8）と `ODV0`（0・1・2・3）の 1 対 1 を AML の事実として与えるので、今の mode は bit の空間で分かる。読むのは attach・set の後・resume の後だけ（**60 秒ごとの周期の読みは止める**、§4.6 の「周期の get」の遷移と §4.9 の「60 秒ごと」を消す）。
  BIOS が起動の時に `ODV0` を埋めるか（§10 の 7）は p004 の最初の確かめ。埋めないなら provider は「今の mode は不明（set の後だけ分かる）」で登録する（ABSENT にしない）。input0 = 0 の get の SMI は使わない（意味の出典が無い）。
- **R2（M3・M4）set の成否と buffer**: 延長の長さは 0 だけ（0x8000 の予備の道は外す。store のたびの写しで二乗の時間になる）。延長 0 を firmware が受けない時は profile の provider を登録しない。
  buffer は「延長の長さ + 0x2C」に固定する 1 つの関数で作り、0x14〜0x23（input の後の 16 byte）を sentinel 0xFFFFFFFF で埋めて呼ぶ。返りで 16 byte が全部 sentinel のままなら「SMI が起きなかった」（`SMBF` は buffer が短いと SMI を飛ばす、91087〜）として EIO。
  status の意味は分からないので、成否は「SMI が起きた」と `ODV0` の読みまで。効いたかは p004 の実機（§6.3 の改訂）で見る。set の失敗の後は今の mode を「不明」にする。set は 1 秒に 1 回まで（それより速い求めは EBUSY）。
- **R3（M1・m2）WMI の attach は SMI を起こさない**: attach の時に解析するのは `_WDG` が Name の device だけ（5330 で要る AMW0・AMWV は Name、91150・94628）。`_WDG` が Method の 5 つ（AMW2 91818、AMW5 93082、AMW4 93763、DIAG 94066、ECMO 100962。AMW2・AMW5 は `DM05` → `GENS(0x21)` で SMI）は評価しない（この WS の provider は使わない。後で要る WS が遅延の評価を足す）。
  §6.1 に「attach と周期の処理が SMI の port を書かない」を harness の SMI の模擬で数える試験を足す。期待の entry の数は Name の device の分（5330 では Debian の 30 と同じにならない。数は p002 で harness の出力を基準にする）。
- **R4（M2・m3・m4・m5）DDV の読み**: method 0x20 は `ECM1` を取らず、`Sleep(3)` の間は interpreter の lock を離す。EC の取引は 2 + 4n と Sleep 1 回。`\ECRD`（97594）が 0（EC の `_REG` の前）だと EC の 1 byte ごとに `EISC` → `GENS(0x08)` の SMI になる（96389・97597〜）ので、provider は読みの前に `\ECRD` = 1 を確かめ、0 なら読まない（STALE）。
  入力の buffer は 3 byte 以上（byte 0〜2 の `CreateByteField`、zedBSD は buffer の外の field を拒む `aml-define.c:403〜406`）、0 を 4 byte。解析は package の長さ（要素 0）→ 3 byte の組 → 0xFF の順（EC の数が 0・0xFF の時の返り `{0, Buffer(1){0}}` を「種類 0 の fan」と読まない）。§4.10 の Acquire の行は「EC の取引の失敗で WMDV が誤りを返す → STALE」に替え、§10 の 5 は harness の EC に失敗を入れて STALE を見る試験にする。
  §1 の「直列」に補い: AML の Sleep・Acquire・Wait の間は interpreter の lock を離す。
- **R5（M5）WMI の事象の口はこの WS から外す**（`drv_acpi_wmi_event_install`、§4.7・§4.9・§5.1 の該当）。使い手が無く、Notify の handler の中で `_WED` を評価する形は handler の契約（`aml-sync.c:62〜67`）に反する。要る WS が評価の thread つきで作る。
- **R6（M6）`/dev/acpi`**: 今は 0666 で、引数の足りない method も評価する（`devfs.c:589〜601`、`acpi-text.c:150`、`aml-eval.c:913〜920`、review の推測、未試験）。一般の user が EC の書き・SMI を起こせる恐れ。WS303 の範囲の外なので Bug の候補として Q1 に送る。§4.13 の「生の WMI を user に出さない」の主張はその Bug の直しに依る。
- **R7（M7・m15）build**: registry（`src/kern/thermal.c`）は、`system-device.c` を build する platform の `vmunix.mk` の kernel の一覧に名指しで足す（weak の symbol は使わない。数は review の数えで 12、p003 で確かめる）。`dell-wmi.c` は amd64 の `CONFIG_DRIVER_ACPI` の block（`vmunix.mk:356〜365`）だけ。i386 の pcat は `src/drivers/acpi` を build しないので足さない。
- **R8（M8）** 起動の時に profile を書かない。§3 の「既定の profile の適用」は消した（本文を直した）。
- **R9（M9・m19）host の harness**: 今の `plan/ws049/tests/` の harness は、引数つきの評価・読むたびに変わる EC の register・output を書く SMI の模擬を持たない。p002 の作業に harness の拡張（`--eval-args`、EC の register ごとの読みの列、SMI の時に SystemMemory を書く模擬）を入れ、LW を足す。
  harness は completed の WS049 の物なので、`plan/tools/aml-host/` に移すか WS303 の tests に写すかを Q1 に頼む（p002 の依存）。DDV・SMBIOS の解析の試験は p004 の前半に移す。
- **R10（M10）5330 の受け入れの試験**（§6.3 を置き換え）: 判定は ssh（`plan/tools/guest/guest.sh` の形、5330 の素の起動の `5330-run.sh` の survey）で `thermal` の出力を回収して読む（console の log では判定しない）。
  rpm は「`HAS_RPM` で値が読めている（idle の 0 も可）、負荷の後に 0 でない」。負荷は guest の `sh -c 'while :; do :; done' &` を CPU の数だけ。profile の比べは quiet → performance と performance → quiet の 2 回、間に温度が下がるのを待つ時間（5 分か TCPU が開始の値に戻るまで）。
  set の時間は測って記録する（合否にしない）。**5330 の BIOS の Thermal Management を試験で書き換える事（終わりに元の値に戻す、戻せなければユーザーが BIOS の setup で戻す）はユーザーの承認が要る（Q303-8）**。WS301 の 5330 の測りと同じ日に重ねない（Q1 の割当）。
  mode の名前の対応（R1）: zedBSD で set → Debian に戻して `platform_profile` を読む（読みだけ）で bit と名前の対を 4 つ取る（BIOS の NVRAM に残る時。残らなければユーザーに BIOS の setup の表示を見てもらう、Q303-7）。Pi 5 は ssh で `thermal` を読む。
- **R11（M11）出典**: Fable の頁で保存があるのは ACPI 6.5 の 11 章の text だけ（`scratchpad/ws303-fable/acpi65_thermal.txt`）。§2.4 の他の行は「要約（頁の保存なし）」に下げて読む。output の位置 0x14 は AML の位置からの推定で、R2 の sentinel の範囲としてだけ使う（値として引用しない）。0x24 の意味は使わない。
  guardrail の「GPL の値は引用で可」（`plan/guardrail.md` の「MIT・GPL の出典のデータの引用」）は全 WS に当たるので、Q303-3 の B に「ユーザーの判断」は要らない（使うかどうかは設計の判断で、Q303-3 は fan の手動の制御を入れるかの問い）。
- **R12（M12）** ユーザーの決定でない物を決定と書いていた所を直した（§0 の 4、§4.4 の見出し）。§0 の 3 は「class・select・set の形は AML で確か、今の mode は `ODV0`、名前の対応は p004」と読む。
  Q303-1 に選択肢を足す: 「温度を `hw.thermal` のままにし、ws.md の到達目標の『/dev/system の口で提供する』を温度については sysctl と読み替える（ws.md の文言を直す）」。
- **R13（M13）** STALLED は `HAS_RPM` と `HAS_LEVEL` の両方があり level > 0 で rpm 0 が 3 周期の時だけ。Dell の fan（level 無し）には出さない。Pi 5 で tachometer が無い（`has_rpm` = 0）時は出さない。host 試験に 2 つの誤報の場合を足す。
- **minor**: m1 DIAG の `_STA` の誤りは本文を直した。m6 `kern_fan_register(name, rpm_max, ops, context)` に揃える。m7 UAPI に `KERN_SYSTEM_FAN_MODE_*` を定義し、`offsetof` の `_Static_assert`、`time_ns` は CLOCK_MONOTONIC、SET では `profile` 以外の欄は 0（でなければ EINVAL）。
  m8 attach の一時の失敗は 3 回まで 1 秒おきに試し直す、FAILED の時の GET は写しと STALE の flag。m9 Pi 5 の `level` は今 lock の外で thread が書くので、lock の範囲を広げる変更になる（p005 の作業）。
  m10 `userland/tests/systemevents/main.c` の class の名前の表に "thermal" を足す。新しい program を古い kernel で "all" の購読をすると EINVAL（kernel と userland は一緒に配るので受け入れる、記録だけ）。m11 DDV の 6 つ目の sensor（種類 0xFF）は ACPI に対が無く、足さない判断で 1 sensor を失う（受け入れる）。
  m12 `IETM._STA` も `DDDR` = 1 の時 `DPST(1)` を呼ぶ（`out-ssdt3.dsl:337〜350`）。zedBSD は起動で `_STA` を評価するので、Debian の後の warm reboot で `DDDR` が残ると EC が「DTT 有り」と見る恐れ（未確認、p004 で `\DDDR` を読む）。
  m13 S0 idle: registry の thread は suspend の間に DDV の EC の読みをしない（sleep の hook で止める）、resume の後に `ODV0` を読み直す。m14 GUID は `_WDG` の生の byte の順で持ち、文字列の表記から変える関数を 1 つ持つ（前 3 欄が逆順）。
  m16 BIOS の admin password で set が拒まれる時は区別できない（status の意味が不明）ので EIO と log。m17 WSMT は Debian の dmesg の ACPI の表の一覧（`survey.txt` の dmesg）で見たが中身は読んでいない（推定と読む）。m18 行の番号（WMDV 94635、SMBF 91087）。
  m20 `docs/architecture/power-management.md:24` は WS301 も変えるので、両 WS の文書の Phase の順は Q1。m21 WS301 の passive の口（WS301 p002）は WS303 が使わない時、使い手の無い口になる（WS301 の設計では「口だけ、host 試験で確かめる」とした）。

### 第 2 版の Phase の表（§7 を置き換える。LW は ±50%）

| Phase | 内容 | 依存 | LW | 受け入れ |
| --- | --- | --- | --- | --- |
| p001 | 調査と設計（この文書）、design-reviewer、ユーザーの判断 | — | （済み） | Q303-1〜8 の答え |
| p002 | ACPI WMI の層（Name の `_WDG` の解析・method・query、事象は無し）、harness の拡張（引数つきの評価、EC の列、SMI の模擬） | p001、harness の置き場所（Q1） | 6 | WMI の host 試験、attach が SMI を起こさない、amd64 の build warning 0 |
| p003 | registry・UAPI・`system-device.c`・12 の `vmunix.mk`・`thermal` の道具・host 試験 | p001（Q303-1） | 5 | registry の host 試験、全 platform の build warning 0、QEMU（ssh で `thermal` を読む） |
| p004 | Dell の provider（DDV の fan、`ODV0` の読み、sentinel の set）、解析の host 試験、5330 の試験（R10） | p002、p003、Q303-7・8 | 6 | R10 の 5330 の試験、mode の名前の対の記録 |
| p005 | Pi 5 の fan を registry に出す（lock の範囲の変更） | p003 | 1 | Pi 5 で ssh の `thermal` |
| p006 | 規約の全文、文書（WS301 と順を Q1 が決める） | p004、p005 | 2 | 規約、文書 |

合計 20 LW（p001 を除く）。p004 は実機の割当とユーザーの BIOS の承認を待つ時間を別に持つ。

### 第 2 版のユーザーに聞く点（§8 に足す・置き換える）

- Q303-1 に R12 の選択肢を足す。Q303-7 は R10 の方法（zedBSD で set → Debian で読み）を推しにする。
- **Q303-8（新）5330 の BIOS の Thermal Management を試験で書き換えてよいか**: (A, 推し) よい（試験の終わりに元の値（今は performance）に戻す。戻せなければユーザーが BIOS の setup で戻す）。(B) よくない（profile の set は試さず、読みと fan の rpm まで。set は QEMU でも試せないので未検証のまま入れるか、p004 から外す）。

## 0. 要約（この設計が決めること）

1. 5330 では OS が fan の duty を書く口は firmware に無い（ACPI の fan の device が無い、§2.1）。OS から出来るのは、**fan の回転数の読み**と **Dell の thermal mode の選択**。
   どちらも Dell の **ACPI WMI**（PNP0C14）の method を AML で評価すれば届く。kernel が SMI の port を直に叩く code は要らない。
2. 新しく作る物は 4 層: (a) ACPI WMI の汎用の層（`_WDG` の解析・method・query・event）、(b) kernel の「fan」と「thermal profile」の registry、
   (c) `/dev/system` の ioctl と事象の class、(d) provider（Dell の 5330、Pi 5 の既存の fan）。温度は今の `hw.thermal`（sysctl）のまま。
3. Dell の provider: 回転数は Dell の data vault（DDV、GUID 8A42EA14…）の method 0x20、thermal mode は Dell の SMBIOS の呼び出し（GUID A80593CE…）の
   class 0x11・select 0x13。値は 5330 の AML と Debian の観測で確かめた（§2）。
4. HAL の API の変更は要らない（AML の SystemIO・SystemMemory・EC の handler は既にある）。UAPI を足す案（決めるのはユーザー、§8 の Q303-1）。
5. 5330 の fan の手動の制御（SMM 経由の fan の state）は、この WS の推しでは入れない（§4.13、Q303-3）。

## 1. 今の振る舞い（code の事実）

- 温度: `src/drivers/acpi/acpi-thermal.c`。thermal zone と `_TMP` を持つ device を集め（`thermal_visitor` 134〜190）、5 秒ごとに `_TMP`・`_PSV`・`_CRT` を読み
  （`THERMAL_PERIOD_MS` 38、`thermal_thread` 235〜270）、`kern_thermal_register` で `hw.thermal` に出す（112）。`_PSV`・`_CRT` は読むだけで何もしない
  （冷却も停止もしない）。sensor の上限 16（37）。
- `hw.thermal` の UAPI: `include/uapi/sysctl.h:240〜313`（`struct thermal_header` 24 byte、`struct thermal_entry` 64 byte、`THERMAL_KIND_ZONE`・`DEVICE`、`THERMAL_FLAG_CPU`）。
  登録は `src/kern/sysctl.c:146〜245`（source は 4 つまで、1 source 16 sensor まで、外さない）。読む物: `userland/base/sysctl/main.c`、Keiland の monitor（`userland/desktop/libkeiland-backend-zedbsd/monitor-zedbsd.c`）。
- Pi 5 の fan: `src/drivers/platform/rpi5/rpi5-fan.c`。device tree の trip と cooling map から level を選び RP1 の PWM に duty を書く（`poll_once` 1155〜1195）、
  回転数の register を読める（`read_rpm` 1139〜1152、log に出すだけ）。温度は `hw.thermal` に 1 つ。fan の状態（level・rpm）を外へ出す口は無い。起動は `src/kern/platform/rpi5.c:194`。
- ACPI: 自前の AML の interpreter（`src/drivers/acpi/aml-*.c`）。評価は引数つきで出来る（`drv_acpi_evaluate`、`include/drivers/acpi/acpi.h:280〜286`、buffer・package の object を作れる 294〜316）、
  Notify の handler（`drv_acpi_notify_install` 361〜365）、SystemMemory・SystemIO の handler（`acpi-kern.c:187〜192`、`io_handler` 1131〜 は port の制限を持たない）、EC（`acpi-ec.c`）。
  **全ての AML は 1 つの interpreter の lock で直列**（`aml-sync.c:13`）。attach の順は `acpi-kern.c:236〜256`（EC → power → thermal → LPS0）。
- WMI（PNP0C14）の driver は無い（`grep PNP0C14 src` が 0）。SMBIOS の表を読む code も無い。
- `/dev/system`: `src/drivers/generic/system-device.c`（ioctl の振り分け 487〜）、UAPI `include/uapi/system.h`。ioctl の番号は 1〜21 を使用（12 は `mountinfo.h`）。
  事象の class は 0x01〜0x80（`KERN_SYSTEM_EVENT_CLASSES 0xff`、`system.h:321〜330`、`src/kern/system-event.c:193` が mask で検査）。root の判定は `cred_is_superuser`（`system-device.c:333〜337`）。
- AML の host 試験: `plan/ws049/tests/`（`aml-host`、5330 の表 `latitude5330/`、`check-latitude5330.sh`、EC と SMI の port の模擬 `aml-host-hardware.c:125・464`）。
  WS049 は completed だが、この試験は plan/tools に移っていない（§6.1 で使う、移すかは Q1）。
- docs: `docs/architecture/power-management.md:24` は「processor frequency control and thermal management」を対応外と書く（WS301 も同じ所を変える）。

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

## 3. 責務と境界

| 層 | 責務 | 持たない物 |
| --- | --- | --- |
| ACPI WMI（`src/drivers/acpi/acpi-wmi.c`、新規） | PNP0C14 を見つけ `_WDG` を解析し、GUID から block を引く。method（`WMxx`）・data の query（`WQxx`）・事象（Notify → `_WED`）を GUID と instance で呼べる口にする | vendor の意味（Dell の buffer の形）。user への口（WMI の生の口は user に出さない） |
| kernel の熱の registry（`src/kern/thermal.c`、新規、`include/kern/thermal.h`） | fan の provider と thermal profile の provider の登録、`/dev/system` の ioctl の中身、事象の発行（既定の profile を起動で書く事はしない、第 2 版 M8） | hardware の操作 |
| Dell の provider（`src/drivers/platform/pcat/dell-wmi.c`、新規） | DDV の fan の読み（method 0x20）、SMBIOS の呼び出しの class 0x11・select 0x13 の get・set、mode と共通の profile の対応 | Dell の他の機能（hotkey、BIOS の属性、電池の延長、privacy の LED）。SMM の fan の state |
| Pi 5 の provider（`rpi5-fan.c` に足す） | 今の level・回転数・level の数を fan の registry に出す（読み） | 新しい制御の policy（Q303-4 で決める） |
| UAPI（`include/uapi/system.h`） | fan の表・profile の get・set の ioctl、事象の class `KERN_SYSTEM_EVENT_THERMAL` | 温度（`hw.thermal` のまま） |
| 道具（`userland/base/thermal/`、新規） | 温度（`hw.thermal`）・fan・profile の表示と profile の設定 | — |

置き場所の理由: WMI は ACPI の規格の層なので `src/drivers/acpi/`（amd64 は `wildcard src/drivers/acpi/*.c` で入る、`platform/amd64/vmunix.mk:358`）。
Dell は PC の vendor の物なので `src/drivers/platform/pcat/`（build の規則に 1 行足す）。registry は arch に依らないので `src/kern/`。
既存の `hw.thermal` の registry（`sysctl.c`）は動かさない（ABI と読み手が既にある。thermal.c に寄せる整理は後の判断）。

## 4. 設計

### 4.1 ACPI WMI の層

- attach: `drv_acpi_wmi_attach()` を `acpi-kern.c` の thermal の後・LPS0 の前に置く（EC が要る: DDV は EC を読む）。namespace を歩いて `_HID` が "PNP0C14" の device を集め、
  `_WDG` を評価する（Name でも Method でもよい。AMW2 は Method）。20 byte の entry を解析する: GUID 16 byte、object id 2 文字（または event の notify の番号 1 byte と予約 1 byte）、
  instance の数 1 byte、flags 1 byte（method・string・event・expensive の bit）。形は Microsoft の「WMI と ACPI」の文書が定める（§9、Fable で版と節を確かめる）。
- 表: block を最大 48（5330 は 30、§2.3 の `/sys/bus/wmi/devices` の数）。同じ GUID が 2 つの device に出る時（5330 の MOF の GUID 05901221… は 9 つ）は先の物を返し、
  `drv_acpi_wmi_find_nth` で n 番目を引ける。
- 口（kernel の中、§5.1）: `drv_acpi_wmi_find(guid, &block)`、`drv_acpi_wmi_method(block, instance, method_id, input, input_size, &result)`（`WMxx(instance, method_id, buffer)` を評価、
  結果は integer・buffer・package・string の AML の object のまま返し、呼び手が解放）、`drv_acpi_wmi_query(block, instance, &result)`（`WQxx(instance)`）、
  `drv_acpi_wmi_event_install(guid, handler, argument)`（その device の Notify のうち notify の番号の物で `_WED(番号)` を評価して handler に渡す）。
- 入力の buffer: `drv_acpi_object_buffer_new` で作る。WMI の method は Arg2 を書き換えて返す事がある（`WMBA` の `Arg0 = GENS(...)` → `Return`）ので、結果は返り値だけを見る。
- AML の lock: method は interpreter の lock の下で走る。DDV・SMBIOS の呼び出しは EC の取引と SMI を含み数 ms〜数十 ms かかり得る（§4.10、§6 で測る）。
- 生の WMI を user に出さない（`/dev/acpi` の評価の口は既にある。WMI の method を任意に呼ばせる口は安全の面で作らない）。

### 4.2 Dell の provider

**見分け**: WMI の block に DDV（8A42EA14…）か SMBIOS の呼び出し（A80593CE…）があり、記述子（8D9DDCBC…）の `WQAA` が "DELL"・" WMI" を返す時に attach。機種の表（DMI の allow list）は持たない
（GUID と記述子で十分、機種の表を持つと機種ごとに手が要る）。DDV の版（method 0x12）が 3 でない時は fan の読みを止めて log に 1 行（形が違う恐れ）。

**fan の読み（DDV method 0x20）**: 入力の buffer は 1 byte 以上（AML は byte 0〜2 を `CreateByteField` する、無いと失敗）、中身は 0 を 4 byte。返りは package `{長さ, buffer}`。
buffer を 3 byte ずつ `[種類, 回転数 LSB, 回転数 MSB]` と読み 0xFF で止める（長さを超えない、§4.10）。fan の数は 4 まで。種類 0x00 は CPU の fan（§2.3 の観測。他の値の意味は §2.4）。
読みは registry の thread（§4.3）から 5 秒ごと（hw.thermal と同じ周期、EC の取引 2 + 3n 回）。

**thermal mode（SMBIOS の呼び出し、class 0x11・select 0x13）**:
- 呼び出しの buffer: 0x2C byte の頭（class・select・input[4]・output[4]・属性・延長の長さ）+ 延長。延長の長さは **0 を既定**にする（AML の上限 0x8000 まで、`SMBF` は dword ごとに
  SystemMemory の region を作るので、0x8000 だと 1 回の呼び出しで約 16000 回の region の作成と写しが interpreter の lock の下で走る）。延長 0 を firmware が受けない時は
  0x8000 − 0x2C にする（p004 で測って決める）。output は 0x14〜0x20 の 4 dword の見込み（§2.4、推定）。status の値と、対応の mode・今の mode の位置は公開の資料に無く、p004 の実機の読みで決める。
- get: input0 = 0 で呼ぶ形を試す（AML は input0 = 1 だけを set と見て、他は SMI の handler に任せる。§2.4 で get の形は不明）。
  output の中の「今の mode」の位置と符号は p004 で決める（方法は Q303-7、推しは書かずに読む: 起動の時の `\ODV0` と get の output を Debian で見た今の値と比べる）。
  起動の時に 1 回と set の後に 1 回、他に 60 秒ごと（§4.6）。
- set: input0 = 1、input1 = mode の bit（1・2・4・8）。AML が `ODV0` を 0〜3 にして SMI へ渡す（§2.1）。set の後に get で読み戻し、違えば EIO。
- mode と共通の profile の対応（§4.3 の profile の名前）: 1・2・4・8 と cool・quiet・balanced・performance の対応は §2.4 と p004 の確かめ（set の後に `/dev/acpi` で `\ODV0` を読む、
  get の答えと Debian の名前の一致）で決める。設計は「provider が bit の表を持つ」形にし、表の値は p004 で埋める。
- 永続: set は BIOS の設定を変える見込み（再起動の後も残る、未確認）。zedBSD は起動の時に mode を書かない（firmware の今の値を読むだけ）。書くのは user（root）が求めた時だけ（Q303-2）。

**SMM の fan の state（DIAG）は使わない**（§4.13）。

### 4.3 kernel の fan と thermal profile の registry

- fan: provider は `kern_fan_register(name, ops, context, flags)` で 1 本の fan を登録（全体で 8 本まで、外さない。hw.thermal の source と同じく kernel の寿命）。
  `ops->read(context, &state)` は回転数（rpm、無ければ flag なし）・level（今の段と段の数、無ければ flag なし）・mode（firmware が自動・OS が自動・手動）を返す。
  registry の thread が 5 秒ごとに全部の `read` を呼び、結果を写しに持つ（ioctl は写しを返す。AML や EC を ioctl の中で待たない、hw.thermal と同じ理由 `acpi-thermal.c:15〜19`）。
  `ops->set_level` は Pi 5 の手動を入れる時だけ（Q303-4）。
- thermal profile: provider は 1 つだけ（2 つ目の登録は EBUSY、log）。`ops->get(context, &available, &current)`、`ops->set(context, profile)`。
  profile は共通の名前の bit: `COOL`・`QUIET`・`BALANCED`・`PERFORMANCE`（Dell の 4 つ。Linux の platform_profile の名前とも揃う、§2.4）と `LOW_POWER`（予約、今は無い）。
- 事象: profile が変わったら（set の成功、または周期の get で firmware の側から変わった事を見たら）`KERN_SYSTEM_EVENT_THERMAL` の CHANGE（value = profile の bit、subject "thermal-profile"）。
  fan が止まった（level か mode が回る事を示すのに rpm 0 が 3 周期続く）時に CHANGE（subject "fan0"、detail "stalled=1"）。温度の critical は Q303-5 で決める。
- 周期の thread は registry に 1 本（`src/kern/thermal.c`）。provider ごとに thread を持たない（Dell の DDV は AML の lock を取るので並べても速くならない）。
  Pi 5 は今の fan の thread が自分の周期（既定 1 秒）で level を決めるので、registry の `read` は Pi 5 の写しを lock の下で返すだけ（MMIO を読まない）。

### 4.4 UAPI（`/dev/system`、推し。Q303-1 で決める）

- `KERN_SYSTEM_GET_FAN`（`_IOWR`、`struct system_fan_info`）: 入力 `index`、出力 `count`（全部の数）と index の fan。index ≥ count は ENOENT（`GET_PCI_DEVICE` と同じ数え方）。誰でも読める。
- `KERN_SYSTEM_GET_THERMAL_PROFILE`（`_IOR`、`struct system_thermal_profile`）: provider の名前、対応の bit、今の bit。provider が無ければ EOPNOTSUPP。誰でも読める。
- `KERN_SYSTEM_SET_THERMAL_PROFILE`（`_IOW`、同じ struct の `profile` だけ）: root だけ（EPERM）。対応の外の bit・2 bit 以上は EINVAL、provider が無ければ EOPNOTSUPP、firmware が拒めば EIO。
- 事象の class `KERN_SYSTEM_EVENT_THERMAL 0x00000100U`。`KERN_SYSTEM_EVENT_CLASSES` を 0x1ff にする（古い user の program は 0xff を使い続けて困らない）。
- 番号: 実装の時点の main の次の空き（今は 22〜24）。WS301 も `/dev/system` に足すので、両方が同時に進む時は Q1 が番号を割る。
- 温度は `hw.thermal` のまま（読み手が既にある）。fan を sysctl（`hw.fan`）にも出すかは Q303-1 の選択肢。

### 4.5 Pi 5

- `rpi5-fan.c` は今の動き（device tree の trip で level を選ぶ、温度が読めなければ全開）を変えない。足すのは `kern_fan_register("rpi5-fan", …)` と、
  `read` が `rpi5_fan` の level・level の数・回転数（最後の poll で読んだ値、`read_rpm` を poll の中で毎回読むようにして写しに置く）を lock の下で返すことだけ。mode は「OS が自動」。
- thermal profile の provider にはしない（推し。Q303-4 で「Pi 5 にも profile（trip の上下）を作る」を選べる）。

### 4.6 状態遷移

Dell の thermal profile（provider の中）:

```
 UNPROBED --attach: 記述子と GUID が有り、get が成功--> READY(current = firmware の値)
 UNPROBED --get が失敗--> ABSENT（profile の provider を登録しない、log 1 行）
 READY --set(p)--> SETTING --SMI の後の get が p--> READY(current = p)、事象 CHANGE
                   SETTING --get が p でない・呼び出しの失敗--> READY(current = get の値)、ioctl は EIO
 READY --周期の get（60 秒ごと）で current が変わった--> READY、事象 CHANGE
 READY --get が 3 回続けて失敗--> FAILED（ioctl は EIO、周期の get で戻れば READY）
```

fan（registry の写し）: `UNKNOWN`（最初の read の前）→ `OK`（read の成功）→ `STALE`（read が失敗、前の値を残し flag で示す）→ `OK`。止まりの判定は §4.3。

### 4.7 資源の所有と寿命

- WMI の block の表・Dell と Pi 5 の provider の状態・registry の表は static で kernel の寿命（hot-plug は無い、PNP0C14 は外れない）。
- AML の object（入力の buffer、結果）は呼んだ関数が作り、その関数の中で `drv_acpi_object_release` で解放する。結果の buffer を写しの外へ持ち出さない。
- 事象の handler（`drv_acpi_wmi_event_install`）は外さない（今の使い手は無い。Dell の hotkey の事象は扱わない）。

### 4.8 並行性と lock

- registry: spinlock（`LOCK_RANK_DEVICE`、irqsave）で写しを守る（hw.thermal と同じ形、`sysctl.c:156〜160`）。provider の `read`・`get`・`set` は lock の外で呼ぶ。
- profile の set と周期の get を直列にする mutex を registry に 1 つ（`thermal_profile_mutex`）。ioctl の set は mutex を取って provider の `set` → `get` → 写しの更新 → 事象。
  set が走っている間の 2 つ目の set は待つ（EBUSY にしない。時間は短い）。
- AML: provider の WMI の呼び出しは interpreter の lock を内側で取る（AML の側）。registry の mutex → interpreter の lock の順だけ。interpreter の lock を持ったまま registry の lock を取る道は無い
  （Notify の handler から registry を呼ばない。今の WMI の事象の使い手は無い）。
- Pi 5: 今の `rpi5_fan.lock` で level・rpm の写しを守る（`rpi5-fan.c:185〜221`）。registry の `read` はその lock を短く取るだけ。

### 4.9 割り込みと DMA

- DMA は無い。割り込み: WMI の事象は SCI → ACPI の事象の thread → Notify の handler（今の power の device と同じ道、`acpi-power.c:149`）。この WS で使う事象は無い（Dell の profile の変化を firmware が知らせる事象は 5330 の AML に見当たらない。周期の get で拾う）。
- SMI: `ASMI` の port の書きで CPU は SMM に入り、全 CPU が止まる時間がある（firmware の物、数十 µs〜ms の見込み、未確認）。profile の set・get は稀（起動 1 回、60 秒ごと、user の求め）なので許す。
  DDV の fan の読みは SMI を使わない（EC だけ、§2.1）。

### 4.10 失敗と回復

| 失敗 | 振る舞い |
| --- | --- |
| PNP0C14 が無い（QEMU、他の機械） | WMI の表が空。Dell の provider は attach しない。fan の count 0、profile は EOPNOTSUPP |
| `_WDG` の長さが 20 の倍数でない・評価の失敗 | その device を飛ばし log 1 行 |
| DDV の buffer が短い・0xFF が無い・長さが package の長さを超える | 読めた所まで、fan の数は 4 で切る、全部読めなければ read の失敗（STALE） |
| DDV の EC の取引の失敗（AML の Acquire の timeout は 0xFFFF = 無限） | AML が返らない恐れ。EC の driver の取引の timeout が AML の評価を EIO で返す事を p002 の host 試験で確かめる（未確認、§10） |
| SMBIOS の呼び出しの output の status が成功でない | get なら profile の provider を登録しない（ABSENT）、set なら EIO と log |
| SMBF の 0x8000 の写しが遅い（interpreter の lock を長く持つ） | 延長 0 を既定にする（§4.2）。p004 で時間を測り、受け入れに上限（1 回 50 ms）を置く |
| 2 つ目の profile の provider | EBUSY、log |

温度が上がった時の保護は今の通り firmware と CPU の hardware（EC の fan、CPU の thermal throttling）に任せる。OS の critical の停止は Q303-5。

### 4.11 既存の framework との関係

- `hw.thermal`（sysctl）: 変えない。Dell の DDV の温度は足さない（ACPI の `_TMP` と同じ値、§2.3）。
- `/dev/system`: ioctl と事象の class を足す（§4.4）。`system-device.c` は registry の関数を呼ぶだけ（weak の symbol にする: ACPI の無い platform・Pi 5 の無い build でも link が通る形、`drv_acpi_power_get` と同じ `system-device.c:109`）。
  registry は `src/kern/` なので全 platform に入る。provider が無ければ空。
- WS301（CPU の性能）: passive cooling（熱で CPU の上限を下げる）は WS301 p002 の口。5330 では firmware と hardware が守るので、この WS の推しでは OS の passive を入れない（Q303-6）。
  profile と WS301 の policy（performance・balanced・powersave）の連動は WS301 の設計で決める（例: profile の CHANGE の事象を WS301 の policy が購読する）。この WS は事象を出すまで。
- amdrdna（WS039）の GPU の fan: GPU の board に閉じた物で、`include/uapi/amdrdna.h:93〜99` の fan_mode のまま。fan の registry に載せるかは後（推しは載せない。GPU の telemetry に既にある）。
- Keiland: profile の表示と切り替え（設定の app の電源の頁）は別の WS（Keiland の app の側）。この WS は UAPI と `thermal` の道具まで。

### 4.12 道具 `thermal`

`userland/base/thermal/`（全 arch で build、§guardrail「userland/base の program の有無」）。`thermal` で温度（hw.thermal）・fan・profile を表で出す、
`thermal profile` で今の profile と選べる物、`thermal profile quiet` で設定（root）。出力は 1 行 1 項目（試験が読みやすい形）。

### 4.13 選ばなかった案

| 案 | 選ばなかった理由 |
| --- | --- |
| SMBIOS の type 0xDA の表から port と command を読み、kernel が物理の buffer を用意して SMI を直に叩く（WMI を通さない） | firmware の共有の記憶を AML が既に持ち、`WMBA` がそれを使う。直の道は 4 GiB 未満の物理の buffer、SMI の port の書き、firmware の版ごとの差を kernel が持つ。5330 は WSMT の表を持ち（dmesg の ACPI の一覧）、SMI の handler が渡された buffer の番地を検める見込みで、自前の buffer は拒まれる恐れ（未確認）。WMI の層は DDV にも要るので、同じ層で済む |
| SMM の fan の state（DIAG の WMI、`dell_smm` の pwm1）で 5330 の fan を手動で回す | 公開の資料で command の値と効き方が確かめられない（GPL の source に頼る）。Linux の文書も自動の制御の切り替えに重い副作用、11 世代以降の Dell で効かない報告、1 回の呼び出しで数秒の停止の報告がある（§2.4）。EC が自動で回す fan を OS が上書きすると、OS が止まった時・寝た時に fan が誤った state のまま残る恐れ。thermal mode で目的（静か・冷たい）の大半に届く。ユーザーが望めば別の Phase（Q303-3） |
| DDV の温度を `hw.thermal` に足す | ACPI の `_TMP` と同じ値（§2.3）。重ねると monitor に同じ温度が 2 つ出る |
| Intel DTT（DPTF）の framework を作る（`_TRT`・`_ART`・`GDDV`・`ODVP` の policy） | 大きい（Intel の policy の data vault は圧縮された独自の形）。5330 は DTT 無しでも firmware と hardware が守る。到達目標（読み・mode の選択）に要らない |
| WMI の生の method を user に出す（Linux の wmi の chardev のように） | 任意の SMI を user が起こせる。必要な機能だけを kernel が意味のある口で出す |
| 温度・fan・profile を新しい device `/dev/thermal` に | 口が増え、権限と事象の仕組みを作り直す。WS301 の推し A と同じ理由で `/dev/system` |
| fan を `hw.thermal` の entry の種類に混ぜる | `thermal_entry` の値は温度の単位で、rpm・level の欄が無い。ABI の version を上げる事になる |

## 5. インタフェース（案。名前は実装の Phase で coding-style に合わせて決める）

### 5.1 kernel の中

```c
/* include/drivers/acpi/acpi.h に足す */
struct drv_acpi_wmi_block;

int drv_acpi_wmi_attach(void);
int drv_acpi_wmi_find(const uint8_t guid[16], struct drv_acpi_wmi_block **block);
int drv_acpi_wmi_find_nth(const uint8_t guid[16], unsigned nth, struct drv_acpi_wmi_block **block);
unsigned drv_acpi_wmi_instances(const struct drv_acpi_wmi_block *block);
int drv_acpi_wmi_method(struct drv_acpi_wmi_block *block, unsigned instance, uint32_t method_id,
    const void *input, size_t input_size, struct drv_acpi_object **result);
int drv_acpi_wmi_query(struct drv_acpi_wmi_block *block, unsigned instance, struct drv_acpi_object **result);
typedef void (*drv_acpi_wmi_event_t)(struct drv_acpi_object *data, void *argument);
int drv_acpi_wmi_event_install(const uint8_t guid[16], drv_acpi_wmi_event_t handler, void *argument);

/* include/kern/thermal.h（新規） */
#define KERN_FAN_HAS_RPM	0x1U
#define KERN_FAN_HAS_LEVEL	0x2U
#define KERN_FAN_MODE_FIRMWARE	1U	/* the firmware or the EC drives it */
#define KERN_FAN_MODE_KERNEL	2U	/* a kernel driver drives it from temperatures */
#define KERN_FAN_MODE_MANUAL	3U	/* held at a level a user set */

struct kern_fan_state {
	uint32_t flags;
	uint32_t mode;
	uint32_t rpm;
	uint32_t level;
	uint32_t level_count;
};

struct kern_fan_ops {
	int (*read)(void *context, struct kern_fan_state *state);
	int (*set_level)(void *context, uint32_t mode, uint32_t level);	/* NULL: cannot be set */
};

int kern_fan_register(const char *name, uint32_t rpm_max, const struct kern_fan_ops *ops, void *context);

struct kern_thermal_profile_ops {
	int (*get)(void *context, uint32_t *available, uint32_t *current);
	int (*set)(void *context, uint32_t profile);
};

int kern_thermal_profile_register(const char *name, const struct kern_thermal_profile_ops *ops, void *context);

/* system-device.c が呼ぶ（weak） */
int kern_thermal_fan_describe(uint32_t index, struct system_fan_info *info);
int kern_thermal_profile_describe(struct system_thermal_profile *profile);
int kern_thermal_profile_change(uint32_t profile);
```

### 5.2 UAPI（`include/uapi/system.h` に足す、全部の欄が固定の幅、ILP32 と LP64 で同じ）

```c
#define KERN_SYSTEM_EVENT_THERMAL	0x00000100U	/* the thermal profile changed, a fan stalled */

#define KERN_SYSTEM_FAN_NAME_MAX	32U
#define KERN_SYSTEM_FAN_HAS_RPM		0x1U
#define KERN_SYSTEM_FAN_HAS_LEVEL	0x2U
#define KERN_SYSTEM_FAN_STALE		0x4U	/* the last read failed; the values are older */
#define KERN_SYSTEM_FAN_STALLED		0x8U

struct system_fan_info {
	uint32_t index;		/* in */
	uint32_t count;		/* out: how many fans there are */
	uint32_t flags;
	uint32_t mode;		/* KERN_FAN_MODE_* */
	uint32_t rpm;
	uint32_t rpm_max;	/* 0: unknown */
	uint32_t level;
	uint32_t level_count;
	uint64_t time_ns;	/* when it was read */
	char name[KERN_SYSTEM_FAN_NAME_MAX];
	uint32_t reserved[4];
};	/* 88 bytes */

#define KERN_SYSTEM_THERMAL_PROFILE_LOW_POWER	0x01U
#define KERN_SYSTEM_THERMAL_PROFILE_COOL	0x02U
#define KERN_SYSTEM_THERMAL_PROFILE_QUIET	0x04U
#define KERN_SYSTEM_THERMAL_PROFILE_BALANCED	0x08U
#define KERN_SYSTEM_THERMAL_PROFILE_PERFORMANCE	0x10U

struct system_thermal_profile {
	uint32_t available;	/* out */
	uint32_t profile;	/* out: current; in: the one to set */
	char provider[16];	/* out: "dell-wmi" */
	uint32_t reserved[2];
};	/* 32 bytes */

#define KERN_SYSTEM_GET_FAN		_IOWR(KERN_SYSTEM_IOC_GROUP, 22, struct system_fan_info)
#define KERN_SYSTEM_GET_THERMAL_PROFILE	_IOR(KERN_SYSTEM_IOC_GROUP, 23, struct system_thermal_profile)
#define KERN_SYSTEM_SET_THERMAL_PROFILE	_IOW(KERN_SYSTEM_IOC_GROUP, 24, struct system_thermal_profile)
```

### 5.3 Dell の buffer（provider の内部、値は §2.1 の AML と p004 の確かめ）

```c
/*
 * Values referenced: the Latitude 5330's DSDT (BIOS 1.31.1), \_SB.AMW0.WMBA
 * (class at 0, select at 2, input words from 4, the extension length at
 * 0x28, the header 0x2C bytes) and \_SB.AMWV.WMDV (method ids 0x12, 0x20).
 */
#define DELL_CALL_CLASS_OFFSET		0x00U
#define DELL_CALL_SELECT_OFFSET		0x02U
#define DELL_CALL_INPUT_OFFSET		0x04U
#define DELL_CALL_OUTPUT_OFFSET		/* p004 で確かめる（§2.4） */
#define DELL_CALL_EXTENSION_OFFSET	0x28U
#define DELL_CALL_HEADER_SIZE		0x2CU
#define DELL_THERMAL_CLASS		0x11U
#define DELL_THERMAL_SELECT		0x13U
#define DELL_DDV_VERSION		0x12U
#define DELL_DDV_FAN_INFORMATION	0x20U
```

## 6. 試験の計画

### 6.1 host 試験（QEMU も実機も要らない）

- WMI の層: `plan/ws049/tests/` の `aml-host` に 5330 の表を読ませ、`_WDG` の解析が 9 device・30 block・GUID と flags を §2.1 と同じに出す（期待の表は Debian の `/sys/bus/wmi/devices` の 30 個）。
- DDV: harness の EC の模擬に EC の RAM の像（0x6F = 1、0x6F の 2 回目 = 0x00、0x70・0x71 = 0x39・0x0f、0x6D と 0x6E の列）を入れる option を足し、`WMDV(0, 0x20, buf)` の package を
  provider の解析の関数に通して「fan 1、種類 0、3897 rpm」、`WMDV(0, 0x12, buf)` = 3。0xFF の欠け・長さの超過・空の buffer の場合。
- SMBIOS の呼び出し: harness の SMI の port の模擬で、`WMBA` に class 0x11・select 0x13・input0 1・input1 4 を渡し、`ODV0` が 2 になる事、SMI の port が書かれる事、
  `SMBA` の像に buffer が写り写し戻る事。延長 0 と 0x8000 − 0x2C の 2 つで、評価の時間（host で）を比べる。
- registry: kcrt の host build で、fan の登録と写し・STALE・STALLED（rpm 0 が 3 周期）・profile の set の検査（EPERM の判定は ioctl の層で別に）・2 つ目の provider の EBUSY。
- UAPI の layout: `_Static_assert`（88・32 byte）、ILP32 と LP64 の build。
- 試験は `plan/ws303/tests/`。WS049 の harness を使うので、harness を plan/tools に移すかは Q1（§1）。

### 6.2 QEMU

- amd64 の q35（PNP0C14 無し）: `thermal` が fan 0 本・profile 無し（EOPNOTSUPP）を出し、起動の回帰（`boot-test.sh`）が PASS。ACPI の無い arch（riscv64 の virt など 1 つ）で `thermal` が動く。
- QEMU の SSDT に PNP0C14 の試験の表（ASL で `_WDG` と `WMxx` を書いた物、`plan/ws049/tests/asl/` の作り方）を足す案は、host 試験で足りるので推しでは作らない。

### 6.3 実機

- 5330（素の起動、`plan/tools/hw5330/5330-run.sh`、T1 か P5 の後継に Q1 が割り当てる。1 本の script `plan/ws303/tests/5330-thermal.sh`）:
  1. 起動、`thermal` の出力を回収: fan 1 本の rpm が 0 でない、profile の provider "dell-wmi"、available が 4 つ、current が Debian で見た値（今は performance）。
  2. profile を quiet にして 60 秒の負荷（`kstress` の CPU の負荷か `yes` を 12 本）、rpm と温度を 5 秒ごと。performance に戻して同じ負荷。quiet の rpm の上限が performance より低い事を数で示す（効かなければ「mode は効かない」と記録し、受け入れから外すかをユーザーに聞く）。
  3. 元の profile に戻し、再起動の後も元の値である事。`/dev/acpi` で `\ODV0` を読み、mode の bit との対応を記録。
  4. Debian に戻って `platform_profile` が元の値である事（読みだけ）。
- Pi 5（素の起動、Q1 の割当）: `thermal` が "rpi5-fan" の level と rpm を出し、負荷で level が上がる。

## 7. Phase の分割と LW（±50%、1 LW ≈ 実時間 15〜25 分）

| Phase | 内容 | 依存 | LW | 受け入れ |
| --- | --- | --- | --- | --- |
| p001 | 調査と設計（この文書）、design-reviewer、ユーザーの判断 | — | （済み） | Q303-1〜6 の答え、design.md の確定 |
| p002 | ACPI WMI の層（`acpi-wmi.c`）と host 試験（5330 の表、EC の像、SMI の模擬） | p001 | 4 | §6.1 の WMI・DDV・SMBIOS の host 試験 PASS、build warning 0（amd64） |
| p003 | kernel の registry（`thermal.c`）、UAPI（§5.2）、`system-device.c` の ioctl、`thermal` の道具、host 試験 | p001（UAPI の決定） | 4 | registry の host 試験 PASS、全 platform の build warning 0、QEMU の §6.2 |
| p004 | Dell の provider（DDV の fan、SMBIOS の thermal mode、mode の表を実機で確定） | p002、p003 | 5 | 5330 の §6.3 の 1〜4（set の 1 回の時間 50 ms 以下）、mode の表の記録 |
| p005 | Pi 5 の fan を registry に出す | p003 | 1 | Pi 5 の §6.3、build warning 0（arm64） |
| p006 | 規約の全文の見直し、`docs/architecture/power-management.md` の更新（ユーザーの review） | p004、p005 | 2 | 規約の全文、文書 |

合計 16 LW（p001 を除く）。選択肢で足す物: Q303-3（5330 の SMM の fan の手動）+4、Q303-4（Pi 5 の profile か手動）+2、Q303-5（critical の停止）+2、Q303-6（WS301 の passive）+2（WS301 p002 の後）。

WS303 の ws.md の依存「p001 は WS301 p002 の後」は、passive cooling を入れる時だけの依存なので、推しの範囲では外せる（Q303-6）。

## 8. ユーザーに決めてほしい点（推しと選択肢）

- **Q303-1 UAPI の置き場所**: (A, 推し) fan と profile は `/dev/system` の ioctl、温度は `hw.thermal` のまま。(B) A に加え fan の読みを `hw.fan`（sysctl）にも出す（monitor が sysctl だけで読める）。(C) 新しい `/dev/thermal`。
- **Q303-2 profile を変えてよい人**: (A, 推し) root だけ（WS301 の設定と同じ、2026-10-27 の決定に揃える）。(B) desktop の session の user も（Keiland の設定の app から sudo なしで）。
  補い: Dell の mode は BIOS の設定を書き換え、再起動の後も残る見込み。
- **Q303-3 5330 の fan の手動の制御**: (A, 推し) 入れない（mode の選択まで）。(B) 別の Phase で SMM の fan の state（0〜2）を入れる（command の値の出典に GPL の file の値を使う事になる、guardrail の「値は引用で可」で扱う）。
- **Q303-4 Pi 5**: (A, 推し) 読みだけ（level・rpm を出す）。(B) 手動の level の上書き（root、温度が critical に近づいたら自動に戻す）。(C) profile（cool・quiet・balanced を trip の上下で作る）。
- **Q303-5 温度の critical**: (A, 推し) 今の通り何もしない（firmware と CPU の hardware が守る、5330 の participant は DPTF の物で ACPI の thermal zone ではない）。(B) `_CRT` を超えたら log と事象だけ。(C) ACPI の thermal zone の `_CRT` を超えたら停止（今の対象の機械に thermal zone は無い）。
- **Q303-6 WS301 の passive cooling**: (A, 推し) この WS に入れない（5330 は hardware が守る）。WS303 p001 の WS301 p002 への依存を外す。(B) WS301 p002 の後に、`_PSV` で CPU の上限を下げる Phase を足す。
- **Q303-7 mode の符号の確かめ方（p004）**: (A, 推し) まず書かずに読む: zedBSD の起動で `\ODV0`（GNVS の値、SMI を起こさない）と get の output を読み、BIOS の今の値（Debian で performance）と比べる。足りなければ set を 4 つ試して元に戻す（BIOS の設定を一時に書き換える）。(B) ユーザーが BIOS の setup で Thermal Management を 1 つずつ選んで起動し、各 mode の output を記録する（ユーザーの手が 4 回要る）。(C) Debian の上で Linux の Dell の driver に mode を切り替えさせ、zedBSD で読む（Debian の設定を変える）。

## 9. 出典と引用の扱い（guardrail の「出典」）

- 5330 の ACPI の表（Dell の firmware、hardware の事実）: method の名前・offset・GUID・EC の番地は値として使い、code の comment に「Value(s) referenced: the Latitude 5330's DSDT (BIOS 1.31.1), \_SB.AMWV.WMDV」の形で block ごとに書く。
- 5330 の Debian の観測: 値の確かめに使う（Linux の source は読んでいない。Linux の label の名前は写さず自前の名前にする）。
- Microsoft「Windows Instrumentation: WMI and ACPI」（`_WDG` の形、`WMxx`・`WQxx`・`_WED`）、ACPI 6.5 §11.3・§11.4（fan と trip、5330 には無いが Pi 5 以外の後の provider の参照）: 手順と値の出典として「Procedure referenced:」「Value(s) referenced:」で block ごとに書く。
- Dell の公開の文書（Service Manual、DCC の参照、KB）: mode の名前（Optimized・Cool・Quiet・UltraPerformance）の出典。zedBSD の名前は共通の profile の名前（§5.2）で、Dell の名前は provider の表の comment に出典つきで書く。
- Linux の文書の散文（ABI・hwmon・wmi の文書）は概念の確かめだけに使い、名前・構造を写さない。
- GPL の source（Linux の dell-* の driver、libsmbios）は読まない。値が要る時（Q303-3 の B）はユーザーの判断で guardrail の「GPL の値は引用で可」を当てる。

## 10. 未確認（実装の前・中で確かめる）

1. SMBIOS の呼び出しの output の位置と status の値、get の形（input0 = 0 か）、mode の bit と cool・quiet・balanced・performance の対応（§2.4 → p004）。
2. 延長の長さ 0 を firmware が受けるか、受けない時の 0x8000 の評価の時間（p002 の host、p004 の実機）。
3. thermal mode が DTT の無い OS で fan と電力に効くか（p004 の実機）。
4. mode が BIOS の NVRAM に残るか（p004、再起動）。
5. EC の取引の失敗で AML の `Acquire(ECM1, 0xFFFF)` の後が返るか（p002 の host 試験で EC の模擬に失敗を入れる）。
6. DDV の fan の種類の byte の他の値（5330 は 0x00 の 1 本だけ、§2.4）。
7. BIOS が起動の時に GNVS の `ODV0` を今の mode で埋めるか（AML の中で `ODV0` を書くのは `WMBA` の set の道だけ。埋めないなら Q303-7 の A の「書かずに読む」は get の output だけに頼る）。
