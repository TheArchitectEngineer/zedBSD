<!-- awesome-plan project=zedbsd record=ws037-p006 -->

# ws037-p006: display（段 P8）

Status: in-progress（2026-10-15 P1。P8a は実機（方式 B、T1）PASS。P8b は直し（`476ca3ba5`）の再試験 R8b（T1-645）待ち。P8c（resident の GPU の device と display の ops）は `2c4d1a39f` で build まで、R8c（T1-647）待ち。P8c の続き（DISPLAY_EVENTS・DISPLAY_CONTROL（refresh、power は EOPNOTSUPP）・cursor・SHARE・scanout の表）を `2791fc6e3`・`e6699aa70` で build と host 試験まで、R8d は R8c の後（下の「P8c の続き」））
Disposition: normal
Parent: [WS037](../ws.md)
Queue: Q1 の割当（2026-10-14 夜「(3) p006（display: GOP の readout と GSP の RM の display で mode set・scanout・flip）」、ユーザーの判断「display … ここまでベータ2に入れます」）
依存: p005（P4〜P6、copy の channel・VRAM の書き込みの道）。既定 off（`CONFIG_DRIVER_PCI_NVRTX=n`）のまま。
設計の正: [nvrtx-design.md](../nvrtx-design.md) の P8 と 2 節の 0（**GOP の出力先以外に scanout を始めない**、Guardrail）。値の出典は作業の文書 `temp/channel-mmu-display.md` の 6 節と nouveau（MIT）の `r535/disp.c`・`r570/disp.c` と nvrm の header（570.144 の番号。r570 で変わった system の control（GET_SUPPORTED 0x730107・GET_ACTIVE 0x73010c・CONNECT_STATE 0x730108）は r570 の値、他は r535 の値。大きさと offset は header を scratchpad で compile して確かめた）。

## 段

| 段 | すること | 印（合格） |
| --- | --- | --- |
| P8a | display の instance memory（VRAM 64 KiB、copy の block の 1 MiB 下、0 で埋める）を `INTERNAL_DISPLAY_WRITE_INST_MEM`（0x20800a49、24 byte）で渡す → `NV04_DISPLAY_COMMON`（0x0073、0x00730000）→ `INTERNAL_DISPLAY_GET_STATIC_INFO`（0x20800a01、windowPresentMask）→ `INTERNAL_INIT_BRIGHTC_STATE_LOAD`（0x20800ac6、status 0x56 = ACPI の _DSM 無し、nouveau と同じ）→ `DP_SET_MANUAL_DISPLAYPORT`（0x731365）→ `GET_NUM_HEADS`（0x730102）・`GET_ALL_HEAD_MASK`（0x730287）・`GET_SUPPORTED`（0x730107）→ root `TU102_DISP`（0xc570、0xc5700000）→ head ごとに `GET_ACTIVE`（0x73010c）→ 点いた head は `OR_GET_INFO`（0x73028b、SOR の番号と protocol）と ARMED の timing（0x68a000 + head×0x400 の +0x64・+0x6c・+0x70・+0x0c） | `P8 heads N (mask), windows …, outputs …`、点いた head ごとに `P8 head H: display … SOR n DP-A/TMDS-A …` と `P8 head H: WxH of HTxVT, clock …`、`P8 ok: display, heads …, windows …, lit …` |
| P8b（実装 2026-10-15、下の「P8b の実装」） | GOP の head を引き継ぐ: core channel（0xc57d、push buffer 4 KiB を coherent sysmem、`INTERNAL_DISPLAY_CHANNEL_PUSHBUFFER`）、RAMHT と ctxdma（notifier と VRAM 全体）を instance memory に kernel が書く、core の init の method（notifier・window の usage bounds）、window channel（0xc57e、GOP の head の window 0）、自前の surface（VRAM、GOP と同じ大きさ、形式 A8R8G8B8 の pitch）を PRAMIN で塗る、mode set は **GOP と同じ timing と SOR**（`SOR_SET_CONTROL`・`HEAD_SET_*` を ARMED の値から）、`UPDATE` と notifier の完了、flip（`SET_OFFSET` と `UPDATE`） | 画面に自前の surface（試験の模様）、notifier の完了、flip の後に別の模様 |
| P8c | text console の付け直し（設計の判断の項目 12: pcat の text の口、WS037 の範囲の外の依存）と `drv_gpu_ops` の display の ops | （後） |

- P8a は画面を変えない（RM の object と読みだけ）。P8b から画面を書き換える。
- 停止: `nvrtx_display_stop()` が root → common を FREE（`gsp_finish` で P6 の graphics・channel の片付けの前）。`nvrtx.stop=P8` で P8 の手前で止まる。

## 確認

| 確認 | 結果 |
| --- | --- |
| nvrtx=y の kernel（`config-amd64-nvrtx.mk`、`BUILD=build/ws037-y`） | rc 0・warning 0・`kernel include check: PASS`・`amd64 vmunix check: PASS` |
| host 試験 | P8a は RM の control と register の読みだけで、host で試せる builder は無い（追加なし） |
| 実機（方式 B） | **未実施** |

## T2 への手順（R8a）

image は `config-amd64-nvrtx.mk` の `NVRTX_RUN=R7`（stop 無し。P5 の後に P8a が走る）。方式 B では guest の OVMF が RTX の option ROM の GOP で RTX の出力を点けていれば、その head が lit に出る（点いていなければ lit 0 で、それも結果として記録）。

合格: R7 の印（`P5 ok …`・`P6 ok`・`P7 ok`）に加えて `P8 begin` → `P8 heads …` → `P8 ok: display, heads …` → `P8b is not in this kernel; stopping after P8a`（`b893faeb6` 以降は `P8b not asked (nvrtx.disp=1); stopping after P8a` と `P8 done; stopping (this revision ends at P8b)`）→ `P6 ok: channel and vaspace freed`（`P6: N display frees failed` が無い）→ `P7 ok: region torn down` → login prompt。点いた head の行（SOR・protocol・大きさ・clock）は値を記録（RTX の出力の monitor の解像度と照らす）。FAIL は `rm: …` の行を PNG に。`P7 fail` なら host を reboot。

## P8b の作業の手順（次の世代へ。2026-10-15 P1 の区切り）

読む物（nouveau、MIT、`plan/ws037/temp/linux/drivers/gpu/drm/nouveau/`）と作る物の順:

1. **push buffer の登録と channel の確保**: `nvkm/subdev/gsp/rm/r570/disp.c` の `r570_disp_chan_set_pushbuf`（`NV2080_CTRL_CMD_INTERNAL_DISPLAY_CHANNEL_PUSHBUFFER` を内部の subdevice へ: addressSpace SYSMEM・cacheSnoop 1・pbTargetAperture PHYS_PCI_COHERENT・physicalAddr・limit・hclass・channelInstance・valid 1・subDeviceId 1）と `r570_dmac_alloc`（handle `(class << 16) | inst`、引数 channelInstance・offset（PUT の初期値）・subDeviceId 1、親は display の root）。r535 の `r535_dmac_*`（`r535/disp.c`）で user 領域の map と `r535_dmac_bind`（RAMHT への登録: context = `chid.user << 25 | (handle & 0x3fff)`）を読む。struct の大きさと offset は p535/p570 の probe（scratchpad の方式: nouveau の header を `-include pre.h` で compile）で出す。
2. **RAMHT と ctxdma**: `nvkm/engine/dma/usergv100.c` の `gv100_dmaobj_bind`（24 byte: flags0・start>>8・limit>>8）、`nvkm/engine/disp/` の RAMHT の hash（`nvkm_ramht_insert`）。instance memory の中の RAMHT（0x2000）の位置は `r535_disp_oneinit` の後半。kernel が PRAMIN で instance memory に書く（`nvrtx_vram_write`）。ctxdma は 2 つ: notifier（VRAM 4 KiB）と VRAM 全体（surface 用）。
3. **core の init**: `drivers/gpu/drm/nouveau/dispnv50/corec57d.c` の `corec57d_init`（`SET_CONTEXT_DMA_NOTIFIER`、window ごとの usage bounds）と `core507d.c`・`corec37d.c` の update と notifier の待ち（`corec37d_ntfy_wait_done`、notifier の offset 0 の STATUS が FINISHED）。method の offset は open-gpu-doc の `classes/display/clc57d.h`（MIT）。
4. **mode set（GOP と同じ timing・SOR）**: `dispnv50/headc57d.c`（`headc57d_mode`・`headc57d_view`・`headc57d_or` など）、`dispnv50/corec37d.c`（`SOR_SET_CONTROL`）、`dispnv50/disp.c` の commit の順。timing は P8a の ARMED の読み（`display_head()`、+0x64/+0x68/+0x6c/+0x70/+0x0c）を全部 struct に残すよう広げる。SOR と protocol は P8a の `gop_sor`・`gop_protocol`。GSP の道では supervisor は RM が処理（`r535_disp_new` に `.super` 無し）。
5. **window と surface**: `dispnv50/wndwc57e.c`（`wndwc57e_image_set`: SET_PRESENT_CONTROL・SET_SIZE・SET_STORAGE（pitch）・SET_PARAMS（A8R8G8B8）・SET_PLANAR_STORAGE（pitch>>6）・SET_CONTEXT_DMA_ISO（VRAM の ctxdma）・SET_OFFSET（>>8）・POINT_IN・SIZE_IN・SIZE_OUT）、`WINDOW_SET_CONTROL`（core、window の owner の head）。surface は VRAM（GR の arena と display の instance の間を避け、P4 の上端の block より下に GOP の大きさ（例 1920×1080×4 = 8 MiB）を取る）。塗るのは PRAMIN（`nvrtx_vram_write`、遅いが 1 回の試験の模様なら足りる）。
6. **kick と完了**: user 領域（core 0x680000、window 0x690000 + w×0x1000）の PUT（+0）に push buffer の書いた位置、GET（+4）で空きを見る。notifier の完了を 2 s まで poll。flip は window に新しい SET_OFFSET と UPDATE。
7. **印と run**: `P8b core channel` → `P8b ok: mode set (WxH on SOR n)` → `P8b ok: flip`。run は R7 に `nvrtx.disp=1`（新しい token、既定は P8a まで）を足した R8b。画面の確かめは RTX の出力（方式 B はユーザーの写真、方式 A の素の起動）。**GOP の出力先以外には出さない**（P8a で lit の head が無ければ P8b は何もしない）。

## P8a の実機の結果と試験の道の判断（2026-10-15）

- T1（Q1 の伝え、`plan/agents/T1/results-20261015-nvrtx-r6r7.md`）: R6・R7・R7g・R7(P8a) PASS。P8a は `P8 heads 4 (f), windows ff, outputs ff00` → `lit 0`。方式 B の QEMU は `rombar=0` で OVMF が RTX の option ROM を走らせず、RTX の GOP は何も点けていなかった。
- **判断（P1、2026-10-15）: (b) 方式 B を `rombar=1` にして引き継ぎを試す。(a)（引き継ぎをやめ最初から mode set）は取らない。** 理由: (1) Guardrail「GOP の出力先以外に scanout を始めない」（2026-10-04 ユーザー）。RTX の GOP が何も点けていなければ、(a) でも出す先が無く P8b は何もしてはならない。(2) ベータ2 の範囲は design の「mode は最初は GOP の mode だけ」（DISPLAY の ops の `mode`）。引き継ぎが本線で、方式 A（素の起動、RTX が primary の GOP）と同じ形を B で作れるのが rombar=1。(3) (a) は nouveau の初回 modeset の全部（head を消す・SOR の割り当て `DFP_ASSIGN_SOR`・DPCD の読みと DP の link training `DP_CTRL`・HDMI の設定）で量が大きい。引き継ぎで RM が SOR・link の状態を受け入れない時の次の段に残す。
- T1-640（rombar=1、`plan/agents/T1/results-20261015-nvrtx-rombar1.md`）: OVMF は RTX の option ROM で止まらず、段の印は emulated の VGA に出た。`P8 head 0: display 200 SOR 1 TMDS-A`、`P8 head 0: 1280x1024 of 1688x1066, clock 108000000`、`lit 1`、P6・P7 ok。monitor は TMDS（HDMI か DVI）で 1280x1024@60。**P8b はこの形（host の `~/nvrtx/start-rombar1.sh`）で試せる。**

## P8b の実装（2026-10-15、P1、`b893faeb6`）

nvrtx.disp=1 の時だけ、P8a の後に走る（既定は P8a で止まり `P8b not asked (nvrtx.disp=1); stopping after P8a`）。`nvrtx.stop=P8b` で手前で止まる。

**方式: modeset なしの引き継ぎ。** head と SOR の method には ARMED の値（core の ARMED の窓 0x688000 + method、nouveau の `gv100_head_state` と同じ読み）を写して送り直す。nouveau は GOP の出力を inherit（`r535_outp_inherit`: GET_ACTIVE と OR_GET_INFO で SOR と protocol を知る）した後、初回の commit で head を消して modeset するが、ここでは同じ値を送るので modeset（SOR の付け替え・link の再 training）が要らない見込み。送る method の集合は nouveau の head の modeset が送る物（view・mode・or・procamp・dither・display id、`headc57d.c`・`headc37d.c`）と同じ。

| 順 | すること | 出典（nouveau、MIT） |
| --- | --- | --- |
| 0 | ARMED を読む: head の 15 個の method と `SOR_SET_CONTROL(sor)`。raster が 0、または SOR の owner に head の bit が無ければ何も送らず fail（GOP の画面を壊さない） | `gv100.c` の head・SOR の state の読み |
| 1 | notifier の 4 KiB（instance memory の直後の VRAM）と surface 2 枚（GR と P9c の compute の arena の reserve の下、`NVRTX_COMPUTE_ARENA_RESERVE`、viewport の大きさ、pitch は 256 byte の整列、A8R8G8B8）を取り、8 本の色の帯（A は上から、B は逆順）を PRAMIN で塗る | — |
| 2 | core channel: push buffer（coherent sysmem 4 KiB）を `INTERNAL_DISPLAY_CHANNEL_PUSHBUFFER`（0x20800a58、r570 の 56 byte）で登録 → root の下に 0xc57d（handle 0xc57d0000、40 byte の引数）→ ctxdma 2 つ（sync 0xf0000000 = notifier の 4 KiB、VRAM 0xf0000001 = VRAM 全体）を instance memory に書き RAMHT（instance の先頭 0x2000、1024 entry）に chid 0 で登録 | `r570_disp_chan_set_pushbuf`・`r570_dmac_alloc`・`r535_dmac_bind`・`gv100_dmaobj_bind`・`nvkm_ramht_insert`・`nv50_dmac_create` |
| 3 | core の init（notifier の ctxdma、window ごとの format・usage の bounds）、SOR の control と head の method（ARMED の値）、window の owner（window i → head i>>1）、core の update（notifier を待つ、window の interlock 無し） | `corec57d_init`・`sorc37d_ctrl`・`headc57d_*`・`corec37d_wndw_owner`・`corec37d_update`・commit_tail の assign_windows |
| 4 | window channel（0xc57e、instance = 2×head、chid 1+w、同じ 2 つの ctxdma）→ image（surface A）・ILUT 無し・CSC の単位行列・不透明の blend、window の update（core と interlock）→ core の cursor と OLUT の ctxdma を 0、core の update（window と interlock、notifier を待つ） | `wndwc57e_image_set`・`wndwc57e_ilut_clr`・`wndwc57e_csc_clr`・`wndwc37e_blend_set`・`wndwc37e_update`・`headc37d_curs_clr`・`headc57d_olut_clr`・commit_tail |
| 5 | 10 秒表示 → flip（window の notifier、`SET_OFFSET` を surface B、window だけの update）→ notifier の BEGUN 以上を待つ → 10 秒表示 | `wndwc37e_ntfy_set`・`base507c_ntfy_wait_begun` |
| 6 | 停止: `nvrtx_display_stop` が window → core の channel を FREE してから root・common。push buffer は GSP の停止の後（`nvrtx_gsp_release`）に返す | `r535_chan_fini` |

