<!-- awesome-plan project=zedbsd record=ws303-design-review -->

# WS303 設計の敵対的レビュー（design.md 第 1 版）

日付: 2026-10-29（host の `date`）。担当: design-reviewer（P5 の依頼）。対象: [design.md](design.md)（§0〜§10、Phase の表、Q303-1〜7）と [ws.md](ws.md)、関係の [WS301 ws.md](../ws301/ws.md)。

照合した物: worktree `p5-design-b`（HEAD 63386d8a4、design.md は未 commit）。読んだ source: `src/drivers/acpi/{acpi-kern.c,acpi-ec.c,acpi-power.c,acpi-thermal.c,acpi-dev.c,acpi-text.c,aml-sync.c,aml-thread.c,aml-eval.c,aml-define.c}`、
`include/drivers/acpi/acpi.h`、`src/drivers/platform/rpi5/rpi5-fan.c`、`src/drivers/generic/system-device.c`、`include/uapi/system.h`・`sysctl.h`・`mountinfo.h`、`src/kern/sysctl.c`・`system-event.c`・`devfs.c`、
`include/kern/lock.h`、`platform/*/vmunix.mk`、`plan/ws049/tests/{aml-host.c,aml-host-hardware.c,Makefile,check-latitude5330.sh,latitude5330/}`、`plan/tools/hw5330/5330-run.sh`、`userland/tests/systemevents/main.c`、`plan/guardrail.md`、`plan/coding-style.md`。
firmware の事実: scratchpad の `ws303-acpi/dsdt.dsl`（`plan/ws049/tests/latitude5330/dsdt.dat` と同じ物であることを `cmp` で確かめた）・`out-ssdt3.dsl`・`out-ssdt9.dsl`、他の SSDT は `.dat` を byte 列で検索した。
観測: `ws303-5330/survey.txt`・`survey2.txt`。`ws303-fable/` は ACPI 6.5 の 11 章の頁だけ。GPL・LGPL の source は読んでいない。何も build・実行していない（試験は未実施）。

重大度: **blocker**（このまま実装すると目的を壊す・firmware の状態を壊しうる）、**major**（実装の前に設計を直す要がある）、**minor**（実装の中で直せる・文書の整合）。「推測」は source・表で確かめられなかった事。

## 要点

- **blocker 1 件**: (B1) thermal mode の「今の mode」を読む get の形が分からないまま、`input0 = 0` の SMI を「試す」形を起動ごと・60 秒ごとに、DMI の限定なしに全ての Dell の機械で走らせる設計。しかも `input0 = 0` は §2.4 で「使わない」とした GPL の言い換えの情報そのもの。AML にはもっと安全な読み（`\ODV0`、SMI 無し、input1 の bit と 1 対 1）がある。
- **major 13 件**: WMI の attach が `_WDG` の Method を評価して起動ごとに SMI を起こす（AMW2・AMW5）、DDV の fan の読みは ECM1 を取らず `Sleep` で interpreter の lock を離す・EC が `_REG` 前なら EC の読みが全部 SMI になる（§4.9 の「DDV は SMI を使わない」が条件つき）、`SMBF` が黙って SMI を飛ばす道と output の判定、延長 0x8000 の道は二乗の写しで成り立たない、WMI の事象の層が Notify の handler の契約に反する、`/dev/acpi` が 0666 で既に誰でも SMI・EC の書きを起こせる、registry が「全 platform に入る」の誤り、「既定の profile の適用」と「起動で書かない」の矛盾、host の harness の能力と所有、block の数の期待（30 でなく 31）、5330 の試験の判定、出典の扱い、ユーザーの決定・確認済みの書き過ぎ。
- **確かめて正しかった主張**も多い（末尾の表）。WMBA の class・select・input の offset と `ODV0` の Case、`WQAA` の記述子、DDV の 0x12・0x20 の形、Debian の観測値、UAPI の 88・32 byte、ioctl 22〜24 の空き、HAL の API の変更が要らないこと、など。

## Blocker

### B1. 今の mode の get を「input0 = 0 を試す」SMI にしている（§4.2 の get、§4.6、§4.9、§2.4 の最後の行、Q303-7）

- 根拠:
  - `dsdt.dsl` 91256〜91300（`WMBA`）: AML が意味を持つのは class 0x11・select 0x13・**input0 = 1** の時だけ（input1 = 1・2・4・8 → `ODV0` = 0・1・2・3、`Notify(IETM, 0x88)`）。input0 が 1 以外の時の意味は AML に無く、SMI の handler（firmware の中）が決める。
  - design §2.4 の表の最後の行:「Dell の mode の bit と名前の対応、get の input0 = 0 の意味 — GPL の comment の言い換え（検索の要約）— **使わない**」。一方 §4.2 の get は「input0 = 0 で呼ぶ形を試す」。`0 = get` は AML から出ない（AML が言うのは「1 は set」だけ）ので、使わないと決めた情報をそのまま使っている。
  - §4.6 の状態遷移は attach の時に get を必須にし（失敗で ABSENT）、§4.2・§4.6 は 60 秒ごとに get を回す。§4.2 の「見分け」は DMI の allow list を持たず、GUID と記述子だけで attach する（Dell の全機種で同じ GUID が出る）。
  - §4.9: SMI の間は全 CPU が止まる。Fable の調べ（§2.4）では Dell の SMM の呼び出しで 2〜4 s の停止の報告がある。
