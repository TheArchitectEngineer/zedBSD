<!-- awesome-plan project=zedbsd record=ws301-design-v3-review -->

# WS301 設計の第 3 版の敵対的レビュー（design.md 第 3 版・HAL の差分の第 3 版・hal-review.md）

日付: 2026-10-29 04:33 JST（host の `date`）。担当: design-reviewer（P5 の依頼）。

- 対象: worktree `p5-design-c` の **f30cec278**（依頼の先頭）の `plan/ws301/design.md`（第 3 版）、`plan/ws301/proposed/hal-cpu-perf.diff`（第 3 版）、`plan/ws301/hal-review.md`、`plan/ws301/ws.md`。前の review [design-review.md](design-review.md)。WS303 は [../ws303/design.md](../ws303/design.md) との食い違いだけを見た（WS303 そのものの review はしない）。
- **注意: review の途中で branch が進んだ**（f30cec278 → 099c3cc7e、2026-10-29 04:23〜04:32 の 4 commit。design.md が「第 3 版の改訂 1」になり、推しが「HAL を変えない」に替わり、diff は「選ばなかった案 C」になった）。本文の指摘と行番号は依頼どおり **f30cec278** の物。改訂 1 にどの指摘が残るかは末尾の §「改訂 1 への持ち越し」に書いた。
- 読んだ source（この worktree）: `include/hal/hal.h`（error の enum 27〜36、CPU の口 113〜241、notify 154〜181）、`include/hal/types.h:13`、`include/kern/lock.h`、`include/uapi/ioctl.h`・`include/uapi/*.h` の ioctl の group、`include/uapi/sysctl.h:240〜310`、`src/hal/amd64/{asm.c,smp.c,int.c}`、`src/hal/amd64/bsp-pcat/{acpi.h,lapic.c}`、`src/hal/idle-suspend-unsupported.c`、`src/kern/{clock.c,sched.c,sleep.c}`。
- 読んだ資料（手元だけ、web の取得なし、GPL の source は読んでいない）: scratchpad の `ws301-5330/`（`read.txt`・`spin.txt`・`spin.py`・`sysfs.txt`）、`ws301-fable/` の Intel SDM（`sdm-1.txt`・`sdm-3b.txt`・`sdm-vol4.txt`）、AMD APM（`apm-v2.txt`）。
- 実行した物: `git apply --check plan/ws301/proposed/hal-cpu-perf.diff`（f30cec278 で通った、当てていない）、`git log`・`git diff`・`git show`。build・QEMU・実機は未実施。
- 重大度: **blocker**（このまま承認・実装すると目的か HAL の API を壊し、後で直すのに再承認が要る）、**major**（実装の前に設計か差分を直す）、**minor**（実装の中か記録の整合で直せる）。「推測」「未確認」は資料と source で確かめられなかった事。

## 要点

- **blocker 1 件**: B1 `highest_khz`（CPUID.16H）を MHz の換算と相対の性能（Q301-2 の推し）の源にしているが、SDM は CPUID.16H を「表示の用途だけ、他の用途に使うな、他の口の値と正しく対応しない」と書く。5330 の 16H の base（2500 MHz）は実際の base（Linux の `base_frequency` 1600・1200 MHz）と違い、その警告どおり。ユーザーはこの警告を知らされずに Q301-2 に答え、HAL の承認の説明（hal-review.md）は「実測と合った」とだけ書く。
- **major 12 件**: §2.3 の「実測と合う」は 1 点の検証だけ（M1）、Most Efficient より下の max を書く表と試験（M2）、PECI の上書きの flag と事象が差分と設計で食い違う（M3）、`feedback_copy` の戻り値が HAL の error の値とぶつかる（M4）、CPU ごとの周期の読みの仕組みが無く notify は S0 idle を終わらせる（M5）、熱の上限と strategy の受けが 1 秒の待ちを mutex の下で重ねる（M6）、全 domain の直の設定が hybrid の単位で壊れる（M7）、Phase の依存の抜け（WS303 p007 ほか、M8）、試験の計画が誤りを見つけられない（M9）、hal-review.md の誤解を招く所（M10）、guardrail の「HAL → kernel は汎用の構造体だけ」との緊張（M11）、WS303 との食い違い（M12）。
- **minor 17 件**。
- §2.3 の生の記録の読みの多く（CPUID.06H・1AH・1FH・EDX の HFI の行、MSR の値の分解、TjMax、PL1・PL2、HWP_CTL の極性、UAPI の 96・240・32 byte）は正しかった（末尾の表）。

## Blocker

### B1. CPUID.16H を周波数の換算と相対の性能の源にしている（SDM が禁じる用途）

