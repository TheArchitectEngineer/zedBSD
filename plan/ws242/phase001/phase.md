<!-- awesome-plan project=zedbsd record=ws242-p001 -->

# ws242-p001: RTX 30・40 の対応（driver）と RTX 20/30/40 の全モデルの認識

Status: in-progress（2026-10-15 P2、利用量の都合で Q1 の通常ラップアップ。途中の WIP commit まで。下の「再開点」）
Disposition: normal
Parent: [WS242](../ws.md)
実行者: P2（phase-runner）
依存: なし（compiler の側は ws242-p002、P4。main 8a550f840 に SM86・SM89 の API が入った）

## 範囲（ユーザー 2026-10-15、ws.md の原文）

1. RTX 20（TU102・TU104・TU106、GTX 16 の TU116・TU117）・RTX 30（GA102〜GA107）・RTX 40（AD102〜AD107）を PCI の device id と identity register の chip で認識する。
2. firmware の package に GA10x・AD10x（と TU116 の booter）の GSP-RM・booter・bootloader を足す（570.144、tu102 と同じ版）。
3. 起動・RM・class の差を chip の表（profile）で切り替える。30/40 は実機の確かめなし（host 試験と build まで）。RTX 2070 の今の道を変えない。

## 設計: chip → profile の表（`src/drivers/gpu/nvrtx/identify.c`、型は `nvrtx-private.h` の `struct nvrtx_profile`）

| profile | chip | firmware（/lib/firmware/nvidia/ の下） | 署名の section | falcon | VRAM size | display fuse | heap os/min | scrubber | channel/copy/3D/compute | display root/core/window/wimm/cursor | window storage | QMD | SM | 既定で起動 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| TU10x | 0x162・0x164・0x166 | tu102/gsp/{gsp,bootloader,booter_load,booter_unload}-570.144.bin、tu102/acr/bl.bin | .fwsignature_tu10x | ports（今の道） | 0x100ce0（magnitude・scale） | 0x021c04 | 0 / 64 MiB | あり | c46f/c5b5/c597/c5c0 | c570/c57d/c57e/c57b/c57a | 0x10 | 2.2 | 75 | する（verified） |
| TU11x | 0x167・0x168 | image・bootloader・bl.bin は tu102、booter は tu116 | .fwsignature_tu11x | ports | 同上 | 同上 | 同上 | なし | 同上 | 同上 | 0x10 | 2.2 | 75 | nvrtx.gsp=1 の時だけ |
| GA10x | 0x172・0x173・0x174・0x176・0x177 | ga102/gsp/ の 4 つ（bl.bin なし） | .fwsignature_ga10x | boot ROM（DMA） | 0x1183a4（MiB） | 0x820c04 | 22 MiB / 170 MiB | なし | c56f/c7b5/c797/c7c0 | c670/c67d/c67e/c67b/c67a | 0（layout field なし） | 3.0 | 86 | nvrtx.gsp=1 の時だけ |
| AD10x | 0x192・0x193・0x194・0x196・0x197 | image は ga102、bootloader・booter は ad102 | .fwsignature_ad10x | boot ROM | 同上 | 同上 | 同上 | なし | c56f/c7b5/c997/c9c0 | c770/c77d/c67e/c67b/c67a | 0 | 3.0 | 89 | nvrtx.gsp=1 の時だけ |

- GA100・GH100・Blackwell は名前だけ（profile なし）。Blackwell は WS240。
- 実機で未確認の family（verified=0）は既定では firmware の画面のまま残し、`nvrtx.gsp=1` で起動する（Q1・ユーザーの判断の候補: 30/40 も既定で起動にするか）。
- 製品の表: `src/drivers/gpu/nvrtx/products.c`（288 の device id、chip・laptop・名前）。生成 `plan/ws242/tests/nvrtx-products-gen.py`、入力 pci-ids.ucw.cz の pci.ids 2026-10-15（sha256 1fc11fdf2fb011ade1689cc428bac8ad90c42c1b4ff09e2b6a4220eb1af6e99e）。open-gpu-kernel-modules 570.144 の README の id と照らし、TU/GA/AD で抜けなし（抜けは GA100・GH100 の data-centre だけ）。N0 で製品名を印に出す（chip は identity register が決める）。