- 起こる状況: (1) 5330 で input0 = 0 が get でなかった場合（例えば「既定へ戻す」や別の select の副作用。推測）、起動のたびに BIOS の設定を書き換えうる。(2) 5330 以外の Dell（同じ GUID の別の世代）で、意味の分からない SMI が起動ごとと 60 秒ごとに走る。(3) get の答えの位置も status も不明（§10 の 1）なので、p004 で「分からない」と決まれば provider は ABSENT になり、WS の到達目標（mode の選択）が届かない。(4) 60 秒ごとの SMI は、mode が実行時に firmware の側から変わる道が 5330 の AML に無い（§4.9 自身が「事象は見当たらない」と書く）のに、定期の停止を足すだけ。
- 直し方の案:
  1. 今の mode の読みを `\ODV0`（GNVS、`dsdt.dsl` 1236 の `OperationRegion (GNVS, SystemMemory, 0x6150C000, …)`、SMI を起こさない）にする。`WMBA` の Case が input1 の bit と `ODV0` の値の 1 対 1 を AML の事実として与えるので、bit の空間で今の mode が分かる。§10 の 7（BIOS が起動で `ODV0` を埋めるか）を p004 の最初の確認にし、埋めないなら provider は「今の mode は不明（set の後だけ分かる）」で登録する（ABSENT にしない）。
  2. SMI の get を使うなら、その意味を Dell の公開の文書か、guardrail の「GPL の値は引用で可」（`plan/guardrail.md` 123）で出典を block ごとに明示して決める。検索の要約の言い換えは版・path・行が言えないので引用にならない。p004 で実機の確認が済むまで、get の SMI は DMI（SKU 0B02 など）の allow list と boot の option の両方で閉じる。
  3. 60 秒ごとの get を止める（set の後と resume の後だけ読む）。§4.6 の「周期の get で current が変わった」の遷移と §4.9 の「60 秒ごと」を消す。
  4. §6.1 に「attach と周期の処理が SMI の port（0xB2）を書かない」を harness の SMI の模擬で数える試験を足す（今の計画ではこの誤りは見つからない）。

## Major

### M1. WMI の attach が `_WDG` の Method を評価し、起動ごとに SMI を起こす（§4.1 の attach、§4.9、§6.1、p002 の受け入れ）

- 根拠: 5330 の PNP0C14 9 つのうち `_WDG` が Method なのは 5 つ（`dsdt.dsl` 91818 AMW2、93082 AMW5、93763 AMW4、94066 DIAG、100962 ECMO）。design §4.1 は「AMW2 は Method」とだけ書く。AMW2 の `_WDG` は `DB01()` → `DM05(One, 0x40)`、AMW5 の `_WDG` は `BBM1()` → `DM05(0x06, 0x08)`、`DM05` は `GENS (0x21, WDBF, SizeOf (WDBF))`（91689〜91705）で **SMI を起こす**。AMW2 はさらに `DB00` で引数から計算した番地の 0x100 byte の SystemMemory の region を作る（91830〜）。
- 起こる状況: WS303 の WMI の層が入ると、全ての Dell の機械で起動ごとに BIOS の属性の SMI（class 0x21）が 2 回以上走る。今の kernel は `_WDG` を評価しない（WMI の driver が無い、§1）ので、これは新しい SMI である。§4.9 の SMI の一覧（profile の set・get だけ）にも §6 の試験にも無い。p002 の受け入れは host 試験だけで、実機で WMI の attach が初めて走るのは p004（起動が止まる・遅れる恐れを最後に知る）。
- 直し方の案: (a) `_WDG` が Name の device だけを attach の時に解析し、Method の `_WDG` は「その GUID を引かれた時に初めて評価する」遅延にする（5330 で要る AMW0・AMWV は Name、91150・94628）。(b) または provider が要る GUID を持つ device を `_UID`（"ADDV" など）で先に絞る案は機種依存なので推さない。(c) p002 の受け入れに 5330 の素の起動の回帰（`plan/tools/hw5330/5330-run.sh`）を 1 回足す。§4.9 に attach の SMI の有無を書く。

### M2. DDV の fan の読みの AML の読みが違い、失敗の分析が別の物を見ている（§2.1 の表の WMDV の行、§4.2、§4.9、§4.10 の 4 行目、§10 の 5）

- 根拠:
  - `dsdt.dsl` 94873〜94921（method 0x20）と 94923〜94980（0x22）は **`Acquire (ECM1, …)` を取らない**。ECM1 を取るのは 1〜0x11（例 94646〜94651）。§2.1 の「EC は AML の mutex `ECM1` の下で読む」は 0x20・0x22 には誤り。
  - 0x20 は `ECWB (0x31, Zero)` の後に `Sleep (0x03)`。zedBSD の `Sleep` は interpreter の lock を離す（`aml-thread.c` 138〜157 `drv_acpi_sleep`、`aml-sync.c` 13〜16）。§1 の「全ての AML は 1 つの interpreter の lock で直列」は Sleep・Acquire・Wait の間は成り立たない。
  - EC の取引は `ECWB(0x31)`・`ECRB(0x6F)` の 2 と、fan ごとに `ECRB(0x6F)`・`ECWB(0x39)`・`ECRB(0x70)`・`ECRB(0x71)` の 4 で **2 + 4n**（§4.2 は 2 + 3n）。
  - `ECRB`・`ECWB` は `ECR1`・`ECW1`（96389・97010）を通り、`\ECRD == 0`（EC の `_REG(3, 1)` の前）なら `EISC` → `GENS (0x08, …)`（97597〜97607）で **EC の 1 byte ごとに SMI** になる。