- 根拠:
  - SDM Vol 1 CPUID.16H（`sdm-1.txt:27472-27478`）「Data is returned from this interface in accordance with the processor's specification and does not reflect actual values. Suitable use of this data includes the display of processor information in like manner to the processor brand string ... The returned information should not be used for any other purpose as the returned information does not accurately correlate to information / counters returned by other processor interfaces.」
  - 5330 の生の記録: 16H の EAX（base）は P・E とも 2500（`read.txt` の各 cpu の `16=2500,...`）、Linux の `base_frequency` は P 1600000・E 1200000（`sysfs.txt` の cpu0・cpu4 の行）。16H の base は製品の base と違い、SDM の「does not reflect actual values」の実例になっている。
  - SDM Vol 3B §17.4.3（`sdm-3b.txt:459-461`）HWP の単位と周波数の対応は「processor family specific」。16H の max が HWP の Highest に当たるという記述は SDM に無い。
  - 差分 diff:92 `uint32_t highest_khz; /* the processor's stated clock at highest (x86: CPUID 16H) */` は 16H の max を「Highest の時の clock」と定義して HAL の契約に固定する。design §4.2（`design.md:120`）、§4.7（`design.md:161-163`）、Q301-2 の推し（`design.md:339`）、hal-review.md:79（「5330 の実測と合った」）。
  - Turbo Boost Max 3.0（CPUID.06H:EAX[14]、`sdm-1.txt` Table 21-16）の CPU では P-core ごとに Highest が違う（前の review の m12）が、16H は「logical processor」ごとでも型番の値（推測: 速い core と他で同じ値）なので、`highest_khz / highest` は速い core の MHz/単位を小さく出す。5330 は bit 14 が 0（EAX 0x00DF8FF7 の bit 12〜14 は 0）なので 5330 では出ない。
- 起こる状況: (1) HAL の API に「16H = Highest の clock」が承認され固定される。別の世代・TBM 3.0 の CPU・16H を 0 で返す CPU で値が狂っても、直すのに HAL の差分の再承認が要る。(2) 相対の性能（ユーザーの要求 (5)）が、vendor が「他の用途に使うな」と書く値の比で出る。5330 で E = 75 が合って見えても他の機械の保証が無い。(3) ユーザーは Q301-2 を「firmware の値が誤り」とだけ聞いて推し（16H の比）に答えた（改訂 1 の design.md:22、ws.md の「ユーザーの回答の第 2 回」）。決定の前提の情報が欠けていた。
- 直す案:
  - hal-review.md・design §4.7・Q301-2 に SDM の文を引いて書き、ユーザーに聞き直す（要求の解釈と危険の受け入れはユーザーが決める）。
  - 源の候補を並べ直す: (a) **測った値**: SDM §17.4.10（`sdm-3b.txt:1140-1147`）の characterization（Min = Max に固定して APERF/MPERF を測る）で、起動の後か試験で Highest（と guaranteed など 2 点以上）の MHz を core の種類ごとに測り、線形かを確かめてから使う。(b) 16H は「表示の上限の目安」だけにし、相対の性能には使わない。(c) HFI（Q301-2 の B）。(d) model ごとの表（SDM・datasheet の根拠つき）。
  - HAL の差分を残すなら、`highest_khz` を削るか「display only (CPUID 16H EBX), not for frequency calculations」と契約に書く。

## Major

### M1. §2.3 の「CPUID.16H の最大 ÷ Highest と実測が合う」は 1 点しか確かめていない（design.md:84-85）

- 根拠: `spin.py` は 1 CPU ずつ 1 秒 spin して APERF/MPERF を読むだけで、要求を変えていない（Linux の REQUEST は Max = Highest のまま、`read.txt` の 0x774 = 0x3806・0x2104）。測れたのは「Highest の時に 4388・3292 MHz」の 1 点で、MHz/単位の傾き（線形か、原点を通るか）は確かめていない。1 core の turbo の上限は TURBO_RATIO_LIMIT 0x1AD の最下位 byte 0x2C = 44（4400 MHz、`read.txt`）で、測った値はそれと一致するだけ。
  - design.md:85 の「P 31 × 80.6 ≒ 2500」は、自分で出した 78.6 MHz/単位でなく 80.6 を使っている。78.6 なら 31 × 78.6 = 2436 MHz（TSC の 2494 MHz と 2.4% 違う）。E の 25 × 100 = 2500 は合う。
  - 低い側: Lowest = 1（0x771 の bit 31:24）を 78.6・100 MHz と換算するが、Linux の `cpuinfo_min_freq` は 400000、PLATFORM_INFO の最小の比は 4（400 MHz、`read.txt` 0xCE）、Linux の REQUEST の Min は P 6・E 4（400 MHz 相当）。Lowest の MHz は線形の換算で 1/4〜1/5 に出る。
- 起こる状況: §5.2 の `step_mhz`・`highest_mhz` と §6.3 の 1（「MHz/単位が §2.3 と合う」）が、確かめていない換算を期待の値にして通ってしまう。lowest の MHz の表示は誤る。
- 直す案: §2.3 の読みを「Highest の 1 点が 16H の max と合う」に直し、傾きは未確認と §10 に足す。§6.3 に「min = max = N（lowest・efficient・guaranteed・highest の 4 点）で spin し MHz を記録」を足して傾きと下端を測る（B1 の (a)）。85 行の 80.6 を直す。

### M2. Most Efficient より下の max を書く表と試験（design.md:141-144・156・309）

