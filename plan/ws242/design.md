<!-- awesome-plan project=zedbsd record=ws242-design -->

# WS242 設計: 残りの段（RTX 30/40 と GTX 16 を既定で起動する所まで）

Parent: [WS242](ws.md)。作成 2026-10-29 P5（q1666、設計だけ）。照合した tree: main 61630bb0b。
前の記録: [p001](phase001/phase.md)（driver、2026-10-15 の WIP は main に入っている）、[p002](phase002/phase.md)（compiler、SM86・SM89 の encode と latency は main に入っている）。
公開の source の調べ: P5 の配下の調べ（Fable、2026-10-29）。Linux v6.19 nouveau（file ごとに MIT）、open-gpu-kernel-modules 570.144（以下 ogkm、MIT）、open-gpu-doc（MIT）、Mesa 25.3.6（src/nouveau、MIT）、linux-firmware の WHENCE、pci.ids（2026-10-29、sha256 41f814a4…afe87、BSD-3-Clause と GPL-2+ の dual と header にある、要確かめ）。値と手順だけを取り、code は写さない（guardrail「MIT・GPL の出典のデータの引用」、実装では block ごとに「Value(s) referenced:」「Procedure referenced:」）。

## 第 2 版の改訂（2026-10-29 P5、[design-review.md](design-review.md) の blocker 1・major 7・minor 9 を反映。この節が下の本文に優先する）

| # | 改訂 | review |
| --- | --- | --- |
| V1 | **§3 の「失敗の段と戻り方」の表は今の code と違う**: `gsp_teardown()`（`gsp.c:1112-1170`）は GSP が走って `gsp_stop()` で止まった後にしか WPR2 を壊さない。FWSEC の timeout（`:1032`）・booter_load の timeout（`:1298`）・INIT_DONE が来ない（`:1372`）はどれも `FAILED_HOLD`（teardown も console の復帰もせず、DMA の memory を保ち、detach は EBUSY、復帰は power cycle）。FRTS・booter の失敗では region が残り「reboot the host」の印だけ。→ 「段ごとに諦めて firmware の画面に戻す道」は無い。作るには bus master を切るか falcon を reset するかの DMA の順を決める段が要る（新しい p005b、6〜8 LW の見込み、`FAILED_HOLD` の各段の安全な戻し方を実装の最初に調べる） | B1、m9 |
| V2 | **Q242-1 の回答の前提が変わった**: 2026-10-29 のユーザーの回答「既定で起動 (推し)」は、§3 の表（諦めの道がある）を前提にした問いへの答え。実際は失敗すると power cycle まで画面と GPU が戻らず、`FAILED_HOLD` と WPR2 の残りで次の warm reboot でも nvrtx は諦める。製品 288 のうち laptop が 115 で、nouveau は GSP に ACPI の data を送る（`r535_gsp_acpi_info`）が nvrtx は送らない（laptop で init が通るかは推測）。→ 事実の報告として聞き直す（Q242-3）。決定を変える提案ではない | M7 |
| V3 | **Q242-2 の回答の前提も変わった**: WS305 は止めたまま（master.md）。passthrough の GPU の画面は QMP の screendump に写らない（AGENTS.md §9 の判定の方法に合わない）。nvrtx の失敗の多くは cold reset が要る。→ 聞き直す（Q242-4）。WS250 の Q250-2 も同じ | M6 |
| V4 | compute の launch の `SET_OBJECT` が 0xc5c0 の固定（`compute-launch.c:239`）。object は `profile->class_compute` で作る（`compute.c:688`）。§2.2 の版ごとの関数に class も渡す。`compute-launch.c` に profile（`nvrtx-private.h`）を持ち込まず、版・class・PCAS2_B の要否の小さな struct を渡す（層の境界） | M1、m4 |
| V5 | **p005（既定で起動）は p004 の後**（vk は既定で on なので、p004 より前だと RTX 30/40 が SM75 の code と c597 の push で起動する）。既定を on にする 1 行は最後の commit | M2 |
| V6 | 試験に足す: 起動の判断（family × firmware の画面 × `nvrtx.gsp`）を純粋な関数に出した host 試験、QMD の bit の期待値を header から機械的に抜く（自己参照を避ける）、Turing の出力が byte で同じ（shared memory が 0 でない入力を含む）、戻す gsp-host-test の既定の firmware の path を `-tu-ga-ad` の cache に（今は古い `dc85cc…` で GA/AD/TU116 の実 file の項が黙って飛ぶ）、`plan/tools/nvrtx/nvrtx-vk-host-test.c` に owner の口の stub、段ごとの失敗（RTX 2070 で `nvrtx.stop=` を段ごと） | M3、M4、M5 |
| V7 | 戻す host 試験 3 本は `plan/ws242/tests/` でなく `plan/tools/nvrtx/` に置き `plan/tools.md` に登録（WS の完了でまた消えないように。登録は Q1） | M5 |
| V8 | minor: §2.1 の 256 MiB は ogkm の「Firmware scrubs the last 256mb」で、量は `fbSize − gspFwRsvdStart`（外の heap の始まり）、失敗の印は「VRAM too small」（`gsp.c:709`）と分ける、ELF は 60.6 MiB。vk-execute.c は `execute_compile`・`execute_start` に引数が要り「呼び出しの行だけ」ではない（WS291 との重なりが広い）。HBLANK_DELAY（0x2364）は c57d に無いので class で分け、armed の値を写す。`boot_default` は常に 1 なので欄を作らず verified の判断を外すだけにする（identify.c の位置の初期化子を崩さない）。p001 は「済み」でなく uncleared で閉じる。carve-out・warps の欄は今使われていない。WS250 の R10 の危険は低い（nouveau も GA/AD で同じ doorbell の形） | m1〜m8 |

