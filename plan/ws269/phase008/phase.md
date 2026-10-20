<!-- awesome-plan project=zedbsd record=ws269-p008 -->

# ws269-p008 Rogue の USC・PDS の encoder、kernel の中の試験の道、data master の道の coherency

- 親: [ws269](../ws.md)、設計: [design.md](../design.md) 改訂 3.1 の §5（D6 A: 自前の USC の backend）・§6 の p008・§3.2 の 4（data master の道）
- Queue: q1345 / q1345-i01（P2、2026-10-20、前半: encoder と host 試験）、q1349 / q1349-i01（P2、2026-10-20、後半: kernel の中の試験の道と data master の道の coherency の code）
- Status: in-progress（2026-10-20 P2: 前半・後半の code、host 試験、VF2 の build まで。実機の 3 行は U29（firmware 入りの image）の続きで未実施。clearance は Q1 の判定）
- 前提: p006（実装済み・実機待ち）。HAL の API は変えない。toolchain は変えない。

## 目的と受け入れの条件

前半（q1345、この記録の範囲）:

1. Rogue の USC（unified shading cluster）の命令 group と各 phase の operation の encoder。BXE-4-32 の feature に合わせる。
2. PDS（programmable data sequencer）の命令と、その出力命令が取る data の word の encoder。BXE-4-32 の feature（PDS_DDMADT あり、SLC_MCU_CACHE_CONTROLS なし）に合わせる。
3. host 試験: Mesa の disassembler などに頼らず、Mesa の field の表から手で組んだ期待の bit 列と比べる。
4. 関係する platform（VF2）の build が warning 0。QEMU は起動しない。

後半（q1349、code まで。判定は実機）:

5. kernel の中の試験の道: 前半の encoder で compute の USC・PDS の program を組み、p006 の compute の FW の context に CDM の kick を出し、buffer を読み戻して期待値と比べ、画面に 1 行（Venus を通さない）。
6. GPU の data master の道の coherency の確かめ（§3.2 の 4 と同じ手順: 0b の対照、64 KiB × 8 回、2 MiB の中で 128 KiB ずつずらす、SLC の flush を入れない、判定は cacheable の view だけ、INCONCLUSIVE なら切り替えない）を compute の書き込みで流し、1 行を出す。
7. DOUTU の temps の単位（4 か 2）を実機の結果から判別できる形にする。
8. 製品の起動を遅くしない（試験の kernel の時だけ有効）。HAL の API は変えない。host 試験（組んだ program の bit 列）。VF2 の build が warning 0。

## 置き場所（決めた事）

Pi 4 の compiler（`src/drivers/gpu/bcm2711/qpu.c`・`shader*.c`、WS141）は kernel の driver の中にあり、kernel の Venus の decode（`vulkan-*.c`）が SPIR-V を native の命令に落とす。WS269 も D6 A で同じ形（kernel の powervr が Venus を decode し、USC・PDS の backend を自前で持つ）なので、encoder は **`src/drivers/gpu/powervr/` に置く**:

| file | 中身 |
| --- | --- |
| `src/drivers/gpu/powervr/usc-encode.h`・`.c`（新規） | USC の命令 group の encoder（`powervr_usc_program_encode`・`powervr_usc_group_size`）と、各 phase の operation の encoder（main の unit の float の加算・乗算、fused multiply-add、単項（rcp・rsq・log・exp・byp・微分など）、pack、32/64 bit の整数の加算・乗加算、test、条件付き move。back end の load・store（burst の即値と register）、data fence、atomic、vertex の出力（write・emit・cut・endtask）。bitwise の unit の phase 0（count・shift・即値 16/32 bit）・phase 1（論理）・phase 2（shift と test）。control の unit の branch、条件実行、mutex、nop、field の無い操作（wop・wdf・lapc・savl）、shared base） |
| `src/drivers/gpu/powervr/pds-encode.h`・`.c`（新規） | PDS の命令（add32・add64・shift と論理 32 bit・compare（register・即値）・branch・load・store・wdf・limm・halt・nop・DMA の乗加算（ddmad）・data の出力（dout））と、data の word（shading の task の issue の 64 bit、DMA の address の 64 bit と control の 32 bit、ddmad の control の 64 bit） |
| `platform/riscv64/vmunix.mk` | `CONFIG_DRIVER_POWERVR` の時に上の 2 file を build |
| `plan/ws269/tests/p008/`（新規） | `run.sh`・`encode-host-test.c` |

encoder は libc に依らず（`stdint.h`・`stddef.h`・`uapi/errno.h` だけ）、p006 の `gpu-mmu.c` と同じく host でも build できる。compiler（共有の IR からの選択・register の割当・scheduling）は p009 以降で、この encoder を使う。

## 設計

