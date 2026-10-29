<!-- awesome-plan project=zedbsd record=ws301-design-review -->

# WS301 設計の敵対的レビュー（design.md 第 1 版と HAL の差分の案）

日付: 2026-10-29 03:33 JST（host の `date`）。担当: design-reviewer（P5 の依頼）。対象: [design.md](design.md)（§0〜§10、Q301-1〜6）、
[proposed/hal-cpu-perf.diff](proposed/hal-cpu-perf.diff)、[ws.md](ws.md)。関係: [WS302 ws.md](../ws302/ws.md)・[survey-source.md](../ws302/survey-source.md)、[WS303 design.md](../ws303/design.md)。

照合した物: worktree `p5-design-b`（**HEAD は aecb7469c**。design.md・diff が書く基点 63386d8a4 はその親で、差は WS303 の設計の commit だけ）。design.md・diff・この文書は未 commit。
読んだ source: `include/hal/hal.h`、`src/hal/amd64/{smp.c,asm.c,task.c,bsp-pcat/acpi.c,bsp-pcat/acpi.h,bsp-pcat/cons.c,bsp-pcat/lapic.c}`、`src/kern/{sched.c,sleep.c,thread.c,system-event.c}`、
`include/kern/{thread.h,pmem.h,boot.h}`、`src/drivers/generic/system-device.c`、`include/uapi/system.h`、`src/drivers/acpi/acpi-event.c`（reset）、`config/kernel-options.list`、`src/hal/` の port の一覧。
firmware の事実: scratchpad の `ws301-acpi/ssdt2.dsl`、`ws303-acpi/dsdt.dsl`、**git にある `plan/ws049/tests/latitude5330/dynamic/ssdt16〜23.dat`**（scratchpad の新しい directory `ws301-review-dyn/` に写して `iasl -d` した）。
公開の資料（data として読んだだけ）: `ws301-fable/` の Intel SDM rev 093（`sdm-1.txt`・`sdm-3b.txt`・`sdm-vol4.txt`）、AMD APM 24593 r3.45（`apm-v2.txt`）、ADL datasheet 655258 rev 011（`adl-ds-vol1.txt`）、
FreeBSD `hwpstate_intel.c`（BSD-2）。GPL・LGPL の source は読んでいない。ACPI 6.5 の本文は無い（`acpi65.pdf` は 5.6 KB で本文でない）。
実行した物: `git apply --check`（diff の検査、何も当てていない）、UAPI と HAL の構造体の大きさを scratchpad の `ws301-review-size/s.c` で host の cc（`-m64`・`-m32`）で数えた。build・QEMU・実機の試験は未実施。

重大度: **blocker**（このまま実装すると起動・目的を壊す）、**major**（実装の前に設計か差分を直す要がある）、**minor**（実装の中で直せる・文書の整合）。「推測」は資料・source で確かめられなかった事。

## 要点

- **blocker 1 件**: (B1) SDM は「HWP の他の MSR は enable の前は触れない」と書くのに、差分の契約は `capabilities`・`get` に前提を書かず、状態遷移は PROBED（能力を読む）→ ENABLED の順。
  しかも amd64 の HAL には #GP を回復する MSR の読み書きが無い（`asm.c:299-322`）ので、§4.10 の「#GP なら HAL_ERR を返す」は実装できず、`CONFIG_CPU_PERF=y` 既定で全 amd64 機の起動が fatal になりうる。
- **major 17 件**: HFI の表の読みと bit 26 の消しの順の競合、HFI の表の形（class・capability の数）が HAL の契約に無く kernel が Intel の表の形を持つ、IA32_HWP_CTL の極性、PECI の上書き（Dell の EC）、
  ユーザーの要求 (2) の「電圧」の群が無い（ADL は IA core が 1 本の VCCCORE）、CPUID.1AH の core の種類に「速い・遅い」の意味を付けている（SDM が禁じる）、module の出し方と定義の矛盾、
  A と B の独立の承認・p002 の依存、CPU ごとの thread の仕組み（`sched_set_cpu` は呼び手 0・飢え・ETIMEDOUT の意味）、**動的な SSDT は git にあり** firmware が core の種類ごとの nominal の性能と周波数を持つ、
  APERF/MPERF の基準（SDM は TSC）、RAPL の読みに HAL の口が無い、§6.0 の Debian の読みが問いに答えられない、既定 y の他の機械と WS052 の回帰、HFI の相対の性能の意味と WS302 の要る物、
  **UAPI の domain の構造体は 232 でなく 240 byte**、policy の ioctl と起動の引数がユーザーの決定の外。
- **minor 13 件**: 差分が `git apply` できない、型と契約の文、S0 idle の「resume」、事象の class の mask、kernel の口の幅、記録の写し、決定の引用の形、AMD、試験の細部など。
- 確かめて正しかった主張も多い（末尾の表）。HWP の MSR 番号と bit、PM_ENABLE の一度きり、SMT の解き方、HFI の 16 + 8 byte（5330 の class 数なら）、`_OSC` を呼ばない事実と SSDT2 の `Load` の条件、80・32 byte、など。

## Blocker

### B1. enable の前に HWP の MSR を読む順と、#GP を回復できない HAL（design §4.8・§4.10・Q301-4、diff:105-138）