- 根拠: SDM §17.4.3（`sdm-3b.txt:509-511`）「Most_Efficient_Performance ... the practical lower limit for the IA32_HWP_REQUEST. The processor may not honor IA32_HWP_REQUEST.Maximum Performance settings below this value.」また Most Efficient は「can change dynamically as a result of workload characteristics」（`sdm-3b.txt:494-495`）で、HWP_STATUS に変化の bit は無い（`sdm-3b.txt:800-830`）。
  5330 の E-core は Guaranteed 12 < Most Efficient 15（0x010F0C21）。
  - POWERSAVE の max = guaranteed（design.md:143）→ E-core に 12（efficient の下）を書く。
  - MANUAL の noise の上限（design.md:144）は noise = 0 で guaranteed = 12 → 同じ。
  - §6.3 の 3（design.md:309）は E-core に max 8 を書いて clock の共有を見る → 8 は efficient の下で「may not honor」。
  - 差分の契約 diff:115-119 は「GUARANTEED・HIGHEST の事象の後に読み直せ」と書き、efficient が事象なしで動く事を書かない。kernel が持つ efficient は古くなる。
- 起こる状況: POWERSAVE・MANUAL の E-core が要求どおりに下がらず、§6.3 の 2（MHz が要求に従う）が E-core で落ちる。§6.3 の 3 は「hardware が無視した」と「clock を共有した」を区別できない。
- 直す案: max の下端を `max(guaranteed, efficient)` にする（表と MANUAL の式）。efficient は要求を作るたびに読み直す（契約に「changes without an event」と書く）。§6.3 の 3 は efficient 以上の 2 値（例 15 と 33）で行う。

### M3. PECI の上書き: 差分は「能力の flag」、設計は「HWP_STATUS の bit を報告」（diff:79、design.md:121・211）

- 根拠: SDM §17.4.4.3（`sdm-3b.txt:672-746`）IA32_HWP_PECI_REQUEST_INFO 0x775（package、RO）の bit 60・62・63 が「今上書き中」の Min・Max・EPP を示し、値は bit 7:0・15:8・31:24。HWP_STATUS の bit 4・5 は入り・出の sticky な事象（`sdm-3b.txt:811-830`）で「今の状態」ではない。
  diff:79 `HAL_CPU_PERF_PLATFORM_LIMIT` は capabilities の flags（「what the processor offers」と同じ欄、diff:115-117）で、意味が「上書きができる」（CPUID.06H:EAX[16]）か「今上書き中」か決まらない。`HAL_CPU_PERF_EVENT_*`（diff:110-112）に PECI の入り・出が無い。`hal_cpu_perf_get` が返すのは REQUEST の値で、上書きの後に効く値（resolved）ではない。
  5330 の記録に 0x775 の読みが無い（`read.txt` の msr の一覧）。
- 起こる状況: Dell の EC（WS303 の thermal mode）が Max か EPP を上書きすると、zedBSD は要求が効いたと表示し、§6.3 の 2 の失敗の原因が見えない。kernel は「制限中」を出す手段を持たない（契約の外）。
- 直す案: diff に「上書き中の field の mask と値」を get か別の欄で返す（0x775 を読む）、事象に PECI_ENTRY・PECI_EXIT を足す。design §4.2 の文を契約に合わせる。§6.3 の script で 0x775 相当の値と WS303 の mode を毎回記録する。

### M4. `hal_cpu_perf_feedback_copy` の戻り値が HAL の error の値とぶつかる（diff:166-175）

- 根拠: diff:168-170「Returns 1 with a copy, 0 when nothing new was written, or a negative HAL error.」`enum hal_error` は `HAL_OK = 0`、`HAL_ERR_INVALID = 1`、… `HAL_ERR_IO = 7` の正の値だけ（`include/hal/hal.h:27-36`）。hal.h に「negative HAL error」の前例は無い（grep 0 件）。
- 起こる状況: 実装が慣例どおり `return HAL_ERR_INVALID;` と書くと kernel は「写した」と読み、0 で埋まった buffer を HFI の表として使う（全部 0 の行 = 「走らせるな」、design.md:167）。`-HAL_ERR_UNSUPPORTED` と書く port と `HAL_ERR_UNSUPPORTED` と書く port が混ざる。
- 直す案: 戻り値は HAL_OK か HAL_ERR_* にし、「新しい写しがあったか」は out の引数（`bool *copied`）で返す。

### M5. CPU ごとの周期の読みの仕組みが無く、notify で読めば S0 idle が終わる（design.md:161・166・175・238）

- 根拠: APERF/MPERF・HWP_STATUS は logical processor の MSR（`sdm-3b.txt:470` の REQUEST と同じ scope、Vol 4 の 771H・774H は Thread、`sdm-vol4.txt:17541-17543`）で、その CPU の上でしか読めない。design は `delivered_mhz`（「last second」、§5.1 238 行）と HWP_STATUS の変化（§4.8 175 行）を前提にするが、誰がいつ各 CPU で読むかを書かない。差分は「the kernel makes the calls from kernel_cpu_notify_handler() on the target CPU」（diff:64-65）。
  `hal_cpu_notify()` は `hal_cpu_idle_suspend()` の待ちを必ず終わらせる（`include/hal/hal.h:154-170`）。`kern_sleep_idle` は最初に戻った CPU が ENTER を WAKE にし（`src/kern/sleep.c:237-245`）、spurious の wake が 10 回続くと sleep が終わる（`sleep.c:55-56` `SLEEP_SPURIOUS_MAX 10U`、`sleep.c:22-23`）。