- 起こる状況: (1) EC の driver が attach しない・`_REG` が走らない時（`acpi-kern.c` 236〜239 は EC の attach の失敗を log するだけで進む）、5 秒ごとの fan の読みが 6 回の SMI になり、§4.9 の「DDV の fan の読みは SMI を使わない」が崩れる。(2) §4.10 の「Acquire の timeout 0xFFFF で AML が返らない恐れ」と §10 の 5 の試験は 0x20 では起きない事を確かめるだけで、本当の失敗（EC の取引の `ETIMEDOUT`、`acpi-ec.c` 707〜736、で method が途中で止まる）を見ない。(3) Sleep の間に他の AML が走る事への備えが無い（5330 の表で EC 0x31・0x39・0x6F〜0x71 を使うのは WMDV だけであることは確かめた。`/dev/acpi` からの WMDV の評価は引数が無く 0x20 に入らない。よって今の表では実害は無い見込み）。
- 直し方の案: §2.1・§4.2 の記述を AML のとおりに直す。provider は attach の時と読みの前に `\ECRD`（Name、97594）が 1 である事を確かめ、0 なら fan の読みを止める（SMI に落ちない）。§4.10 の行を「EC の取引の失敗で WMDV が error を返す → STALE」に替え、§10 の 5 は harness の EC の模擬に `ETIMEDOUT` を入れて STALE になる事を見る試験にする。§1 の「直列」に「Sleep・Acquire・Wait の間は lock を離す」を書き足す。

### M3. `SMBF` が黙って SMI を飛ばす道があり、output の成否を判定できない（§4.2 の呼び出しの buffer・set、§4.10）

- 根拠: `dsdt.dsl` 91087〜91129: `If ((Arg2 > 0x8000)) Return (Arg1)`、`If ((SizeOf (Arg1) < Arg2)) Return (Arg1)` — buffer が `延長の長さ + 0x2C` より短い時、SMI を起こさず **入力をそのまま返す**。`WMBA` は `Local1 > 0x8000` の時 `Local0` を初期化しないまま `Return (Local0)`（91297〜91300）。また `WMBA` は set の時 SMI の **前に** `ODV0` を書き `Notify` する（91266〜91290）。
- 起こる状況: (1) 実装が延長の長さと buffer の大きさを食い違えると、output は入力の 0 のまま返る。status の値が不明（§10 の 1）なので、0 を「成功」と読む実装は SMI が起きていないのに set の成功を報告しうる。(2) set の SMI を firmware が拒んでも（BIOS の admin password など、推測）`ODV0` は新しい値のまま残り、B1 の案 1 の読みがずれる。
- 直し方の案: output の 4 dword を呼ぶ前に sentinel（例 0xFFFFFFFF）で埋め、返りで sentinel が残っていれば「SMI が起きなかった」として EIO にする。buffer の大きさを `延長の長さ + 0x2C` に固定する関数を 1 つにする（host 試験で 2 つの値を食い違えた時に EIO になる事を見る）。set の失敗の後は `ODV0` を信じない（provider の写しを「不明」にする）、と §4.6 に書く。

### M4. 延長 0x8000 の道は二乗の写しで、50 ms に収まる見込みが無い（§4.2、§4.10 の 6 行目、§10 の 2、§6.1）

- 根拠: `SMBF` の写し戻しは `Arg1 = SNVG (Arg1, Local0)` を dword ごとに繰り返す（91121〜91125）。zedBSD は local・arg への store で data を写す（`aml-eval.c` 1674〜1692 `store_local`: 「A local gets its own copy of data」、arg も同じ `store_argument` 849〜872）。よって 1 回の写し戻しは 8192 回 × 32 KiB の写し（約 256 MiB の memcpy と 8192 回の 32 KiB の確保）。各 `SNVP`・`SNVG` は `Field (…, ByteAcc, Lock, …)` で、byte ごとに FACS の Global Lock を取る（`Lock` の規則、zedBSD は `start_global_lock` で firmware と共有）。design §4.2 は「約 16000 回の region の作成」だけを数えている。
- 起こる状況: 延長 0 を firmware が受けなかった時の予備の道が、interpreter の lock を長く（推測: 数百 ms〜秒）持ち、EC の query・電源の事象・温度の thread を止める。§7 の p004 の受け入れ（set 1 回 50 ms 以下）は満たせない。
- 直し方の案: 予備の道を設計から外し、「延長 0 を受けない firmware では profile の provider を登録しない」とする。残すなら p002 の host 試験で時間を測る前に「二乗の写し」を設計に書き、kernel の interpreter の store の写しを避ける最適化（同じ object への store の自己代入を写さない等）を別の Phase か Bug として切り出す（AML の意味を変えない範囲で）。

### M5. WMI の事象の層が Notify の handler の契約に反し、使い手も無い（§4.1 の `drv_acpi_wmi_event_install`、§4.7、§4.9、§5.1）

- 根拠: `aml-sync.c` 62〜67:「The handler runs in the thread that runs the AML, with the interpreter held: it must not evaluate AML」。design §4.1 は「その device の Notify のうち notify の番号の物で `_WED(番号)` を評価して handler に渡す」— handler の中で AML を評価する形。power の device は handler で印を立て、別の thread が評価する（`acpi-power.c` 130〜165）。design §4.9 はこれを「同じ道」と書くが、評価の thread の段が抜けている。5330 の `_WED`（94532〜）は `WVCU()` で event の buffer を消費する。
- 起こる状況: 実装すると契約違反（interpreter の入れ子で動いても、`_WED` の中の EC の読み・Sleep が Notify を出した AML の途中に割り込む）。この WS の中に使い手は無い（§4.7・§4.9 が自分で「今の使い手は無い」と書く）ので、試験もされない code になる。
- 直し方の案: 事象の口をこの WS から外す（後で hotkey 等の WS が要る時に、評価の thread を持つ形で作る）。残すなら「handler は印と notify の番号を記録し、WMI の thread が `_WED` を評価して provider の handler を lock の外で呼ぶ」と書き、§4.8 の lock の順に WMI の thread を足す。

