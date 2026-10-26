# WS290 design.md の敵対的レビュー（版 1 に対して）

- 対象: [design.md](design.md)（commit 2721c49fc、branch agent/p1-ws290）、範囲は [ws.md](ws.md)・[phase001/phase.md](phase001/phase.md)。
- 照合した物: amaterrace-gpu `docs/ghci.md`（4d12da1、行番号は `git show 4d12da1:docs/ghci.md` と同じ写し）、同 `include/ghci2_regs.h`・`include/ghci_cmd.h`・`plan/ws001/phase004`・`phase005`・`phase007`（4d12da1）、zedBSD の `src/drivers/gpu/ghci/`（cp.c・ghci.h・ghci.c・memory.c・pci-attach.c・display.c）、`include/drivers/gpu/gpu.h`、`src/drivers/gpu/gpu.c`（drv_gpu_register の検査）、`src/drivers/gpu/amdrdna/vk/vk-execute.c`、`plan/master.md` の decisions-log。
- 読み方: cat・grep・`git show` だけ。amaterrace-gpu・zedBSD の source と設計文書は変えていない。build・試験・QEMU は行っていない。
- 「設計の行」は design.md の行番号。「推測」と書いた物は確かめていない。

## 指摘

### R01（Critical）reset の後の VA の再生が lock の順と ring の容量で詰まる

- 設計の節: §5.4 の (4)（296 行）、§8（368〜372 行）、§6.3・D10。
- 根拠:
  - 設計は再生（`ghci_vm_replay`: 生きている全 mapping の VM_MAP を ring に置く）を cp_lock を持ったまま、`CTRL.RUN` の前（(4) の後に (5) RUN）に行う。mapping の list と VA は device.lock が守る（§8 368 行）。VM_MAP の経路は device.lock → cp_lock（§5.5 303 行、§8 370 行）。
  - 今の recover は cp_lock の中で device.lock を取らない（cp.c 1068〜1161 行。fail_sessions は cp_lock を離してから device.lock、1203〜1227 行）。ghci.h 44〜50 行の順も device.lock → display.lock → cp_lock。
  - ghci.md §5 367・368 行: RING_WPTR（doorbell）を受けると書かれているのは RUNNING だけ。IDLE は「RING_BASE, RING_SIZE, SCRATCH, etc.」。
- 起こる状況:
  1. 再生が cp_lock の中で device.lock を取ると、VM_MAP・destroy の thread（device.lock を持って cp_lock を待つ）と ABBA の deadlock。取らずに list を読むと、device.lock の下で list を変えている途中の thread と data race になり、二重の VM_MAP や、unmap の途中の mapping の再生が起こる。
  2. 再生の量は sessions × mapping（63 × 4096 の resource と executor の object）で、1 MiB の ring（VM_MAP 10 dw で約 26000 個）を超え得る。RUN の前なので CP は ring を読まず、RPTR は進まないので ring_room が永久に空かない（今の ring_room は RPTR を見る、cp.c 789〜815 行）。IDLE で doorbell を受けるかも ghci.md で未定義。
- 直し方の案: reset と再生を分ける。recover は cp_lock の中で reset・fence 語・RUN までを行い、`replay_pending` を立てて cp_lock を離す。service thread が device.lock → cp_lock の順で取り直し、RUN の後に ring の余地を見ながら（RPTR を待ちながら）再生する。再生が終わるまで新しい submit は `replay_pending` で待たせる（submit の側は cp_lock の中で flag を見る）。lock の順は「再生は device.lock → cp_lock」と §8 に明記する。IDLE の doorbell は G の項目に足す（R20）。

### R02（High）p004 の capability の組が drv_gpu_register で拒まれる

- 設計の節: §4.3 の表（220〜225 行）、D11。
- 根拠: `src/drivers/gpu/gpu.c` 1149〜1157 行「NOTIFICATION を出すなら `ops->commands` の submit・drain が要る、無ければ EINVAL」。1030 行で JOB は NOTIFICATION を要る。今の driver は NOTIFICATION を vk-node の bind でだけ出す（vk/vk-node.c 90 行）、BASE は `RESOURCE|BLOB|TRANSFER|MAPPING|SHARE|ALLOCATION_SHARE`（ghci.c 55〜56 行）。
- 起こる状況: p004 の段（`RESOURCE|BLOB|TRANSFER|MAPPING|NOTIFICATION|FENCE`、command 無し）では `drv_gpu_register` が EINVAL を返し、attach が失敗する。host 試験の stub の `drv_gpu_register` が gpu.c の検査を持たないと PASS してしまう。
- 直し方の案: p004・p005 の段から NOTIFICATION を外す（FENCE も executor が無ければ意味が無いので p007 へ）。p007 で `CAPSET|COMMAND|NOTIFICATION|JOB|JOB_CAPACITY` と recovery を同時に出す。host 試験の stub は gpu.c 1000〜1240 行の検査（または検査だけを取り出した関数）を通す。

