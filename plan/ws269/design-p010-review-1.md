<!-- design-reviewer のレビュー 1（2026-10-20、ws269 design-p010.md 版 1）。Q1 が報告の本文をそのまま置いた -->

## WS269 p010 設計の追補のレビュー 1

対象: `/home/awe/zedBSD-claude1/plan/ws269/design-p010.md` 版 1。照合した物: design.md 改訂 3.1、ws.md、phase010/phase.md、`src/drivers/gpu/powervr/`、`userland/desktop/libvulkan/`（wsi-image.c・wsi-swapchain.c）、`userland/tests/vkdemo`・`venus-frame`、`src/kern/sysctl.c`、`plan/guardrail.md`。外部の資料は、Mesa 26.2.5（GitLab API、tag `mesa-26.2.5`）と Linux v7.3（`gh api` の `ref=v7.3`）を取って読みました。どちらも拒まれていません。取った file は `/tmp/claude-1000/-home-awe-zedBSD-claude1/70c47280-c9e2-5e3f-b5d9-072cd0539dfc/scratchpad/mesa/` と `/tmp/claude-1000/-home-awe-zedBSD-claude1/70c47280-c9e2-5e3f-b5d9-072cd0539dfc/scratchpad/linux/` にあります。source と設計文書は編集していません。build・試験・QEMU は走らせていません。

### Blocker

**B1. sysctl `hw.gpu.powervr.stages` は新しい UAPI になり、「driver の中」では作れない**
- 場所: §8.1（198 行「sysctl の追加は driver の中で HAL の API ではない」）、§8.2 の段 1、p010a。
- 根拠: sysctl の名前は `src/kern/sysctl.c:45-75` の静的な表 `leaves[]` にあり、OID は `include/uapi/sysctl.h`（例 `HW_GPU_ATTACHING 4`、45-49 行）で決まります。driver が名前を登録する口はありません。design.md §2 は「新しい UAPI は作らない」と決めています。さらに、この 2 つの file は subagent の修正範囲（AGENTS.md）の外です。
- 起こる状況: p010a はすでに q1364 として割り当て済みです（master.md:40）。このまま実装すると、未承認の UAPI と kern の変更が入ります。
- 直し方: sysctl をやめます。gpu-selftest の段 1 は既存の `kern.msgbuf`（sysctl.c:59）から `powervr: ` の行を拾って写すか、段 1 自体を消します（kernel の行は画面にもう出ているため）。どうしても sysctl が要るなら、D-P6 として UAPI の差分の案を Q1 とユーザーに出します。HAL の変更ではありません。

**B2. 出典の書き方が Guardrail に反する**
- 場所: §10 の必須項目「出典の行（`Value(s) referenced:` と M の番号・file・blob SHA）を各 source file の頭に」。
- 根拠: `plan/guardrail.md:120`「file の先頭の comment には、参考にした source・引用したデータ・…出典の一覧（SHA-256 を含む）を一切書かない」「その block の直前の comment に書く」（複数行の形）。design.md §0.2 も同じです。
- 起こる状況: p010a〜m の全部の phase.md にこの誤りが写り、後でユーザーが行う監査で全部の file が違反になります。
- 直し方: 「使う block の直前の複数行の comment に `Value(s) referenced: <版> <path> (<名前>)`」に直します。内部の M 番号は comment に書きません。

**B3. free list が尽きた時に「grow の要求に応えない」と書いてあるが、これは FW を止める**
- 場所: §4.1「FW の『free list が尽きた』の通知（grow の要求）には応えず、その render を失敗にし」、D-P1、§8.1 の `fail(free list)`。
- 根拠:
  - mainline は、grow できない時も `ROGUE_FWIF_KCCB_CMD_FREELIST_GROW_UPDATE` を `delta_pages = 0` で必ず返します（v7.3 `pvr_free_list.c:360-391`）。
  - HWR の後には `FREELISTS_RECONSTRUCTION` を処理して応答します（同 599-633 行、`pvr_ccb.c:153-158`）。
  - Mesa は geometry と fragment の間に、partial render の fragment の job（`DRM_PVR_SUBMIT_JOB_FRAG_CMD_PARTIAL_RENDER`、FW の命令の種類は FRAG_PR）を**常に**積みます（`winsys/powervr/pvr_drm_job_render.c:486-512`）。geometry の命令にもそのための fence（`partial_render_geom_frag_fence`、`pvr_rogue_fwif_client.h:152-156`）があります。