### M6. `/dev/acpi` は 0666 で、既に誰でも引数の無い（足りない）method を評価して SMI と EC の書きを起こせる（§4.1 の最後の行、§4.13 の「WMI の生の method を user に出す」）

- 根拠: `src/kern/devfs.c` 589〜601: 名前の特別の扱いが無い device は 0666。`acpi-dev.c` 184〜228: write した path を `drv_acpi_text_evaluate` が評価、`acpi-text.c` 150 は引数 0 で `drv_acpi_evaluate`。`aml-eval.c` 913〜920:「a missing one stays missing」— 引数の要る method も足りないまま走る。5330 の表では、`\_SB.AMW0.WQDS`（91204〜、Arg0 を使わず EC 0x6A に `ECCR = Zero` を書く）、`\_SB.VDGS._STA`（94479〜、`GENS (0x22, …)` で SMI）、`\_SB.AMW2._WDG`（M1、SMI）が一般の user から評価できる（実機では未確認。code の読みでの推測）。
- 起こる状況: design の安全の前提「任意の SMI を user に出さない」は WMI の層の外で既に破れている。WS303 は p004 の試験で `/dev/acpi` を使う（§6.3 の 3）ので、この口に依存を増やす。
- 直し方の案: WS303 の範囲の外なので Q1 に Bug として報告する（`/dev/acpi` の write を root だけにする、か評価を method の引数の数が合う物と Name・Field だけに限る）。design §4.1・§4.13 には「`/dev/acpi` の評価の口は今は誰でも使える（Bug で扱う）」と事実を書き、WS303 の安全の主張をその Bug に依存させる。

### M7. registry は「`src/kern/` なので全 platform に入る」の誤り（§3 の置き場所の理由、§4.11 の `/dev/system`、p003 の受け入れ）

- 根拠: kernel の source は platform ごとの `vmunix.mk` に file を名指しで並べる（`system-event.c` を並べる `vmunix.mk` は 12: amd64・arm64・armv7・riscv32・riscv64・mips32-malta・mips64-malta・ppc32-mac99・ppc64-pseries・sparcv9・m68k-virt・x68k）。`wildcard` で入るのは amd64 の `src/drivers/acpi/*.c`（`platform/amd64/vmunix.mk` 356〜358）だけ。design は同じ節で「registry は全 platform に入る」と「system-device.c は weak の symbol で呼ぶ」を並べており、前者が真なら weak は要らず、偽なら全 platform の mk を直す要がある。`rpi5-fan.c` が `kern_fan_register` を呼ぶ（§4.5）ので arm64 では強い参照になる。
- 起こる状況: p003 の「全 platform の build warning 0」が、12 の mk の変更か、weak の参照の組み合わせのどちらで成り立つかが決まっていない。片方だけ直すと、thermal.c の無い platform で system-device.c の weak の参照は NULL で通るが、Pi 5 の provider を持つ arm64 で thermal.c が入っていなければ link が落ちる。
- 直し方の案: 「`src/kern/thermal.c` を 12 の `vmunix.mk` の kernel の一覧に足す（weak は使わない）」と決めて p003 の作業と LW に入れる。i386 の pcat・pc98 が `system-device.c` を持つかも確かめて書く（今回は確かめていない）。

### M8. 「既定の profile の適用」と「起動の時に mode を書かない」が矛盾する（§3 の表の registry の行、§4.2 の永続）

- 根拠: §3 の表: registry の責務に「既定の profile の適用」。§4.2:「zedBSD は起動の時に mode を書かない（firmware の今の値を読むだけ）。書くのは user（root）が求めた時だけ（Q303-2）」。Dell の mode は BIOS の設定（NVRAM、§2.4 の Dell KB）に残る見込み。
- 起こる状況: 実装者が §3 に従うと、起動ごとに BIOS の設定を書き換える SMI が走り、Debian・Windows の側の mode も変わる（flash の書き換えの回数も増える、推測）。
- 直し方の案: §3 の「既定の profile の適用」を消すか、「この WS では持たない（将来 user の設定として持つなら別の決定）」と書く。

### M9. host 試験の計画が、今の harness の能力・所有と合わない（§6.1、§1 の最後の行、p002 の内容と受け入れ）

- 根拠:
  - `plan/ws049/tests/aml-host.c` 15〜64: `--eval PATH` は引数無しの評価だけ。buffer を引数にできるのは無く（`--package-arg` は固定の package）、`WMDV(0, 0x20, buf)`・`WMBA(0, 0, buf)` を呼べない。
  - `aml-host-hardware.c` 95・216〜245・497〜508: EC は 256 byte の静的な RAM。0x6F を読むたびに「数 → 種類」と変わる列、0x39 の index で 0x70・0x71 が変わる形は無い。SystemMemory は普通の記憶（`aml-host.c` 1082〜1089）で、SMI は何もしないので「写し戻し」は入力と同じになり、output の解析は試せない。
  - harness の `Makefile` 11〜18 は `aml-*.c` と `acpi-{tables,event,ec,text,resource}.c` だけを build する。`acpi-wmi.c` を足す変更が要る。
  - harness は completed の WS049 の物で、AGENTS.md §4 で他の WS の file は読むだけ。design は「plan/tools に移すかは Q1」と書くが、p002 の依存の表に入っていない。
  - §6.1 の DDV の解析・SMBIOS の buffer の試験は Dell の provider（p004 の code）を使うのに、p002 の受け入れに入っている。
  - §6.1 の期待「9 device・30 block」: 9 つの `_WDG` の大きさ（0x3C・0x28・0x8C・0xA0・0x28・0x3C・0x28・0x28・0x28）の和は **31 entry**。Debian の 30 には AMW0 の A3776CE0（"BC"、data、91150 の `_WDG` の 0x50〜0x63）が無い。AMW0 に `WQBC` は無い（`grep 'Method (WQ'` で確かめた）。Linux が query の method の無い data block を出さない振る舞いと見えるが、それは Linux の実装の規則（推測）で、Microsoft の文書の規則か確かめていない。