## 出典（値は事実として使用、名前・構造は独自、guardrail の WS037 の節）

- nouveau（Linux v6.19、plan/ws037/temp/linux、MIT の file）: `nvkm/subdev/gsp/{tu102,tu116,ga102,ad102,fwsec}.c`、`nvkm/falcon/{ga102,ga100,gm200,gp102,base,fw}.c`、`nvkm/engine/sec2/{tu102,ga102}.c`、`nvkm/subdev/gsp/rm/{tu1xx,ga1xx,ad10x}.c`・`rm/r570/{rm,gr}.c`・`rm/r570/nvrm/gsp.h`（heap）、`nvkm/subdev/fb/ga102.c`（VRAM size）、`nvkm/subdev/devinit/{tu102,ga100}.c`、`nvkm/engine/dma/usergv100.c`、`dispnv50/{core,wndw,wndwc67e}.c`、`engine/device/base.c`。
- open-gpu-doc（MIT）: classes の clc57d/clc67d/clc77d・clc57e/clc67e・clc57a/clc67a・clc57b/clc67b・clc597/clc797/clc997・clc5c0/clc7c0/clc9c0・clc3c0qmd/clc6c0qmd・clc5b5/clc7b5・clc46f/clc56f・tex。
- open-gpu-kernel-modules 570.144 `src/nvidia-modeset/src/nvkms-evo3.c`（MIT、C6 の window に MEMORY_LAYOUT が無いこと）。
- Mesa 25.3.6（MIT）: NAK `qmd.rs`（QMD 3.0）、NVK `nvk_cmd_dispatch.c`（Ampere の SEND_SIGNALING_PCAS2_B）、winsys `nouveau_device.c`（shared の carve-out）。

## 世代の差の調べ（結果）

- MMU・BAR・fault・doorbell（0xbb0090）・devinit 待ち（0x118128/0x118234）・VGA workspace（0x625f04）・WPR2（0x1fa824/828）・FWSEC の結果（0x1438/0x1454）・SEC2/GSP の base・display の user 領域と armed・HDMI infoframe・texture header: Turing〜Ada で同じ（nouveau が GA102/AD102 にも tu102 の bar/mmu/fault と gv100 の dma を使う）。
- 3D の method: c597→c797 は SET_BINDLESS_TEXTURE（nvrtx は未使用）の削除と追加だけ、c797 と c997 は同一。display core: c57d→c67d/c77d は追加だけ。window: c67e は SET_STORAGE の MEMORY_LAYOUT（bit 4）が無い → profile の window_storage（pitch は context DMA の kind=0 で示す）。copy: c7b5 は BYPASS_L2 の bit の削除だけ（nvrtx 未使用）。channel: c56f は追加だけ。
- QMD 3.0（clc6c0qmd V03_00）: 2.2 から version（3,0）、shared config の幅 7→6 bit、BARRIER_COUNT 955→763、SLM の場所、CRS なし。他は同じ位置。Ampere/Ada の SM の carve-out {0,8,16,32,64,100} KiB、warp 48。
- 起動: GA/AD は FWSEC v3（VBIOS の desc の 44 byte の後に 384 byte の署名、fuse 0x8241c0+(ucode-1)*4 で選ぶ）、booter は meta（fuse・engine・ucode、実 file で 1/1/3）・fuse 0x824140 系で選ぶ署名、DMA（0x110〜0x128）で IMEM・DMEM、boot ROM の parameter（+0x1210・0x119c・0x1198・0x1180）。falcon の reset に select（+0x1668）・reset_ready（0x0f4 bit31）・scrub（0x0f4 bit12）。GSP の RISC-V への切り替えに +0x1668 の 0x111、RISC-V の status は +0x1388 bit7。SEC2 の PMC の bit は使わない（nouveau の GSP の道と同じ）。

## 実装済み（WIP commit、下の SHA）

