<!-- awesome-plan project=zedbsd record=ws269-p010p -->

# ws269-p010p — format の広げ（純粋な関数の側）

- ID: ws269-p010p（親 [ws269](../ws.md)）
- Status: in-progress（2026-10-20 P2 q1389-i01: R32_SFLOAT・R16G16B16A16_SFLOAT など、host 試験・M9・VF2 の build まで。2026-10-21 P2 q1518-i01: raw 64/128 の copy と PBE の F16 を実装。残り: 板での確認（Q1、vkmatrix の copy-64・copy-128・render-f16））
- Disposition: normal

（Q1 2026-10-20: P2 が p010m と付けたが p010m は既に「仕上げと受け入れ」の ID なので p010p に付け直した。）

- 親: [ws269](../ws.md)。Queue: q1389 / q1389-i01（P2、2026-10-20）。状態: in-progress（host 試験・M9 の pack・VF2 build まで。T1 の実機は未実施）。

## 範囲と使われ方

userland を読んだ結果（Keiland・vkdemo）: image は B8G8R8A8_UNORM・R8G8B8A8_UNORM・D32_SFLOAT だけ。vertex attribute は
R32G32/R32G32B32/R32G32B32A32_SFLOAT と、`userland/desktop/wayland/batch.c:300` の **R32_SFLOAT**（表に無く、pipeline が通らなかった）。
sRGB・R8・R8G8・R16 の image は使われていない。このため R32_SFLOAT の追加を最優先にし、他は pack・表の側を先に用意した。

## 変えたもの

- `src/drivers/gpu/powervr/image-state.{c,h}`: `POWERVR_FORMAT_FLOAT` を追加。R32_SFLOAT（tex 18、pbe 0x36、vertex buffer・sampled・transfer）・
  R16G16B16A16_SFLOAT（tex 28、pbe 0x0A、sampled+linear filter・transfer）を追加。R32G32_SFLOAT（tex 34）・R32G32B32A32_SFLOAT（tex 61）に
  texture の code と sampled・transfer の features を付けた。float の format は colour attachment を名乗らない（compiler の pack が U8888 だけ）。
- `transfer-program.c`: `half_from_float`（Mesa `pvr_float_to_f16(value, false)` と同じ、float 演算なし）と、
  `powervr_transfer_clear_colour` の pack を Mesa `pvr_pack_clear_color` に合わせて広げた: R32/R32G32/R32G32B32A32_SFLOAT は raw bits、
  R8・R8G8 unorm・sRGB の2つ・R16G16B16A16_SFLOAT は half（sRGB と F16x4 は2 word）。U8888 unorm は従来どおり。

## 確認

- host: `sh plan/ws269/tests/p010/run.sh build/p010fmt/host` 全 PASS（image 55、transfer 27 ほか 0 failed）。image の「表に無い format」の試験は R16_UNORM に変えた。
  transfer に sRGB・F16x4・R8・R8G8・R32・R32x4 の clear pack の試験を追加。
- M9: Mesa 26.2.5 の `pvr_float_to_f16` の本体を `build/p010fmt/h.c` に抜き出し、`half_from_float` と代表値15個＋0x1235 刻みの全正の float で比較、不一致 0。
- VF2: `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p010fmt/vf2 vmunix -j16` rc 0、warning 0。

## 未実施・残り

- 64・128 bit の raw copy（transfer の program が pixout に n word を移す・`tiles_in_flight` の pixel 幅）、transfer の PBE source format の format 別の選択、
  render-program の `pbe_source` の F16/32-bit 分け、render-load の U8888 以外、float の blit は今回入れていない（ENOTSUP のまま）。
- P1 の file で format を止めている所（今回は触っていない）:
  - `render-job.c` `job_check`: colour が `texel_bytes != 4` なら ENOTSUP。外すには render-program の pbe_source を F16 の format に広げ、
    pixel 幅（output registers）を texel に合わせてから `texel_bytes <= 16` にする。
  - `vk-transfer.c:244`: copy_image で format が違えば ENOTSUP（Vulkan の互換 format の copy）。raw format が 8/16 byte に広がった後、
    `texel_bytes` が同じなら許す形にできる。
  - clear の regs（`powervr_transfer_regs_build`）は U8888 以外の source format を選んでいないため、clear pack が広がっても kick 側はまだ U8888 だけ。
