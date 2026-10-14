<!-- awesome-plan project=zedbsd record=ws037-p005 -->

# ws037-p005: VRAM・MMU・channel・fence・回復（段 P4〜P6）

Status: in-progress（2026-10-14 P1。設計を先に書いてから実装、build と host 試験まで。実機（方式 B）は T1 待ち）
Disposition: normal
Parent: [WS037](../ws.md)
Queue: Q1 の割当（2026-10-14「次は p005 … を 4a〜4e と同じ形で進める」）
依存: [p004](../phase004/phase.md)（P0〜P3・P7。commit `59cd3b4b9`〜`82b183ee7`、main に merge 済み）
実行者: phase-runner（high）

設計の正は [nvrtx-design.md](../nvrtx-design.md) の 3 節（P4〜P6）と 4 節（command の順）。値の出典は改名の後の作業の文書 `plan/ws037/temp/rename/renamed/channel-mmu-display.md` と、参照の header を temp で compile して出した大きさ（`plan/ws037/temp/offsets/`、commit しない）、MIT の open-gpu-doc（`classes/host/clc46f.h`、`manuals/turing/tu104/dev_vm.ref.txt`）。

## 0. 形（4a〜4e と同じ）

- host で試せる部品を先に作る（hardware に触れない file）。GPU に触る段は `nvrtx.gsp=1` かつ P3 まで成功した時だけ。
- どこで止めても既定（`CONFIG_DRIVER_PCI_NVRTX=n`）の build と動作は変わらない。`nvrtx.gsp=1` が無い y の kernel も p003 と同じ。
- 各段は `nvrtx.stop=<段>` で手前で止まり、worker は最後に必ず P7（unload・FWSEC-SB・booter_unload）を流して全部返す（p004 のまま）。P4〜P6 で作った物（vaspace・channel）は P7 の unload が消す。消す前に自分で FREE も試す（P6 の停止）。
- **最初の目標は GR（3D）を使わない「copy の channel で host の semaphore を release する」こと**（golden context と TU10x の scrubber は GR の channel に要る物で、copy の channel に要るかは未確認。要ると分かったら 5f で足す）。

## 1. 置き場所の方針

| 物 | 置き場所 | 理由 |
| --- | --- | --- |
| page table（PD3・PD2・PD1・PD0・PT の 5 段、各 4 KiB） | VRAM | nouveau と同じ（`SET_PAGE_DIRECTORY` の aperture VIDMEM）。sysmem も RM の flag にはあるが試された道ではない |
| instance block（4 KiB、先頭 0x200 が RAMFC）・USERD（0x200） | VRAM | nouveau と同じ（channel の確保の addressSpace 2） |
| GPFIFO（8 byte × 512）・push buffer（16 KiB）・semaphore（4 KiB） | system memory（coherent DMA） | nouveau の GART の物と同じ。PTE の aperture は system coherent・VOL |
| method buffer（大きさは RM に聞く） | system memory | nouveau と同じ（addressSpace 1） |

- **VRAM の CPU からの書き込みは PRAMIN の窓**（BAR0 の 0x700000〜、窓の base は 0x001700 に 64 KiB 単位）で行う。BAR1・BAR2 は使わない（BAR2 の page table と BAR1 の根は RM と分け合う物で、p005 の範囲では要らない）。窓は書く間だけ動かし、元の値に戻す。
- **VRAM の確保**: static info の FB region の表（offset 344、16 個、各 48 byte: base・limit・reserved・performance・compressed・ISO・protected）から、reserved でも protected でもなく compressed と ISO の両方を持つ region を使える範囲とし、**最後の使える region の上端から下へ 1 MiB 揃えの 1 塊**（p005 では 64 KiB で足りる）を取る。GOP の framebuffer（方式 A で VRAM の下の方）と離すため上端から取る。RM の内部の確保とは region の表で分かれている前提（nouveau と同じ）。

## 2. 段