### R03（High）per-session の VA 空間を前提にしているが、ghci.md は 1 つの page table に session の tag を持つ形とも読める

- 設計の節: §4.2 `struct ghci_vm`（198〜205 行）、§5.5（302 行）、G06。
- 根拠: ghci.md 882 行 PTE の [11:6] Session ID tag、868〜869 行 PDI/PTI は 1 組（10 + 10 bit）、am-ws001-p004 の範囲 2 は内部 register `MMU_PT_ROOT`（単数）。一方 854 行は「the session's GPU virtual address space」。session ごとに page table を持つなら PTE の tag は要らない。
- 起こる状況: hardware が「1 つの page table + PTE の tag で持ち主を検査」なら、設計の session ごとの allocator（`[GHCI_VA_BASE, GHCI_VA_LIMIT)` を各 session が同じ範囲で持つ）は 2 つの session に同じ VA を出し、後の VM_MAP が前の mapping を上書きする（別の process の memory を指す）。G06 は tag の幅（6 bit）だけを書き、この二択を問うていない。
- 直し方の案: G の一覧に「page table は session ごとか、全体で 1 つ（PTE の tag で検査）か」を C で足す。答えまでの推しは **device 全体で 1 つの VA allocator**（どちらの答えでも正しく動く。代わりに全 session の合計が 4 GiB に制限される）。`struct ghci_vm` は session の番号と mapping の list だけを持ち、allocator は `struct ghci_device` へ。

### R04（High）VM_UNMAP を置けない時・LOST の時に、GPU がまだ触れる物理を返す

- 設計の節: §5.5 unmap（304 行）、§6.3、§7（356〜364 行）、§10 の LOST（398 行）、§5.7。
- 根拠:
  - gpu.h 110〜111 行「Close and resource_destroy cannot fail」、180〜184 行 `drv_gpu_report_error` の前に「every resource destroy/close path must retain uncertain native or DMA ownership」。
  - 設計の unmap は VM_UNMAP を ring に置いて ticket を記すが、ring の満（`ETIMEDOUT`、§10 399 行）や LOST（`EIO`）で置けなかった時の扱いが無い。
  - 今の recover は reset が失敗しても `completed = fence_issued`・generation++ をする（cp.c 1130〜1146 行）ので、古い ticket は全部「done」になり遅延の解放が進む。設計の §5.4 もこの形を保つ。
  - pci-attach.c の detach は `ghci_device_stop`（ring・fence page を返す）の後に `release_bus` で Bus Master を切る（pci-attach.c 214〜221・449〜454 行）。
- 起こる状況: (a) destroy の VM_UNMAP が置けないまま物理を返すと、GPU の page table に残った mapping から返した page へ DMA（他の process の memory の読み書き）。(b) reset が失敗した（quiesce が終わらない、AXI_STUCK 相当）GPU はまだ DMA し得るのに、ERROR で完了した ticket を「過ぎた」として物理を返す。(c) LOST の device の detach で、Bus Master を切る前に ring と fence page を返す。
- 直し方の案: mapping に「unmap を置けていない」状態を足し、物理を hold したまま service thread が再試行する（成功するか、成功した reset の後の再生から外れるまで）。LOST では「物理を一切返さない（leak を記録）」を規則にし、`drv_gpu_report_error` の前に quarantine を立てる。detach と LOST では Bus Master を切ってから memory を返す（`ghci_device_stop` の中で `drv_pci_device_set_bus_master(false)` を先に）。host 試験に「VM_UNMAP の置き損じ」「reset の失敗の後に物理が返らない」を足す。

### R05（High）大きな submit の分割で、ほかの session の packet・reset が間に入る

- 設計の節: §11.3 の 3（435 行）、§5.2（288 行）、§8（370・372 行）。
- 根拠:
  - 設計は「ring の余地より大きい列は packet の境で分けて置く」「ring の満で待つ間は cp_lock を離す」。fence は submit の最後に 1 つ。
  - ghci.md 376 行: reset は「GPU pipeline states」を消す。§7・§8 の bind（PSO・vertex buffer・descriptor table・dynamic state）は CP の状態で、session ごとかは書かれていない。
  - ghci.md §6.2 436〜437 行: 満なら最古の fence を待つ。分割の途中の列には fence が無い。