- 置き場所: `src/drivers/gpu/nvrtx/scanout.c`（hardware）、`disp-records.c`・`nvrtx-disp.h`（push の符号化・RAMHT の hash と entry・ctxdma・RM の引数・method の列。host で試す）。`channel.c` に `nvrtx_vram_fill`・`nvrtx_vram_read32`、`stage.c` に `nvrtx.disp=1`。GR の arena の reserve を `NVRTX_GR_ARENA_RESERVE`、P9c の compute の arena の reserve を `NVRTX_COMPUTE_ARENA_RESERVE`（nvrtx-gpu.h、main の merge の時に compute.c から移した）にし、surface は両方の下に取る。
- 値の出典: method の offset と field は open-gpu-doc の `classes/display/clc57d.h`・`clc57e.h`（MIT）、RM の struct の大きさと offset は 570.144 の nvrm（nouveau の `r570/nvrm/disp.h`）を scratchpad で compile して確かめた（PUSHBUFFER 56 byte: space 0・address 8・limit 16・snoop 24・class 28・instance 32・valid 36・aperture 40・size 44・subdevice 48。channel 40 byte: instance 0・offset 12・subdevice 32）。
- 未確定の危険: (1) ARMED と同じ値を送っても hardware・RM が supervisor（modeset の処理）を起こすか。起きた時に RM が inherit していない SOR を受け入れるか。(2) RAMHT の context の client の欄（nouveau は display 用の client の handle の下位 14 bit、ここは主の client で 0）。(3) PUT の値は byte offset（nouveau の `NV507C_PUT_PTR` の 11:2 の使い方に合わせた。C57D の header は 9:0）。FAIL の時の印 `P8b fail: core owners notifier … get … put …` で切り分ける。

## 確認（P8b）

| 確認 | 結果 |
| --- | --- |
| nvrtx=y の kernel（`make -j16 ZEDBSD_CONFIG=plan/ws037/tests/config-amd64-nvrtx.mk BUILD=build/ws037-y vmunix`） | rc 0・warning 0・`kernel include check: PASS`・`amd64 vmunix check: PASS`、`git diff --check` 0 |
| host 試験 `sh plan/ws037/tests/nvrtx-gsp-host-test.sh` | PASS 194 項目（P8b の display の records 12 項目を足した。firmware の cache が無いので実 file の項目は飛ばし） |
| 既定 n | 新しい file は `CONFIG_DRIVER_PCI_NVRTX=y` の時だけ build（vmunix.mk）。既定の build と動作は不変 |
| 実機（方式 B、rombar=1） | **未実施**（R8b を T へ） |

## T への手順（R8b、rombar=1）

image は `config-amd64-nvrtx.mk` の `NVRTX_RUN=R8b`（`nvrtx.gsp=1 nvrtx.disp=1`）。QEMU は T1-640 と同じ rombar=1（host の `~/nvrtx/start-rombar1.sh`）。run の前に host の reboot と `prepare.sh --check`。P8b は表示に合計 20 秒止まる（A 10 秒・B 10 秒）。

合格（screendump の段の印）: R7(P8a) の印（`P8 ok: display, heads f, windows ff, lit 1`）に続いて `P8b begin` → `P8b head 0 armed: view … raster … clock …, SOR 1 …` → `P8b surfaces 1280x1024 pitch 5120 at … and …` → `P8b channel c57d.0 made` → `P8b core owners notified after N us` → `P8b channel c57e.0 made` → `P8b core show notified after N us` → `P8b ok: mode set (1280x1024 on SOR 1), surface A shown` → `P8b window flip notified after N us` → `P8b ok: flip, surface B shown` → `P8b ok: head 0 shows the driver's surfaces` → `P8 done; stopping (this revision ends at P8b)` → `P6 ok: channel and vaspace freed`（`P8b: N channel frees failed`・`P6: N display frees failed` が無い）→ `P7 ok: region torn down` → login prompt。
**monitor（ユーザーの目視）**: `surface A shown` の後の 10 秒は上から白・黄・水色・緑・紫・赤・青・黒の 8 本の横の帯、`flip` の後の 10 秒は逆順（上が黒）。それぞれの時刻を記録（monitor が消える・乱れる・GOP の画面のままも結果）。
FAIL の時: `P8b fail: …` の行（`rm: control 20800a58 status …`・`rm: class c57d … status …`・`notifier … get … put …`）を PNG に。`P7 fail` なら host を reboot。

## R8b の結果と直し（2026-10-15、P1）

T1-642（`plan/agents/T1/results-20261015-nvrtx-r7c.md` の末尾、rombar=1、main 20abf069a）: **FAIL**。`P8b head 0 armed: view 03200500 raster 042a0698 clock 066ff300, SOR 1 …` → `P8b surfaces 1280x800 …` → `P8b channel c57d.0 made` → **`P8b fail: core owners notifier 00000000 after 2 s, get 18c put 18c`** → `P6 ok` → **`P7 unload call: 42`・`P7 fail: the GSP did not stop (42)`**。ユーザーの目視「何も映らない、信号が出ていない」。`rm:` の行は無し。

### 原因の見立て（nouveau（MIT）の r535・r570 と dispnv50 と 1 つずつ照合）

| 点 | 照合の結果 |
| --- | --- |
| RAMHT の entry | nouveau `r535_dmac_bind`: context = chid << 25 \| (client の handle & 0x3fff) \| (object の offset << 9)。open-gpu-doc（MIT）の `dev_disp.h` の HASH_TBL（CLIENT_ID 13:0、INSTANCE 24:14、CHN 31:25）と合う。表の大きさ 0x2000（tu102 の `ramht_size`）、hash（10 bit で畳み、chid を 6 bit 上げる）、object は 32 byte の刻みも同じ。**違い無し** |
| ctxdma | `gv100_dmaobj_bind` の 24 byte、flags0 = 0x45（VRAM・RW・small page）。同じ |
| push buffer の登録・channel の確保 | `r570_disp_chan_set_pushbuf`（56 byte）・`r570_dmac_alloc`（instance・offset・subDeviceId）と同じ。GET が PUT（0x18c）に追いついた＝core は push を全部読んだ |
| PUT の単位 | nouveau は `NV507C PUT PTR 11:2` で byte offset を書く。同じ（GET も 0x18c で一致） |
| notifier | `corec37d_update`: SET_NOTIFIER_CONTROL（MODE WRITE・OFFSET >> 4・NOTIFY ENABLE）→ INTERLOCK → UPDATE → NOTIFY DISABLE。同じ |
| **head の状態** | **nouveau は GSP の下で head の ARMED を読まない**（`r535_head_state` は空）。DRM の初回の commit は modeset になり、**disable（head の LUT・cursor・display id を消し、SOR_SET_CONTROL を 0 にして update）→ `nvif_outp_acquire_sor`＝RM の `NV0073_CTRL_CMD_DFP_ASSIGN_SOR`（0x731152）→ enable（display id、SOR_SET_CONTROL = protocol << 8 \| head の bit、head の view・mode・dither・procamp・or、window の owner、update）** の順。初版は ARMED の値をそのまま送り直し、**DFP_ASSIGN_SOR を呼ばず**、HEAD_SET_CONTROL・USAGE_BOUNDS なども GOP の値のままだった |