| 段 | すること | 印（合格） | 失敗の時 |
| --- | --- | --- | --- |
| P4a | FB region の表を読み、VRAM の塊を決める | `P4 vram: region N base..limit, block at X` | 使える region が無い → P7 |
| P4b | VA の空間: VA 0x20000000 から 2 MiB を 1 本の PT（4 KiB page）で覆う 5 段の表を VRAM に PRAMIN で書く（PD3→PD2→PD1→PD0 の dual PDE の small 側→PT）。GPFIFO・push buffer・semaphore を map | `P4 page tables at X` | — |
| P4c | RM に外部所有の vaspace（class 0x90f1、index 0、flags bit 3）を device の下に作らせ、device に `SET_PAGE_DIRECTORY`（0x801813: 根の物理・entry 数 4・aperture VIDMEM・hVASpace）を送る | `P4 ok: vaspace` | status → P7 |
| P4d | TLB の invalidate（0xb830a0 = 根 >> 8、0xb830a4 = 0、0xb830b0 = 0x80000001、bit 31 が落ちるまで 2 s） | （P4 ok に含める） | timeout → P7 |
| P5a | RM の内部の subdevice に `FIFO_GET_DEVICE_INFO_TABLE`（0x20801112、3212 byte）を送り、GR0 と COPY の engine の runlist を読む。GR0 の runlist と違う runlist の最初の COPY を選ぶ。`CE_GET_FAULT_METHOD_BUFFER_SIZE`（0x20802a08）で method buffer の大きさ | `P5 copy engine N runlist R, method buffer B` | — |
| P5b | instance block を VRAM に作り、page table の根を書く（inst+0x200 = 根 \| VER2 \| 64KiB big、+0x208 = limit-1、subcontext 0 の PDB と mask）。USERD を 0 で埋める。channel（class 0xc46f、chid 8、368 byte の引数: GPFIFO の VA と entry 数・flags・hVASpace・engineType・instance/USERD/RAMFC/method buffer の記述）を device の下に確保し、`BIND`（0xa06f0104）と `GPFIFO_SCHEDULE`（0xa06f0103、bEnable 1）を channel に送る | `P5 ok: channel 8 on runlist R` | status → P6 の停止 → P7 |
| P5c | push buffer に host の method を書く: SEM_ADDR_LO/HI・PAYLOAD_LO（0x5c〜0x64）= semaphore の VA と値、SEM_EXECUTE（0x6c）= RELEASE \| WFI、MEM_OP_A〜D（0x28〜0x34）= SYS_MEMBAR、NON_STALL_INTERRUPT（0x20）。GPFIFO に entry、USERD+0x8c（GP_PUT）、USERD を読み戻し、doorbell 0xbb0090 = (runlist << 16) \| chid。semaphore を 2 s poll | `P5 ok: fence 0x1234 reached` と GP_GET が GP_PUT に追いつく | 届かない → 印に GP_GET と semaphore の値 → P6 |
| P6 | 回復と停止: 待ちの間に RC の event（0x1004）が来たら engine・chid・exceptType・fault の番地を印に出す。終わりに channel と vaspace を FREE（RPC 10）、`UNSET_PAGE_DIRECTORY`（0x801814）を試す（失敗は印だけで P7 に進む） | `P6 ok: channel and vaspace freed` | 印だけ。P7 の unload が残りを消す |

- copy の class（0xc5b5）の object と copy の method（fill）は 5e（任意、時間があれば）。合格は「semaphore の page の別の場所が copy の値で埋まる」。
- GR の golden context と scrubber は 5f（P5b の channel の確保が RM に断られた時、または 3D に進む時）。

## 3. 実装の単位（どこで切っても既定が変わらない）

| 単位 | 中身 | 実機 |
| --- | --- | --- |
| 5a | host だけの部品: VER2 の PDE・dual PDE・PTE の符号、5 段の表の index と組み立て（table の memory を抽象化）、FB region の解読と VRAM の塊の決定、control の call（24 byte の header）、vaspace・SET/UNSET_PAGE_DIRECTORY・device info・channel・BIND・SCHEDULE・FREE・copy の object の引数の組み立て、push buffer の method header・GPFIFO の entry の符号、instance block の PDB の欄 | 不要（host 試験） |
| 5b | P4（PRAMIN の窓の書き込み、表、vaspace、invalidate） | 方式 B |
| 5c | P5（device info、channel、BIND・SCHEDULE、semaphore の fence） | 方式 B |
| 5d | P6（RC の event の印、FREE、UNSET） | 方式 B |
| 5e | copy の object と fill（任意） | 方式 B |

## 4. run（方式 B、p004 の R0〜R4 に続く）

`plan/ws037/tests/config-amd64-nvrtx.mk` に足す。boot の語は全部 `nvrtx.gsp=1` と次の stop。

| run | 語 | 合格の印 |
| --- | --- | --- |
| R5 | `nvrtx.stop=P5` | R4 の印に加えて `P4 ok: vaspace`、最後に `P7 ok: region torn down` |
| R6 | `nvrtx.stop=P6` | R5 に加えて `P5 ok: channel 8 on runlist R`、`P5 ok: fence 0x1234 reached …`、`P5 ok: copy engine filled 64 words`（その前に `fence 0x1235`）。P6 は飛ばし、P7 の unload が消す |
| R7 | （stop 無し） | R6 に加えて `P6 ok: channel and vaspace freed`、`P7 ok: region torn down` |

判定は p004 と同じ（仮想の VGA の画面の PNG と serial の対話の `dmesg | grep nvrtx`、log では判定しない）。固まった時は gdbstub と QMP。

## 5. 危険と未確認