- 起こる状況: (a) cp_lock を離した間に別の session の packet が入ると、後半の draw は別の session の bind した PSO・table（その VA を自分の session の空間で解く）で走り、fault（装置全体の reset）か誤描画。(b) 間に reset が入ると、前半は古い generation で捨てられ、後半だけが新しい generation で走る（bind の無い draw）。(c) ring に自分の前半しか無く fence が未発行だと「最古の fence を待つ」は待つ物が無く `ETIMEDOUT`。
- 直し方の案: 1 つの submit は cp_lock を持ったまま全部置く（今の ring_write と同じく RPTR を待つ、doorbell は分割ごとに鳴らす）。待ちは fence でなく RPTR（§6.4 450〜451 行）。どうしても離すなら、再開の時に generation を比べて変わっていれば残りを捨てて submit を失敗にし、各分割の頭で bind を全部出し直す。§8 の「cp_lock を離す」の文を直す（R12）。

### R06（High）表示は「packet だけが替わる」のではない: copied の present と cursor は BLT に依る

- 設計の節: §13（472〜475 行）、D12（658 行）、§3 の `blit.c`（112 行）、§18 の p011（565 行）。
- 根拠:
  - display.c 22 行「copied (ordinary storage): the blitter copies the storage」、562〜615 行 cursor の staging への `ghci_blit_run`、1075〜1145 行 copied の present の blit。設計は `blit.c` を p004 で捨てる。
  - ghci.md 1798〜1801 行: discrete では scan-out の image は local memory。設計は local の確保を p010 まで ENOTSUP（§5.6 316 行）。
  - display の image は `gpu_address`（aperture の 32 bit）で FB_NEXT・CURSOR に書く（ghci.h の `struct ghci_display_image.base`）。設計は `gpu_address` を無くす（§4.2 208 行）が、FB_BASE・FB_NEXT・CURSOR_BASE・flip の record が bus か VA かは G28 が record だけを問う。
- 起こる状況: p011 は G28 の答えが来ても、copied の present・cursor・idle frame（local memory）が書けない。p011 の依存（p005 だけ）と LW（16）が足りない。§13.3 の boot framebuffer の除外（p004〜p005 の attach）も FB_BASE の address の空間が決まらないと書けない。
- 直し方の案: §13 と D12 を「scan の register の意味と lease・queue・record の論理は残る。転送（copied・cursor）は G22 の copy か shader の copy（R16）、UMA は CPU の copy」に直す。p011 の依存に p009（または G22）と p010（discrete の時）を足す。G28 に「FB_NEXT・CURSOR の image と record の address の空間」を足す。

### R07（High）Bus Master を reset の前に立てる今の attach の順は、aperture の無い 2.0 では守られない

- 設計の節: §5.1 の 1・4（278・281 行）、§14 の PCI（483 行「今の pci-attach.c の形」）、G30。
- 根拠: pci-attach.c 165〜178 行は interrupt を取り Bus Master を立ててから `ghci_device_start`（174 行の注「the aperture still keeps it off until the start sets it」）。2.0 には APERTURE が無い（ghci.md 170〜173 行）。ghci.md 374 行: reset は RING_BASE・RING_SIZE・IRQ_ENABLE を保つ。
- 起こる状況: 前の kernel（warm reboot・kexec、FLR の無い function）が RUNNING のまま残した GPU は、Bus Master が立った瞬間から古い RING_BASE・fence の address へ DMA し得る。その memory は今は別の持ち主の物。
- 直し方の案: 2.0 の attach は「BAR0 を map → MAGIC・VERSION → `CTRL.RESET`（Bus Master は切ったまま。reset は MMIO だけで済む）→ READY → Bus Master を立てる → ring」。FLR が使えるなら先に FLR。G30 の推し「DMA の門は Bus Master と CTRL.RUN」をこの順と合わせて書く。

### R08（Medium）SCRATCH の確かめを reset の前に行うと、FAULTED の device を誤って拒む

- 設計の節: §5.1 の 2・4（279・281 行）、§10 の 1 行目（395 行）。
- 根拠: ghci.md §5 の表: FAULTED が受けるのは「Reads, IRQ_STATUS/ENABLE」（371 行）、HALTING は「Reads」（369 行）、RESETTING は「Reads」（372 行）。設計は identify（SCRATCH の書き読み）を reset（step 4）の前に置く。
- 起こる状況: 前の利用者が FAULTED で残した GPU では SCRATCH の書き込みが無視され、`SCRATCH の不一致` で attach しない（reset すれば使えるのに）。
- 直し方の案: 順を MAGIC・VERSION・DEVICE_CAPS（読みだけ）→ reset → READY → SCRATCH → EXTCAP にする。p004 の場面に「FAULTED で残った device を reset して attach できる」を足す。

### R09（Medium）reset の後の表示の数え直しを RUN の後に置いている