- 起こる状況: p002 の 4 LW の中で harness の拡張（引数つきの評価、列の EC、SMI と SystemMemory の模擬、Makefile）と、所有の移しを待つ事になる。期待の表を 30 にすると、`_WDG` を仕様どおり全部出す実装が FAIL し、Linux の規則を写す方向へ誘う。
- 直し方の案: p002 の前に Q1 に「harness を `plan/tools/aml-host/` に移す（か WS303 の tests に写す）」を頼み、依存の表に書く。harness の拡張（`--eval-args PATH TYPE:VALUE…`、EC の register ごとの読みの列、SMI の時に SystemMemory の output を書く模擬）を p002 の作業として明記して LW を足す。DDV・SMBIOS の解析の試験は p004 に移す。期待は「`_WDG` の 31 entry をそのまま出す」とし、query の無い data block の扱いは zedBSD の規則として自分で決めて書く。

### M10. 5330 の受け入れ試験が受け入れ条件を示せない・危険を受け入れる判断が抜けている（§6.3、§7 の p004）

- 根拠と起こる状況:
  - 「fan 1 本の rpm が 0 でない」: idle で EC が fan を止める機械では 0 rpm が正しい値になりうる（推測: Debian の観測の 3741 rpm は CPU 49〜55 °C の時）。起動の直後に読む試験は揺れる。
  - 負荷の道具: `kstress` は host の script の集まり（`plan/tools/kstress/`）で guest の CPU の負荷の program ではない。`yes` は `userland/base` に無い（`ls userland/base` で確かめた）。
  - 60 秒の負荷で EC の fan の曲線の上限に届くか分からない。quiet → performance の順の 1 回だけでは、温度の持ち越しで差が逆に出うる。
  - 「set の 1 回の時間 50 ms 以下」を合否にしているが、SMI の中の時間は OS が決められず（BIOS の NVRAM の書きを含む見込み、推測）、超えた時の設計の振る舞いが無い。
  - profile の set は BIOS の設定を書き換える（§4.2、Q303-2 の補い）。試験の手順 2・3 はこれを前提にしているが、ユーザーの承認（危険の受け入れ、AGENTS.md §2）の項目が §8 に無い。Q303-7 の B・C は試験の方法の選択で、承認ではない。WS301 も同じ 5330 で電力と周波数を測る（WS301 ws.md の受け入れ 2）ので、mode が変わったままだと WS301 の測定を汚す。
  - QEMU（§6.2）と Pi 5（§6.3）の `thermal` の出力をどう取るか（AGENTS.md §9: console・serial の log で判定しない。`guest.sh` の SSH か `serial.py` の対話）が書かれていない。
- 直し方の案: rpm は「負荷の後に 0 でない、または `KERN_SYSTEM_FAN_HAS_RPM` で値が読めている（0 も可）」にする。負荷は guest の `sh -c 'while :; do :; done' &` を CPU の数だけ、か既存の base の program を名指す。順を入れ替えた 2 回と、温度が下がるのを待つ時間を script に入れる。50 ms は「測って記録する」にし、超えた時は「profile の set を 1 秒に 1 回までに絞る」等の振る舞いを §4.10 に書く。§8 に「5330 の BIOS の Thermal Management を試験で書き換え、元に戻す事の承認」を足し、script は失敗の時も元の mode に戻す（trap）。WS301 の 5330 の試験と時間を分ける事を Q1 の割当の条件に書く。QEMU・Pi 5 の出力は `plan/tools/guest/guest.sh` の SSH で取る、と書く。

### M11. 出典の記録が検証できず、AML から出ない値の出所が書かれていない（§2.4、§4.1、§4.2、§9）

- 根拠:
  - design §0 の冒頭と §2.4 は「取った頁は `scratchpad/ws303-fable/`」と書くが、その directory にあるのは ACPI 6.5 の 11 章（`acpi65_thermal.*`、`acpi65_wayback.html`）だけ。Microsoft の「WMI and ACPI」、Dell の Service Manual・DCC・KB、LKML、Linux の ABI・hwmon・wmi の文書、Intel・coreboot の頁は無い。§4.1 自身も「Fable で版と節を確かめる」と未了を書いており、§2.4 の表の「確か」と食い違う。
  - §5.3 の `DELL_CALL_HEADER_SIZE 0x2C` と §4.2 の「頭（class・select・input[4]・output[4]・属性・延長の長さ）」: AML が示すのは class 0・select 2・input0 4・input1 8・延長の長さ 0x28・頭 0x2C だけ。input が 4 つ・output が 0x14 から 4 つ・0x24 が「属性」は AML から出ない。§2.4 は「AML の位置からの推論」と書くが、その推論は 4 + 4 の dword を前提にしている。出所が Linux の ABI の文書（Linux の tree の文書）なら、それは guardrail の上では出典を示して使える値（`plan/guardrail.md` 116〜123）であり、隠す要は無いが、出典を書く要がある。
  - 検索の要約に GPL の comment の言い換えが混じった事を design は認めている。頁を保存していないので、他の「確か」の行がどの要約から来たかを後で監査できない。
