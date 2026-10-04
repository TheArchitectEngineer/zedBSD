<!-- awesome-plan project=zedbsd record=ws037-license-audit -->
# nvrtx の参照の正本と license の監査（ws037-p001）

作成: 2026-10-04、P2（q692）。design-reviewer の review（2026-10-04）を反映。この文書は GPL の code・comment・定数の名前を含まない（path・hash・license の判定だけ）。

## 正本

| 正本 | 版 | 取得元（`plan/ws037/temp/`、commit しない） | 用途 |
| --- | --- | --- | --- |
| Linux の nouveau | tag `v6.19`（commit `05f7e89ab9731565d8a62e3b5d1ec206485eeb0b`） | `https://github.com/torvalds/linux`（sparse、`drivers/gpu/drm/nouveau/`・`include/uapi/drm/nouveau_drm.h`） | GSP の起動・RPC・channel・MMU・display の手順 |
| Mesa（NVK・NAK） | tag `mesa-25.3.6`（commit `06f9e28304d5d3f109c33535c1c25b9df5769af2`） | `https://gitlab.freedesktop.org/mesa/mesa.git`（sparse、`src/nouveau/`） | 世代の差、class、userspace が kernel に求める物、compiler |
| NVIDIA open-gpu-kernel-modules | tag `570.144`（commit `8ec351aeb96a93a4bb69ccc12a542bf8a8df2b6f`） | `https://github.com/NVIDIA/open-gpu-kernel-modules`（sparse、SDK の class・ctrl の header、GSP の header、RPC の struct） | RM の API（class・ctrl・RPC）の定義、device ID の一覧（README） |
| NVIDIA open-gpu-doc | commit `9fdf5c4062007929d9f4e6cbad9c9771fe61b880` | `https://github.com/NVIDIA/open-gpu-doc` | class の method、manual（register）、VBIOS・Falcon の文書 |
| linux-firmware | commit `d947e4e8e314e9254a1242dc1a5d9cede2cce33d`（調べの時の版）。package の取得元は tag `20260410`（commit `dc85ccedc9c973682fbcf4d628ca61174bcc3120`、AX211・i915 の package と同じ）。下の 5 つの firmware の file の SHA-256 は両方で同じ（2026-10-04 に確かめた） | `https://gitlab.com/kernel-firmware/linux-firmware.git`（sparse、`nvidia/tu102/`・`tu106/`・`WHENCE`・`LICENSES/LICENCE.nvidia`） | GSP の firmware（BLOB） |

判定の方法: 各 file の先頭 6000 byte の SPDX の行、または MIT の permission notice の機械判定（`plan/ws037/temp/lic.py`、commit しない）。先頭だけの判定は file の途中に埋め込まれた別の license を見逃すので、取り込む file は p009 で全文を確かめる（例: 下の open-gpu-doc の html）。

## 判定の要点

1. **nouveau は大部分が MIT**（WS141 の vc4・v3d と違う）。取得した nouveau の file 1306 個の内訳: MIT 1246、UNKNOWN 57、GPL-2.0-only 1、GPL-2.0 OR MIT 1、GPL (notice) 1。MIT でない物は `Kconfig`（GPL-2.0-only）、`nouveau_ttm.c`（GPL-2.0 OR MIT）、古い falcon の source 1 個（GPL の notice）と、license の表記の無い header など（kernel の既定の GPL-2.0 として扱う）。Guardrail の WS141・WS037 の方式（作業の文書は temp で commit しない、定数は一括で改名、最後に類似の監査）はそのまま守る。
2. **open-gpu-kernel-modules は、取得した範囲では MIT**（`COPYING`: 断りの無い file は MIT。ただし Linux の kernel module として link した物は MIT/GPLv2 の dual になるという注記がある。zedBSD は header の定義だけを取り、Linux の module を作らない）。取得した 914 個の内訳: MIT 912、UNKNOWN 2（`README.md`・`COPYING`）。RM の class・ctrl・RPC の struct の定義の出典はこれにする。
3. **open-gpu-doc は MIT**（repository の `LICENSE.md`、SHA-256 `414e9305cf86aeb46f2ce7c447049d32c7aa9349cc909a42fffb3c2d1a2e0134`）。取得した 367 個の内訳: MIT 301、UNKNOWN 66（表記の無い物は html・pdf・csv などで、repository の MIT に従う）。**ただし asciidoc で作った html（`Falcon-Security.html` の 532〜537 行、`Shader-Program-Header.html` の 553〜558 行など）は GPL の JavaScript を埋め込んでいる**。本文（事実の記述）は MIT、埋め込みの script は GPL で取り込まない。
4. **Mesa の `src/nouveau` は大部分 MIT**。取得した 389 個の内訳: MIT 338、UNKNOWN 51。表記の無い物（`headers/nv_push.h`・`winsys/` など）は Mesa の既定（`docs/license.rst`: 大部分 MIT、file ごとに SPDX を見よ）。
5. **BLOB**: GSP の firmware（`gsp-570.144.bin` など）は NVIDIA の `LICENCE.nvidia`（再配布可。OSI の open source の OS で、binary を変えず、license の写しを添えるとき。reverse engineering・翻訳・貸与の禁止、特許の訴えをした者の権利の停止、輸出の規制、NVIDIA の processor でだけ使う）。base の system・kernel・repository の source には入れず、`userland/firmware/README.md` の規約の既定 off の firmware の package（tag `20260410` から取得・検証、`/lib/firmware` へ LICENCE.nvidia・WHENCE・manifest と一緒に install）にし、driver が file から load する。NVIDIA の MIT の header に定義された container（ELF の section・bin の header）だけを読み、disassemble しない。`gen_bootloader-570.144.bin` は Turing の道で使わないので入れない。nouveau・open-gpu-kernel-modules の source の中に firmware に当たる byte の配列があるかは p009 で確かめる（今回の調べの範囲では、GSP の道は全部 file から load する）。
6. VBIOS（FWSEC を含む）は GPU の ROM から実行時に読む。repository に入れない。

