<!-- design-reviewer のレビュー 1（2026-10-19、ws273 design.md 版 1）。Q1 が報告の本文をそのまま置いた -->

## WS273 p001 設計のレビュー 1

対象: `/home/awe/zedBSD-claude1/plan/ws273/design.md`（版 1）、`plan/ws273/ws.md`、`plan/ws273/phase001/phase.md`。
既存の MIPS HAL（`src/hal/mips/`、`platform/mips32-malta/`）、`plan/ws271/ws.md`・`plan/ws272/ws.md`、`plan/guardrail.md` と照らした。外部の出典は `gh api` で取得して読んだ（Linux master、OpenDingux `jz-6.1`、UBIBoot master、buildroot）。取得物は scratchpad の `/tmp/claude-1000/-home-awe-zedBSD-claude1/70c47280-c9e2-5e3f-b5d9-072cd0539dfc/scratchpad/src/` に置いた。file は編集していない。build・QEMU は実行していない（設計のレビューなので不要）。

**合っていた事実**: SoC の address と INTC の番号（OST を除く）、memory の 2 bank、UART2 の 57600 と pin（PC28・PC30）、panel の timing（6.0 MHz、htotal 400 × vtotal 250 で 60 Hz）、backlight の PE1、alt key の PE28、PRId の D1、PageCtrl の `0xa9000000`、Count が無いこと、I cache の VTAG、Kconfig（R2・SCACHE・HIGHMEM）、`LD_ADDR`、argv の形。

---

### Blocker

**B1. UBIBoot は Cause.IV=1 のまま kernel に入る。HAL には EBase+0x200 の割り込み vector が無い（§1.3・§2.2）**
- 根拠:
  - UBIBoot `src/head.S` は `li $9, 0x00800000; mtc0 $9, $13` で Cause に bit 23（IV）を立てる。同じ所で Status = `0x00400004`（BEV=1、ERL=1）にする。
  - zedBSD の `src/hal/mips/vectors.S` が置く vector は、refill（0x000）・0x080・cache error（0x100）・general（0x180）だけ（`asm.h:315-318`）。`locore.S` も他の場所も Cause を書かない（grep で確認）。
  - IV=1 かつ BEV=0 のとき、R2 の割り込みは EBase+0x200 に入る。XBurst は `cpu_has_divec 1`（Linux の cpu-feature-overrides.h）。
- 起こる状況: p005 で `wait_for_ticks()` が割り込みを開けた瞬間、最初の tick が 0x200 に入る。そこには `.text.vectors` の後ろに link された無関係な関数があり、確実に壊れる。
  - QEMU の malta は IV を立てないので、T1 の回帰では見えない。
  - 実機には UART も gdbstub も無いので、原因の切り分けがほぼできない。
- 直し方:
  - §2.2 の表に「入口の CP0 の状態」の行を足す。`locore.S` の先頭で Cause を 0 にする（`mtc0 $0, $13`）。念のため vectors.S の 0x200 にも `j mips_trap_entry` を置く。
  - これは共通の層の変更なので p002 に入れ、malta の回帰に含める。

---

### Major

**M1. UBIBoot の argv は D cache の中だけにあり、kernel の bss の消去で失われる（§1.3-4・5、§2.1 の「UBIBoot の argv の解釈」）**
- 根拠:
  - UBIBoot の link script（`ldscripts/target-jz4760.ld`）は、`0x80000000` からの 8 KiB に code・data・stack をすべて置く。boot ROM が cache に載せて走らせる形。
  - `main.c` の comment によると、この data の cache line は dirty の印が付いておらず、追い出されると中身が失われる。
  - `kernel_params[]` とその文字列は、この窓の `.data`・`.rodata` にある。
  - 一方、zedBSD の `locore.S` は、`mips_cmain` → `mips_board_boot` が argv を写すより前に、KSEG0 から bss 全体を消す。page の bitmap だけで 16 KiB を超える。D cache は 16 KiB・4 way の見込み（推測）なので、argv の line は確実に追い出される。
  - 設計の §1.3-5 は、この comment を「kernel の読み込みが D cache を汚さない」と読んでおり、意味を取り違えている。