- 設計の節: §5.4 の (5)・(6)（296 行）。
- 根拠: ghci.md 1827〜1830 行「after a GPU reset the driver waits for STATUS[31] to fall (in IDLE) and reads TAKEN0 and TAKEN1」。今の recover も `ghci_display_recover_count` を `program_ring` と RUN の前に置く（cp.c 1155〜1158 行）。
- 起こる状況: RUN の後では新しい ring の FLIP（再生の後に置かれる present）が TAKEN を進め得るので、どの flip が reset の前に取られたかを取り違える（present の二重の完了か取りこぼし）。
- 直し方の案: (5) と (6) を入れ替え、「IDLE のまま STATUS[31] を待ち TAKEN0/1 を読む → RUN」にする。

### R10（Medium）detach の手順が FAULTED・LOST・表示を扱わない

- 設計の節: §5.7（321 行）。
- 根拠: ghci.md 371 行 FAULTED を出るのは CTRL.RESET だけ（HALT を受けない）。1712〜1714・1843〜1845 行 表示を止めるのは ENABLE = 0 と idle の待ち、表示は GPU の reset で止まらない（1734〜1735 行）。今の stop は RUNNING の時だけ HALT し、その後 reset して aperture を 0 にする（cp.c 147〜166 行）。設計は「HALT → HALTED を待つ → CTRL = 0」で、RING_BASE は ring を指したまま。
- 起こる状況: FAULTED・LOST では HALTED を待って timeout。p011 の後、driver の framebuffer を scan している DISP2 が、返した memory を読み続ける。IDLE に戻った CP の RING_BASE が返した ring を指したまま（次に RUN を書く者がいれば読む）。
- 直し方の案: 「(p011 の後) 表示を消し STATUS.idle を待つ → RUNNING なら HALT → 状態に依らず CTRL.RESET → Bus Master を切る（R04）→ memory を返す → window を開ける」。

### R11（Medium）reset の失敗の数え方が文書の中で食い違う

- 設計の節: §5.4（296 行の (2)「落ちなければ lost」と末尾「3 回続けて失敗したら lost」）、§6.1（331 行）、§10（398 行）。
- 起こる状況: 実装者が 1 回で LOST にするか 3 回試すかを決められない。3 回なら、失敗の間に memory を返さない規則（R04）と、どの失敗（RESET が落ちない、READY が立たない、reset の後の liveness の fence が来ない、reset が成功しても直後にまた fault）を数えるかが要る。待つ条件も §5.1 の 4（CTRL.RESET が落ち READY）と §5.4（RESETTING が落ち READY）で違う。
- 直し方の案: 「RESET が `GHCI_RESET_WAIT_MS` で落ちない・READY が立たない → 即 LOST」「reset は成功したが N 秒以内に再び fault・hang が 3 回 → LOST」に分けて §5.4・§6.1・§10 を揃える。待つ条件は CTRL.RESET = 0 かつ RESETTING = 0 かつ READY = 1 に統一。

### R12（Medium）今の code の説明が事実と違う（cp_lock を離す・cache_clean）

- 設計の節: §5.2（288 行「cp_lock を離して fence_wake で眠る。今の `fence_window_wait` の形」）、§8（370 行）、§9.3（389 行「今の `cache_clean` の相当」）。
- 根拠: `fence_window_wait` は cp_lock を持ったまま `ghci_cp_wait` で眠る（cp.c 852・860・974〜997 行、注「cp_lock is held」）。`ring_room` も cp_lock を持ったまま 1 ms ごとに RPTR を読む（cp.c 789〜815 行）。host memory の ring に cache clean は無く、非 coherent の時は uncached の view で済ませている（cp.c 913〜920 行、pci-attach.c 372〜395 行、memory.c 617〜645 行）。
- 起こる状況: 「今の形」を写すつもりで lock を離す実装にすると R05 の問題が生まれる。cache clean を足すと uncached の view と二重になる（害は無いが根拠の無い code）。
- 直し方の案: §5.2・§8 を「cp_lock を持ったまま RPTR・fence を有界に待つ（今の形）」に直し、離す場合の条件は R05 の案で書く。§9.3 は「coherent なら何もしない、非 coherent は uncached の view（今の形）なので clean は要らない」に。

### R13（Medium）session の失敗の報告の意味を取り違えている

- 設計の節: §5.4 末尾（296 行「session は落とさない（drv_gpu_report_session_error で client に知らせる）」）、§10 の fault（396 行）、D10（656 行）。
- 根拠: gpu.c 858〜861 行 `drv_gpu_report_session_error` は `gpu_session_fail_locked` で session の以後の submit を止める（sticky）。gpu.h 186〜189 行。今の `fail_sessions` は旧 generation に未完の仕事を持つ session 全部に報告する（cp.c 1203〜1227 行）。
- 起こる状況: (a) 報告した session は落ちるので、D10 の「device lost を避けられる」は未完の仕事が無かった session にだけ当たる。(b) §10 の「G14 の session の欄が有ればその session だけ」に従うと、装置全体の reset で仕事を失ったほかの session に知らせず、Vulkan の側は半分しか描かれていない memory を正しい結果として使い続ける。
- 直し方の案: 「reset で未完の仕事を失った session は全部 `report_session_error`（今と同じ）。fault の session の特定は log と R23 の方針にだけ使う」と書く。D10 の理由を「仕事の無かった session は続けられる」に直す。