- 片付け（Q1 に依頼）: `build/p010fmt/`。

## q1388-i01（2026-10-20 P1）: gpu-selftest の `present`

- `userland/tests/gpu-selftest/main.c`: `present` の段を作った（`vkdemo --duration=4` を exec、上限は 3 倍）。vkdemo は depth を使うので段の need を `depth` にし、depth が通るまでは `present: skip(depth)` の 1 行。`-d PATH` で vkdemo の場所。
- vkdemo の package を visionfive2 にも、gpu-selftest の依存と試験の image の config（`config-visionfive2-selftest.mk`）に vkdemo。selftest-host.sh に vkdemo の stand-in、期待値を直した（0 failed）。VF2 の userland で vkdemo・gpu-selftest の build rc=0。

## q1518-i01（P2、2026-10-21）: raw 64/128 bit の copy と PBE の F16（R16G16B16A16_SFLOAT の colour attachment）

### raw 64/128 bit の copy（transfer-program.c）

- raw の format を 2 つ足した（Mesa 26.2.5 の `pvr_arch_formats.c`、`texstate.xml`、`pbestate.xml`、`pvr_get_raw_copy_format` に照らした）:
  - `raw_64` ＝ R32G32_UINT（texture U32U32 35、pack mode U32U32 0x28、XY01）
  - `raw_128` ＝ R32G32B32A32_UINT（U32U32U32U32 62、0x0b、XYZW）
  - `powervr_transfer_raw_format` が、8・16 byte の texel にこれを返す。
- copy の program（`powervr_transfer_copy_usc(format, program)`、引数に format を足した）: sample の raw の語を、1・2・4 個、pixel output register 0〜3 へ順に写す（overlap の check 付き）。
- `powervr_transfer_regs_build`:
  - raw の 3 つとも norm を落とす。
  - **pixel の幅**: 128 bit の texel は 4 register（`USC_PIXEL_OUTPUT_CTRL` の width 1、partition は 1024 語で 5、tile は 4 のまま）。64 bit は core の最小の 2 register のまま。Mesa の `pvr_arch_setup_tiles_in_flight` に照らした。
- PBE（render-program.c の `pbe_source`）: 2・4 語の format の source format は 0（Mesa の raw の copy は 0 のまま）。

### PBE の F16（R16G16B16A16_SFLOAT へ描く）

- compiler:
  - shader-spirv-interface.c: `color_format_known` は register の数を返す（8-bit の 2 つは 1、R16G16B16A16_SFLOAT 97 は 2）。half の attachment の色は clamp しない（blend の後も）。PIXEL_LOAD・PIXEL_STORE の immediate_2 ＝ 1 が half。
  - shader-backend.c: PIXEL_STORE は、channel 0・1 と 2・3 を `pck.f16f16`（format 0b01110、rpt 2、scale なし）で 2 つの output register へ詰める。PIXEL_LOAD は `upck.f16f16` で 2 つから戻す。Mesa の `pco_trans_nir.c`（`nir_op_pack_half_2x16`・`unpack_half_2x16`）と `pco_isa.py`（F_PCK_FORMAT f16f16 0b01110）に照らした。
- PBE: `pbe_source` は R16G16B16A16_SFLOAT に source F16_PER_CHANNEL（0）・gamma 0（Mesa の `pvr_arch_pbe_get_src_format_and_gamma`）。pack mode は表の 0x0A。
- image-state.c: R16G16B16A16_SFLOAT の optimal に `COLOR_ATTACHMENT`・`COLOR_ATTACHMENT_BLEND` を足した（`FEATURES_RENDER_F16`）。blit の program は U8888 だけなので、BLIT は名乗らない。
- **P1 の file の変更（最小）**:
  - `render-job.c` の `job_check`: colour が 4 byte の時に加えて R16G16B16A16_SFLOAT を通す（1 か所）。
  - `transfer-job.c`: `powervr_transfer_copy_usc(request->source->format, …)`（引数を足した 1 行）。
- render の LOAD（load の draw）は、attachment の format の key で compile した program が sample して書く。F16 でも同じ道を通る（板で未確認）。

### 確認