- 起こる状況: argv（`hwvariant`・`kernel_bak` など）の pointer と文字列が、DRAM の中身の無い所を指す。範囲の検査が甘いと、LCD が出る前の段で黙って止まる。
- 直し方（どちらか）:
  - (a) argv を使わない。この WS の到達点には要らない。
  - (b) `locore.S` で bss を消す前に、argv の文字列を KSEG0 から読み、KSEG1 の宛先（uncached）へ写す。
- §1.3 の記述も直す。

**M2. OST の割り込みの経路が誤っている。tick の source の決め方も未確定（§1.8・§2.2 の timer の行）**
- 根拠:
  - `jz4770.dtsi` では、OST の node（`timer@e0`）は `tcu` node の子で、`tcu` 自体が `interrupt-controller`（親は intc、`interrupts = <27 26 25>`）。したがって OST の `interrupts = <15>` は TCU の中の番号（TFR の bit 15）で、INTC の 15 ではない。
  - 実際、同じ file の GPIO の port C が INTC の 15 を使っている（`gpc … interrupts = <15>`）。
  - Linux の `ingenic-ost.c` は OST を clocksource にだけ使い、割り込みを使わない。`jz4770-ost` は `is64bit = true`（jz4760b の info）。
  - OpenDingux の `gcw0-base.dtsi` は、system timer を TCU の channel 0（750 kHz）、clocksource を channel 2、OST を 12 MHz にしている。channel 1 は backlight の PWM、channel 4 は振動の motor が使う。
- 起こる状況: INTC 15 を開けて待つと、tick が来ずに `HAL TIMER FAIL` で止まるか、GPIO C の割り込みを tick と取り違える。
- 直し方:
  - §1.8 を次のように直す: OST の割り込みは TCU の内部の 15、INTC には 27・26・25 のどれかを通る（どれかは未確認）。OST の幅は 64 bit（確、ingenic-ost.c）。
  - 最初の案は「TCU の channel 0 を 1000 Hz の tick、OST を 64 bit の counter」にする。INTC の 25〜27 を全部開け、TFR を見て振り分ける。
  - `clock.c` の分け方も書く:
    - tick は IP7 ではなく IP2 → INTC → bsp の handler から来る。`int.c` の IP7 の経路と、`wait_for_ticks` の `MIPS_INTERRUPT_TIMER` の扱いを決める。
    - level の割り込みなので、kernel に渡す前に TFR の flag を消す。
    - `mips_irq_begin_local` と `MIPS_ACK_LOCAL` はそのまま使えるか確かめる。
    - OST が 64 bit なら、Count の wrap を数える処理は要らない。

**M3. I cache の VTAG への対処が足りない（§2.2 の I cache の行）**
- 根拠:
  - Linux の `arch/mips/mm/context.c` は、ASID が一巡する時に `if (cpu_has_vtag_icache) flush_icache_all();` を呼ぶ。I cache の tag は VA と ASID と読める（推測）。
  - zedBSD の `space.c` の `asid_assign()` は、世代が変わる時に `tlb_flush_all()` しかしない（1281-1285 行）。
  - `hal_space_prot()`（`hal_space_prot_query`）は、EXEC を付ける時に I cache も D cache も同期しない。`src/kern/ptrace.c:315-329` の comment は、HAL がこれを同期する前提で書かれている。
  - `ptrace.c:328` は、tracee の user VA で `hal_icache_invalidate_range` を呼ぶ。実行しているのは tracer の space で、Hit の操作は届かない。
  - arm64 では同じ不足が実機の SIGILL になり、承認の上で `hal_space_prot` に同期を足した（guardrail の 2026-09-24 の行、WS044 p009）。