- firmware の package（`userland/firmware/nvidia-gsp/` の Makefile・manifest・notice、`userland/firmware/README.md`）: 16 file（新しい 9: tu116 の booter 2、ga102 の 4、ad102 の 3。大きさ・sha256 は linux-firmware dc85cced から取り直して確かめた、Makefile の表）。cache の directory を `<rev>-tu-ga-ad` にした（古い 7 file の cache を壊れた cache と誤らない）。scrubber-570.144.bin・tu116/acr/bl.bin（tu102 と同一）は入れない。image が約 64 MB 増える。zedBSD-firmware の mirror には新しい file が無い（upstream に落ちる）。
- driver: profile と chip の表（identify.c）、products.c、firmware の file 名と署名の section（gsp.c・file.c）、VRAM size と heap（layout.c、`nvrtx_wpr_layout` に heap の 2 つの引数、`nvrtx_vram_size_mib`）、FWSEC v3（vbios.c）、booter の meta と fuse の署名選び（image.c）、Ampere の falcon（falcon.c: reset・DMA・boot ROM・RISC-V mode/status）、boot ROM の FWSEC・booter（boot.c）、sequencer の resume（rpc.c）、display fuse（rom.c）、attach の verified の判断（nvrtx.c）、N0 の製品名（discover.c）、class を profile から（channel・compute・gr・draw・work・display・scanout、copy fill に class 引数、draw init の class 版 `nvrtx_push_draw_init_class`、window の storage 引数）。`platform/amd64/vmunix.mk` に products.c。
- build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws037-k-y build/ws037-k-y/vmunix` rc 0・warning 0（2026-10-15 12:05、main 27b361f75 を取り込んだ tree）。

## 再開点（未完）

1. **compiler の SM**（Q1 の連絡、main 8a550f840）: vk-pipeline.c:676・draw.c:443・compute.c:708・vk-execute.c:388 を `nvrtx_sm75_compile_*_sm(..., profile->sm, ...)` に。vk の層は `session->owner` しか持たないので、owner の口 `nvrtx_vk_shader_model(void *owner)`・`nvrtx_vk_threed_class(void *owner)` を nvrtx-vk.h に足し node.c で実装、host 試験（nvrtx-vk-host-test.c・nvrtx-vk-d3-host-test.c）に stub を足す。vk-execute.c:428 の `nvrtx_push_draw_init` を `nvrtx_push_draw_init_class(..., nvrtx_vk_threed_class(owner))` に（vk-execute は P1 も変更中、呼び出しの行だけ）。draw.c:564 も class 版に。
2. **QMD 3.0**: compute-launch.c に `nvrtx_qmd_build_for(qmd, info, profile)`・`nvrtx_push_compute_launch_for(push, va, profile)` を足す（旧名は Turing の wrapper のまま、nvrtx-sm75-host-test を壊さない）。3.0 は上の位置、PCAS2_B（0x2c0、値 3）、class は profile。compute.c の呼び出しを替える。
3. **host 試験の追従**（今は compile が通らない）: nvrtx-gsp-host-test.c の `nvrtx_wpr_layout`（heap 引数: Turing は 0, 64 MiB）・`nvrtx_push_copy_fill`（class 0xc5b5）・`nvrtx_disp_first_window(_flip)`（storage 0x10）、vbios の「version 3 refused」の項を v3 の解読の項に。nvrtx-identify-host-test.c の `gsp_tu102` を profile に。新しい項: 全 chip → profile、profile の各 field の値（上の表）、製品の表の整列・重複なし・chip の一致、booter の meta と fuse の index（実 file があれば ga102・ad102・tu116 の booter を parse）、FWSEC v3 の合成 ROM、`nvrtx_vram_size_mib`、Ampere の heap の layout。RTX 2070（TU106、8 GiB）の layout・class・storage・QMD 2.2 が今と同じことを確かめる。試験の実 firmware の既定の path を `<rev>-tu-ga-ad` に。
4. 規約の自己 review（coding-style 全文）、`git diff --check`、build を取り直し、WIP commit、Q1 に merge 依頼。T1 に RTX 2070（10.0.30.5）の回帰（desktop まで）を依頼する案を Q1 に。

## 未実施

- 30/40 の実機（無い、ユーザーの指示どおり）。RTX 2070 の実機の回帰（T1、上の 4）。firmware の package の fetch（新しい cache）と image の build（T1/Q1）。
