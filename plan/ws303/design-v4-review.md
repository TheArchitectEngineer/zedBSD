<!-- awesome-plan project=zedbsd record=ws303-design-v4-review -->

# WS303 設計の敵対的レビュー（design.md 第 4 版と「第 4 版への追記」）

日付: 2026-10-29（host の `date`、04:23 JST）。担当: design-reviewer（P5 の依頼）。
対象: [design.md](design.md) の「第 4 版への追記」（10〜38 行）・「第 4 版の改訂」（40〜72 行）と、それらが「第 3 版の改訂」（74〜140 行）・「第 2 版の改訂」（142〜194 行）・本文 §0〜§10（196〜638 行）と食い違わないか。[ws.md](ws.md)。
関係: [WS301 ws.md](../ws301/ws.md)・[WS301 design.md](../ws301/design.md)（第 2 版と「第 2 版への追記」）。前の review: [design-review.md](design-review.md)（第 1 版）、[design-v3-review.md](design-v3-review.md)（第 3 版）。

照合した物: worktree `p5-design-c`（HEAD d94ceac00）。design.md の「基点」は `p5-design-b`（63386d8a4）だが、引用の source（`src/drivers/acpi`・`src/kern/{sysctl,signal,shutdown}.c`・`system-device.c`・`userland/base/init`・monitor・`rpi5`・`include/uapi`）は 63386d8a4..d94ceac00 で変わっていない（`git diff --stat` で `gpu-op.h` と sessiond の 2 file だけ）。
読んだ source: `userland/base/init/main.c`（38、50〜88、270〜350、790〜850、1015〜1030、1515〜1535、1840〜2060、2255〜2280、2455〜2480）、`src/kern/signal.c`（関数の一覧、425〜470、1410〜1430）、`include/kern/process.h`（200〜215）、`src/kern/process.c`（2695〜2715）、`src/kern/shutdown.c`（1〜140）、
`src/drivers/acpi/acpi-event.c`（1000〜1105）、`src/drivers/acpi/acpi-kern.c`（395〜450）、`src/drivers/acpi/aml-thread.c`（48〜107）、`src/drivers/acpi/aml-sync.c`（1〜140）、`src/drivers/acpi/acpi-ec.c`（708〜750）、`src/kern/platform/{pcat.c:640〜700,rpi5.c:300〜327}`、
`src/drivers/generic/system-device.c`（150〜180、325〜340、540〜575）、`src/kern/vfs.c`（455〜475）、`src/kern/devfs.c`（570〜610）、`include/kern/cdev.h`（70〜110）と全部の `cdev_register` の呼び場所、`include/kern/system-event.h`、`include/uapi/system.h`（311〜368）、`include/uapi/{ioctl,termios,signal}.h`、全 UAPI の ioctl の group、
`src/kern/sysctl.c`（hw.thermal の registry）、`include/kern/sysctl.h`（80〜100）、`include/uapi/sysctl.h`（252、286〜300）、`userland/base/sysctl/main.c`（hw.thermal の部分）、`userland/desktop/libkeiland-backend-zedbsd/monitor-zedbsd.c`（20〜25、640〜685）、`src/drivers/platform/rpi5/rpi5-fan.c`（40〜100、330、391〜393、1000〜1030）、
`Makefile`（395〜420、947〜955）、`config/kernel-options.list`、`plan/guardrail.md`（112〜125）、`plan/master.md`（91〜94、139〜146）、`plan/ws300/ws.md`（頭）。
firmware: scratchpad の `ws303-acpi/dsdt.dsl`（`_PTS` 27540〜、`EV1` 101583、`ECM9` 96364、`ECIN` 96311、`ECW1` 97010、DIAG 94054〜94071）、`out-ssdt3.dsl`（TCPU の TJMX 1390〜1483）。
**GPL・LGPL の source は読んでいない。web は使っていない。何も build・実行していない（試験は未実施）。**

重大度: **blocker**（このまま実装に入ると目的・安全の道を壊す、または実装の担当が一意に読めない）、**major**（実装の前に設計を直す要がある）、**minor**（実装の中で直せる・文書の整合）。「推測」は source・表で確かめられなかった事。

## 要点

- **blocker 1 件**: (B1) 4 つの改訂の節と本文の層で、実装の担当が一意に読めない。Phase の表の本体が無く（ws.md は「第 3 版の表が本体」と書く）、p007 の受け入れは Q303-14 A と食い違い、Pi 5 の profile（V4 の cool・quiet）は追記の strategy の抽象と食い違い、唯一の具体の code（本文 §5）は `/dev/system`・`KERN_FAN_MODE_MANUAL`・profile の bit のまま。第 3 版の review の M9 の直し（節ごとの「置き換え」の印）も入っていない。
- **major 11 件**: strategy の抽象の UAPI・受け手の口・状態（起動の時・custom と manual・部分の失敗・軸の向き）が無い（M1）、Q303-17 A の lock の順の逆転（M2）、受け手の持ち主が WS303 p007 と WS301 p004 で二重（M3）、WS301 の記録と Phase の依存の食い違い（M4）、`/dev/thermal` が `CONFIG_CPU_PERF` を持たない kernel で link する形が無い（M5）、非常の判定の thread が provider の呼びで止まり得る（M6）、V8 の「唯一の読みの源は acpi-thermal」が Pi 5（非常の停止が実際に効く唯一の機械）を外す（M7）、SIGUSR2 の handler の無い init は SIGUSR2 で死に、120 秒の直の切りは温度の条件で起きないことがある（M8）、`_PTS` の「lock を 1 秒で」は評価の長さを縛らない（M9）、非常の停止の QEMU の試験が init の道と直の切りの道を見分けられない（M10）、p003 が 8 LW に収まらない（M11）。
- **minor 16 件**（下）。
- 確かめて正しかった主張は末尾の表（group 'T' の空き、init の signal の行、`signal_send_process`、`mutex_trylock` の有無、hw.thermal の読み手の全部、guardrail の GPL の値の規則、rpi5 の GPL の値の前例など）。

## 依頼の (1) 層の形の矛盾の一覧

上の節が下を置き換える規則で読んだ時、置き換えが明示されず下の記述が生きて見える所:

| 項目 | 生きて見える記述 | 置き換える記述 | 判定 |
| --- | --- | --- | --- |
| 版の優先 | 5 行「第 3 版の review を受けた『第 4 版の改訂』が最優先」 | 10 行「この節（追記）が第 4 版の改訂の問いの推しに優先する」 | 頭の版の行が古い。追記は「推し」だけに優先と書き、V1〜V10 の本体には優先しないとも読める（minor m1） |
| UAPI の置き場 | §0 の 2（200〜201）・§3（298・301）・§4.4（360〜367）・§4.11（428）・§4.13（449「`/dev/thermal` を選ばない」）・§5.2（505〜546）・§6.2（583） | 第 3 版 T1（102〜106）、追記の解釈 5（28） | 本文に置き換えの印が無い。§5 が唯一の具体の code（B1） |
| `hw.thermal` | T1（106）「残す」、V8（55）「`thermal_entry` は変えない」、§4.11（427）「変えない」 | 追記の解釈 1（22）「p009 で外す。V8 の『残す』はこれで置き換わる」 | V8 のうち何が残るか（2 つの registry、`KERN_THERMAL_*`、group 'T'、唯一の読みの源、「2 回続けて」の数え方）が書かれていない（B1） |
| strategy の名前 | T2（107）cool・quiet・balanced・performance、T7（116）、V4（50）Pi 5 の cool・quiet、第 4 版 Q303-11（69）、§4.3（354）・§5.2（530〜534）の profile の bit、§4.12（438）`thermal profile quiet`、§6.3（590）・R10（162）の quiet → performance の比べ | 追記の解釈 2（23〜25）performance・balanced・powersave・manual、「第 4 版の Q303-11 の表は取り下げ」 | V4 は取り下げの対象に名指しされていない（p006 の担当は V4 を読む）。5330 の試験（R10・§6.3）の手順が古い名前のまま（B1） |
| Q303-10 の推し | 第 4 版 Q303-10（68）「B 推し」、m2（59） | 追記（14・22）「root/wheel、B は取り下げ」 | 合う（追記が原文の答え） |
| Phase の表と LW | 第 3 版の表（120〜131、29 LW）＋ 第 4 版の差分（63、30 LW）＋ 追記の差分（38、32 LW） | ws.md（25〜35）「第 3 版の表が本体（下は写し）」（15）、再開点「合計 約 30 LW」（39） | 本体が 3 つの差分の合成で、ws.md の本体の指定が古い。ws.md の表の和は 32 なのに再開点は 30（B1、m2） |
| p007 の受け入れ | 第 3 版の表（128）「5330 で TCPU が passive を超えた時に上限が下がる記録」 | Q303-14 A（27）「仕組みと host 試験まで、実機は後」 | 第 4 版・追記の差分は p007 の受け入れを直していない（B1） |
| p008 の依存 | 第 3 版の表（129）「p004〜p007」 | ws.md（34）「p004〜p007、p009」、追記（38）は書かない | 本体の design に無い（m2） |
| 非常の停止の手順 | T4（112）「60 秒」「`KERN_SYSTEM_POWEROFF` の中の処理」 | V3（47〜49）「120 秒」「`system_shutdown_prepare` を通さない」 | V3 が置き換えると読めるが T4 に印が無い（B1） |
| hot | T1（104）`THERMAL_GET_SENSOR` の hot・emergency、T4（111〜112）「hot で boost を最大」、T1 の事象（103）の「hot の跨ぎ」 | V2（46）「hot の規則を外す」、V10（58）sensor は passive・critical だけ | 事象の種類から hot を外すとは書いていない（m3） |
| passive の段 | T5（114）「HWP の単位で highest の 10%」 | V6（53）千分率 | T5 の単位が古い（m4） |
| weak の symbol | §4.11（428）「weak の symbol にする」 | R7（157）「weak の symbol は使わない」 | R7 が正。本文に印が無い（B1、M5 にも効く） |
| 出典 | §9（628）「GPL の source は読まない。値が要る時はユーザーの判断で」 | R11（166）・追記の解釈 3（26） | §9 が古い（m15） |

## Blocker

### B1. 層の形で実装の担当が一意に読めない（design.md 全体、ws.md:15・25〜39）

- 根拠: 上の表。とくに次の 4 つは、実装の担当が「上の節が下を置き換える」規則を正しく当てても誤る。
  1. **Phase の表の本体が無い**。ws.md:15 は「Phase の表は design.md の『第 3 版の Phase の表』が本体（下は写し）」。第 3 版の表（design.md:120〜131）には p009 が無く、p003 は 7 LW、p007 の依存に WS301 p003 が無い。第 4 版（63）と追記（38）は差分の文だけ。AGENTS.md §2「本体（owner）が意味を決め、他の表は写し」の本体が文の合成になっている。
  2. **p007 の受け入れが Q303-14 A と食い違う**（design.md:128 と 27・71）。p007 の担当は 5330 で届かない受け入れ（第 3 版 review の B1）を本体の表から読む。
  3. **Pi 5 の profile**: V4（design.md:50）は「balanced = DT の trip、cool = 全 trip −5 °C、quiet・performance = DT の trip」で、追記（23〜25）の「cool・quiet は strategy の名前から外れる」「Pi 5 の provider は noise から trip の組を決める」と別の物。追記の「第 4 版の Q303-11 の表は取り下げ」は 69 行の問いの表で、V4 の本文を名指ししない。
  4. **唯一の具体の code（本文 §5.1・§5.2、design.md:456〜546）が古い**: `/dev/system` の ioctl 22〜24、`KERN_SYSTEM_EVENT_THERMAL 0x100`、`KERN_FAN_MODE_MANUAL`「held at a level a user set」と `set_level(context, mode, level)`（ユーザーの「下回らせない」に反する）、profile の 5 つの bit、`kern_thermal_profile_register`（「provider は 1 つだけ、2 つ目は EBUSY」§4.3:353）。V10（58）は「`KERN_FAN_MODE_MANUAL` は外す」と書くが §5.1 の本文は残る。第 3 版の review の M9 の直しの案（本文の食い違う節の頭に「置き換え」の 1 行）は入っていない。