- 起こる状況: ASID の世代が変わった後、別の process が古い code を実行する。mprotect で実行可能にした page や、ptrace の breakpoint が反映されない。どれも QEMU（cache を模擬しない）では見えない。
- 直し方: §2.2 にこれらを全部挙げる。
  - 世代が変わる時に `Index_Invalidate_I` で I cache 全体を消す。
  - `hal_space_prot` で EXEC になる時に、kernel の alias で D cache を書き戻し、I cache 全体を消す。
  - VTAG の時は `hal_icache_invalidate_range` と `hal_sync_instruction_stream` を常に全体の消去にする。
  - いずれも共通の層の変更（malta の回帰）。最後の項は malta の潜在の不具合の修正にもなる。

**M4. L2 cache（Config2 は在る）と DMA・uncached の整合が抜けている（§1.1-4・L2、§1.4、§2.2 の cache の行）**
- 根拠:
  - Linux の `arch/mips/mm/sc-mips.c` の `mips_sc_probe()` は、JZ4770 で Config1.M を見て Config2 を読む。way の数が 5 と出るのを `MACH_INGENIC_JZ4770` の時だけ 4 に直している。
  - `bcops = &mips_sc_ops` なので、DMA の前に `Hit_Writeback_Inv_SD` を使う。L2 は DMA に対して coherent でないと読める（推測）。
  - 設計の「Config2 が無い」（cpu-probe.c の comment から取った）は、この sc-mips.c と矛盾する。
  - zedBSD の `cache.c` は L1 しか操作しない（0x19・0x11・0x15）。`page.c` の `hal_pmem_map_uncached` も L1 しか消さない。
  - rpi4 では、framebuffer の cache の書き戻しが無くて実機で画面に出ない不具合が実際に起きた（guardrail の 2026-09-24 の行、WS044 p006）。
- 起こる状況: LCDC の DMA は L1 から L2 へ書き戻された line を見ず、DRAM の古い中身を描く。uncached の view も L2 の dirty な line と食い違う。
- 直し方:
  - §1.1 を「Config2 は在る、way は 4 に直す（確 sc-mips.c）」に直す。
  - framebuffer と LCDC の descriptor を、KSEG1（uncached）から書く方式にすることを §1.4 に明記する。framebuffer は `mips_board_init` が `mips_page_init` より前に呼ばれるので、image の bss（約 300 KiB、page に揃える）に置く。
  - `cache.c` に L2 の Hit の操作（SD）を足すことを §2.2 に入れる。これは HAL の内部の変更。

**M5. ボタンの表が出典と食い違っている。起動の手順（D1）が依存する X の位置も確かでない（§1.3-2、§1.6、§7-7）**
- 根拠:
  - `gcw0-base.dtsi`（RG350 も上書きしない）は、PE27 を `"Button X"`、PE28 を `"Button Y"` としている。
  - UBIBoot の `board-gcw0.c` は、PE28 を「X button」、PE27 を「Y button」と呼んでいる。
  - 設計の §1.6 は [OD-dts] を出典にしながら、UBIBoot の呼び名（X=PE28）を書いている。
  - L1 は、rg280v.dts が `&btn8 { gpios = <&gpb 10 …> }` で PB10 に上書きしている。L2 は PB20 で、L1 と同じ pin ではない（§7-7 の疑いは誤読）。
- 起こる状況: ユーザーが違うボタンを押すと、黙って OpenDingux が起動する。zedBSD が失敗したのとも見分けがつかない（M6 を参照）。
- 直し方: 表を DT のとおりに直し、「UBIBoot が見るのは PE28。物理のボタンの刻印は未確認」と書く。手順に「X で OpenDingux が起動したら Y を試す」を足す。

**M6. 起動の道（D1 A）の前提と、失敗の見分けが足りない（§1.3、§5 D1）**
- 根拠（UBIBoot `src/fat.c` の `mmc_load_kernel`）:
  - X を押した時の探す順は `UZIMAGE.BAK` → `VMLINUZ.BAK` → `UZIMAGE.BIN` → `VMLINUZ.BIN`。header が不正だと次の file へ進むので、zedBSD の image が拒まれると黙って通常の OpenDingux が起動する。
  - `.BAK` は OpenDingux の更新が前の kernel を残す名前（`kernel_bak` の引数）。置くと既存の予備の kernel を上書きし、OpenDingux の次の更新では逆に上書きされる。
  - 第 1 partition は FAT32 でないと読まない（`strncmp(vinfo->fs_type, "FAT32", 5)`）。
  - ユーザーの card の UBIBoot の版（Anbernic の stock の firmware か、OpenDingux の新しい版か）は未確認。