- 見立て: update が head・SOR の状態を変えると supervisor（modeset の処理）が起き、GSP の下では RM が処理する。RM は display に SOR が割り当てられていない（DFP_ASSIGN_SOR が無い）ので処理を終えられず、update が終わらない（notifier 無し、head は消えたまま＝信号なし）。RM がその処理の途中のまま unload を受けて返事をしない（42）。push を全部読んだ（GET = PUT）こと、`rm:` の行が無い（RPC は全部成功）こととも合う。**未確認**（実機の再試験で確かめる）。
- view の読み: ARMED の VIEWPORT_SIZE_IN = 0x03200500 は 1280x800 で、読み違いではない（GOP の surface が 1280x800 で、head の scaler で 1280x1024 に拡大していた）。nouveau は view を mode の大きさ（in = out）にする。直しでは見える大きさを blank の start − end（P8a の readout と同じ式、1280x1024）から出し、view の in と out、自前の surface をその大きさにした。
- unload の 42: 上の見立てどおりなら直しで消える。P8b の片付けの順（window → core の channel の FREE → root → common → P6 → P7）は nouveau の `r535_chan_fini`・`r535_disp_fini` と同じなので変えていない。

### 直し（commit `476ca3ba5`）

`scanout_take_head()` を nouveau の初回の commit の段に分けた。各段の後に印と 5 秒の待ち（`P8b step N: … (5 s)`）を入れ、monitor の信号がどこで消える・戻るかを目視で分けられるようにした。

1. `P8b step 1: core channel made` — channel の確保だけ（head に触れない）。
2. core の start（notifier の ctxdma、8 window の bounds）を**それだけで** update（`core start notified`）→ `P8b step 2: core start released`。head を変えない update なので、ここで notifier が来なければ RAMHT・ctxdma・notifier の道の誤りと分かり、head は GOP のまま（安全）。nouveau は start を初回の commit の update で一緒に出す（分けたのは切り分けのためだけ）。
3. disable: head の OLUT・cursor を消し、display id を 0、`SOR_SET_CONTROL(gop_sor) = 0`、update（`core disable notified`）→ `P8b step 3: head disabled`（ここで信号が消えるのは nouveau と同じ）。
4. `DFP_ASSIGN_SOR`（display common 0x00730000、80 byte、displayId = P8a の display id、除外 0）→ `P8b SOR n assigned to display 200 (the firmware's was 1)`。
5. enable: display id、`SOR_SET_CONTROL(n) = protocol << 8 | (1 << head)`、head の view（in = out = 見える大きさ）・raster 4 語・VERT_BLANK2・CONTROL 0（progressive）・pixel clock と max（hertz）・USAGE_BOUNDS 0x1114（nouveau の値）・dither（ARMED）・procamp 0・OUTPUT_RESOURCE（ARMED、sync の極性と depth）、window の owner、update（`core owners notified`）→ `P8b step 4: head enabled`。
6. 以後は前と同じ（window channel、surface A、flip、surface B）。
- notifier が来ない時は `P8b core exception %08x data %08x code %08x`（0x611020/24/28、method は bit 11:0 ×4）と `P8b other %08x supervisor %08x core state %08x`（0x611854・0x611c30・0x610630）を出す。
- DP の出力（protocol 8・9）は link training が要るので何もしない（`P8b: protocol N is not TMDS; the picture is left alone`）。
- 確認: nvrtx=y の kernel（`BUILD=build/ws037-y`）rc 0・warning 0・include check / vmunix check PASS。host 試験 `nvrtx-gsp-host-test.sh` PASS 222 項目（mode の導出、head の mode の method 25 語、display id・SOR control、DFP_ASSIGN_SOR の引数と返りの読み、push の jump）。実機は未実施。

### T への手順（R8b の再試験、rombar=1。2026-10-15 夜に既定 on の語へ更新）

image は main の `7ca217eab` 以降（P1 の直し `476ca3ba5` と既定 on を含む）で `plan/tools/guest/test-image.sh --no-harness plan/ws037/tests/config-amd64-nvrtx.mk build/ws037-nvrtx/r8b NVRTX_RUN=R8b`。boot の語は **`nvrtx.gsp=1 nvrtx.disp.test=1 nvrtx.resident=0`**（既定 on の後は `nvrtx.disp=1` は無い語。`disp.test=1` が帯の surface・flip・step ごとの 5 秒の待ちを出し、`resident=0` が最後に GSP を止める）。QEMU は `~/nvrtx/start-rombar1.sh`。run の前に host の reboot と `prepare.sh --check`。**ユーザーが monitor の前で、各 `P8b step` の印の時に信号があるかを見る**（step ごとに 5 秒止まる。screendump は 5 秒おきが望ましい）。全体で P8b は約 40 秒（step 4 つ × 5 秒、帯 10 秒 × 2）。

合格（screendump）: `P8 ok: display, heads f, windows ff, lit 1` → `P8b begin` → `P8b head 0 armed: view … raster … clock …, SOR 1 …` → `P8b head 0 mode: 1280x1024, view out …` → `P8b surfaces 1280x1024 pitch 5120 at … and …` → `P8b channel c57d.0 made` → `P8b step 1: core channel made (5 s)` → `P8b core start notified after N us` → `P8b step 2: core start released (5 s)` → `P8b core disable notified after N us` → `P8b step 3: head disabled (5 s)` → `P8b SOR n assigned to display 200 (the firmware's was 1)` → `P8b core owners notified after N us` → `P8b step 4: head enabled (5 s)` → `P8b channel c57e.0 made` → `P8b core show notified after N us` → `P8b ok: mode set (1280x1024 on SOR n), surface A shown` → `P8b window flip notified after N us` → `P8b ok: flip, surface B shown` → `P8b ok: head 0 shows the driver's surfaces` → **`P8 done; this revision ends at P8c`**（以前の `P8 done; stopping (this revision ends at P8b)` から変わった）→ `P6 ok: channel and vaspace freed`（`P8b: N channel frees failed`・`P6: N display frees failed` が無い）→ `P7 ok: region torn down` → login prompt。
- 方式 B では GOP の画面が emulated の VGA にあるので `P0 console …` の行は出ない（console の保持は方法 A の物）。`P0 skipped: …` が出たら語の `nvrtx.gsp=1` が欠けている。
- monitor（目視）: step 1・2 は GOP の画面のまま（または既に黒）、step 3 で信号が消えてよい、step 4 で信号が戻る（画面は GOP の残りか黒）、`surface A shown` の後 10 秒は 8 本の帯（上が白）、flip の後 10 秒は逆順。**どの step で信号が消え・戻ったか**を記録。
- FAIL の時: `P8b fail: …` と、その直後の `P8b core exception …`・`P8b other …` の 2 行、`rm: control 00731152 status …` があればそれを PNG に（次の「FAIL の読み方」で読む）。`P7 fail` なら host を reboot。

### FAIL の読み方（R8b、P2 が 2026-10-15 に用意。register の意味は open-gpu-doc（MIT）`manuals/turing/tu102/dev_display_withoffset.ref.txt`）

notifier が 2 秒で来ない時、`scanout_diagnose()` が 2 行を出す:
`P8b core exception E data D code C` と `P8b other O supervisor S core state T`。

| 値 | register | 読み方 |
| --- | --- | --- |
| E | `NV_PDISP_FE_EXCEPT(0)` 0x611020（core channel） | 11:0 = 例外の method の offset（push の method の番号、clc57d.h で引く）、14:12 = 理由: 0 なし・1 PUSHBUFFER_ERR・2 TRAP・3 RESERVED_METHOD・4 INVALID_ARG・5 INVALID_STATE・7 UNRESOLVABLE_HANDLE。31 = RESTART pending |
| D | `NV_PDISP_FE_EXCEPTARG(0)` 0x611024 | その method の引数（送った値） |
| C | `NV_PDISP_FE_EXCEPTERR(0)` 0x611028 | 23:0 の誤りの code（公開の意味の表は無い。値を記録） |
| O | `NV_PDISP_FE_EVT_STAT_EXC_OTHER` 0x611854 | bit 0 = core の例外 pending、bit 16〜23 = cursor 0〜7 の例外 |
| S | `NV_PDISP_FE_RM_INTR_STAT_CTRL_DISP` 0x611c30 | bit 0〜2 = supervisor 1〜3 の pending（modeset の処理。GSP の下では RM が処理する）、3 VBIOS_RELEASE、7 ERROR（詳細は 0x611848）、8 AWAKEN |
| T | `NV_PDISP_FE_CHNSTATUS_CORE` 0x610630 | 3:0 stage 1（0 IDLE・1 READ_METHOD・4 CHK_CTXDMA・5 CTX_DMA_LOOKUP・6 WAIT_FOR_STG2・7 WAIT_FOR_UPD・8 EXCEPTION）、7:4 stage 2、20:16 状態（0x0b IDLE・0x0c BUSY・0x08 UNCONNECTED・0x05〜0x07 EFI_INIT1〜EFI_OPERATION）、24 FIRSTTIME、25 method FIFO not empty、26 read pending、27 notifier の書き込み pending |

