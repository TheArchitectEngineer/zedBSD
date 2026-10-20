<!-- awesome-plan project=zedbsd record=ws269-p010e -->

# ws269-p010e: PBE の store と EOT・固定の program・kernel の render check の三角形

- ID: ws269-p010e（親 [ws269](../ws.md)）
- Status: in-progress（q1378-i01 は前半。kick への組み込みと render check・実機は後半、p010n の merge の後）
- Disposition: normal
- Queue: q1378 / q1378-i01（2026-10-20 P2、前半）
- 設計: [design-p010.md](../design-p010.md) 版 2.1 の §4.2・§4.3・§5.4・§8.3・§10 の p010e
- 依存: O-p010c（image-state.c の format の表・layout、main に merge 済み）、O-p010n（kick・render check の骨。前半は依らない）

## 受け入れ条件（design-p010.md §10 の p010e の行）

pixel event の PDS・EOT の USC（static の EOT の PDS を含む）、clear の色と attachment の load の background（`pds_pr_bgnd` の中身）、PBE の clip（extent ∩ renderArea）、LINEAR・twiddled への store、kernel の中の三角形（vertex の USC と PDS、VDM・PPP）、render check の clear と三角形。host 試験（VDM・PPP の stream、固定の USC の bytes、PBE の word と clip は M9 の照合）、build。実機: `render check: OK`、`ta`・`3d` の段（p010n の分も）。

## q1378-i01 の範囲（前半、Q1 の依頼）

p010n に依らない部分だけ: pixel event の PDS・EOT の USC・static の EOT の PDS、background（clear）の USC と PDS、三角形の vertex の USC と PDS、VDM・PPP の stream、PBE の word と clip。触らない: render-queue.c・render.c・render-kick-layout*.c・render-private.h（P1 の q1377）。

## 作った物（2026-10-20 P2）

`src/drivers/gpu/powervr/render-program.{h,c}`（新規。lock も memory も持たず、heap の offset と device の address を受けて program と word を返す。host でも同じ file で build）:

| 関数 | 中身 | Mesa 26.2.5 の出所 |
| --- | --- | --- |
| `powervr_render_eot_usc` | EOT の USC: movi32 で PBE の state の 2 word を temp 0・1 へ、emitpix（s0・s2、freep、end）。temps 2 | `pvr_usc_eot`（即値の形）、`pco_map.py` の O_MOVI32・O_EMITPIX |
| `powervr_render_event_data` | pixel event の PDS の data 2 word（EOT の USC の task word）。code は fixed-area.c が PDS heap の +128 に書いた DOUTU 1 word のまま（BUG-342 の物と揃えた。変えていない） | `pvr_pds_generate_pixel_event`（emit の対 0）、`pvr_sub_cmd_gfx_per_job_fragment_programs_create_and_upload` |
| `powervr_render_event_words` | fragment の command の EVENT_PIXEL_PDS_CODE（128）・DATA・INFO（const_size 1、usc_sr_size 1） | `pvr_frag_state_stream_init` |
| `powervr_render_clear_usc` | background の clear の USC: shared 0 → pixout 0 の mov（overlap check、end）。temps 0 | `pvr_uscgen_loadop`（1 word の clear） |
| `powervr_render_clear_shared` | clear の色を shared 0 に書く PDS（data-program.c の shared の program、DOUTW 1 つ） | `pvr_load_op_constants_create_and_upload` |
| `powervr_render_kick_build` | USC の task だけを起こす PDS（data 8 word に task word、code は DOUTU 1 word）。background と三角形の fragment の「shader」の program | `pvr_pds_kick_usc` |
| `powervr_render_background_words` | `pds_bgnd`／`pds_pr_bgnd` の 3 word（CR_PDS_BGRND0・1・3） | `pvr_pds_bgnd_pack_state` |
| `powervr_render_colour_usc` | 三角形の fragment: movi32 で色を temp 0、pixout 0 へ mov（overlap check、end） | `pco_map.py` |
| `powervr_render_vertex_usc` | 三角形の vertex: vtxin 0〜2 を uvsw 0〜2、1.0 を uvsw 3、emit・endtask | `common.cl` の `vs_passthrough_common` |
| `powervr_render_vertex_pds` | 三角形の vertex の PDS（data-program.c の vertex の program、1 頂点 3 word の DMA） | `pvr_pds_clear_vertex_shader_program_init_base` |
| `powervr_render_triangle_ppp` | PPP の state の 19 word（clear の template の color の形: 半透明の三角形、depth は ALWAYS で書かない、PDS の 6 word、region clip なし、wclamp、vtxsize 4 と rhw、varying なし、pretransform、cull なし） | `rogue/pvr_clear.c` の template と `pvr_emit_ppp_from_template`、`pvr_cs_write_load_op_for_view` |
| `powervr_render_triangle_vdm` | VDM の stream の 10 word（PPP_STATE0/1、VDM_STATE0・2〜5、INDEX_LIST0/2 の 3 頂点 TRI_LIST、STREAM_TERMINATE）。cam_size 31・instances 16 は BXE-4-32 の値 | `pvr_pack_clear_vdm_state`、`pvr_arch_calculate_vertex_cam_size` |
| `powervr_render_pbe_words` | PBE の state 2 word と reg 3 word。clip は attachment の extent ∩ renderArea（max は MIN2(edge − 1, offset + extent − 1)）、LINEAR（stride = row_pitch / texel）と twiddled（stride = padded_width、size_x・size_y は log2）、swizzle は image-state.c の format の channels から、sRGB は F16 と gamma、8 bit の unorm は 8_PER_CHANNEL。depth の format と他の幅の format は ENOTSUP（p010g で要れば足す） | `pvr_setup_pbe_state`、`pvr_arch_pbe_pack_state`、`pvr_arch_pbe_get_src_format_and_gamma` |

