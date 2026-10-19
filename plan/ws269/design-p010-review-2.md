<!-- design-reviewer の差分の確認（2026-10-20、ws269 design-p010.md 版 2）。Q1 が報告の本文をそのまま置いた -->

## WS269 p010 設計の追補 版 2 の差分の確認

対象は `/home/awe/zedBSD-claude1/plan/ws269/design-p010.md` の版 2（7541d5612）です。照らし合わせた物は次のとおりです。

- `git diff 8f61cd9e5 7541d5612 -- plan/ws269`
- `plan/ws269/phase010b/phase.md`、`plan/ws269/ws.md`、`plan/queue.md` の q1367 の行
- `src/drivers/gpu/powervr/`、`userland/desktop/libvulkan/`
- 資料: 前回の scratchpad の `mesa/`・`linux/` と、`p010b/`

足りなかった file は今回取りました（拒否はありませんでした）。置き場所は `/tmp/claude-1000/-home-awe-zedBSD-claude1/70c47280-c9e2-5e3f-b5d9-072cd0539dfc/scratchpad/r2/` です。

- v7.3 の `pvr_vm.c`・`pvr_queue.c`・`pvr_cccb.c`
- Mesa 26.2.5 の `winsys/powervr/pvr_drm.c`・`winsys/pvr_winsys_helper.c`

file は編集していません。build・試験・QEMU も走らせていません。

前回のレビュー 1 に、私の誤りが 2 つありました。下の M-1 と M-2 で正します。

### §12 の対応表の確かめ

| 指摘 | 判定 |
| --- | --- |
| B2・B4・M1・M8・M9、M6 (a)(b)(c)(d)、Minor 1〜4・6〜13 | 本文で直っている |
| B1 | 直っていない。D-P6 としてユーザーの判断に回した。記録の扱いとしては妥当 |
| B3 | 方向は正しい。ただし M-1・M-3 の誤りが新しく入った |
| M2 | 直ったが、mainline の static data area が抜けている（M-4） |
| M3 | 直った。ただし M7 の直しの中に事実の誤りがある（M-5） |
| M4 | 直った。ただし clip（M-2）と奇数の stride（Minor 3）が抜けている |
| M5 | D-P7 に回した |
| M6 (e) | 解けていない。「p010n の phase.md で決める」は先送りで、答えはもう決まっている（M-6） |
| M7 | 一部だけ直った（M-5） |
| Minor 5 | 私の元の指摘が誤りだった（M-2） |
| Minor 10 | design.md:315 に Q1 の追記がある |

### Blocker

ありません。ただし M-1〜M-4 は、関係する Phase（p010d・p010n・p010e・p010f）を始める前に必ず直してください。

### Major

**M-1. 時間切れの後に「HWRT を CLEANUP して捨て、次の render で作り直す」は、p009 の abandoned の規則と矛盾する（解放後の使用の危険）**
- 場所: §2（53 行）、§4.1「時間切れ・HWR の後」（118 行）、D-P4。
- 根拠:
  - 時間切れの session は `abandoned` になります（`vk-queue.c:912-916`）。以後の submit は全部 EIO です（`vk-queue.c:285-286`）。
  - core が触り得る memory は「stays with the core」として device の reset まで解放しません（`vk-queue.c:22-25`、`vk-queue.c:208-216`）。
  - つまり、その session に「次の render」はありません。止まった context に CLEANUP を送っても busy が返り続けます。
  - HWR の後については、mainline は HWRT を作り直していません。RECONSTRUCTION で state を `RTDATA_STATE_HWR` にするだけです（p010b の表）。
- 起こる状況: 文面のとおりに作ると、FW がまだ参照している HWRT・free list の memory を返すか、busy の再試行で止まり続けます。
- 直し方:
  - 時間切れの後の HWRT・free list は executor の memory と同じ扱いにし、core に残します（isolate・reset の後に返す）。
  - HWR の後は RECONSTRUCTION の応答だけにします。
  - CLEANUP の busy の再試行には上限を付け、超えたら isolate に倒します。
  - これは私がレビュー 1 の D-P4 に付けた条件の誤りです。