- 直し方（D1 の推奨 A の条件として書く）:
  - 置く前に、既存の `UZIMAGE.BAK` と `VMLINUZ.BAK` を退避して記録する。
  - card の第 1 partition の file の一覧と FAT の種類を、p003 の最初に確かめる。
  - 成功の印を OpenDingux の起動と見分けられる形にする（M7）。

**M7. LCD が出る前の段に観測の手段が無い。調べる範囲の上限も無い（§2.3、§3 の p003〜p004）**
- 根拠:
  - `mips_early_exception()` は console に書くだけで、LCD が無い段では何も見えない。
  - p004（CGU・LCDC・panel をゼロから、10 LW）は、資料の [PM] が無く、手探りの作業になる。
  - DT には電源の LED（`gpb 30`、active low、`panic-indicator`）がある。backlight の PE1 も GPIO として点滅させられる。
- 直し方:
  - 電源の LED と backlight の点滅の回数で、段（入口・C・HAL の init・LCDC）と early exception の code を示す。これを §2.3 と p003 に入れる。
  - p004 に実機の試行の上限（例: 3 回）と、上限に達した時の扱い（D4 B を再検討して uncleared にする）を書く。
  - backlight だけの印は、IPS の panel に信号が無いと暗く光るだけの可能性がある（推測）。

**M8. 共通の層の変更が p002 に揃っておらず、p003〜p005 に malta の回帰が無い（§2.2、§3）**
- 根拠: 次の変更が要るのに、p002 の中身として挙がっていない。
  - B1 の Cause の消去。
  - `cmain.c:147-149` の `display = "VGA"` の固定（§4 の `DISPLAY LCDC` には board から名前を得る hook が要る）と、40 字に分けた判定の文字列。
  - M2 の tick の経路（`int.c` の IP7、`cmain.c` の `wait_for_ticks` の IM の bit）。
  - M3 の I cache、M4 の L2。
  - M1 の argv の早い段の写し（`locore.S`）。
- §3 は p003〜p005 の確認を実機だけにしている。
- 直し方: 共通の層の変更を全部 p002 に集める。後の Phase で `src/hal/mips/*`（bsp-* を除く）に触れる時は、mips32-malta・mips64-malta の build と T1 の回帰を必須にする。

**M9. 試験の計画では cache の誤りを見つけられない。検証の方法の承認も無い（§3）**
- 根拠:
  - QEMU は cache を模擬しないので、M3・M4 の誤りは T1 では見えない。
  - AGENTS.md の起動の確認は `boot-test.sh` だけで、例外の一覧に「ユーザーが実機の画面を目で見て写真で返す」は無い。
  - ユーザーの memory には「試験を今すぐ頼まず、ユーザーが拾える試験の一覧に置く」とある。
- 直し方:
  - D7「実機の検証の方法（ユーザーの目視・写真を証拠にする、試験の一覧に置く、結果が届くまで Phase は uncleared）」を足す。
  - p005 の実機の self-test に次を足す。
    - I cache の整合: user の page に code を書いて実行し、書き換えて prot・map を変え、もう一度実行して新しい結果が出るか。
    - uncached・L2 の整合: cached で書いて clean し、KSEG1 から読んで一致するか。