- 起こる状況: 1 秒ごとに 12 CPU へ notify すると、idle の E-core を毎秒起こして idle の電力を上げ（WS の目的に反する）、S0 idle は約 10 秒で「spurious」で終わる（§6.3 の 5 の回帰が落ちる）。読みを ioctl の時だけにすれば `delivered_mhz` の「last second」が意味を持たない。
- 直す案: 読みの仕組みを設計に書く: (a) その CPU の tick（`kernel_timer_handler` の中で 1 秒に 1 回、tick が止まる S0 idle では読まない）で自分の MSR を読み写しに置く、notify は使わない。(b) `delivered_mhz` は ioctl の時に 2 回の標本の差（直前の標本との差）で出す。どちらでも「sleep の間（`sleep_state` が ENTER の間）は cpuperf の notify を出さない、出すなら sleep の後まで箱に貯める」を §4.10 に書く。p006 の受け入れに「CONFIG_CPU_PERF=y で S0 idle が spurious で終わらない」を足す。

### M6. 熱の上限と strategy の受けが、mutex の下で 1 秒の待ちを重ねる（design.md:122・136・156・198-199、WS303 design:48・53・114）

- 根拠: ioctl は「全部の相手の確かめを 1 秒待ち」（design.md:122）、`cpuperf_mutex` は ioctl・strategy の受け・熱の上限の全部を直列にする（design.md:198）。strategy の受けは WS303 の strategy の mutex の下（design.md:199）で、受け手は 1 秒以内に結果を返す約束（design.md:136）。`kern_cpuperf_limit` を呼ぶ文脈（thread か、lock を持つか、待つか）は書かれていない（design.md:156）。WS303 の passive（T5、WS303 design:114）は 5 秒の周期で上限を下げ、WS303 V8 は `acpi-thermal.c` の 5 秒の thread を唯一の読みの源にし、同じ周期で非常の停止を判定する（WS303 design:48・56）。
- 起こる状況: root の直の設定の ioctl が `cpuperf_mutex` を持って 1 秒待つ間に、user の strategy の選択（WS303 の mutex を持つ）と passive の呼び出しが `cpuperf_mutex` で待つ。strategy の受けは 1 秒の約束を越え（WS303 は「partial」か timeout と見る）、passive の呼び手が温度の thread なら、その thread の critical の判定が最大 2 秒以上遅れる。passive の呼び手が spinlock を持っていれば mutex で眠れない（推測、WS303 の実装次第）。
- 直す案: `kern_cpuperf_limit` は待たない口にする（`cpuperf_mutex` を取らず、source ごとの値を atomic に置き、箱を作り直す仕事は cpuperf の側で後で行う）と契約に書く（「thread context, no spinlock held, never blocks」）。strategy の受けも「箱に置いて notify するまで、確かめは待たない」にし、結果は「適用を依頼した」で返す。待つのは root の ioctl だけにし、待つ間は `cpuperf_mutex` を放す（世代の数で確かめる）。

### M7. 全 domain の直の設定（`domain = 0xffffffff`）が hybrid の単位で壊れる（design.md:279-283・§4.5）

- 根拠: `struct thermal_cpu_setting` は min・max・desired・preference を抽象の単位で 1 組だけ持ち、`domain = 0xffffffff` で全 domain に当てる（design.md:280）。単位は core の種類で違う（P の Highest 56、E 33、design.md:78）。§4.6 は同じ理由で熱の上限を千分率にした（design.md:156）。差分の `set` は highest を超えれば HAL_ERR_INVALID（diff:134-139）。どの field を変えるかの mask が無い。
- 起こる状況: `max = 56` を全 domain に書くと E-core の domain は全部 INVALID で、P だけ変わる（一部の失敗の返し方が無い）。EPP だけを変えたい root が min・max も書き直すしかない。
- 直す案: 全 domain の時は千分率（§4.6 と同じ）か、全 domain の口を外し per-domain だけにする。`reserved` の 1 語を「有効な field の mask」にする。一部の失敗は domain ごとの結果か EINVAL で全部を書かない、を決める。

### M8. Phase の依存の抜けと受け入れの読めなさ（design.md:316-327）

- 根拠:
  - p006（5330 の受け入れ、§6.3 の 2「strategy の 4 つで MHz が要求に従う」）の依存は p004・p005 だけ。strategy が WS301 に届くには WS303 p003（strategy の ioctl と `/sbin/thermal`）と WS303 p007（WS301 の受け手の登録、design.md:135・327、WS303 design:51）が要る。WS303 p007 は WS301 p002・p003・p004 に依る（WS303 design:53・128）。
  - p004 の受け入れ「5330 で §6.3 の 1・6」は表の読みの口（p005 の UAPI と道具）が無いと読めない（前の review の m10、未対応）。
  - p002 の依存「HAL の承認（Q301-1。host 試験は stub で先に）」と受け入れ「全 platform の build warning 0」: 承認の前は hal.h に宣言が無く、kernel の build が通らない。どこまでを p002 で閉じるかが書かれていない。
