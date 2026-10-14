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