### 第 2 版の Phase の表（§5 を置き換える）

| Phase | 内容 | 依存 | LW |
| --- | --- | --- | --- |
| p003 | host 試験の復元（`plan/tools/nvrtx/`）と追従、identify・profile・products・§2.1・起動の判断の項 | — | 7 |
| p004 | SM を渡す、QMD 3.0・class・PCAS2_B（V4）、HBLANK_DELAY、QMD と Turing の byte の試験 | p003 | 9 |
| p005a | §2.1 の検査、firmware の package の確かめ | p003 | 2 |
| p005b | 段ごとに諦めて firmware の画面に戻す道（V1）、段ごとの失敗の試験 | p004 | 7 |
| p005c | 既定で起動（Q242-3 の答えしだい、最後の 1 行） | p004、p005b | 1 |
| p006 | RTX 2070 の実機の回帰（Q242-4） | p004・p005 | 3 |
| p007 | 規約の全文と出典の監査 | 全部 | 3 |

計 32 LW（前 24）。

2026-10-29 P2（q1678-i01）: Phase の ID を wsNNN-pNNN の形に振り直した: p003・p004 はそのまま、p005a → **p005**、p005b → **p006**、p005c → **p007**、p006（RTX 2070）→ **p008**、p007（規約）→ **p009**。下の本文の旧番号はこの対応で読む。

### 第 2 版のユーザーに聞き直す点（Q1 へ）

- Q242-3（Q242-1 の聞き直し、事実の報告）: 今の nvrtx は GSP の起動に途中で失敗すると画面と GPU が power cycle まで戻らない（`FAILED_HOLD`）。推し「p005b（諦めて firmware の画面に戻す道、7 LW）を作ってから既定で起動」。選択肢: (b) 今のまま既定で起動（失敗した機械は `nvrtx.gsp=0` を boot の行に足して再起動）、(c) desktop の製品だけ既定で起動（laptop 115 個は `nvrtx.gsp=1`）、(d) 諦めの道ができるまで `nvrtx.gsp=1` のまま。
- Q242-4（Q242-2・Q250-2 の聞き直し）: WS305 は止めたまま、passthrough の GPU の画面は QMP の screendump に写らず、nvrtx の失敗の多くは cold reset が要る。推し「RDNA と差し替えて素の起動（SwitchBot で電源）、判定は ssh と boot-test.sh」。選択肢: WS305 を再開して VFIO（画面の判定は別の方法が要る）。

## 0. 要点