- 根拠:
  - SDM Vol 3B §17.4.2（`sdm-3b.txt:449-451`）「Additional MSRs associated with HWP may only be accessed after HWP is enabled, with the exception of IA32_HWP_INTERRUPT and MSR_PPERF.」
    IA32_HWP_CTL も同じ（`sdm-3b.txt:787-788`「If the IA32_PM_ENABLE[HWP_ENABLE] (bit 0) is not set, access to this MSR will generate a #GP fault.」）。AMD も enable の前は能力の register が 0（`apm-v2.txt:40465-40467`）。
    FreeBSD も「Many MSRs aren't readable until feature is enabled」と書き、enable してから caps を読み、全部 `rdmsr_safe`・`wrmsr_safe`（`hwpstate_intel.c` の `set_autonomous_hwp_cb`）。
  - diff:105-112 `hal_cpu_perf_capabilities` は「Read the current CPU's capabilities」で enable の前提も enable の前の答えも書かない。diff:132-138 `hal_cpu_perf_get` は Returns も前提も無い（`set` だけ HAL_ERR_STATE を書く、diff:122-127）。
  - design §4.8（`design.md:119-120`）「PROBED --enable--> ENABLED」: PROBED で何を読むかが無く、§4.5・§4.10 は enable の前に Highest などで判断する形に読める。Q301-4 (A) は `CONFIG_CPU_PERF=y` を amd64 の既定に、起動の時に enable。
  - `src/hal/amd64/asm.c:299-322`: `asm_read_msr`・`asm_write_msr` は素の `rdmsr`・`wrmsr`。#GP の fixup の表・`*_safe` は amd64 の HAL に無い（grep 0 件）。design §4.10 の表（`design.md:136`）「enable が効かない・要求の書きが #GP → HAL が HAL_ERR を返し、その CPU を ABSENT」はこの HAL では成り立たない。
- 起こる状況: (1) kernel が契約どおり `hal_cpu_perf_capabilities()` を enable の前に呼ぶ（Q301-4 (B) の「n の kernel は触らない」以外の全部、表示だけの読み、provider の probe）と #GP → kernel の fault → fatal。
  (2) CPUID.06H:EAX[7] を出すが MSR を実装しない hypervisor や、firmware の設定で MSR が拒まれる機械（推測、確かめていない）で、既定 y の kernel が起動ごとに止まる。5330 以外の amd64 の実機・QEMU の全部が回帰の対象。
  (3) `get` を enable の前に呼べば同じ。
- 直す案: (a) diff の契約に「enable の前は `capabilities` は flags と feedback の欄だけを返し levels は 0、`get` は HAL_ERR_STATE」を書く（実装は HAL の中で PM_ENABLE を読んでから他を触る。PM_ENABLE は CPUID.06H:EAX[7] で読める）。
  (b) 状態遷移を「PROBED（CPUID だけ）→ enable → 能力を読む → ENABLED」に直し、§4.5 の判断を enable の後に置く。(c) HAL の実装に #GP を回復する MSR の読み書き（例外の時に戻り先を持つ形）を p003 に足す（HAL の実装の変更で承認は不要、ただし LW を足す）。
  それが入るまで §4.10 の行を「#GP は fatal」と書き直す。(d) CPUID.01H:ECX[31]（hypervisor）が立つ時は provider を入れない既定にする（QEMU は §6.2 の回帰を変えない）。(e) Q301-4 の推しを、p006 の受け入れまで既定 n（5330 の試験の config だけ y）に変える案を並べる（下の M14）。

## Major

### M1. HFI の表を読む前に bit 26 を消す契約（diff:152-158、design §4.5・§4.9）と IA32_PACKAGE_THERM_STATUS の書き

- 根拠: SDM §17.6.6（`sdm-3b.txt:1918-1921`）bit 26 は sticky で「Once set, the hardware will not generate any further updates to this structure until the OS clears this bit by writing 0.」
  diff:152-158 `hal_cpu_perf_feedback_ready()` は「has written ... since the last call (and clears the indication)」— 1 回の呼び出しで見て消す。design §4.5（`design.md:101`）は「更新を周期で見て読み直し」。
  IA32_PACKAGE_THERM_STATUS の他の bit は R/WC0 の log と RO と reserved の混在（`sdm-3b.txt` §17.4.5 の図 17-10 と §17.8.5、Vol 4 Table 2-2）。
  SDM §17.6.5（`sdm-3b.txt:1861-1863`）「The OS may clear bit 0 only after receiving an indication ... that the structure initialization is complete」。
- 起こる状況: 消してから読むと、kernel が表を写している間に hardware が書き換え、header と行がちぐはぐの値（片方の CPU の古い性能）を読む。bit 26 を消す wrmsr が他の R/WC0 の log bit に 0 を書けば熱の log（PROCHOT・power limit の log）を消し、
  reserved に 1 を書けば #GP（B1 のとおり fatal）。起動の直後に HFI を止める道（失敗の後始末）で、初期化の完了を待たずに bit 0 を消すと SDM の言う競合。
- 直す案: HAL の口を「bit 26 を見るだけ」と「読み終えた後に消す」の 2 つに分けるか、HAL が表を写してから消す 1 つの口（M2 の案）にする。消す書きは「R/WC0 の bit は 1、bit 26 は 0、reserved は 0」の mask を HAL が持つ
  （Value(s) referenced: SDM Vol 4 Table 2-2 IA32_PACKAGE_THERM_STATUS）。enable の後の最初の bit 26 を待ってから止められる、を diff:140-150 に書く。

### M2. HFI の表の形が HAL の契約に無く、kernel が Intel の表の形を持つ（diff:75-84・140-158、design §2.2:48・§4.5・§4.11）