- 起こる状況: p006 に着手した時に strategy の道が無く止まる。p004 を cleared にする証拠が出せない。
- 直す案: p006 の依存に WS303 p003・p007 を足す（順: WS301 p004 → WS303 p007 → WS301 p006）。または p006 の strategy の項を試験の ioctl で `kern_cpuperf_strategy` を直に呼ぶ形に変える（試験の build だけ）。p004 の受け入れは p005 の後に回すか、試験の build の ssh の道具で読むと書く。p002 は「承認の前は host 試験だけ、build は承認の後」と分ける。

### M9. 試験の計画が誤りを見つけられない（design.md:294-312・323）

- 根拠と状況:
  - **合否の数が無い**: §6.3 の 2「MHz が要求に従う」、p006 の受け入れ「script の PASS」に許す幅が無い（前の review の M11 の「±x%」は未対応）。
  - **#GP の回復の道を誰も通さない**: 差分の安全の要（diff:66-69、hal-review.md:67）だが、QEMU は hypervisor の bit で B を全部 UNSUPPORTED にするので（design.md:118・303）回復の道を通らず、5330 は MSR が有るので #GP が起きない。回復の表の誤り（戻り先・register の保存）は本番で初めて出る。
  - **§6.3 の 4 はユーザーの手が要る**（design.md:310「AC を抜く。ユーザーの手が要る」）: AGENTS.md §3「ユーザーに試験を頼まず、ユーザーが拾える試験の一覧に置く」に反し、p006 の受け入れ（§6.3 の 1〜5）が人の手に止まる。
  - **動く値を期待の値にする**: §6.3 の 1「Highest 等が §2.3 と合う」の Guaranteed・Most Efficient は電力の上限と負荷で動く（`sdm-3b.txt:492-495`・`498-503`）。§2.3 の値は Dell の mode が未記録の時の物（m2）。
- 直す案: p006 の前に数を決める（例: min = max = N の固定で spin の MHz が N × MHz/単位 の ±5%、P・E ごと）。試験の build だけの self-test（QEMU で存在しない MSR を回復つきで読み、HAL_ERR_UNSUPPORTED が返り kernel が止まらない事を ssh の道具で読む）を §6.2 と p003 の受け入れに足す。§6.3 の 4 は p006 の受け入れから外し、ユーザーが拾える試験の一覧（Q1）に置くか、RAPL の energy の counter の読み（前の review の M12）に替える。§6.3 の 1 の比べる field を Highest・Lowest・core の種類・CPU の数・HFI の行に限る。

### M10. hal-review.md がユーザーに誤解を与える所（hal-review.md:11・37・59・61・67・71・79）

- 79 行「`highest_khz` … 5330 の実測と合った」: B1 の SDM の禁止と M1 の 1 点だけの検証を書かない。承認の判断の材料として最も重い欠落。
- 11 行・59 行「HWP は第 6 世代以降」「HWP 無し（第 5 世代以前）」: SDM Vol 4 は Xeon D と Xeon E5 v4（Broadwell の世代）の表に IA32_PM_ENABLE・HWP_CAPABILITIES・HWP_REQUEST を載せる（`sdm-vol4.txt:17522-17545` の Table 2-36）。design §4.9（design.md:182）も同じ誤り（m17）。
- 37 行「module を L2 cache を共有する組から作る」: 5330 の CPUID.04H は読んでいない（design.md:76・347「04H の値そのものは未読」）事を書かない。
- 61 行「i386・pc98・arm64・riscv64・他（12 の HAL の directory 全部）」: `src/hal/` の 12 は amd64・arm64・armv7・i386・m68k・mips・ppc32・ppc64・riscv32・riscv64・sparcv9・x86 で、x86 は共通の file の置き場、pc98 は `src/hal/i386/bsp-pc98`。sparcv9 はユーザーが作業する port（Q1 の memory、触る前に聞く）。
- 67 行「HAL は例外で止まらずに対応外を返す」: その道を試験が通さない（M9）事を書かない。
- 71 行「再起動で消えるので Debian に戻っても困らない」: 消えるのは RESET の時（`sdm-3b.txt:444`「Only RESET will clear this bit」）。INIT では消えない。Debian は HWP を自分で使うので困らないのは正しいが、理由が違う（m16）。
- 判断の点（hal-review.md:75-81）に、M11 の guardrail との緊張と M4 の戻り値の誤りが無い。
- 直す案: 上を直し、§6 の判断の点に「16H の扱い（B1）」「HFI の生の表を kernel に渡す事（M11）」を足す。

### M11. guardrail「HAL → kernel は汎用の構造体だけ」と HFI の生の表（diff:86-90・154-175、`plan/guardrail.md:105-109`）