1. **p001 の再開点 1〜3 は今も未着手**: driver の 4 か所は今も `nvrtx_sm75_compile_ir`／`_stage`（SM の引数なし）を呼び（`draw.c:443`、`compute.c:736`、`vk/vk-pipeline.c:676`、`vk/vk-execute.c:412`）、QMD は 2.2 だけ（`compute.c:811` の `nvrtx_qmd_build`、`:820` の `nvrtx_push_compute_launch`）、launch は `SEND_SIGNALING_PCAS_B` 固定（`compute-launch.c:261`）、draw の init は Turing の class の wrapper（`draw.c:568`、`vk/vk-execute.c:453` の `nvrtx_push_draw_init`）。→ RTX 30/40 は起動しても SM75 の code と Turing の 3D class を使う（正しくない）。
2. **host 試験が消えている**: p001 が追従させる予定だった `nvrtx-gsp-host-test.{c,sh}`・`nvrtx-identify-host-test.{c,sh}`・`nvrtx-vk-d3-host-test.{c,sh}` は commit 41a28534b（2026-10-28、WS037 の片付け）で `plan/ws037/tests/` から消えた。git の `41a28534b^` から `plan/ws242/tests/` へ戻して追従させる（新しい Phase p003 の最初）。WS242 の phase.md から参照されていたので、AGENTS.md §9「参照されていれば消さない」に当たる消し方だった（Q1 へ報告済み）。
3. **profile の表の値は公開の source と一致**（調べの結果、§1）。抜けていて要る物は 2 つ: GOP が scrub 済みの VRAM の末尾（256 MiB）に WPR が収まるかの検査（§2.1）と、Ampere 以降の compute の `SEND_SIGNALING_PCAS2_B`（§2.3）。
4. **既定で起動**（2026-10-15 ユーザーの決定「GTX 16・RTX 30・40 はデフォルトで有効にします。」）: profile の `verified` を「既定で起動するか」の意味に分け直し、未検証の family も既定で起動する。段ごとの失敗で firmware の画面に戻す道（§3）を受け入れの条件にする。
5. 30/40 の実機は無い（ユーザーの指示どおり host 試験と build まで）。RTX 2070 の回帰は RTX を差した機械が要る（今の試験の台に無い、§6、問い Q242-2）。

## 1. 表の値の確かめ（調べの結果）

| 項目 | 結果 | 出典 |
| --- | --- | --- |
| firmware の file | 正しい。WHENCE の link: ga103〜ga107 → ga102、ad103〜ad107 → ad102、tu104・tu106 → tu102、tu117 → tu116。tu116 の image・bootloader は tu102 への link、booter は別。ad102 の image は ga102 への link、bootloader・booter は別 | linux-firmware WHENCE |
| 署名の section | 正しい（`.fwsignature_tu10x`・`tu11x`・`ga10x`・`ad10x`） | nouveau `nvkm/subdev/gsp/{tu102,tu116,ga102,ad102}.c` |
| VRAM size | 正しい（GA/AD は 0x1183a4 の MiB） | nouveau `nvkm/subdev/fb/ga102.c:29-32` |
| display fuse 0x820c04 bit 0 | devinit の「display 無し」の判断の register としては正しい（nouveau `nvkm/subdev/devinit/ga100.c:66-73`）。nvrtx は VRAM の VBIOS の写しを諦める判断にも使う（`rom.c:151-154`）が、nouveau の VRAM の写しの判断はこの fuse を読まない（`nvkm/subdev/bios/shadowramin.c:67-68`）。PROM の道（`rom.c:206`）が残るので害は無い。**実機で未確認**として記録 | nouveau |
| heap os・min | os 22 MiB は正しい。min は ogkm 570.144 の既定が 88 MiB（`gsp_fw_heap.h:101`）、nvrtx の 170 は nouveau r570（`rm/r570/nvrm/gsp.h:568-574`）と同じで安全側。Turing の 64 は一致 | ogkm・nouveau |
| scrubber の channel | 正しい（TU102/104/106 だけ） | nouveau `rm/r570/gr.c:95-103` |
| class | 全部正しい。usermode の class は nvrtx が doorbell を register に直に書く（`channel.c:106`）ので要らない（vfn の領域は tu102・ga100 で同じ） | nouveau `rm/{tu1xx,ga1xx,ad10x}.c`、`nvkm/subdev/vfn/` |
| window storage | 正しい（C6 の window に MEMORY_LAYOUT が無い） | ogkm `nvkms-evo3.c`、nouveau `dispnv50/wndwc67e.c` |
| QMD の版・SM・warps・carve-out | 正しい（GA/AD は V03_00、Ada も c6c0 の V03_00、warps 48、carve-out {0,8,16,32,64,100} KiB） | Mesa `nak/qmd.rs:659-709`、`winsys/nouveau_device.c:116-135, 202-215` |
| 製品の表 | `products.c` の 288 個は 2026-10-29 の pci.ids と全部一致（chip の食い違い 0、表にあって pci.ids に無い物 0。2026-10-15 から新しい TU/GA/AD の id は増えていない） | 調べの script（scratchpad、commit しない） |

