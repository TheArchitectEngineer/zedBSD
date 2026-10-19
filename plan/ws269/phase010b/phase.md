# ws269-p010b: 資料の固定（M1〜M3 の path と blob SHA）

- ID: ws269-p010b（親 [ws269](../ws.md)）
- Status: cleared 待ち（Q1 の判定）
- Disposition: normal
- Queue: q1365 / q1365-i01（2026-10-20 P1）
- 設計: [design-p010.md](../design-p010.md) 版 1 の §0・§10 の p010b。コードなし
- 依存: p002（版の固定）

## 範囲と結果

- Q1 の依頼の範囲: M1〜M3 の file の path と blob SHA を API で確かめ、design-p010.md の §0 の表（M1〜M3）を確定の値に直す。design-p010.md は §0 の表と未確認の行（旧 20 行）だけを直した（design-reviewer の並行レビューのため）。
- 確かめ方（2026-10-20）: Mesa は GitLab API `projects/mesa%2Fmesa/repository/tree?path=src/imagination&recursive=true&ref=ddaf85e353442d9d7f192fdcf1823e652e3f2ee3`（8 page、blob 279 件）。Linux は `gh api repos/torvalds/linux/contents/drivers/gpu/drm/imagination?ref=c03c1a14507dfaf8b856c2608f9ac216021fc222` と `include/uapi/drm/pvr_drm.h`。FWIF の 3 つの header は raw を取って `git hash-object` が API の SHA と一致、struct の行を grep した。拒否はなかった。
- 取得物の置き場所（plan/ には置かない）: `/tmp/claude-1000/-home-awe-zedBSD-claude1/70c47280-c9e2-5e3f-b5d9-072cd0539dfc/scratchpad/p010b/`（`mesa.txt`・`mesa.json`・`linux.txt`・`linux/*.h`）。片付けは Q1。
- 記憶との違い: Mesa 26.2.5 の XML は `csbgen/rogue/` の下で接頭辞 `rogue_` なし、TA の XML は無い。vulkan の job・cmd_buffer・pass・pipeline・tex_state は `pvr_arch_*.c` に分かれ、`pvr_cmd_buffer.c`・`pvr_job_render.c`・`pvr_job_transfer.c` は無い（`.h` のみ）。`pvr_clear.c` は `vulkan/rogue/`。FW の command の struct は `pvr_rogue_fwif_client.h`。

## 未実施・残り

- design §10 の p010b の行にある BXE の feature の分岐（TLA・blend・twiddled の padding・PBE の linear の stride の単位・descriptor の word の大きさ）と §5.3 の 2 は、今回の依頼の範囲外で未確認（file の中身を読んでいない）。p010c 以前に別の依頼で読む必要がある。
- design 本文の §3〜§7 に残る旧い file 名（`pvr_job_render.c` 等）は直していない（§0 の M2 の行に対応を書いた）。

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