- 根拠: guardrail（2026-10-18 ユーザー、`plan/guardrail.md:107-108`）「HALからカーネルへは、汎用の構造体しか渡せない規約です。固有の情報は、名前文字列で取得するAPIがあります」。差分は Intel の HFI の表を byte のまま kernel に写し（`feedback_copy`）、kernel が表の形（頭 16 byte・行 8 byte、Thread Director の時は `8 + CP × CL + R8`、`sdm-3b.txt:1520-1640`）を解く前提で `feedback_row`・`feedback_capabilities`・`feedback_classes` を返す。`core_type` も「x86: CPUID 1AH EAX[31:24]」の番号（diff:37）。前の review の M2 の「HAL が表を解き CPU ごとの 2 つの値を返す」案は採られていない（理由の記録も無い）。
- 起こる状況: 承認の後に guardrail の監査で「HAL が Intel 固有の表を kernel に渡す」と判定されれば、HAL の API の作り直し（再承認）になる。kernel の中に Intel の表の形が入り、§4.2 が案 (b) を退けた理由（arch の境界）と同じ問題が残る。
- 直す案: (a) HAL が表を解き `hal_cpu_perf_feedback_read(cpu, &performance, &efficiency)` の汎用の値を返す。(b) 生の表を渡すなら guardrail との関係をユーザーに明示して判断を求める（hal-review.md の判断の点に足す）。`feedback_classes` が「CPUID の数」か「HAL が入れた数（HFI だけなら 1）」かも決める（m7）。

### M12. WS303 との食い違い（design.md:144・147・156、WS303 design:24・52・53・114）

- 起動の時: design.md:147「strategy は firmware のまま … WS301 は何も書かない。`CONFIG_CPU_PERF=y` の kernel が enable した時の最初の要求は SDM の推奨（EPP 0x80）」は自分の中で矛盾する。HWP の enable で PERF_CTL は無視され（`sdm-3b.txt:441-443`）、CPU の振る舞いは firmware のままでなくなる。WS303 V5（WS303 design:52）は「firmware のまま … WS301 の policy には何も渡さない」と表示する。y の kernel で strategy の表示「firmware のまま」は CPU について偽になる。
- noise の軸: design.md:144 は MANUAL の max に noise を使う。WS303 の解釈 2（WS303 design:24）は「WS301 の CPU の provider は speed と strategy から EPP・min・max を」決めると書く（noise を使わない）。どちらも P5 の記録で、片方を直す。
- 熱の上限の段: WS303 T5（WS303 design:114）は「HWP の単位で highest の 10%」、V6（WS303 design:53）と WS301 §4.6 は千分率。WS301 の口は千分率なので、WS303 の T5 の文は V6 で置き換わった事を WS303 側に返す（WS303 の review の範囲、ここでは指摘だけ）。
- 熱の下限 guaranteed: Guaranteed は熱・電力の制約で hardware が自分で下げる値（`sdm-3b.txt:492-493`・`498-503`）なので、下限 guaranteed の passive は hardware の絞りより下に効かない。E-core は guaranteed 12 < efficient 15 で、下限は M2 の efficient にも当たる。WS303 と同じ決定（V6）なので、意図を両方の design に一文で書く。
- 直す案: 起動の時の表示を「HWP を入れた（EPP 0x80）」の別の状態にするか、y の kernel でも enable を最初の strategy の選択まで遅らせる（PM_ENABLE は一度きりなので、選ばれるまで firmware のまま）。noise の扱いを両 WS で揃える。

## Minor

