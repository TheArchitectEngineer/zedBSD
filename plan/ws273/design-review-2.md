<!-- design-reviewer の差分の確認（2026-10-19、ws273 design.md 改訂 2）。Q1 が報告の本文をそのまま置いた -->

## WS273 p001 改訂 2 の差分の確認

対象は次の 3 つ。`/home/awe/zedBSD-claude1/plan/ws273/design.md`（版 2、commit 8ff12718d）、`/home/awe/zedBSD-claude1/plan/ws273/proposed/mips32-h-timer-comment.diff`、`/home/awe/zedBSD-claude1/plan/ws273/proposed/mips32-h-fpregs-fr0.diff`。
照らした先は、既存の `src/hal/mips/`（locore.S・vectors.S・cache.c・space.c・page.c・clock.c・irq.c・int.c・cmain.c・fpu.c）、`src/kern/ptrace.c`・`src/kern/clock.c`、`platform/mips32-malta/vmunix.ld`、`include/hal/arch/mips32.h`、および scratchpad の出典の複写（`/tmp/claude-1000/-home-awe-zedBSD-claude1/70c47280-c9e2-5e3f-b5d9-072cd0539dfc/scratchpad/src/`）。
実行したのは読み取りと `git apply --check`（2 つの diff）だけ。file は編集していない。build・host 試験・QEMU は行っていない（設計の確認なので不要）。

### レビュー 1 の指摘の対応の確かめ

| 指摘 | 判定 | 補足 |
| --- | --- | --- |
| B1 | 直った | 入口の Status は mips32 で `0x00000000`（asm.h:68）なので、BEV・ERL も既存の locore.S:37-39 で落ちる。0x200 は、mips32 では `.text.vectors` の中で `mips_early_vector`（vectors.S:283）より前に置く必要がある。順序だけの注意で、誤りではない。 |
| M1 | 直った | (a) argv を使わない。UBIBoot `main.c` の `jz_flush_dcache` が comment out されていることも確かめた。 |
| M2 | 直った | `irq.c:120` の `mips_irq_begin_local` と `kern/clock.c:92` の `kernel_timer_handler` は、その場で戻り、task を切り替えない。したがって INTC の経路の中から呼んでよい。TFR を先に消す方針で正しい。 |
| M3 | 直った | ただし試験が (1) を覆わない（Minor 5）。 |
| M4 | **直しで新しい誤りが入った**（Major 1） | |
| M5・M6 | 直った | DT と UBIBoot `fat.c` の順（`i ^ (alt ? 2 : 0)`）を確かめた。 |
| M7 | 概ね直った | `_start` の点滅の仕組みが無い（Minor 4）。 |
| M8 | 直った | p006 は例外（Minor 6）。 |
| M9 | 概ね直った | Minor 5。 |
| M10 | 結論は妥当 | 根拠の一部は私のレビュー 1 の誤り（Minor 2）。 |
| Minor 1〜12 | 全部対応された | 表と本文が一致する。 |

---

### Blocker

なし。

### Major

**M-a. KSEG1 の framebuffer と descriptor を bss に置くと、bss の消去が残した dirty な cache line が後で DRAM に書き戻り、描いた内容を壊す。あわせて「入口の cache の全体の操作」は既存に無い（§1.4、§2.2 の「入口の cache」「L2」の行）**

根拠:
- `locore.S:68-76` は、bss 全体を KSEG0（cached）から `sw $0` で消す。
- XBurst の L1 D は write-back。Linux の overrides.h は `cpu_has_inclusive_pcaches 0` で、L2 は L1 を包含しない。
- 300 KiB の framebuffer を消すと、16 KiB を超えた分は L2 へ追い出され、dirty な 0 の line として残る（L2 が write-back であることは推測）。L1 にも最後の約 16 KiB が dirty で残る。
- その後 bsp が KSEG1 から描いても、これらの line が追い出された瞬間に 0 が DRAM に書き戻り、文字が line 単位で消える。
- descriptor の line が書き戻されると、LCDC は次の descriptor の address 0 を辿り、画面が崩れる。