切り分け:
1. **E の理由が 0 以外** → push の誤り。11:0 の method と D の値を clc57d.h（MIT）と `disp-records.c` の該当の書き手で照合（7 UNRESOLVABLE_HANDLE なら RAMHT の entry か ctxdma の handle、4 INVALID_ARG なら値の field）。
2. **E が 0 で S の bit 0〜2 が立つ** → update が supervisor を起こし RM が処理を終えていない。前回（T1-642）の見立てと同じ形。step 3（disable）で止まれば head の消し方、step 4（owners）で止まれば SOR の割り当てか head の mode の値（`P8b head 0 mode` の行と ARMED の値）を疑う。`rm: control 00731152` の status が 0 でなければ DFP_ASSIGN_SOR の引数。
3. **E が 0、S が 0、T の 20:16 が 0x0b（IDLE）で get = put** → core は push を全部読んで update も済んだが notifier が書かれていない → notifier の ctxdma（handle 0xf0000000、instance memory の object の 24 byte、`gv100_dmaobj_bind`）か notifier の offset（`SET_NOTIFIER_CONTROL` の OFFSET は >> 4）。T の bit 27 が立っていれば書き込みが詰まっている（ctxdma の範囲・aperture）。
4. **T の 3:0 が 7（WAIT_FOR_UPD）** → interlock の相手（window）の update を待っている。`core show` の update は window と interlock するので、window channel の update（0x690000 + w×0x1000 の get/put）を確かめる。
5. **T の 20:16 が EFI_*（5〜7）** → core channel が firmware（GOP）の所有のまま。RM の channel の確保（`channel c57d.0 made`）の後でもこの値なら、RM が GOP からの引き継ぎを終えていない。
6. **`step 1` より前で止まる**（`P8b fail: …` が surfaces・channel の段）→ RM の control の status（`rm: control 20800a58 …`・`rm: class c57d …`）を読む。

監視の補い（実機の結果で要れば次の版で足す。今の image には無い）: `NV_PDISP_FE_EVT_STAT_ERROR_DISP` 0x611848（S の bit 7 の詳細）、`NV_PDISP_FE_SUPERVISOR_MAIN` 0x6107a8（bit 4 = mode switch 中）、`NV_PDISP_FE_SUPERVISOR_HEAD(h)` 0x6107ac + h×4。

nouveau（MIT）との照合（2026-10-15 夜、P2）: channel の確保（`r570_dmac_alloc` の handle `(class << 16) | inst`、引数 instance・offset・subdevice）、RAMHT の context（`r535_dmac_bind`: chid << 25 | client の handle & 0x3fff。nouveau は display 用の別の client を作るが、その client の handle を使う点は同じ。ここは channel を確保した主の client の handle で一貫）、window の user 領域（`r535_wndw` の user 1 → 0x690000 + w×0x1000）、core の user 領域 0x680000 は P1 の照合どおり違い無し。core の start を最初の commit と別の update で送る点だけが nouveau と違う（切り分けのため、head を変えない update）。

## P8c の設計: zedBSD の GPU の display の口へ（2026-10-15 P1、設計まで）

最終の目標は Keiland（compositor）が RTX の画面に出ること。口は i915 の resident display と同じ `struct drv_gpu_ops` の `display`（`include/drivers/gpu/gpu-display.h`）と `scanout`（`gpu-scanout.h`）の表（design 5 節の表の DISPLAY・DISPLAY_EVENTS・scanout の行）。P8b の部品をそのまま使う:

| op | nvrtx の実装（P8b の部品） | 後の Phase |
| --- | --- | --- |
| `display.query` | P8a の readout（lit の head、SOR・protocol、ARMED の timing と viewport）。EDID は RM の `SPECIFIC_GET_EDID_V2`（nouveau `r535_tmds_edid_get`）で足す | p006 P8c |
| `display.mode` | 最初は GOP の mode（ARMED の viewport と raster）だけを返し、別の mode は ENOTSUP（design の判断 7） | P8c |
| `display.claim`・`release` | claim で core と window の channel を作り head を引き継ぐ（P8b の 0〜4、表示する surface は claim の側の buffer）。release で GOP 相当の自前の console の surface に flip で戻し、channel は残す（RM が落ちた時は buffer を reset まで保持、design M11） | P8c |
| `display.present` | window の `SET_OFFSET` と update（`nvrtx_disp_window_flip`・`window_update`）、完了は window の notifier。surface は VRAM の BO（scanout の constraints: VRAM の連続、offset 256 byte、pitch 64 byte、linear） | P8c と p007（BO・RESOURCE） |
| `display.wait`・`events` | vblank は head の timing の割り込み（nouveau `r535_head_vblank_get`: 0x611d80 の bit 1 を立て、0x611c00 を見て 0x611800 に書いて消す）。GSP の道でも display の stall の割り込みは CPU に来る（`nvkm_gsp_intr_stall` で vector を RM に聞く） | P8c |
| `scanout.query_device`・`constraints` | VRAM の連続と整列を返す。`import_image` は ENOTSUP（system memory の scanout は未確認、design の判断 9） | P8c |
| `cursor_*` | core の `HEAD_SET_CONTROL_CURSOR`・ctxdma・offset（`headc37d_curs_set`）。最初は ENOTSUP でよい | 後 |

- 前提（計画に無い依存ではないが順序の制約）: present の surface は compositor の BO なので、`GPU_CAP_RESOURCE`（VRAM の BO の確保と CPU からの map、BAR1）が先に要る。BAR1 の map は p005 の未実施（golden context は済、BAR1/BAR2 は未）。したがって Keiland が RTX に出るまでの順は: P8b の実機 PASS → BAR1 の map と VRAM の BO（p005 の残り、または p007 の RESOURCE）→ P8c の display・scanout の ops → `drv_gpu_ops` の登録（p007）。3D・compute の投入の口は shader の compiler（P3、p008）と p007 で合わせる（必要なら Q1 経由）。
- text console の付け直し（設計の判断の項目 12、pcat の text の口）は WS037 の範囲の外の依存のまま。方式 A で GSP の起動の後に console を自前の surface に出すには、この口が要る。

## P8c の実装（2026-10-15、P1、`2c4d1a39f`。build まで、実機は未）

`nvrtx.resident=1`（新しい token）の時、P8b の引き継ぎが通った後に GSP を止めず、GPU を zedBSD の GPU の device として登録する（`drv_gpu_register`、/dev/gpuN）。以後 GSP は電源を切るまで動いたまま（detach は EBUSY）。P8b の表示の待ちは resident の時は 3 秒ずつ。