- 根拠: SDM §17.6.2（`sdm-3b.txt:1601-1640`）Thread Director のある CPU の表は「8 + (N+1) × (CP × CL + R8)」で、CL は CPUID.06H:ECX[15:8]、CP は EDX[7:0] の立つ bit の数。
  16 byte の頭 + CPU ごとに 8 byte（design §2.2:48）は CP × CL ≤ 8 の時（5330 の CL = 4 なら 8 byte、推測ではなく式から。CL の実値は §6.0 で未確認）だけ。EDX[31:16] は「may be same for multiple logical processors ... may not be contiguous」（`sdm-1.txt` Table 21-16）。
  diff の `struct hal_cpu_perf_capabilities` は `feedback_row`・`feedback_pages` だけで CL・CP・行の幅を返さない。design §4.2（`design.md:82`）は案 (b) を「kernel に x86 の register が入り、arch の境界が崩れる」で退けたが、
  §4.5 は kernel が Intel の表の byte の配置を解く。
- 起こる状況: kernel は CPUID を実行できない（MSR・CPUID は HAL の中だけ）ので行の幅を知れず、8 byte の決め打ちになる。CL が 4 より大きい CPU（後の世代、推測）で行がずれ、別の CPU の値を読む。
  bit 1（Thread Director）を立てるかも未定（diff も design も書かない）。
- 直す案: HAL が表（`hal_pmem_alloc` で HAL が持つ）と解き方を持ち、`hal_cpu_perf_feedback_read(cpu, &performance, &efficiency)` の形で CPU ごとの class 0 の 2 つの値を返す（kernel は Intel の表の形を知らない）。
  それが重ければ capabilities に `feedback_stride`・`feedback_header` を足す。IA32_HW_FEEDBACK_CONFIG は bit 0 だけ（Thread Director は入れない）と明記。

### M3. IA32_HWP_CTL の極性を見ていない（design §4.3:89、§2.2:42）

- 根拠: SDM §17.4.4.4（`sdm-3b.txt:748-788`、Table 17-2）IA32_HWP_CTL[0]（PKG_CTL_PLR）= 1 の時は意味が逆になり、**bit 42 = 0 で REQUEST_PKG が使われる**。CPUID.06H:EAX[22] で有無。design §2.2 は bit 22 を「HWP_CTL」と挙げるが、§4.3 は「bit 42 を使わない」だけ。
- 起こる状況: firmware が PLR = 1 にしていると（初期値は 0 だが firmware の設定は未確認、推測）、kernel が書く IA32_HWP_REQUEST は無視され、全 CPU が REQUEST_PKG（firmware の値か 0）に従う。§6.3 の「設定に従う」が成り立たず、原因が見えない。
- 直す案: HAL の enable の後に（PM_ENABLE の後でないと #GP）0x776 を読み、PLR = 1 なら bit 42 を 1 にして thread の MSR を選ぶ（HAL の実装）。§6.0 で 0x776 を読む（CPUID の bit 22 が立つ時だけ）。

### M4. PECI の上書き（Dell の EC）と WS303 の thermal mode（design §4.3・§6.3、受け入れ 2）

- 根拠: SDM §17.4.4.3（`sdm-3b.txt:672-746`）「PECI initiated settings take precedence over the relevant fields ... irrespective of the Package Control bit」、Min・Max・EPP を EC が上書きでき、IA32_HWP_PECI_REQUEST_INFO（0x775）と STATUS の bit 4・5 で分かる（CPUID.06H:EAX[16]）。
  5330 の fan と mode は EC と SMM が持つ（WS303 design §2）。design は PECI に触れない。diff の `HAL_CPU_PERF_EVENT_*` に PECI の入り・出が無い。
- 起こる状況: Dell の thermal mode（Quiet・Cool）が EC から Max か EPP を上書きすると（推測、確かめていない）、§6.3 の「domain の max に APERF/MPERF が従う」が mode 次第で通ったり落ちたりし、zedBSD の誤りと見分けられない。
- 直す案: §6.0 で 0x775 と CPUID.06H:EAX[16] を読む。HAL の `get` か capabilities に「上書きされている field の mask」を足す（diff の変更）。§6.3 は WS303 の mode を固定して測り、mode を記録する（WS303 と同じ日に重ねない、WS303 design:31）。

### M5. ユーザーの要求 (2)「電圧や周波数を共有している群」の電圧が無い（design §0.4・§4.3、UAPI `domain` 1 つ）

- 根拠: ws.md:11「どのコア群が電圧や周波数のを共有しているかのグルーピング」。ADL の datasheet 655258 rev 011（`adl-ds-vol1.txt:10881`）「VCCCORE — Processor IA Cores and Ring power rail」: P-core と E-core は 1 本の電源。
  design §4.3 は「物理 core を 1 domain」（周波数の群）だけで、UAPI（`design.md:177-178`）は `domain` 1 つ。JH7110 は 1 clock と 1 電源（design §2.2:54）、Pi は 1 clock で一致するが、5330 では電圧の群（package 全体）と周波数の群（core か module）が違う。
- 起こる状況: 5330 で「電圧を共有する群」を問う利用者（WS302 の電力の判断、ユーザー）に、周波数の群を電圧の群として返す。受け入れ 1 の「domain（共有の群）」が要求の半分しか示さない。
- 直す案: UAPI と kernel の表に「周波数の群」と「電圧の群」を分けて持つ（`frequency_domain`・`voltage_domain`、x86 の電圧は package、出典は datasheet の block に Value referenced）。分けない場合はユーザーに Q として聞く（要求の解釈なので担当の判断でない）。

### M6. CPUID.1AH の core の種類に「速い・効率の良い」の意味を付けている（diff:25-27、design §2.2:47）

- 根拠: SDM Vol 1 Table 21-63（`sdm-1.txt:27767-27773`）「The core type may only be used as an identification of the microarchitecture ... This field neither implies nor expresses any other attribute to this logical processor and software should not assume any.」
  diff:26-27 `HAL_CPU_CORE_PERFORMANCE`「the faster kind」・`HAL_CPU_CORE_EFFICIENT`「the slower, more efficient kind」を HAL が 1AH から決める（design §4.1:75）。design §2.2:47 は「値の大小に意味は無い」と書きながら、diff は意味を付ける。