TU〜AD で同じと確かめた物（chip で分けなくてよい）: GspFwWprMeta、FRTS 1 MiB、VGA workspace、GSP_SYSTEM_INFO、GSP_ARGUMENTS、registry の 3 key、WPR2 の register、GFW boot の待ち、VBIOS の読み方、FWSEC の command、GR の context buffer の一覧と promote（RM から大きさを読む、`gr.c:255-290`）、3D の init の method（Mesa NVK の条件は `>= TURING_A`・`>= HOPPER_A`・`>= BLACKWELL_A` だけ）。

## 2. 足す物

### 2.1 GOP の scrub 済みの末尾の検査（新、layout の段）

- 事実: GOP（firmware）が scrub 済みにする VRAM の末尾は TU〜AD とも 256 MiB（ogkm `g_kernel_gsp_nvoc.h:1417` の pre-scrubbed の大きさ、`g_kernel_gsp_nvoc.c:1005-1007`）。RM はそれを超える時、Ada では scrubber の ucode を SEC2 で走らせ（`kernel_gsp_ad102.c`）、TU・GA では heap の最大を縮める（`kernel_gsp.c:3330-3345, 5096-5107`）。nouveau は scrubber を持たず確かめもしない。
- nvrtx の GA/AD の layout は ELF 63.5 MiB（`userland/firmware/nvidia-gsp/Makefile:37`）＋ heap 170 ＋ boot ＋ FRTS 1 ＋ 記録 ＋ 外の heap 1 ＋ VGA workspace ≈ 237 MiB で、余りは約 20 MiB（調べの見積もり、実装の時に `layout.c` で数え直す）。
- 設計: layout の後に「VRAM の末尾から WPR の始まりまでが 256 MiB 以下」を確かめ、超えれば `layout fail`（GPU は未変更、`gsp.c:177-182` の道）。Ada の scrubber は入れない（GOP が走った実機は済んでいる想定、headless・二次の Ada は諦める）。host 試験で境界を見る。

### 2.2 QMD V03_00（p001 再開点 2 の確定）

nvrtx が書く field（`compute-launch.c:29-62`）の V02_02 → V03_00（open-gpu-doc `clc3c0qmd.h`・`clc6c0qmd.h` の位置を比べた）:

| field | V02_02 | V03_00 |
| --- | --- | --- |
| QMD_VERSION・MAJOR | 579:576・583:580 | 同じ位置、値 (3, 0) |
| MIN・MAX・TARGET の SM_CONFIG_SHARED_MEM_SIZE | 568:562・575:569・663:657（7 bit） | 567:562・574:569・662:657（6 bit。663 は SHARED_ALLOCATION_ENABLE、0 のまま） |
| BARRIER_COUNT | 959:955 | 767:763 |
| SM_GLOBAL_CACHING_ENABLE、API_VISIBLE_CALL_LIMIT、CTA_RASTER、CTA_THREAD_DIMENSION、CONSTANT_BUFFER_VALID・ADDR・SIZE、REGISTER_COUNT_V、SHARED_MEMORY_SIZE、PROGRAM_ADDRESS | — | 変わらない |

shared の符号（KiB/4+1）は同じ（100 KiB は 26 で 6 bit に入る）。nvrtx が今書かない field で動いた物（将来書く時の注意、block の comment に書く）: SHADER_LOCAL_MEMORY_LOW・HIGH、CRS_SIZE（無い）、SASS_VERSION、8 bit の REGISTER_COUNT（無い）、RELEASE0/1 の全部、PROGRAM_OFFSET。AD10x の `clc9c0qmd.h` の V03_00 は nvrtx が使う field で c6c0 と同じ位置 → AD も V03_00。

関数: `nvrtx_qmd_build_for(qmd, info, profile)`・`nvrtx_push_compute_launch_for(push, va, profile)` を足し、今の名前は Turing の wrapper のまま（compiler の host 試験 `plan/tools/nvrtx/nvrtx-sm75-host-test.sh` が使うので壊さない、p001 再開点 2 の案）。

### 2.3 Ampere 以降の PCAS2_B

`SEND_SIGNALING_PCAS2_B`（0x02c0、値 3 = INVALIDATE_COPY_SCHEDULE）を compute の class が Turing より新しい時に使う（Mesa `nvk_cmd_dispatch.c:329-337`、open-gpu-doc `clc6c0.h:372-377`）。今は `SEND_SIGNALING_PCAS_B`（`compute-launch.c:261`）固定。profile に「PCAS2_B を使う」の欄を足すか class で決める（実装で選ぶ。profile の欄が他の判断と同じ形で推し）。