- 起こる状況: WS の最後の license の監査（guardrail の「block 単位の出典」）で、code の comment の「Value(s) referenced:」に書ける出典（版・path・節）が無い値が残る。B1 の `input0 = 0` のように、使わないと決めた情報が設計に入り込んでも見分けられない。
- 直し方の案: §2.4 の各行に「取った頁の file 名（scratchpad か `plan/ws303/temp/`、git に入れない）、版、節」を書き、保存の無い行は「未確認（要約）」に下げる。§5.3 の 0x14・0x24 の値の出典を 1 つに決めて書く（Linux の ABI の文書なら、その版と path を「Value referenced:」の形で）。§9 の「GPL の値は Q303-3 の B でユーザーの判断」は guardrail 123 で既に全 WS に許されているので「ユーザーの判断」は要らない（使うかどうかは設計の判断で、出典の明示が条件）と直す。

### M12. ユーザーの決定・確認済みの書き過ぎ（§0 の 3・4、§4.4 の見出し、§2.1 の DIAG の行、§4.4 の温度）

- 根拠:
  - §0 の 4「UAPI は足す（ユーザーの決定、§8）」と §4.4 の見出し「ユーザーの決定、Q303-1」: Q303-1 は §8 の未決の質問。ws.md の出典の原文は「DVFSとP-core/E-coreは、/dev/systemに入れるのが適切ならそれでもいいです…ファン制御もあるといいですね。」と、WS301 の UAPI のクリック（WS301 ws.md 18）で、WS303 の UAPI の形（ioctl の番号・struct・事象の class）は決まっていない。
  - §0 の 3「値は 5330 の AML と Debian の観測で確かめた」: class・select・set の形は AML で確か。get の形・output の位置・status・bit と名前の対応は未確認（§10 の 1 自身が書く）。
  - ws.md の到達目標は「温度の読み、fan の回転数の読み、…を、/dev/system の口で提供する」。design は温度を `hw.thermal`（sysctl）のままにする（§4.4・§4.11）。到達目標の文言との違いを決める者が書かれていない。
- 起こる状況: Q1・ユーザーが「決定済み」と読んで Q303-1 を聞かずに進む。WS の受け入れの時に「温度は /dev/system に無い」で到達目標を満たさない。
- 直し方の案: §0・§4.4 の「ユーザーの決定」を「推し（Q303-1 で決める）」に直す。§0 の 3 を「class・select・set の形は AML で確か、get・output・名前の対応は p004 で確かめる」に直す。Q303-1 に「温度を `hw.thermal` のままにし、ws.md の到達目標の文言を直す」を選択肢として明記する（ws.md の改訂は P5 か Q1）。

### M13. 止まりの判定（STALLED）が Dell の fan には定義できず、Pi 5 では誤報しうる（§4.3 の事象、§5.2 の `KERN_SYSTEM_FAN_STALLED`、§6.1 の registry の試験）

- 根拠: §4.3「level か mode が回る事を示すのに rpm 0 が 3 周期続く」。Dell の fan は level を持たず mode は FIRMWARE（§4.3・§4.5）で、mode も「回るべき」を示さない。EC は低温で fan を止める（推測）。Pi 5 は tachometer が無い構成で `has_rpm` が 0 になり `read_rpm` は 0 を返す（`rpi5-fan.c` 1139〜1152）。
- 起こる状況: 実装者が「FIRMWARE の mode は回る」と読むと、5330 の idle で fan0 stalled の事象が出続ける。Pi 5 で tachometer の無い fan に level > 0 で stalled が出る。
- 直し方の案: STALLED は「`KERN_SYSTEM_FAN_HAS_RPM` と `HAS_LEVEL` の両方があり、level > 0 で rpm 0 が 3 周期」に限り、Dell の fan には出さない、と §4.3 に書く。host 試験にこの 2 つの誤報の場合を足す。

## Minor