**M-2. 「PBE の clip は p011」は誤り。clip は PBE の reg の word の必須の field**
- 場所: §4.3（133 行）、§6、p010e・p010f。
- 根拠:
  - `pbestate.xml` の REG_WORD0 の `minclip_x`（266 行）と、REG_WORD1 の `minclip_y`・`maxclip_x`・`maxclip_y`（277-281 行）。
  - Mesa は clip に常に「image の extent と renderArea の交わり」を入れます（`pvr_arch_cmd_buffer.c:1210-1217`）。
- 起こる状況:
  - clip を 0 のままにすると、pixel (0,0) しか書かれません。
  - 最大の値にすると、16 の倍数でない高さ（1080 など）の LINEAR の image や buffer に tile の端まで書き、隣の memory を壊します（transfer の PBE も同じです）。
- 直し方:
  - p010e と p010f で、clip を常に「extent ∩ renderArea／dst の矩形」にすることを必須にします。
  - そうすると、部分の renderArea も color なら受けられます。外側は store されないためです。断るのは depth の STORE がある時だけで足ります。
  - レビュー 1 の Minor 5（「外側を壊す」）は、clip を見落とした私の誤りでした。

**M-3. partial render と render の kick の組み方が、Mesa・mainline の形と違う（Q1 の「同じ形で避ける」に反する）**
- 場所: §4.1 の free list、§4.2、p010n。
- 根拠と直し方:
  - (a) **`pds_pr_bgnd` が無い**:
    - 根拠: fragment の reg には `pds_pr_bgnd[3]` があります（`pvr_rogue_fwif_client.h:232`）。Mesa は本番の fragment に `pr_pds_bgnd_reg_values` を必ず入れます（`pvr_arch_job_render.c:1077-1083`）。FRAG_PR の側は、別の pbe の word と pixel event を書き換えています（1383-1405 行）。
    - 起こる状況（推測）: PR が起きた後の本番の fragment が、address 0 の PDS を走らせます。その結果は `ta: fail(free list)` ではなく fault です。
    - 直し方: `pds_pr_bgnd` に §5.4 の「attachment の load」の background を入れます。FRAG_PR の EOT は本番と同じにします。こうすると、単一 sample の color は SPM なしでも正しい画になり得ます（推測）。
  - (b) **`partial_render_geom_frag_fence` の意味が違う**:
    - 根拠: mainline はこの fence に、**geometry の queue の timeline の UFO** と「geometry の seqno − 1」を入れます（`pvr_queue.c:718-727`）。
    - 直し方: §4.2 の「FRAG_PR と結ぶ」を直します。
  - (c) **combined kick と FENCE_PR**:
    - 根拠: mainline は geometry と FRAG_PR を対にして、`ROGUE_FWIF_KCCB_CMD_COMBINED_GEOM_FRAG_KICK`（117）で 1 回で kick します（`pvr_job.c:640-680`、`pvr_queue.c:777-800`、`pvr_cccb.c:242-267`）。待ちは全部 `CCB_CMD_TYPE_FENCE_PR`（215）で書き、本番の fragment は geometry の完了を FENCE_PR の最後に置きます（`pvr_queue.c:677-713`）。zedBSD は今、KICK 101 だけです（`render-queue.c:1185-1189`）。
    - 直し方: p010n にこの形を明記します。
  - (d) **kick の cleanup ctl**:
    - 根拠: HWRT を使う kick は、`hwrtdata.cleanup_state` の address を `cleanup_ctl_fw_addr` に付けます（`pvr_cccb.c:197-211`、FRAG_PR だけは付けない）。zedBSD は `POWERVR_KICK_CLEANUPS = 0` です（`render-queue.c:1189`）。
    - 起こる状況（推測）: 付けないと、FW の HWRTDATA の CLEANUP が使用中の HWRT を「済み」と答えるか、busy のまま止まります。
    - 直し方: p010d・p010n に足します。

