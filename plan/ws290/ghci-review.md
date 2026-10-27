# GHCI レビュー 第 1 版（ghci.md eb296d1 に対する疑問 N01〜N29、2026-10-27 19:43）


- 対象: `git -C /home/awe/amaterrace-gpu show eb296d1:docs/ghci.md`（2392 行）。前の版 8035b4a への疑問 M01〜M58（[ghci-review-8035b4a.md](ghci-review-8035b4a.md)）を、`git diff 8035b4a eb296d1 -- docs/ghci.md` と改訂後の本文で判定した。行の番号は eb296d1 の ghci.md の物。
- header（`include/ghci_cmd.h`）は食い違いの確認にだけ見た（CMD_FENCE の flags、DRAW_INDIRECT、BEGIN_RENDER_PASS）。
- 改訂で変わらなかった節: §7.3 の既存の packet（FENCE・WAIT_FENCE・BEGIN_RENDER_PASS・BARRIER・EXECUTE_COMMANDS など）、§8.3・§8.5・§8.6、§10（Faults の行）、§11.2・§11.3・§11.5、§12（§12.8 の 1 項を除く）、§13.2 の本文。
- 回答の要約の M の番号（M39 mesh、M40 query、M42 entry point、M47 render pass）は質問の番号とずれている。ここでは**質問の番号**で判定した（元の M39 render pass、M40 indirect の flags、M42 vertex stride、M47 descriptor の field）。

## 要約

- M01〜M58 の判定: **解決 12・一部 31（うち改訂で新しい食い違い 10）・未解決 15**。
- 次に GHCI の担当へ送る疑問 **N01〜N29: C 5・H 12・M 9・L 3**。
- C の中心: (1) shader の program の形が §8.2（header 無し、最初の命令を指す）と §12.10（PROGRAM gpu-op、16 語の header と interface table）で食い違う、(2) resource の bind が §8.4 と §12.10 の二通りのまま、(3) render pass に depth/stencil・clear の値・resolve が無く、§7.3 と header の BEGIN_RENDER_PASS の形が違う、(4) 二次 command buffer で CMD_FENCE・WAIT_FENCE（host の物理番地）が禁止されていない、(5) session の fault の後のその session の packet と fence の扱いが無い。
- **driver の実装は全体としてはまだ始められない（C が 5 件残る）**。register の初期化・ring・doorbell・fence の待ち・IRQ の骨格は今の版で書ける。shader・描画・user の command の受け付け・fault の回復は C の答えが要る。

## GHCI の担当へ送る次の疑問

重さ: C＝その部分の driver が書けない、H＝その部分を書く前に答えが要る、M＝仮の扱いで進められるが答えが要る、L＝文書の整理。