**M10. D3（FR=0 で FPXX）は HAL の API の意味を変えるので、§2.4 の「API の変更は不要」と矛盾する（§1.1 の FPU、§2.2 の FPU の行、§5 D3）**
- 根拠:
  - `include/hal/arch/mips32.h:133-141` の `struct hal_fpregs` は「Status.FR を立てた時の 32 本の 64 bit の register」と定義している。
  - `fpregs.S` は `.set fp=64` で、奇数の register にも `sdc1` を使う。FR=0 では予測できない動作になる。
  - `src/kern/ptrace.c:405-497` は `hal_fpregs` を debugger の `struct fpreg` に写すので、FR=0 にすると debugger から見える意味も変わる。
  - 一方、今の `fpu.c:82-86` は FIR.F64=0 の時に FPU を使わないので、p002 の「`fpu.c` の FIR の判定」は既にある。
  - D6 A（userland は範囲外）なら、この WS で FP を使う user は無い。
  - buildroot の `BR2_mips_xburst` の help によると、XBurst の FPU には fused madd の不具合があり `-mno-fused-madd` が要る。LLVM なら `-mno-madd4` に当たる（推測）。
- 直し方: D3 の推奨を「この WS では FIR.F64=0 なら FPU を使わない（今のまま）。FR=0 の対応は userland の WS で、hal_fpregs の意味の変更の承認と一緒に決める」に変える。madd の注記を userland の WS へ引き継ぐ。

---

### Minor

1. **§1.1 の override**: 「4K の cache」は `cpu_has_4k_cache`（R4K 型の cache の命令）の誤読。大きさの意味ではない。I・D 16 KiB・4 way・line 32 byte は Config1 で確かめる（未確認）。
2. **§1.3-3・§2.5 の読み込み**: UBIBoot が `load` へ memmove するのは最初の 1 sector だけで、残りは `load` へ直に読む（`fat.c` の `load_cluster_chain` → `process_uimage_header(…, MMC_SECTOR_SIZE)`）。
   - 「5 MiB を超えると重なる」心配は当たらない。代わりに、image の後ろに cluster の余りが書かれる（bss は後で消すので害は無い）。
   - `VMLINUZ` の入口は KSEG1 の `0xa0600000`。
3. **入口の状態は分かる**: Status = BEV|ERL、Cause = IV（B1）。UBIBoot は `board_init()` で既に PageCtrl に `0xa9000000` を書いている。kernel でも書いてよい（害は無い）。「I cache の状態は未確認」は狭めて書ける。
4. **§1.4 の panel**:
   - Linux の panel-simple は `enable-gpios` しか使わず、`reset-gpios`（PE2）は読まない（`panel-simple.c:690`）。PE2 の順序と時間には参照が無いことを書く。
   - bus の flag（DE は high、pixel は立ち下がりで sample）を足す。
   - OD の DT は LPCLK の親を PLL1（432 MHz）にしているが、UBIBoot が PLL1 を動かすかは未確認。PLL0 を 166 で割る案と、LCD の clock gate を書く。
5. **§2.1 の cons・font の共有**: `cons.c` の格子は compile 時の 80×30（`MALTA_CONSOLE_COLUMNS`・`ROWS`）。pixel を描くのは `framebuffer.c` の `malta_framebuffer_cell`（PCI の KSEG1 へ書く）。共有には「stride・bpp」だけでなく、格子の大きさと cell を描く callback を bsp から渡す形が要る。
6. **§4 の 40×15**: `HAL ENTRY prid=…` は約 70 字で折り返し、p004・p005 の写真の前に PRId・Config1・FIR の行が画面の外へ流れる。最後にまとめを出し直すか、小さい font を使う。
7. **§0・§8 の GPL の規則**: 2026-10-15 の guardrail（112-119 行）に合わせる。値・表は block ごとの複数行の comment「Value(s) referenced: …」で、手順は「Procedure referenced: …」で引用してよい。file の先頭には書かない。code・comment・構造・symbol の名前は写さない。今の「table を写さない」は古い規則のまま。
8. **phase.md の受け入れの条件 2**: `src/hal/mips32/` とあるが、実際は `src/hal/mips/`。
9. **§3 の実機の手順**: Select（PD18）は boot の選択（BOOT_SEL1）を兼ねるので（`gcw0-base.dtsi` の comment）、電源を入れる時に押さないよう書く。
10. **mips32.h の comment**: `HAL_TIMER_FREQUENCY` の comment（「the coprocessor 0 counter」）が RG350 では不正確になる。直すなら HAL の API の file なので、承認の要否を決める。
11. **Phase の大きさ**: p005（INTC・timer・self-test・VFS で 10 LW）は、malta では p003 と p004 の合計 22 LW だった。「TIMER TICK まで」と「self-test と VFS まで」に分けると、実機の試行の当否を切り分けやすい。
12. **D2 B の見込み**: `page.c` の `PAGE_HIGH_ALIAS 0x80000000` は malta の形。高い側の bank（`0x30000000`）を使うには、bsp が memory の map を渡す形が要る。Future Work の記述でこれを補う。

