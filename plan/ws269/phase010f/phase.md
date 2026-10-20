<!-- awesome-plan project=zedbsd record=ws269-p010f -->

# ws269-p010f: transfer（copy・clear）

- ID: ws269-p010f（親 [ws269](../ws.md)）
- Status: in-progress（q1379-i01 は前半。transfer の context・kick・recovery・sync の word への組み込み、kernel の transfer check、実機は後半、p010n の merge の後）
- Disposition: normal
- Queue: q1379 / q1379-i01（2026-10-20 P2、前半）
- 設計: [design-p010.md](../design-p010.md) 版 2.1 の §6・§10 の p010f
- 依存: O-p010c（image-state.c）、O-p010d、O-p010n（context の表・kick の形。前半は依らない）

## 受け入れ条件（design-p010.md §10 の p010f の行）

transfer context とその identity・recovery・sync の word（§6）、IPF の stream を TRANSFER_FRAG に、FAST_2D、PBE の clip（dst の矩形）、奇数の幅は 1 行ずつ、固定の program、`CopyBufferToImage`・`CopyImageToBuffer`（twiddled を含む）・`CopyImage`・`ClearColorImage`、buffer の命令は compute、kernel の transfer check、gpu-selftest の `clear`・`copy`、linear の TRANSFER の features。host 試験（IPF の stream・transfer の register の golden、M9 の照合を IPF に）、build。実機: `transfer` の段、gpu-selftest の `clear`・`copy`。

## q1379-i01 の範囲（前半、Q1 の依頼）

transfer の command の組み立て（copy と clear、src/dst の矩形と PBE の clip、固定の program）を lock・memory を持たない純粋な関数で。触らない: kick・context の作成・render-queue.c・render.c・render-kick-layout*.c・render-private.h（P1 の q1377）。

## 設計からの変更（Q1 の確認が要る）

- **IPF・FAST_2D ではなく、FAST_SCALE と背景の object にした。** Mesa 26.2.5 の `pvr_arch_job_transfer.c` を読むと、普通の copy・clear（1 つの source、矩形 1 つ、custom mapping なし）は `pvr_3d_copy_blit` → `pvr_3d_copy_blit_core` の道で、`CR_ISP_RENDER` は FAST_SCALE、copy は ISP の背景の object（`CR_PDS_BGRND*`、`ISP_BGOBJVALS.enablebgtag`）が転送の program を全 tile に走らせ、clear は object なしで `USC_CLEAR_REGISTER` の値を PBE が書く。IPF の control stream（`pvr_isp_ctrl_stream`）と FAST_2D は `pvr_3d_clip_blit` の道だけで、custom mapping（`double_stride` など）・複数の mapping・DS merge で使う。`pvr_blit.c` は FAST2D の flag を立てない。design-p010.md §6 の「copy・clear で FAST_2D」は Mesa の実際の道と食い違うので、Mesa の道に従った。IPF の stream と TRANSFER_FRAG の heap は今は使わない（`isp_mtile_base`・`isp_rgn` は 0）。設計の §6 の文面の直しは Q1 に。
- **奇数の row length** は design のとおり 1 行ずつの job（`powervr_transfer_needs_rows`、組み立ては EINVAL で断り、呼び手が行に分ける）。
- **EOT** は transfer 専用の DOUTW の program（Mesa の `pvr_pbe_setup_emit` は PBE の state を shared へ DOUTW して shared の state を読む EOT）ではなく、p010e の即値の EOT（`powervr_render_eot_usc`）と fixed area の +128 の code を使う形にした（job ごとに EOT の USC を置く。結果は同じで道が 1 つで済む）。

## 作った物（2026-10-20 P2）

`src/drivers/gpu/powervr/transfer-program.{h,c}`（新規、host でも同じ file）:

| 関数 | 中身 | Mesa 26.2.5 の出所 |
| --- | --- | --- |
| `powervr_transfer_regs_build` | transfer の command の register 一式（`struct powervr_transfer_regs`、FW の transfer_regs の並び: pds_bgnd 3、isp_mtile_base、pbe 9、isp_bgobjvals、usc_pixel_output_ctrl、usc_clear 4、isp_mtile_size、isp_render_origin、isp_ctl、isp_aa、event_pixel_pds_info/code/data、isp_render、isp_rgn、frag_screen）と EOT の PBE の state 2 word | `pvr_3d_copy_blit_core`、`pvr_setup_hwbg_object`、`pvr_pbe_setup_codegen_defaults`（clip は dst の矩形、tile の origin・数は clip から、transfer は packed の USC channel なし = 8 bit unorm は F16）、`pvr_isp_tiles`、`pvr_arch_setup_tiles_in_flight`（BXE-4-32: 10 partition、4 tile in flight、pipe_enable 3、2REGISTERS、partition mask 0x3ff）、`pvr_submit_info_stream_init` の並び |
| `powervr_transfer_copy_usc` | copy の USC: x_p・y_p（special 97・100）を temp へ、shared の 4 float を temp へ、fmad で source の非正規化の座標、smp（非正規化、2D、4 channel）、wait、最初の raw word を pixout 0（overlap check、end）。temps 12。group は compiler の back end（`shader_backend_group`）、smp だけ normalised 0 で作り直す | `pvr_uscgen_tq`・`_frag_coords`（FLOATS_4、iterated なし）・`_frag_load`（nncoords、uint32）、`pco_trans_nir.c`（POS の x・y は x_p・y_p） |
| `powervr_transfer_shared_words` | copy の shared 12 word: source の image state（raw の R32_UINT として）、sampler（point、clamp、非正規化、dadjust 0、w の wrap なし）、float（scale 1、offset = source − dst の角） | `pvr_image_state_set_codegen_defaults`、`pvr_sampler_state_for_surface`、`pvr_uv_space`、`pvr_3d_copy_blit`（同じ format の point の copy は 32 bit を R32_UINT の raw で） |
| `powervr_transfer_shared_program` | 12 word を shared 0 へ DMA する PDS（data-program.c） | `pvr_3d_copy_blit_core` の texture DMA |
| `powervr_transfer_clear_colour` | clear の色の pack（4 channel の 8 bit unorm、float の bit から整数だけで floor(v·255+0.5)、NaN・∞・1 以上は 255、負は 0） | `pvr_pack_clear_color`・`pvr_float_to_ufixed` |
| `powervr_transfer_raw_format`・`powervr_transfer_needs_rows` | raw の 32 bit format（tex U32 24、PBE U32 0x34、X001）、奇数の行の判定 | `pvr_arch_formats.c` |

- copy の dst も raw の R32_UINT で書く（Mesa と同じ。PBE の norm は 0、source は 8_PER_CHANNEL）。
- `render-program.h` の `POWERVR_RENDER_USC_MAX` を 96 → 256（copy の program が 120 byte）。
- `platform/riscv64/vmunix.mk` に transfer-program.c（kernel からはまだ呼ばない）。