| 物 | 中身 |
| --- | --- |
| `node.c`（新） | `drv_gpu_ops`: capability = RESOURCE・BLOB・TRANSFER・MAPPING・DISPLAY。storage（`resource_create`）は RAM の連続（`kern_pmem`）、blob（`blob_create`）は P4m の VRAM の BO を BAR1 に map（0 で塗る）。`resource_read`/`write`（storage は memcpy、blob は BAR1 越し）、`resource_map`（storage は RAM、blob は BAR1 の物理を DEVICE で）、`get_info`（driver "nvrtx"、1 resource 64 MiB まで）。destroy は画面に出ている blob を先に console の surface に戻す（戻せなければ VRAM を返さず保持） |
| `present.c`（新） | `drv_gpu_display_ops`（query・mode・claim・release・present・wait）。display は 1 つ（id 1、generation 1）: GOP の head、mode は GOP の mode だけ（見える大きさと ARMED の clock・raster からの refresh）。lease は 1 つ。present は FIFO: storage は CPU が BAR1 越しに node の 2 枚の frame の片方へ写して flip、blob はそのまま flip（offset 256 byte・pitch 64 byte の揃え）。flip の begun を待って返るので wait はすぐ答える。release・close・画面の blob の destroy で console の surface（P8b の A）に戻す。flip が始まらなければ display を broken にし、以後の present は EIO |
| `scanout.c` | `nvrtx_scanout_frame()`: window の image 全部・flip・update を送り begun を待つ。push buffer の端では nouveau の wind（jump を書いて PUT を 0 に、PUT は jump を含めない） |
| `disp-records.c` | surface に format（A8R8G8B8＝BGRA・A8B8G8R8＝RGBA）、`nvrtx_push_jump` |
| `gsp.c`・`nvrtx.c`・`stage.c` | P8b の後に `nvrtx_node_start`、成功なら `gsp_finish` を呼ばずに worker を終える。detach は resident なら EBUSY |
| `config-amd64-nvrtx.mk` | `NVRTX_RUN=R8c`（`nvrtx.gsp=1 nvrtx.disp=1 nvrtx.resident=1`、`display-inventory`・`display-control` を image に） |

- 範囲の外・後の段: `GPU_CAP_DISPLAY_EVENTS`（vblank・hotplug）・`DISPLAY_CONTROL`（power・refresh）・cursor・scanout の表（`query_device`・`constraints`）・SHARE（Keiland の direct scanout は他の fd からの import が要る）・3D と compute の投入（p007）。Keiland の compositor は libvulkan（Venus の executor）を通るので、RTX の画面に出るには p007 が要る。
- resident の間に GSP が送る非同期の message（RC・hotplug など）は、登録の後に起こす drain の thread（`node_drain`、1 秒ずつ `nvrtx_rpc_wait` で受けて捨てる。RC は `P6 channel error: …`、知らない message は `rpc: message …` の印）が読む（nouveau の msgq の worker と同じ役）。device の ops は GSP に RPC を出さないので reader は 1 つ。
- 確認: nvrtx=y の kernel rc 0・warning 0・include check / vmunix check PASS、host 試験 222 PASS（P8c の ops は hardware の上だけで host の builder は無い）。実機は未実施。

### T への手順（R8c、rombar=1。R8b の再試験（T1-645）が PASS してから）

image は `NVRTX_RUN=R8c`（既定 on の後の語は `nvrtx.gsp=1 nvrtx.disp.test=1`、resident は既定。P8b の帯は 3 秒ずつ）。QEMU は `~/nvrtx/start-rombar1.sh`、run の前に host の reboot と `prepare.sh --check`。
1. 起動の印: R8b の合格の印（step 1〜4、`P8b ok: …`）に続いて `P8c begin` → `P8c ok: gpu device registered, display 1280x1024 at 60020 mHz` → `P8c: the GSP stays up until power-off`（`P7` の行は出ない）→ login prompt。
2. serial で: `display-inventory`（GPU の node が複数なら `--node=/dev/gpuN` で nvrtx の物。`DISPLAY-INVENTORY output index=0 … connected=1 … mode=1280x1024@60020 … name=nvrtx head`）。
3. `display-control --hold=10`（同じ node）: `DISPLAY-CONTROL claim lease=1`、`DISPLAY-CONTROL present error=0`、power・refresh の行は EOPNOTSUPP（zedBSD の errno で 21）でよい（DISPLAY_CONTROL は未実装）、`DISPLAY-CONTROL done error=0`。画面の印 `P8c lease 1 claimed`・`P8c lease 1 released after 1 frames`。
4. monitor（目視）: present の後、緑（BGRA 0xff30c060）の一面が約 20 秒（hold 10 秒 × 2）、release の後に 8 本の帯（上が白）に戻る。
5. 後始末: GSP は止まらないので guest を止めた後 **host を reboot**。
FAIL の時: `P8c fail: …`・`P8b fail: frame notifier …`・`P8b core exception …` の行を PNG に。

## P8c の続き: events・control・cursor・SHARE・scanout の表（2026-10-15 P1、`2791fc6e3`・`e6699aa70`。build と host 試験まで、実機は未）

Q1 の依頼（Keiland の compositor を RTX に出すまでの不足分、順に 1. DISPLAY_EVENTS と DISPLAY_CONTROL、2. cursor、3. SHARE と scanout の表）。すべて `CONFIG_DRIVER_PCI_NVRTX=y` の中、既定 n は不変。nouveau（MIT）と open-gpu-doc（MIT）に従い、GPL の物は使っていない。

| 物 | 中身 | 出典 |
| --- | --- | --- |
| DISPLAY_EVENTS | `GPU_CAP_DISPLAY_EVENTS`。この ABI の events は topology の sequence（接続・切断の変化、i915 と同じ意味）で、**固定の 1**。RM の hotplug の event（`NV01_EVENT_KERNEL_CALLBACK_EX` と `NV2080_NOTIFIERS_HOTPLUG`、nouveau `r535_disp_oneinit`）はまだ取らないので動かない。vblank は ABI では DISPLAY_CONTROL の refresh、flip の完了は present（begun を待って返る）と wait（すぐ答える）が担う | i915 `drv_i915_display_events` |
| DISPLAY_CONTROL | `GPU_CAP_DISPLAY_CONTROL`、query に `REFRESH_COUNTER`。**refresh**: head の frame counter（`NV_PDISP_RG_DPCA(head)` = 0x616330 + head×0x800 の 31:16、読みに副作用なし）を待つ間 tick ごとに読み、16 bit の差分を足して戻らない count にする（i915 `control.c` の PIPE_FRMCOUNT と同じ形、count は 1 から）。message drain の thread も毎秒読み、wrap（60 Hz で約 18 分）に追いつく。**power**: display・generation・lease（EBUSY）を確かめた後 **EOPNOTSUPP**（`GPU_DISPLAY_POWER_CONTROL` は立てない）。理由: 出力の off/on は RM の modeset（head の disable → `DFP_ASSIGN_SOR` → enable）が要り、device の ops は GSP に RPC を出さない（drain が channel の唯一の reader） | open-gpu-doc `manuals/turing/tu102/dev_display_withoffset.ref.txt`、i915 `control.c` |
| cursor | `GPU_DISPLAY_HAS_CURSOR`、`cursor_set`・`move`・`hide`。node の起動（drain の前）に head の cursor の PIO channel（0xc57a、handle `(0xc57a << 16) \| head`）: `INTERNAL_DISPLAY_CHANNEL_PUSHBUFFER`（memory 無し・valid 0・subdevice 1）→ display root の下に alloc（16 byte、channelInstance = head）。画像は VRAM の 2 slot（64×64 A8R8G8B8、16 KiB ずつ、BAR1 越しに CPU が写す。RGBA は R と B を入れ替え、画像の外は透明）。**表示**: cursor channel（user 0x6d8000 + head×0x1000）に POINT_OUT（0x208、x 15:0・y 31:16 の 16 bit 2 の補数）と UPDATE（0x200 = 1）、core に `HEAD_SET_CONTROL_CURSOR` 0x800001cf（enable・W64_H64・A8R8G8B8）と `COMPOSITION` 0x72ff（K1 255、cursor×K1 + 画面×(1 − K1×α)＝premultiplied）、`CONTEXT_DMA_CURSOR`（core の VRAM の ctxdma）、`OFFSET_CURSOR`（>> 8）、core の update（cursor の interlock、notifier を頼むが待たない）。**移動**は cursor channel だけ。**隠す**: `CONTROL_CURSOR` 0xcf・ctxdma 0・core の update（cursor の interlock）。新しい画像は表示中でない slot に書き、前の core の update が終わる前は EBUSY（ABI の「一瞬の EBUSY」）。表示と隠すは前の update を 50 ms まで待つ。lease の終わりで隠して画像を忘れる。core の push buffer（1024 語）は wind する（GET = PUT を待ち、端なら jump と PUT = 0） | nouveau `r535_curs_init`・`r570_disp_chan_set_pushbuf`・`r535_chan_user`・`cursc37a.c`・`curs507a.c`（space: FREE ≥ 4 を 100 ms）・`headc37d_curs_set`/`curs_clr`・`corec37d_update`・`nv50_wndw_flush_set`/`flush_clr`・`nv50_dmac_wind`、open-gpu-doc `clc57a.h`・`clc57d.h` |
| SHARE | `GPU_CAP_SHARE`。blob の VRAM を `struct nvrtx_backing`（参照の数）にし、blob・import・export が参照を持つ。export は参照を 1 つ足し、release で引く。import は同じ backing を指す新しい blob（resource id を付ける）。最後の参照で VRAM を返す（画面が読むかもしれない kept の VRAM は返さない）。`get_scanout_backing` は無し（別の device に VRAM の物理を貸さない） | i915 `resource.c` の share（same device only） |
| scanout の表 | `query_device`: roles = DISPLAY だけ（command stream が無いので RENDER を名乗らない。libvulkan は display だけの node を最初の renderer と組む）、companion 0。`constraints`: SHARED \| COPY（FOREIGN 無し）、BGRA・RGBA、stride 64、offset 256、placement 0、DMA 上限 0。`import_image` は無し（system memory や他の device の BAR を window が読むのは未確認、design の判断 9） | i915 `scanout.c` |
| 試験の道具 | `userland/tests/display-scanout`（新、汎用の GPU ioctl だけ）: info・device・display・events・constraints・claim・別の open の blob を export → import → client を閉じて BLOB で present・cursor の set/表示/100 回の移動/隠す/再表示・release。run は `NVRTX_RUN=R8d`（R8c と同じ boot の語に probe を足した） | — |