- 起こる状況: Intel が新しい種類（Lunar Lake の LP E-core、Vol 4:18265 の世代など、推測）を出した時に、HAL が「速い・遅い」を誤って答え、WS302 が逆に割り付ける。SDM の禁止に反する契約を HAL の API に固定する。
- 直す案: HAL は「microarchitecture の種類の番号（同じなら同じ）」だけを返す（`core_type` を opaque の ID に）。速い・遅いは kernel が HFI か HWP の Highest の比で決める（相対の性能と同じ源）。WS302 は「性能の順位」と「効率の順位」を受ける（M15）。

### M7. module の出し方と定義の矛盾（diff:14-22、design §4.1）

- 根拠: diff:16-17 module を「a group of cores sharing a cache level below the package, such as four efficient cores on one L2」と定義、diff:20-21「A level the machine does not report has the identifier of the level above it (a CPU without modules has module == core)」— core は module の**下**の level なので文が矛盾。
  design §4.1:75 は CPUID.1FH（無ければ 0BH）から module を取る。SDM の 1FH の module domain（type 3）は cache の共有と定義されていない（Table 21-70、`sdm-1.txt:27975-`）。ADL の 1FH が module の level を出すかは資料に無い（**推測: 出さないかもしれない**、§6.0 で `cpuid -r` を読めば分かる）。
  identifier が package をまたいで一意か（core の ID が package ごとか全体か）が書かれていない。design §4.3 の「topology の core が同じ CPU」は全体で一意を前提にする。
- 起こる状況: 1FH に module が無いと E-core の 4 core の群が作れず、§4.3 の「module を domain にする」が実装できない。複数 package の機械で core の ID が package ごとなら別 package の core が 1 domain になる。
- 直す案: diff の文を「level above」でなく「the identifier of the next lower reported level」に直す（実装と一致させる）か、module の定義を「L2 を共有する core の群」にして出し方を CPUID.04H（cache の共有の数）と明記。ID は「package・die・module・core とも system 全体で一意」と契約に書く。

### M8. A と B は「別々に承認できる」でない、p002 の依存、全 port（diff:3-5、design §5.4・§7）

- 根拠: diff:3-5「Either may be approved alone」。B の `hal_cpu_perf_enable` は「applies to the whole package」（diff:115-117）、`hal_cpu_perf_feedback` は「the current CPU's package」（diff:141）で、kernel は package ごとに 1 CPU を選ぶのに A（package の ID）が要る。
  design §7:246 p002（枠組み・domain の組み立て）の依存は p001 だけ、受け入れは「全 platform の build warning 0」。§4.1:76「kernel は `hal_cpu_topology` で表を作る」は A が hal.h に入るまで build できない。
  `src/hal/` は amd64・arm64・armv7・i386・m68k・mips・ppc32・ppc64・riscv32・riscv64・sparcv9・x86 の 12 の directory。§5.4:219「全 arch に入る宣言」に対し p003 の受け入れ（`design.md:247`）は「amd64 の build」だけ。
- 起こる状況: B だけ承認されると x86 の provider が package を決められない。p002 を p003 の前に始めると、HAL の口を呼べず枠組みの domain を組めない（stub で組むなら p003 の後に書き直し）。p003 で amd64 だけ作ると、他の port の link が kernel の呼び出しで落ちる。
- 直す案: diff の頭を「B は A を前提にする。A だけは承認できる」に直す。p002 の依存に「p003 の A」を足すか、p002 は HAL を呼ばない（host の試験の topology の表を入れる口だけ）と明記。
  p003 は既存の `src/hal/idle-suspend-unsupported.c`・`cpu-up.c` と同じ形の共通の既定の file で全 port に答えを入れ、受け入れを「全 platform の build」にする。p003 の LW（4）に B1 (c) の MSR の回復を入れて見直す。

### M9. CPU ごとの thread の仕組み（design §1:23・§4.2:83・§4.9・§4.10:138）

- 根拠: `sched_set_cpu`（`src/kern/sched.c:1665-1758`）は **sched.c の外にも中にも呼び手が無い**（grep 0 件、WS302 survey-source.md:22 も同じ）。THREAD_NEW の thread に使えるか（`hal_task_transfer` が一度も走っていない task を受けるか）は確かめた試験が無い。
  sched は厳密な priority で aging が無い（survey-source.md:16・56）。design は cpuperf の thread の priority を書かない。§4.10:138「ioctl は 1 秒で ETIMEDOUT、要求は queue に残る」。§6.2 の QEMU は HWP が無いので thread を作らない形しか通らない。
- 起こる状況: (1) 最初の使い手として未試験の道に当たり、5330 で初めて壊れる（QEMU・host の試験では出ない）。(2) 高い priority の thread（WS302 の nice → priority の後）が CPU を占めると cpuperf の thread が走れず、設定が ETIMEDOUT で返った後に**遅れて効く**（利用者は失敗と見たのに設定は変わる）。
  (3) policy を 12 の CPU に配る間 `cpuperf_mutex` を持って最大 12 秒待つ（1 秒が CPU ごとか全体か未定）。
- 直す案: thread をやめ、既存の `hal_cpu_notify()` で対象の CPU を起こし `kernel_cpu_notify_handler()` の中で CPU ごとの「待っている要求」を HAL に書く形を比べる（HAL の口は今のまま、CPU 間の呼び出しを足さない、priority の飢えが無い）。
  thread を残すなら priority を最高に、ETIMEDOUT の時は要求を queue から外す（返した結果と状態を一致させる）、QEMU で HWP の無い時も偽の provider で CPU ごとの thread と `sched_set_cpu` を通す試験を §6.2 に足す。