「入口の cache の全体の操作」も成り立たない:
- §2.2 は「`cache.c` の既存の全体の操作を早い段で呼ぶ」とするが、`cache.c` に全体の操作は無い。
- 唯一の Index の操作 `mips_cache_flush_page_colors`（cache.c:131-186）は、`cache_alias == 0` なら何もせず戻る（144 行）。RG350 は 4 KiB/way なので常に 0。
- cache の形は `mips_cache_init`（cmain.c:104）で初めて読まれ、これは `mips_board_init`（cmain.c:76、LCD を起こす所）より後に来る。
- 書かれたとおりに実装すると、RG350 では入口の flush が黙って無操作になる。L2 にも届かない。

起こる状況: p004 の console が時間とともに欠ける、または画面が崩れる。QEMU は cache を模擬しないので T1 では見えない。p006 の `L2 PASS` 試験（cached で書いて clean し、KSEG1 から読む）でも、この向きは検出できない。

直し方（§1.4・§2.2・p002 の行に書く）:
1. framebuffer と descriptor は、`vmunix.ld` で `__bss_end` の後、`__kernel_vma_end` の前の専用の NOLOAD の section に置き、locore.S の cached の消去から外す。bsp が KSEG1 から消して書き、KSEG0 の symbol address では決して触れないと明記する（page.c:142-143 の `reserve_range(0, kernel_end)` に入る）。
2. 入口の flush は新しい関数として設計する。p002 の項に書く内容:
   - Config1 と Config2 をその場で読んで L1 と L2 の形を求める。
   - L1 D の `Index_Writeback_Inv_D`、L1 I の `Index_Invalidate_I`、L2 の `Index_Writeback_Inv_SD` を全 way・全 set に掛ける。
   - 置き場所は「bss の消去の後、`mips_board_init` より前」と指定する。
3. 1 にできない場合は、`mips_board_init` で最初に KSEG1 から書く前に、その範囲へ L1 の Hit_Writeback_Inv_D と L2 の Hit_Writeback_Inv_SD を掛ける。そのとき、L2 の line の大きさを `mips_cache_init` より前に得る方法も書く。

---

### Minor

1. **p005 の判定の文字列が届かない（§3.1 p005、§4）**
   - `MIPS32 HAL READY DISPLAY …` は `hal_cpu_start_others`（cmain.c:132-160）が出す。これは kernel が呼び戻した後で、`mips_paging_selftest` を通った後に限られる。同じ行に `PAGING PASS` が入る。
   - したがって p005 の確認「`HAL READY DISPLAY LCDC` と `TIMER TICK`」は、p006 の範囲が終わらないと出ない。
   - 直し方: p005 の判定を `wait_for_ticks`（cmain.c:242）の `MIPS32 HAL TIMER TICK ticks=… count=… Hz` にし、`HAL READY` の行は p006 へ移す。

2. **ptrace の根拠は MIPS では誤り（§2.2 の VTAG (3)、§2.4-2、proposed の fpregs の注記）。レビュー 1 の M3・M10 の私の誤りが持ち越された**
   - `src/kern/ptrace.c` は `platform/amd64/vmunix.mk` と `platform/arm64/vmunix.mk` にしか入っていない。
   - 405-497 行は `__x86_64__` と `__aarch64__` の分岐で、MIPS の fpreg の写しは無い。
   - よって「ptrace.c:328 は malta の潜在の不具合」「ptrace.c が hal_fpregs を struct fpreg に写す」は、今の MIPS には当たらない。
   - 結論は変わらない: VTAG の (3) は `hal_space_map`（space.c:438-440）が kernel の alias で `hal_sync_instruction_stream` を呼ぶことから要る。D3 は `hal_fpregs` の定義（mips32.h:132-141）と `fpregs.S:23` の `.set fp=64` から承認が要る。
   - 直し方: 根拠を space.c:440 と mips32.h・fpregs.S に差し替え、ptrace は「将来 MIPS で ptrace を build する時」の注記にする。