- 起こる状況（推測。FW の実際の挙動は未確認）: 大きな場面で PB が尽きると TA が待ち続け、2 秒で時間切れになり、session は abandoned になります。cleanup は context が busy で失敗し、isolate か device 全体の fault に倒れ、GPU が全部使えなくなります。`fail(free list)` の行は出ません。abandoned になった HWRT の状態（RTDATA の state）が次の render に残る危険もあります。
- 直し方:
  - p010b で、FRAG_PR が CCB に無い時に OOM の FW が止まるかを mainline と Mesa から確かめる項目を足します。
  - p010d に次を入れます: GROW_UPDATE を delta 0 で返す、RECONSTRUCTION に応答する、必要なら最小の FRAG_PR（結果は捨てて失敗と報告）を積む、時間切れの後は HWRT を作り直す。
  - fail の理由は FW の通知から取ります。

**B4. compiler との境 §5.1 に、pipeline から渡す compile の入力が無く、D-P3 (a) は Mesa の形と逆になっている**
- 場所: §5.1（「compiler は SPIR-V の module と entry point と stage を受け」）、§5.3 の 2、D-P3。
- 根拠: Mesa の pco は compile の入力として、vertex の属性の format（`pco_data.h:42-44` の `attrib_formats`）、fragment の出力の format（81 行の `output_formats`）、blend の設定と blend 定数の範囲（99-101 行）を取り、pack と blend を compiler（NIR の `nir_lower_blend`）で行います（`pvr_arch_pipeline.c:2229-2259`、blend 定数は shared register、548-552 行）。vertex の format の変換も shader の中です（`pco_nir_pvfio.c`）。driver が出来上がった USC の末尾に命令を足す D-P3 (a) は、register の割り当てと出力の register を driver が知る必要があり、事実上もう 1 つの backend を作ることになります。
- 起こる状況: P2 が q1363 で compiler を今まさに作っています（queue.md:737）。境の契約を後で変えると、両方の作り直しになります。
- 直し方:
  - §5.1 に「compile の入力（vertex の属性の format、color attachment の format、blend の設定、sample の数）」を足します。
  - 要求の表に `BLEND_CONSTANTS` を足します。
  - D-P3 の推奨を (b)「compiler が pack と blend を行う」に変えます。
  - Q1 経由で P2 に急いで伝えます。

### Major

**M1. color の store に要る EOT（pixel event）の program が設計に無く、p008 の encoder にも要る命令が無い**
- 場所: §4.2（「fragment の command: … PBE の state（attachment の store）」）、§5.4、p010e。
- 根拠: Rogue の store は、tile の終わりに走る PDS の pixel event の program と USC の EOT の program（PBE の state の word を emitpix で出す）で行います（Mesa `pvr_arch_cmd_buffer.c:533-640`）。PBE の state は 2 つに分かれます。state の word は USC に、reg の word は fragment の命令の `pbe_word[8][3]`・`event_pixel_pds_info/data` に入ります（v7.3 `pvr_rogue_fwif_client.h:171-244`、Mesa `pvr_arch_job_render.c:853-879`）。一方、encoder には back end の命令が fence・ld・st・atomic と UVSW しかありません（`usc-encode.h:186-197`、`usc-encode.c:1272-1294`）。SMP（texture）・FITR/FITRP（補間）・EMITPIX は未実装です。
- 起こる状況: §5.4 の固定の program（clear・load・transfer・三角形）は、どれも「p008 の encoder で直に書く」ことができません。
- 直し方:
  - encoder を広げる Phase を独立させ、LW を付けます。
  - `usc-encode.c` は P2 の compiler も触るので、持ち主を Q1 と決めます。
  - p010e に EOT の program と pixel event の PDS を明記します。