- **group の組み立て**: compiler は group ごとに unit（main・bitwise・control）、条件、end・atomic・repeat・overlap check・write enable、phase ごとの operation の bytes、source 6 個（下 3 個・上 3 個）と source selector（is0）、内部の feed-through の selector（is1〜is5）、destination 2 個を渡す。encoder は source・destination の word の形（1〜6 byte の 8 形・1〜4 byte の 5 形）を、Mesa と同じ順で「全部の register を名付けられる最初の形」として選び、header の length（half-word 数、16 は 0）と decode assist を埋め、word の padding（0xff）を足す。並びは header、operation（後の phase から: back end、phase 2、phase 1、phase 0）、下の source、上の source、内部 selector（main だけ）、destination、padding。
- **main・bitwise の group は使わない source の半分も 1 byte で持つ**、control の group は使わない半分を持たない（Mesa の `pco_igrp_src_variant` の通り）。main の phase の組（oporg）は表にある 8 通りだけを受け、他は EINVAL。
- **program の alignment**: 最後の group を 8 byte の境界で終わるように、最後の 1 つ前の group を最後の group が 8 byte の境界で始まるように pad する（0xf0 | half-word 数、続けて 0xff）。各 group の length は pad を含む。
- **2 byte の main の header は出さない**: ISA には end・atomic・repeat の無い 2 byte の main header（ext = 0）があるが、Mesa 26.2.5 は repeat の数を常に 1 以上で持つ（`pco_ops.py` の `OM_RPT` nzdefault 1）ので実際には出さず、hardware での意味が確かめられていない。encoder は常に 3 byte の header を使う。
- **control の操作の番号**: header の ctrlop は定義のある 0〜8・11・12 だけを受ける。operation を作るのは branch・cnd・mutex・nop・sbo と field の無い物（wop・wdf・lapc・savl）。ditr・itrsmp（graphics の iterator、p010）は作っていない。
- **back end**: tiled の store（4 byte）と sample（smp）、fitr・phas・setl・vistest・msk・emitpix は p010（graphics）で足す。
- **範囲の検査**: 各 field の幅を超える値、未定義の code（atomic の 3、logical の 3、test の 10 以上・type の 7、cache mode の 3、data size の 3、内部 selector is3 の 2 など）は EINVAL で、出力の bytes は意味を持たない。

### BXE-4-32 の feature

| feature（Mesa `bxe-4-32.h`） | encoder への影響 |
| --- | --- |
| `has_pds_ddmadt` あり | ddmad の control の 64 bit の上半分（bounds test と limit、`pds.xml` の `DDMAD_FIELDS_SRC3` の msize 33..63・test 32）を作る `powervr_pds_dma_add_control_word` |
| `has_slc_mcu_cache_controls` なし | DOUTD の address の word の SLC mode の field は使わない（常に 0） |
| `has_compute` あり | dout の宛先 DOUTC（5）を受ける |
| `has_usc_itrsmp`・`_enhanced` あり | itrsmp は p010 で（今回は作らない） |

### Mesa の中の食い違い（記録、今回の encoder には影響しない）

- DOUTU の temps の単位: `pds.xml` は `UNIT_SIZE 2`、`pvr_rogue_pds_defs.h` は `_TEMPS_ALIGNSIZE 4`。実際に使われる `pvr_pds.c` の `pvr_pds_setup_doutu` は 4 で割る。encoder は 4 の単位（`pvr_pds.c` の手順）。実機で確かめる（p008 後半の compute の kick）。
- DOUTD の SLC mode の field: `pds.xml` は bit 60..61、`pvr_rogue_pds_defs.h` は shift 62。BXE には無い field なので使わない。

## 後半の設計（q1349）

### 置き場所と有効にする条件

| file | 中身 |
| --- | --- |
| `src/drivers/gpu/powervr/compute-program.h`・`.c`（新規、host でも build できる） | driver 自身の compute の program: 書き込みの USC の program（`powervr_compute_store_program`）、temps の単位の USC の program（`powervr_compute_temps_program`）、task を出す PDS の program（`powervr_compute_kick_program`）と stream の終わりの fence の PDS の program（`powervr_compute_fence_program`）、CDM の control stream の kernel の 7 語（`powervr_compute_kernel_words`）と終端の語、FW の compute の命令（`powervr_compute_command`、96 byte） |
| `src/drivers/gpu/powervr/compute-check.c`（新規、試験の kernel だけ） | `powervr_compute_check`: session を開き、buffer を写し、program を書き、kick して読み戻し、画面に 3 行 |
| `src/drivers/gpu/powervr/usc-encode.h`・`.c` | `powervr_usc_program_build`（group を callback で 1 つずつ受ける形。2,058 group の program を配列なしで組む）。`powervr_usc_program_encode` はその包み |
| `src/drivers/gpu/powervr/render-queue.c`・`render-private.h` | `powervr_session_kick_compute`（CDM の命令と timeline の update を client CCB に置いて kick）、`powervr_session_wait`（render.c の `wait_timeline` を公開にした物）、`powervr_compute_check` の weak の宣言 |
| `src/drivers/gpu/powervr/render.c` | self-test の後、node の登録の前に `powervr_compute_check` があれば呼ぶ（weak、通常の kernel では NULL で何もしない） |
| `src/drivers/gpu/powervr/interface.h` | `POWERVR_CLIENT_COMPUTE`（205 \| TASK \| magic）、命令の 96 byte と stream・resume の位置 |
| `platform/riscv64/vmunix.mk` | `compute-program.c` を常に（powervr と一緒に）、`compute-check.c` を `CONFIG_POWERVR_COMPUTE_CHECK=y` の時だけ |
| `plan/ws269/tests/p008/config-visionfive2-compute.mk`（新規） | 試験の kernel の構成（CI の VF2 の構成 + `CONFIG_POWERVR_COMPUTE_CHECK := y`）。WS256 p023 の `vf2-dmatest` と同じ流儀 |
| `plan/ws269/tests/p008/compute-host-test.c`（新規）・`run.sh` | 組んだ program の bit 列の host 試験 |