### 2.4 display（任意）

nouveau は GA/AD の core に Turing の core の code（c57d）をそのまま使い、window だけ c67e（`dispnv50/core.c:46-48`、`wndwc67e.c`）。ogkm の nvkms は C6 以降で raster の後に `HEAD_SET_RASTER_HBLANK_DELAY` を常に書く（HDMI の DSC 以外は 0、`nvkms-evo3.c:1549-1590`）。必須の差は見つからなかった。HBLANK_DELAY = 0 を c67d・c77d の head の設定に足すのは小さく安全側なので p004 に入れる（Q242 にしない、技術の判断）。

### 2.5 compiler に SM を渡す（p001 再開点 1、変更なし）

4 か所を `_sm(..., profile->sm, ...)` に。vk の層は session の owner しか持たないので `nvrtx_vk_shader_model(void *owner)`・`nvrtx_vk_threed_class(void *owner)` を `nvrtx-vk.h` に足し `node.c` で実装。draw の init は `nvrtx_push_draw_init_class(..., class)`（`draw.c:568`、`vk/vk-execute.c:453`）。

## 3. 既定で起動と、段ごとの諦め

- 今: `struct nvrtx_profile` の `verified`（`nvrtx-private.h:127-133`）が 0 の family は `nvrtx.gsp=1` の時だけ起動（`nvrtx.c:168-185`）。verified の family も「firmware の画面がその GPU にある」（`device.gop_on_gpu`）時だけ既定で起動し、二次の GPU は `nvrtx.gsp=1` が要る。この条件は変えない（§2.1 の scrub 済みの末尾も GOP が走った GPU の前提）。
- 変更: `verified` を「実機で見た」の記録の意味に残し、起動の判断は新しい欄（例 `boot_default`）にする。TU10x・TU11x・GA10x・AD10x とも `boot_default = 1`（ユーザーの決定）。`nvrtx.gsp=0` は今のとおり全部を firmware に残す。未検証の family の起動の時は段の印に「not yet seen on hardware」を出す（log の文字列、開発の管理情報は書かない）。
- 失敗の段と戻り方（調べの事実: FWSEC-FRTS と booter_load は display を触らない。ogkm・nouveau の unload は GSP falcon の reset → FWSEC-SB → booter_unload、nvrtx の `boot.c:303-340` も同じ順）:

| 段 | 失敗の時 | 画面 |
| --- | --- | --- |
| layout まで（identify・VRAM・VBIOS・FWSEC・booter の meta・§2.1 の検査） | GPU は未変更（`gsp.c:177-182`） | firmware の画面が残る |
| devinit・FRTS | WPR2 が立つだけ。teardown（FWSEC-SB・booter_unload） | 残る見込み |
| booter_load の後、GSP-RM の INIT_DONE が来ない | teardown | **未確認**（GSP-RM の init が GOP の scanout を止めるかは公開の source に無い。実機でしか分からない） |
| GSP の後（display・GR・channel） | 今の fault の道 | nvrtx の display が持つ |

- 危険の受け入れ: 3 行目は実機の無い family で「画面が出ない」になりうる。ユーザーの決定（既定で有効）はこの危険を含むと読むが、確かめる（Q242-1）。

## 4. 試験

| 試験 | 形 | 合格 |
| --- | --- | --- |
| 戻した host 試験 3 本（`plan/ws242/tests/`） | host | 今の API に追従して通る（p001 再開点 3: `nvrtx_wpr_layout` の heap の引数、copy の class、window の storage、vbios の v3、identify の profile）。RTX 2070（TU106、8 GiB）の layout・class・storage・QMD 2.2 が今と同じ |
| 新しい項 | host | 全 chip → profile、profile の各欄（§1 の表）、製品の表の整列・重複なし・chip の一致、booter の meta と fuse（実 file があれば ga102・ad102・tu116）、FWSEC v3 の合成の ROM、`nvrtx_vram_size_mib`、Ampere の heap、§2.1 の境界（255・256・257 MiB）、QMD 2.2 と 3.0 の bit の位置（open-gpu-doc の値と比べる）、PCAS と PCAS2_B |
| compiler | host | `plan/tools/nvrtx/nvrtx-sm75-host-test.sh`・`nvrtx-sm75-desktop-test.sh --strict` が今と同じ（287 checks・138 compiled、p002 の記録） |
| build | — | `make … BUILD=build/ws242-pNNN build/ws242-pNNN/vmunix`（`plan/tools/nvrtx/config-amd64-nvrtx.mk` と CI の `config/ci/config-amd64.mk`）で warning 0 |
| RTX 2070 の回帰 | 実機（T1） | 起動・display・desktop・vkcube 相当（WS037 の受け入れと同じ）。機械は Q242-2 |
| RTX 30/40 | — | 無し（ユーザーの指示） |