**M2. HWRT・free list の構造と GPU の VA の heap が足りない**
- 場所: §4.1。
- 根拠:
  - HWRT は free list を 2 つ（local と global）取り、local の最小の page 数も検査されます（v7.3 `pvr_drm.h:937-963`、`pvr_hwrt.c:19・50-64`）。
  - rt_data は 2 組で、それぞれ PM の MLIST・macrotile の配列・region header を持ちます。ほかに TPC・VHEAP の表・RTC が要ります（`pvr_drm.h:902-935`、Mesa `pvr_arch_job_render.c:201-670`）。
  - region header は RGNHDR の heap（`0xDBF0000000`、256 MiB）に置きます（Mesa 483 行、v7.3 `pvr_rogue_heap_config.h:71-72`）。
  - transfer は TRANSFER_FRAG の heap（`0xE400000000`）を使います（M4）。
  - 今の session の VA は GENERAL の heap だけです（`render.c:587`）。PDS と USC の program は executor の固定の場所に置かれています（`vk-queue.c:56-67`）。pipeline や draw ごとの program を置く割り当ての仕組みはありません。
- 直し方:
  - p010d に、local と global の free list、rt_data の 2 組と各 buffer、heap の追加（RGNHDR・TRANSFER_FRAG）、PDS と USC の heap の中の割り当てを書きます。
  - 「session ごとに 1 つ」は「最初の render で作る」に直します（compute だけの session に 16 MiB を払わせない）。

**M3. render・transfer の FW の context の静的な state と、recovery の範囲が抜けている**
- 場所: §4.1「p006 の compute の context の作り方と同じ形で 2 本」。
- 根拠:
  - render の context の静的な state は、VDM の context の store と resume の task、TA の context の state の buffer を持ちます（v7.3 `pvr_rogue_fwif_shared.h:142-187`）。Mesa はそのための SR・PT の PDS・USC の program を作ります（`pvr_arch_job_context.c:465-845`）。
  - 今の compute の context は共通の部分だけを書き、残りは 0 です（`render-queue.c:1084-1093`）。
  - session は FW の context 1 つ・identity 1 つ・timeline 1 つの形です（`render-private.h:110-160`）。
  - design.md §3.5 は、stop・isolate が session の全部の FW の context を対象にすることを求めています。HWRT と free list を壊すには、FW の CLEANUP（HWRTDATA・FREELIST の種類）が要ります。
- 起こる状況（推測）: FW が geometry を context switch すると、address 0 の program を走らせて fault します。recovery が 1 つの context しか止めないと、retire が終わらずに device の fault に倒れます。
- 直し方: p010b で、context switch を止めるか program を作るかを確かめて決めます。p010d に、4 つの context の cleanup・stop_poll・isolate、HWRT と free list の CLEANUP、context ごとの identity を入れます。

**M4. transfer の形の事実が違い、見積もりも小さい**
- 場所: §6、p010f（8 LW）。
- 根拠: Mesa の transfer は、IPF の control stream（region の配列と primitive の block）を TRANSFER_FRAG の heap に置き、ISP_MTILE_BASE を heap の base からの相対で書き、ISP の render の mode を FAST_2D にします（`pvr_arch_job_transfer.c:4125-4144・4550-4565`）。PBE の word は transfer の register の `pbe_wordx_mrty[9]` に入ります（`pvr_rogue_fwif_client.h:317-358`）。Mesa のこの file は 6,065 行あります。TLA は、`bxe-4-32.h` に `has_tla` が無いので「無い」は事実です。
- 直し方: §6 を IPF・FAST_2D・heap の相対の address で書き直します（FW 側の base の設定は未確認として p010b へ）。p010f は copy・clear と、拡大縮小の blit の 2 つに分けます。

