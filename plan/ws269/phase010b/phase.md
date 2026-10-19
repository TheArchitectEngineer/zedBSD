# ws269-p010b: 資料の固定（M1〜M3 の path と blob SHA）

- ID: ws269-p010b（親 [ws269](../ws.md)）
- Status: cleared 待ち（Q1 の判定）
- Disposition: normal
- Queue: q1365 / q1365-i01（2026-10-20 P1、path・SHA）、q1366 / q1366-i01（2026-10-20 P1、BXE の分岐と review 1 の B3・M2・M3・M4・Minor 1・2）
- 設計: [design-p010.md](../design-p010.md) 版 1 の §0・§10 の p010b。コードなし
- 依存: p002（版の固定）

## 範囲と結果

- Q1 の依頼の範囲: M1〜M3 の file の path と blob SHA を API で確かめ、design-p010.md の §0 の表（M1〜M3）を確定の値に直す。design-p010.md は §0 の表と未確認の行（旧 20 行）だけを直した（design-reviewer の並行レビューのため）。
- 確かめ方（2026-10-20）: Mesa は GitLab API `projects/mesa%2Fmesa/repository/tree?path=src/imagination&recursive=true&ref=ddaf85e353442d9d7f192fdcf1823e652e3f2ee3`（8 page、blob 279 件）。Linux は `gh api repos/torvalds/linux/contents/drivers/gpu/drm/imagination?ref=c03c1a14507dfaf8b856c2608f9ac216021fc222` と `include/uapi/drm/pvr_drm.h`。FWIF の 3 つの header は raw を取って `git hash-object` が API の SHA と一致、struct の行を grep した。拒否はなかった。
- 取得物の置き場所（plan/ には置かない）: `/tmp/claude-1000/-home-awe-zedBSD-claude1/70c47280-c9e2-5e3f-b5d9-072cd0539dfc/scratchpad/p010b/`（`mesa.txt`・`mesa.json`・`linux.txt`・`linux/*.h`）。片付けは Q1。
- 記憶との違い: Mesa 26.2.5 の XML は `csbgen/rogue/` の下で接頭辞 `rogue_` なし、TA の XML は無い。vulkan の job・cmd_buffer・pass・pipeline・tex_state は `pvr_arch_*.c` に分かれ、`pvr_cmd_buffer.c`・`pvr_job_render.c`・`pvr_job_transfer.c` は無い（`.h` のみ）。`pvr_clear.c` は `vulkan/rogue/`。FW の command の struct は `pvr_rogue_fwif_client.h`。

## q1366: file の中身で確かめた値（2026-10-20）

取得は GitLab API の `repository/files/.../raw?ref=ddaf85e3…`（Mesa）と `raw.githubusercontent.com/torvalds/linux/c03c1a14…`（Linux）。拒否なし。置き場所は scratchpad の `p010b/src/`・`p010b/linux/`（plan/ には置かない、片付けは Q1）。追加で読んだ file: Mesa `common/device_info/bxe-4-32.h`・`common/pvr_device_info.{c,h}`・`vulkan/pvr_common.h`・`vulkan/pvr_descriptor_set.h`・`pco/pco_data.h`・`pco/pco_nir.c`・`pco/pco_nir_pvfio.c`、Linux `pvr_free_list.c`・`pvr_hwrt.c`・`pvr_context.c`・`pvr_ccb.c`・`pvr_job.c`・`pvr_fw.c`・`pvr_rogue_heap_config.h`・`pvr_rogue_fwif_check.h`（SHA は §一覧の tree の取得と同じ ref）。

### BXE の feature の分岐（Value(s) referenced）