| ID | 重さ | 疑問 | 根拠（ghci.md eb296d1） |
| --- | --- | --- | --- |
| N01 | C | shader の program の形はどれか。§8.2 は `vs/fs/cs/ms_code_va` が最初の命令を指し header も reflection も無いと言い、§12.10 は廃止の PROGRAM gpu-op で 16 語の header・interface table・code を渡し、GPU が検査（失敗は VK_ERROR_INITIALIZATION_FAILED）すると言う。header が無いなら、register の数・flags（DEPTH の export、CENTROID など）・workgroup size・VARYING_COUNT・interface table はどこから GPU に渡るか。検査の失敗はどこで分かるか。 | §8.2 995〜996 行、§12.10 1883〜1986 行、§7.5 |
| N02 | C | resource の bind はどちらか。§8.4 は shader の命令（`tex ld`・`gsi1_load`・`SAMPLE r0, r1, r2, s0`、どれも GSI1 に無い）が descriptor table を直に引く、§12.10 は CP が interface table で slot と bank を埋める。型の混ざる table の descriptor_size_bytes、combined sampler の index の組が指す table、descriptorCount>1、dynamic offset は。constant bank は §8.4・LOAD_CONST が c[0..511]、GSI1 の operand は c0〜c31: shader は c32 以降をどう読むか。LOAD_CONST と interface table が書く bank の語の重なりは。 | §8.4 1076〜1128 行、§7.3 LOAD_CONST、§12.3、§12.10 |
| N03 | C | render pass: `GhciRenderPassDesc` は色の 8 個だけで、depth/stencil の attachment・resolve の先・sample 数・layer/level が無い。`image_va_lo` は 32 bit だけ（VA は 40 bit まで）、`row_pitch_bytes` は 16 bit（16384×4 byte＝65536 が入らない）、`format` は 8 bit で「VkFormat か内部の ID」。clear の値は §7.3 に無く（clear_flags の bit の割当も無い）、header の `GhciCmdBeginRenderPass` は width/height/layers/clear_flags の代わりに color_clear_rgba（attachment 0 だけ）・depth・stencil・flags を持つ。どちらの形が正か。attachment 1〜7 と整数・float の format の clear、ops の LoadOp=Clear と clear_flags の関係は。 | §7.3 777〜788 行、§8.4 1090〜1105 行、header 520〜531 行 |
| N04 | C | 二次 command buffer の禁止は VM_MAP/UNMAP・SESSION_*・BIND_SESSION・DISP_SET・FLIP だけで、CMD_FENCE・CMD_WAIT_FENCE（§6.0 で host の物理番地・IOVA）と WRITE_TIMESTAMP（「GPU VA or host memory」）は入っていない。user の二次の中でこれらは許されるか。許されるなら番地の空間は何か（物理なら user が任意の host の memory に書ける）。禁止の packet の fault は kind 3 と kind 6 のどちらか（§7.3 の入れ子は `GHCI_FAULT_BAD_PROGRAM`）。 | §6.0 541〜563 行、§7.3 810〜849 行 |
| N05 | C | session の fault の後: primary の ring に残るその session の packet（BIND_SESSION i の後の draw、EXECUTE_COMMANDS）は捨てられるか、再び fault するか。その後ろの CMD_FENCE は書かれるか。閉じた session への BIND_SESSION は何になるか。driver はどの fence が来ないかをどう知るか。FAULT_INFO の W1C はどの bit に何を書くか。 | §3.3 343〜347 行、§3.5、§10 1269〜1274 行 |
| N06 | H | fault の範囲: §10 の Faults の行は今も「kind 1〜10 の fault は FAULTED を立て CP は FAULTED へ」で、§3.3・§10 Recovery・§11.6 と食い違う。§3.3 は「active な session が全部 fault したら FAULTED」と言う: client が一つだけの時、その fault は device の reset になるか。session 0 は「active」に数えるか。CAP_PROT=0（walker はある）の GPU の session の fault は何になるか。 | §3.3 345〜347 行、§10 1265〜1274 行、§11.6 |
| N07 | H | session 0: SESSION_OPEN が要るか。番地の空間は identity か、VM_MAP が要るか。reset の後の既定は session 0 と言う（§6.0）が、session 0 の VA で何が見えるか。 | §6.0 551 行、§11.6 1525〜1528 行 |
| N08 | H | reset の後の手順: §5.2・§10 は「ring の pointer を 0 にして RUN=1 だけ」、App. A は「SESSION_OPEN で session を開き直す」。reset は SESSION_ACTIVE と CP の内部の session の root を消すか。開き直すなら前の VM_MAP は失われ、CP_HEAP の page は戻るか（全部 map し直すか）。reset は CTRL.IRQ_GLOBAL・WDT_EN も 0 にする（§3.3）: §5.2 の「RUN=1 だけ」で割込みが止まるのでは。保持の一覧が §3.3（WDT_CYCLES・CP_HEAP）と §5.2（RING_BASE・RING_SIZE・SCRATCH）で違う: 和が正か。 | §3.3 CTRL[0]、§5.2 516〜520 行、§10 1276 行、App. A 2390〜2392 行 |
| N09 | H | CP_HEAP: MMU_OOM で「CP_HEAP_LIMIT を広げて再試行」とあるが CP_HEAP は IDLE でしか書けない。RUN=0 で IDLE にして書き、RUN=1 に戻す手順でよいか（RUN=0 で ring の位置と session は保たれるか）。heap は連続で LIMIT を伸ばすだけか。「呼んだ session だけ止まる」の session は VM_MAP の session_id の物か、KMD（session 0）か、止まった session は SESSION_ACTIVE が 0 になるか。CP_HEAP の 32 bit の「GPU physical address」は local か host か、要る量の目安、0 で埋める必要。 | §3.3 Lifecycle、§5.2、§10 1275 行、§11.2 1353〜1363 行 |
| N10 | H | ring の packet の失敗: 開いた session への SESSION_OPEN、root の確保の失敗、閉じた session への VM_MAP、map 済みの範囲への map、部分の unmap、未 map の unmap は何になるか（fault の kind、どの session が止まるか）。PROT の無い GPU で PROTECTED は kind 2 の fault だが、KMD が出した packet の fault は session 0 の fault（device 全体）になるか。§4.1 の PROT の段は今も VK_ERROR_FEATURE_NOT_PRESENT と BUFFER_GRANT を言う。 | §4.1 472〜477 行、§7.3 VM_MAP・SESSION_OPEN、§11.6 1519〜1523 行 |
| N11 | H | 番地の空間の残り: WRITE_TIMESTAMP の dst_addr、BEGIN/END_QUERY の query_pool_va、DRAW_INDIRECT の buffer・count buffer はどの空間か。fence_addr は §6.0 で host だけ、§7.3・§9.1 で「または local DRAM」: discrete で local に fence を置けるか。WAIT_FENCE は「host/GPU address」。CMD_FENCE の flags は §7.3 で [1]＝IRQ の前の writeback、§9.1・header で [1] MEM_WRITE・[2] HOST_FLUSH: どちらか、[1]=0 の fence は何を書くか。 | §6.0 550〜563 行、§7.3 821〜849 行・923〜926 行、§9.1 1241〜1253 行、header 375〜377 行 |
| N12 | H | MEM の LOCAL_BASE・HOST_LIMIT は今も 32 bit の「GPU address」: session ごとの VA の下で何の空間か（HOST_LIMIT は意味を持つか）。VM_FLAG_LOCAL の phys は絶対の番地か LOCAL_BASE からの offset か。scanout の FB_BASE・FLIP の base（32 bit の物理）と CP_HEAP（32 bit）は、PA_BITS 40 の host の 4 GiB より上に置けないか。 | §11.2 1351 行、§11.3 1454〜1457 行、§13.2 2229〜2232 行、§7.3 FLIP |
| N13 | H | image と転送: tiled の形、mip level 1 以降の位置、D24S8・D32S8 の stencil の置き場。buffer↔image の copy の command が無い。COPY_IMAGE（8 dw）・BLIT・RESOLVE_IMAGE の payload、COPY_BUFFER の flags と align。 | §7.2 0x40〜0x44、§7.3 851〜869 行、§8.3、§8.5.2 |
| N14 | H | PSO に無い state: stencil の fail/pass/depthFail op と front/back の別、attachment ごとの blend と write mask、logic op の種類（enable の bit だけ）、MSAA（sample 数・sample mask・alpha to coverage: §12.11 は廃止の SET_REG で MS_CTRL を書く）、VARYING_COUNT、line width、topology の TRIANGLE_FAN・adjacency・patch。vertex の stride は PSO の 8 bit×4 binding、§8.6 の「12 bit、4095」、BIND_VERTEX_BUFFERS の 32 bit のどれが効くか。 | §8.2 980〜1018 行、§8.6、§7.3 694〜703 行、§12.11 2003 行 |
| N15 | H | cache の無効化: §11.5 は今も DRAW・LOAD_PROGRAM・INVALIDATE（1.0 に無い）で無効化と言う。host が code・PSO・descriptor を書き換えた時、何が I cache・texture・data cache・CP の PSO の cache を無効にするか（BARRIER の INVAL_L1 だけか）。 | §11.5 1479〜1486 行、§7.3 BARRIER |
| N16 | H | §12 は今も CAPS[19]・[24]〜[30]、CAPS2、ISA_ID で判定する（subgroup、TU_FULL、packed、MS4、CORE1、native ISA、L2・TU L1・VF・LDS の大きさ、tex slot、const words）。§3 の note は CAPS・CAPS2 を廃止と言う: それぞれ今はどの register のどの bit・field か。 | §3 192〜199 行、§11.5.1 1500〜1508 行、§12.1・§12.5・§12.9〜§12.12 |
| N17 | H | query の結果の形（番地・幅・availability）、query の型（occlusion だけか、precise か）、query_pool_va は lo の 32 bit だけで足りるか。8 dw の DRAW_INDIRECT（count buffer）に flags が無い: indexed かどうかをどう示すか（header も同じ）。DEVICE_CAPS[3] は indirect dispatch もあると言うが command が無い。 | §3.2.1 252 行、§7.3 756〜775 行・923〜926 行、header 310〜327 行 |
| N18 | M | CAP_HW_WALKER=0（「Flat physical mode」）の GPU の番地は何か、SESSION_OPEN（Mandatory）は何をするか。CAP_PROT と CAP_HW_WALKER の関係（walker があれば PROT が無くても session ごとの VA か）。SESSION_OPEN の PROTECTED の意味、PROT の register（0x1D0）の中身。VM_MAP の COHERENT・WC は §11.4（GPU は coherence を外さない）とどう両立するか。 | §3.2.1 251・259 行、§4.1、§11.2 1345〜1351 行、§11.4、§11.6 |
| N19 | M | format: maxColorAttachments は §3.2.2 で MAX_MRT、§8.6 で FMT_LIMITS[3:0]。FMT の native の bit（0〜5）に DEPTH_STENCIL_ATTACHMENT・COLOR_ATTACHMENT_BLEND・VERTEX_BUFFER・texel buffer の bit が無い、FMT_BUFFER の bit の意味、EXT_CAP_FMT0 の bit の割当。§4.1 の ROP の段は今も「COLOR_ATTACHMENT_BIT (bit 7)」。sRGB の attachment（Path 1）の capability。 | §3.2.2 285〜286 行、§4.1 448〜470 行、§8.6 1230 行、§12.12 |
| N20 | M | fault の kind: ISA の fault は §3.5 で kind 1、§12.6 で今も kind 8（MMU_OOM）。FAULT_ADDR の PC は「その stage の code の base からの相対」だが、FAULT_INFO にどの stage かが無い。 | §3.5、§12.6 1703〜1704 行 |
| N21 | M | IRQ: IRQ_DISP[3] は bit 9〜12 と同時に立つか。IRQ_RESULT[7]・IRQ_RETIRE[8]・IRQ_DBG_DONE[4] を立てる物は何か（RESULT・debug port は 1.0 に無い）。§6.2 の framing の誤りは今も「`GHCI_FAULT_BAD_HEADER`, kind 2」（表では kind 3）。 | §3.4 349〜372 行、§6.2 601 行、§13.2 |
| N22 | M | bind point: BIND_PIPELINE は graphics と compute で一つか（PSO は vs/fs/cs を一つに持つ）。compute の PSO を bind すると graphics の state は消えるか。 | §7.3 0x20、§8.2、§8.4 1107〜1109 行 |
| N23 | M | descriptor の field: texture の format（uint8、VkFormat か）、sRGB の flag と format の重複、sampler の compare_op の値（PSO と同じか）、border color は RGBA8 だけ（整数・float の border）、filter と mipmap の値の範囲。 | §8.3 |
| N24 | M | limit と ISA: workgroup の size（program の 8 bit×3 と {256,256,64}）、LDS 4096（KC705、Vulkan の最小 16384 を下回る）の実際の値の読み場所、timestampPeriod は §8.6 の 10 ns 固定か GCLK_HZ か、§8.6 に無い limit。MSAA の 2x は §12.11（1 か 4）と食い違う。§12.8 の LOAD64・STORE64 は §12.5 の opcode map に無い: encoding は。 | §8.6、§12.8 1760 行、§12.5、§12.11 |
| N25 | M | 表示: CMD_FLIP の record は「64-bit host DMA address」だが dword 3 の一語だけ。FLIP の後の packet は take を待つか。mode は MODE の表だけか（sink の EDID を読む経路は無いか）。 | §7.3 935〜941 行、§13.2 |
| N26 | M | watchdog: WDT_CYCLES の reset の値、「一つの command」は packet か draw か、WAIT_FENCE や FLIP の待ちを数えるか。HALT は今の packet が WAIT_FENCE・FLIP の待ちの時に HALTED に入れるか。 | §3.1 0x048、§3.3 CTRL[2]、§10 1277〜1283 行 |
| N27 | L | VM_UNMAP は「TLB の無効化の後で RPTR が進む」（§11.2）、§6.4 は「RPTR は引数を写した時に進む」。page の再利用は fence の後でよいか。 | §6.4、§11.2 1391〜1396 行 |
| N28 | L | 文書の整理: §7.3 の見出しと header の語に残る「0x21 / 0x2B」「0x31 / 0x30」「0x34 / 0x37」（0x21 は BIND_VERTEX_BUFFERS、0x37 は END_QUERY）。§1.3 と §12.6 の「job tag」。§11.2 の IMPORTANT の囲みの BUFFER_GRANT。§4.1 の固定 offset の「0x100 MEM」（map は 0x1E0）、list の header 0x180〜0x1BF と VEND 0x180〜0x19F の重なり。§3.4 の表で [5] IRQ_SOFT が二行。§13.2 の WAIT_IDLE・RESULT・QUERY・FLIP COMPLETE。§15.2 の MSI の条件に IRQ_GLOBAL が無い。§10・App. A の IRQ_FAULT（§3.4 は IRQ_ERROR）。§2 の 128 KiB と §15.1 の 64 KiB、GCLK_HZ が 0x064 と VEND+0x10、§12.11・§12.12 の draft、minor の版が 0 でない時の扱い。 | §1.3 123 行、§3.4、§4.1 457 行、§7.3、§10、§11.2、§13.2、§15、App. A 2386 行 |
| N29 | L | boot の framebuffer の引き継ぎ（§13.3）は local memory と local window が前提: UMA の時の手順。 | §13.3 |