- 起こる状況: p003 の担当が §5 を下敷きに `include/uapi/thermal.h` を書き、MANUAL の fan の段・profile の bit・provider 1 つの形を作る。p006 の担当が V4 の cool・quiet を作る。p007 の担当が 5330 の受け入れを試みて uncleared にする。Q1 が ws.md の古い本体の指定で Queue を切る。
- 直し方の案: 実装の Phase（p002・p003）に入る前に **第 5 版として本文を 1 つに書き直す**（今の 4 つの改訂の節を本文に織り込み、改訂の節は「経緯」として末尾に移すか消す）。少なくとも (a) Phase の表を 1 つの完全な表として書き（依存・LW・受け入れ、p009 を含む）、ws.md:15 の本体の指定を直す、(b) §5 を `include/uapi/thermal.h` と `include/kern/thermal.h` の案に置き換える（M1 の strategy、V10 の欄、M5 の CPU の ioctl の範囲）、(c) §0・§3・§4.3・§4.4・§4.11〜§4.13・§6・§9 の頭に「第 n 版の X で置き換え」の 1 行、(d) V4 を追記の形（noise → trip、fan は DT より遅くしない）に書き直す。

## Major

### M1. strategy の抽象の UAPI・受け手の口・状態が定まっていない（追記の解釈 2・5、Q303-15〜17、V5、V10）

- 根拠: 追記（design.md:23〜25）は 4 つと manual の 3 軸（0〜100）、「対応は driver が決める」まで。V10（58）の strategy の欄は「対応の bit・今・受け手ごとの結果」で軸が無い。V5（51〜52）「起動の時は firmware のまま、表示は firmware の今の mode（Dell は `ODV0`）」「一部が失敗したら partial」。Q303-17 A（34）「直の設定は strategy より優先し、strategy は『manual（custom）』と表示」。受け手の口 `kern_thermal_strategy_receiver_register`（51）の引数・戻り・呼ぶ文脈はどこにも無い。
- 起こる状況（状態遷移の抜け）:
  1. **起動の時の値**: 抽象の 4 つに firmware の値の対応が無い。5330 の `ODV0` が Cool か Quiet（Dell の 4 つのうち抽象に 1 対 1 で当たらない物）の時、GET は何を返すか（performance・balanced・powersave・manual のどれでもない）。Pi 5 と QEMU（受け手の firmware が無い）も同じ。第 3 版の review の m6（「不明」の値を UAPI に持つ）は入っていない。
  2. **manual と custom の混同**: Q303-17 A は WS301 の直の設定の後を「manual（custom）」と示す。manual はユーザーが 3 軸を選ぶ strategy なので、直の設定の後に GET した 3 軸の値に意味が無い（あるいは前の manual の値が残る）。次に「manual に同じ軸」を SET した時に直の設定が消えるかも決まらない。
  3. **軸の向き**: 「騒音 0〜100」が 100 = 静か（騒音を抑える）か 100 = 騒音を許すかが無い。「実行速度」「細粒度パワーゲート」も 0 と 100 のどちらが省電力か書いていない。driver ごとに逆に実装され得る。
  4. **非 manual の時の軸**: SET の struct の軸の欄を performance などの時に 0 にさせるか（EINVAL）、無視するか。GET は非 manual の時に軸を返すか。
  5. **部分の失敗**: SET の戻り（0 か EIO か EAGAIN）、partial の時に事象を出すか、受け手 0（QEMU）の時の戻り（第 3 版 review M8 の「EOPNOTSUPP か『受け手なし』」）、Dell の 1 秒に 1 回の制限（R2、EBUSY）で 2 回目の SET が partial になる事、WS301 の ETIMEDOUT（WS301 R8: 要求は箱に残り後で効く）で partial と表示したまま実は効く事。
  6. **後から登録された受け手**: 受け手の登録は attach の順（Dell は ACPI、WS301 の x86 の provider は p004、Pi 5 は platform）。user が SET した後に登録された受け手に今の strategy を渡すか。Q303-15 B（rc で渡す）を選んだ時の順にも効く。
  7. **受け手の口の契約**: 呼ぶ thread（ioctl の中で同期か、registry の thread か）、眠ってよいか、時間の上限（Dell の SMI は数十 ms〜数 s の報告、design §2.4:283）、strategy の mutex を持ったまま呼ぶか（M2）。WS301 の p004 はこの口に合わせて作るので、WS 間の契約として先に要る。
- 直し方の案: 第 5 版の §5 に (a) `enum`: `KERN_THERMAL_STRATEGY_{FIRMWARE（起動の時・未設定）, PERFORMANCE, BALANCED, POWERSAVE, MANUAL, CUSTOM（直の設定、読みだけ）}`、(b) 軸の 3 つの uint8 と向きの定義（例: speed 100 = 速さ優先、noise 100 = 静かさ優先、gating 100 = 省電力優先）と「非 manual の SET では 0、でなければ EINVAL」、(c) 受け手の結果の表（最大の数、名前 16 byte、errno、適用済みの flag）、(d) SET の戻りの規則（受け手 0 → EOPNOTSUPP、全部失敗 → 最初の errno、一部 → 0 と partial の flag と事象）、(e) 受け手の口の C の宣言と契約（呼ぶ thread、mutex を持たずに呼ぶ、上限の時間、登録の時に今の strategy を渡すか）を書く。UAPI はユーザーの決定（`plan/master-design-policy.md`）なので、この案を p003 の前に review に出す事を Phase の表に書く。

### M2. Q303-17 A（推し）は lock の順を逆にする（追記 Q303-17、第 3 版 §4.8 の lock、WS301 §4.9）