**M-4. mainline の static data area（各 VM の固定の場所）が設計に無い（推測: FW か硬が使う）**
- 根拠:
  - mainline は、各 heap の先頭に固定の区画を出しています（`pvr_vm.c:912-943`）:
    - GENERAL+0: MCU fence（「must point to valid physical memory」、`pvr_drm.h:346-352`）
    - GENERAL+128: YUV の係数
    - PDSCODEDATA+0: VDM sync の PDS
    - PDSCODEDATA+128: EOT の PDS の code
    - USCCODE+0: VDM sync の USC
  - 各 heap の割り当ては、その後ろの page から始まります（GET_RESERVED_SIZE）。
  - Mesa は VM ごとにこの区画を埋めます（`pvr_winsys_helper.c:281-350`）。VDM sync を Mesa の他の code は参照していないので、FW 側が使うと読めます（推測）。
  - zedBSD の session の GENERAL の割り当ては base+0 から始まっています（`render.c:587`）。
- 起こる状況（推測）: cache の flush の時に MCU の fence が session の最初の resource を上書きするか、未写像で fault します。これは p009 の実機の compute にも関わり得ます。
- 直し方:
  - p010d の heap の節に、この 5 区画の確保と中身を足します（VDM sync は USC の nop と PDS の DOUTU で、今の encoder で書けるはずです。推測）。
  - GENERAL・PDS・USC の割り当ては次の page から始めます。
  - p009 の実機の前に、Q1 が知っておくべき危険として残します。

**M-5. FW の通知の事実の誤り（M7 の直しの中）**
- 場所: §4.1（105 行）、§8.1（258 行）。
- 根拠:
  - 112 は `ROGUE_FWIF_FWCCB_CMD_CONTEXT_FW_PF_NOTIFICATION` で、**FW 自身**の page fault です（中身は `fw_fault_addr` だけ、`pvr_rogue_fwif.h:1368-1371・1401`）。zedBSD も device lost として扱っています（`render-queue.c:1506-1509`）。
  - context の GPU の fault は、105 の `context_reset_data` に入ります（`pvr_rogue_fwif.h:1347-1366`）。中身は `reset_reason`、`dm`、`flags`（`FLAG_PF` bit 0、`FLAG_ALL_CTXS` bit 1、1343-1345 行）、`fault_address` です。
- 直し方:
  - 段の行の `fault <va>` は 105 の `fault_address`（`FLAG_PF` の時だけ）から取ります。
  - 段への割り当ては `dm` の field で行えば、identity を 4 つに分ける必要はありません。mainline も render の context の geometry と fragment で 1 つの id です（`pvr_context.c:1103`）。
  - `FLAG_ALL_CTXS` の時は、全部の context を対象にします。

**M-6. Phase の依存の誤り**
- (a) **p010n の render check（clear）と `3d` の実機の確かめは、O-ENC と p010e に無条件に依る**:
  - fragment の kick は、pixel event の PDS（`event_pixel_pds_info/data`）と EOT の USC（EMITPIX）が無いと走らせられません。clear の background には、色を出す USC が要ります。
  - EOT は p010e の範囲で、EMITPIX は O-ENC です。§8.3・p010n の「O-ENC（足りなければ）」「p010e に移すかは p010n の phase.md で決める」は、もう答えが決まっています。
  - 直し方: p010n の確かめを host 試験だけにし、`ta`・`3d`・render check の実機は p010e に移します。p010n の O-ENC の条件は外します（SR・PT が今の encoder で書けるなら）。
- (b) **p010f と p010n が並行すると、transfer の context の持ち主が無くなる**:
  - p010n は「4 つの context の identity・recovery」と「kick ごとの sync の word」を作ります。一方、transfer の context を作るのは p010f で、p010f は p010n に依っていません。`transfer: fail(timeout)` も sync の word が要ります。
  - 直し方: p010f に O-p010n を足すか、transfer の context の recovery・sync の word を p010f の範囲に明記します。
- (c) **O-ENC の範囲が決まっていない**:
  - 要る命令の一覧は「p010n・p010e・p010f の phase.md に書いて渡す」（§5.3 の 3、§5.4）とあり、p013 の完了の条件が後から変わります。
  - 直し方: 固定の program ごとの命令の種類を Mesa（`pvr_usc.c`・`rogue/pvr_clear.c`・`pvr_arch_job_context.c`・`pvr_winsys_helper.c`）から今数え、§5.4 に書いて O-ENC の中身を固定します。