## file ごとの表（作業の文書が参照した file）

| path（Linux v6.19、commit `05f7e89ab9731565d8a62e3b5d1ec206485eeb0b`） | SHA-256 | license | 扱い |
| --- | --- | --- | --- |
| `drivers/gpu/drm/nouveau/gv100_fence.c` | `90fd0491398dd6e4e563967a628ef0a7b68d1f61ad9da8765f5ab179b0e1c8b8` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nouveau_abi16.c` | `c15e290e989b4d13bb527e53cdf4553ac10ba71935941e4dbe452229273e0e14` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nouveau_chan.c` | `394e43ccdbd5fa9e1df4d94dea6eab17ea2d1873e6d62ff6e4716a4c47466ea0` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nouveau_drm.c` | `db67d4fceae915ff30108da2f1f0fa51a55afdbd9cf3f54432d13b4d2fafa69f` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nouveau_exec.c` | `46bd717b115103ef62bb733a00d761ab2b1137ecd1e3fe2b595cd82d41e7431b` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nouveau_fence.c` | `537a4ea14fad343610baf26416f2516717a1e7ff781ae4219636f884894204b2` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nouveau_sched.c` | `b0536a7e8b405bcb30c381b3cdfdb526166e27e6b6e22ba14ebe3ddd80224157` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nouveau_uvmm.c` | `0849670fe53980ba3ada0653b36322e3c80016032e3ccfc0a9280a00d13d0895` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvif/chan.c` | `e98905277d7b5d6d8c7efecf76dab79013307a36475f2ec5c34f31498cb459e5` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvif/chan506f.c` | `b54547fd09b608fe3551dfe6c73d1ea86c20511d2756a7ca478ba06b31d0b429` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvif/chan906f.c` | `22a6898f0abd328323c2cd6ea61ea3aa77e3ffca5f2cc4a078bc0cd8afeb68c3` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvif/chanc36f.c` | `db4e75d1b74edb05cbbd0aac2a2bb74c741cc6ae981ed155c3ff769b258dcda9` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvif/userc361.c` | `50acac4e13f9f9a1516977c3b5b67e5ea4937104011bf99c81382635ad287d3e` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/core/firmware.c` | `ee0c4a1260716796bffa336655870abe643e11f31a35b8d04c74e8b7f5204bc8` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/falcon/base.c` | `ca82bd574209c1f5c759b9e683e527ff207cd48287ad911e416c844043f66706` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/falcon/fw.c` | `a58616d3dca74703598b89c5911815203876bcd6dc1c4fdb0a579ed91625114c` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/falcon/gm200.c` | `48d93b34e737d97655249ecaaa607b4637d77b0e1a1643fe148da873a08cee81` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/falcon/gp102.c` | `cb167e631f63d042de874cf621010675719ebdb44176c97e5281ea42a2ef666d` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/falcon/tu102.c` | `95aa412aea719267c1a2f83e35e0742232d1268739270c7033ec3b6c3334fb0d` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/bar/gf100.c` | `f55718a9e8e53db6f566f3e117efbb348bcfe36b5b1732f2d3c08588c4ac8abf` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/bios/base.c` | `46ed30acc887a81f9f7f6e1e13ebaf1eb89f61d4e4f9461b3bf88eb65e722541` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/bios/image.c` | `52da790d1fe46c8a47cb933558b0991e334fe8dda8a303550e396fded0f5423d` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/bios/pmu.c` | `3f55b85a1e01cb1dc4e59b0d8d68bf6c9f2625046c5d251f283b3905ce41df41` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/bios/shadow.c` | `5677d72bb76103162f48bc0436a5ae7af443abeb4408dfb1619631cdf19804e6` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/bios/shadowramin.c` | `5993608771cf9fd77db71b2983b00b5ec1e69ef6e534005bd6b2cfea47d31ffc` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/bios/shadowrom.c` | `20d4fe363b323165a95914a42f2082d2abf5d24be778f92fd10e04430731bb3a` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/mmu/tu102.c` | `4fc1ff72d8b01784b3545d3d94f962557fca4f65f14418d1c10a42f6c46dccc1` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/mmu/uvmm.c` | `43c6a9fb52906ab67d0b582d4501152b74e6ed1cce6587d692ae9b066fd13e67` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/mmu/vmmgf100.c` | `8485236819c37bfcbfae9824c4b03b767314b6e07156c3ed2e203f2447ad89ea` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/mmu/vmmgp100.c` | `75ebc73311b04e7237151d68b812b76d5717e853b464be254a9f625d5f3f2967` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/mmu/vmmgv100.c` | `ddf0be77a946cf03c0cab7b9414aa65860f5aef4e6be36d65fac551651a3147d` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/mmu/vmmtu102.c` | `8aca422a4cec48cf1d9cf9cf5f39e475da7bad6f5cffb45ad71d4f729cf35d65` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/fb/base.c` | `07f5466f090a34a8080c7646121c8e010891edd9135f217084101df874a97c0c` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/fb/gp102.c` | `188b2b89134544fe538baa0722daacd5a5a92c23b997b91e100ec31a6e939a59` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/fb/r535.c` | `8d8c31fa93751a2950d1ea7136f04b05527b11864d1cc4ad38dd44e44f947723` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/fb/tu102.c` | `7853313980448891be82ce13c6dcb438e78f052dda64be69ae354944146ab161` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/base.c` | `211acfe67e349ae0f699c1263e0ef0a1ba9b1fef32a2f7c2c4b393b2c0636d38` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/fwsec.c` | `86cf199a6be0a1a48eedfbe079ee23526ccb66f2aec1de8f53b4ec52358504f2` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/priv.h` | `f812a149a208564adee5500712a41afda6bd47a446b6fb8237cb2c6705409cff` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/tu102.c` | `6ec714fdcd541bba6c471ddb8fd6082b6dbc8623bdb04c5d58f2ddbb74bca540` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/client.c` | `51fc056df3f2aa1e94f53cb1ad5cab6d2e577307f6332cefa264a40e0868f2e4` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/engine.c` | `accf12d2029a47b7ab3620146f8b00b8a9e47db8b911ccd4cc11adab57c76285` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/gpu.h` | `d9b548b73fa70da157ff33173531ad7d06b870d3f2fdeffb72682e6e72833011` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/gr.c` | `ae46c70a4c0f692f692447649e8f0a5d7b28ca68f9f061bd3edea7fc82e9ff1a` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/handles.h` | `f0f5564157cb0d44fd5c70f258f8bb3a4412b1dd735a5faa872c71dc7644cc86` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/rm.h` | `10a6e1cfe920f4e2b139ee5d919dcb6ae0eebe49884a08bc6c2ce78232fb0701` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/tu1xx.c` | `133b6b725c6395f75ad53b229e22146746c6294dfc18b3be942f1f61c6b25d16` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/alloc.c` | `df817dcd85bf4d8fa1c2c653bad55a28ca974bbfe62d3d8ec3cf56c362c4767f` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/bar.c` | `8c1733dd49e5b721522fa9377dacb5f6285b49463b35bb3bb704a955c5e5aa49` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/ce.c` | `addbad035a26265424cd6ce1bfb0faebd816d886fc38d7e598f54650e93b0b9d` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/client.c` | `b2a9e9e6454a59ec81411f05cdeea0d70965362bfe8f6d4bf676760c1df7ac12` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/ctrl.c` | `5dfb6d2bf7dea23f5789ef9f94b62bd0f760e9a3fa0189a9b287f203801efe13` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/device.c` | `857111d9d39419cfbd72646d01331aabbec46a9f700058593e24eb74d51c0e22` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/disp.c` | `d9d00ad4c7cd67408233a599746a879bd85c18b37b566553faac1f317cf8c0ba` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/fifo.c` | `2fb17d5491a0c026f3542d66a2f4f90f6aa65226db99039277233dc854e10d9c` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/gr.c` | `68872d9abdacc39754bbb878af03fcae81e744ce85c6babd88182ac8af5192f5` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/gsp.c` | `b782302ac0998ec75502a78e1b310876305d631ab993fd6caafbedfc9250ec68` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/rpc.c` | `6ab6fa460a1d9926b5f533237068cd34208750077442b02f73b753cf34ee2ddd` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/vmm.c` | `a394728b5e729f082c77988ab30675dd705677f77397559f204ee3ee67e4c1ad` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/nvrm/alloc.h` | `062ddea5ecd41309b68a4e123c18e2476b9251088650f3e81cec1ff0631ebb76` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/nvrm/bar.h` | `9596fb75665934c92fca0d08658f4db45f5b6e5ea8f3728cfa19576bfadb740e` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/nvrm/disp.h` | `4fb59fc1744a4d42b5e12f2fc2f6866d2582196ec79357bcca29048c3864c6a0` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/nvrm/fifo.h` | `9ed8b89522d88772d59a0da64da40e0285fefa62c02a00f36458cd3da08c1150` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/nvrm/gsp.h` | `5cd27171015fbfa5c0d23cb5fd9b9f72c6c15f30ee65cc3094ae770ef2b43b28` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/nvrm/msgfn.h` | `3b569a693416c6a8cf5dc5583e6c7619a00be012611b5af732b23a4871691137` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/nvrm/rpcfn.h` | `844ba52fb3dde1ac25887fa4dcf4cfebc0e7f6261e5df4c1c850462690a29b39` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/nvrm/vmm.h` | `b80036a21410ee48e86ef3ce0fa646fcb08f46d11167142135004e26037fd675` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r570/client.c` | `ab23dd6a5801609706bdaed7f0049a62176004fe9683ce8269642a3f8622ae92` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r570/disp.c` | `fb788f969c215feaa0086b17433e1976e1a9ce8bd8572d5d08ee52de85c30c6e` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r570/fbsr.c` | `729f92f5680be8febf74959b76e22eadf4513228fdc654e4d23a195f4d1228ed` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r570/fifo.c` | `78f774431d1d9436fab61ae37ebd407baedd5a90eb790577b87108a498691dfe` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r570/gr.c` | `76a52cf79df09b40997de3d107de38ef084d5c4993d69835eaf8384a75a7fb93` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r570/gsp.c` | `7c4945eaf5de419e7f3ae426cbafea77675233027ea58cf06e386f1de880cbc0` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r570/rm.c` | `7820633e527a66c21d5485149acc4166a8f6c1d5fa2a49f2caee8bc051fe4fc1` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r570/nvrm/disp.h` | `d91b148945c6f5d3873dadca022ce72add35e3b9812e83718318f167fa84a7e1` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r570/nvrm/fifo.h` | `8fd8404bcb00527b7f6268fc074d5cdd7d45275f0a85978399f78fd67835a7bf` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r570/nvrm/gsp.h` | `8e161a416c602db531025b6633473f05830fb6c1998f3270656dcb36125db7b7` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r570/nvrm/msgfn.h` | `e65988fb1dae4e17ed73b210c89dd2a91182e1a3fb87ab9fcbce7f1d15232569` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r570/nvrm/rpcfn.h` | `fa11dfa19d920673822ee9fd509e855b87a3f929238503e6d291532439d818cc` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/pci/base.c` | `a70e35a16d0ce6d60dc3d6e637f4d8a5e5b2b9ab1fa1bd689d043d09dc64fc2a` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/pci/gp100.c` | `40abc2e7c6f20fcbc23eedf6328ed98c977bc9261320dd06ccd5657b386be958` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/vfn/base.c` | `71c545f266ec902884f16868ee64e525e92cb3e3684a0a095930c0882784fe66` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/vfn/r535.c` | `0e9d638be153aca0aebb00333a8ce48cad7e0fecdc87809ecf3b0b9e66ec11c8` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/vfn/tu102.c` | `5299a6b1c4bc89b4d5bf43802e8a94645fbb85a2411eebcd38b06586ca77f861` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/devinit/base.c` | `5b3532b800941b5d200bc912d40a65f78d28f965ec0850d233df17ad309d97c6` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/devinit/r535.c` | `0cfbe5d4ff12eb7955fa84f302cb2f1584157f91b09b6bb2ad6b899a8d9f858f` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/devinit/tu102.c` | `4615b4adcfd9d662f362e2fefc14ebc7f06c7719a6110c6d4a42c1a19117993c` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/engine/device/base.c` | `9b7dd794ddcb78536336a7252dc6cdebf60e6588df964735c8d96320ed8c9dcf` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/engine/device/pci.c` | `614c54c068c04af104080d57914bd2f703e34114fcb3182b4e94fbfb3cef7e66` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/engine/sec2/tu102.c` | `c9f42fde8bf7a961c98b5b327f8f287056c2d34af21a5d96fc5333e07f529149` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/engine/disp/gv100.c` | `7ba533edfeca4453f29876fe34579a4db69b8d9b744d26f26edec25a21d65b3f` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/engine/disp/tu102.c` | `40f1682301caeb41b67f299031f1703107826dcdadc5c9ec06f271413267cff5` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/engine/dma/usergv100.c` | `c8edbc9e2c9f52d435b52a3abdbf202a02e0e90dd7584641b99fbe2820e12539` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/engine/fifo/chan.c` | `912a0f91c1297c6688c09910abb9c97f8fe4f8dd890ae5a1980981b863e853b4` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/engine/fifo/ga100.c` | `b505d7e10d584626d6374d603058d782e5dec7b4128d66f6cf6a3b670c30d300` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/engine/fifo/gf100.c` | `0d464ff702ee8121e401e2c27a3120ca4ef7470703fd29fa2b3b16fa3c9116c0` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/engine/fifo/gv100.c` | `0e36822f980a8a8a5c37a56ce8442f5d61a69cbc6af3ad29e5b94b3050deaf8f` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/engine/fifo/tu102.c` | `5b512476da29c20d89ee1d3506bbdc50c31bf34a03166c2e5b31352f144d491b` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/engine/fifo/uchan.c` | `8108d7efc60c8fad8b733704f2e4dedce750c38ad6dff4183cdd07979773657d` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/engine/gr/ga102.c` | `6d4feaf42395495f9ded6d9a4b47424f4e108d285f7bd97cddbb836b6974c537` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/engine/gr/tu102.c` | `fbc51944c09550e75d85a3ef134d04d918615bcdcf3525cdffb690f1f0ce7f9e` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/include/nvif/class.h` | `accf5976f71d17943dca4b633b5bf5bd3e60bdc5f35fb202c8ea9854d95478d1` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/include/nvif/if0020.h` | `bd52119521bf1ec03d1ca73efdf3391267aa630e91b28bc8b6d4544345d391f1` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/include/nvif/push906f.h` | `45e760a06b5835f7ca9621096c2c82b345d7d583cf7879ba123963c2f6ad4710` | 表記なし（kernel の既定の GPL-2.0、COPYING） | 読むだけ（作業の文書は temp） |
| `drivers/gpu/drm/nouveau/include/nvkm/core/layout.h` | `29ba23dec58528d454038c5aa167d5ee3557b87e83ecaf554443ef137725fb89` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/include/nvkm/subdev/gsp.h` | `3cccc225a53518cb86899d95efb71b2e495fb77f6f56f416a17d77bde0499905` | 表記なし（kernel の既定の GPL-2.0、COPYING） | 読むだけ（作業の文書は temp） |
| `drivers/gpu/drm/nouveau/include/nvkm/engine/falcon.h` | `f6bc13c54ad465439f6a52fc9960f2aa65a3a6cd24e772e1c612080cb4c1e4bd` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/include/nvfw/fw.h` | `29524e3b1586a674924d5019a2f694b5eb47737032e75c91b4ba2c443cf5e27a` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/include/nvfw/hs.h` | `2f84c0ecaa6223f66a95b815a6595d55d6b9b601d98abc5dae6192dd148c38d2` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/include/nvfw/flcn.h` | `d3c894670485f624a0efed951e2fe16546c976158b034ada566dcf33eb0273fb` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/nvrm/ctrl.h` | `adeb3d9bb879117d9723208740c4238f413b77737d4c31963151fd71ef612850` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/nvrm/client.h` | `edbbe07e5db61a43e7626b619eebdeceb9aba294dd96319a54dd17a1d745c7e3` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/nvrm/device.h` | `8dbaa91693678d30aa9cddead8405e322aa5e39865b734d11330115fbd200b83` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/ad10x.c` | `928d56a37d77fde25dbee17e4a6b9322ca643db68a258b81d2bcf09e2eb3c8c9` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/ga1xx.c` | `6b0346f745d86233021ce0c97cc1f9a6b3140a281df838f529492917a1386049` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/gb10x.c` | `c15214a74bef25258a6aa14e71c3a2ca1e7ddc78b08ca520ee0d66d2f269bdb6` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/gb20x.c` | `a60d597e802bca7b4a916d9ee6c7a18865a4708438f0a1ae4845e0b0a6d4427d` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/ga102.c` | `b5cc57963d9e73c0031c1f6ae97481d2a2b3e3cdc4dfd66d0c2deb862db4ebdd` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/gh100.c` | `31b6e4551bb5963bf6e85cd006fa6f78ae8a60d13660dc1942b5337e423ce222` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/gb202.c` | `fe5e20bd699c103eeba47d55e21c5218b9fc3f435e8bab996edf296e12f3cc18` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/acr/tu102.c` | `997deb9c688bd843b7ef200e459e94ed44fd8dfa0648730696fee83f8394c921` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nvkm/subdev/fb/gf100.c` | `af6325c3c341149580905dd017ef1916cc8adbd8eaaac5234d3d3b64523f85c9` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/nv84_fence.c` | `7550d90eea1822761e6f63b77473b1237940c7baffbfc1218d04170657bf0974` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/dispnv50/base507c.c` | `cf1f300628913f2f6d35fdce4fef059fb7add65ca18579d6507d0a126561bb06` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/dispnv50/corec37d.c` | `e447a0582ef4a1fee45fc788cfb582ef3b541f4efe228af60f543ab86eb6f66d` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/dispnv50/corec57d.c` | `ddc2b2d4488e1b7c4145bdc4983b97bc45e52d9d27a6e14680a56c9c7977cff8` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/dispnv50/disp.c` | `c8466c003e9bdbfb974a5935640d841c2c7644f08efc018fb7dc642b24dfdbd1` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/dispnv50/disp.h` | `65a49cdf22a9e1350f3b81742d3e3af789dfba004130908bd0b9c363fa175efc` | 表記なし（kernel の既定の GPL-2.0、COPYING） | 読むだけ（作業の文書は temp） |
| `drivers/gpu/drm/nouveau/dispnv50/headc57d.c` | `f076a3b8405a57b4f584c797ee7f5146a16074ee3ed2fc5bb6ccbc53ca22415b` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/dispnv50/wndw.c` | `342ce2b1e986451165a358ac76c03330712a468ac87ccb74f60c23169d214d39` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/dispnv50/wndwc37e.c` | `333bb4ba1d41c6005c292138d73c7368169b0e908d072897cd1699ad91c1ac4f` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `drivers/gpu/drm/nouveau/dispnv50/wndwc57e.c` | `498f293150e76bc609826eeac37bb9a0dd9d33b541c6bf9884e42e196bb6ff92` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |

| path（NVIDIA open-gpu-kernel-modules 570.144、commit `8ec351aeb96a93a4bb69ccc12a542bf8a8df2b6f`） | SHA-256 | license | 扱い |
| --- | --- | --- | --- |
| `src/nvidia/inc/kernel/gpu/gsp/gsp_init_args.h` | `6d1117eaf4f4ca805715a95bd7edd46f1f87160b78b4e440936cae499d13631c` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nvidia/inc/kernel/gpu/gsp/message_queue_priv.h` | `cbb3aa6001524c75ba3f570dc0e92523f042a293aa6e3779ac00162c100a2980` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nvidia/generated/g_rpc-message-header.h` | `c205def1ec5ece91320e6d4c368bbb010168e8937d91352085648afc2a554b1f` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nvidia/arch/nvalloc/common/inc/rmgspseq.h` | `553778eea5e9a71f63c1b012e064f5ade016e3bfa498fd657a1a41fbf23c0c1e` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/common/sdk/nvidia/inc/ctrl/ctrla06f/ctrla06fgpfifo.h` | `daf6ad74ec4cfcc9d2770b6b930e1c325ab4c1b26ac70dc7e44da87e3822fe22` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/common/sdk/nvidia/inc/ctrl/ctrl0080/ctrl0080dma.h` | `b0d085b358a4c06c0e12cecfdace5b1eb764d5aee01c1229e03f1926d32fc184` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080fifo.h` | `207445c7b731392b5cb38b578b5348879a090f519c05fe02eb8e9d992779c2dc` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080gpu.h` | `5e7b247d68713323f1110c61691d11bfe8bd1f31bbf67302fb7f98b3ef47fca1` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/common/inc/swref/published/nv_ref.h` | `b005b5b81742069cef688b199a753ced36517e1745d0a6a91a8c0485315576a5` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/common/inc/swref/published/turing/tu102/dev_bus.h` | `58d106d908a1b56a99b10075744067b15be95d0973ae4b4c9a50e914843cdd44` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/common/inc/swref/published/turing/tu102/dev_falcon_v4.h` | `0f7e56e311895fc92788ca8acf5bd587513ee6f71b8aed7f6ab5e2d1990d4034` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/common/inc/swref/published/turing/tu102/dev_fb.h` | `124bc4f386ffc12afa07b874a7daca0e8b977a7e818d9baf45c151e3f5fcddbd` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/common/inc/swref/published/turing/tu102/dev_gc6_island.h` | `2a2b677175b6cb8936f4c9d5deb8b677a19ba74ec274c43d140140e620186e2d` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/common/inc/swref/published/turing/tu102/dev_gsp.h` | `55f4af9040fca1645324712c1a1e4f00b9209ee98b112d2a0449b7cf48b5c3e8` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/common/inc/swref/published/turing/tu102/dev_nv_xve.h` | `4421613bea6c68c86555d40d5ff208af861bde4da48873218c3caffcdbb638ad` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/common/inc/swref/published/turing/tu102/dev_riscv_pri.h` | `ad6cf06acbcf8859b5ba586c8cd3b090931a9b33491887c9750529518f68262d` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/common/sdk/nvidia/inc/ctrl/ctrl90f1.h` | `ed30642e8648456affe5923c9c6284d7c61964b56bab67ccb0d7f4dc595967a9` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/common/sdk/nvidia/inc/ctrl/ctrlc36f.h` | `c67efacd419f3ac55ae2a94c2c8b611e9973ef300dd6e1bd762d0dcc7992b896` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/common/inc/swref/published/turing/tu102/dev_gc6_island_addendum.h` | `192415f7fc931441531dda6705700480b6e0715ed25ab07ed86a04a0588183f5` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `README.md` | `8fcec472d54b8f4839e26b5ecd025cfc4ed6388a77877bddc97d686fc10670bc` | 表記なし（`COPYING` の既定の MIT） | device ID の一覧の事実 |
| `COPYING` | `d8e96d29bc323000e7ba56102d77c88226a7a3d0d94e2081cb4b071ae9de4a06` | （license の文書: MIT、Linux の module として link した物は MIT/GPLv2 の dual） | 判定の根拠 |

| path（NVIDIA open-gpu-doc、commit `9fdf5c4062007929d9f4e6cbad9c9771fe61b880`） | SHA-256 | license | 扱い |
| --- | --- | --- | --- |
| `Falcon-Security/Falcon-Security.html` | `fc5341852fb707d19b759f97fc9527b1b4fda42aa6cc8291d0531e8f002b31b1` | 本文は MIT（repository の `LICENSE.md`）、532〜537 行の埋め込みの JavaScript は GPL | 本文の事実だけ（script は取り込まない） |
| `manuals/turing/tu104/dev_display_withoffset.ref.txt` | `932e5195b2cf9fcc49cffd88c396b2e87db6643b8d29c28269fa772127ecfe42` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `manuals/turing/tu104/dev_mmu.ref.txt` | `ec62970d7dd90df86e9477a34641ee26e1072e3c58812e42f0ea14643844da8e` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `manuals/turing/tu104/dev_pbdma.ref.txt` | `20435e77be160372f8c8d7c32ff3e7c42ca9d65c7a13743d372ba63f350ac597` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `manuals/turing/tu104/dev_ram.ref.txt` | `9f64baa92a36af2c8344ef1737086a902b19fd0e2b956340cd33362790cbc1fe` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `manuals/turing/tu104/dev_usermode.ref.txt` | `a5064d313f6a832fe71f2fab03e22f79abea61ba0096df937eafb463a03d6576` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `manuals/turing/tu104/dev_vm.ref.txt` | `db2244d1fc9d4a3f61f57db816c25f7032479d561676b7b2659d7c0b54da83f9` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `manuals/turing/tu104/pri_mmu_hub.ref.txt` | `3c0c31c7306f6416ae3a428cabc4baf2d1257b625b2974629b64db72229fc84a` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `classes/host/clc46f.h` | `cc7da374f17926ca4a03cc6a8b5e3f94c975226d541049beac791bea86a6a38a` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `classes/display/clc57a.h` | `640407ae4f398f550a934dd5bfc848c56b9105e0b3a2a05035cfbbe6d19c2242` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `classes/display/clc57b.h` | `701db3c4c10b8db033feb5bd6a7dde55eea74d6d48cd1bead9e2a15383e8c31a` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `classes/display/clc57d.h` | `9b551f3ea03e500df96e16a4ea59076aaf7e4daf0387776aa7d0708513e0f12f` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `classes/display/clc57e.h` | `41c626719938aa959d8cfd5f0fb21b310017432fd4a512a697fa87729bc6321c` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `Shader-Program-Header/Shader-Program-Header.html` | `07bd44bd7623823070ded0254dceaf95cc182deef833a7bc2cdf691e6cd0a48c` | 本文は MIT（repository の `LICENSE.md`）、553〜558 行の埋め込みの JavaScript は GPL | 本文の事実だけ（script は取り込まない） |

| path（Mesa 25.3.6、commit `06f9e28304d5d3f109c33535c1c25b9df5769af2`） | SHA-256 | license | 扱い |
| --- | --- | --- | --- |
| `src/nouveau/vulkan/nvk_cmd_buffer.c` | `02564d3637cadcce126e9441820498d3113b2487a5c7e30a8fa6f684b72aeae6` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/vulkan/nvk_cmd_copy.c` | `334672d68ffd1bf68364d3fce655fecd859424ccfeeeae615ba273ce463342b5` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/vulkan/nvk_cmd_dispatch.c` | `833d88ad7fec1699673a794689ebba19a0365f7bc3271fa586c2eb05a9f353a2` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/vulkan/nvk_cmd_draw.c` | `1efce3602df6caa20ddc9735847f6e184e1d6dc698a052655943c83bbd63f927` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/vulkan/nvk_cmd_indirect.c` | `e6abd84799fda2227e5eb17ac3323e2924d8e40c17335d6b5893d5b93861c564` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/vulkan/nvk_descriptor_types.h` | `77039b415779eb8ef5a8f07531e540e53e834261326198797d2a64e9be1519cc` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/vulkan/nvk_device.c` | `e78f74b947a5b08b5cb75a1425db9b7b2f4ec73a507e73cc37e55e527b192f09` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/vulkan/nvk_image.c` | `362958810a316b938a9e53e34936d2cbc5ebca20065feaa91b9718c100542327` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/vulkan/nvk_nir_lower_descriptors.c` | `79e25a2385e9b6dc3ca530f6c1d4aa9d7b41829fd94a483a73a9f81e79df091b` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/vulkan/nvk_physical_device.c` | `96a102b9277c3adecc721a29ca2ed6d8c60d95e76188350c11e1c726b6571245` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/vulkan/nvk_query_pool.c` | `6b562244186ff412e0ed9898549d86ba01a16544834200ce936c632184d5b62c` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/vulkan/nvk_queue.c` | `75466632f37af088199233acd36ad7db700d5cfd002fac834d1dd3b48845b5cf` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/vulkan/nvk_queue.h` | `d0a04e521aa608febb1e598de69fbc69ba52e736998bbeb3e8f45cb56ca75d0c` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/vulkan/nvk_shader.c` | `57d46463518031ec13315dd705ad2f1d7079857900d3df5b7c5c30b9db255ba0` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/vulkan/nvk_shader.h` | `526a1b4bb7e65f34c9dc1b7e3b6c2e8c30f3798751bc96a83e9f30e804d1eddc` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/vulkan/nvkmd/nouveau/nvkmd_nouveau.h` | `18a929d4be3b1583e9ff431cc325b2936d98e933dca44b6a7a6e9ea1725ecb81` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/vulkan/nvkmd/nouveau/nvkmd_nouveau_ctx.c` | `f73aa1d94f6a685810d5949447ed08d0f747c74946b532357f1c8506c7dfd652` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/vulkan/nvkmd/nouveau/nvkmd_nouveau_pdev.c` | `f0762bdd6d96b385d43c4667b2e80d4fd6dc07d2ecb4c3d747726c88a888687a` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/vulkan/nvkmd/nouveau/nvkmd_nouveau_va.c` | `88ff93b6236795ec19915ad39afaa19e46ef6ba13a736e7a20ad924c33fc1fe4` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/headers/nv_device_info.h` | `d85062da28fca879a92e9c43b89feafa3ab0bb39a00f942bf19ca89ebcb127ff` | 表記なし（Mesa の `docs/license.rst` は大部分 MIT・file ごとに SPDX を見よ） | 読むだけ（取り込むなら判断の項目 2） |
| `src/nouveau/headers/nv_push.c` | `5b47253f24e6115a9d90e2732a5c09e43b54689b0523130760dda5a0b546328f` | 表記なし（Mesa の `docs/license.rst` は大部分 MIT・file ごとに SPDX を見よ） | 読むだけ（取り込むなら判断の項目 2） |
| `src/nouveau/headers/nv_push.h` | `a5b77405cb071505abbd0fb200ff9ad1ed0d1ad5dee319504242ffa7df9cb808` | 表記なし（Mesa の `docs/license.rst` は大部分 MIT・file ごとに SPDX を見よ） | 読むだけ（取り込むなら判断の項目 2） |
| `src/nouveau/headers/nv_push_dump.c` | `dece6040f0d589b877f45b8cb041690ea68bbc4a731fc31d03273935f2ce34a3` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/headers/nvidia/g_nv_name_released.h` | `f1122d6b533af333a2f751d1501732ad350d259259db33d2cf0328b66f0f3ef5` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/headers/nvidia/classes/cla097sph.h` | `a804dc66816ec4bd5a49be6c6745f7aa2cda31881e48472625abe4a691f0c1a3` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/headers/nvidia/classes/clc3c0qmd.h` | `761286640c931b8cea41e414dc8be5170bc60c50294b2b7b46ec1ed6c3db482b` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/headers/nvidia/classes/clc797sph.h` | `06133cfa73ea27ab72129a804b054f2bad9b9706a1e679a43e8fd46c870e090b` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/drm/nvif/class.h` | `8486732ad1988054b5f2415970111bc19e4ebc8cf4b059288d462699e39e7a2a` | 表記なし（Mesa の `docs/license.rst` は大部分 MIT・file ごとに SPDX を見よ） | 読むだけ（取り込むなら判断の項目 2） |
| `src/nouveau/nil/descriptor.rs` | `b46b470d2360c8351d52057a414b96ec126c830a1c5fad9f26d7b4f0778a369e` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/nil/image.rs` | `9d2b8d31edd1c33644ad80de96871a9a9bac8cee5d93846dce9fd98d8659e984` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/nil/tiling.rs` | `016d1d356f1bfd66cc5321e616c751465788157969a24e8f64e8c2f31d62d994` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak_nir.c` | `98c5c2e61b6203681d10ddfa80b46ad48c321d50fc1c0e313da282675b694a25` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak_nir_lower_cmat.c` | `2f6329ad6ab87fbb1f89a7182824491520372ec217fa85a68abae543fcedbbe8` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak_nir_lower_non_uniform_ldcx.c` | `e30feb433f247eb1965cf16489a2bad4d3cf361f402fdec2b4155ab605ee1d5e` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak_nir_lower_scan_reduce.c` | `08d4e846fd1306911002956e07adcbcc9d829907a81489ad2e1817a86c846e6b` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak_nir_lower_tex.c` | `801e5f42ad67cac466462da08c23be8a786c44dad33619e51819a5589b9cb4df` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak/api.rs` | `4cce91f930b6a9a9e23cbd30e3953a6c57c38f3bae07d61a971fbc8828d5f6a5` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak/from_nir.rs` | `734f35b552f4225b885aef708863ad016ee034bd7289788ab92d63b607fff11a` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak/ir.rs` | `bb34c0499527f92e312d01ce5e2adc2c2774772ab502b9bd59f7696c9866b09e` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak/lower_copy_swap.rs` | `d55ab31c1da9e6e6dfffece0229f625e483555b3d2b0f430831ee9ca95fcd8b0` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak/opt_copy_prop.rs` | `b88c80890b36607a0b4e25c6c06011a225dc8f395c8891928f89d6195bf8203c` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak/opt_uniform_instrs.rs` | `556be9bff1e10698e8ac83bb3b97f64071a624f2c57de1014df30943b01c5b5a` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak/qmd.rs` | `73f4daf73c8af8ec5aeb5d93b936aaaa454d1ea6f5bb06edf923028fe2996698` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak/sm120_instr_latencies.rs` | `3f25d85efffd6140a62ed54d0c44884da13409a664d3e1c989c1c9c3b641c60c` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak/sm20.rs` | `fe86777534ea8c7519c7bc9d00d709d9ba9673d541d2524f6abe1e6fc04449a5` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak/sm30_instr_latencies.rs` | `16d41aa25fe189979b487018c39d904e8aab34fb9ca43bde020948e11627ddc1` | 表記なし（Mesa の `docs/license.rst` は大部分 MIT・file ごとに SPDX を見よ） | 読むだけ（取り込むなら判断の項目 2） |
| `src/nouveau/compiler/nak/sm32.rs` | `061931824689373a96a647ceeff097a5ec57eae33b398a6f7a43d28920a8c3d7` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak/sm50.rs` | `87aff966ec2fb62dff67dded8181d62a0a5b9863dc9a67084be4ef24e4caee30` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak/sm70.rs` | `5048695512c9ace320d9a38b0b6455e11d3fadc0f8ce24a80d1eb4d5f712de27` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak/sm70_encode.rs` | `111b841c9661f46729b52072ef070b72fc1bbbe5b304651c43aaf40e297b6695` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak/sm75_instr_latencies.rs` | `419b8b1d25fb16ddc8ae726768b15e593d3b84f467cc6bb59101eb39bf764878` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak/sm80_instr_latencies.rs` | `3aeeb5b8a91c5d34323eb2fa18eb1848325ab90c58d409425fce3e15ef049797` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/compiler/nak/sph.rs` | `e845a822a7a82822e0c07fd84cacce413146debeeaeb7484dc48deffc492c368` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/drm-shim/nouveau_noop.c` | `aea598665413a8ae6f9efabe156d9cdb63b38d5b3e36387260fbd22e0839a410` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/winsys/nouveau_bo.c` | `eeacced920056faf3ab4d47bd267527f9f0118af165480abb75e243dcfee09fe` | 表記なし（Mesa の `docs/license.rst` は大部分 MIT・file ごとに SPDX を見よ） | 読むだけ（取り込むなら判断の項目 2） |
| `src/nouveau/winsys/nouveau_context.c` | `9488371cd28a828666949558a9260153e63f065f791aaaa505aa9de7fcbbf21f` | 表記なし（Mesa の `docs/license.rst` は大部分 MIT・file ごとに SPDX を見よ） | 読むだけ（取り込むなら判断の項目 2） |
| `src/nouveau/winsys/nouveau_device.c` | `d5d5b61761e51b04cedc84690530ce91f2d1550cbe0c34637084d3a07f9ecc0d` | 表記なし（Mesa の `docs/license.rst` は大部分 MIT・file ごとに SPDX を見よ） | 読むだけ（取り込むなら判断の項目 2） |
| `src/nouveau/winsys/nouveau_device.h` | `7f160c7dcdc866609a1f746451006f9e4c68bdaf54e71e5646379124b2ebdb49` | 表記なし（Mesa の `docs/license.rst` は大部分 MIT・file ごとに SPDX を見よ） | 読むだけ（取り込むなら判断の項目 2） |
| `src/nouveau/mme/mme_builder.h` | `e33ed972e416e2192add46959ef83d5dcdefb72f32941c7a2b78cd0ba03263b0` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `src/nouveau/mme/mme_sim.c` | `7b22fac9b4bb22d04ec92fc8d2562d23702c0993ed9bfe5d15135995088f251b` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `include/drm-uapi/nouveau_drm.h` | `2758ed28465a923c5a0eb1c2d2a4af7b43a7fc52e98b38723924a3fae48ccc39` | MIT | 定義・手順の取り込みの候補（名前は一括で独自に改名、MIT の表示を残す） |
| `docs/license.rst` | `0d1a0472ecc81830e75c20d59b0ea02841e3db21255e0ebad97ab682c54d6615` | （license の説明の文書） | Mesa の既定の license の根拠 |