- 根拠: SET_STRATEGY は strategy の mutex（§4.8:399 の `thermal_profile_mutex` の後継、名前は未定）を取って受け手を呼ぶ形（§4.8:399「ioctl の set は mutex を取って provider の set → get → 写しの更新 → 事象」）。WS301 の受け手は `cpuperf_mutex` を取る（WS301 design.md:186「設定（ioctl・policy・熱の上限）は `cpuperf_mutex` で直列」）。Q303-17 A では WS301 の直の設定（`cpuperf_mutex` の下）が WS303 に「strategy を custom にせよ」と知らせる要がある。
- 起こる状況: thread A（SET_STRATEGY）: strategy の mutex → `cpuperf_mutex`。thread B（domain の直の設定）: `cpuperf_mutex` → strategy の mutex（custom の印と事象）。同時に来ると deadlock。p007 の passive（registry の thread → `kern_cpuperf_limit` → `cpuperf_mutex`）も registry の lock を持ったまま呼べば同じ形になる。
- 直し方の案: 「受け手は WS303 の lock を持たずに呼ぶ（strategy の mutex は SET の直列だけに使い、受け手を呼ぶ間は世代の番号で守る）」か「WS301 は custom の印を WS303 に同期で知らせず、WS303 が GET の時に WS301 の `kern_cpuperf_is_custom()` を lock なしで読む」のどちらかを lock の順として書く。§4.8 に全体の順（strategy の mutex → 受け手の lock → AML の interpreter の lock、逆は無い）と、WS301 から WS303 へ呼ぶ道が無い事を書く。

### M3. strategy の WS301 の受け手の持ち主が二重（V5 と WS301 の追記）

- 根拠: V5（design.md:51）「WS301 の policy を受け手として登録する小さな作業は WS303 p007 に入れる」。追記の解釈 2（24）「WS301 の CPU の provider は speed と strategy から EPP・min・max を」決める。WS301 の追記（WS301 design.md:15）「x86 の provider の対応の表（strategy と speed の軸 → EPP・min・max）は p004 で設計する」。AGENTS.md §4「修正してよい範囲: 割り当てられた WS・Phase の source」。
- 起こる状況: WS303 p007 の担当が WS301 の `src/kern/cpuperf.c`（WS301 の source）に受け手を書くか、WS301 p004 の担当が同じ物を書くかで二重に作るか、どちらも作らない。WS303 p007 が WS301 の source を書くと file の所有の規則に触れる（Q1 の調整が要る）。
- 直し方の案: 受け手の登録と対応の表は **WS301 p004**（x86 の provider）に置き、WS303 p007 は「passive（`kern_cpuperf_limit` を呼ぶ）」だけにする。WS303 の受け入れには「WS301 の受け手が登録されたら strategy が届く」を host 試験の模擬の受け手で確かめる形にする。V5 の文を直し、WS301 の次の版への申し送りを Q1 に頼む。

### M4. WS301 の記録・Phase の依存が WS303 の第 4 版と食い違う（依頼の (5)）

- 根拠:
  - WS301 design.md:65 の **Q301-7「(A, 推し) policy の名前を入れる」がまだ問いの一覧にある**。WS301 の追記（14）は「WS301 は独自の policy の名前と起動の parameter を持たない」。本文 §4.4（154〜155）は PERFORMANCE・BALANCED・POWERSAVE・CUSTOM と `cpuperf=`、既定 BALANCED のまま。
  - WS301 R12（38）「WS301 の ioctl は 22〜、事象の class は 0x100（mask 0x1ff）」、§5.2（227〜267）・§5.3（270〜272）は `/dev/system` のまま（追記の 13 行が取り下げ）。
  - WS301 §4.6（167）「今の使い手は無い（WS303 の推しは OS の passive cooling を入れない、WS303 Q303-6）」は WS303 の Q303-6「入れる」と食い違う。§5.1（224）は `uint8_t maximum` のまま（追記 15 行が千分率に改めると書くだけ）。
  - WS301 の Phase の表（WS301 design.md:44〜53）の p005（UAPI と `cpuperf` の道具）は WS303 p003（`/dev/thermal` の device と group 'T' の番号の割り）に依るが、依存に無い。WS303 の表にも「WS301 の ioctl の番号の範囲を p003 で取る」作業が無い（WS301 追記 13 行「番号は WS303 の p003 の案と一緒に決める」）。
  - V6（design.md:53）の下限 guaranteed: WS301 §4.8（179）は Highest・Guaranteed が実行時に変わる（HWP_STATUS の事象）。千分率の基準（lowest = 0、highest = 1000）と下限が動くと、passive の段の数え方が変わる。STEPS の provider（JH7110 の OPP、WS301 §4.3:150）で千分率をどう丸めるかも無い。
  - WS301 の 2026-10-27 の決定「設定できるのは root だけ」（WS301 ws.md:18）と、sessiond 経由の wheel の user が strategy で WS301 の要求を変える事の関係（第 3 版 review M3 (d)）は記録されていない。
  - master.md:140・142 の WS301・WS303 の行は「/dev/system」「p001（WS301 p002 の後）」のまま（写しは Q1 の物）。
- 起こる状況: Q1 が Q301-7 をユーザーに中継し、答え次第で WS301 が policy の名前を持ち WS303 の strategy と二重になる。WS301 p005 が WS303 p003 より先に始まり、番号を勝手に取る。
- 直し方の案: Q1 に (a) WS301 の Q301-7 を取り下げ（WS303 の Q303-11 の答えで決着）、(b) WS301 の Phase の表の p005 に「WS303 p003」の依存、(c) WS301 §4.4・§4.6・§5 の置き換えの印、(d) master.md の 2 行の写しの更新を頼む。WS303 の p003 の内容に「group 'T' の番号を WS303（1〜31）と WS301（32〜63）のように範囲で割る」を足す。V6 に「千分率は呼んだ時の lowest・highest に対する値、guaranteed が変われば下限も追う、STEPS は切り下げで近い step」を足す。

### M5. `/dev/thermal` に WS301 の ioctl を載せる時の build の形が無い（追記の解釈 5、R7）

- 根拠: `/dev/thermal` は `src/kern/thermal.c` が持ち全 platform に入る（T1、design.md:102、R7:157「weak の symbol は使わない」）。WS301 の枠組みは `CONFIG_CPU_PERF`（WS301 design.md:121、既定 n、WS301 R1:22）。
- 起こる状況: `thermal.c` の ioctl の振り分けが `kern_cpuperf_*` を呼ぶと、`CONFIG_CPU_PERF=n` の kernel（既定）は link できない。weak を使うと R7 に反し、`#if CONFIG_CPU_PERF` を thermal.c に置くと WS303 の file が WS301 の config に依る。
- 直し方の案: `thermal.c` に「ioctl の番号の範囲ごとの handler の登録の口」（`kern_thermal_ioctl_range_register(first, last, handler)`）を置き、WS301 の `cpuperf.c` が自分の範囲を登録する（無ければ ENOTTY か EOPNOTSUPP）。M3 の受け手の口と同じ向き（WS301 → WS303 の登録、呼びは WS303 → WS301）。p003 の内容と host 試験（登録の無い時の errno）に書く。