3. **proposed/mips32-h-fpregs-fr0.diff は壊れていて適用できない**
   - `git apply --check` の結果は `error: corrupt patch at line 21`。
   - hunk の header は `@@ -133,9 +133,13 @@` だが、実際の `/*` は 132 行で、旧の行は 7、新の行は 11。
   - 中身にも食い違いがある:
     - 前置きは「f[2n] に 64 bit で詰める」と書く。comment は「各 entry の下半分に 32 bit、上半分は 0」と書く。
     - FR=0 で偶数の register に `sdc1` を使うと、対が f[2n] に入るのが自然。
     - 「fir tells the two forms apart」は不正確。FIR.F64 は能力の bit で、今の Status.FR の状態ではない。
   - D3 A では使わないので急がないが、承認の叩き台として出すなら header と記述を直す。

4. **`_start` での点滅の仕組みが無い（§2.3 の表の 1 行目）**
   - `_start` は bss の消去と stack より前の共通の locore.S にあり、board の GPIO を叩く手段が無い。
   - 直し方: p002 に「locore.S から呼ぶ、stack を使わない bsp の葉の関数（malta は空）」を足す。または p003 で RG350 に専用の入口の stub を置く、と書く。

5. **I cache の試験が M3 (1) と ASID の推測を覆わない（§3.1 p006）**
   - 設計の試験は、同じ space での書き換え（M3 の (2)・(3)）しか見ない。
   - 足すもの:
     - (a) self-test の世代の上限を下げる仕組み（space.c:875 付近）で世代を回し、前の世代と同じ ASID を得た別の space で、同じ VA の別の code を実行する。
     - (b) 同じ世代の 2 つの space（ASID が違う）で、同じ VA の別の code を交互に実行する。
   - (b) で結果が混ざれば、tag に ASID が無いと分かる。その場合は space を選ぶたびに I cache 全体を消す必要があり、§7-8 の推測を確に変えられる。
   - L2 の試験には逆の向き（KSEG1 で書き、invalidate し、cached で読む）も足す。

6. **p006 は共通の層（selftest.c）を変えるのに、確認に malta の回帰が無い（§3.1 p006 の行）**
   - `ICACHE PASS`・`L2 PASS` を共通の self-test に置くと、malta の出力も変わる。L2 の無い malta では `L2 NONE` などを出す必要がある。
   - §3.1 の箇条の規則（`src/hal/mips/*` に触れたら malta の回帰）に従い、p006 の確認の列に mips32・mips64 の malta の build と T1 を足す。出力の変化も書く。

7. **機種と panel の版が未確認の一覧に無い（§1.4、§7）**
   - OpenDingux の DT では、RG350M は `leadtek,ltk035c5444t`（640×480）、RG350 は `frida,frd350h54004`（`od-rg350m.dts:11-13`、`od-rg350.dts`）。
   - §7 と §3.2-1 に「機種（RG350／RG350P／RG350M）と画面の解像度をユーザーに確かめる」を足す。p004 の試行の上限を panel の取り違えで使い切らないため。

8. **UBIBoot が uImage に対応しない版だった時の道が無い（§2.5、§3.2）。推測を含む**
   - `fat.c` では、`VMLINUZ` は KSEG1+LD_ADDR（`0xa0600000`）へそのまま飛ぶ。
   - 古い UBIBoot が `UZIMAGE` に対応しない可能性がある（推測、未確認）。
   - link address を `0x80600000` にすれば、locore.S は絶対 address で KSEG1→KSEG0 へ移るので（52-66 行）、同じ image が `VMLINUZ.BAK`（raw）でも `UZIMAGE.BAK` でも走る。代わりに image の下の 6 MiB が予約で減る。
   - 案として §2.5 に書き、選ぶかを p003 で決める。