### M10. 動的な SSDT は git にあり、firmware が core の種類ごとの nominal の値を持つ（design §2.1:35・§2.2:46・§4.7・§6.0:226・§10.3）

- 根拠: `plan/ws049/tests/latitude5330/dynamic/ssdt16〜23.dat` が git にある（README の「FACP・LPIT と動的な SSDT（ws052-p002）」の表、Cpu0Psd・ApPsd・Cpu0Hwp・ApHwp ほか）。design §2.1:35「動的な表の中身は今の dump に無く」は誤り。
  `ssdt19.dat`（Cpu0Hwp）の `GCPC`: `INFS` が立つ時、Nominal Frequency に `NMFQ`、Nominal Performance と Reference Performance に big core なら `SFBC`・`RFBC`、それ以外は `SFSC`・`RFSC`（`CORE` の bit が CPU の番号ごとに big か）を入れる。
  これらは静的な SSDT2 の field（`ssdt2.dsl:306-312`、`CORE 32・SFSC 16・SFBC 16・NMFQ 16・INFS 8・RFBC 16・RFSC 16`）で、`_OSC` を呼ばなくても namespace にある（zedBSD は SSDT を load する、`src/drivers/acpi/acpi-tables.c`）。
  `ssdt18.dat`（Cpu0Psd）の `GPSD`: CFGD の bit 25 と 22 が立つ時、_PSD は「domain = CPU の番号、数 1、HW_ALL（0xFE）」（CPU ごとの群）、他は CMxx の nibble から。
- 起こる状況: 設計が「MHz/単位は §6.0 で測る」「E-core の module の共有は不明」とした問いの一部に、firmware が既に答えを持つのを見ずに、測りの手順（M13、判別できない形）に頼る。
- 直す案: §2.1 を直し、zedBSD の AML で `\_SB.CFGD`・`CORE`・`SFBC`・`SFSC`・`NMFQ`・`INFS` を読む（評価だけ、`_OSC` は呼ばない）案を §4.7 と Q301-2 に加える。firmware の値は Dell の著作物の data として「Value referenced: 5330 BIOS 1.31.1 SSDT CpuSsdt の field 名」で引く。
  CFGD の値は 5330 の今の dump で静的には決まらない（NVS の中、推測）ので、§6.0 か zedBSD の起動の log で値を読む。

### M11. APERF/MPERF の基準と「設定に従う」の判定（design §4.7:114、受け入れ 2、§6.3、diff:83・94-96）

- 根拠: SDM Vol 1 Table 21-16 CPUID.06H:ECX[0]（`sdm-1.txt:25905-25910`）「as a percentage of the expected processor performance when running at the **TSC frequency**」。
  SDM §17.2（`sdm-3b.txt:120-127`）「MPERF increments in proportion to a fixed frequency, which is configured when the processor is booted」「Only the IA32_APERF/IA32_MPERF ratio is architecturally defined」。
  design §4.7 は「比 × MSR_PLATFORM_INFO の最大非 turbo の比 × 100 MHz」。diff:96 `reference` は「counts at the reference rate」、diff:83 `reference_khz` の出し方は未定。
- 起こる状況: TSC の周波数と PLATFORM_INFO の比 × 100 MHz が 5330 で違えば（推測、確かめていない）、P-core・E-core の MHz の表示と受け入れ 2 の数がずれる。受け入れ 2 の閾値（何 % 以内で「従う」か）が無いので、ずれても合否が決まらない。
- 直す案: 基準は TSC（zedBSD の timecounter の測った周波数）とし、PLATFORM_INFO は表示の比べだけに。§6.0 で Debian の turbostat の Bzy_MHz（同じ spin）と照らし、§6.3 の判定に「設定の max の MHz 換算 ±x%」の数を p006 の前に決める。
  `hal_cpu_perf_get` で APERF と MPERF を割り込みを止めて続けて読む、を契約に書く。

### M12. RAPL の電力の読みに HAL の口が無い（受け入れ 2、design §6.3:239、ws.md:74）

- 根拠: MSR は HAL の中だけ（ws.md:32、`asm.c`）。diff に RAPL（MSR_PKG_ENERGY_STATUS、Vol 4）の口が無い。design §6.3「zedBSD に無ければ p006 で足すか Debian との比」。
- 起こる状況: p006 で RAPL を足すと HAL の API の変更で承認が要り、Phase の途中で止まる。Debian との比は別の OS の idle の電力なので、zedBSD の powersave の効果を示さない。
- 直す案: diff に (C) として「今の CPU の package の energy の counter と単位」の口を足して一緒に承認を求めるか、受け入れ 2 の電力の部分を「外の計測（battery の放電の速さ、ACPI の `_BST` の rate、WS052 の道具）」に変える。どちらかを §8 の Q に足す。

### M13. §6.0 の Debian の読みが問いに答えられない（design §6.0:225-227・§10.1-2）

- 根拠: §10.1「PM_ENABLE の今の値（firmware か Linux が enable 済みか）」— Debian の intel_pstate（HWP の mode）は自分で PM_ENABLE を立てるので、Debian で読んだ 1 は firmware の状態を示さない（PM_ENABLE は RESET まで消えない、`sdm-3b.txt:441-444`）。
  §6.0:227 E-core の module の共有:「1 つの E-core で spin、同じ module の他の E-core と別の module の scaling_cur_freq を比べる（設定は変えない）」— 走っていない（C-state の）E-core の scaling_cur_freq・Bzy_MHz は共有の clock を示さない。
  `rdmsr 0x772` は CPUID.06H:EAX[11] が 0 なら失敗する。