| 項目 | 値 | 出典 | 印 |
| --- | --- | --- | --- |
| TLA | 無い。`bxe-4-32.h` に `has_tla` 無し、26.2.5 の `pvr_device_info.h` に TLA の feature 自体が無い。transfer は全部 ISP・PBE の 3D の道（`pvr_arch_job_transfer.c` の分岐は `simple_internal_parameter_format`(_v2)・`paired_tiles`・`ipf_creq_pf`・`screen_size8K` で、BXE は全部 true） | Value(s) referenced: Mesa `bxe-4-32.h`・`pvr_arch_job_transfer.c` | 事実 |
| blend | compiler（pco）の NIR の `nir_lower_blend` で行う（`pco_nir.c:997`）。driver は attachment ごとの factor・op と output format を `pco_data` で渡す（`pvr_arch_pipeline.c:2229-2270`）。blend 定数は shared register（548-552）。BXE の feature の分岐なし | Procedure referenced: Mesa `pvr_arch_pipeline.c`・`pco_nir.c` | 事実 |
| fragment の出力の pack | compiler の `pco_nir_pvfio.c`（`pack_to_format`、PBE の format へ `nir_pack_pco`）。PBE は pack 済みの raw を受ける | 同 `pco_nir_pvfio.c:86-217` | 事実 |
| twiddled の padding | twiddled（と mip 2 段以上）は幅・高さ・深さを next power of two。3D の image は extent の alignment 4、他は 1。mip 2 段以上の layer の大きさは 1×1 までの全段で数える | Value(s) referenced: Mesa `pvr_image.c:73-110・140-186` | 事実 |
| PBE の LINEAR の stride（R6） | 幅を `pbe_stride_align` pixel に揃える。BXE は `pbe_stride_align_1pixel` が無いので `PBESTATE_REG_WORD0.LINESTRIDE.UNIT_SIZE` = 2 pixel（field は bit 34-49）。Mesa に 32 byte の条件は無い（R6 の 32 byte は WS277 の側の条件として残る） | Value(s) referenced: `pvr_image.c:467-471`、`pbestate.xml:258-259` | 事実 |
| image の alignment | winsys の page の大きさ（`pvr_image.c:308`） | 同 | 事実 |
| texture state の word | IMAGE_WORD0・1 の各 64 bit（`texstate.xml:272・292`）、LINEAR は `STRIDE_IMAGE_WORD1`（315）。sampler は SAMPLER_WORD0・1 の各 64 bit（342・363） | M1 `texstate.xml` | 事実 |
| descriptor の大きさ | buffer 16 byte（addr 64・size 32・offset 32）、image 16 + meta 4×4 = 32 byte、sampler 16 + meta 16 + gather 16 = 48 byte、combined 80 byte（`pvr_common.h:190-226`、meta の数は `pco_data.h:141-157`）。`ROGUE_NUM_TEXSTATE_IMAGE_WORDS`・`SAMPLER_WORDS` = 2 は XML の struct の数からの読み（define の場所は生成物で未確認） | Value(s) referenced: `pvr_common.h`・`pco_data.h` | 事実（2 の値は推測寄り） |
| design §5.3 の 2 | pack も blend も compiler の側（上 2 行）。D-P3 (a)「driver が末尾の命令を足す」は Mesa と逆 | 同 | 事実 → **設計の変更が要る** |

BXE の他の値（`bxe-4-32.h`）: tile 16×16、`isp_max_tiles_in_flight` 4、`tpu_parallel_instances` 4、`num_clusters` 1、`gpu_multicore_support`・`roguexe`・`s8xe`・`tpu_image_state_v2`・`pbe2_in_xe`・`paired_tiles` あり。quirks は BRN70165・72168・72463・74056、enhancements は ERN35421・38748・42307・45493。

### review 1 の B3・M2・M3・M4 の確かめ

