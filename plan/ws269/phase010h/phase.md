# ws269-p010h: descriptor と compiler の境（driver 側）

- ID: ws269-p010h（親 [ws269](../ws.md)）
- Status: in-progress（実装と host 試験は済み。Q1 の判定待ち。実機・T1 の試験は p010 の最後にまとめる）
- Disposition: normal
- Queue: q1374 / q1374-i01（2026-10-20 P2）
- 設計: [design-p010.md](../design-p010.md) 版 2.1 の §5（5.1・5.2）・§10 の p010h
- 依存: O-p010c（image の state の word、descriptor の大きさ）、O-SHADER-ABI（`shader.h`、main に merge 済み）

## 受け入れ条件

- descriptor set の memory の並び（buffer 16・image 32・sampler 48・combined 80 byte、image の meta 4 word と PCK_INFO、sampler の meta と gather の word）。
- `shader.h` の要求の表（SET_BASE・DESCRIPTOR・PUSH_CONSTANTS・LITERAL・WORKGROUP_ID/NUM_WORKGROUPS・BLEND_CONSTANTS、vertex_index_reg・instance_index_reg）から shared register を埋める PDS の data の program と、vertex の属性の DMA の program。
- 値と手順は Mesa 26.2.5、出典は使う block の直前の comment。HAL の API は変えない。
- build warning 0（VF2・riscv64-virt・amd64）、host 試験（descriptor の bytes、PDS の program の bytes を Mesa の表から作った期待と、compute の道の回帰）。

## 実装（2026-10-20 P2）

kernel（`src/drivers/gpu/powervr/`）:
- `descriptor-set.h`・`descriptor-set.c`（新規、lock も memory も持たず host で build できる）: `powervr_set_layout_build`（binding の番号の順に詰める、count 0 は場所なし、dynamic の buffer は ENOTSUP = p011）、`powervr_set_descriptor_offset`（buffer の address・image の state・sampler の state の場所、combined は sampler が 32 byte 先）、`powervr_pack_info`（R8・RG8・RGBA8 UNORM は U8888 | scale | unorm = 0x140、sRGB・BGRA・D16 は invalid | unorm = 0x11f、他は 0x1f）、`powervr_descriptor_buffer`・`_image`（meta: layer size・buffer elems 0・z slice 0・pck info）・`_sampler`（compare NEVER と reserved 3、gather の word は filter を linear・mip なしに）。出典: `pvr_descriptor_set.c` の `pvr_CreateDescriptorSetLayout`、`pvr_arch_descriptor_set.c` の `write_*`、`pvr_common.h` の struct、`pvr_arch_tex_state.c` の `setup_pck_info`、`pco.c` の `pco_pipe_to_pck_format`、`pvr_arch_sampler.c` の gather、`u_format_parse.py` の `is_unorm`。
- `data-program.h`・`data-program.c`（新規、host で build できる）:
  - `powervr_data_resolve`: 要求の表と bound の状態（set の layout と device address、push constant・blend constant の buffer の address、workgroup の id と数）から load の列。SET_BASE・LITERAL・WORKGROUP は DOUTW の write（偶数の register へは 2 word の組、奇数は 1 word ずつ）、DESCRIPTOR・PUSH・BLEND は set・buffer からの DOUTD の DMA。
  - `powervr_data_shared_build`: Mesa の `pvr_pds_generate_descriptor_upload_program` の形（64 bit の constant を先に、32 bit を後に、最後の DMA に LAST_EN と end、空なら halt）。DOUTD の control は size・store address・cached・common store、DOUTW の control は common store・address・write mask。最大 42 load（32 bit の constant の番号が 127 まで）。
  - `powervr_data_vertex_build`: Mesa の `pvr_pds_generate_vertex_primary_program`（robust・indirect・base instance なし）の形。constant 0・1 に DOUTU の task word、属性ごとに 8 word（stride を +3、address を +4、DDMADT の test なしの control を +6・+7）、index の temp 0 から DDMAD、vertex index は 64 bit temp 0 の下半分・instance index は上半分を DOUTW（control は `pvr_find_constant` と同じ最初の空き）、最後に LAST_EN、DOUTU（cc・end）、halt、data の大きさは使った constant から。per instance の属性は範囲外（p011）。