- 起こる状況: 5330 を割り当てても §10.1・§10.2 が埋まらず、p004 の「module を domain にするか」が決まらない。
- 直す案: firmware の状態は zedBSD の起動の時に HAL が CPUID の後に 0x770 を読んで log 1 行（変更の無い読み）にするか、Debian を `intel_pstate=disable` で起動して読む（Debian の設定の変更なので Q1 の許可）。
  module の共有は、2 つの E-core の両方を走らせ、片方の max を下げた時にもう片方の APERF/MPERF が下がるか（設定を変えるので許可が要る）か、M10 の firmware の `_PSD` で答える。0x772 は CPUID を見てから読む。

### M14. 既定 y の起動の enable が他の amd64 機と WS052 に効く（Q301-4、design §4.2:84、§6）

- 根拠: Q301-4 (A)「amd64 の既定 y、起動の時に enable」。enable の後は IA32_PERF_CTL が無視され（`sdm-3b.txt:441-442`）、既定の request は Min = Lowest・Max = Highest・EPP 0x80（`sdm-3b.txt:563-581`）で turbo まで上がる。
  今の zedBSD は firmware の置いた P-state のまま（ws.md:29）。試験の機械は 5330 のほか 5320・13400F・SV7 など（共有の memory の Test hosts）。§6 の試験は 5330 と QEMU だけで、WS052 の S0 idle（`src/kern/sleep.c:156-200`）を HWP の enable の後に回さない。
- 起こる状況: 5330 の受け入れの前に、他の WS が使う全 amd64 の実機で電力・発熱・fan の振る舞いが変わる（回帰の比べの基準が動く）。HWP の enable の後の S0 idle の深さ・wake は誰も確かめない（影響が有るかは推測、確かめていない）。
- 直す案: p006 の受け入れまで既定 n、5330 の試験の config だけ y。§6.3 に KERN_SYSTEM_SLEEP（S0IDLE）の 1 回と WS052 の判定を足す。既定 y にする時は 5320（HFI 無しの uniform の道、推測: Tiger Lake）と 13400F（hybrid）の素の起動の回帰を足す。

### M15. HFI の相対の性能の意味と WS302 の要る物（design §4.5・§4.10:137・§5.1、Q301-2）

- 根拠: SDM §17.6.3・17.6.7（`sdm-3b.txt:1937-1950`）HFI の値は「Change in core frequency, voltage, or power budget」で動き、0 は「走らせるな」、SMT では「hardware zeroes one of the core's logical processors only」。
  design §4.5 は `100 × perf(i) / max(perf)` を 1 秒ごとに計算し変われば事象。ユーザーの要求（ws.md:14）は「一番速いコアを100としたときのコアごとの性能」。WS302（ws.md:15・25-27）は「遅くて消費電力の低い CPU」を選ぶ。
  §5.1 の `kern_cpuperf_cpu` の `efficient` は HWP の Most Efficient の level で、HFI の energy efficiency の値ではない。kernel の利用者への変化の通知（callback か世代の数）が無い。
- 起こる状況: (1) P-core が熱で絞られると E-core が 100 になり、WS302 が 1 秒ごとに割り付けを反転しうる。(2) 全 CPU が 0（初期化の前・force idle）で 0 除算。(3) P-core の片方の SMT の thread だけ 0 になり「core ごとの性能」が core の中で食い違う。
  (4) WS302 は効率の順位を得られない。(5) 1 秒ごとの小さな揺れで事象が溢れ（ring 64、`system.h` の KERN_SYSTEM_EVENT_QUEUE）、OVERFLOW になる。
- 直す案: 「静的な相対の性能（enable の直後の表か、熱の制約の無い時の値）」と「今の相対の性能」を分けるかをユーザーに聞く（要求の解釈）。max が 0 の時は unknown。core ごとの値は SMT の兄弟の大きい方。
  効率の値（HFI の byte 1 の相対）を `kern_cpuperf_cpu` と UAPI に足す。kernel の利用者には世代の数を出す。事象は丸めた値が 2 回続けて変わった時だけ、などの抑えを書く。

### M16. UAPI `struct system_cpu_perf_domain` は 232 byte でなく 240 byte（design §5.2:186-198）

- 根拠: 4 + 4 + 8×8 + 4 + 4 + 4×16 + 4×16 + 4×4 + 4 + 4 + 4×2 = 240。scratchpad の `ws301-review-size/s.c` を host の cc で `-m64`・`-m32` とも数えて 240（`system_cpu_perf_info` 80、`system_cpu_perf_setting` 32、`system_cpu_perf_policy` 16 は主張どおり）。
- 起こる状況: 既存の UAPI の形（`system.h` の `_Static_assert(sizeof(...) == N, "... ILP32 and LP64")`）に 232 を書くと build が落ちる。user の道具が 232 を前提にすると ioctl の size の encode が kernel と食い違う。
- 直す案: 240 に直す（`reserved[2]` を残すなら 240、232 にしたいなら `reserved` を 0 にする）。offset の `_Static_assert`（`cpus` が 8 に並ぶ）も書く。

### M17. policy の ioctl と起動の引数はユーザーの決定の外、SET_POLICY の権限が無い（design §4.4・§5.2:206-209・§5.3）

- 根拠: ユーザーの決定（ws.md:18）は「設定は domain ごとの min・max・desired・EPP、設定できるのは root だけ」。design は `KERN_SYSTEM_GET/SET_CPU_PERF_POLICY`、`CUSTOM`、起動の引数 `cpuperf=performance` を足す。UAPI の変更はユーザーの明示の決定（`plan/master-design-policy.md:189`、ws.md:53）。
  §5.2 は SET_CPU_PERF_DOMAIN にだけ「root」を書き、SET_POLICY の権限を書かない。§8 の Q に policy の UAPI が無い。
