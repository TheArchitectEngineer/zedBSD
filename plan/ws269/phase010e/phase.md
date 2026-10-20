<!-- awesome-plan project=zedbsd record=ws269-p010e -->

# ws269-p010e: PBE の store と EOT・固定の program・kernel の render check の三角形

- ID: ws269-p010e（親 [ws269](../ws.md)）
- Status: in-progress（q1378-i01 は前半。q1380-i01 で kick への組み込みと render check を実装、host 試験と build まで。実機の `render check: OK`・`ta`・`3d` は未確認、ユーザーの UAT 待ち）
- Disposition: normal
- Queue: q1378 / q1378-i01（2026-10-20 P2、前半）、q1380 / q1380-i01（2026-10-20 P1、後半の kernel 側）
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

## q1381-i01: attachment の load の background（2026-10-20 P2）

`src/drivers/gpu/powervr/render-load-program.{h,c}`（新規。P1 の q1380 の衝突を避けて render-program.{c,h} とは別の file）:

| 関数 | 中身 | Mesa 26.2.5 の出所 |
| --- | --- | --- |
| `powervr_render_load_usc` | load の USC: x_p・y_p（special 97・100）を temp へ、shared 0〜3 の image と 4〜7 の sampler で非正規化の smp、wait、pack U8888（scale）、pixout 0（overlap check、end）。temps 9。4 channel の 8 bit unorm だけ（sRGB 他は ENOTSUP） | `pvr_uscgen_loadop`（rt_load_mask、POS の x・y、image・sampler の state を shared から、attachment の format で store） |
| `powervr_render_load_words` | shared 8 word: attachment の image state（`powervr_view_words`）と point・clamp・非正規化の sampler（dadjust 0、w の wrap なし） | `pvr_load_op_constants_create_and_upload`、`pvr_sampler_state_for_surface` |
| `powervr_render_load_shared` | 8 word を shared 0 へ DMA する PDS（data-program.c） | `pvr_load_op_pds_data_create_and_upload` |

- background の 3 word（`pds_bgnd`・`pds_pr_bgnd`）は今の `powervr_render_background_words`（kick の PDS ＋ この shared の program、shared_registers 8）で作れる。
- 確認: `sh plan/ws269/tests/p010/run.sh OUT` に `render-load-host-test.c` を足して `render-load-program host test: 5 passed, 0 failed`（program を back end の group から作り直した bytes と一致、shared の word、PDS、refusal）。sampler の word は p010f の M9 で Mesa の transfer の sampler と照合済み。VF2 の kernel（`BUILD=build/p010f/vf2 vmunix`）rc=0、warning 0。
- 未実施: 実機。kick への組み込み（load op LOAD の時の background と `pds_pr_bgnd`）は後半。

## q1380-i01（2026-10-20 P1、後半の kernel 側）

main d0d40dd4c を merge（ce7d84418）: P2 の render-program.{c,h} をそのまま残し、P1 の SR・PT の program を `render-sr-program.{c,h}` に改名（include・vmunix.mk・program-host-test・run.sh を直した）。run.sh・ws.md・render-private.h は両方を残した。

作った物:
- `render-command.{c,h}`（新規、host で build）: fragment の command の register（PBE の word（attachment 0）、`pds_bgnd`、`pds_pr_bgnd`、USC_PIXEL_OUTPUT_CTRL（2 register、partition mask 0x3ff）、ISP_BGOBJDEPTH 1.0、ISP_BGOBJVALS（tag・mask）、ISP_AA 0、ISP_CTL（sample_pos・empty tile・pipe 4・multicore の header skip を quirk 72168・72463 の形で）、EVENT_PIXEL_PDS_INFO・DATA）と geometry の command（VDM の stream、PPP_CTRL（wclamp・fixed point 1）、TE_PSG（completeonterminate・region stride）、resume task0 size（shared register の common size）、flags FIRST|LAST）。値は Mesa `pvr_frag_state_stream_init`・`pvr_geom_state_stream_init`・`pvr_arch_setup_tiles_in_flight` と bxe-4-32.h、mainline の partition の大きさから。
- `render-check.c`（`CONFIG_POWERVR_RENDER_CHECK=y` の時）: 自分の session、render context、64×64 の target、GENERAL（VDM stream・PPP・頂点・64×64 R8G8B8A8 LINEAR の attachment）、PDS・USC の窓の buffer に P2 の program（EOT と pixel event、clear の background と shared の色、三角形の colour・vertex・PDS）を置き、P2 の PPP・VDM・PBE の word と render-command で command を組み、`powervr_session_kick_render`（117＋101）→ `powervr_render_finish`（上限 2 秒、`ta`・`3d` の段の行）→ 読み返し。clear 赤 0xff0000ff、三角形 緑 0xff00ff00、頂点 (8,8)(56,8)(32,56)。辺から 1 pixel 程度の pixel は判定しない。
- 出す行（UAT で止まった所が読めるように）: `powervr: render check: prepared …` / `FAILED at <段> (<errno>)`、`kicked (geometry to G, fragment to F)` / `FAILED at kick`、`fence reached (geometry g, fragment f)` / `FAILED at fence (err; geometry g of G, fragment f of F)`、`OK (N pixels judged)` / `FAILED at read back: pixel (x, y) is 0x…, want 0x…`、と `ta`・`3d` の段の行。時間切れの時は session を abandoned にして core に残す（memory も解放しない）。
- `render-target.c`: `powervr_target_layout_of`（tile 数と region header の大きさを command に渡す）。
- `plan/ws269/tests/p010/config-visionfive2-selftest.mk`: `CONFIG_POWERVR_RENDER_CHECK := y`（UAT の試験の image で render check が走る）。
- render context の 1 halt の仮の `pds_pr_bgnd` は、command が background を持たない時の既定として残した（render check は自分の clear の background を `pds_bgnd`・`pds_pr_bgnd` の両方に入れる）。

確かめ（2026-10-20）:

| 確かめ | 結果 |
| --- | --- |
| `sh plan/ws269/tests/p010/run.sh build/p010n/p010` | stage 24/0、image 55/0、target 77/0、kick 81/0、program 38/0、**command 22/0**、selftest 0 failed、data 83/0、render-program 26/0、transfer-program 15/0 |
| p009 | 165 passed, 0 failed |
| M9（tree の外、`build/p010n/m9/compare.c`） | 63 words/values, 197 fields, 0 mismatches（ISP_CTL・USC_PIXEL_OUTPUT_CTRL・ISP_BGOBJVALS・PPP_CTRL・TE_PSG・resume の PDS_STATE0 を足した） |
| VF2・riscv64-virt・amd64 の vmunix | rc=0、warning 0 |
| VF2 render check 付き、試験の image の config（`plan/ws269/tests/p010/config-visionfive2-selftest.mk`、`BUILD=build/p010n/selftest`） | rc=0、warning 0、render-check.o あり |

QEMU・実機は未実施（QEMU に VF2 の GPU は無い）。実機で見る点（ユーザーの UAT の一覧へ）:
- `render check:` の行がどこまで進むか、`ta`・`3d` の段の行。
- SR の PDS の data の H32/L32 の順（p010n の phase.md）: context switch の時の fault の候補。
- ISP_CTL の pipe（tiles in flight 4）・partition mask は Mesa の式で出した値。scissor・depth bias の table は 0（三角形の PPP は使わない前提、推測）。border colour table は 0。
- FRAG_PR は本番と同じ command（EOT も同じ）。PR が起きると clear の background が走り、tile の途中の色は失われる（64×64 では PR は起きない見込み、推測）。
- CDM の cswitch state は範囲外（Q1）。