- 置き場所: `present.c`（events・power・refresh、ops の表）、`cursor.c`（新、cursor の slot と ops）、`share.c`（新、share と scanout の ops）、`scanout.c`（cursor channel・core の待たない update・wind・frame counter）、`disp-records.c`・`nvrtx-disp.h`（PIO の引数、cursor の method、位置・pixel・frame の計算。host で試す）、`node.c`（backing、capability、drain の refresh）。`platform/amd64/vmunix.mk` の P8c の行に `cursor.c`・`share.c` を足した（NVRTX の ifeq の中）。
- Keiland の direct scanout まで残るもの: (1) client が同じ RTX の node の blob に描く道（p007 の 3D、または CPU の描画）。(2) Keiland の scanout は `/dev/gpu0` 固定（`scanout-zedbsd.c`）で、RTX の node が gpu0 でなければ開かない。(3) 観察（未検証、ws122 の範囲）: Keiland の `scanout_import` は import の image に client の description を入れて送るが、core の `gpu_import_ioctl` は image が 0 でなければ EINVAL を返す。(4) hotplug（events の sequence を動かす）と power は RM の event・modeset が要る。
- 未確定の危険: (1) GSP の下で CPU が RG_DPCA を読めるか（nouveau の r535 は rgpos を持たない）。counter が動かなければ refresh は ETIMEDOUT（display-control の `refresh now boundaries=0`）。(2) 隠すときの core の update は cursor channel の update 無しで cursor と interlock する（nouveau と同じ）。止まれば次の cursor の操作が EBUSY を返し続ける。(3) cursor の PIO channel の FREE が 4 に満たない → `P8c fail: cursor channel has N free entries`。(4) core の push の wind を初めて使う（cursor の変更 1 回で約 16 語、約 60 回で 1 周）→ 止まれば `P8c fail: channel at 680000 stopped, get … put …`。

### 確認（P8c の続き）

| 確認 | 結果 |
| --- | --- |
| nvrtx=y の kernel（`make -j16 ZEDBSD_CONFIG=plan/ws037/tests/config-amd64-nvrtx.mk BUILD=build/ws037-y vmunix`） | rc 0・warning 0・`kernel include check: PASS (420 objects)`・`amd64 vmunix check: PASS`、`git diff --check` 0 |
| host 試験 `sh plan/ws037/tests/nvrtx-gsp-host-test.sh` | PASS 233 項目（P8c の cursor・control の records 11 項目を足した。firmware の cache が無いので実 file の項目は飛ばし） |
| probe（`make … BUILD=build/ws037-y ZEDBSD_USER_PROGRAMS=display-scanout build/ws037-y/bin/display-scanout`） | rc 0（-Wall -Wextra -Werror） |
| 既定 n | 新しい file は vmunix.mk の NVRTX の ifeq の中だけ。既定の kernel は build していない（変えた行は ifeq の中だけ） |
| 実機（方式 B、rombar=1） | **未実施**（R8d を T へ、R8c の PASS の後） |

### T への手順（R8d、rombar=1。R8c（T1-647）が PASS してから）

image は main に merge した commit で `plan/tools/guest/test-image.sh --no-harness plan/ws037/tests/config-amd64-nvrtx.mk build/ws037-nvrtx/r8d NVRTX_RUN=R8d`（語は R8c と同じ `nvrtx.gsp=1 nvrtx.disp.test=1`）。QEMU は `~/nvrtx/start-rombar1.sh`、run の前に host の reboot と `prepare.sh --check`。GSP は止まらないので run の後は **host を reboot**。GPU の node が複数なら nvrtx の物を `--node=/dev/gpuN` で（`display-scanout` の `driver=nvrtx` で分かる）。

1. 起動の印: R8c の印（`P8b ok: …`、`P8c begin`）に続いて `P8c channel c57a.0 made` → `P8c ok: cursor plane 64x64 at …` → `P8c ok: gpu device registered, display 1280x1024 at 60020 mHz` → `P8c: the GSP stays up until power-off` → login prompt。
2. `display-inventory --node=/dev/gpuN`: `DISPLAY-INVENTORY sequence=1 count=1` と output の行（R8c と同じ）。
3. `display-control --node=/dev/gpuN --hold=3`: `display id=1 … power=0 counter=1`、`refresh now boundaries=` 59〜61 `ms=` 約 1000 `virtual=0 error=0`、`claim lease=…`、`present error=0`、`refresh shown boundaries=` 約 60、`power state=off error=21`（EOPNOTSUPP）、`refresh-off error=0`（power が効かないので boundary は続く。ここでは 0 が正しい）、`power state=on error=21`、`refresh on boundaries=` 約 60、`done error=0`。画面は緑の一面（約 9 秒）→ 8 本の帯。
4. `display-scanout --node=/dev/gpuN --hold=5`:
   - `info caps=0x91cd share=1 events=1 control=1 driver=nvrtx`
   - `device roles=0x2 companion=0 error=0`
   - `display id=1 generation=1 flags=0x525 cursor=1 mode=1280x1024@60020`
   - `events sequence=1 error=0`
   - `constraints flags=0x3 formats=0x3 stride=64 offset=256 placement=0x0 error=0`
   - `claim lease=…`（画面の印 `P8c lease N claimed`）
   - `shared blob=0 write=0 export=0 import=0 present=0 sequence=1`
   - `cursor set=0 show=0 moves=100 move=0 hide=0 again=0`
   - `done error=0`（画面の印 `P8c lease N released after 1 frames`）