| 項目 | 確かめた事 | 出典 | 印 |
| --- | --- | --- | --- |
| B3 GROW | FW の `FWCCB_CMD_FREELIST_GROW` に、grow が出来なくても `KCCB_CMD_FREELIST_GROW_UPDATE` を `delta_pages = 0`（new_pages・ready_pages 付き）で必ず返す | Linux `pvr_free_list.c:360-391`、`pvr_ccb.c:153-158` | 事実 |
| B3 RECONSTRUCTION | `FWCCB_CMD_FREELISTS_RECONSTRUCTION` に各 free list を作り直して `KCCB_CMD_FREELISTS_RECONSTRUCTION_UPDATE` を返す。HWRT の state を `RTDATA_STATE_HWR` にし `HAS_LAST_GEOM` を落とす | `pvr_free_list.c:560-633`、`pvr_ccb.c:149-151` | 事実 |
| B3 FRAG_PR | Mesa は 1 render に geometry・FRAG_PR（`DRM_PVR_SUBMIT_JOB_FRAG_CMD_PARTIAL_RENDER`）・fragment の 3 つを常に積む。kernel は flag を `CCB_CMD_TYPE_FRAG_PR` にするだけで、FRAG_PR の有無を検査しない（geom・frag の対は `pvr_jobs_link_geom_frag` で隣の job を組むだけ）。geometry の命令に `partial_render_geom_frag_fence` | Mesa `pvr_drm_job_render.c:486-512`、Linux `pvr_job.c:187-188・640-680`、`pvr_rogue_fwif_client.h:152-156` | 事実 |
| B3 FRAG_PR が無い時の FW | source に無い（FW の binary の中）。止まるかは **未確認** | — | 未確認 |
| M2 free list | HWRT は local・global の 2 つの handle（`PVR_DRM_HWRT_FREE_LIST_LOCAL` 0・`GLOBAL` 1）。local は `free_list_min_pages`（runtime info の query）以上でないと作れない | `pvr_drm.h:937-963`、`pvr_hwrt.c:38-50` | 事実 |
| M2 rt_data | `rt_data_args[2]`（double buffer）、各組に PM の MLIST・macrotile の配列・region header の address。Mesa は region header を 2 組分まとめて RGNHDR の heap に取る（alignment `CR_TE_PSGREGION_ADDR_BASE_ALIGNMENT`） | `pvr_drm.h:902-963`、Mesa `pvr_arch_job_render.c:470-495` | 事実 |
| M2 heap | GENERAL 0x80_0000_0000（128 GiB）、PDSCODEDATA 0xDA_0000_0000（4 GiB）、RGNHDR 0xDB_F000_0000（256 MiB）、USCCODE 0xE0_0000_0000（4 GiB）、FW 0xE1_C000_0000、TRANSFER_FRAG 0xE4_0000_0000（16 GiB）、VISTEST 0xF2_0000_0000（2 MiB）。PDS・USC の base は kernel が sysinit の `pds_exec_base`・`usc_exec_base` で FW に渡す（`pvr_fw.c:375-376`）。uapi の注に「RGNHDR は BRN63142 の GPU だけ」とあり、BXE の quirks に BRN63142 は無いが、Mesa は無条件に RGNHDR の heap を使う | Value(s) referenced: `pvr_rogue_heap_config.h:41-103`、`pvr_drm.h:272-277` | 事実（RGNHDR の要否は BXE で未確認） |
| M3 render の context | kernel は render の context に `static_context_state` を必須にする（長さ 0 は -EINVAL）。中身は `geom_registers_caswitch`（VDM の context state の base・resume、TA の context state の base、store・resume の task 0〜4 × 2）。Mesa は SR・PT の PDS・USC の program を作る（`pvr_arch_job_context.c:88-845`）。compute も必須（CDM の cswitch）。transfer は長さ 0 でないと -EINVAL（静的な state なし） | Linux `pvr_context.c:88-135`、`pvr_rogue_fwif_shared.h:142-187`、Mesa `pvr_arch_job_context.c` | 事実 |
| M3 context switch を止められるか | mainline は sysinit の `config_flags` に `DISABLE_DM_OVERLAP` を条件付きで立てるだけで、CTXSWITCH の INICFG（MODE_RAND・SRESET・PROFILE）は立てない。context switch を止める設定は kernel の source に無い。state が 0 の時の FW の挙動は **未確認** | `pvr_fw.c:417-424`、`pvr_rogue_fwif.h:444-489` | 事実 / 未確認 |
| M3 CLEANUP | HWRT の破棄は `pvr_fw_structure_cleanup(ROGUE_FWIF_CLEANUP_HWRTDATA)`、free list は `ROGUE_FWIF_CLEANUP_FREELIST`（busy なら再試行） | `pvr_hwrt.c:506`、`pvr_free_list.c:448-492` | 事実 |
| M4 transfer | IPF の control stream（region の配列 + primitive の block）を TRANSFER_FRAG の heap に取り、stream の address と `CR_ISP_MTILE_BASE` を heap の base からの相対で書く。`CR_ISP_RENDER.MODE_TYPE` は FAST_2D（拡大縮小の道は FAST_SCALE、3028 行） | Procedure referenced: Mesa `pvr_arch_job_transfer.c:4125-4146・4555-4565・3027-3028` | 事実 |
| M4 heap の base を誰が設定するか | kernel の source（sysinit は pds・usc・texture_heap_base だけ、`pvr_rogue_fwif.h:1849-1856`）にも FW の transfer の register（`pvr_rogue_fwif_client.h:317-358`）にも TRANSFER_FRAG の base は無い。FW か硬の固定と読めるが **未確認** | — | 未確認 |
| Minor 1 | §0 の M2 の行の path は q1365 で直した。本文 §3〜§7 の旧い名前を q1366 で `pvr_arch_*`・`csbgen/rogue/*.xml`・`rogue/pvr_clear.c` に直した。`pvr_rt_dataset.c`・`pvr_free_list.c`（Mesa、76・163 行）は存在を確かめた。`pvr_rogue_fwif_check.h` は取得した | — | 事実 |
| Minor 2 | `has_tla`・`pbe_stride_align_1pixel` は `bxe-4-32.h` に無い。linestride の単位 2 は `pbestate.xml:258-259` | 上の表 | 事実 |