- `vk.h`・`vk-layout.c`: set layout が binding の場所（`struct powervr_set_layout`）を持つ。set は layout の大きさの bytes（CPU の写し）を持ち、`vkUpdateDescriptorSets` の write が buffer（VK_WHOLE_SIZE は buffer の終わりまで、null は 0）、sampled image・sampler・combined の descriptor を書く。compute の道の slot（storage buffer の保持）は今のまま。
- `platform/riscv64/vmunix.mk`（共有の file、Q1 の merge で確認を）: `descriptor-set.c`・`data-program.c`。HAL・toolchain は変えていない。

決めた事・Mesa との違い:
- Mesa は set の使う範囲を 1 回の DMA で shared へ写し compiler が shared から読むが、zedBSD の compiler の要求は descriptor の part ごと（design §5.2）なので、要求ごとに DMA する。DMA の control の形は Mesa と同じ。
- **set の memory は CPU の写し**（`set->bytes`）。core の memory への置き場所（heap の割り当て）は P1 の p010d の API が main に入ってから p010i で合わせる（draw の submit の時に executor の領域へ写すか、set を heap に置く）。衝突を避けるため今は heap に触れていない。
- **compute の inline の焼き込みは置き換えていない**。理由: zedBSD の executor は workgroup ごとに 1 つの CDM の kernel を出し、compiler は WORKGROUP_ID を shared register で頼む。Mesa の形（usc_common_shared の shared update の kernel を 1 つ出し、後の kernel が共有する）に変えると workgroup ごとに shared update と kernel の組が要り、共通の store の受け渡しの順は実機で確かめていない。実機なしに既に動いている compute の道を変える危険が大きいので、resolve・shared の program は用意だけし、compute の置き換えは workgroup id を PDS で temp に書く形（Mesa の `pvr_pds_compute_shader` の work_group_input_regs）と合わせて、実機で確かめられる時に行う（残り）。

## 確認（2026-10-20 P2、host のみ。QEMU は起動していない）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| descriptor と PDS の program | `sh plan/ws269/tests/p010/run.sh build/p010h/p010` | `data-host-test: 83 passed, 0 failed`（layout の場所と拒否、pack info 8 format、buffer・image・sampler の bytes と gather、resolve 11 load、shared の program の data 33 word・code 11 word を Mesa の DOUT・DOUTD・DOUTW の field から組んだ期待と比べる、空は halt、vertex の program の DDMAD・DOUTW・DOUTU・halt と data の大きさ 16・8・3）。同じ run の `stage-host-test`・`image-host-test`・`selftest-host` も PASS |
| compute の道の回帰（executor） | `sh plan/ws269/tests/p009/run.sh build/p010h/p009` | `powervr executor host test: 162 passed, 0 failed`（compute の frame の後、set の 16 byte の buffer descriptor が buffer の address と大きさを持つことを足した） |
| VF2 の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p010h/visionfive2 vmunix -j16` | rc=0、`warning:`・`error:` 0 件 |
| riscv64-virt | 同 `config-riscv64-virt.mk BUILD=build/p010h/riscv64-virt` | rc=0、0 件 |
| amd64 | 同 `config-amd64.mk BUILD=build/p010h/amd64` | rc=0、0 件 |
| `git diff --check` | | 空 |

Mesa の source: `https://archive.mesa3d.org/mesa-26.2.5.tar.xz`（sha256 `794b3d17…a0a5`、p010b・p010c と同じ）を `build/p010h/dl/` へ、`src/imagination` と `src/util/format` だけを `build/p010h/src/` へ展開して読んだ（tree に入れない）。

未実施: 実機（VF2）。PDS の program が core の上で shared・vertex input の register を埋めることは、p010i の draw（gpu-selftest の `draw`・`sample`）で初めて確かめられる。

## 残り

- p010i: set の bytes を core の memory に置き（P1 の heap の API）、`powervr_data_resolve`・`_shared_build`・`_vertex_build` を graphics の pipeline と draw に繋ぐ。push constant（`vkCmdPushConstants` の記録）と blend constant の buffer の置き場所も p010i。
- compute の inline の焼き込みの置き換え（上の理由、実機の確かめと一緒に）。
- dynamic の buffer・per instance の属性・robust の bounds は p011。
- 片付け（Q1 に path を渡す）: `build/p010h/`（Mesa の tarball・展開、host 試験・build の出力）。