5. monitor（ユーザーの目視）: 青の一面（client の blob、client を閉じた後も出たまま）5 秒 → その中央に白い 32×32 の四角（黒い縁 2 px）の cursor が 5 秒 → 右下へ約 3 秒かけて 400 px 斜めに動く → 消えて 5 秒 → 止まった所に再び出て 5 秒 → release で 8 本の帯（上が白）に戻り cursor は消える。cursor の乱れ（色・縁の欠け・ちらつき）と、動きの途切れを記録。

FAIL の時: `P8c fail: …`（`cursor channel has N free entries`・`channel at 680000 stopped, get … put …`・`a flip did not begin`）、`P8b core exception …`・`P8b other …`、probe の行の error の数（zedBSD の errno: 17 = EBUSY、5 = EIO、21 = EOPNOTSUPP、3 = EINVAL、48 = EXDEV、42 = ETIMEDOUT）を PNG に。refresh が 0 なら（RG_DPCA が動かない）その旨を記録。

## 既定 on と方法 A（2026-10-15、P1 の作業中の patch を P2 が回収・完成）

ユーザーの判断（2026-10-15）「nvrtx を既定で on」。CONFIG は main で y 済み。runtime の段の既定と、RTX が primary の機械で素で起動する道（方法 A）を足した。build と host 試験まで、実機は未実施。

| 物 | 中身 |
| --- | --- |
| 既定の段 | GSP の起動・P4m・P8a・P8b（引き継ぎ、**console を出す**）・P8c（GPU の device）が既定で走る。試験の段（`nvrtx.gr=1`・`compute=1`・`draw=1`）は従来どおり明示の時だけ。opt-out は `nvrtx.gsp=0`（N1 の後に firmware へ返す）・`nvrtx.disp=0`（P8a で止まる）・`nvrtx.resident=0`（引き継ぎの後に GSP を止める）・`nvrtx.off=1` / `nvrtx=off`（GPU を見ない） |
| 既定で GSP を起こす条件 | **RTX に firmware の画面（GOP の framebuffer）がある時だけ**（P2 が足した: 画面の無い 2 枚目の RTX は既定では触らず、`N1 done; no firmware picture on …, left to the firmware (nvrtx.gsp=1 boots it)`）。`nvrtx.gsp=1` は画面の有無に関係なく起こす（方式 B の試験の run は全てこれ） |
| console の保持（方法 A の本体） | P0 の前に `nvrtx_console_check`: GOP の画面が BAR1 の先頭（offset 0）、BAR1 が physical の mode（0xb80f40 の bit 31 が 0）、形式 BGRX8888 の時に保持（`P0 console WxH pitch P kept: N KiB at the VRAM's start`）。保持できない時は `nvrtx.gsp=1` が無ければ P0 に入らない（`P0 skipped: the console stays the firmware's`）。RM へ `GspSystemInfo.consoleMemSize`（sysinfo の +64）で大きさを渡す（open-gpu-kernel-modules（MIT）の osinit.c と同じ）。P4m で BAR1 の先頭の page を VRAM の先頭へ map し直す（`P4m console: bar1 0..N on the VRAM's start again`）。allocator と CPU の BAR1 の map は console の後から。map できなければ GSP を止めて mode を戻す |
| P8b（既定） | 試験の帯の surface ではなく **console（VRAM の先頭、GOP の pitch）** を window に出す（`P8b console WxH pitch P at the VRAM's start` → `P8b ok: head H shows the console`）。step の 5 秒の待ち・帯・flip は `nvrtx.disp.test=1` の時だけ。console が mode の見える大きさと違う、pitch が 64 の倍数でない時は head に触れない（`ENOTSUP`、GOP のまま） |
| 終わり | `gsp_end`: console を保持して GSP が動いていれば止めない（`P8: the GSP stays up to keep the console`、RM の unload が console の map と head を壊すため）。GSP を止めた時は BAR1 の mode を firmware の値に戻す（`P7 console: bar1 mode …`） |
| run | `config-amd64-nvrtx.mk`: 既存の run は既定 on に合わせ opt-out の語を足した（R0 `nvrtx.gsp=0`、R7・R7g・R7c・R7d は `nvrtx.disp=0 nvrtx.resident=0`、R8b は `nvrtx.disp.test=1 nvrtx.resident=0`、R8c・R8d は `nvrtx.disp.test=1`）。新しい **RA**（語なし＝本番の既定の boot、`display-inventory`・`display-control` を image に） |

未確定の危険: (1) RM が BAR1 を virtual にしてから P4m が console を map し直すまで、kernel の console の書き込みは BAR1 の fault になる（RM が BAR1 の fault で RC を起こすかは未確認）。(2) GOP の解像度と head の見える大きさが違う（scaler を使う GOP、T1-640 の 1280x800 → 1280x1024）と console を出さない。(3) GSP の firmware は CI の image（config/ci/config-amd64.mk）に入っていない（`nvidia-gsp-firmware` は license の同意が要る package）。入っていない image では P0 で firmware の読みに失敗し（`P0 fail …; the GPU is untouched`）、GOP の console のまま（function は保持され system suspend は断る）。image への収録は Q1・ユーザーの判断。

### 確認

| 確認 | 結果 |
| --- | --- |
| `make -j16 BUILD=build/p2/amd64 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/p2/amd64/vmunix`（p2 worktree） | rc 0・warning 0・`kernel include check: PASS (420 objects)`・`amd64 vmunix check: PASS`、`git diff --check` 0 |
| `sh plan/ws037/tests/nvrtx-gsp-host-test.sh` | `PASS nvrtx gsp: 234 checks`（sysinfo の consoleMemSize の 1 項目を足した。firmware の cache が無いので実 file の項目は飛ばし） |
| 実機（方法 A、RA） | **未実施** |

### T への手順（RA、方法 A: RTX 2070 の機械（10.0.30.5）を USB から素で起動）

image: `plan/tools/guest/test-image.sh --no-harness plan/ws037/tests/config-amd64-nvrtx.mk build/ws037-nvrtx/ra NVRTX_RUN=RA`（main の merge の commit、firmware の license は `yes | make firmware-licenses`）。USB に書いて RTX の出力に monitor を繋いで起動（BIOS で CSM off、primary display が PEG）。判定はユーザーの目視と写真（serial は無い）。
合格: `N1 gop WxH … in bar1 at +0` → `P0 console … kept` → `P0 ok …` → … → `P4m console: bar1 0..… on the VRAM's start again` → `P8 ok: display … lit 1` → `P8b console …` → `P8b ok: head 0 shows the console` → `P8c ok: gpu device registered …` → login prompt が **RTX の monitor に出続ける**（GSP の起動の間に一時的に印が欠けるのは可）。`display-inventory` で nvrtx の output の行。
FAIL の時: 最後に見えた印の写真。`P0 console …: cannot be kept`（GOP の位置・形式）、`P4m fail: the console's …`、`P8b: the console is …, the mode …`（大きさの違い）は値を記録。画面が消えたら、その直前の印。

## Resume point

（2026-10-15 P1 の更新）T の順: R8b の再試験（T1-645、P8b の直し）→ R8c（T1-647、GPU の device）→ R8d（上の「P8c の続き」の手順: events・refresh・cursor・SHARE・scanout の表）。どれもユーザーが RTX の monitor の前にいる時。R8b が FAIL なら印で切り分け、引き継ぎが RM に受け入れられない場合は (a) の nouveau の初回 modeset（`DFP_ASSIGN_SOR`、TMDS なら HDMI の設定、DP なら DPCD と `DP_CTRL`）を足す。R8d の後に残るもの（Keiland の direct scanout まで）: client が RTX の blob に描く道（p007）、Keiland の scanout の `/dev/gpu0` 固定、hotplug の event と power（RM の event・modeset が要る）。

以前の resume（P8b の実装の時点）: P8b は実装・build・host 試験まで（`b893faeb6` と直し）。次は T の R8b（rombar=1、上の手順）とユーザーの目視。PASS なら P8c（上の設計、BAR1 の map と VRAM の BO が先）。