### M6. 非常の判定をする thread が provider の呼びで止まり得る（V3、V8、§4.3:351〜357）

- 根拠: §4.3（351、357）「registry の thread が 5 秒ごとに全部の `read` を呼ぶ」「周期の thread は registry に 1 本」。Dell の DDV の read は AML の評価（`Acquire(ECM1, 0xFFFF)`、R4・§4.10:418）。V3（49）の「写しが 30 秒以上古ければ非常として扱う」は、誰かが写しの古さを見る事が前提。非常の判定・passive の `kern_cpuperf_limit`（WS301 R8: 相手の CPU を 1 秒待つ）・boost の set（Dell は SMI）がどの thread で走るかは書かれていない。
- 起こる状況: 判定が registry の thread にあると、DDV の read が返らない・Dell の SMI が数秒止まる・WS301 の 1 秒の待ちで、判定そのものが止まる。写しの古さの規則は判定の thread が回っている事に依るので、止まった時に働かない。AML の止まり（第 3 版 review B2 の筋）は acpi-thermal の写しも止めるので、安全の道の全部が 1 つの止まりで消える。
- 直し方の案: 非常の判定（と 120 秒の timer）を **provider を呼ばない専用の軽い thread か timer の callout** に置き、写しの読みだけをする（spinlock の下）。provider の read・boost・passive は別の thread。判定の thread が自分の周期の遅れも見て log と事象を出す。host 試験に「provider の read が返らない時も判定が進む」を足す。

### M7. V8 の「唯一の読みの源は acpi-thermal の 5 秒の thread」が Pi 5 を外す（V8、Q303-13 A）

- 根拠: V8（design.md:55）「`acpi-thermal.c` の 5 秒の thread を唯一の読みの源にし、`hw.thermal` と `/dev/thermal` は同じ写しを出す」。Pi 5 の温度は `rpi5-fan.c` の自分の thread（既定 1 秒、`rpi5-fan.c:68`）が読み、`record_temperature`（1004〜1029）で写しに置き、`kern_thermal_register`（330）で hw.thermal に出す。Pi 5 の sensor は `THERMAL_KIND_ZONE`・`THERMAL_FLAG_CPU`（391〜393）。Q303-13 A（27）では非常の停止が実際に効くのは **Pi 5（DT の critical 110 °C）だけ**（5330 は firmware に任せる）。
- 起こる状況: V8 の字面のとおりに registry を作ると、Pi 5 の温度が新しい registry に入らず、Pi 5 で非常の停止も passive も動かない。読み失敗の時 Pi 5 は `time_ns = 0`・`THERMAL_HAVE_TEMPERATURE` を落とす（1013〜1018）ので、「時刻が違う 2 つの値」「30 秒の古さ」の数え方も ACPI と違う。
- 直し方の案: V8 を「sensor の源は provider ごと（ACPI は acpi-thermal の thread、Pi 5 は rpi5-fan の thread）。registry は源の写しを push で受け、時刻（CLOCK_MONOTONIC）と有効の flag を源が付ける」に直す。非常の判定が Pi 5 の 1 秒の周期の値も 5 秒の値も同じ規則（時刻が違う 2 つ、30 秒の古さ）で数える事、読み失敗（有効でない）の時の扱いを書く。p006 の受け入れに Pi 5 の非常の道（M10 の注入）を足す。

### M8. SIGUSR2 の handler の無い init は SIGUSR2 で死に、120 秒の直の切りは温度の条件で起きないことがある（V3、依頼の (3)）

- 根拠: init は SIGHUP・SIGINT・SIGTERM だけに handler を入れる（`userland/base/init/main.c:293〜296`、handler 2470〜2480）。SIGUSR2（`include/uapi/signal.h:36`）の既定の動作は終了（既定で無視するのは SIGCHLD・SIGCONT・SIGURG・SIGWINCH だけ、`src/kern/signal.c:1414〜1428`）。kernel に pid 1 を既定の終了の signal から守る code は見当たらない（`pid == 1` は `process.c:2705` の reaper の選びだけ）。`signal_send_process` は `struct process *` を取る（`signal.c:435`）ので kernel は `process_find_ref(1)`（`include/kern/process.h:207`）が要る。V3（48）「送ってから 120 秒たっても水準以上なら直に切る」。
- 起こる状況:
  1. 新しい kernel と古い init の組（kernel だけを入れ替える運用。この project の実機の配置は `/boot/vmunix` の入れ替えが多い）で非常に達すると、SIGUSR2 で **init が終わる**。sync も service の停止も無い。120 秒の後に温度が水準を下回っていれば（init が死んで負荷が減った時など）直の切りは起きず、**init の無い system が動き続ける**（login も停止の操作もできない）。
  2. init が handler を入れる前（`main.c:293` より前）に送っても同じ（起動の直後の窓、推測では稀）。
  3. init の起動の前半（`root_check`・`/sbin/mount`・`/sbin/swapon`、`main.c:299〜311`）は `action_requested` を見ないので、fsck が長いと 120 秒まで停止が始まらない（120 秒の直の切りが拾うので安全の面では可、記録だけ）。
- 直し方の案: (a) kernel は SIGUSR2 を送る前に pid 1 の disposition が handler（SIG_DFL・SIG_IGN でない）である事を確かめ、そうでなければ init を経ずに直の切りへ進む（signal.c に読みの口を足す、`signal_ignored_disposition_locked` と同じ lock の下）。(b) 120 秒の後の切りを温度だけで決めない（第 3 版 review の m9）: 停止を求めた後は「init が `KERN_SYSTEM_POWEROFF` に入らない」か「init が居ない」で切る。温度が下がった時に止めるなら hysteresis（例 critical − 10 °C を 60 秒）を書く。(c) init の SIGUSR2 の道では service ごとの猶予（`STOP_GRACE_MS` 5 秒 × 2、`main.c:69`）を縮める（init の変更の範囲を Q1 に確かめる）。