**M5. venus-frame は p010 の受け入れの program として VF2 では動かない**
- 場所: §1、§8.2 の段 12、p010m（「命令の不足を埋める」）。
- 根拠:
  - venus-frame は 1 つの fd で Venus と `GPU_RESOURCE_CREATE`・`GPU_PRESENT` を行い、既定は `/dev/gpu0` です（`venus-frame.c:106・898・919`）。VF2 では gpu0 が display の node（Venus なし）、gpu1 が render の node です。render の node には display の ops も `GPU_CAP_DISPLAY` もありません（`render.c:153-167`）。
  - README のとおり、shader も graphics の pipeline も使いません（clear と copy だけ）。
- 直し方: 受け入れを「vkdemo と gpu-selftest の clear・copy」に変えるか、venus-frame を 2 つの node に対応させる作業を Phase に明記します。design.md §6 の受け入れの文面が変わるので、Q1 とユーザーの判断が要ります。

**M6. Phase の分割・依存・LW の誤り**
- (a) compiler に Phase が無い。ws.md:42-43 は Queue の試行の番号 `q1363` を依存に書いており、Awesome Plan の依存（Phase か、名前を付けた出力）になっていません。合計 80 LW は compiler を含まないのに、design.md の p010 の 60〜100 LW は vertex・fragment を含んでいました。直し方: compiler の Phase を ws.md に作り、LW を数え直します。
- (b) p009・p007・p006 は uncleared で、実機の確かめも待っています（ws.md の表）。直し方: Phase 全体への依存ではなく、名前を付けた出力への依存（「main に merge 済みの code と host 試験」）として書き、実機で p005〜p009 が外れた時に p010 の作業が無効になる危険を §11 に書きます。
- (c) p010m の「p010l は任意」は、design.md §6 の p007「import の試験は p010」と WS277 R6 に反します。直し方: p010l を受け入れに必須にするか、決定として記録します。
- (d) §11 は「compiler の前に readback で clear の色だけを実機で通す」と書く一方、p010k は p010j（compiler）に依存しています。直し方: p010k を「clear だけの present（p010f と p007 に依存）」と残りに分けます。
- (e) p010d の実機の確かめ（`ta`・`3d` の行）には、出す仕組みがありません。kernel の render check は p010e で作るからです。clear の background も p010e の固定の program に依ります。直し方: p010d の実機の確かめを p010e に移すか、check を p010d に移します。
- (f) LW の見積もり: p010d（M2・M3 を入れる）と p010f（M4）は、8 LW ではおそらく足りません（推測）。

**M7. UAT の行で、どこで止まったかが分からない**
- 場所: §8.1。
- 根拠: 今は session に timeline 1 つ・identity 1 つで（`render-private.h`）、FW の通知（`POWERVR_NOTICE_CONTEXT_RESET 105`・`PAGE_FAULT 112`、`interface.h`）を geometry・fragment・transfer に分けられません。submit は最後の job の fence しか待たないので、`ta: fail(timeout)` と `3d: fail(timeout)` を区別できません。「この順で出る」も誤りです（gpu-selftest の `clear` は `draw` より先に transfer を流します）。
- 直し方:
  - kick ごとに自分の sync の word を UPDATE させ、FW の context ごとに identity を分けます。
  - 理由には FW の reset の理由・fault の VA・DM を入れます（例 `3d: fail(fault 0x80001234)`）。serial を使わずに調べられる、唯一の手がかりになります。
  - 「順」の記述は消します。