## 設計の変更が要る（本文は変えていない、版 2 の q1367 へ）

- D-P3・§5.3 の 2: blend と fragment の出力の pack は Mesa では compiler（`nir_lower_blend`・`pco_nir_pvfio.c`）の側。driver が末尾に命令を足す (a) は逆で、compile の入力に output format・blend の設定・blend 定数の shared register を足す形（review 1 の B4）にする必要がある。
- B3: free list の grow の要求に応えない形は不可。delta 0 の GROW_UPDATE と RECONSTRUCTION の応答は必須（mainline の手順）。FRAG_PR を積むかは FW の挙動が未確認なので Mesa に倣って積むのが安全（推測）。
- M3: render の context は静的な state が必須（mainline の kernel の検査）。SR・PT の program を作るか、0 で済むかは FW の未確認の挙動に依る。

## 未実施・残り

- FRAG_PR が無い時の OOM、context switch の state が 0 の時、TRANSFER_FRAG の base の設定は FW の中で、source からは確かめられない（未確認）。実機の試験か FW の挙動の観察が要る。
- design-p010.md の本文は事実の訂正（file 名、TLA・twiddled・alignment・word の大きさ・descriptor の大きさ、§0）だけ。D-P3・B3・M2〜M4 の本文への反映は版 2（q1367）。

## 一覧（blob SHA と path）

### M1・M2: Mesa 26.2.5（ddaf85e353442d9d7f192fdcf1823e652e3f2ee3）