- 三角形は Mesa の clear の道（`pvr_clear.c`）と同じ形にした: 頂点は pixel の座標（pretransform）、vertex は passthrough、PPP は clear の template。viewport の word は要らない。
- encoder の不足は無かった（emitpix・movi32・mov・uvsw は今の `usc-encode.c` で書けた）。encoder・compiler は変えていない。
- `platform/riscv64/vmunix.mk` に render-program.c を足した（kernel からはまだ呼ばない）。

## 確認（2026-10-20）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| host 試験 | `sh plan/ws269/tests/p010/run.sh OUT`（`render-host-test.c` を足した） | `render-program host test: 26 passed, 0 failed`。他の段（stage 24・image 55・target 77・kick 50・selftest 0 failed・data 83）も PASS |
| USC の bytes | 同上: 各固定の program を、compiler の back end（`shader_backend_group`、p010-compiler の group 試験で Mesa の表と照合済み）で同じ group を作り encode した bytes と比べる。emitpix の byte（0xc2）は Mesa の I_BACKEND の piece から | EOT・clear・colour・vertex の 4 つ一致 |
| M9 の照合（一度だけ、tree に入れない） | Mesa 26.2.5（`build/ws269-p014/mesa/mesa-26.2.5`、p014 で取得済みの tarball）の `csbgen/gen_pack_header.py` を `python3 -I` で走らせ `build/p010e/gen/{vdm,ppp,pbestate,cr,pds}.h` を作り、`build/p010e/m9.c` で zedBSD の word と Mesa の pack を比べる | `M9: 87 words/values compared, 346 fields, 0 mismatches`。内訳: PBE 10 件（R8G8B8A8 の LINEAR 64×64・1920×1080・部分の area、twiddled 64×64・320×240、B8G8R8A8、sRGB 2、R8、R8G8）× 5 word、CR_PDS_BGRND 3、EVENT_PIXEL_PDS 3、PPP 19＋2、VDM 10。生成した header の sha256 の頭: cr `16f0f16f6e450483`、pbestate `f4a38b2818f29fe8`（p010c と同じ）、pds `c145534e2010959a`、ppp `d6ff74caa570dcb4`、vdm `71e7e8fcd9c3b83c` |
| VF2 の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p010e/vf2 vmunix -j16` | rc=0、`warning:` 0 件、render-program.o を link |

- 制限: M9 は「Mesa ならこの field にこの値を入れる」を私が書いて Mesa の pack で詰めた物との比較で、field の配置の誤りは見つかるが、Mesa がどの値を選ぶかの読み違いは見つからない（その値は上の表の出所を読んで決めた）。
- 未実施: QEMU（Venus を通らない kernel の道なので QEMU では確かめられない）と VF2 の実機。実機の確かめは後半（render check）で。

## 残り（後半、p010n の merge の後）

- kick への組み込み（fragment の command の `pbe_word`・`event_pixel_pds_*`・`pds_bgnd`・`pds_pr_bgnd`、geometry の command の VDM の stream の address）と、program・word を GPU の memory（PDSCODEDATA・USCCODE・general の heap）に置く所。render-queue.c・render.c は P1 の q1377 の後に。
- render check（`CONFIG_POWERVR_RENDER_CHECK`）の clear と三角形、実機の `render check: OK`・`ta`・`3d`。
- attachment の load の background（`ditr`・`smp` の USC と texture の state の DMA の PDS、§5.4 の表の 1 行）は前半で作っていない（後半か p010g）。
- depth の ZLS、他の format の PBE（p010g）。