- **m1**（§2.1 の DIAG の行）「`_STA` は `GENS(0x22)` が 1 の時だけ有効」は DIAG の物ではない。`GENS (0x22, TOK0, 0x04)` の `_STA` は `\_SB.VDGS`（`DELL0029`、"Dell CORE Gateway Service"、`dsdt.dsl` 94472〜94490）の物。DIAG（94054〜）に `_STA` は無い。直す。
- **m2**（§4.1）`_WDG` が Method の device は AMW2 だけでなく 5 つ（M1 の根拠）。
- **m3**（§4.2 の fan の読み）「入力の buffer は 1 byte 以上」は誤り。WMDV は byte 0〜2 を `CreateByteField` し（94637〜94639）、zedBSD は buffer を超える field を作る時に拒む（`aml-define.c` 403〜406）ので **3 byte 以上**。同じ文の「0 を 4 byte」は足りている。
- **m4**（§4.2・§4.10 の DDV）EC の数が 0 か 0xFF の時、WMDV は `{0, Buffer(1){0x00}}` を返す（94880〜94891）。buffer は 0xFF で終わらず中身は 0x00 なので、package の長さ（要素 0）を先に見ないと「種類 0 の fan」と読み違えうる。解析は長さ → 3 byte の組 → 0xFF の順にする、と書く。
- **m5**（§4.2）EC の取引は 2 + 4n、加えて 1 回の読みに `Sleep (3)` が 1 つ（M2）。
- **m6**（§4.3 と §5.1）`kern_fan_register` の引数が §4.3 `(name, ops, context, flags)` と §5.1 `(name, rpm_max, ops, context)` で違う。1 つにする。
- **m7**（§5.2）`struct system_fan_info.mode` の comment が kernel の中の `KERN_FAN_MODE_*`（`include/kern/thermal.h`）を指す。user は kernel の header を include しないので、UAPI に `KERN_SYSTEM_FAN_MODE_*` を定義する。`system_power_info` と同じく `offsetof` の `_Static_assert` を足す。`time_ns` の時計（CLOCK_MONOTONIC）を書く。SET の入力で `profile` 以外の欄（`available`・`provider`・`reserved`）を 0 で求める（EINVAL）か無視するかを書く（`system_sleep_request` は「every other field is zero」、`system.h` 410〜）。
- **m8**（§4.6）状態遷移の穴: attach の時の get の一時の失敗で永久に ABSENT（再試行が無い）。SETTING で「get が失敗」の時の current が「get の値」と書かれていて値が無い。FAILED の時に GET の ioctl が写しを返すか EIO かが不明。set の失敗の後の `ODV0`（M3）。
- **m9**（§4.5・§4.8）Pi 5 の `rpi5_fan.lock` は今 `entry` だけを守り、`level` は thread だけが lock 無しで書く（`rpi5-fan.c` 193〜199 の comment、1180〜1187）。「今の lock で level・rpm の写しを守る」は今の code の事実ではなく、lock の範囲を広げる変更である、と書く。
- **m10**（§4.4）事象の class を足す時、`userland/tests/systemevents/main.c` の class の名前の表（64 付近）に "thermal" を足す。`KERN_SYSTEM_EVENT_CLASSES` を 0x1ff にすると、新しく build した program を古い kernel で走らせた時に "all" の購読が EINVAL になる（`system-event.c` 193）。zedBSD は kernel と userland を一緒に配るので実害は小さいが、§4.4 の「困らない」は古い program の側だけの話と書く。
- **m11**（§2.3・§4.13）「DDV の温度は ACPI の `_TMP` と同じ値（TCPU だけ別）」は言い過ぎ。DDV の 6 つ目（種類 0xFF、34 °C）には ACPI の対の zone が無く、CPU も 50 と 55 で違う。足さない判断は変えなくてよいが、失う物（1 sensor）を書く。
- **m12**（§2.2・§2.4 の IETM の `_OSC` の行）`IETM._STA`（`out-ssdt3.dsl` 337〜350）も `DDDR == 1` の時 `DPST (One)` を呼ぶ。zedBSD は起動で全 device の `_STA` を評価する（`acpi-kern.c` 223、`aml-table.c` 579〜581）。`DDDR` は External（GNVS と見られる、推測）で、Debian の `_OSC` の後に warm reboot した時に 1 が残るかは未確認。「`_OSC` を呼ばないので DTT 無し」の推定に、この道を書き足す。
- **m13**（§4.3・§4.9）S0 idle（WS052）との関係が無い。suspend の途中の周期の EC の読み・SMI を止めるか、resume の後に mode を読み直すかを書く（今の `acpi-thermal.c` も持たない。WS303 の新しい thread は同じ穴を足す）。
- **m14**（§5.1）`const uint8_t guid[16]` の byte の順（`_WDG` の生の順か、文字列の順か）を書く。5330 の DDV の GUID の表記（8A42EA14-4F2A-FD45-6422-…）は生の byte `14 EA 42 8A 2A 4F 45 FD 64 22 …` と前の 3 つの欄が逆順。
- **m15**（§3 の置き場所）`src/drivers/platform/pcat/dell-wmi.c` は amd64 の `CONFIG_DRIVER_ACPI` の block（`vmunix.mk` 356〜365）に入れる。i386 の `platform/pcat` は `src/drivers/acpi` を build しない（HAL の `src/hal/i386/acpi.c` だけ）ので、そこに足すと link が落ちる。
- **m16**（§4.10）BIOS に admin password がある時の set の失敗（推測: Dell の BIOS の属性の WMI は password を求める、§2.4）を EIO と分けて EACCES にするか、log で区別する。
- **m17**（§4.13 の 1 行目）「5330 は WSMT の表を持ち（dmesg の ACPI の一覧）」は手元の材料（scratchpad の表、survey）で確かめられない。推測と明記する。
- **m18**（§2.1）行の番号の小さな差: `WMDV` は 94635 から、`SMBF` は 91087 から。
- **m19**（§7）LW: harness の拡張と移し（M9）、12 の mk（M7）、sentinel と ODV0 の読み（B1・M3）、遅延の `_WDG`（M1）、試験の script の trap と 2 回の負荷（M10）で、p002 に +2、p003 に +1、p004 に +1〜2 の見込み。p004 は実機の割当とユーザーの BIOS の承認を待つ時間を別に持つ。
- **m20**（p006）`docs/architecture/power-management.md:24` は WS301 も変える。両 WS の p006・p009 の順か担当を Q1 が決める、と依存に書く。
- **m21**（§7 の WS301 p002 の依存を外す提案）WS303 の範囲では妥当。ただし WS301 ws.md（63 行）は「passive の口は WS301 p002 に入れ、熱と fan の WS がそれを使う」と書いており、外すと WS301 の口に使い手が居なくなる。Q303-6 の補いに書く。

## 確かめて正しかった主張（抜粋）