| 確認 | 結果 |
| --- | --- |
| p010（host） | 全部 0 failed（transfer-program 32: 64・128 bit の copy の register と program、8-bit の copy の拒否を追加） |
| p010-compiler | 411/0（half の attachment の keyed の fragment 2 つ）、group 22/0（`pck`・`upck` f16f16 の bytes ＝ Mesa の表の 0x9c 0x0e） |
| p009 | 505/0。p008 は 94/0・84/0 |
| vkmatrix（host の lavapipe） | **18/18**（新しい case: `copy-64`・`copy-128`・`render-f16`） |
| VF2 | kernel `build/p2q1518/vf2/vmunix` rc 0・warning 0、userland の vkmatrix rc 0 |
| 板 | 未実施（Q1） |

- 板の期待: `vkmatrix: copy-64: PASS`、`copy-128: PASS`、`render-f16: PASS`、`18/18 passed`。
- 片付け（Q1 に依頼）: `build/p2q1518/`。

## q1520-i01（2026-10-21、診断の build）

- board 97 の copy-64・copy-128・render-f16 は、どれも texel の word 0 は合い、後ろの word が違う（PBE を通る全経路で共通）。
- host 側を読んだ結果: PBE state word 1 の source_pos・source_format・packmode、linestride の単位（pixel）は Mesa の
  `pvr_pbe_setup_codegen_defaults` と同じ形。読むだけでは原因を決められなかったので、診断の build にした。
- vkmatrix: `wide_compare` は、違う texel の数、どこかで違った word の位置の bit mask、最初の違う texel の全 word（seen/wanted）を出す。
  新しい case clear-64・clear-128（vkCmdClearColorImage → copy out）で image→buffer の leg だけを試す。
- 確認: `plan/ws269/tests/vkmatrix/run.sh build/q1520-vkm`（lavapipe）20/20 PASS。VF2 `vmunix` と `bin/vkmatrix`
  （`plan/ws269/tests/p010/config-visionfive2-selftest.mk`、`BUILD=build/q1520/vf2`）rc 0、warning 0。driver の source は変えていない。
- 次: board の出力（word の位置、clear-* の結果）で、壊れる leg（buffer→image／image→buffer／PBE）を決めて直す。

### board 99 と次の診断（2026-10-21）

- board 99: copy-64・clear-64・render-f16 は全 4096 texel で word 1 = word 0、copy-128・clear-128 は (w0 w1 w0 w1)。
  clear も同じなので image→buffer の copy out（copy program の sample か PBE）が第一の容疑。
- 読んだ範囲（PBE state・swizzle・pack mode・pixel output width・partition・texture format・pixout の番号）は Mesa と一致し、原因は決まらない。
- 診断（一時的、transfer-program.c）: copy program が sample の word を逆順で pixel output に書く。読み方:
  - clear-64 が w0 w0 → sample が word 0 を繰り返している。w1 w1 → PBE が pixout0 を繰り返し読んでいる。
  - copy-64（逆順が 2 回）が全部 PASS → 両方とも正しく、clear・upload 側が原因。
- 確認: p010 の host 試験は全部 0 failed。VF2 の vmunix と bin/vkmatrix は rc 0、warning 0。

### board 100 と試み（2026-10-21）

- board 100（逆順の診断）: clear-64 は w1 w1、clear-128 は w3 w2 w3 w2。sample は全 word を出力へ届けている。
  PBE（pixel output）は、64-bit で pixout0 を、128-bit で pixout0・1 を繰り返し読み、与えた幅の半分しか見ていない。
- transfer の register の並び（Mesa の `pvr_submit_info_stream_init`・`rogue_fwif_transfer_regs`）と USC_PIXEL_OUTPUT_CTRL の enum は Mesa と一致する。
  Mesa 26.2.5 の 3D transfer の USC は `pvr_usc_tq` が未実装（finishme）で、実機で確かめた参照が無い。
- 試み: 逆順を外し、wide な texel には書く register の倍の幅を指定する（64-bit → 4REGISTERS、128-bit → 8REGISTERS、partition はその幅で数える）。
  幅が足りているなら、余分な幅は害が無い。
- 確認: p010 の host 試験は全部 0 failed（transfer-program 32 passed、64/128 の register の期待値は 0x1ff9・0x1ffa/0x21000 に更新）。VF2 は rc 0、warning 0。
- board の判定:
  - copy-*・clear-* が PASS → この幅で確定する。
  - 変わらない → 幅以外（PBE の emit・firmware の TQ の扱い）を調べる。