---

### ユーザーの判断 D1〜D6 の妥当さ

| ID | 判定 |
| --- | --- |
| D1 | A に賛成。ただし M5・M6 の条件（`.BAK` の退避、FAT32 と UBIBoot の版の確認、見分けられる成功の印）を付ける。 |
| D2 | A でよい。 |
| D3 | 推奨を「この WS では保留（FIR.F64=0 なら FPU を使わない）」に変える（M10）。 |
| D4 | A でよい。M7 の LED・backlight の点滅を代わりの観測の手段にし、試行の上限に達したら B を再検討する。 |
| D5 | B でよい（host の Python の zlib.crc32 で足りる）。 |
| D6 | A でよい。 |
| D7（新） | 実機の検証の方法を足す（M9）。 |

HAL の API: B1・M1〜M4・M8 は `src/hal/mips/` の内部と platform だけで閉じ、2026-09-25 の規則により承認なしで直せる見込み。D3 で FR=0 に対応する場合と、mips32.h の comment を直す場合だけ、承認が要る。

### 結論

**このままでは受け入れられない。** Blocker 1 件（B1）と Major 10 件を直した版 2 を、もう一度レビューにかけること。

設計の骨格は妥当と考える: UBIBoot の `.BAK` で起動する、timer・LCDC・INTC を bsp-jz4770 に置く、HAL の API は変えない、到達点は VFS の段。ただし、実機に特有の入口の状態（Cause.IV、cache の中にある argv）、cache（VTAG・L2）、割り込みの経路（OST）に誤りと抜けがある。これらは QEMU の回帰でも、ユーザーの目視でも原因を切り分けにくい。

未実施: build・host 試験・QEMU（設計のレビューなので行っていない）、実機の確認（全部）。

推測として残るもの:
- I cache の tag に ASID が入ること（Linux の振る舞いから推測）。
- L2 が DMA に対して coherent でないこと（Linux の振る舞いから推測）。
- L1 の大きさ。
- OST が INTC のどの線（25〜27）を通るか。
- 物理のボタンの刻印。
- UART の pad の有無（WebSearch でも見つからなかった）。

Sources:
- [Ingenic Semiconductor (Wikipedia)](https://en.wikipedia.org/wiki/Ingenic_Semiconductor)
- [linux-mips.org Ingenic](https://linux-mips.org/wiki/Ingenic)
- [Buildroot MIPS: add support for XBurst cores (search result)](https://lists.buildroot.org/pipermail/buildroot/2016-October/175211.html)
- [OpenDingux Beta Firmware for RG350 and RG280 (retrogamecorps)](https://retrogamecorps.com/tag/beta/)
- [RetroGame350 AppRepo](https://github.com/glebm/RetroGame350-AppRepo)
- その他は `gh api` で読んだ: torvalds/linux（master の `jz4770.dtsi`・`cpu-probe.c`・`cpu-feature-overrides.h`・`ingenic/Kconfig`・`c-r4k.c`・`sc-mips.c`・`mm/context.c`・`ingenic-ost.c`・`panel-simple.c`）、OpenDingux/linux（`jz-6.1` の RG350 の DT の連鎖と `gcw0-base.dtsi`）、pcercuei/UBIBoot（master の `head.S`・`main.c`・`fat.c`・`uimage.c`・`board-gcw0.c`・`config*.h`・ld script）、buildroot（`arch/Config.in.mips`）、OpenDingux/buildroot（`configs/od_gcw0_defconfig`）。