**M8. readback の道の事実の誤りと、shared の道に落ちない危険**
- 場所: §7 の表の readback の行（「linear の image の作成・bind」）、p010k（「CopyImageToBuffer の LINEAR」）、p010c。
- 根拠:
  - アプリが描く swapchain の image は OPTIMAL です（`wsi-swapchain.c:1522`）。readback はその twiddled の image から buffer への copy です（2791-2831 行）。LINEAR の image は shared の道の scanout だけです（`wsi-image.c:80`）。よって p010k は transfer の twiddled の読みに依ります。
  - p010c で linear の SAMPLED を出すと、libvulkan は shared の道を先に試し（`wsi-image.c:56-58`、`wsi-swapchain.c:1095-1110`）、`FORMAT_NOT_SUPPORTED`・`FEATURE_NOT_PRESENT` 以外の失敗では readback に落ちず、swapchain の作成が失敗します。ENOTSUP は `FEATURE_NOT_PRESENT` になりますが、ENOMEM・EINVAL はそれ以外の error になります（`vk-object.c:315-330`）。
- 直し方: 表を直します。p010c か p010k に、「p010l までは external の chain と export の拒否が必ず ENOTSUP になる」ことの host 試験を足します。

**M9. host 試験の golden は、bit の位置の誤りを見つけられない**
- 場所: p010c・p010e・p010f の「word の golden を M1 の値から」。
- 根拠: 同じ人が同じ XML を読んで実装と golden の両方を作るので、読み違いは両方に入ります。実機でしか分からず、UAT の往復の費用が大きいです。
- 直し方:
  - Mesa の `csbgen/gen_pack_header.py` を自分の `build/<phase>/` の中で取得した tarball に対して走らせ、出来た pack の header で同じ field の値を詰めて比べる照合を、一度だけの確かめとして行います（tree には入れず、phase.md に証拠として残す）。
  - FW の struct の offset は、mainline の `pvr_rogue_fwif_client_check.h` の値と比べます。

### Minor

1. **§0 の path の訂正**: Mesa 26.2.5 には `pvr_job_render.c` などはありません。正しくは次のとおりです。
   - `pvr_arch_job_render.c`・`pvr_rt_dataset.c`・`pvr_free_list.c`（rt dataset の作成は `pvr_arch_job_render.c`）
   - `pvr_arch_job_transfer.c`・`pvr_arch_job_context.c`・`pvr_arch_job_compute.c`・`pvr_arch_cmd_buffer.c`・`pvr_arch_pass.c`・`pvr_arch_hw_pass.c`・`pvr_arch_pipeline.c`・`pvr_arch_tex_state.c`・`pvr_arch_image.c`・`pvr_arch_formats.c`・`pvr_arch_spm.c`
   - `rogue/pvr_clear.c`、`pvr_usc.c`（EOT の program）
   - XML は `csbgen/rogue/{cr,ipf,vdm,ppp,pbestate,texstate,lls,cdm,pds,kmd_stream}.xml` で、`rogue_` の接頭辞は付きません。`volcanic/` は使いません。
   - FW の命令の struct は `pvr_rogue_fwif_client.h`（と `_check.h`）にあり、M3 の一覧に足します。既存の `interface.h` は StarFive の v6.12 の clone を出典にしています。v7.3 との差は、client の header には無く、`reset_reason` の型と enum の追加だけでした（diff で確認）。どちらの版を引用するかを揃えます。
2. **BXE の feature**: `bxe-4-32.h` に `has_tla` も `pbe_stride_align_1pixel` もありません。PBE の linestride の単位は 2（`pbestate.xml:258-259`）なので、「2 pixel」は事実です。推測の印を外せます。
3. **vertex の format**: vkdemo は `R32G32B32_SFLOAT` を使います（`renderer.c:1424`）。§3.2 の「`R32G32(B32A32)_SFLOAT`」は曖昧なので、3 成分を明記します。
4. **sampler と LINEAR の texture**: CLAMP_TO_BORDER は Vulkan の core にありますが、address mode の一覧に無く、「border は透明の黒だけ」と矛盾します。LINEAR の texture は `STRIDE_IMAGE_WORD1`（`texstate.xml:315-332`）を使うので、p010c に明記します。
5. **部分の renderArea**: 「部分は scissor で同じ結果」は誤りです。background の clear と PBE の store は tile 全体に効き、外側を壊します。p010 では部分の renderArea を断るか、PBE の clip を実装します。
6. **mip の layer**: mip が 2 段以上の時、layer の大きさは 1×1 までの全段で数えます（Mesa `pvr_image.c:174-186`）。§3.1 に書きます。
7. **vkdemo の引数**: vkdemo に `--frames` はありません（`main.c:79`）。`--duration=N` を使います。
8. **段の行と gpu-selftest の細部**:
   - 段の名前を揃えます（`render node` と `render-node`）。
   - recovery の reset の後に段の ok を出し直すかを決めます。
   - 自動で流す gpu-selftest と Keiland の display の lease が重ならないようにします。
   - fork した子は、それぞれ自分の instance を作ります。
   - lease の間に出た kernel の行が release の後に描き直されるか（`display.c:1387-1438`）は未確認です。
