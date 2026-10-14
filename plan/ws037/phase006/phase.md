<!-- awesome-plan project=zedbsd record=ws037-p006 -->

# ws037-p006: display（段 P8）

Status: in-progress（2026-10-15 P1。P8a は実機（方式 B、T1）PASS。P8b（GOP の head の引き継ぎ・自前の surface・flip）を実装、build と host 試験まで（`b893faeb6` と直し）。実機の R8b は T 待ち）
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

## Resume point

P8b は実装・build・host 試験まで（`b893faeb6` と直し）。次は T の R8b（rombar=1、上の手順）とユーザーの目視。PASS なら P8c（上の設計、BAR1 の map と VRAM の BO が先）。FAIL なら印で切り分け、引き継ぎが RM に受け入れられない場合は (a) の nouveau の初回 modeset（`DFP_ASSIGN_SOR`、TMDS なら HDMI の設定、DP なら DPCD と `DP_CTRL`）を足す。