9. **TCU と LCD の初期化の抜け（§1.4・§1.8、p004・p005 の作業）**
   - TCU:
     - TCU の他の channel の割り込み（半分の match の flag を含む）を mask し、残った flag を消す。これをしないと、25〜27 を開けた時に割り込みが止まらなくなる。
     - TCU と OST の clock gate と channel の clock の源を明示する。UBIBoot は CLKGR に触れない（`board-gcw0.c`）。
   - LCD:
     - LCD の pin の機能を 24 bit の group に切り替える（`gcw0-base.dtsi:328-330`、`rg280m-v1.0.dts:105-107`）。UBIBoot は LCD の pin を設定しない。
     - KSEG1 で書いてから DMA を始める前に `sync` を入れる。
   - D2 A のため、`mips_board_memory_total` も 256 MiB を返すと明記する（page.c は高い側を `PAGE_HIGH_ALIAS` の形で扱うため）。

10. **実機の確認が一つずつ進むので、WS が止まりやすい（§3.1、D7）**
    - D7 A（試験の一覧に置き、今すぐは頼まない）では、p003→p004→p005→p006 の各段がユーザーの結果待ちで止まる。
    - p003 と p004 の実機の確認を 1 回にまとめる（LED の点滅と画面を同じ起動で見る）などの案を書く。p003 にも試行の上限を付ける。

11. **§1.1 の「rdhwr は使える見込み」と `cpu_has_userlocal 0` の関係**
    - TLS 用の hwr 29 は無い。kernel は今 UserLocal を使わない（`task.c` は値を保持するだけ）ので害は無い。
    - userland の WS への引き継ぎ（§6）に足す。

12. **QEMU で新しい経路を一度も通さない（§3.1 p002）**
    - VTAG の全体の消去と L2 の操作は、malta では判定で外れるので、p002 では一度も実行されない。
    - 試験用の build の切り替えで、malta でもこれらの経路を強制して走らせる案を足すと、少なくとも hang や例外は p002 の T1 で見つかる。整合の誤りは見つからない。

---

### D1〜D8 の妥当さ

| ID | 判定 |
| --- | --- |
| D1 A | 妥当。条件（FAT32、版、`.BAK` の退避、見分けの印）は出典と合う。Minor 7・8 を条件に足すとよい。 |
| D2 A | 妥当。memory_total の注記を足す（Minor 9）。 |
| D3 A | 妥当（今の fpu.c:83 の判定で FPU を切る）。根拠は Minor 2 のとおり直す。 |
| D4 A | 妥当。p004 の試行の上限で B を再検討する条件も適切。 |
| D5 B | 妥当。 |
| D6 A | 妥当。 |
| D7 A | 妥当で、必要。AGENTS.md の `boot-test.sh` だけの規則の例外として、ユーザーに明示の承認を取ること。止まりやすさは Minor 10。 |
| D8 | A でも B でもよい。A なら mips64.h:13 の同じ文言が残る（mips64 の基板は Count を持つので害は無い）。 |

### proposed の 2 つの diff（HAL の API の変更として）

- `mips32-h-timer-comment.diff`: `git apply --check` は通った。comment だけの変更で意味は変わらない。HAL の API の file なので承認は要るが、変更として妥当。
- `mips32-h-fpregs-fr0.diff`: 壊れていて適用できない（Minor 3）。記述の食い違いもある。この WS では使わない叩き台なので Blocker ではない。ただし、このままではユーザーの承認の対象にできない。

### 結論

**Major 1 件（M-a）を直せば受け入れてよい。** Blocker は無い。

- B1・M1〜M10・Minor 1〜12 は、M-a の一点を除き正しく反映されている。
- M-a は M4 の直しで新たに入った誤りで、p002 の範囲（入口の cache の新しい関数、L2 の全体の操作、framebuffer の section）を変える。p002 の着手前に design.md を直すこと。直しは局所的なので、全体のレビューをやり直す必要は無い。Q1 がその差分を見れば足りる。
- Minor は p002〜p003 の phase.md を作る時に取り込めばよい。ただし Minor 1（p005 の判定）、2（ptrace の根拠）、3（壊れた diff）は design.md の記述の誤りなので、M-a と一緒に直すのがよい。

推測として残るもの: L2 が write-back であること、I cache の tag に ASID が入ること、古い UBIBoot が uImage に対応しないこと、JZ4770 の TCU の flag の配置（JZ4740 の形からの類推）。

未実施: 実機の確認（全部）、build・試験。