- 起こる状況: 決定されていない UAPI を p005 で入れ、後で消すと ABI の変更になる。SET_POLICY を誰でも呼べると、決定（root だけ）に反して session の user が全 domain の設定を変えられる。
- 直す案: §8 に「policy を UAPI に出すか（出すなら SET は root）、起動の引数を足すか」の Q を足し、決まるまで p005 は domain の設定だけ。SET_POLICY を出す時は root と明記。

## Minor

- m1. diff が当たらない: `git apply --check` が「corrupt patch at line 163」。hunk の頭 `@@ -232,6 +232,140 @@` に対し、旧の context は 4 行、足す行は 148 行（diff:9-162）。基点も 63386d8a4 と書くが worktree の HEAD は aecb7469c（hal.h は同じ）。承認に出す前に `diff -u` で作り直し、SHA256 を付ける（guardrail の承認済みの表の形）。
- m2. diff の型と文: `hal_cpu_perf_feedback(uint64_t physical_address)` は hal.h の他の口と同じ `hal_physaddr_t` に。`hal_cpu_perf_get` に Returns の文が無い。`window_us` は SDM の 7 bit の仮数と 10 の冪（`sdm-3b.txt:584-588`）で表せない値の丸めを書く。
  EPP の無い CPU は SDM が IA32_ENERGY_PERF_BIAS を使う（`sdm-3b.txt:581-583`）ので「ignored」でなく HAL が EPB に写すかを書く。HWP_STATUS を `get` が消すなら読み手が 1 つ（cpuperf の周期だけ）と契約に書く。
- m3. S0 idle の「resume」（design §4.8:122・§4.10:139）: FreeBSD の resume の入れ直し（`hwpstate_intel.c` の `intel_hwpstate_resume`）は S3 の物で、zedBSD は S0 idle だけ（`system-device.c:345`）。S0ix で HWP の MSR が残るか（推測: 残る）と、
  hook を置く所（`kern_sleep_s0idle` の resume の並び、`sleep.c:186-199`、今は CPU の hook が無い）を書く。周期の読みは S0 idle の間に tick が止まる（hal.h:221-225）ので止まる事を書く。
- m4. 事象の class: `KERN_SYSTEM_EVENT_CPU_PERF 0x200` を足すなら `KERN_SYSTEM_EVENT_CLASSES`（`system.h:329`、今 0xff、WS303 で 0x1ff の案）を 0x3ff にする事を書く（`system-event.c:193` が mask で拒む）。subject・value・detail の中身も決める。
- m5. kernel の口の幅: `kern_cpuperf_limit(..., uint8_t maximum)`・`kern_cpuperf_cpu` の `uint8_t` は HWP の 0〜255 に縛られ、§4.3 の「provider の抽象の単位」・JH7110 の OPP（MHz）と合わない。`struct kern_cpuperf_setting` が §5.1 に定義されていない。
  WS302 が sched の lock を持ったまま `kern_cpuperf_cpu` を呼ぶなら、cpuperf の spinlock との lock の順（sched → cpuperf、cpuperf の lock の中で thread を起こさない）を書く。`kern_cpuperf_limit` を呼べる文脈（ACPI の Notify の handler から mutex を取れるか）を書く。
- m6. 記録の写し: design §7（p004 5・p005 3・p007 3・p009 2、合計 24/32）と ws.md の表（6・4・4・3、31/40）、ws.md:40・92 の `_PSS`＋PERF_CTL の fallback と `_PSD` の domain は design で落ちた（§4.10 は「HWP 無し → provider 無し」）。
  落とした事を §4.11 に書き、ws.md の表を同じ commit で直す（本体と写し）。
- m7. ユーザーの決定の引用: design:7 は「」で囲むが、ws.md:18 は「ユーザー（クリック）: …」のクリックの答えの要約で原文の形でない。「（ws.md:18 のクリックの答えの記録）」と書き、原文と分ける。Q301-5 の「枠だけ」の解釈は解釈と書いてあり良い。
- m8. AMD: diff (B) と §0.1 は AMD CPPC も HAL に入れるが、試験の機械も §6 の項も無い。AMD でも enable の前は能力が 0（`apm-v2.txt:40465`）、CAP1 の並び（Highest 31:24 … Lowest 7:0）は Intel の CAPABILITIES（Highest 7:0 … Lowest 31:24）と逆。p003 から外すか、試験の対象を置く。
- m9. PERFORMANCE の `min = guaranteed`（§4.4:96）: SDM §17.4.10（`sdm-3b.txt:1148-1149`）Min が Guaranteed より上になると excursion の通知が続く。Guaranteed は動く（`sdm-3b.txt:491-492`）ので、書いた後に Guaranteed が下がると続く。min は書いた時の Guaranteed より下か Most Efficient に。
- m10. §6.3 と p004 の受け入れ: p004 の受け入れ「5330 で §6.3 の表」は p005（UAPI と道具）に依存しない p004 では読めない（klog か host の dump で示すと書く）。受け入れ 1 の「Debian の値と矛盾しない」の Guaranteed・HFI は動く値なので、比べる field（Highest・Lowest・core の種類・CPU の数）と許す幅を決める。
- m11. HFI の表の止め: kernel の寿命の間は解放しない（§4.9）は良い。reboot は ACPI の reset register（`acpi-event.c:1129-1133`）で RESET なので PTR が残らない、を書いておく（SDM: PTR・CONFIG は INIT では残る、`sdm-3b.txt` §17.6.4・17.6.5）。
- m12. CPUID.06H:EAX[14]（Turbo Boost Max 3.0）を §2.2 の bit の列に足す。uniform の CPU でも P-core ごとに Highest が違い（favored core）、§4.5 の「uniform なら Highest の比」の結果が 100 未満の core を持つ事を書く（正しい振る舞いだが WS302 への意味が要る）。
- m13. 調べの道具の記録: §2.2 の「Fable、2026-10-29」の調べは、Q1 の運用（Fable はユーザーが頼んだ時の助言の subagent だけ）と合うかを Q1 が確かめる（推測: 頼まれた物かもしれない。この review では判定しない）。
  GPL の project の commit の散文の値（78.741 kHz）を設計の文書に残すのは良いが、code・comment に入れない事を §9 に書く（guardrail は GPL の値も出典つきで使ってよいとするが、design は使わないと決めた。その決定と整合）。