## 5. Phase の表（LW は ±50%、1 LW ≈ 実時間 15〜25 分）

| Phase | 内容 | 依存 | LW |
| --- | --- | --- | --- |
| p001 | driver の表と起動（2026-10-15 の WIP は main に入った）。この設計で残りを p003〜p005 に移し、p001 は「済み（実機なし）」で閉じる案 | — | — |
| p002 | compiler の SM86・SM89（済み） | — | — |
| p003 | host 試験を git から戻し今の API に追従、新しい項の半分（identify・profile・products・layout の §2.1） | — | 6 |
| p004 | driver が SM を渡す（§2.5）、QMD 3.0 と PCAS2_B（§2.2・§2.3）、HBLANK_DELAY（§2.4）、host 試験の残り | p003 | 8 |
| p005 | 既定で起動と段ごとの諦め（§3）、§2.1 の検査、firmware の package の確かめ（cache の `<rev>-tu-ga-ad` の fetch と image の build） | p003 | 4 |
| p006 | RTX 2070 の実機の回帰（T1） | p004・p005、機械（Q242-2） | 3 |
| p007 | 規約の全文と出典の監査（block ごとの Value(s)/Procedure referenced、sm75-schedule.c の Collabora の表示の扱いは p002 §3.1 の Q1 の判断） | p004〜p006 | 3 |

計 24 LW。WS250 の R10（Ampere の runlist ごとの chid）は p004 で確かめる: nvrtx の固定の chid（work 4・copy 8・session 16〜）が Ampere の runlist ごとの chid の範囲で衝突しないか（RM は runlist ごとの chid の管理を持つ、ogkm `kernel_fifo.c` の `kfifoGetChidMgr`）。

## 6. 並行の作業

- WS250（取り消しと復帰）は work.c・vk-node.c・gr.c の promote を変える。WS291 の NVIDIA の段（P2）は work.c・vk-execute.c を変える。WS243（NVDEC、P6 が実装、main に merge しない）も nvrtx の node に触る。
- 推しの順: WS242 p003〜p005 → WS250 p002・p003 → WS291 の NVIDIA の段。p004 の vk-execute.c の変更は呼び出しの行だけ（p001 再開点 1 のとおり）。Q1 が P2・P6 と調整。

## 7. 選ばなかった案

- **未検証の family を `nvrtx.gsp=1` のまま**: ユーザーの決定に反する。
- **Ada の scrubber を入れる**: SEC2 で走る ucode の道が増え、試す実機が無い。§2.1 の検査で諦める方が安全。
- **QMD の field を 1 つの関数で版を if で分ける**: compiler の host 試験が今の名前を使い、Turing の出力を byte 単位で保つ約束（p002）があるので、版ごとの関数と wrapper にする。

## 8. ユーザーの回答（2026-10-29、原文は ws.md）

Q242-1「既定で起動 (推し)」、Q242-2「WS305 の VFIO (推し)」。順は Q1 が WS242 p003〜p005 → WS250 p002・p003 → WS291 の NVIDIA の段に決めた。下は記録として残す問い。

### 8.1 問い

- Q242-1 既定で起動の危険: GSP の起動が booter_load の後で止まった時に、実機の無い RTX 30/40・GTX 16 で画面が出なくなるかは公開の source で分からない。推し「決定どおり既定で起動し、`nvrtx.gsp=0` で逃げられることを文書に書く」。選択肢: 実機で一度確かめるまで `nvrtx.gsp=1` のまま。
- Q242-2 RTX 2070 の回帰の機械: 推し「WS305 の Linux＋VFIO の passthrough（10.0.10.3）で RTX を使える時に T1」。選択肢: RDNA と差し替えて素の起動（WS291 の P2 が RDNA を使用中）。
- （報告）host 試験 3 本が 41a28534b で消えた。p003 で git から戻す。