### R14（Medium）VM_UNMAP は GPU 全体の drain なので、object ごと・submit ごとの map/unmap は GPU を止める

- 設計の節: §7（358・360 行）、§11.3 の 2（434 行）、§5.5、§4.2 の `extents`。
- 根拠: ghci.md 885〜889 行: VM_UNMAP は全 unit の in-flight の読み書きが drain するまで待ち、TLB を flush してから RPTR を進める。設計は PSO（128 byte）・descriptor table・program・submit ごとの一時の memory を、それぞれ確保して session の VA に map し、destroy・submit の終わりで unmap する。VM の単位は 4 KiB（866 行）。
- 起こる状況: pipeline・descriptor set の create/destroy や毎 frame の submit ごとに VM_UNMAP が入り、そのたびに全 session の GPU の仕事が drain する（他の session の負荷で frame が詰まる）。128 byte の PSO が 4 KiB を占め、`ghci_range_map` の extents（今は `GHCI_EXTENTS_MAX` 4096）を使い切る。
- 直し方の案: session ごとに executor の heap（例 1〜16 MiB を一度だけ VM_MAP）を持ち、PSO・table・program・一時の memory はその中を sub-allocate し、返す時は fence で待つだけ（unmap しない）。VM_MAP・UNMAP は VkDeviceMemory と import の単位だけにする。§7 の表と §11.2 の `vk-memory.c`・`vk-pipeline.c` の役を直す。

### R15（Medium）G24 の推しに実現できない所と、ghci.md・include の読み違いがある

- 設計の節: §19 の G24（615 行）。
- 根拠:
  - ghci.md 674〜675 行: 属性の format は 4 bit（属性ごと）。VkFormat の番号（core だけでも 0〜184）は入らない。推し「format の符号は VkFormat の番号をそのまま」は属性には使えない。
  - `include/ghci_cmd.h` 94〜105 行は blend factor を 0〜11 で定め、10 = SRC1_COLOR・11 = SRC1_ALPHA。VkBlendFactor では 10 = CONSTANT_COLOR、15 = SRC1_COLOR。推し（VkBlendFactor の番号）は include と食い違うのに触れていない（G27 では include の `GHCI_STAGE_*` を正にする推しで、方針が揃っていない）。
  - ghci.md 676〜677 行: 属性の offset は 8 bit。Vulkan 1.0 の必須の limit `maxVertexInputAttributeOffset` は 2047 以上（Vulkan 1.0 仕様の Required Limits。repo の外の事実）。G24 は「0〜7 だけ」を書くが幅の不足は書かない。
  - ghci.md 516 行: `CMD_BIND_VERTEX_BUFFERS` は binding ごとの `stride_bytes` を持つ。G24 の「binding ごとの stride（PSO は vertex_stride 1 つ）」が欠けている、という書き方は誤り。問題は PSO の `vertex_stride`（670 行）とどちらが勝つかの食い違い。
- 直し方の案: 属性の format は「GHCI の 4 bit の表（または 8 bit に広げる）を ghci.md に書く」を推しにする。blend factor は include の 0〜11 を基に Vulkan の CONSTANT_* を足す表を推すか、VkBlendFactor に揃えるなら include の直しも項目に入れる。offset の幅（Vulkan の 2047）と stride の優先を G24 に足す。

### R16（Medium）copy・clear・event を「待ち（C）」としすぎている

- 設計の節: G22（613 行、C）、G23（614 行）、§11.4（450〜452 行）、§18 の p009・p010。
- 根拠: amdrdna の executor は copy・blit・buffer と image の間の copy・clear を自分の program の quad の draw で行う（`src/drivers/gpu/amdrdna/vk/vk-execute.c` 22〜29 行）。設計はこの executor を型にしている（§11.1）。kernel の中に GSI1 の compiler があり、`CMD_DISPATCH` と storage buffer（kind 6、ghci.md 1500 行）で fill・copy の compute を書ける。vkCmdWaitEvents は同じ queue の GPU が立てた event なら `CMD_BARRIER` の順で満たせる。
- 起こる状況: G22 の回答まで p009・p010（local memory の 0 埋めも）を止める計画になり、WS の進みが必要以上に回答に縛られる。
- 直し方の案: G22 の推しを「専用の command が来るまで executor の compute program で copy・fill・clear・resolve を行う（buffer の descriptor G20 と render target G21 は要る）」にし、重さを H に下げる。local memory の 0 埋めも compute の fill で可。WaitEvents は「GPU が立てた event は barrier、host の vkSetEvent を待つ時だけ待ちの command が要る」に分ける。