通常の image（`config/ci/config-visionfive2.mk`）は check を含まず、起動の時間は変わらない。

### program（Mesa 26.2.5 の手順を Procedure referenced で採った）

- **CDM の control stream**（1 kick に 1 本、15 語）: 主の kernel（KERNEL0・1・2・3・4・5・8、間接と global offset なし、`pvr_compute_generate_control_stream` の順）、fence の kernel（`pvr_arch_compute_generate_fence`: fence 1、sd_type PDS、usc_common_shared 1、usc_target ANY、1 instance、max_instances 8（`pvr_compute_flat_slot_size(0, false, 1)`、BXE に BRN 49032 なし））、STREAM_TERMINATE。主の kernel は workgroup 1 つ、大きさ 32（1 task = 32 instance、max_instances 0 = 32）、usc_target ANY、sd_type NONE、共有 register なし、pds_temp_size は compute の入力の 3 register 分（`PVR_PDS_NUM_COMPUTE_INPUT_REGS`）、usc_unified_size は temps × 4 byte の 16 byte 単位。
- **PDS**（data 2 語 + code、heap の中の 64 byte の slot）: 主は `pvr_pds_compute_shader` の kick_usc だけの形: `BRA IF1 neg → 2`（係数の更新の pass を飛ばす）、`HALT`、`DOUTU end`（data の 64 bit の task の語）、`HALT`。fence は `BRA`、`HALT`、`ADD64 ptemp64[0] = 0 + 0`（clear_pds_barrier）、`DOUTC`、`HALT`。Mesa は compute の DOUTU を cc=1 で出すが、ここは cc=0（無条件）にした（cc=1 の述語の状態に依らないため）。
- **USC の書き込みの program**（7 temps、2,058 group、約 20 KiB）: r5 = sr51（instance 番号、`load_instance_num_pco`）、r0 = base の下位 + 4 × r5、r1 = base の上位、r2 = r0（data = その語の device address の下位 32 bit）、r3 = 128、r4 = 1。512 回: `st r2 → [r0:r1]`（burst 1、32 bit、write-through、drc 0。Mesa の `trans_store_global` と同じく address の対の直後の register に data）、`wdf drc0`（`pco_schedule` の通り）、r0 += 128、r2 += 128（madd32 r × 1 + r3）。最後に `nop` に end（`pco_end`）。32 instance が 32 語おきに書き、64 KiB の窓を埋める。下位 32 bit だけ進めるので、窓が 4 GiB の境を跨がない事を build の時に確かめる。
- **USC の temps の program**（8 temps、13 group）: r7 = sr51 を最初に置き、r3〜r6 に別の値を入れ、r0・r1・r2 で address を作り、r2 = r7 を `base + 4 × instance` に書く。
- **FW の命令**: `rogue_fwif_cmd_compute`（96 byte）の control stream の base（40）と context の resume の area（48、4 KiB の stream の buffer の先頭）だけを入れ、他は 0（border colour の table、CDM の item、cluster、tpu_tag、flags、execute_count）。client CCB に CDM の命令（205 \| TASK）と timeline の update を置き、KCCB の kick を出す（`pvr_queue_submit_job_to_cccb` の順）。

### 写し方

session の GPU の address に直に写す（`powervr_gpu_mmu_map`、写した後に MMU の cache の flush、閉じる前に unmap と flush）。属性は全部「CPU が見る buffer」の既定（`powervr_gpu_buffer_flags(1, 0)` = SLC_BYPASS、CC=0）:

| buffer | GPU の address | 大きさ |
| --- | --- | --- |
| 書き込みの run | general heap + 1 MiB | 2 MiB |
| control stream と resume の area | general heap + 4 MiB | 64 KiB |
| PDS の program | PDS heap + 64 KiB（KERNEL1・2 の offset は heap の中） | 4 KiB |
| USC の program | USC heap + 64 KiB（DOUTU の address は heap の中） | 9 × 32 KiB |

### 試験の流れと画面の 3 行

1. **kernel の中の試験の道**: 窓 0 を別の値で埋め、窓 0 の program を kick し、uncached の別名で 16,384 語を数える。
2. **data master の道の coherency**（1 が全部書けた時だけ）: coherency.c と同じ 0b の対照（CPU だけで古い line を見せられるか）、8 回の round（各 round の窓を別名で `before` = 期待値の反転で埋め、cacheable の view を invalidate してから読んで clean の line を載せ、その窓の program を kick、SLC の flush なし、cacheable の view で一致・古い値を数え、別名で届かなかった語を数える）。判定の語は coherency.c と同じ（COHERENT は 8/8 かつ control OK の時だけ。INCONCLUSIVE・MATCHED WITHOUT CONTROL・NOT COHERENT では切り替えない。切り替えは別の Phase で Q1 の承認）。
3. **temps の単位**: 同じ temps の program を DOUTU の temps の field = 2（4 の単位で 8 register）と 4（2 の単位で 8 register）で 2 回 kick。2 の単位なら field 2 は 4 register しか取らず、instance の register が隣り合って並ぶ時は隣の instance の r3 の値が r7 に入り、語が instance 番号でなくなる。field 4 は単位がどちらでも足りるので、これが失敗したら試験そのものが壊れている。