- (d) LW（推測）: p010n 10 LW と p010e 8 LW は少ないと見ます。
  - p010n: p006 の session を 4 つの context に作り替え、SR・PT（Mesa で約 380 行）、combined kick を入れる。
  - p010e: EOT と background に加え、kernel の中の三角形（vertex の USC と PDS、VDM・PPP）を作る。

### Minor

1. **RGNHDR の事実が違う**:
   - mainline は BRN63142 の無い GPU では RGNHDR の heap の size を 0 で返し、範囲の検索からも外します（`pvr_vm.c:1055-1061・1123-1127`）。Mesa はその時、region header を GENERAL の heap に置きます（`pvr_drm.c:386-389・601-604`）。
   - BXE（BRN63142 なし）での「Mesa と同じく無条件に使う」（§0 26 行、§4.1 98 行）は誤りで、Q1 の判断の前提も同じです。BRN の回避の区画なので、どちらでも害は無いと見ます（推測）。「Mesa・mainline と同じ形」を採るなら GENERAL です。文面を直してください。
2. **「p010l まで linear の features を 0」は広すぎる**:
   - libvulkan は先に `GPU_CAP_SHARE` を見て、次に linear の **SAMPLED** だけを見て `FORMAT_NOT_SUPPORTED` を返します（`wsi-image.c:51-58`）。
   - features を全部 0 にすると、gpu-selftest の段 5（LINEAR の image の作成と rowPitch）が仕様上作れません。§3.2 の R8 の「TRANSFER（p010l から）」とも合いません。
   - 直し方: 外すのは linear の SAMPLED だけにし、TRANSFER は出します。
3. **奇数の row length**:
   - readback は `bufferRowLength` 0（詰め）で copy します（`wsi-swapchain.c:2804-2807`）。幅が奇数だと、PBE の linestride（2 pixel 単位）で表せません。Mesa は `double_stride` で回避しています（`pvr_arch_job_transfer.c:4717-4722・5335`）。
   - p010 は display の全画面（偶数）なので問題になりませんが、p010f に「奇数は 1 行ずつに分けるか断る」と書き、p011 へ送ってください。
4. **free list が尽きた事の検出**:
   - grow 0・ready 0 では、FW が GROW を送らず、そのまま PR に入る可能性があります（推測）。FWCCB の UPDATE_STATS（`NUM_PARTIAL_RENDERS`・`NUM_OUT_OF_MEMORY`、`pvr_rogue_fwif.h:1405-1413`）も印に使ってください。
   - GROW_UPDATE の応答では、FW が ready の page を使い切った後なので `ready_pages = 0`、`new_pages` は current＋ready です（`pvr_free_list.c:374-388`）。「今の値」という書き方は曖昧です。
5. **`free_list_min_pages` はもう決まる**: roguexe で BRN66011 が無ければ 25 です（`pvr_free_list.c:21-45`）。BXE の quirks に 66011 はありません。p010d へ先送りする必要はありません。
6. **§4.1 の現状の記述**: PDS・USC の program は、すでに mainline の heap の base＋0x10000 にあります（`vk-queue.c:63-67`、`interface.h:105-106`）。sysinit も一致しています（`firmware-boot.c:761-762`）。「今の値と合うかを確かめる」は済んでいます。
7. **p010d の依存**: p010d は p009 の program の置き場所を変えるので、O-P009 を依存に足してください。
8. **O-SHADER-ABI の切り出し**: O-SHADER-ABI（`shader.h`）が p014（O-ENC に依る）に含まれているため、p010h が encoder の完成まで待ちます。`shader.h` の合意を p014 の最初の出力として切り出してください。
9. **ws.md の記録**:
   - D-P7 が 2 回書かれています（74・75 行）。
   - D-P2 の文面（「自動に流すかは config で選ぶ」、72 行）が design の D-P2 (a) と違います。
   - p010a の行に古い名前「render node」が残っています。
   - p010 の行の依存が「p009、p007」のままです。