9. **error の値**: `vkCreateRenderPass` は仕様上 `VK_ERROR_FEATURE_NOT_PRESENT` を返せません。内部の約束と仕様の値を分けて書きます。
10. **design.md との矛盾**: design.md §3.5 の「FW の grow の要求に応える」は D-P1 と矛盾するので、置き換えを記録します。
11. **twiddle の式**: OPTIMAL の image に CPU が触らない（全部 GPU の transfer で読み書きする）なら、CPU 側の Morton の式は要りません。host 試験は大きさ（2 の冪への切り上げ）だけを見ます。
12. **memory の見積もり**: 1080p の OPTIMAL の swapchain は 2048×2048×4 = 16 MiB／枚で、全部が 4 GiB の下の物理で連続の確保です（`render-private.h:35-48`）。FW の heap（16 MiB、`mips-table.h:41`）も、render・transfer の context が増えた分を p010d で数えます。p010（vkdemo は 320×240）では小さいですが、p011 の前に表を作ります。
13. **dc8200 の zero copy**: `dc8200.zerocopy=test` の道（`display.c:39-53`）が §7 にありません。p010l で、export した image の SLC_BYPASS と loader の pitch との一致を書きます。

### D-P1〜D-P5 の推奨の妥当さ

- D-P1 (a): B3 を直し、M2（local と global、最初の render で作る）を入れるなら妥当です。
- D-P2 (a): 妥当です。Minor 8 の注意を付けます。
- D-P3 (a): 不適切です。B4 のとおり (b) に変えます。
- D-P4 (a): 妥当です。待つのは core の lock の外です（`vk-queue.c:898-912`）。ただし時間切れの後の HWRT の作り直し（B3）が条件です。
- D-P5 (a): 妥当です。

### HAL

HAL の変更（`hal.h`）は要らない、という判断に同意します。heap の追加と program の割り当ては driver の中です。問題は UAPI と kern の変更（B1）で、HAL ではありません。

### 確かめられなかった点（推測と未確認）

- FRAG_PR が無い時の OOM と、context switch の state が 0 の時の FW の挙動（推測）。
- transfer の heap の base を誰が設定するか（未確認）。
- venus-frame の wire が powervr の executor と合うか（opcode は Venus と同じ値でしたが、通るかは未確認）。present ができないことは code で確かめました。
- 実機・QEMU の試験は一切していません。

### 結論

1. **p010b**（資料の固定、コードなし）は今すぐ始めてよいです。B3・M2・M3・M4・Minor 1・2 の確かめをその範囲に足してください。
2. **p010a** は、B1・B2 を直した版 2 の後に、kernel の段の行（`stage.c`、`kern_logf`）と gpu-selftest の骨（sysctl を使わない形）に限れば始めてよいです。q1364 の範囲から sysctl を外してください。
3. **p010c** は、p010b の後に M8 の host 試験を入れれば始めてよいです。
4. **p010d〜g・p010k〜m** は、M1〜M6 を入れた版 2 の再レビューの後にしてください。
5. **p010h・p010i** は、B4 の契約を P2 と合意してからにしてください。q1363 が進んでいるので、B4 は Q1 から P2 へ急いで伝える必要があります。