kick の待ちは 1 回 2 秒。時間切れの時は buffer と session を GPU に残して止める（後で書かれ得るので解放しない）。GPU の page fault は p006 の fault の道（render の node の quarantine）に入る（試験の kernel だけ）。

### 実機の試験の文面（T1・ユーザー向け、beta2 の U29 の続き）

image: `make ZEDBSD_CONFIG=plan/ws269/tests/p008/config-visionfive2-compute.mk BUILD=build/uat/vf2-compute -j16 build/uat/vf2-compute/native.img`（U29 と同じく firmware の LICENSE.powervr の同意の後。通常の U29 の image では下の 3 行は出ない）。起動は U27・U29 と同じ。U29 の p006 の 3 行の `render node ready` の直前に次の 3 行が出る:

| 行 | 期待 | 読み方 |
| --- | --- | --- |
| `powervr: compute kernel: OK, 16384/16384 words written` | OK | `FAILED, <n>/16384 words written`（program か kick の誤り）、`FAILED (<番号>)`（42 = 時間切れ、その後 `compute check: stopped`）、`not run, <段> (<番号>)` |
| `powervr: coherency (data master writes, slc bypass): <判定>, control <OK\|INCONCLUSIVE>, <n>/8 rounds, <s> stale, <m> missing` | 判定の語をそのまま記録（期待は決めない） | COHERENT だけが切り替えの候補。kernel が FAILED の時は出ない |
| `powervr: compute temporaries: fours <a>/32 wrong (<e>), twos <b>/32 wrong (<f>): <読み>` | 読みをそのまま記録 | `BLOCKS OF 2`（単位は 2）、`INCONCLUSIVE, no clobber seen (blocks of 4 not excluded)`（4 と矛盾しない。並びが隣り合わないと区別できない）、`BROKEN …`（試験の誤り）、`NOT RUN` |

この 3 行と、その後に `render node ready` が出て login できる事を画面で見る。

## 出典

値・手順は Mesa 26.2.5（`ddaf85e353442d9d7f192fdcf1823e652e3f2ee3`、MIT、p002 の記録）から取り、plan/guardrail.md の「MIT・GPL の出典のデータの引用」の通り、block ごとに複数行の comment で `Value(s) referenced:` / `Procedure referenced:` を書いた。file の上部には書いていない。code・構造・symbol の名前は写していない（名前は独自: 例 `POWERVR_USC_FLOAT_MULTIPLY_ADD` でなく `powervr_usc_float_multiply_add`、form の表は「piece の表 + field の値の配列」の独自の形）。参照した file と blob:

| file | blob |
| --- | --- |
| `src/imagination/pco/pco_isa.py` | d8e90778e645394e9eff9f52dbad8c5716bb704b（p002 の記録と一致） |
| `src/imagination/pco/pco_map.py` | 295debc90bb6d1d0a1895e014eefd3de4e67076d |
| `src/imagination/pco/pco_map.h.py` | 818ac4b879256a41fe8fb30f06e233beb3ed415f |
| `src/imagination/pco/pco_group_instrs.c` | b46a2b163196d969fa0769ed5ca46bf4feb2c2a1 |
| `src/imagination/pco/pco_binary.c` | 7c9aaf0fb35ff93faa7858bcba76c19a2e164927 |
| `src/imagination/include/hwdef/rogue_hw_defs.h` | 61a6284867a5c5c86981c27cb65794dcca5e8a5e |
| `src/imagination/vulkan/pds/pvr_rogue_pds_defs.h` | 249b688d559c3000b519c6400defac2111354081 |
| `src/imagination/vulkan/pds/pvr_rogue_pds_encode.h` | 04b20ea07a3f83c4d6b80a413ab5ff95fbeccaec |
| `src/imagination/csbgen/rogue/pds.xml` | b774fe41671b3e2a46ae5ab8114ee5b32d8cbfdc（p002 の記録と一致） |