### M9. `_PTS` の「lock を 1 秒で取れた時だけ」は評価の長さを縛らない（V3、依頼の (3)）

- 根拠: `drv_acpi_poweroff` は `drv_acpi_evaluate(NULL, "\\_PTS", …)` を先に呼ぶ（`acpi-event.c:1041〜1048`）。interpreter の lock は `mutex_lock`（`acpi-kern.c:421〜426`）で、時間つきの口は無い。`mutex_trylock` は kernel にある（`include/kern/lock.h:118`）。AML の Sleep・Acquire の待ちは lock を離して poll する（`aml-sync.c:13〜16`）ので、止まっている評価があっても trylock は取れる事がある。5330 の `_PTS`（`dsdt.dsl:27540〜`）は `EV1` → `ECM9`（96364）→ `ECIN`（96311）で EC の読み書き・`GENS(0x2D)` の SMI・`EISC` を走らせ、`ECW1`（97010）・`ECR1` は Serialized の method。EC の 1 回の取引は `wait_status` の poll で上限がある（`acpi-ec.c:708〜735`）。
- 起こる状況: lock が取れても `_PTS` の評価が長く続くか、止まっている評価が持つ Serialized の method の mutex を待って返らない（推測: 5330 では非常の停止を firmware に任せるので直には当たらないが、thermal zone を持つ ACPI の機械一般に当たる）。V3 の「時間つきの lock の口が今の AML の層にあるかは p003 で確かめる」は、lock の取得だけを縛り、評価を縛らない。また `drv_acpi_poweroff` は `_PTS` の後に event の lock（`acpi-event.c:1056`）を取るので、`_PTS` を飛ばす版を `src/drivers/acpi` に作る要がある（`drv_acpi_poweroff` の引数か別の関数）。
- 直し方の案: 非常の直の切りでは **`_PTS` を評価しない**（ACPI の S5 は SLP_TYP・SLP_EN の書きで足りる。`_PTS` は firmware への知らせで、非常の時の電源断を優先する）か、`_PTS` を別の kernel の thread で評価し 1 秒で見切って SLP_EN を書く。どちらにしても `drv_acpi_poweroff_emergency()`（`_PTS` 無し、event の lock は取る）を `src/drivers/acpi` に足すと書き、`kern_platform_poweroff` の platform ごとの形（pcat は ACPI、rpi5 は `hal_poweroff` で `_PTS` 無し、`rpi5.c:303〜312`）を V3 に書く。init の `KERN_SYSTEM_POWEROFF` が同時に `system_shutdown_prepare` の中に居る時（`shutdown.c:65〜72`）に kernel が SLP_EN を書く事を許すか（PCI の shutdown と並ぶ）も書く。

### M10. 非常の停止の QEMU の試験が init の道と直の切りの道を見分けられない（V9、依頼の (6)）

- 根拠: V9（design.md:57）「注入 → init の poweroff → QEMU の終了を QMP の `query-status` と process の終了で判定」「直の切りの道は init を止めた試験の image で同じく」。どちらの道も最後は QEMU の終了で、QEMU が終わった後は `query-status` を聞けない。AGENTS.md §9 は console・serial の log で判定しない。
- 起こる状況: init の道が壊れて 120 秒の直の切りで落ちても、試験は PASS する（安全の道の半分を見ない）。「init を止めた試験の image」の作り方（SIGUSR2 を無視する init、M8 の古い init と同じ形か）も無い。
- 直し方の案: 判定を 2 つの観測にする: (a) 注入から QEMU の終了までの時間（host の monotonic で測る。init の道は数〜十数秒、直の切りは 120 秒前後）と QMP の `SHUTDOWN` 事象の有無、(b) 再起動の後に ssh で読む guest の file（init の停止の記録、例えば init の timeline の file か、試験の service が SIGTERM で書く印）で sync まで届いたかを見る。init の道を止める試験は「SIGUSR2 を ignore する試験の service ではなく、試験の build の init の option」など具体に書く。注入する sensor の kind（thermal zone として扱うか）と critical の値を注入の ioctl の欄に入れる（Q303-13 A の「thermal zone と DT だけ」に乗せるため）。`CONFIG_THERMAL_TEST_INJECT` は `config/kernel-options.list` と `Makefile` に `CONFIG_INPUT_TEST_INJECT`（`kernel-options.list:4`、`Makefile:414・947〜948`）と同じ形で足すと書く。

### M11. p003 が 8 LW に収まらない（第 4 版・追記の Phase の差分、依頼の Phase の分け方）

- 根拠: p003 の内容: registry、`/dev/thermal` の cdev と事象、`include/uapi/thermal.h` の案とユーザーの review、水準と非常の判定（M6 の専用の thread）、init の SIGUSR2（`userland/base/init`、別の program）、直の切りと `_PTS` 無しの poweroff（M9、`src/drivers/acpi`）、試験の注入の option、`/sbin/thermal`、strategy の 4 つと manual の 3 軸と受け手の口（M1）、WS301 の ioctl の範囲の登録の口（M5）、`hw.thermal` を同じ registry から、12 の `vmunix.mk`、host 試験、QEMU の非常の停止の 2 本の試験（M10）。第 3 版 review の m15 は init と注入を足す前で 9〜10 LW と見た。
- 起こる状況: p003 が 1 Queue に収まらず途中で uncleared になり、UAPI のユーザーの review の待ちと実装が同じ Phase に入って止まる。
- 直し方の案: p003 を (p003a) registry・UAPI・cdev・`/sbin/thermal`・strategy と受け手の口・host 試験（UAPI の review の後に着手）と (p003b) 非常の停止（判定の thread、init、直の切り、注入、QEMU の 2 本）に分ける。p003b は p003a の後、p006（Pi 5）の非常の受け入れは p003b の後。合計の LW を数え直す。

## Minor