| 主張 | 確かめた所 |
| --- | --- |
| PNP0C14 は DSDT に 9 つ、行 62928・63129・91147・91730・93072・93750・94056・94625・100951 | `dsdt.dsl` の grep |
| `WMBA`: 0x28 の dword + 0x2C が全体、0x8000 まで、word 0 = class、word 2 = select、dword 4・8 = input0・1、class 0x11・select 0x13・input0 1 で input1 1・2・4・8 → `ODV0` 0〜3 と `Notify(IETM, 0x88)`、その後 `WMI` → `GENS(3, …)` → `SMBF` → `ASMI`（port `PSMI` 0xB2 に 0xE0） | `dsdt.dsl` 91256〜91310、90978〜91138 |
| `WQAA` が "DELL"・" WMI"・1・0x8000・1（instance 0 の時） | `dsdt.dsl` 91184〜91202 |
| `_WED(0xD0)` が AMW0 の事象の buffer を返す | `dsdt.dsl` 94532〜94605 |
| DDV: 0x12 は 3、0x20 は `{3n+1, [種類, LSB, MSB]… 0xFF}`、0x22 は sensor ごとに 5 byte と 0xFF | `dsdt.dsl` 94873〜94985 |
| 5330 の静的な表と動的な SSDT（`latitude5330/dynamic/`）に ACPI の fan（EISAID PNP0C0B、`_FST`・`_FIF`・`_FPS`）が無い。`INTC1048` は SSDT3 の `GHID` の文字列だけ | `.dat` の byte 列の検索 |
| `IETM.ODVP` が `ODV0`〜`ODV5` を返す、`_OSC` が `DPST (One)` を呼ぶ | `out-ssdt3.dsl` 490〜498、363〜420 |
| Debian の観測: profile 4 つと今の performance、DDV の fan 3741 rpm と生の `00 39 0f ff`、温度の生の列、`dell_smm` の fan1_max 4850・target 3000・pwm1 128・enable 1・cooling 1/2、BIOS 1.31.1・SKU 0B02、type 218 が 2 つ（0xDA00・0xDA01）、type 11 の "Dell System"・"1[0B02]"、WMI の device 30 | `survey.txt`・`survey2.txt` |
| 引数つきの評価（`drv_acpi_evaluate`）、buffer・package の object の作成、Notify の handler の登録 | `include/drivers/acpi/acpi.h` 277〜365 |
| interpreter の lock は 1 つの mutex（`LOCK_RANK_DEVICE`） | `acpi-kern.c` 174〜177 |
| arg への store は reference なら通し、他は置き換え（`WMI` の `Arg0 = GENS(…)` を正しく扱う）。引数は呼び手と object を共有する | `aml-eval.c` 849〜872、913〜920 |
| `ObjectType`・`Switch`（While・Break）・method の中の `OperationRegion`・`Name`・`CreateDWordField`・`Index`・`ToString`・`Sleep`・`Acquire` の opcode がある | `aml-operator.c`・`aml-eval.c`・`aml-define.c` の case の一覧（評価が通るかは未試験） |
| SystemMemory・SystemIO の handler があり、SystemIO は port の制限を持たない、attach の順は EC → power → thermal → LPS0 | `acpi-kern.c` 187〜194、236〜256、1131〜 |
| EC の `_REG` は EC の address space の登録で走る（ECRD = 1 になる道がある） | `acpi-ec.c` 133〜135・343、`aml-field.c` 160〜186 |
| `hw.thermal` の header 24 byte・entry 64 byte、source 4・1 source 16 sensor、5 秒の周期、上限 16 | `sysctl.h` 290〜312、`sysctl.c` 146〜161、`acpi-thermal.c` 37・38・112・135・236 |
| `/dev/system` の ioctl は 1〜21 を使い 12 は `mountinfo.h`、22〜24 は空き。事象の class 0x01〜0x80、`KERN_SYSTEM_EVENT_CLASSES 0xff`、mask の検査、root の判定 | `system.h` 273〜464、`mountinfo.h` 19、`system-event.c` 193、`system-device.c` 333〜337 |
| §5.2 の struct は 88 byte と 32 byte で、ILP32（i386 の 8 byte の整列 4）でも LP64 でも同じ（`time_ns` は offset 32） | 欄の和の計算 |
| Pi 5 の `poll_once`・`read_rpm`、周期 1 秒、起動 `rpi5.c:194` | `rpi5-fan.c` 66・1139〜1195、`src/kern/platform/rpi5.c` 194 |
| amd64 は `src/drivers/acpi/*.c` を wildcard で入れる | `platform/amd64/vmunix.mk` 358 |
| HAL の API（`include/hal/hal.h`）の変更は要らない | 設計のどの節も HAL の宣言・契約を使わない（AML の handler は既にある） |
| `amdrdna.h` の fan_mode | `include/uapi/amdrdna.h` 93〜99・249〜252 |

## 確かめられなかった事（推測のまま）

- SMI の handler の class 0x11・select 0x13 の input0 ≠ 1 の意味、output の位置と status、SMI の時間（firmware の中）。B1・M3・M4・M10 の一部はこれに依る。
- BIOS が起動で `\ODV0` を今の mode で埋めるか（B1 の案 1 の前提、design §10 の 7 と同じ）。
- `/dev/acpi` から `\_SB.AMW0.WQDS` などが実機で最後まで走るか（M6 は code の読みだけ）。
- 5330 の EC が idle で fan を止めるか（M10・M13）。
- query の method の無い data block を Linux が出さない理由（M9 の 31 と 30 の差）。
- i386 の pcat・pc98 が `system-device.c` を build するか（M7）。
- 実機・QEMU・host の試験は 1 つも行っていない。