## 確かめ

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| host 試験 | `timeout 120 sh plan/ws269/tests/p008/run.sh build/ws269-p008/host` | `powervr encode host test: 91 passed, 0 failed`。期待の bytes・word は Mesa の field の表から手で組み、各 case の comment に field の並びを書いた（operation 33 形、group 8 本（3 group の program の alignment、2 group の penultimate の pad を含む）、PDS の命令 14・data の word 5、拒否の case 25） |
| VF2 の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/ws269-p006-vf2 vmunix`（log `build/ws269-p008/vf2.log`、BUILD の名前は p006 の物を使った） | rc=0、`warning:`・`error:` 0 件（-Werror）。`usc-encode.c`・`pds-encode.c` が compile・link された |
| 開発中の照合（commit しない、試験ではない） | Mesa の generator（`pco_isa.h.py`・`pco_common.h.py`、mako）で作った `pco_isa.h` の encode 関数と、`pvr_rogue_pds_encode.h` の関数を host で呼び、同じ field の値で encoder の出力と比べた（`build/ws269-p008/dev/xcheck.c`・`xpds.c`） | USC: 全部の operation の形の全組合せ・乱数の main の group 200,000 本（使えた物）・control の header で 91,646 件一致、0 件不一致。PDS: 13 命令 × 200,000 の乱数で 2,600,000 件一致、0 件不一致。この照合で、destination の配列の範囲外の読み（修正済み）と、未定義の ctrlop を受けていた事（修正済み）を見つけた |

- 前半では riscv64-virt・amd64 の build は流していない（変更は `CONFIG_DRIVER_POWERVR` の VF2 の source の list だけで、他の platform は build しない file）。
- QEMU・実機の試験は無い（encoder は hardware に触れない。実機の確かめは後半の kernel の中の試験の道で行う）。

後半（q1349、2026-10-20 P2）:

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| host 試験 | `timeout 120 sh plan/ws269/tests/p008/run.sh build/ws269-p008/host` | `powervr encode host test: 91 passed, 0 failed`（`powervr_usc_program_encode` を `powervr_usc_program_build` の包みにした後も同じ）、`powervr compute host test: 70 passed, 0 failed`（control stream の 7 語 × 2 と終端、PDS の data・code の語、temps の field、書き込みの program の group の数（2,058）・8 byte の終わり・group 0・1・9〜11・13・最後の st・end の bytes、temps の program の group 0・1・9 の bytes、FW の命令の語、拒否の case。期待は cdm.xml・pvr_rogue_pds_defs.h・pco_isa.py の field から手で組み、各 case の comment に field を書いた） |
| VF2 の試験の kernel | `timeout 3600 make ZEDBSD_CONFIG=plan/ws269/tests/p008/config-visionfive2-compute.mk BUILD=build/ws269-p008-vf2 vmunix`（log `build/ws269-p008/vf2-compute.log`、名前の定数の整理の後に `vf2-compute2.log` で再 build、同じく rc=0・0 件） | rc=0、`warning:` 0 件（-Werror）。`compute-program.c`・`compute-check.c` が compile・link、`nm` で `powervr_compute_check` が定義済み |
| VF2 の通常の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/ws269-p008-vf2b vmunix`（log `build/ws269-p008/vf2-plain.log`） | rc=0、`warning:` 0 件。`compute-check.c` は build されず、`nm` で `powervr_compute_check` は weak の未定義（`w`、呼ばれない） |
| riscv64-virt の kernel（共有の `platform/riscv64/vmunix.mk` に触れたため） | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk BUILD=build/ws269-p008-virt vmunix`（log `build/ws269-p008/virt.log`） | rc=0、`warning:` 0 件、`error:` 0 件 |

- amd64 は触れた file を build しないので流していない。QEMU は起動していない。実機の 3 行は未実施（U29 の続き、上の文面）。

q1369（2026-10-20 P2）: 書き込み・temps の program の instance number（special register 51）の読みを、s0 からの `mbyp` から、compiler と同じ `movs1` の形（s1 に special register、mux s1、is4 fte、phase 2 の move）に直した（Mesa は special register を s1・s2・s4 でしか許さない）。新しい `special_group`（`src/drivers/gpu/powervr/compute-program.c`）。

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| host 試験 | `timeout 120 sh plan/ws269/tests/p008/run.sh build/q1369/p008` | encode 91 passed 0 failed、compute 70 passed 0 failed（group 0 の期待を movs1 の 12 byte に。header・phase 2・is4・destination は手で、source の 4 byte は encoder の配置のまま） |
| p009・p010-compiler の host 試験 | `sh plan/ws269/tests/p009/run.sh build/q1369/p009`・`.../p010-compiler/run.sh build/q1369/p010-compiler` | executor 112/0、compiler 298/0、group 20/0 |
| kernel | `make ZEDBSD_CONFIG=config/ci/config-{visionfive2,riscv64-virt,amd64}.mk BUILD=build/q1369-<c> vmunix -j16` | 3 つとも rc=0、`warning:` 0 件 |

- 実機の compute check（U35）は未実施。片付け: `build/q1369/`・`build/q1369-visionfive2/`・`build/q1369-riscv64-virt/`・`build/q1369-amd64/`・`build/q1369-*.log`。

## 残り

1. 実機（T1・ユーザー、U29 の続き）: 上の「実機の試験の文面」の 3 行。期待と違えば、kernel の行が FAILED なら program・kick の誤り（gdbstub は無いので、画面の数と FW の notice の行から）、時間切れなら FW の context の static state（0 のまま）や CDM の register の値を疑う。
2. temps の単位: 実機で `BLOCKS OF 2` なら p009 以降の DOUTU は 2 の単位（今の試験の program の store は 2 の単位で組み、どちらでも足りる）。`INCONCLUSIVE` なら 4 は否定されないが確定もしない（並びが隣り合わない構造では区別できない）。その時は 2 の単位（多めに取る）を既定に保つ。
3. 逆向き（CPU が cacheable で書き flush せず、GPU が読む）の data master の道は作っていない。USC の load（`ld` の data の返り先の routing）が要り、p009 の compute の load と一緒に確かめる（p006 は FW の fence で逆向きを確かめ済み）。
4. coherency の判定が COHERENT でも、既定（非 coherent）の切り替えは別の Phase で Q1 の承認を得て（design §3.2 の 4）。
5. Q1 への片付けの依頼: `build/ws269-p008/`（`mesa2/` は Mesa の raw の file、`host/` は試験の出力、`*.log`）、`build/ws269-p008-vf2/`・`build/ws269-p008-vf2b/`・`build/ws269-p008-virt/`（この worktree の build の出力）。前半の依頼の `build/ws269-p006-vf2/` は既に無い。

## q1428-i01（P2、2026-10-20）: 板の compute kernel が 91/16384 の件の切り分けの probe

- 板の結果（Q1、PMU の直しの後）:
  - `compute kernel: FAILED, 91/16384 words written`
  - `compute temporaries: fours 31/32 wrong, twos 31/32 wrong: BROKEN`
  - 通るもの: fence・end は finished、transfer の clear・copy は OK、reset の通知は無い。
- 読みで確かめたこと:
  - CDM の KERNEL0〜8 の field の位置は Mesa の cdm.xml と同じ。
  - workgroup は 1、workgroup の大きさは 32（`instances - 1` = 31）、max_instances 0（= 32）。
  - USC の special register の読み（inst_num 51）は、transfer（板で動いた x_p 97）と同じ MOVS1 の形。
  - 違いが 1 つあった: KERNEL0 の `usc_unified_size` に、こちらは USC の temporaries（7 → 2 単位）を入れている。Mesa は `vtxins`（PDS が DOUTW で書く入力の register、compute では local id）を入れ、temporaries は DOUTU の field だけで与える。害は割り当てが大きくなるだけと見て、今回は変えていない（次の結果しだい）。
  - Mesa の compute は invocation の番号を inst_num ではなく、CDM が PDS の temp 0・1 に入れる local id を DOUTW で unified store に書いて得る（`pvr_pds_compute_shader` の local_input_regs）。こちらの store・temps・fill は inst_num に頼る。
- 見込み（未確定）:
  - (a) inst_num が 0..31 にならない。
  - (b) 整数の multiply-add（IMADD32）の encode の誤り。
  - (c) 1 つの task の instance が 1 つしか走らない。
  - どれでも「32 の中で 1 つだけ正しい」は説明できる。91 は (a)・(b) で address がずれる場合にありうる。SLC は PTE の bypass があり、transfer の PBE の書き込みは届いているので、可能性は低いと見た。
- 足したもの（板の 1 回で切り分ける）:
  - `compute-program.{c,h}` の `powervr_compute_probe_program`。4 種類:
    - MADD: 全 instance が 3×5+7 を書く。
    - INSTANCE: inst_num を書く（最後に書いた instance の値が残る）。
    - INSTANCE_MADD: inst_num×4 を書く。
    - ADDRESS: 各 instance が base+4×inst_num に自分の番号を書く（temps の program から fill を抜いた形）。
  - `compute-check.c`: 上の 4 種類と、1 step・4 step の store の program の kick を足した（USC の room を 1 つ、kick を 6 つ）。結果は temporaries の行の後に 2 行で出す:
    - `powervr: compute probes: madd N (want 22, e), instance last N (e), instance*4 last N (e), own words MASK [1]=… [2]=… [31]=… (e)`
    - `powervr: compute probes: store of 1 step N/32 (e), of 4 steps N/128 (e)`
- 読み方:
  - madd が 22 でなければ (b)。
  - instance last が常に 0 で、own words が 00000001 なら (a) か (c)。[1]・[2]・[31] の値が 0xffffffff のままなら書かれていない。
  - 1 step が 32/32 で 4 step が満たないなら、step の進め（IMADD による address の加算）。

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| host（p008） | `sh plan/ws269/tests/p008/run.sh build/p016/p008` | encode 94/0、compute 70/0 |
| host（p009） | `sh plan/ws269/tests/p009/run.sh build/p016/p009 0` | 511 passed、0 failed |
| VF2 の vmunix（compute の check 入り） | `make ZEDBSD_CONFIG=plan/ws269/tests/p008/config-visionfive2-compute.mk BUILD=build/p016/vf2 vmunix -j16` | rc 0、warning 0。`build/p016/vf2/vmunix` |

- 直し自体はまだ無い。板の probe の 2 行で原因を決めて、次の Queue で直す。
- 片付け（Q1 に依頼）: `build/p016/`（vmunix を取り出した後）。

## q1430-i01（P2、2026-10-20）: probe の 2 回目、instance ごとに違う address への store

- 板の 1 回目の probe（Q1、vf2-dmesg27）:
  - madd 22（IMADD32 は正しい）。
  - instance last 31、instance*4 last 124（inst_num は 0..31、掛け算も正しい）。
  - own words は 00000001 だけ（[1]・[2]・[31] は 0xffffffff のまま）。
  - store は 1 step で 8/32、4 step で 21/128。
  - 固定の address へは全 instance が書く。instance ごとに違う address へは一部しか届かない。
- 読みで確かめたこと:
  - st の encode（I_ST_IMMBL: drc・srcseladd・burstlen 1・cachemode write-through・srcseldata・dsize 32）は Mesa の st32 と同じ。
  - Mesa の `trans_store_global` は address の対と data を 1 つの vector（addr lo、addr hi、data）に置き、address は s3、data は s0 で読む。こちらも r0・r1・r2 で同じ並び。
  - Mesa の schedule は st の後に wdf を置くだけ。
  - 違いは見つからなかった。
- 見込み:
  - (1) memory への書き込みが一定の granule（16 byte など）単位で、古い中身ごと書き戻されて上書きされる（stale な read-modify-write）。1 step の 8/32 は 16 byte の granule で 32 word に 8 個、と合う。
  - (2) address が数 instance ごとに 1 つ（例えば 4 instance の先頭）しか使われない。8/32 は 4 instance の先頭の 8 個と合う。
  - (3) address の計算から st までの依存。
- 足したもの（板の 1 回で分ける）:
  - `compute-program.{c,h}` に probe を 4 種類足した: STRIDE_16・STRIDE_64・STRIDE_128（instance の word の間を 16・64・128 byte にする）と ADDRESS_WAIT（address の計算と st の間に wait を挟む）。
  - `powervr_compute_probe_stride(kind)` を足した。
  - `compute-check.c` は probe の場所を 4 KiB ずつにし、USC の room を 1 つ足した。出力の行を 1 つ足した: `powervr: compute probes: own words at stride 16 MASK (e), 64 MASK (e), 128 MASK (e), with a wait MASK (e)`。
- 読み方:
  - stride が大きいと全部（ffffffff）になるなら (1)。その境の byte 数が granule。
  - stride によらず 4 instance ごと（11111111 の形、bit 0・4・8…）なら (2)。
  - wait の版だけ全部なら (3)。

| 確認 | 結果 |
| --- | --- |
| p008 の host（encode 94、compute 70） | 0 failed |
| VF2 の vmunix（`plan/ws269/tests/p008/config-visionfive2-compute.mk`、`build/p017/vf2/vmunix`） | rc 0、warning 0 |

- 片付け（Q1 に依頼）: `build/p017/`。

## q1432-i01（P2、2026-10-20）: probe の 3 回目、計算した address の値そのもの

- 板の 2 回目（vf2-dmesg29）:
  - stride 16・64・128 でも、wait を挟んでも、own words は 00000001 だけ。
  - store は 1 step で 3/32、4 step で 0/128。kernel は 100 word で、起動ごとに違う。
  - granule でも、4 instance ごとでも、依存でもない。計算した address へは instance 0 しか届かない。
- 足した probe（`compute-program.c`、`compute-check.c`、Q1 の (a)〜(d)）:
  - **COMPUTED**（a）: 各 instance が ADDRESS と同じ計算をした r0 と r1 を、固定の base（r8・r9）と base+4（r10・r11）に store する。最後に書いた instance の address が見える（期待は base+124）。
  - **ZERO_MADD**（b）: address = inst_num×0 + base（全 instance で同じ値、ただし IMADD を通す）、data は inst_num。期待は [0]=31 前後（最後に書いた instance）。
  - **CONSTANT**（c）: address = 1×4 + base（inst_num を使わない）、data は inst_num。期待は [1] が最後に書いた instance。
  - **MOVED**（d の代わり）: IMADD で r6 に address を計算し、pass で r0 に移してから store する。IMADD が書いた register を address に直接使うことの問題かを分ける（整数の add の encoder がまだ無いので、ADD の版は作らなかった）。
  - 出力の行を 1 つ足した: `powervr: compute probes: computed low … high … (want …), zero madd [0]=…, constant [0]=… [1]=…, moved MASK`。
- 読み方:
  - computed low が want なら値は正しく、st の address の読みの問題になる。違えば値の問題で、high も見る。
  - zero madd の [0] が 31 など instance の番号なら、IMADD を通した address でも全員が届く。
  - constant の [1] が埋まれば、inst_num 由来の address だけが壊れる。
  - moved が ffffffff なら、IMADD の結果の register を address に使うことの問題。
- 確かめていない（Mesa）: pco の compute の store の address の作り方（`add64_32` と carry、register の対の並び）は、まだ照らしていない。こちらの IMADD の group は Mesa の `is[4]=ft0` を明示しておらず、既定の 0 に頼っている（INSTANCE_MADD が正しいので、値は書けている）。

| 確認 | 結果 |
| --- | --- |
| p008 の host（encode 94、compute 70） | 0 failed |
| probe の encode（12 種、host） | 全部 0、長さ 64〜144 byte |
| VF2 の vmunix（`plan/ws269/tests/p008/config-visionfive2-compute.mk`、`build/p018/vf2/vmunix`） | rc 0、warning 0 |

- 片付け（Q1 に依頼）: `build/p018/`（vmunix の後）。

## q1435-i01（P2、2026-10-20）: address を 64 bit の add で作る

- 板の 3 回目（vf2-dmesg31）:
  - `computed low 001fa004 high 7532c3d9`: IMADD32 の後、address の高い word の r1 が壊れている。
  - zero madd（積が 0）は全 instance が届く。constant（積 4）も届かない。moved（IMADD の結果を r6 に取ってから r0 へ移す）も届かない。
  - つまり、積が 0 でない IMADD32 の後に、IMADD の書き先と関係なく r1 が壊れる。
- 直し（`compute-program.c`）: Mesa の compiler（とこちらの shader-backend の `MACHINE_ADD64`）と同じ形にした。
  - 1. instance の offset を IMADD32 で別の temp（r8 など）に作る。address の対を書く前に行う。
  - 2. 基底の対 r0・r1 を immediate で書く。
  - 3. `add64_group`（`add6432`、s0・s1 が基底の対、s2 が offset、w0・w1 に ft0・fte。Mesa の `group_map O_ADD64_32`）で address の対を書く。
  - store の program の各 step の address の進めも IMADD から add64 に替え、data（address の低い word）は pass で取る。
  - temps・fill の program と、probe の ADDRESS・STRIDE・ADDRESS_WAIT も同じ順にした。
  - IMADD の group に Mesa の `is[4]=ft0` を明示した。
  - temporaries は store が 9（r8 まで）、temps は 8 のまま（offset は r2 に）、fill は 8。
  - COMPUTED・ZERO_MADD・CONSTANT・MOVED の probe は、IMADD の影響を見るために前の形のまま残した。
- r1 が壊れる理由（IMADD32 の何が dst 以外の register を書くのか）は分かっていない。Mesa は address を add64 で作り、IMADD32 の結果を address の対に置かない。
- 試験: `plan/ws269/tests/p008/compute-host-test.c` の期待を新しい group の並びに直した。
  - store: group 3 が madd32（offset）、4 が r0、6 が add64_32、11 が add64_32。
  - temps: 15 group で、10 が add64_32、14 が end。

| 確認 | 結果 |
| --- | --- |
| p008 の host | encode 94/0、compute 73/0 |
| p009 の host（切り替え 0） | 511 passed、0 failed |
| VF2 の vmunix（`plan/ws269/tests/p008/config-visionfive2-compute.mk`、`build/p019/vf2/vmunix`） | rc 0、warning 0 |

- 板で見ること:
  - `compute kernel: OK, 16384/16384`。
  - temporaries の行の読み（BLOCKS OF 2 か INCONCLUSIVE）。
  - probes の own words、stride 16・64・128、wait が ffffffff になること。
  - store の 1 step が 32/32、4 step が 128/128。
- 片付け（Q1 に依頼）: `build/p019/`。

## q1437-i01（P2、2026-10-20）: store の address を step ごとに作り直す、temporaries を多めに

- 板の 4 回目（vf2-dmesg32）:
  - `compute kernel: FAILED, 1648/16384`（前は 60〜100）。
  - `fault 0x80007c4000`。run の base は 0x8000001000（前回の probe の want から逆算）なので、約 7.8 MB 先。
  - その後は context を失い、ほかは NOT RUN。
- 逆算: 正しい word の 1648 は 32 instance × 約 51 step。そこから先で address が大きく飛んだ。step ごとの加数（r3＝128）が 1 回だけ大きければ、こうは飛ばない。前の 3 回（MOVED・CONSTANT・COMPUTED）でも、IMADD の後で dst 以外の register（r1）が壊れていた。
- 見込み: 1 つの instance の temporaries が、隣の instance の register と重なっている（task issue の temporaries の field の単位の思い違い）。または前の step から運ぶ register（r0・r1・r3）が途中で壊れる。重なりなら、temps の probe の BROKEN（31/32）も、IMADD が「r1 を壊す」ように見えたことも説明できる。ただし確かめてはいない。
- 直し（`compute-program.{c,h}`）:
  - store の program は、step ごとに address を作り直す: step の offset（immediate k×128）→ base の対（r10・r11、最初に 1 回だけ書く）＋ instance の offset（r8）を add64 → それに step の offset を add64 → data を pass → st → wait。step から step へ address を運ばない。
  - 1 step は 6 group、512 step で 29,776 byte（USC の room の 32 KiB に収まる）。
  - store の kick の temporaries を 16 にした（`POWERVR_COMPUTE_STORE_TEMPORARIES`。使うのは r0〜r11）。field の単位が思っていたのと違っても、各 instance に足りるようにするため。
- 試験: `plan/ws269/tests/p008/compute-host-test.c` の store の group の期待を新しい並びに直した（6＋6×512＋1 group、7・8 が add64、10 が st、11 が wdf）。使わなくなった `immediate_r0` を外した。

| 確認 | 結果 |
| --- | --- |
| p008 の host | encode 94/0、compute 72/0 |
| p009 の host（切り替え 0） | 511 passed、0 failed |
| VF2 の vmunix（`plan/ws269/tests/p008/config-visionfive2-compute.mk`、`build/p020/vf2/vmunix`） | rc 0、warning 0 |

- 板の読み方:
  - kernel が OK、または fault 無しで正しい word が増えるなら、運ぶ register が壊れていた（重なり）。
  - まだ fault が出るなら、その address を報告してほしい。instance の offset（r8）か base の対（r10・r11）が壊れている。
- 片付け（Q1 に依頼）: `build/p020/`。