- **m1 版の行**: design.md:5「『第 4 版の改訂』が最優先」を「追記が最優先、次に第 4 版の改訂」に直す。10 行の「推しに優先する」は V1〜V10 の本体にも優先する所（V4・V8 の一部）を名指す。6 行の基点の worktree（p5-design-b）は、今の tree（p5-design-c、d94ceac00）で引用の行が変わっていない事を書き添える（このレビューで確かめた）。
- **m2 ws.md の写し**: ws.md:15 の本体の指定（第 3 版の表）、ws.md:27 の p001「Q303-9〜15 待ち」（答えが来たのは 9〜14、残りは 15〜17）、ws.md:39 の「Q303-9〜15 の答えを待つ」「合計 約 30 LW」（表の和は 32）、ws.md:15 の design-v3-review.md の link が無い。p008 の依存に p009 があるのは ws.md だけで design.md に無い。
- **m3 事象の種類**: T1（103）の事象の「温度の水準の跨ぎ（passive・hot・critical）」から hot を外す（V2・V10 に合わせる）。第 3 版 review の m4（passive の跨ぎに hysteresis を付けないと ring の 64 個（`system.h:315〜318・338`、`KERN_SYSTEM_EVENT_QUEUE`）を埋め「非常の停止の開始」を捨て得る）は、p003 の入力の一覧に回されたが安全の道に効くので設計に書く（非常の開始の事象は溢れで捨てない、または停止の状態を GET で読める）。
- **m4 T5 の段の単位**: 「HWP の単位で highest の 10%」を V6 の千分率（100‰ ずつ、下限 guaranteed）に書き直す。
- **m5 「後で外す」の読み**: Q303-9 B の原文の選択肢は「hw.thermal は後で外す」（design.md:135）。追記は同じ WS の p009 で外すと読んだ。「後で」が別の WS・別の時の意味でないかは解釈として明記する（AGENTS.md §8: 解釈と原文を分ける）。原文が Q1 の要約の形（12 行）なので、Q1 に原文の有無を確かめる。
- **m6 p009 の読み手と書き手の全部**（依頼の (4)、grep で確かめた）: 読み手は `userland/base/sysctl/main.c`（32・99 の名前の一覧・222〜223・697〜745 `show_thermal`）と `userland/desktop/libkeiland-backend-zedbsd/monitor-zedbsd.c`（23〜24 の comment・643〜681）の 2 つだけ。書き手と定義: `src/kern/sysctl.c`（55 の MIB、493、registry）、`include/kern/sysctl.h:82〜99`（`kern_thermal_read_t`・`kern_thermal_register`）、`include/uapi/sysctl.h`（252 `HW_THERMAL 9`、`thermal_header`・`thermal_entry`・`THERMAL_*`）、`src/drivers/acpi/acpi-thermal.c:112`、`src/drivers/platform/rpi5/rpi5-fan.c:330`、comment の `include/drivers/acpi/acpi.h:519`・`src/drivers/acpi/acpi-kern.c:246`・`src/drivers/platform/rpi5/rpi5-fan.h:16`・`Makefile:403`。試験・道具・文書（`plan/tools.md`・`tests/scenarios`・`docs/`・`plan/ws134/tests/`）からの参照は無い（`plan/agents/T1/requests.md:551` の T1-482 は済みの記録）。p009 の内容に、`HW_THERMAL 9` の番号を再利用しない事（欠番の comment）、monitor の Keiland の file の所有（userland/desktop、Keiland の WS との調整は Q1）を書く。
- **m7 cdev の作り方**（依頼の (4)）: `/dev/thermal` の device の番号が決まっていない。使用中は 0x00010000（console）・0x00010001（graphics・ptmx）・0x00010002（system）・0x00010003〜0x00010007（null〜urandom）・0x00010010〜（console の端末）・0x00010020（tty）と driver の定数。登録の呼び場所は `src/kern/vfs.c:466` の `drv_system_device_register` の並び。devfs の mode は名前で決まり既定 0666（`devfs.c:589〜601`）なので、SET・注入の root の判定は ioctl の中で `cred_is_superuser`（`system-device.c:333〜337` の形）。注入は `/dev/input-inject`（0600、`devfs.c:596〜597`）のように別の node にする案も比べる（0666 の node に試験の ioctl を置くと、試験の build で一般の user が EPERM を返される口を常に持つ）。
- **m8 事象の仕組みの使い回し**: 追記の解釈 5（28）「事象の record も同じ」は `struct system_event` の形（`system.h:356〜368`）だけか、`system-event.c` の ring と subscriber（`include/kern/system-event.h:24〜29`、class の bit は 1 つの名前空間）も使い回すかを書く。使い回すなら `/dev/thermal` の class の bit が `/dev/system` の `KERN_SYSTEM_EVENT_CLASSES`（`system.h:329`）とぶつからない割り方が要る。
- **m9 jump の規則**: V1（44）「前の読みから 20 °C 以上跳ねた値は 1 回捨てる」の「前」が最後に採った値か捨てた値か、読み失敗（Pi 5 は有効でない値）の後の扱いが無い。最後に採った値と比べると本当の段差を永く捨て続ける。「127 °C 以上は定めない」が trip の値の規則か読みの値の規則かも書く。
- **m10 S0 idle と非常の停止**（第 3 版 review の m7、未反映）: 停止を求めた後の `KERN_SYSTEM_SLEEP` を EBUSY にする事、suspend の間に 120 秒の timer が進む・止まる扱い。thermal zone を持つ amd64 の機械で効く。
- **m11 120 秒の見込み**: init の service は最大 32（`main.c:38`）、1 service で最大 10 秒（`STOP_GRACE_MS` 5 秒 × 2）、他の process に 10 秒、最後の action の再試行は 12 回 × 5 秒の後 60 秒ごと（`main.c:86〜88`、1903〜1925）。最悪は 120 秒を超える（base の service は 7 つ、`userland/base/init/services`）。「見込む」の根拠に数を書き、超えた時は sync の前でも切る事をユーザーの「電源を落とす」の解釈として記録する。
- **m12 Q303-16 A の範囲**: 「PCIe の ASPM・NVMe の APST（WS300）、GPU の power gating、CPU の C-state の上限」は WS300（planning）・GPU の driver・今は `hlt` だけの idle（WS301 ws.md:42 は C-state の governor を範囲外）に受け手を作る事になり、WS303 の Phase の表に作業が無い。(C)「軸は UAPI に持ち保存して示すだけ、受け手は各 WS が後で足す」を選択肢に足し、推しにするか比べる。
- **m13 Dell の Quiet と「下回らせない」**: 追記の解釈 2（25）「どの軸でも fan を必要な回転数より下にはしない、noise は firmware の mode の選びにだけ効く」は、noise で Dell の Quiet（firmware が fan を遅くする）を選ぶ事を許す読み。Pi 5 は V4 で「遅くしない」にしたので、firmware の機械と OS の機械で扱いが違う。原文 Q303-5「ストラテジだけファームウェアに伝える」で支えられる読みだが、解釈として明記する。
- **m14 Q303-15 B と R8**: B（前回の strategy を rc で渡す）は起動ごとに Dell の set（BIOS の設定の書き、SMI）を起こし、R8「起動で firmware に書かない」と食い違う。B の説明に「同じ値なら firmware に渡さない」を書く。
- **m15 出典**（依頼の (7)）: Q303-12 の「GPL の値を出典つきで」は guardrail（`plan/guardrail.md:123`「GPL の file の定数・表もデータとして、block ごとに引用元を示して使ってよい。code・comment・構造は写さない」、範囲は全部の WS）に合う。前例は `rpi5-fan.c:44〜47`（Raspberry Pi Linux の GPL の file の値を「Values referenced:」で引用）。ただし SMM の fan の state は「値」だけでなく呼び出しの手順（どの register に何を入れ、何が失敗の印か）を含む。手順の参照（「Procedure referenced:」）を GPL の file から取る事を guardrail が全 WS に明示しているのは書式（122）までで、明示の許しは WS039 の節（125）だけ（推測: 全 WS に当たると読めるが字面は WS039）。Q1 に確かめるか、手順は公開の `dell-smm-hwmon` の文書（design §2.4:282）と AML（`WMDM` の EAX〜EDX、design §2.1:238）から組み、GPL の file からは値だけを取ると書く。§9（628）の「GPL の source は読まない」を直す。
- **m16 5330 の受け入れの手順**: R10（162）・§6.3（590）の quiet → performance の比べ、§6.3 の profile の provider "dell-wmi"・available 4 つを、strategy の抽象（performance・powersave・manual の noise）と p004 で決める Dell の対応の表で書き直す。V9 の boost の試験に第 3 版 review M7 の安全の止め（TCPU が一定を超えたら boost を最大か 0 に戻す）を足す。