10. **M9 の照合**: 取得した tarball の `gen_pack_header.py` を実行するのは、取ってきた code の実行です。専用の directory で `python3 -I` で走らせ、入力の path は引数で渡す、と phase.md に書いてください。
11. **p010b の SHA の一覧**: static assert の元になる `_client_check.h`・`_check.h`・`pvr_ccb.c`・`pvr_fw.c`・heap_config の SHA が足りません（§0 では「p010d で足す」）。p010d の必須項目に明記してください。

### D-P1〜D-P7 の推奨の妥当さ

- **D-P1 (a)**: M-3 (a)〜(d) と Minor 4 を直せば妥当です。`pds_pr_bgnd` に load の background を入れれば、color の画は尽きた時も正しくなり得ます（推測）。
- **D-P2 (a)**: 妥当です。ws.md の文面を揃えてください。
- **D-P3 (b)**: 妥当です。Mesa の形と一致します（p010b）。
- **D-P4 (a)**: 待つこと自体は妥当です。ただし付けた条件「時間切れの後に作り直す」は M-1 のとおり誤りで、「core に残す」に直す必要があります。
- **D-P5 (a)**: 妥当です。
- **D-P6 (a)**: 受け入れ得ます。読み取りだけの文字列で、空白も `-` に置き換えています（`stage.c:59-60`）。ただし design.md §2 の「新しい UAPI は作らない」を変えるので、ユーザーの判断が要るという扱いは正しいです。HAL の変更ではありません。
- **D-P7 (a)**: 妥当です（レビュー 1 の M5）。design.md §6 の改訂は Q1 です。

### HAL

HAL（`hal.h`）の変更は要りません。M-4 の static data area も driver の中の話です。

### 確かめられなかった点（推測・未確認）

- static data area（MCU fence・VDM sync・EOT）を FW か硬が実際に使うか（推測）。
- grow 0 の時に FW が GROW を送るか（推測）。
- `pds_pr_bgnd` が 0 の時の FW（推測）。
- cleanup ctl が無い時の CLEANUP の答え（推測）。
- FW が combined kick を必須にするか（未確認。mainline の形を採れば避けられます）。
- 実機・QEMU の試験は一切していません。

### 結論

1. **p010c は今すぐ始めてよいです。** Minor 2（外すのは SAMPLED だけ）を phase.md に入れてください。
2. **p010d** は、M-1・M-3 (d)・M-4 と Minor 1・5・6・7 を直してから始めてください。どれも文面の局所の直しで、**Q1 が差分を見るだけで足ります**。
3. **p010n・p010e・p010f** は、M-2・M-3・M-5・M-6 を直してから始めてください。M-6 は Phase の依存と O-ENC の中身の変更で、p013（P2）の phase.md にも波及します。それでも design-reviewer の再レビューまでは要らず、**Q1 が差分を見て、P2 と O-ENC の命令の一覧を合意すれば足ります**。
4. **p010g〜p010m** は上の直しに従って進めてよいです。p010h・p010i は O-SHADER-ABI（Minor 8）を待ちます。
5. **p013・p014（P2）** には、M-6 (c) の命令の一覧が固まるまで、O-ENC の完了の条件を「暫定」と明記させてください。

参照した file:
- `/home/awe/zedBSD-claude1/plan/ws269/design-p010.md`
- `/home/awe/zedBSD-claude1/plan/ws269/phase010b/phase.md`
- `/home/awe/zedBSD-claude1/plan/ws269/ws.md`
- `/home/awe/zedBSD-claude1/src/drivers/gpu/powervr/vk-queue.c`
- `/home/awe/zedBSD-claude1/src/drivers/gpu/powervr/render-queue.c`
- `/home/awe/zedBSD-claude1/src/drivers/gpu/powervr/render.c`
- `/home/awe/zedBSD-claude1/userland/desktop/libvulkan/wsi-image.c`
- `/home/awe/zedBSD-claude1/userland/desktop/libvulkan/wsi-swapchain.c`
- 今回取った資料: `/tmp/claude-1000/-home-awe-zedBSD-claude1/70c47280-c9e2-5e3f-b5d9-072cd0539dfc/scratchpad/r2/`（`pvr_vm.c`・`pvr_queue.c`・`pvr_cccb.c`・`pvr_drm.c`・`pvr_winsys_helper.c`）