1. copy の channel の確保に GR の golden context が要るか（nouveau は channel の確保の前に必ず GR の oneinit を済ませる）。断られたら 5f。
2. PRAMIN の窓（0x001700）を GSP-RM が動く間に kernel が動かしてよいか（RM が同じ窓を使うなら壊す）。動かす間は短くし、元に戻す。
3. VRAM の塊が RM の確保と重ならないか（FB region の表だけが根拠）。
4. doorbell の token を `(runlist << 16) | chid` と自分で作る（nouveau と同じ。RM に聞く ctrl は使わない）。
5. instance block の PDB を kernel が書いた後に RM が書き直すか。
6. non-stall の割り込みは使わない（poll だけ）。割り込みの木の設定は後。

## 5'. 範囲の外

BAR1・BAR2 の map、VRAM の汎用の allocator、複数の channel・session、`drv_gpu_ops` への登録、display（p006）、3D（p007）。

## 結果（2026-10-14、P1）

| 単位 | commit | 中身 |
| --- | --- | --- |
| 設計 | `b49989dc6`・`476b1bd4a` | この phase.md、`config-amd64-nvrtx.mk` の R4（`stop=P4`）〜R7 |
| 5a | `6f709626a` | `mmu.c`（VER2 の 5 段の表・instance block の PDB・FB region からの VRAM の塊）、`objects.c`（control の call・vaspace・SET_PAGE_DIRECTORY・device info・channel の 368 byte・fence の push・GPFIFO の entry・FREE） |
| 5b〜5d | `d0db26710` | `channel.c`（P4: VRAM の塊・PRAMIN の書き込みと読み戻し・vaspace・根・TLB の invalidate。P5: device info から copy engine・method buffer・instance block と USERD・channel・BIND・SCHEDULE・fence。P6: 待ちの間の RC の event の印・channel と vaspace の FREE・UNSET_PAGE_DIRECTORY）。`rpc.c` に `nvrtx_rm_allocate`・`nvrtx_rm_control`・`nvrtx_rm_free` と RC の event の印。`gsp.c` が P3 の後に P4・P5 を呼び、終わりに P6 の停止 → P7 |
| 5e | （この commit） | copy の object（0xc5b5、subchannel 4）と 64 word の fill（remap の定数）、その後の fence 0x1235、words の確認。`nvrtx.stop=P5e` で飛ばせる |

- 検証: host 試験 `nvrtx-gsp-host-test.sh` 165 項目 PASS（表の各段の entry・dual PDE の small 側・PTE の system coherent・volatile、instance block の PDB と subcontext、FB region の選び方、control の header、device info からの選び方、channel の引数の各欄、fence と fill の method、GPFIFO の entry）。amd64 の y の kernel は rc 0・warning 0・check PASS。既定 n は `vmunix.mk` の `ifeq` の中だけの変更なので変わらない。
- 未実施: 実機（方式 B）での R5〜R7。危険は 5 節（特に 1: copy の channel に golden context が要るか、2: PRAMIN の窓）。

## 5f の設計（2026-10-14、P1。GR の golden context と TU10x の scrubber、未実装）

Q1（2026-10-14）「R6 で RM が channel を断った時に要る GR の golden context と TU10x の scrubber を … run の語（例 `nvrtx.gr=1`）で選べるように」。この節は実装の前の設計と、集めた事実（値は参照の header を temp で compile した物、`plan/ws037/temp/offsets/gr.c`・`fifo.c`、commit しない）。**実装は次の P1 の世代で行う**（context の大きさのため、2026-10-14 Q1 の許可で区切った）。

### 5f.1 流れ（nouveau の r535/r570 の gr の oneinit と同じ順）

`nvrtx.gr=1` の時だけ、P5 の channel の前に次を流す（無ければ今の copy の channel の道のまま）。

1. **context buffer の情報**: RM の内部の subdevice に control 0x20800a32（GET_CONTEXT_BUFFERS_INFO、引数は 8 engine × 26 entry × {size u32, alignment u32} = 1664 byte、0 で送り返りを読む）。engine 0 の entry だけを使う。entry の番号（engine ID）→ promote の buffer ID・global・init・ro の対応:

   | engine ID | buffer ID | global | init | ro | 備考 |
   | --- | --- | --- | --- | --- | --- |
   | 0x00 GRAPHICS | 0 MAIN | no | yes | no | 大きさ = 4 KiB 揃え + 64 page（subcontext の header） |
   | 0x10 PATCH | 2 | no | yes | no | |
   | 0x11 BUNDLE_CB | 3 | yes | no | no | |
   | 0x0d PAGEPOOL | 4 | yes | no | no | |
   | 0x13 ATTRIBUTE_CB | 5 | yes | no | no | VA の揃えは大きさの 2 の冪 |
   | 0x14 RTV_CB_GLOBAL | 6 | yes | no | no | |
   | 0x17 FECS_EVENT | 9 | yes | yes | no | |
   | 0x18 PRIV_ACCESS_MAP | 10 | yes | yes | yes | promote では bNonmapped（map しない） |
   | （同じ物の写し） | 11 UNRESTRICTED_PRIV_ACCESS_MAP | yes | yes | yes | 新しく取る時（golden）は ro で map して送る。global を共有する通常の channel では飛ばす |

   page の大きさ: 2 MiB 以上は 2 MiB、64 KiB 以上は 64 KiB、それ以外 4 KiB（揃え）。buffer は VRAM（init の物も init でない物も）。