## 確かめて正しかった主張

| 主張 | 根拠 |
| --- | --- |
| init は今 SIGINT で reboot、SIGTERM で halt、poweroff の signal は無い（V3） | `userland/base/init/main.c:293〜296`、2470〜2480 |
| init の service の停止は SIGTERM の後 5 秒・SIGKILL の後 5 秒（V3 の「5+5 秒」） | `main.c:64〜70`、1951〜1966、`STOP_GRACE_MS` |
| SIGUSR2 は 17 で、今どこも使っていない（init の handler に無い） | `include/uapi/signal.h:36`、`grep SIGUSR userland/base/init/main.c` が 0 |
| kernel から process に signal を送る関数がある | `src/kern/signal.c:435` `signal_send_process(struct process *, int)`、pid から引くのは `process_find_ref`（`include/kern/process.h:207`） |
| `KERN_SYSTEM_POWEROFF` は pid 1 だけで、`system_shutdown_prepare` の後に platform の poweroff | `system-device.c:552〜568` |
| `system_shutdown_prepare` は先の呼び手を待つ（直の切りが通さない理由） | `src/kern/shutdown.c:65〜72` |
| pcat の poweroff は `_PTS(5)` を評価してから SLP_TYP・SLP_EN を書く | `acpi-event.c:1011〜1105`、`pcat.c:660〜676` |
| 時間つきの lock の口は AML の層に無いが、`mutex_trylock` は kernel にある | `acpi-kern.c:421〜426`（`mutex_lock`）、`include/kern/lock.h:118` |
| ioctl の group 'T' は空き | `include/`・`src/`・`userland/`（packages を除く）の `'T'` の ioctl と `*_IOC_GROUP 'T'` が 0。UAPI の group は 'A' 'B' 'E' 'F' 'G' 'H' 'L' 'R' 'S' 'b' 'c' 'f' 'g' 'h' 's' 't'（termios は 't'、`termios.h:135`） |
| Keiland の monitor は `sizeof(*entry)` で進む（V8 の「thermal_entry を変えない」の理由） | `monitor-zedbsd.c:664・672` |
| hw.thermal の読み手は `sysctl` の道具と Keiland の monitor の 2 つだけ | m6 の grep |
| 5330 の TCPU は `_PSV` = `_CRT` = `_HOT` = TJMX 110 °C（V1） | `out-ssdt3.dsl:1390、1466〜1483` |
| DIAG の `_WDG` は Method で Name の buffer を返すだけ（V7） | `dsdt.dsl:94054〜94071`（XWDG は DM（method）と MO の 2 つの GUID） |
| Pi 5 の sensor は thermal zone の kind で critical は 110 °C（Q303-13 A の Pi 5） | `rpi5-fan.c:68〜70、391〜393、1019〜1027` |
| 試験の build だけの注入の option の前例がある（V9 の `CONFIG_THERMAL_TEST_INJECT` の形） | `config/kernel-options.list:4`、`Makefile:414、947〜948` |
| GPL の file の値は block ごとの出典で全 WS に使える（Q303-12） | `plan/guardrail.md:123`、前例 `rpi5-fan.c:44〜47` |
| `/dev/acpi` の件は BUG-351 として Bug Board にある（第 3 版 review の m11 は解消） | `plan/bugs/BUG-351.md`、`plan/known-bugs.md:221` |
| ws.md の Phase の表（p001〜p009）の LW の和は 32 で、追記の「合計 約 32 LW」と合う | `ws.md:28〜35`、design.md:38 |
| 受け入れの判定は ssh・host 試験・QMP で、console の log を使っていない（AGENTS.md §9） | V9、R10、ws.md の表。ただし M10 のとおり QEMU の非常の試験は道を見分けない |