## 確認（2026-10-20）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| host 試験 | `sh plan/ws269/tests/p010/run.sh OUT`（`transfer-host-test.c` を足した） | `transfer-program host test: 15 passed, 0 failed`。同じ run.sh の stage 24・image 55・target 77・kick 50・selftest 0 failed・data 83・render 26 も PASS |
| M9 の照合（tree に入れない） | `build/p010f/m9.c`（Mesa の `gen_pack_header.py` を `python3 -I` で走らせた `build/p010f/gen/{cr,pbestate,texstate}.h`） | `M9: 77 words/values compared, 183 fields, 0 mismatches`。clear 3 件・copy 3 件（linear 64×64・1920×1080、twiddled の内側の矩形・320×240）の ISP_RENDER・ORIGIN・MTILE_SIZE・BGOBJVALS・ISP_CTL・PIXEL_OUTPUT_CTRL・FRAG_SCREEN・ISP_AA・PDS_BGRND 3・USC_CLEAR_REGISTER・PBE の state word 1 と reg word 1、transfer の sampler の 2 word、float 3 |
| VF2 の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p010f/vf2 vmunix -j16` | rc=0、`warning:` 0 件、transfer-program.o を link |

- M9 の限界は p010e と同じ（Mesa が選ぶ値の読み違いは見つからない）。照合の途中で、私の Mesa 側の期待値の誤り（twiddled の size_x/y、raw の norm）と、実装の誤り（sampler の dadjust・addrmode_w を Mesa の transfer の値に、raw の norm を 0 に）を直した。
- raw の image state は p010c の view の word（M9 済み）の format の code だけが違う物で、M9 は形式の code の位置（27〜33 が 24）を host 試験で見ただけ。
- 未実施: QEMU（Venus を通らない kernel の道）と VF2 の実機。

## 残り（後半、p010n の merge の後）

- transfer の context（FW の context の作成、identity・recovery の表に transfer の行、kick の sync の word、`transfer: fail(timeout)`）と kick への組み込み（register の block を FW の transfer の command に、program と word と shared の 12 word を GPU の memory に）。
- vkCmdCopyBufferToImage・CopyImageToBuffer・CopyImage・ClearColorImage を vk-command.c から job に（奇数の行は 1 行ずつ）、buffer の命令は compute、linear の TRANSFER の features、kernel の transfer check、gpu-selftest の `clear`・`copy` の実機。
- 8 bit・16 bit の texel（R8・R8G8 の raw の R8_UINT・R8G8_UINT）、sRGB の clear（F16 の pack）、64・128 bit の texel は ENOTSUP のまま（要れば足す）。
- design-p010.md §6 の FAST_2D の文面（上の「設計からの変更」）。

## q1381-i01（2026-10-20 P2）: 版 2.2 の注記・clear の source format の直し・p010o の blit の組み立て

- **design-p010.md 版 2.2**: §6 に「transfer は FAST_SCALE と背景の object、IPF・FAST_2D は clip blit の道だけ、奇数の row length は 1 行ずつ、EOT は p010e の即値の形」の注記と、§13.1 の表を足した（Q1 が 2026-10-20 に採用）。
- **直し（q1379-i01 の誤り）**: transfer の PBE の source format を「8 bit unorm は F16」にしていたのは誤り。Mesa の `pvr_pbe_setup_swizzle` が R8G8B8A8・B8G8R8A8 unorm の fill と F16_U8 の pixel（copy・blit）を 8_PER_CHANNEL に戻すので、render と同じ 8_PER_CHANNEL のままにした（直しの前は M9 の私の期待値も同じ誤りで一致していた）。clear の state word 1 は 0x82000。
- **p010o の blit**（[ws.md](../ws.md) の p010o の前半、この file に記録）:
  - `POWERVR_TRANSFER_BLIT`: copy と同じ FAST_SCALE と背景の object で、dst は raw でなく自分の format（PBE の norm 1、8_PER_CHANNEL）。
  - `powervr_transfer_blit_usc`: copy の program の smp の後に pack U8888（scale）を挟む（F16_U8、`nir_pack_unorm_4x8`）。temps 13。4 channel の 8 bit unorm だけ。
  - `powervr_transfer_blit_words`: source の矩形・dst の矩形・flip x/y・linear から shared 12 word。float は Mesa の `pvr_dma_texture_floats`（FLOATS_4）の比 src/dst と (src.x·dst − src.w·dst.x)/dst（flip は dst の大きさを負に、遠い端を使う）を、整数だけで最近接偶数に丸めた float（`float_from_ratio`）。大きさが同じなら point（`pvr_transfer_set_filter`）。copy の float も同じ関数に寄せた（値は前と同じ）。
- 確認: host 試験 `transfer-program host test: 22 passed, 0 failed`（blit の word: scale 0.5・flip・1/3 の丸め・同じ大きさの point、blit の program、blit の register）。M9（`build/p010f/m9.c`）`M9: 2104 words/values compared, 2254 fields, 0 mismatches`（前の 77 に、500 件の乱数の矩形の scale・offset を host の float の割り算と比べた 2000 値、linear の sampler、blit の register 2 件）。VF2 の kernel rc=0、warning 0。
- 残り（p010o の後半）: vkCmdBlitImage から job へ（p010f の後半の組み込みの後）、sRGB・他の format、3D の source。