## 確かめて正しかった主張

| 主張（design・diff） | 確かめた物 |
| --- | --- |
| CPU の周波数・HWP・HFI の code が無い、`_OSC`・`_PDC` を呼ぶ code が無い（§1:19・24） | `grep` で 0x770・HWP・0x17D0・`_OSC` が 0 件（`_OSC` 似の文字は rpi4-audio・rpi5-fan・pci-vmd の別の語） |
| HAL の CPU の口と行（§1:20）、`struct amd64_acpi_cpu { apic_id }`（acpi.h:23-25）、BSP を 0 に密な番号（smp.c:44-99）、cons.c の 1FH/0BH は x2APIC の ID だけ（cons.c:579-616） | source |
| `sched_set_cpu` は走っていない thread だけ（sched.c:1665-1686） | source（ただし呼び手 0 件、M9） |
| SSDT2 の `SSDT` の package（`ssdt2.dsl:92-120`）、`GCAP` の `Load` の条件: HWP の表は OSYS ≥ 0x07DF・CFGD bit 22・`\_SB.OSCP & 0x40`（`ssdt2.dsl:400-470`） | firmware の逆アセンブル |
| `\_SB._OSC` は CAP0 を OSCP に写し、0x04 と USB4（`U4FN`）を扱う（`dsdt.dsl:28362-28400`）、PR00〜PR63 は ACPI0007（`dsdt.dsl:30494-`） | 同上 |
| PM_ENABLE 0x770 は package、bit 0 は R/W1Once で RESET まで消えない | `sdm-3b.txt:438-454` |
| CAPABILITIES 0x771 の 4 byte の並び、REQUEST 0x774 の Min・Max・Desired・EPP・Window・bit 42・valid 59〜63、0x772・0x773・0x777 | `sdm-3b.txt:470-660`・`792-860` |
| SMT の兄弟は 1 つの周波数で両方の REQUEST を見る、bit 20 で idle の兄弟を無視、周波数は package 全体か物理 core ごと | `sdm-3b.txt:987-1004` |
| OS の推奨（Desired 0・Window 0・Min = Lowest・Max = Highest、Min = Max は HWP の迂回） | `sdm-3b.txt:1118-1137` |
| CPUID.06H:EAX の 7〜11・15・16・17・18・19・20・22・23 の意味 | `sdm-1.txt` Table 21-16 |
| HFI: EDX[7:0] 能力・[11:8] page 数 − 1・[31:16] 行、PTR 0x17D0・CONFIG 0x17D1（package）、0 は「走らせない」、bit 26 の更新、止める時は bit 26 を待つ | `sdm-3b.txt:1511-1935`、`sdm-1.txt` Table 21-16（16 + 8 byte は CP × CL ≤ 8 の時、M2） |
| CPUID.1AH の 0x20 Atom・0x40 Core、値の大小に意味が無い、1FH の level の型 | `sdm-1.txt:27748-27773`・`27975-` （ただし diff はそれに反する、M6） |
| AMD: Fn8000_0008_EBX[27]、C001_02B0/02B1/02B3、ENABLE は package で一度きり | `apm-v2.txt:40400-40470` |
| FreeBSD hwpstate_intel は hybrid で package の制御を切る（最後の core で package の周波数が決まる恐れ）、resume で入れ直す | `hwpstate_intel.c`（BSD-2） |
| `_CPC` は 23 の要素（NumEntries と Revision 3 を含む）、_PSD に 0xFC と 0xFE を使う | 5330 の `ssdt19.dat`・`ssdt18.dat`（firmware の事実。ACPI 6.5 の本文は未確認のまま） |
| UAPI `system_cpu_perf_info` 80 byte、`system_cpu_perf_setting` 32 byte、cpus[8] が HAL_CPU_MAX 512 と一致 | host の cc（`-m64`・`-m32`）、hal.h:120 |
| ioctl 22〜24（WS303）と 25〜29（WS301）は今の 1〜21 と重ならない、WS303 の事象の class は 0x100 で 0x200 と重ならない | `system.h:272-464`、WS303 design:233・376 |
| `kern_pmem_alloc` は整列つきの物理の RAM の確保、`kern_pmem_to_kernel` は direct map | `include/kern/pmem.h:40-62` |
| HWP の enable の後に Debian（再起動）で困らない: reset は ACPI の reset register | `acpi-event.c:1129-1133`、`sdm-3b.txt:441-444` |

## 確かめていない事（この review）

- 5330 の CPUID.06H・1AH・1FH・04H の実値、0x770・0x776・0x775 の値、`\_SB.CFGD`・`INFS`・`NMFQ` などの実行時の値（M3・M4・M7・M10・M13 の前提）。
- KVM・他の hypervisor が CPUID.06H:EAX[7] を出すか（B1 の (2)）。S0ix で HWP の MSR が残るか（m3）。5320 の CPU の世代（M14）。
- ACPI 6.5 の `_CPC`・`_PSD` の節（本文が無い）。