2. **golden の address space**: device の下に vaspace（0x90f1）を **外部所有でなく**（flags 0）作り、vaspace に control 0x90f10106（COPY_SERVER_RESERVED_PDES、184 byte: hSubDevice 0・subDeviceId 4・pageSize 8 = 512 MiB・virtAddrLo 16 = 0x1_0000_0000・virtAddrHi 24 = +512 MiB − 1・numLevelsToCopy 32 = 3・levels[6] 40〜（各 24 byte: physAddress 0・size 8・aperture 16 = 1・pageShift 20））。levels は上から root（size 32、shift 47）・PD2（size 4096、shift 38）・PD1（size 4096、shift 29）。kernel は VA 4 GiB〜4.5 GiB をこの予約に空けておく（その範囲の表を自分の物で埋めない）。
3. **golden の channel**: VRAM に 0x12000 の塊（instance +0、USERD +0x1000、method buffer +0x2000、**method buffer も VRAM**）、chid 1（RM の予約の次）、engine GR0（1）、runq 0、**priv**（flags の bit 5 と internal flags の ADMIN = 1）、GPFIFO の VA 0・長さ 0x1000（投入はしない）。nouveau はこの channel に join（instance block の PDB を書くこと）をしない。
4. **promote**: その vaspace の device の subdevice に control 0x2080012b（PROMOTE_CTX、560 byte: engineType 0 = 1・hClient 4・ChID 8・hChanClient 12 = 自分の client・hObject 16 = channel・hVirtMemory 20・virtAddress 24・size 32・entryCount 40・promoteEntry[16] 48〜 各 32 byte: gpuPhysAddr 0・gpuVirtAddr 8・size 16・physAttr 24 = 4・bufferId 28（u16）・bInitialize 30・bNonmapped 31）。golden では全部の buffer を新しく取り、init の物だけ物理と大きさを書き、map する物は VA を書く（PTE の priv の bit 5 を立て、PRIV_ACCESS_MAP は ro）。
5. **3D の object**（class 0xc597、引数なし）を channel に作る → RM が golden context を作る。その後 3D の object と channel を FREE（RM は context を保持する）。global の buffer は保持し、以後の GR の channel で共有する。
6. **scrubber（TU102・TU104・TU106 だけ）**: 別の vaspace（外部所有でない、handle は nouveau の 0xdada0042 の系統。zedBSD は自分の handle）・別の chid・engine GR0・非 priv の channel（VRAM に 0x2000 + method buffer の大きさ）→ promote（golden でない形: global は共有、non-global は新しく）→ 3D の object → 内部の subdevice に control 0x20800a46（INIT_BUG4208224_WAR、1 byte: bTeardown 0）。停止の時は bTeardown 1 を送ってから FREE。

### 5f.2 未確認（実装の前に決める）

1. **外部所有でない vaspace で、GPU はどの page table を使うか**。kernel が golden の VMM に map した context buffer の VA を、RM の側の root から引けるのか（COPY_SERVER_RESERVED_PDES の向き: 「server の PDE を client に写す」か逆か）。ogkm の sparse の範囲に RM の実装が無いので、nouveau の `r535_mmu_vaspace_new` と `nvkm_vmm` の join の関係をもう一度読む（`vmm->rm.external` が false の時に `nvkm_vmm_join` が呼ばれるか）。
2. 今の 5 段の表の builder は 2 MiB の 1 範囲・4 KiB の page だけ。golden には数 MiB の buffer が複数あるので、範囲を増やす（PD1 の entry ごとに PD0、2 MiB ごとに PT）か、64 KiB の page（LPT、dual PDE の下半分）を使う形に広げる。VRAM の塊も 64 KiB では足りないので、VRAM の arena（例 64 MiB、上端から）を取る。
3. golden の channel に join が要らないなら、instance block は RM が書く。要るなら kernel が書く。

### 5f.3 印と run（予定）

`P5g begin` → `P5g ctxbufs: N buffers, M KiB` → `P5g ok: golden context`（3D の確保の status 0）→ TU10x で `P5g ok: scrubber`。run は R6 と R7 に `nvrtx.gr=1` を足した R6g・R7g。合格は今の印に加えて上の 3 行。