### R17（Medium）Phase の依存に抜けがある

- 設計の節: §18 の表と依存の図（556〜580 行）。
- 根拠と起こる状況:
  - p006（compiler の接続、依存 p002 だけ）は `isa.c` と options を DEVICE_CAPS から埋める（p004 の identify の field）と、「compiler を build に戻す」（p004 が外す）を含む。p004 より前に実行すると、外されていない物を戻すことになり、DEVICE_CAPS の field も無い。→ p006 の依存に p004 を足す。
  - p011（依存 p005）は R06 のとおり p009（または G22）・p010（discrete）・p007（libvulkan の present は VkDeviceMemory の blob）に依る。
  - p003 の「要る回答: なし」は、model の VM（G05 の opcode・G06・G08）を推しの定数で書く事を隠す。→「G03・G05・G06・G08・G11 を推しで」と書く。
- 直し方の案: 図と表を直す（`p004 → p006`、`p007・p009・p010 → p011`）。

### R18（Medium）Phase の受け入れが自分の作業と食い違う

- 設計の節: §18 の p002（556 行）と p004（558 行）。
- 根拠と起こる状況:
  - p002 は「kernel の build は p004 まで変えない」と言いながら、build に入っている `check.c`（ring の単位、native の検査を外す）と `ghci-words.h`（native・blit・DISP_SET の宣言を外す）を直す。今の `cp.c`（`ghci_ring_plan` を dword で使う、cp.c 801 行）・`state.c`・`native.c`・`blit.c` は build に入ったままなので、`CONFIG_DRIVER_PCI_GHCI=y` の build が p002〜p004 の間で壊れるか、ring の単位が黙って変わる。
  - p004 の受け入れ「1.1 の register 名・command・STATE の code が tree に無い（grep で 0）」と「vk/・display は p007・p011 まで build の外」は両立しない（vk/・display の 1.1 の code は tree に残る）。
  - p004 で vk/・display・compiler を build から外すと、`ghci.c`（`ghci_vk_node_bind`、display の ops）・`cp.c`（`ghci_display_recover_*`・`ghci_vk_reset`・`ghci_vk_service`・`ghci_display_service`、cp.c 735・742・1068・1156・1190 行）・`isa.c`（compiler）の呼び出しの扱い（stub か条件か）が書かれていない。
- 直し方の案: p002 は新しい関数を別名で足し（例 `ghci2_ring_plan`）、古い物は p004 で消す、と書く。p004 の grep は「build に入る file」に限る。p004 の作業に「vk/・display・compiler を呼ぶ所を外す（または空の stub を置く）」を明記する。

### R19（Medium）試験の設計が、この設計の危ない所を覆わない

- 設計の節: §15.2・§15.3（500〜528 行）、§22 の 2。
- 根拠と起こる状況:
  - stub は 1 thread（504 行）なので、lock の順（R01）、handler と recover の競り、2 つの session の submit の交互（R05）を見つけられない。
  - model の範囲（503 行）に DISP2 の flip・record・TAKEN・event、fill・copy・query の command、MEM の local memory の振る舞いが無いが、p009・p010・p011 の場面はそれを要る。model を広げる作業が どの Phase にも LW にも無い。
  - 次の場面が無い: R04（unmap の置き損じ、LOST で物理を返さない）、R07・R08（FAULTED で残った device の attach、Bus Master の順）、R01（ring より大きい再生）、R02（register の検査を通る capability）、session の数の上限（64 番目の open）、2 つの session に同じ VA を出さない（R03）。
- 直し方の案: stub に lock の順の記録と検査（取った順を記録し、決めた順に反すれば FAIL）を入れる。stub の sleep の hook で「待ちの途中に割り込み・reset・別の session の submit を入れる」場面を作る。model の拡張（DISP2・転送・MEM）を p009・p010・p011 の作業と LW に入れる。上の場面を §15.3 に足す。

### R20（Medium）ghci.md の疑問の一覧に抜けている項目