- m1. §2.3 の CPUID.06H:EAX の行（design.md:70）は bit 17（Flexible HWP）・18（HWP_REQUEST の fast access）を落としている（0x00DF8FF7 の bit 16〜23 は 0xDF、`sdm-1.txt` Table 21-16 の EAX[17]・[18]）。HWP_INTERRUPT 0x773 = 5（Linux が guaranteed の変化と excursion の割り込みを入れた、`read.txt`）と 0x775 の未読も表に書く。
- m2. design.md:79 の「platform_profile performance」と 88 行の「Dell の thermal mode が performance の時の値」は、生の記録（`sysfs.txt`・`read.txt`）に platform_profile も Dell の mode も無い。記録が無いなら「未確認」に直す。
- m3. design.md:76「Linux は module を L2 の共有（CPUID.04H）から作る」は Linux の中の作りの主張で、黒箱の観測（§8）からは言えない。観測できたのは `cluster_cpus` の組と `cluster_id` が組の先頭の x2APIC ID（0・8・16・24）である事だけ。「推測」と書く。
- m4. topology の契約（diff:16-28）: module を「a level … that the processor reports」と定義するが、5330 の module は CPU が報告しない L2 の組から作る（design.md:107-108）。「報告された level か、無ければ die の下の最後の共有 cache の組」と契約に書く。ID は「compare equal for CPUs that share that level」だけで「共有しない CPU とは違う」が無い（複数 package で core の ID が package ごとだと壊れる、前の review の M7）。起動に失敗した AP の答え（HAL_ERR_INVALID か既定か）が無い。
- m5. `HAL_CPU_PERF_ENABLED` と「before hal_cpu_perf_enable()」（diff:115-119・131-139）: firmware か前の OS が PM_ENABLE を 1 にして起動した時（design.md:350 の未確認の 6）に、HAL の呼び出しの履歴と hardware の状態のどちらで答えるかが無い。enable は「already on」で HAL_OK と書くので、hardware の状態で答えると書く。
- m6. `hal_cpu_perf_feedback(uint64_t physical_address)`（diff:163-164）は hal.h の慣例 `hal_physaddr_t`（`include/hal/types.h:13`、hal.h:613・625・639）と違う。前の review の m2 が未対応。
- m7. HFI の class の数: 5330 は CPUID.06H:ECX[15:8] = 4（Thread Director）。HAL が IA32_HW_FEEDBACK_CONFIG の bit 0 だけを立てれば表は class 0 だけ（`sdm-3b.txt:1848-1849`）。`feedback_classes`（diff:90）がどちらを返すか、bit 1 を立てるかを契約に書く。
- m8. design.md:119「IA32_HWP_CTL は bit 0 を 0 に保ち（reset の値、5330 は 0）」: 5330 の 0 は Linux が動いた後の読み（`read.txt` 0x776）で firmware の値ではない。「保つ」でなく「enable の後に読み、1 なら 0 を書く（logical processor の MSR なので CPU ごと、`sdm-3b.txt:748`）」と書く。
- m9. PERFORMANCE の min = guaranteed（design.md:141）: SDM §17.4.10（`sdm-3b.txt:1148-1149`）「If … Minimum_Performance is set higher than … Guaranteed Performance then notification of excursions to Minimum Performance may be continuous」。Guaranteed は動くので、書いた後に下がれば excursion の事象が毎秒出る。前の review の m9 が未対応。事象の抑えか min = efficient を書く。
- m10. p003 の「12 の directory の既定の答え」（design.md:292・320）は、既存の共通の file（`src/hal/idle-suspend-unsupported.c` の形）1 つで足りる。sparcv9 に触る前にユーザーに聞く（m10 と M10 の 61 行）。
- m11. UAPI（design.md:252-283）: flags の値が無い。`thermal_cpu_info.count` の意味（CPU の数か）が無い。RANGE の時の `step_count`（2 か）と `steps[0]`・`steps[1]` の決まりが曖昧。GET を誰に許すか（`/dev/thermal` の node の権限、WS303 の Q303-9/10 は root か wheel）が無い。flag の名前の接頭辞（`KERN_THERMAL_CPU_*`、WS303 V8 の `KERN_THERMAL_*`）を書く。ETIMEDOUT の後の「in force」が要求の値か効いた値か。大きさ 96・240・32 は正しい（末尾の表）。
- m12. p002 の「全 platform の build warning 0」（design.md:319）: `CONFIG_CPU_PERF` の既定が n なので、既定の build は cpuperf.c を compile しない。y の build を全 platform（か amd64 と 1 つの別の arch）で行うと書く。
- m13. ws.md が古い（AGENTS.md §5・§8 の本体と写し）: ws.md:125-137 は `/dev/system` を推し、ws.md:166-180 の Phase の表は p001〜p009（arm64・riscv64 の provider、`/dev/system` の p005）で状態の列が無い、ws.md:121 の範囲は `_PSS`＋PERF_CTL・`_CPC`・`_PSD`（Q301-3 で外れた）、ws.md:87 の Related は WS132（`/dev/system` の事象）。ws.md:192 は「第 2 版の Phase の表が本体」と書き、204-207 行は「§7 が本体」と書く。
- m14. design §0 の 1 行目（design.md:13）は原文でなく「ws.md の 5 つの要求」の要約。原文は ws.md:90-95 にあるので引用する（AGENTS.md §8）。
- m15. lock（design.md:198）: CPU ごとの箱の spinlock の rank が無い（`include/kern/lock.h:13-56`）。notify の handler の中（割り込み）で取るので irqsave の全部の取り手と、`sched_cpu_notify`（`src/kern/sched.c:1764-1794`、scheduler の lock を取る）の前に放す事を書く。HWP_STATUS の変化の事象を handler の中で見つけた時、WS303 の事象の ring に割り込みから出せるか（出せないなら後で出す）を書く。
- m16. 再起動と PM_ENABLE（hal-review.md:71）: M10 のとおり RESET だけが消す。zedBSD の reboot が ACPI の reset register 以外（KBC・triple fault）に落ちた時の振る舞いは未確認。
- m17. §4.9 の事実（design.md:182-185）: 「HWP は Skylake から」は Broadwell の Xeon D・E5 v4 が HWP の MSR を持つ（`sdm-vol4.txt:17522-17545`）ので不正確。「AMD の CPPC は Zen 2 の一部から」は出典が無い（APM は CPUID Fn8000_0008_EBX[27] の検出だけ、`apm-v2.txt:40400-40401`）→「推測」。BIOS の設定で HWP を CPUID から隠せるか（推測）も書く。推し A（枠だけ）は 5330 の WS の範囲として妥当（ただし改訂 1 でユーザーの Q301-8 の答えにより legacy の道を入れる形に変わった）。

## 確かめて正しかった主張