## M01〜M58 の判定

判定: 解決 / 一部（残る点）/ 未解決。「新」は改訂で新しく生じた食い違い。

| M | 判定 | 根拠・残る点 | 次 |
| --- | --- | --- | --- |
| M01 | 一部 | §3 の note と §3.2.1 で DEVICE_CAPS が正と決まった。§12・§11.5.1 は今も CAPS[...]・CAPS2・ISA_ID | N16 |
| M02 | 解決 | §3.1 0x000「strictly 0x47484349」 | |
| M03 | 解決 | §3.2.2 VA_BITS_ENC＝VA−32、PA_BITS_ENC＝PA−40 | |
| M04 | 解決 | §3.2.1 CAP_UMA が正、UMA なら LOCAL_SIZE 0。DUAL_SOURCE は DEVICE_CAPS[5] が正 | |
| M05 | 一部（新） | §3.2.2 は MAX_MRT、§8.6 は FMT_LIMITS[3:0] のまま | N19 |
| M06 | 一部（新） | list と固定 offset のどちらでもよい、ID 5・6 は driver は触らない。固定 offset の「0x100 MEM」が map（0x1E0）と違う、0x180 の重なりが残る | N28 |
| M07 | 一部 | FMT の bit 0〜5 が決まった。depth/stencil・blend・vertex の bit、FMT_BUFFER、EXT_CAP_FMT0 が無い。§4.1 に「bit 7」が残る | N19 |
| M08 | 解決 | §7.5 は全部廃止。ただし §12.10 は今も PROGRAM gpu-op に依る | N01 |
| M09 | 解決 | SCRATCH 0x044、WDT_CYCLES 0x048（§3.1・§14） | |
| M10 | 解決 | §3.3「Driver-Visible Lifecycle States」で RUN と RUNNING・CP_STATE から判定 | |
| M11 | 解決 | reset の保持の一覧、IRQ_GLOBAL の式（§3.4・§9.2）、HALT=0 の戻り先。§5.2 の一覧の違い・§15.2 の式が残る | N08・N28 |
| M12 | 解決 | §3.3 TLB_FLUSH は診断用、driver は書かない | |
| M13 | 解決 | §2 32 bit の access で LO/HI、TIMESTAMP_LO の読みで HI を latch。§1.2 の register の数を直した | |
| M14 | 未解決 | WDT_CYCLES の reset の値・単位・「command」の定義が無い | N26 |
| M15 | 解決 | §9.1 fence は 64 bit、int64 の比較 | |
| M16 | 一部 | §6.0 に番地の 3 つの空間。WRITE_TIMESTAMP・query・indirect が分類に無い。fence_addr の local の可否が §6.0 と §7.3・§9.1 で違う | N11 |
| M17 | 未解決 | §6.2 は今も「BAD_HEADER, kind 2」 | N21 |
| M18 | 一部（新） | §9.1・header の flags（[1] MEM_WRITE、[2] HOST_FLUSH）と §7.3（[1] writeback）が食い違う | N11 |
| M19 | 一部 | IRQ_DISP[3] の clear が決まった。bit 9〜12 との関係、IRQ_RESULT・RETIRE・DBG_DONE の出所が無い | N21 |
| M20 | 一部 | §3.3・§10 Recovery・§11.6 は session だけ止める形で揃った。§10 の Faults の行が残る。「全 session が fault で FAULTED」の意味 | N06 |
| M21 | 一部 | 後の fault は落とす（SESSION_ACTIVE で分かる）。その session の残りの packet・fence・W1C の書き方が無い | N05 |
| M22 | 解決 | 0〜15 の 16、SESSION_ACTIVE の bit i。「job tag」の語が残る | N28 |
| M23 | 一部（新） | §3.5 で ISA は kind 1、入れ子は kind 3。§12.6 は今も kind 8、§7.3 は BAD_PROGRAM、二次の禁止は「3 or 6」 | N20・N04 |
| M24 | 未解決 | CP_HEAP は IDLE でだけ書ける、「呼んだ session」が不明。RUN=0 で IDLE にする手順は本文に無い | N09 |
| M25 | 一部（新） | §5.2・§10（RUN=1 だけ）と App. A（session を開き直す）が食い違う | N08 |
| M26 | 一部 | App. A は ring を作り直さず RING_WPTR=0。HALT と待ちの関係が無い | N26 |
| M27 | 一部 | SESSION_OPEN/CLOSE の payload と表が揃った。失敗の知らせ方（fault の kind と止まる session）が無い | N10 |
| M28 | 一部 | 4 KiB の align と size。重なり・部分の unmap、COHERENT・WC、walker の無い GPU が無い | N10・N18 |
| M29 | 一部 | §11.6・§12.8 は session の VA になった。MEM の LOCAL_BASE・HOST_LIMIT と VM_FLAG_LOCAL の phys が残る | N12 |
| M30 | 未解決 | session 0 は「kernel 用」とだけ。SESSION_OPEN・VM_MAP の要否、番地の空間が無い | N07 |
| M31 | 一部（新） | primary は KMD だけ、二次の禁止の一覧。FENCE・WAIT_FENCE・WRITE_TIMESTAMP（host の物理番地）が禁止に無い | N04 |
| M32 | 解決 | §11.6 同じ phys を二つの session に VM_MAP するのが共有、grant は廃止。§4.1・§11.2 に旧い語が残る | N10・N28 |
| M33 | 未解決 | CP_HEAP の空間・量・align・初期化は変わっていない | N09 |
| M34 | 未解決 | §11.2 と §6.4 は変わっていない | N27 |
| M35 | 未解決 | tiled・mip の位置・stencil の置き場が無い | N13 |
| M36 | 未解決 | COPY_IMAGE の size だけ変わった。buffer↔image の copy、payload が無い | N13 |
| M37 | 一部 | §7.2 の表は header と揃った。§7.3 の見出しと header の語に二つの opcode が残る（0x21・0x30・0x37 は他の command） | N28 |
| M38 | 一部 | DRAW_MESH_TASKS・BEGIN/END_QUERY・LOAD_CONST の payload が付いた、WAIT_IDLE と単一の bind は消えた。query の結果の形と VA の hi が無い | N17 |
| M39 | 一部（新） | GhciRenderPassDesc が付いた。depth/stencil・clear の値・resolve が無く、§7.3 と header の BEGIN_RENDER_PASS の形が違う | N03 |
| M40 | 一部 | 7 dw の形に flags が付いた。8 dw の形に flags が無い | N17 |
| M41 | 一部 | topology・cull・polygon・compare・blend・write mask の値が付いた。stencil op・attachment ごとの blend・logic op の種類・MSAA・line width が無い | N14 |
| M42 | 未解決 | stride の 8 bit・12 bit・32 bit は変わっていない（回答の M42 は entry point） | N14 |
| M43 | 一部（新） | §8.2 は「最初の命令、header 無し」、§12.10 は header と interface table と PROGRAM gpu-op のまま | N01 |
| M44 | 未解決 | §8.4 と §12.10 の二通りのまま | N02 |
| M45 | 一部 | LOAD_CONST の payload（offset・count 1〜32）。c0〜c31 と c[0..511]、interface table との重なりが残る | N02 |
| M46 | 一部 | descriptor の slot は graphics と compute で共有。PSO の bind point が無い | N22 |
| M47 | 未解決 | descriptor の field の値は変わっていない（回答の M47 は render pass） | N23 |
| M48 | 未解決 | §11.5 は DRAW・LOAD_PROGRAM・INVALIDATE のまま | N15 |
| M49 | 未解決 | DEVICE_CAPS[3] は indirect dispatch を言い、command が無い | N17 |
| M50 | 未解決 | §8.6 は変わっていない | N24 |
| M51 | 一部 | FMT の register が正（§4.1）なので表は目安。FMT の bit の欠け・sRGB の attachment が残る | N19 |
| M52 | 一部 | 判定は DEVICE_CAPS[31]。2x と §12.11 の 1 か 4 が残る | N24 |
| M53 | 一部（新） | §12.8 に LOAD64・STORE64。§12.5 に encoding が無い | N24 |
| M54 | 一部（新） | DISP_SET・FLIP の payload が付いた。FLIP の record は 64 bit なのに一語、take の待ちが無い | N25 |
| M55 | 一部 | scanout は物理（§6.0・§13.2）。32 bit で 4 GiB より上に置けない件が残る | N12 |
| M56 | 一部 | DISP2 は Approved、lifecycle は M10 で決まった。EDID の経路が無い | N25 |
| M57 | 未解決 | §13.3 は変わっていない | N29 |
| M58 | 一部 | CAP_IOMMU の意味、§15.2 の aperture は直った。draft・64 KiB・GCLK_HZ の二か所・minor の版が残る | N28 |

DONE