- 設計の節: §19（590〜629 行）。
- 根拠と項目（それぞれ driver の挙動を決める）:
  1. CP の状態ごとに受ける register の書き込み: IDLE が RING_WPTR・IRQ_STATUS の W1C を受けるか（367 行の「etc.」）。R01 の再生の順と §5.1 の 5 に要る。
  2. `CTRL.IRQ_GLOBAL` が reset で保たれるか消えるか（374〜376 行の両方の list に無い）。
  3. IRQ_ENABLE = 0 の時に IRQ_STATUS の bit が立つか（283〜284 行は「IRQ_ENABLE が 1 で…assert」）。poll の device（IRQ_ENABLE = 0）が fault と fence を見られるかに関わる。
  4. CP が自分で取る memory: scratch（1531〜1536 行「the command processor allocates … bytes of GPU memory」）を 2.0 でどこから取るか（1.1 の HEAP は 621 行で廃止側）。G08 の page table の pool と同じ問い。
  5. VM_MAP の約束: gpu_va・size の揃え（`ghci_cmd.h` の注では 4 KiB）、既に map された範囲への VM_MAP（上書きか fault か）、一部だけ map された範囲の VM_UNMAP。R01 の再生、R03 に関わる。
  6. bind の状態（PSO・vertex buffer・descriptor table・dynamic state）が session ごとか ring 全体か、session が替わる時に消えるか。R05・R22 に関わる。
  7. DISP2 の FB_BASE・FB_NEXT・CURSOR_BASE の address の空間（R06）。
- 直し方の案: G32〜G38 として足し、それぞれに推しと止まる Phase を付ける。

### R21（Medium）host memory の VM_MAP に COHERENT・WC の flag をどう付けるかが無い

- 設計の節: §5.5、§9.2（384〜385 行）、§4.2 の `flags`（193 行）。
- 根拠: ghci.md 861〜862 行 `GHCI_VM_FLAG_COHERENT`「Hardware-enforced cache coherency with CPU host」・`GHCI_VM_FLAG_WC`、880〜881 行 PTE の C・WC。一方 §11.4（968〜971 行）は「GPU は coherence を外さない、No Snoop を付けない」。
- 起こる状況: COHERENT を付けない mapping で GPU が snoop しない経路を使う実装なら、PC（coherent、cached の direct map）で CPU の書いた vertex・uniform を GPU が古い値で読む。逆に付けると性能の差が出る実装もあり得る。設計はどちらにするか書かない。
- 直し方の案: 推し「host memory は `host_uncached` に依らず常に COHERENT、WC は付けない」を §5.5 に書き、G の項目（§11.2 の flag と §11.4 の関係）を足す。

### R22（Medium）executor が「この command buffer の中で bind した状態」を検査する事、executor の memory を user に見せない事が書かれていない

- 設計の節: §11（405〜461 行）、§1.2 の「ring を書くのは kernel だけ」。
- 根拠: CP の bind の状態が ring 全体の物なら（R20 の 6）、bind の無い draw は直前の別の session の PSO の VA を自分の session の空間で解く。ghci.md 1084〜1085 行（1.1 の PROT の節だが同じ考え）「The driver does not put an address that a client chose into the CP-executed micro-ops」。PSO・descriptor・program を client が map できると、kernel で compile して検査した program（Guardrail「SPIR-V の compile は kernel」）を client が書き換えられる。
- 起こる状況: 悪意の client が bind 無しの draw を送ると fault → 装置全体の reset（他の session の仕事が全部失われる、R23）。executor の heap が `resource_map` で map できると検査の意味が無くなる。
- 直し方の案: `vk-execute.c` は draw・dispatch の前に「同じ command buffer で pipeline・その pipeline が使う set・必要な dynamic state が bind 済み」を検査し、欠ければ submit を拒む（記録の時点で VK_ERROR）。各 submit の頭で bind を全部出す。executor の memory は `resource_map` の対象にしない、と §7 と §11.2 に書く。

### R23（Medium）1 つの session の fault が何度でも装置全体を reset させる

- 設計の節: §10（396〜397 行）、§5.4。
- 根拠: ghci.md 791〜794 行 回復は装置全体。設計は fault・hang ごとに reset し、session は報告の後も（未完の仕事が無ければ）続く。原因の session を止める方針が無い。
- 起こる状況: shader の範囲外の書き込みや無限 loop を繰り返し送る client が、ほかの全 client の仕事を毎回失わせる（DoS）。
- 直し方の案: fault の session（G14 の欄、無ければ hang の時に未完の最古の ticket の session）を数え、短い時間に N 回なら その session を `report_session_error` で止め、以後の submit を拒む。答えが無く特定できない間は「未完の仕事を持っていた全 session」を同じく数える。

### R24（Medium）session の close の drain が終わらない時と、session の番号の再利用が決まっていない