| path（linux-firmware、commit `d947e4e8e314e9254a1242dc1a5d9cede2cce33d`、tag `20260410` でも同じ hash。TU106 の名前 `nvidia/tu106/gsp`・`acr` は `WHENCE` の link で tu102 を指す） | SHA-256 | 大きさ（byte） | license | 扱い |
| --- | --- | --- | --- | --- |
| `nvidia/tu102/gsp/gsp-570.144.bin` | `3052aee2872182a14d8d7c069e3a14fe4642405894b24692c4aca4101dfb1809` | 28,542,040 | LicenseRef-LICENCE.nvidia（再配布可） | GSP-RM の本体（ELF）。既定 off の firmware の package へ（BLOB、`/lib/firmware/nvidia/tu102/` に install） |
| `nvidia/tu102/gsp/bootloader-570.144.bin` | `12e987b636c2f00fa40f42fd95097515c0817b158119c584049a37faf38f8f96` | 4,196 | LicenseRef-LICENCE.nvidia（再配布可） | GSP の RISC-V の bootloader。既定 off の firmware の package へ（BLOB、`/lib/firmware/nvidia/tu102/` に install） |
| `nvidia/tu102/gsp/booter_load-570.144.bin` | `7bb181f544a942299bbf4642da6d9bb58bfed8459725094e2ace56647cd3ec8c` | 59,272 | LicenseRef-LICENCE.nvidia（再配布可） | SEC2 で走らせる起動の ucode。既定 off の firmware の package へ（BLOB、`/lib/firmware/nvidia/tu102/` に install） |
| `nvidia/tu102/gsp/booter_unload-570.144.bin` | `5c7f1aa37d045cc20bb81584a830e8dfa4483f1cd63943de83a58c4be19a1782` | 39,304 | LicenseRef-LICENCE.nvidia（再配布可） | SEC2 で走らせる停止の ucode。既定 off の firmware の package へ（BLOB、`/lib/firmware/nvidia/tu102/` に install） |
| `nvidia/tu102/acr/bl.bin` | `01326cc997716bebb0eab9fe1f463fffa0ee586079e4ca0e5bb096ab6fcfedab` | 1,280 | LicenseRef-LICENCE.nvidia（再配布可） | VBIOS の FWSEC を GSP の falcon に載せる bootloader。既定 off の firmware の package へ（BLOB、`/lib/firmware/nvidia/tu102/` に install） |
| `LICENSES/LICENCE.nvidia`（tag `20260410` では repository の直下の `LICENCE.nvidia`、同じ hash） | `bc5225a57f49c5249dcf238e4ae6437811677a2a8a7f579c3d839e058653ee44` | — | （license の文書） | firmware の package に添える license の写し（BLOB ではない） |