```
83cf0e89253fca14f7a9a87aadb4392ccc0faed9 src/imagination/csbgen/rogue/cdm.xml
823026654a0f55d1fd3773696e17f5af17f1a622 src/imagination/csbgen/rogue/cr.xml
1d5322a2a6d14864b74a5def7b9d24b958d08a0b src/imagination/csbgen/rogue/ipf.xml
8d3c24e0733dbc0a93beab756475cbb6dddcba9a src/imagination/csbgen/rogue/kmd_stream.xml
a736a938b0837f9e32f690985b6c05ba27171f3d src/imagination/csbgen/rogue/lls.xml
b06f02db0df6a83f0c8d18bf86dcfdee07015b21 src/imagination/csbgen/rogue/pbestate.xml
b774fe41671b3e2a46ae5ab8114ee5b32d8cbfdc src/imagination/csbgen/rogue/pds.xml
61605e64218164c2cd28699647b139b1b086ccae src/imagination/csbgen/rogue/ppp.xml
d4028d7544b2ca64edfcfb28732dbcacf56d84f9 src/imagination/csbgen/rogue/texstate.xml
f6a8a84a9e6dd2421da732f59d21878070d3a88f src/imagination/csbgen/rogue/vdm.xml
0b58cbab52d271a7540becac8a48ba5eba299812 src/imagination/vulkan/pds/pvr_pipeline_pds.c
1990433e30b5020834ec74ba0fe501cf26696b18 src/imagination/vulkan/pvr_arch_border.c
2d8da1ed87be1636568e2e4c10dfef0e46e03f86 src/imagination/vulkan/pvr_arch_cmd_buffer.c
308a9fc42ed52c43dedadf63c45750c2ab25823c src/imagination/vulkan/pvr_arch_cmd_query.c
66edec7622b789a5fcf0d5f87d15a2e96ef476fb src/imagination/vulkan/pvr_arch_csb.c
ce0ac6d4e11c57d1d9aa76db06324344a0854485 src/imagination/vulkan/pvr_arch_descriptor_set.c
64b0e3244c97d8b836a9180fe24a58fb6b87ba73 src/imagination/vulkan/pvr_arch_device.c
4da2bce43857d5a54d9ddf9c6117db62a343395b src/imagination/vulkan/pvr_arch_formats.c
a70748fc290ebaeb24898fed472b1606ee7c1305 src/imagination/vulkan/pvr_arch_framebuffer.c
27465b2f4911e3c78ed0f202dba0935391df6457 src/imagination/vulkan/pvr_arch_hw_pass.c
af39eab80eaf93196c8dba88f56353ec3f2d1d35 src/imagination/vulkan/pvr_arch_image.c
f420deb6969c14020a05e333780eae9056c01f7c src/imagination/vulkan/pvr_arch_job_common.c
c976eedd982d2f0186113d4323be4d93eb3db655 src/imagination/vulkan/pvr_arch_job_compute.c
5ad4d2b152246ab9794fad61ff3d3bdda0cea70f src/imagination/vulkan/pvr_arch_job_context.c
5ce426018e16add8616e09c3cd7d237488b0aa58 src/imagination/vulkan/pvr_arch_job_render.c
df05efe7e2e5ce9544bbe2febdc4027152724f5c src/imagination/vulkan/pvr_arch_job_transfer.c
bd59ccd64c5f4da64a2c42d17be1bc36cd343351 src/imagination/vulkan/pvr_arch_mrt.c
7a58159f40a3b86138e0391b7d5af96be298b04f src/imagination/vulkan/pvr_arch_pass.c
a3769750e4587eeb43f11b79834ac71a5c67415e src/imagination/vulkan/pvr_arch_pipeline.c
d9915f043b6cf774ab397b31bbf41fe6cb040439 src/imagination/vulkan/pvr_arch_query_compute.c
90a1408db880824c8158c0412fa15526a4e69817 src/imagination/vulkan/pvr_arch_queue.c
1b96bfccfe6efc2e050b7e05e3c6aa02a2c7e92f src/imagination/vulkan/pvr_arch_sampler.c
73808e457e232547595805bcc5c72d94e589f256 src/imagination/vulkan/pvr_arch_spm.c
8233913f9266410ee9cf894a08e4d979d39b2a72 src/imagination/vulkan/pvr_arch_tex_state.c
c5a389642dfdcb505e4ed9ddae6bb0320301bc9c src/imagination/vulkan/pvr_descriptor_set.c
5d5a6d982d9c5f0734aef0df29b207775e822a4b src/imagination/vulkan/pvr_formats.c
654ba7761a0904636482e73a609f16c2848a0c42 src/imagination/vulkan/pvr_image.c
5e66a6923e864b648e1d27db6a27963c7fa580b6 src/imagination/vulkan/pvr_spm.c
e034cc0e3d96b5efd45a5a772e5398a53ec331ce src/imagination/vulkan/rogue/pvr_clear.c
321bdbcfb08f07294901677eb03e83484cab806d src/imagination/vulkan/winsys/powervr/pvr_drm_job_render.c
9ae0f64dadee628bc9f535ed26387b29a8bde65e src/imagination/vulkan/winsys/powervr/pvr_drm_job_transfer.c
```

### M3: Linux v7.3（c03c1a14507dfaf8b856c2608f9ac216021fc222）

```
406e0758e860f23e29d5be24905bc8f385985f68 drivers/gpu/drm/imagination/pvr_context.c
faf5e586d8dc178d229b68582f91145ff32e7c71 drivers/gpu/drm/imagination/pvr_free_list.c
05c68c78da229591ef2ac8ce927a6422a04c53f7 drivers/gpu/drm/imagination/pvr_hwrt.c
b8a58d817009b69db90c05231e7870209d0baf73 drivers/gpu/drm/imagination/pvr_job.c
5d590c4c25663e431601dd26944465b62c31526b drivers/gpu/drm/imagination/pvr_rogue_fwif.h
6e224400083a2c5c85cffb76e8858d61ce47e729 drivers/gpu/drm/imagination/pvr_rogue_fwif_client.h
fe54c1cad7a9c5383b09607c64421a70631226a4 drivers/gpu/drm/imagination/pvr_rogue_fwif_shared.h
ccf6c2112468f3f53b093bc298dac2bbbfd35e6e include/uapi/drm/pvr_drm.h
```

struct の位置: `pvr_rogue_fwif.h` fwrendercontext 734・fwtransfercontext 779・rta_ctl 2042・freelist 2061・hwrtdata_common 2117・hwrtdata 2141。`pvr_rogue_fwif_client.h` geom_regs 98・cmd_geom 136・frag_regs 171・cmd_frag 246・transfer_regs 317・cmd_transfer 360。`pvr_rogue_fwif_shared.h` static_rendercontext_state 180。