- 設計の節: §5.5 の close（306 行）、§6.2（339 行）、§7（355 行）。
- 根拠: gpu.h 110〜111 行 close は失敗できない。設計の close は「全 unmap → ticket を待つ → 物理を返す → 番号を返す」で、待ちが timeout（GPU の hang）した時が無い。G06 で session を壊す command が無く、G08 で reset が page table を消すかも未定。
- 起こる状況: hang の間の close で、待ちを諦めて物理を返せば R04 と同じ DMA の危険、番号を返せば次の open が同じ番号を得て、GPU の page table に残る古い mapping（返した物理を指す）をそのまま持つ。
- 直し方の案: close は待たずに「VM_UNMAP を置いた mapping と物理を deferred に積み、番号も deferred（その ticket が過ぎるか、成功した reset の後で、かつ page table が消える答えが有る時だけ返す）」にする。§6.2 に `CLOSING`（番号は予約のまま）→ `RETIRED` を足す。

### R25（Low）vkCmdSetEvent・ResetEvent を CMD_FENCE で書くのは §9.1 の「fence は ring ごとに単調に増える」と合わない

- 設計の節: §11.4（452 行）。
- 根拠: ghci.md 762〜765 行。CMD_FENCE の値を event の 1・0 に使うと単調でない。hardware が最後の seq を内部で比べる・保つ（G11 の 48 bit）なら壊れる（推測）。
- 直し方の案: G23 に「CMD_FENCE を device の fence 以外の任意の値に使ってよいか」を足す。答えまで event は R16 の compute の 1 語の書き込みで。

### R26（Low）ring の dword を little endian で書く前提が書かれていない

- 設計の節: §4.4（253 行）、§9.3。
- 根拠: ghci.md 149 行 register は little endian。設計は PSO・descriptor を byte で書いて endian を避けるが、ring・command の dword は `ring_put` で native の `uint32_t` のまま（cp.c 758〜768 行）。zedBSD には big endian の platform（ppc・m68k・sparc）がある。
- 直し方の案: 「対象は little endian の platform（amd64・riscv64）だけ。他で build する時は dword を LE で書く」と §16 に書く（または dword の encoder も byte で書く）。

### R27（Low）出典・数の誤り

- 設計の節: §0・§3・§19・§20・§18。
- 根拠と内容:
  - `src/drivers/gpu/ghci/` は 49 file（`find`・`git ls-tree 660281830` とも 49）、設計は 50（15 行）。合計の 33136 行は合う。
  - `compiler/*` は 10588 行（`wc -l`）、設計は 10432（120 行）。
  - `gsi1_isa.h` と 4d12da1 の差は 1 行（2 行目 "Amaterrace GPU" → "zedBSD"）、設計は「2 行」（120・465 行）。
  - G13 の「§3.3 277 行 STATUS[31:5] reserved」は 278 行。
  - G11 は 607 行「Emits a 64-bit sequence counter」（48 bit の seq とも食い違う）を挙げていない。
  - `gpu-op.h` の範囲が §14（486 行: 2066〜2076）と U1（635 行: 2016〜2077）で違う。
  - §18 の「合計 176 LW（2 + … を足した値: …）」の「2 + …」は意味をなさない（値 176 は合う）。
- 直し方の案: 数と行を直す。

### R28（Low）G04・G06 の根拠に ghci.md の定義と既存のユーザーの決定が抜けている

- 設計の節: G04（595 行）、G06（597 行）。
- 根拠: ghci.md 114 行「GPU address: … 32-bit address space」、120 行「Session: … named by the job tag of the client's packets」（2.0 の header に job tag が無い事の直接の根拠）、1924 行「GPU host addresses (section 11.2) are PCIe bus addresses」。`plan/master.md` 705 行 2026-10-24 のユーザーの決定（「プロセスIDの使用開始、使用終了のオプションコマンドを追加し、未対応なら未対応と応答させる…これらはCapabilityとして見える」、Q1 の推し「ID は ring の job tag だけから」に同意）。
- 起こる状況: G06 の推し（session の開始・終了の command）は既にユーザーの方針と同じ形なのに、新しい判断としてユーザーに再び問うことになる。「未対応なら未対応と応答」は 2.0 に返事の仕組みが無い（G08）点と結べていない。
- 直し方の案: G04・G06 に上の行と決定を引用し、「方針は 2026-10-24 に決まっている。2.0 の command の形と、返事の無い 2.0 での未対応の知らせ方だけを問う」に絞る。

## 件数の内訳

| 重さ | 件数 | ID |
| --- | --- | --- |
| Critical | 1 | R01 |
| High | 6 | R02〜R07 |
| Medium | 17 | R08〜R24 |
| Low | 4 | R25〜R28 |
| 合計 | 28 | |

確かめていない事: gpu.c の検査は読んだだけで、p004 の組で実際に `drv_gpu_register` を呼んではいない（R02）。Vulkan の `maxVertexInputAttributeOffset` の最小値 2047 は Vulkan 1.0 の仕様からの知識で、repo の中では確かめていない（R15）。R25 の hardware の振る舞いは推測。