| 主張（design §2.3 など） | 確かめた物 |
| --- | --- |
| CPUID.01H:ECX[31] = 0（全 CPU） | `read.txt` `01:ecx=7ffafbff`（bit 31 = 0） |
| CPUID.06H:EAX = 0x00DF8FF7 の HWP・通知・window・EPP・pkg・Highest の変化・PECI・HFI・idle の兄弟・HWP_CTL・Thread Director | bit 7〜11・15・16・19・20・22・23 が立つ（`sdm-1.txt` Table 21-16）。bit 17・18 も立つ（m1） |
| ECX 0x409 = APERF/MPERF・EPB・class 4 | ECX[0]・[3]・[15:8] = 4 |
| EDX の HFI: 能力 3・1 page・行 0,0,1,1,2,2,2,2,3,3,3,3 | `read.txt` の edx（0x00000003〜0x00030003）、`sdm-1.txt` EDX[7:0]・[11:8]・[31:16] |
| 1AH: P 0x40000001、E 0x20000001 | `read.txt` |
| 1FH: SMT（type 1、shift 1）・core（type 2、shift 6）、sub-leaf 2 は invalid、module の level は無い | `read.txt` の 1f（ECX 0x100・0x201・0x2）。x2APIC ID P 0・1・8・9、E 0x10〜0x1E の偶数 |
| 0x771 P 56・21・20・1、E 33・12・15・1 | 0x01141538・0x010F0C21 の byte の分解 |
| 0x774 P 6・56・0・0、E 4・33・0・0、0x772 0x8000FF01、0x776 = 0、bit 42 = 0 → 論理 CPU の REQUEST だけ | `read.txt`、`sdm-3b.txt:748-786` Table 17-2（PLR 0・PKG_CTL 0 → REQUEST） |
| HWP_CTL の極性の読み | 同上 |
| 0xCE: 最大の非 turbo 25、最小 4 | 0x0804043DF0811900 の bit 15:8 = 0x19、bit 47:40 = 0x04 |
| MPERF 2494 MHz ≒ TSC | `spin.txt`、`sysfs.txt` の dmesg「2496.000 MHz TSC」 |
| 1 core の spin で P 4388・E 3292 MHz | `spin.txt`（ただし 1 点だけ、M1） |
| nominal_freq が P・E とも 1600、E の nominal_perf 12 | `sysfs.txt`。E の reference_perf 25 × 100 = 2500 が TSC に合うので「E に 1600 は当たらない」は独立の根拠でも支えられる |
| HFI の MSR 0x17D0・0x17D1 = 0 | `read.txt` |
| TjMax 100 °C | 0x1A2 = 0x80640000 の bit 23:16 = 0x64 |
| PL1 25 W・PL2 55 W・TDP 15 W | 0x610 の bit 14:0 = 0xC8、bit 46:32 の下 15 bit = 0x1B8、0x614 = 0x78、単位 0x606 の bit 3:0 = 3（1/8 W） |
| PM_ENABLE は package、R/W1Once、RESET だけが消す | `sdm-3b.txt:440-444` |
| HFI の bit 26 は sticky、消すまで次を書かない（写してから消す順は正しい） | `sdm-3b.txt:1916-1921` |
| UAPI 96・240・32 byte、`cpus` は offset 8 で 8 に並ぶ | 数え: 24 × 4 = 96、8 + 64 + 8 + 64 + 64 + 16 + 4 + 4 + 8 = 240、8 × 4 = 32（-m32 でも同じ並び） |
| ioctl の group 'T' は未使用 | `include/uapi/*.h` の `*_IOC_GROUP` と `_IO*('x'` の grep（B・G・f・s・R・h・F・t・A・E・H・S・c・g・L・b） |
| `git apply --check` が通る | f30cec278 で実行 |

## 確かめていない事（この review）

- CPUID.16H の max と HWP の Highest の関係を SDM・datasheet が定めるか（手元の資料には無い）。
- 5330 の CPUID.04H の L2 の値、0x775、firmware が起動の時に残す 0x770・0x776。
- KVM・他の hypervisor が CPUID.06H:EAX[7] を出すか。BIOS の設定で HWP を隠せるか。
- AMD の CPPC の世代、SV7 の世代。
- zedBSD の reboot の全部の道が RESET か。

## 改訂 1 への持ち越し（HEAD 099c3cc7e の design.md、概観だけ）

改訂 1 は HAL を変えず x86 の driver が CPUID・MSR を直に扱う形にした。上の指摘の行き先:

- **残る**: B1（改訂 1 の design.md:131・171 も CPUID.16H を MHz の元にし、Q301-2 の答えは SDM の警告なしで得た）、M1、M2、M3（driver で 0x775 を読む形に置き換えて残る）、M5（悪化: legacy の governor は 100 ms の周期で CPU ごとの APERF/MPERF を読む、design.md:210・219。notify で読めば S0 idle は 1 秒で spurious の上限に達する）、M6、M7、M8（Phase の番号が 1 つずれて p007 が受け入れ、WS303 p007 への依存は同じく無い）、M9（#GP は改訂 1 で「kernel が止まる」と明記、design.md:229。関門の誤りを試験が通さない事は同じ）、M12、m1〜m3・m8・m9・m11〜m15・m17（design.md:§4.9 の「Skylake から」は改訂 1 でも同じ）。
- **無くなる**: M4・M11・m4〜m7・m10（HAL の差分に固有）。M10 は hal-review.md の改訂を見ていないので未確認。
- **新しい食い違い（改訂 1）**: §0 の Q301-7 の解釈「WS301 は自分の policy の名前・起動の parameter を持たない」（design.md:20）と §4.12（design.md:241）に対し、改訂 1 は起動の parameter `cpuperf.hwp=off`（design.md:133・186-187・212・358）と `cpuperf=off`（design.md:229）を足した。試験の build だけに限るのか、ユーザーの決定の範囲かを決めて §0 か §4.2 を直す。
