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

## q1438-i01（P2、2026-10-20）: 同じ fault の address、temporaries の field を広く

- 板の 5 回目（vf2-dmesg34）: kernel は 9752/16384（1648 から増えた）。`fault 0x80007c4000` は前回と同じ address。
- どの kick の fault か:
  - log の順は「kernel: FAILED 9752」→ reset の notice → `compute: fail(fault …)` → temporaries は NOT RUN。kernel の kick は fault 無しで終わり、次の temporaries の kick（fields four）が fault した。
  - temporaries の program は前回と変わっていない。同じ program なら壊れ方も同じなので、fault の address も同じになる。store の program を変えても address が変わらないことと合う。
  - run の buffer（0x8000001000〜0x8000201000）の外の固定の場所を、壊れた address の対で書いている。
- 見込み: 1 つの instance の temporaries が足りず、隣の instance の register と重なっている。field の単位の思い違い（Mesa の DOUTU は 4 register 単位）か、CDM の側の割り当てか。これで次のことを説明できる:
  - field を広げるほど正しい word が増えた（store の field は 4 → 8 で、1648 → 9752）。
  - 8 temporaries の temps の program は field 2（four）でも 4（two）でも壊れ、今回は fault した。
  - IMADD の後に dst 以外（r1）が壊れて見えた。
- 直し（`compute-check.c`）:
  - store・probe・新しい temps の kick の field を、広い 16（`CHECK_WIDE_FIELD`）に固定した。単位が 1 でも 4 でも、instance あたり 16 register 以上になる。
  - temps の program を広い field で流す kick（`KICK_TEMPS_WIDE`）を足し、`powervr: compute temporaries: wide field N/32 wrong (e)` を出す。
  - 順を「kernel → probes → wide の temps → four と two の temps」にした。fault しうる kick を最後に回し、probes が読めるようにするため。
- 板の読み方:
  - kernel が OK で wide の temps が 0/32 なら、原因は field の幅。正しい単位を four・two の結果から決めて、vk の compute（`vk-queue.c` の task word の temps）にも当てる。
  - wide でもまだ壊れるなら、CDM の KERNEL0 の `usc_unified_size`（Mesa は vtxins だけ）か、instance の割り当ての別の原因を見る。

| 確認 | 結果 |
| --- | --- |
| p008 の host | encode 94/0、compute 72/0 |
| VF2 の vmunix（`plan/ws269/tests/p008/config-visionfive2-compute.mk`、`build/p021/vf2/vmunix`） | rc 0、warning 0 |

- 片付け（Q1 に依頼）: `build/p021/`。

## q1439-i01（P2、2026-10-20）: unified の大きさと field の格子

- 板の 6 回目（vf2-dmesg-vf2-gpu）:
  - kernel は 10116/16384。
  - store の 1 step は 32/32（field 16 の効果）、4 step は 108/128。
  - wide の temps は 30/32 wrong。
  - COMPUTED の r1 は依然として壊れている（high c5208a85）。
  - fours の kick は同じ 0x80007c4000 で fault。
- 読みで分かったこと:
  - `write_kick` の `temporaries` の引数は、CDM の KERNEL0 の `usc_unified_size`（temporaries×4 byte を 16 byte 単位で切り上げ）にもなる。
  - store の kick は temporaries 16 で unified 4 単位、field 16。wide の temps の kick は temporaries 8 で unified 2 単位、field 16。
  - 同じ field 16 で、unified の大きい store の 1 step は全部届き、unified の小さい temps は壊れた。field より unified（KERNEL0）が効いている見込みがある。Mesa は unified に vtxins だけを入れ、temporaries は DOUTU で与える。
  - COMPUTED の probe（temporaries 12 → unified 3 単位、field 16）でも r1 が壊れる。field だけの問題ではない。
- (2) fours の program を手で追う: temps の program は r7 に inst、r3〜r6 に fill、r2 に 4、r0 に 0、r2 = r7×r2 + r0、r0・r1 に base、add64、r2 = r7、st の順。instance どうしの register が重なると、隣の instance の fill（`0xdead000N`）や base が r0・r1・r2 に入る。この形の 64 bit の address（例えば高い word が fill の値）は run の外になり、毎回同じ値になる。0x80007c4000 は高い word が 0x80 のままで、低い word が 0x007c4000。base の低い word（0x001f1000 前後）に、壊れた offset（r2）が足された形と見る。r2 が壊れる（r7 が隣の instance の値になる、または r2 が重なる）とすると説明できるが、どの register かは格子の結果で決める。
- 足したもの（`compute-check.c`）:
  - temps の program の kick の格子を足した: unified の register 数 {32, 16, 8, 0} × field {16, 8, 4, 2} の 16 個。広いものから順に流し、fault で止まる。
  - 1 行で出す: `powervr: compute grid (wrong of 32, unified regs u, field f): u32: f16=N f8=N f4=N f2=N u16: …`。kick が失敗した所は `F(err)`。
  - 順は「kernel → probes → 格子 → wide の temps → four と two」。
- 読み方:
  - 行（u）で決まり、列（f）によらないなら、instance の register は KERNEL0 の unified が決める（単位は 0 になる境から分かる）。
  - 列で決まるなら DOUTU の field。その単位は、壊れ始める f と program の 8 register から分かる。
  - どこでも壊れるなら別の原因（r1 の破壊を含めて）。

| 確認 | 結果 |
| --- | --- |
| p008 の host | encode 94/0、compute 72/0 |
| VF2 の vmunix（`plan/ws269/tests/p008/config-visionfive2-compute.mk`、`build/p022/vf2/vmunix`） | rc 0、warning 0 |

- 片付け（Q1 に依頼）: `build/p022/`。

## q1441-i01（P2、2026-10-20）: encoder の register の field の M9、と store の形の probe

- 板の 7 回目（vf2-dmesg38）:
  - grid の u32・f16 でも temps は 31/32 wrong。u32・f8 で fault（0x8000213550）。
  - register の数の単位では説明できない。
- **M9（register の operand の encode）**:
  - Mesa 26.2.5 の `pco_isa.h.py`・`pco_common.h.py` から `pco_isa.h` を生成した。usc-encode.c の lower の source の 8 form、upper の 8 form、destination の 5 form の layout（`layout_place`）の bytes を、Mesa の `pco_src_*_encode`・`pco_dst_*_encode` の bytes と比べた。register は各 form の bank と index の上限の中で乱数（r0〜r3 も r4 以上も、bank も、mux も含む）。
  - 結果は **84000 passed、0 failed**。register の番号と bank の field（r4 以上、src2、dst の bank を含む）は Mesa と bit 単位で一致する。
  - 試験は `plan/ws269/tests/p008/m9-forms.sh MESA_SRC OUTDIR`（`m9-forms.c`、Mesa の展開した tree が要る）。
- group の並び（読みで確かめた）:
  - Mesa の `pco_encode_igrp` の順（header、命令は後ろの phase から、lower、upper、iss、dest、padding）と同じ。
  - main・bitwise の group は使わない側の source の 1 byte の form も出す（Mesa の `pco_igrp_src_variant` も、control 以外は必ず出す）。
  - main の group には必ず iss がある（`pco_igrp_iss_variant`）。
  - da（main は header と phase 2・back end の命令の長さ）と length も Mesa と同じ。
  - IMADD32 の group（`36 82 00 e2 c5 64 00 40 00 00 40 ff`）を手で読むと、s0=r5、s1=r4、s2=r0、dst=r0、w1p=0、is4=ft0 で正しい。
- encode に誤りが見つからないので、違いを板で切り分ける probe を足した。基準にするのは、板で 32/32 通った 1 step の store の形:
  - offset を IMADD で r8 に作る。
  - base を r10・r11 に置く。
  - add64 で r0・r1 を作る。
  - data は pass で入れる。
- 足した 4 種:
  - LIKE_STORE: その形そのまま（data は address の低い word）。期待は ffffffff。
  - LIKE_STORE_NUMBER: data を inst_num にする。ADDRESS の probe との違いはここだけ。
  - LIKE_STORE_IN_PLACE: base を r0・r1 に置き、add64 が読む pair に書く。
  - LIKE_STORE_EARLY: r0〜r11 を IMADD の前に書く。
  - 出力の行: `powervr: compute probes: like store (address) MASK (e), number MASK (e), in place MASK (e), early MASK (e)`。
- probe の場所を 0x1a0000 からに移し、USC の room を 3 つにした（16 種 × 4 KiB と、2 つの短い store）。前は 0x1f1000 からで、16 種なら run の外に出る。
- 読み方:
  - LIKE_STORE が ffffffff で NUMBER が 1 だけなら、data が inst_num の時だけ壊れる（data の register か、値が 0 でない時）。
  - IN_PLACE だけが壊れるなら、読む pair と同じ pair への書き込み（ADDRESS・temps・fill・前の store の形）。
  - EARLY だけが壊れるなら、IMADD より前に書いた register が壊れる（COMPUTED の r1 と同じ）。

| 確認 | 結果 |
| --- | --- |
| M9（operand の form） | 84000 passed、0 failed |
| p008 の host | encode 94/0、compute 72/0 |
| probe の encode（16 種、host） | 全部 0 |
| VF2 の vmunix（`plan/ws269/tests/p008/config-visionfive2-compute.mk`、`build/p023/vf2/vmunix`） | rc 0、warning 0 |

- 片付け（Q1 に依頼）: `build/p023/`。

## q1444-i01（P2、2026-10-20）: 64 bit の add を in place にしない

- 板の 8 回目（vf2-dmesg41）: like store（address）・number・early は全部 ffffffff、**in place だけ 00006db1**。
- 決定: 64 bit の add が自分の読む pair（base）に書くと、多くの instance の結果が誤る。IMADD・pass・inst_num・register の encode・temporaries の数は原因ではなかった。
- 直し（`compute-program.c`）:
  - `add64_group` は dst の pair が base の pair と重なると EINVAL を返す。in place の probe（LIKE_STORE_IN_PLACE）だけが、確かめ用に検めを通らない `add64_group_any` を使う。
  - store の各 step: `add64 r0:r1 = r10:r11 + r8` の後、`add64 r12:r13 = r0:r1 + r9`。data は r12 を pass し、st の address は r12。
  - temps: offset は r3（r2 を読むので r2 には書かない）、`add64 r4:r5 = r0:r1 + r3`、st は r4。
  - fill: `add64 r2:r3 = r0:r1 + r5`、st は data が r6、address が r2（13 group）。
  - ADDRESS・STRIDE・ADDRESS_WAIT の probe: `add64 r12:r13 = r0:r1 + r8`、st は r12。probe の temporaries は 14 にした。
  - COMPUTED・ZERO_MADD・CONSTANT の probe（IMADD を r0 に in place で書く）は、比べるために残した。
- compiler の back end（`shader-backend.c`）: `MACHINE_ADD64` は `select_address` だけで出す。dst は `TEMP_ADDRESS`（r6・r7）、source は値の register（r8 から）か scratch（r1〜r3）なので、in place にはならない。直しは要らない。ほかの 32 bit の op の in place（dst と同じ register を source に読む）は、今回の板の結果では壊れていない（IMADD の dst と別の source、pass）。
- 理由（調べた範囲）:
  - Mesa の register の割り当て（`pco_ra.c`）は、生存の範囲が命令で終わる source と dst を同じ register にしてよいとしている。repeat が 2 以上の時と vec の時だけ、src と dst を干渉させる。add64 の pair について、in place を避ける制約は見つからなかった。
  - `pco_isa.py` にも記述は無い。
  - hardware の側の read-after-write（w0 を書いた後に s1 を読む、など）か、64 bit の op の内部の順だと推測する。確かめてはいない。制約として、この phase.md と `add64_group` の comment に残した。

| 確認 | 結果 |
| --- | --- |
| p008 の host | encode 94/0、compute 72/0 |
| probe 16 種・fill（144 byte）・temps（168 byte）・store 512 step（29,776 byte）の encode（host） | 全部 0 |
| VF2 の vmunix（`plan/ws269/tests/p008/config-visionfive2-compute.mk`、`build/p024/vf2/vmunix`） | rc 0、warning 0 |

- 板で見ること:
  - `compute kernel: OK 16384/16384`。
  - store の 4 step が 128/128 で、lockup しない。
  - grid と wide の temps が 0/32 wrong（temps の program も in place をやめた）。
  - ADDRESS・STRIDE の own words が ffffffff。
- 片付け（Q1 に依頼）: `build/p024/`。

## q1446-i01（P2、2026-10-20）: st の data は address の対の次の register に

- 板の 9 回目（vf2-dmesg43）:
  - ADDRESS の own words に garbage が書かれた（[1]=a032b820 など）。
  - store は 1 step が 0/32 に退行し、その後 lockup した。
  - like store の 3 種は正しい。in place は 00006db1 のまま。
- 原因（q1444 で入れた形）:
  - st の data を address の対と離して置いた（address r12、data r2。fill は address r2、data r6）。
  - Mesa の `trans_store_global` は address の低・高と data を 1 つの vector に並べる。hardware は data を「address の対の次」の register から取ると見る（data の selector とは関係なく）。通った形（LIKE_STORE、前の store）は、どれも address r0・r1 と data r2 だった。
  - COMPUTED の probe の「low 001fa004」も同じ理由で説明できる。st の address は r8・r9 だったので、data として r10（＝ base+4）が書かれた。
- 直し:
  - `compute-program.c`:
    - `store_group` は data が address+2 でなければ EINVAL を返す。comment に規則を書いた。
    - store の step: address r12・r13、data r14（pass）。
    - temps: address r4・r5、data r6。
    - fill: address r2・r3、data r4（pass、14 group）。
    - ADDRESS 系の probe: address r12・r13、data r14。
    - COMPUTED: r0 を r8・r9・r10 で、r1 を r12・r13・r14 で store。
    - probe の temporaries は 15。store の 16 で r14 まで足りる。
  - **compiler の back end（`shader-backend.c`）にも同じ誤りがあった**: st の data は任意の値の register で、address は r6・r7。data を r8（`TEMP_STORE_DATA`、新しく予約）に MOVE してから st する形にした。値の register は r9（`TEMP_FIRST_VALUE`）から。vk の compute（vkcompute の fill）の store も、これで並びが正しくなる。
- 試験:
  - `plan/ws269/tests/p008/compute-host-test.c` の temps の group 11 を r6 にした。
  - compiler の host 試験（396・group 20）は、そのまま PASS。

| 確認 | 結果 |
| --- | --- |
| p008 の host | encode 94/0、compute 72/0 |
| compiler の host（p010-compiler） | 396/0、group 20/0 |
| p009（切り替え 0） | 511 passed、0 failed |
| p010 | 全部 0 failed |
| probe 16 種・fill・temps・store 512 step の encode | 全部 0 |
| VF2 の vmunix（`plan/ws269/tests/p008/config-visionfive2-compute.mk`、`build/p025/vf2/vmunix`） | rc 0、warning 0 |

- 板で見ること:
  - kernel が OK。
  - store が 1 step 32/32、4 step 128/128。
  - own words・stride・grid・wide の temps が全部正しいこと。
  - その後に gpu-selftest の compute（vkcompute）も見る。
- 片付け（Q1 に依頼）: `build/p025/`。

## q1448-i01（P2、2026-10-20）: KERNEL0 の unified に temporaries を書かない、field は 4 register 単位

- 板の 10 回目（vf2-dmesg45）:
  - **`compute kernel: OK, 16384/16384`**（初めて通った）。
  - own words・stride・wait は全部 ffffffff。store は 1 step 32/32、4 step 128/128。
  - grid は u32・f16 が 0 wrong、u32・f8 が 32 wrong、u32・f4 で fault。
- 読み:
  - temps の program は r0〜r7 の 8 register を使う。unified 32（KERNEL0 の `usc_unified_size` に 32 register）では、field 16（16×4＝64 register）なら足り、field 8（32 register）では全部誤る。
  - task issue の field は 4 register 単位（Mesa の `pvr_pds_setup_doutu` の `TEMPS_ALIGNSIZE 4`）で、instance の割り当ての全部を表す。KERNEL0 の unified（Mesa では vtxins、PDS が DOUTW で書く入力）は、その先頭を占める。だから unified に 32 を書くと、temporaries は 32 番目から後になる。
  - 今までは KERNEL0 の unified に temporaries の数を書いていたので、その分だけ instance あたりの register が足りなかった（field を広げるほど直った理由）。
- 直し:
  - `compute-program.{c,h}`: `struct powervr_compute_kernel` に `usc_inputs`（unified の入力の register、Mesa の vtxins）を足し、KERNEL0 の `usc_unified_size` は `usc_inputs` から作る。`usc_temporaries` は KERNEL0 に書かない（task issue の field が持つ）。comment に理由を書いた。
  - vk の executor（P1 の `vk-queue.c`）は `usc_temporaries` を設定し、`usc_inputs` を 0 のまま渡すので、file を変えずに unified は 0 になる。field は `temps` を 2 単位で数える（4 単位の 2 倍）ので、足りる。
  - `compute-check.c`:
    - `write_kick` の引数を unified の入力の数（`inputs`）に変え、普通の kick は 0 にした。
    - store・probe・短い store の field は Mesa の通り 4 単位（`powervr_compute_temporaries_field(…, 4)`）にした。
    - wide の temps は field 16 のまま（比べ用）。
    - grid は `CHECK_GRID 0` で既定では走らせない。走らせる時も、狭い cell の fault が後を潰さないよう、最後に回した。
- 試験: `plan/ws269/tests/p008/compute-host-test.c` の store の KERNEL0 の期待を `0x00000106`（unified 0）にし、unified の入力 2 で `0x00001106` を足した。

| 確認 | 結果 |
| --- | --- |
| p008 の host | encode 94/0、compute 73/0 |
| p009（切り替え 0） | 511 passed、0 failed |
| VF2 の vmunix（`plan/ws269/tests/p008/config-visionfive2-compute.mk`、`build/p026/vf2/vmunix`） | rc 0、warning 0 |

- 板で見ること:
  - kernel が OK のまま。
  - temps の行: fours（Mesa の field）が 0/32、twos も 0/32 なら INCONCLUSIVE。
  - wide の temps が 0/32。
  - 次に gpu-selftest の compute（vkcompute）。vk の executor は unified 0・field 2 単位になる。
- 片付け（Q1 に依頼）: `build/p026/`。

## q1451-i01（P2、2026-10-20）: 4 step の store の lockup、in place の probe を止める

- 板の 11 回目（vf2-dmesg46）:
  - kernel は OK、probes も正しい。store の 1 step は 32/32。
  - 4 step の kick で reset lockup（context 0x2、dm 4）が起き、以後は NOT RUN。
- field の確かめ:
  - 4 step の store の program は、kernel（512 step）・1 step と同じ生成器でできる。使う register は r0〜r14 の 15。
  - `POWERVR_COMPUTE_STORE_TEMPORARIES` は 16 で、field は 4（16 register）。足りている。
  - kernel（同じ field）と 1 step（同じ field）は通る。field の不足ではない。
- 見込み:
  - 4 step の kick の前に、in place で address を作る probe（CONSTANT の IMADD、LIKE_STORE_IN_PLACE の add64）が、壊れた address で session の memory のどこかに書いた。それが後の kick の program を壊した。
  - 4 step の program（`USC_STEPS_FOUR`）は probe の program の直後の room にある。
  - in place の結果は instance ごとに違う garbage の address になる。field と register の割り当てが変わったので、今回はたまたまそこに当たったと見る。確かめてはいない。
- 直し（`compute-check.c`）: CONSTANT と LIKE_STORE_IN_PLACE の probe の kick を流さない（error −1、値 ffffffff として報告する）。in place が壊れることはもう確かめてあり（q1441・q1444）、確かめのために memory を壊す危険を取らない。program と kick は残してある（流す時は skip を外す）。

| 確認 | 結果 |
| --- | --- |
| p008 の host | encode 94/0、compute 73/0 |
| VF2 の vmunix（`plan/ws269/tests/p008/config-visionfive2-compute.mk`、`build/p027/vf2/vmunix`） | rc 0、warning 0 |

- 板で見ること:
  - 4 step が 128/128 で lockup しない。
  - temps の行（fours・twos・wide）。
  - その後、手で `vkcompute`。
- 片付け（Q1 に依頼）: `build/p027/`。

## q1452-i01（P2、2026-10-20）: task の field は使う register より 1 つ多く

- 板の 12 回目（vf2-dmesg48）:
  - kernel は OK、store は 4 step まで 128/128、wide の temps（field 16）は 0/32。
  - fours（8 register の program に field 2＝8 register）は fault 0x80003e2000、32/32 wrong。
- Mesa の確かめ:
  - `pco_ra.c` の `func->temps` は「使う最大の register の番号+1」（`temp_alloc_offset` を含む）。
  - `pvr_pds_setup_doutu` は、それを `DIV_ROUND_UP(temps, 4)` にする。
  - 余白は見当たらない。
- 板で分かったこと:
  - 15 register の store は 4 block（16 register）で通る。
  - 8 register の temps は 2 block（8 register、ちょうど）で重なる。
  - 「使う register＋1」を 4 で割って切り上げれば、両方に合う。1 つ余分に要る理由は分からない（hardware が 1 つ予約する、instance の割り当ての境、など）。
- 直し:
  - `compute-program.{c,h}`: `powervr_compute_task_field(temporaries, &field)` を足した。`ceil((temporaries + 1) / 4)` で、comment に理由を書いた。
  - `compute-check.c`: store・probe・短い store の field をこの式にした（store 16 → 5、probe 15 → 4）。
  - temps の試験を「単位の確認」に直した:
    - driver の field（`powervr_compute_task_field(8)`＝3）を先に流す。
    - ちょうど（2）は最後に流す。fault しても後を潰さないようにするため。
    - 行は `powervr: compute temporaries: exact field N/32 wrong (e), one more N/32 wrong (e): READING`。
    - 読みは「OK, one register more than used is needed」「OK, the exact field is enough too」「BROKEN, the driver's field failed」のどれか。
  - host 試験（p008）に task field の 8→3、15→4、16→5 を足した。
- vk の executor（P1 の `vk-queue.c`、Q1 へ）:
  - 今は `powervr_compute_temporaries_field(temps, 2, &field)`、つまり `ceil(temps / 2)` で、どの temps でも `ceil((temps + 1) / 4)` 以上なので、今のままでも足りる。
  - 揃えるなら `powervr_compute_task_field(temps, &field)` に替える。式は compute-program の 1 か所に置いた。

| 確認 | 結果 |
| --- | --- |
| p008 の host | encode 94/0、compute 76/0 |
| VF2 の vmunix（`plan/ws269/tests/p008/config-visionfive2-compute.mk`、`build/p028/vf2/vmunix`） | rc 0、warning 0 |

- 片付け（Q1 に依頼）: `build/p028/`。

## q1454-i01（P2、2026-10-20）: fault の address の逆算と、読んだ register を次の group が書き換える hazard

- 板の 13 回目（vf2-dmesg50）: temps の program（8 register）は field 4（wide）なら OK、field 3・2 では fault 0x80003e2000（毎回同じ address）。
- fault の address の逆算:
  - run の base は 0x8000001000。temps の base は run＋0x1f0000＝0x80001f1000。
  - 0x80003e2000 ＝ temps の base ＋ 0x1f1000（＝ base の低い word）。
  - temps の program は index 6 で r0=0、index 7 で `r3 = r7×r2 + r0`、index 8 で `r0 = base の低い word` の順。IMADD が読んだ r0 が、**次の group が書いた base の低い word** だった。こう考えると、offset ＝ base_low＋4i、address ＝ base＋base_low＋4i で、fault の address と合う。
  - fill の program も同じ順（r0=0 → IMADD → r0=base）。
  - 通っている program（store・probe の ADDRESS 系・LIKE_STORE）は、IMADD の source（r6 の 0 など）を後で書き換えない。in place の失敗（q1441・q1444）も同じ種類と見る。
  - field で結果が変わるのは、一度に走る instance の数か順が変わって、hazard が出るかどうかが変わるためと推測する（確かめてはいない）。
- 直し:
  - `compute-program.c` の temps の program を、各 register を 1 回だけ、読まれる前に書き、後で書き換えない形にした（10 group、r0〜r7）: r7 inst（＝ data）→ r0・r1 base → r2=4 → r3=0 → `r4 = r7×r2 + r3` → `add64 r5:r6 = r0:r1 + r4` → st（data r7 ＝ address の次）→ wait → end。fill を詰める仕組み（`TEMPS_FILL`）は外した。
  - fill の program（`POWERVR_COMPUTE_FILL_TEMPORARIES` 13）も同じ規則にした: r8 offset、r12 value、`add64 r10:r11`、st は data r12。
  - 確かめの probe を 2 つ足した: REWRITE（LIKE_STORE_NUMBER の IMADD の加数 r6 を、次の group で 0x12340 に書き換える）と REWRITE_LATER（間に 2 group を挟む）。行は `powervr: compute probes: addend rewritten by the next group MASK (e), two groups later MASK (e)`。
  - field は前回の `powervr_compute_task_field`（＋1）のまま。exact（2）は最後に流す。hazard が原因なら、exact も 0/32 になるはず。
  - 試験: p008 の temps の group の期待を 10 group の並びに直した。
- compiler（`shader-backend.c`）への含み（未対処、vkcompute の結果しだい）: register の割り当ては、生存が終わった register を次の命令の dst に使いうる。IMADD・add64 の source を次の group が書き換える並びが出れば、同じ hazard になる。板で REWRITE が壊れて REWRITE_LATER が通れば、間隔の規則（source を書く前に N group 空ける）を back end の割り当てに入れる。

| 確認 | 結果 |
| --- | --- |
| p008 の host | encode 94/0、compute 76/0 |
| probe 18 種・fill（144 byte）・temps（112 byte）・store 512 step の encode | 全部 0 |
| VF2 の vmunix（`plan/ws269/tests/p008/config-visionfive2-compute.mk`、`build/p029/vf2/vmunix`） | rc 0、warning 0 |

- 片付け（Q1 に依頼）: `build/p029/`。

## q1455-i01（P2、2026-10-20）: compiler の back end に「読んだ register を 2 group は書き換えない」規則

- 板の 14 回目（vf2-dmesg51）: `addend rewritten by the next group ffff0000`、`two groups later ffffffff`。hazard の仮説が確かめられた。temps は exact でも 0/32 で、field は exact で足りる（+1 は要らない）。
- 直し（`shader-backend.c`・`shader-private.h`）:
  - `MACHINE_NOP`（control の nop）と `SHADER_HAZARD_GAP`（2）を足した。板で確かめたのは「間に 2 group で正しい」ことで、間に 1 group は確かめていないので 2 にした。
  - `shader_backend_space_hazards` を足し、`shader_backend_encode` の最初に呼ぶ。直前の 2 group のどれかが読む temporary を書く group の前に、足りない数の nop を入れる。
    - 範囲は register の pair を丸ごと数える。st は address の対と data（＋2）を読むとみなし、load と sample は 4 つ書くとみなす（安全側）。
    - 挿入より後に着地する branch の target は、一緒に動かす。その group 自身への branch は、最初の nop に着地する。
  - `shader_backend_hazards`（残った hazard の数）を足した。
- 試験（`plan/ws269/tests/p010-compiler/`）:
  - `compile-host-test.c` で、compile したすべての shader に「hazard 0」の確かめを足した。
  - nop を入れる仕組みの単体の試験（`test_hazard_spacing`）を足した: 読んだ直後に書くと hazard 1 → nop 2 つ → 書きは後ろへ、branch は最初の nop へ → hazard 0。
  - `machine-run.c`（simulator）に `KIND_NOP` を足した。
- 未対処（記録）: loop の戻りの辺。loop の末尾で読み、branch で戻った先の頭で書く並びは、間が branch の 1 group だけになる。直線の順でしか確かめていない。vkcompute の fill は loop を持たない。
- **main の build が通らない（Q1 の変更、P1 の file）**: `vk-queue.c` の `QUEUE_KERNELS_MAX` が 128 になったため、`QUEUE_USC_BYTES`（0x400×128＝128 KiB）と `QUEUE_PDS_BYTES`（0x40×129）が、`POWERVR_EXECUTOR_USC_BYTES`（0x10000）と `POWERVR_EXECUTOR_PDS_BYTES`（0x2000）を超える。`_Static_assert`（vk-queue.c:74・75）で、VF2 の vmunix と p009 の host 試験の compile が失敗する。直し方は次のどれか:
  - `render-private.h` の 2 つの大きさを 2 倍（USC 0x20000、PDS 0x4000）にする。session の program の heap の中の place も合わせる。
  - `QUEUE_USC_ROOM` を 0x200 にする（kernel の program が 512 byte に収まるなら）。
  - kernel を 64 ずつの複数の submission に分ける。

| 確認 | 結果 |
| --- | --- |
| compiler の host（p010-compiler） | 452 passed、0 failed（全部の shader で hazard 0）、group 20/0 |
| p008 の host | encode 94/0、compute 76/0 |
| p009 の host・VF2 の vmunix | **失敗**。main の vk-queue.c の `_Static_assert`（上）が原因で、この Queue の変更とは無関係 |

- 片付け（Q1 に依頼）: `build/p030/`。

## q1457-i01（P2、2026-10-20）: loop の戻りの辺の hazard

- `shader_backend_space_hazards` に branch の確かめを足した。branch の直前の group が読む temporary を、branch の着地先の group が書くなら、branch の前に nop を 1 つ入れる。branch 自身と nop で、間が 2 group になる。loop の戻りも、前への jump も同じ扱い。nop を入れる処理は `insert_nops` に分けた。
- `shader_backend_hazards` も同じ確かめを数える。
- 試験（`compile-host-test.c`）:
  - loop の単体の試験を足した: 頭で r9 を書き、末尾で r9 を読み、頭へ戻る branch → hazard 1 → branch の前に nop → hazard 0。
  - loop.comp（loop-o・loop-0）を含む全部の shader で hazard 0 を確かめる（branch の分を含む）。

| 確認 | 結果 |
| --- | --- |
| compiler の host | 455 passed、0 failed、group 20/0 |
| p009 | 511 passed、0 failed |
| p010 | 全部 0 failed |
| VF2 の vmunix（`build/p031/vf2/vmunix`） | rc 0、warning 0 |

- 注意: q1458 の板の結果で、nop の group が USC を止めている疑いが出た。この Queue は nop を使う。q1458 の結果しだいで、埋め草の形を替える。

## q1458-i01（P2、2026-10-20）: vkcompute の lockup・fault、nop の probe と compiler の in place の IMADD

- 板（vf2-dmesg53・54）:
  - 128 workgroup の vkcompute は、nop 入り（288 → 368 byte）で lockup した。次の回は fault 0x800000a020 で、buffer の末尾の先、data[4104] への store。
  - 同じ shader で fault 0x8000025800 → lockup → +0x20 と毎回違う。timing に依存しているように見える。
- (1) probe: `compute-program.c`:
  - NOP_BETWEEN（LIKE_STORE_NUMBER の、store より前の各 group の後に、compiler と同じ形の control の nop を 2 つ）を足した。
  - WAIT_BETWEEN（同じく wait を 2 つ）を足した。
  - `compute-check.c` は WAIT の行を probes の中で出す。NOP の行（`powervr: compute probes: nop between groups MASK (e)`）は、core を止めうるので check の最後（`run_nop_probe`）に流す。
- (2) nop の encode を Mesa と照らした:
  - 我々の nop の group は `02 80 6e 00`（header の length 2 半語、ext、alutype control 0x60 と ctrlop nop 7<<1、命令の 1 byte の 0）。
  - Mesa の `group_map O_NOP` は control の header で、ctrlop nop、miscctl は end。板で動く END の group（`04 80 ee 00`）と比べると、違うのは end の bit だけ。
  - 誤りは見つからない。それでも板で一度も流していないので、**compiler の埋め草（`nop_operation`）は板で確かめた wait（wdf）に替えた**。`MACHINE_NOP` の定義は残した。
- (a) host で vkcompute の fill を compile し、machine の operation の列を見た（`build/p032/ops.c`）:
  - **gid の計算が in place の IMADD だった**（`IMADD r11 = r11×1 + r9`、workgroup×32 の後に local id を足す所）。in place の IMADD は板の CONSTANT・COMPUTED の probe で壊れることが分かっている。hazard の検め（間の group の数）では、同じ group の中の in place は捕まえていなかった。gid が壊れると data[4104] のような範囲外の store になり、timing で毎回違う結果になることと合う。
  - 直し（`shader-backend.c`）:
    - `operation_in_place` を足した。
    - `stage_in_place` は、自分が読む register に書く operation の結果を、新しく予約した staging の対（`TEMP_STAGE` r9・r10。値の register は r11 から）に書かせ、直後に元の register への MOVE（同じ condition）を入れる。
    - control の counter の step（`MACHINE_CONDITION`）と branch は、設計の上で in place なので除く。
    - `shader_backend_hazards` は in place の数も数える。
  - fill の出力（staging の後）: `IMADD r9 = r13×S1 + r11` → wait 2 → `MOVE r13 = r9`。in place は 0。
- (b) 板で動いた手書きの kernel（compute-program.c）の gid は、`r8 = r5×r4 + r6`（別の register に）で、add64 は別の pair に書く。compiler の出力も、in place をやめたことで同じ形になった。

| 確認 | 結果 |
| --- | --- |
| compiler の host | 455 passed、0 failed（全部の shader で hazard と in place が 0） |
| p008 | encode 94/0、compute 76/0 |
| p009 | 511 passed、0 failed |
| p010 | 全部 0 failed |
| probe 20 種の encode | 全部 0 |
| VF2 の vmunix（`plan/ws269/tests/p008/config-visionfive2-compute.mk`、`build/p032/vf2/vmunix`） | rc 0、warning 0 |

- 板で見ること:
  - compute check の `wait between groups` が ffffffff。
  - 最後の `nop between groups`（nop の encode そのものの確かめ）。
  - その後、手で vkcompute。
- 片付け（Q1 に依頼）: `build/p032/`。

## q1461-i01（P2、2026-10-20）: vkcompute の fill の fault、IMADD の special register の定数

- 板（vf2-dmesg56）:
  - nop と wait の probe は、どちらも ffffffff（nop の encode は良い）。
  - vkcompute はまだ fault: 0x800002a020 ＝ buffer の先頭＋0x24020、word 36872 ＝ 0x9008。前回は 4104 ＝ 0x1008。
- 逆算: 36872 ＝ 36×1024＋8、4104 ＝ 4×1024＋8。gid（workgroup×32＋local id）の workgroup の項が、**×32 ではなく ×1024（＝×32×32）**になっている。
- host で compile した列（workgroup 36 の kernel、`powervr_shader_compile_inline` の形。vk の executor は shared の値を即値に埋めるので、shader は shared register を読まない。直す前の分。WAIT は省く）:

```
  1 IMM     r12 = 0x24                 (workgroup id 36、即値)
  2 IMM     r1  = 0x20                 (32)
  3 IMADD   r13 = r12 * r1 + sr0       (workgroup×32、加数は special の 0)
  4 IMADD   r9  = r13 * sr1 + r11      (＋local id、乗数は special の 1)
  7 MOVE    r13 = r9                   (gid)
 ...
 38 IMADD   r15 = r11 * r1(4) + sr0    (gid×4)
 41 IMADD   r11 = r15 * sr1 + sr0
 43 ADD64   r6:r7 = r12:r14 + r11      (buffer＋offset)
 44 STORE   data r8, address r6
```

- 原因の見込み: op 4 の乗数の special の定数 1（sr1）を、hardware は temporary の r1 として読んだ。r1 はその時 32 なので、gid ＝ (wg×32)×32＋lid になり、計算と合う。
  - 板で動いた手書きの program（compute-program.c）は、IMADD の source が全部 temporary。
  - 加数の sr0（0）が r0 として読まれても、r0 は EMC の counter（0）なので害が出ない。
  - operand の encode は M9 で Mesa と同じなので、bank の field の誤りではない。int32_64 の op が special の bank を読めない（Mesa は定数を temporary に置く）と推測する。確かめてはいない。
- 直し（`shader-backend.c`）:
  - `integer_source` を足した。整数の IMADD（add・multiply）の source が special register（0・1・float の 1・all ones）なら、scratch の temporary に IMMEDIATE で載せてから使う。
  - 直した後の fill: `IMADD r13 = r12 * r1 + r2`、`IMADD r9 = r13 * r1(1) + r11` と、全部 temporary。368 → 456 byte、temps 16。
- Q1 の質問への答え:
  - **COMPUTED の probe**: in place の IMADD（r0 に書く）で address を作り、その r0・r1 の値を書き出す probe で、比べのために残してある。in place は壊れる（q1441・q1444）ので、low が want と違い、毎回変わるのは想定どおり。`(0)` は kick の error の値（kick は通った）で、合否ではない。
  - **shared word の probe**: vk の executor は shared の値（workgroup id、buffer の address）を compile の時に即値として埋める（`powervr_shader_compile_inline`）。fill の shader は shared register を読まないので、作らなかった。同じ理由で、PDS が書く shared と USC の最初の読みの間の hazard も、今の fill には関係しない。shared register を実際に読む shader（push constant など）が出たら確かめる。

| 確認 | 結果 |
| --- | --- |
| compiler の host | 455 passed、0 failed |
| p009 | 511 passed、0 failed |
| p010 | 全部 0 failed |
| VF2 の vmunix（`plan/ws269/tests/p008/config-visionfive2-compute.mk`、`build/p033/vf2/vmunix`） | rc 0、warning 0 |

- 片付け（Q1 に依頼）: `build/p033/`。

## q1462-i01（P2、2026-10-20）: compiler の出力と executor の道の切り分け

- 板（vf2-dmesg58）: 456 byte、16 temps の fill で lockup（fault は無い）。手書きの compute check は全部 OK。
- 足した probe（`compute-check.c`、どちらも check の最後に流す。止まりうるため）:
  - (1) **compiled fill**:
    - kernel の中で vkcompute の fill の SPIR-V（`userland/tests/vkcompute/shader.h`）を `powervr_shader_read` → `powervr_shader_compile_inline` で compile する。workgroup id 0、buffer は run の 0x1c0000 を即値にする（executor と同じ）。
    - check の道で 1 kernel（32 instance、check の PDS program、unified 0、field ＝ `powervr_compute_task_field(temps)`）を流す。
    - 行は `powervr: compute probes: compiled fill N/32 (e), BYTES bytes, TEMPS temps, field F`。期待は data[x] ＝ 3x＋7 で 32/32。
  - (2) **many kernels**:
    - executor と同じ並び（1 つの stream に 128 個の kernel、各 kernel は同じ PDS program、最後に fence の kernel と end）で、手書きの 1 step の store を流す。
    - stream は stream の buffer の 0x8000 から置く（kick の slot 112）。
    - 行は `powervr: compute probes: 128 kernels of the one-step store in one stream N/32 (e)`。
  - 読み方:
    - (1) が 32/32 で (2) が止まるなら、executor の道（kernel の数・stream）が悪い。
    - (1) が止まって (2) が通るなら、compiler の出力が悪い。
- (3) compiled fill と手書きの program の比べ（compiler の今の出力、`build/p034/ops.c`）:

| 項目 | 手書き（check の store・LIKE_STORE、板で OK） | compiled fill（直す前・直した後） |
| --- | --- | --- |
| IMADD の source | 全部 temporary | 直す前は special の定数（q1461 で temporary に） |
| IMADD の結果を読むまでの間 | 2 group 以上（例: IMADD r8 → IMM r10 → IMM r11 → ADD64） | 直す前は 1 group（`IMADD r11` → `MOVE r8` → `ADD64` が r11 を読む）。**今回 2 に広げた**（下） |
| in place | なし | q1458 で staging にした（0） |
| 読んだ register を書くまでの間 | 2 group 以上 | 2（q1455、wait を埋め草に） |
| st の data | address の対の次 | `MOVE r8` → address r6・r7、data r8（q1446） |
| st の後 | wdf、end | wdf、end（同じ） |
| ADD64 の結果を読むまでの間 | 0（ADD64 → 次の group の pass が読む、板で OK） | 0（ADD64 → STORE） |
| temps / field | store 16 → 5 block | 16 → `powervr_compute_task_field(16)` ＝ 5（probe の道）。executor は `ceil(16/2)` ＝ 8 |
| 長さ | store 1 step 144 byte | 464 byte、56 group（wait を多く含む） |
| 埋め草 | なし | wdf（q1458） |

- 違いからの直し（`shader-backend.c`）: IMADD の結果を直後の group が読む形（RAW、間 0〜1）は、手書きの program に無く、板で確かめていない。`integer_result_read` を足し、`SHADER_HAZARD_GAP` の間隔を IMADD の結果を読む側にも当てた。fill は `IMADD r11` → `MOVE r8` → wait → `ADD64` になった（456 → 464 byte）。`shader_backend_hazards` もこれを数える。

| 確認 | 結果 |
| --- | --- |
| compiler の host | 455/0、group 20/0 |
| p008 | encode 94/0、compute 76/0 |
| p009 | 511 passed、0 failed |
| p010 | 全部 0 failed |
| VF2 の vmunix（`plan/ws269/tests/p008/config-visionfive2-compute.mk`、`build/p034/vf2/vmunix`） | rc 0、warning 0 |

- 片付け（Q1 に依頼）: `build/p034/`。

## q1464-i01（P2、2026-10-20）: executor の並びを kernel 数で二分、compiled fill を先に

- 板（vf2-dmesg59、Q1 の読み直し）: 128 kernel の probe は store を終えた後に lockup した。HWR の後でも fence は届くので wait は 0 で返り、data は 32/32。そのため session が lost になり、compiled fill は kick を断られた（5 ＝ EIO）だけで、走っていない。**executor の並びが core を止める**。compiled fill は未判定。
- 変えた順（`compute-check.c`）:
  1. **compiled fill を先に**流す。その前に prefix を流す（`run_prefixes`）: fill の先頭の N group（4 刻み、最後は end の前まで）の後に、既知の正しい marker の store（r16・r17 に address、r18 に 0xc0de0000|N、st、wdf、end）を繋いだ program を 1 kernel ずつ流す。行は `compiled fill prefix N of M groups: marker V (ok|MISSING, e)`。失敗したら、そこで check を止める。
  2. **executor の並びを 1・2・4・8・16・32・64・128 kernel** で、小さい順に別々の kick で流す（`run_many_kernels(state, N)`。同じ stream の場所 0x8000、各 kernel は 1 step の store と同じ PDS program、最後に fence の kernel と end）。行は `powervr: compute probes: executor layout N kernels M/32 (e)`。
     - 注意: lockup した kick 自身は、HWR が fence を進めるので (0) で返る。**境界は「(0) の最後の N」で、その次の N が (5) refused になる**。
- (3) 普通の check の kick と executor の並びの違い（静的）:

| 項目 | check の普通の kick（OK） | executor の並び（止まる） |
| --- | --- | --- |
| stream の中の kernel の数 | 1（＋fence の kernel） | 128（＋fence の kernel） |
| kernel の語 | `powervr_compute_kernel_words`（7 語） | 同じ関数、同じ形 |
| 各 kernel の PDS program | kick ごとの slot | 全部の kernel が 1 つの slot を使う（同じ data・code） |
| fence の kernel | あり（`PDS_FENCE` の slot、max_instances 8、shared_from_pds・common_shared） | 同じ |
| end の前 | fence の kernel、すぐ end の語 | 同じ |
| stream の長さ | 15 語（60 byte） | 128×7＋7＋1 ＝ 904 語（3,616 byte）、0x8000〜0x8e20（4 KiB の境界は跨がない。vk-queue は 0x11000〜0x11e20 で、これも跨がない） |
| resume（context の保存）の領域 | stream の buffer の先頭 0x1000 byte を全部の kick で共有 | 同じ |

- 怪しい点（推測、板で決める）:
  - 長い stream の途中で FW が compute の context を切り替える（time slice）と、resume の領域（`CDM_CONTEXT_STATE_BASE`、0x1000 byte、resume の PDS は `POWERVR_COMPUTE_SWITCH_STATE` の時だけ）からの再開が要る。その形が不完全で、再開か保存で止まる。kernel 数が増えるほど切り替えに当たりやすい。
  - 128 の kernel が同じ PDS の data・code を共有する。check は kick ごとに別の slot を使う。
  - CDM の 1 stream の kernel の上限（Mesa は 1 dispatch に 1 kernel で、workgroup は KERNEL3〜5 の global size で数える）。我々は workgroup ごとに 1 kernel にしている。Mesa の形（1 kernel、global_size 128）に寄せるのが本筋の候補。

| 確認 | 結果 |
| --- | --- |
| p008 の host | encode 94/0、compute 76/0 |
| VF2 の vmunix（`plan/ws269/tests/p008/config-visionfive2-compute.mk`、`build/p035/vf2/vmunix`） | rc 0、warning 0 |

- 片付け（Q1 に依頼）: `build/p035/`。

## q1466-i01（P2、2026-10-20）: compiled fill の書き先・register の dump、executor の並びの 9〜16 kernel と判別の probe

板の前回（vf2-dmesg60）: prefix は全部 OK、compiled fill は 0/32（0）、464 byte、16 temps、field 5。executor の並びは 1・2・4・8 kernel が 32/32、16 kernel で store の後に lockup、32 kernel は refused。

### (A) compiled fill の data が違う原因を探す probe（`compute-check.c`）

1. **書き先の走査**: compiled fill の kick の前に、run の `+0x1b0000`〜`+0x1d0000`（fill の語 `+0x1c0000` の前後 64 KiB）の語を、それぞれ `0x5a000000 | 語の番号` で埋める。kick の後で、変わった語を最初の 48 個まで 1 行ずつ出す（`compiled fill wrote +OFF: VALUE`）。最後に個数を出す（`compiled fill changed N words in +1b0000..+1d0000`）。
2. **register の dump**: prefix の marker の代わりに、fill の先頭 N group（4 刻み、最後は end の前の 55 まで）の後に dump を繋いで流す。dump は r0〜r15 の全部（fill が使う register は 16 個）を、lane ごとに `+0x1c8000 + lane×64 + K×4` へ store する。
   - 作り（`dump_program`）: 中身は fill の back end の写しで、kept の group に dump の operation を足して `shader_backend_encode` に渡す（compile と同じ間隔の付け方）。結果を書く operation の後には、どれも 2 つの spacing group（MACHINE_WAIT）を置く。
   - dump の operation:
     - r19 ＝ lane（s51）
     - r21 ＝ 6
     - r20 ＝ r19 << r21
     - r23 ＝ address の上位
     - register ごとに: r22 ＝ 下位＋K×4、r24:r25 ＝ add64(r22, r23, r20)、r26 ＝ rK、st r26 → [r24]（data ＝ address＋2）、wdf
   - 使う temps は 27。field は ceil(28/4) ＝ 7。program は 1,256〜1,656 byte で、置き場は `USC_COMPILED` の後の 8 KiB。
   - 行の形: `dump N/55 lane L (e, bytes): r0 … r15`。lane は 0・1・31。行の前に `compiled fill dump: run at HI_LO, dump at +1c8000` を出す。
3. **host の simulator の対の値**: `plan/ws269/tests/p008/fill-registers.sh OUTDIR ADDRESS`（新規、`fill-registers.c`）。
   - ADDRESS には板の `run at` の値に 0x1c0000 を足した値を渡す。
   - fill を同じ値で inline に compile し、machine-run で先頭 N group に end を足して走らせる。同じ形の `dump N/55 lane L (0): r0 … r15` を出す。最後に全体の program の 3x+7 を確かめる。
   - 板の行と diff すれば、**最初に食い違う N（4 group の幅）** が分かる。address を含む register は、ADDRESS を板の値にすれば一致する。
   - host（ADDRESS 0x8001c0000）: 43 行、全体は 32/32。
   - 例: N＝55 で r6 ＝ 0x001c0000＋lane×4、r7 ＝ 8、r8 ＝ 3·lane＋7（store の data）。
4. **dump の形の host 確認**（使い捨て、commit しない）: kernel の `dump_append`・`dump_program` をそのまま host で compile した。prefix の 14 通りすべてで、次を確かめた。
   - encode 0。
   - hazard の数 0。
   - machine-run の dump の語が simulator の register と全 lane で一致（bad 0）。

### (B) executor の並びの lockup の境界と判別（`run_layouts`、`run_many_kernels(state, N, flags)`）

- 各 case は**自分の session で流す**（teardown → setup で開き直す）。lockup で session が lost になっても、次の case は走る。
- 各 case の kick の直後に、同じ session で 1 kernel の store（`KICK_STEPS_ONE`）をもう一度流す。lockup した kick 自身は (0) で返るので、**「one kernel after」が (5) なら、その case が lockup した**と読む。
- 行の形: `executor layout N kernels[ flags] W/32 (e), S words (B bytes), one kernel after A/32 (e2)`。
- 順序:
  1. 1・8・9・10・12・14・16 kernel（境界を詰める）。
  2. 16 kernel で fence の kernel 無し（`MANY_NO_FENCE`）。
  3. 16 kernel で kernel ごとに別の PDS slot（`MANY_OWN_SLOTS`。probe の slot 13〜28 に 1 step の store の PDS program を書き直して使う。DOUTU が 1 slot を共有するかどうかの判別）。
  4. 16 kernel で全部の kernel に fence の bit（`MANY_ALL_FENCED`。kernel の並行を無くす）。
  5. 16 kernel で command の resume の語（word 15）無し（`MANY_NO_RESUME`）。
- context switch を session ごとに切る経路（`POWERVR_COMPUTE_SWITCH_STATE`）は session の作りの中なので、ここでは kick 単位の resume の語だけを外した。以前の記録では「state の語なし」は end だけの stream でも lockup している。そのため、この case は最後に置いた。
- 判別の読み方:

| 結果 | 示唆 |
| --- | --- |
| 9〜14 のどこかで境界 | stream の語数・kernel 数のしきい値。境界の bytes を行の値で見る |
| no fence だけ通る | fence の kernel の位置（多数の kernel の後の fence）が原因 |
| own slots だけ通る | 1 つの PDS slot（DOUTU）を多数の kernel が共有するのが原因 |
| all fenced だけ通る | kernel の並行（USC の資源・shared の確保）が原因 |
| no resume word だけ通る | resume（context switch）の領域の作りが原因 |
| どれも止まる | kernel 数そのもの。Mesa の形（下）へ |

### Mesa の形の executor の設計（vk-queue.c は P1 の file。Q1 へ）

1 dispatch を **1 kernel** にし、workgroup の数は KERNEL3〜5 で数える。workgroup の id は immediate で埋め込まず、PDS が USC へ渡す。
- 参照: Mesa 26.2.5 の `src/imagination/vulkan/pvr_cmd_buffer.c`（pvr_compute_generate_control_stream）、`src/imagination/csbgen/rogue/cdm.xml`、`pvr_pds.c`（pvr_pds_compute_shader の work_group_input_regs）。
- kernel の語（`powervr_compute_kernel_words` を拡張する。値は現在の関数の bit の置き方のまま）:
  - word0（KERNEL0）:
    - block KERNEL
    - usc_unified_size ＝ ceil(USC の入力 4 語×4 / 16)。workgroup id x・y・z を vtxin r0〜r2 に置くので、usc_inputs ＝ 4（16 byte 単位で 1）
    - pds_temp_size ＝ ceil(3×4/16)＝1。PDS の入力の temps 0〜2 に、CDM が workgroup id を入れる
    - pds_data_size
    - any_cluster
    - fence 0
  - word1（KERNEL1）: PDS の data の場所。shared_from_pds・common_shared は今と同じ。
  - word2（KERNEL2）: PDS の code の場所。
  - **word3・4・5（KERNEL3〜5）＝ groupCountX−1・groupCountY−1・groupCountZ−1**。今は 0、0、0 に固定している。
  - word6（KERNEL8）: max_instances<<27 | (local_size_x−1)<<18。y・z の local size の field がある場合は、その位置に local_size_y−1・local_size_z−1。
  - その後に fence の kernel（今と同じ）と end の語。stream は 1 dispatch あたり 7＋7＋1 ＝ 15 語で、check の普通の kick（OK）と同じ形になる。
- PDS の program（`powervr_compute_kick_program` の拡張）: DOUTU の前に DOUTW を 1 つ置く。PDS の入力の temps 0〜2（workgroup id）を USC の vtxin 0〜2 へ写す。DOUTU の task の word は今と同じ（temps の field ＝ ceil((temps+1)/4)）。
- compiler: `POWERVR_SHADER_REQUEST_WORKGROUP_ID` を inline の値ではなく、vtxin bank（`POWERVR_USC_BANK_VERTEX_INPUT` r0〜r2）から読む形にする（`MACHINE_MOVE` で temp へ）。buffer の address など dispatch に共通の値は今の inline のままでよい。program は workgroup の数に関係なく 1 つになる。
- 前提の確認（板）: (B) の結果で、1 kernel・fence・end の形は OK と分かっている（1 kernel 32/32）。KERNEL3〜5 を 0 以外にした時の workgroup id の渡り方は未確認。実装の最初の Phase で probe を作る: 1 step の store の program を vtxin r0 で x を足す形に変え、groupCountX ＝ 4 で 4 つの窓に書けるか見る。

| 確認 | 結果 |
| --- | --- |
| `fill-registers.sh`（host、ADDRESS 0x8001c0000） | 43 行、全体 32/32 |
| dump の形の host 確認（使い捨て） | 14 通りすべてで encode 0・hazard 0・bad 0 |
| VF2 の vmunix（`config-visionfive2-compute.mk`、`build/p2q1466/vf2/vmunix`） | rc 0、warning 0 |
| 板 | 未実施（Q1・T1 へ） |

- 片付け（Q1 に依頼）: `build/p2q1466/`。

## q1469-i01（P2、2026-10-20）: workgroup id を coefficient から読む compiler と、板で受け渡しを見る probe

- 前提: P1 の 1 kernel の executor（main 5c2e09d32、`POWERVR_EXECUTOR_ONE_KERNEL`、設計は phase010q の q1467-i01）。workgroup id は PDS の `powervr_compute_workgroup_program` の DOUTW が coefficient cf0〜cf2（common store の 0・1・2）に書く。
- **compiler**（`shader-backend.c`、`shader.h`）:
  - `POWERVR_SHADER_REQUEST_WORKGROUP_ID` の word k は inline せず（`compile_inline` でも）、shared register からも読まない。`MACHINE_MOVE` で coefficient bank の register k から読む。
  - 切り替えは `POWERVR_SHADER_WORKGROUP_COEFFICIENTS`（既定 1）。
  - **注意（Q1・P1 へ）**: vk-queue.c の既定 `POWERVR_EXECUTOR_ONE_KERNEL 0`（workgroup ごとに 1 kernel、PDS が coefficient を書かない）とは組み合わない。1 kernel の executor を既定にするか、旧の形で build するなら `-DPOWERVR_SHADER_WORKGROUP_COEFFICIENTS=0` にする。
- **host の試験の追従**: machine-run に `coefficient_word` の callback を足した（compute の時、bank 3 はそれを読む）。compile-host-test は compute の時に workgroup id を返す。`fill-registers.c` は workgroup 0 として 0 を返す。
- **compute check**:
  - `write_workgroup_kick(state, field, groups)`: 1 kernel の executor の形の kick を作る。PDS は P1 の workgroup program（data 4 語、code 6 語）を最初の probe の slot に置く。kernel 語は workgroups[0]、usc_common 3、pds_temporaries 4。その後に fence の kernel と end。program は `USC_COMPILED`、kick は `KICK_PROBE_FIRST`。
  - compiled fill とその dump の prefix は、この形（workgroup 1）で流す。fill の program が cf0 を読むようになったため。`KICK_COMPILED` は無くした。
  - **`run_workgroup_probe`**（compiled fill の後、executor の並びの前。各 case は自分の session で流す）:
    - 4 workgroup × 32 instance の 1 kernel を流す。手書きの USC program（`workgroup_program`、dump と同じ組み立てと encode）が、1 つの register を読む。値の下位 2 bit を workgroup とし、その値を `+0x1d8000 + workgroup×128 + instance×4` へ store する。
    - 読む場所は 3 通り: cf0、cf2（flat varying の C の +2 の約束の場合）、sh0。
    - 行の形: `compute probes: workgroup id via coefficients <場所> MASK (e), N bytes, first words w0 w1 w2 w3, one kernel after A/32 (e2)`。
    - 期待: cf0 で MASK f、first words は 0 1 2 3。
  - host の使い捨ての確認（kernel の関数をそのまま host で compile、cf0、coefficient ＝ wg）: encode 0、272 byte、hazard 0。128 語すべてが自分の wg id（bad 0）。

| 確認 | 結果 |
| --- | --- |
| p010-compiler の host（`run.sh`） | 455/0、group 20/0 |
| p009 の host | 511/0 |
| `fill-registers.sh` | 全体 32/32 |
| VF2 の vmunix（`build/p2q1469/vf2/vmunix`） | rc 0、warning 0 |
| 板 | 未実施 |

- 片付け（Q1 に依頼）:
  - `build/p2q1469/`
  - scratchpad の `wgtest.c`・`wgtest`・`wg.c`

## q1470-i01（P2、2026-10-21）: fence kernel を Mesa と bit 単位で照合し、変種を check の最初に流す

板の前回（vf2-dmesg62、Q1 の読み）:
- workgroup id の probe は cf0 で MASK f、first words は 0 1 2 3。P1 の DOUTW と cf0 の読みは正しい。
- lockup が多い。16 kernel で lockup しないのは fence 無しだけ。
- vk の 1 kernel の dispatch は `fault 0x8000470010`（buffer は 0x8000047000〜）。

### (1) fence kernel の照合（Mesa 25.0.7）

- 参照:
  - `pvr_cmd_buffer.c` の pvr_compute_generate_fence と pvr_compute_generate_control_stream
  - `pvr_device.c` の pvr_device_init_compute_fence_program（fence＋clear_pds_barrier）
  - `pvr_pds.c` の pvr_pds_compute_shader
  - `csbgen/rogue_cdm.xml`
- PDS の命令は、Mesa の `pvr_rogue_pds_encode.h` の encoder（`pvr_pds_inst_encode_bra`・`_halt`・`_add64`・`_dout(DOUTC)`）と我々の `powervr_compute_fence_program` を、使い捨ての host の program で同じ引数で並べて比べた。

| 語 | Mesa | 我々 | |
| --- | --- | --- | --- |
| PDS code 0（bra IF1 neg keep +2） | c2c00002 | c2c00002 | 同じ |
| code 1（halt、coefficient の pass） | d3000000 | d3000000 | 同じ |
| code 2（add64 const0+const0 → ptemp0） | 80000010 | 80000010 | 同じ |
| code 3（DOUTC、END 0） | f0000005 | f0000005 | 同じ |
| code 4（halt） | d3000000 | d3000000 | 同じ |
| PDS data（fence の定数 0、2 語） | 0、0 | 0、0 | 同じ |
| KERNEL0 | data_size 1、usc_target ANY、fence 1、他 0 → 0x00000007 | 0x00000007 | 同じ |
| KERNEL1 | data の場所、sd_type PDS(1)<<2、usc_common_shared 1<<1 | 同じ | 同じ |
| KERNEL2 | code の場所 | 同じ | 同じ |
| KERNEL3〜5 | global 1 → 0 | 0 | 同じ |
| KERNEL8 | max_instances ＝ pvr_compute_flat_slot_size(0, false, 1) ＝ min(1×8, 32) ＝ 8（ROGUE_CDM_MAX_PACKED_WORKGROUPS_PER_TASK 8）、workgroup size 1 → 0 | 8<<27 | 同じ |
| work の kernel（参考） | sd_type NONE、usc_common_shared 0、max_instances 32→0、usc_target ANY | 同じ | 同じ |

- **結論: fence kernel の語と PDS の program は Mesa と bit 単位で同じ。** そのため (b)「Mesa の bytes に合わせた fence」は (a) と同じになる。(b) は、Mesa の別の分岐 deallocate_shareds＝false（shared を解放しない fence）に替えた。
- Mesa との違いで残るのは、kernel の外側だけ:
  - 我々は work の kernel を 16 個並べる（Mesa は 1 dispatch に 1 kernel）。
  - shared の確保の kernel（sd_type USC、usc_target ALL）は、const shared がある時だけ Mesa が出す。我々は出さない（const shared は無いので Mesa も出さない）。
  - context の作り（静的な state、resume）。

### (2) check の最初に流す fence の変種（`run_fence_variants`）

- 各変種は 16 kernel の 1 step の store で、自分の session で流す。最後に新しい session を開き、残りの check はそこで流す。
- 行の形: `compute probes: fence variant X W/32 (e), one kernel after A/32 (e2)`。e2 が (5) なら、その変種で lockup した。

| 変種 | 中身 |
| --- | --- |
| a (as is, Mesa's bytes) | 今の fence kernel |
| b (keeps the shared registers) | fence の KERNEL1 の usc_common_shared を 0（Mesa の deallocate_shareds false） |
| c (none, the store waits) | fence kernel 無し。1 step の store の program は end の前に wdf を既に持っている（store ごとの wdf）ので、追加は無い |
| d (store kernel with the fence bit) | fence の kernel を、1 step の store の kernel 語（PDS は普通の store の program）に fence の bit だけを立てた物にする |
| e (two fence kernels) | fence kernel を 2 つ続ける |

- `run_many_kernels` に MANY_FENCE_KEEP_SHARED・MANY_FENCE_STORE_PDS・MANY_TWO_FENCES と variant の名前の引数を足した。executor の並びの行は今と同じ。

### (3) vk の fault 0x8000470010（静的な解析。板の dump は未実施）

- vkcompute の fill を 1 kernel の形（wg は cf から、buffer 0x8000047000 は immediate）で compile した machine の operation:
  - r11 ＝ lane（s51）
  - r12 ＝ cf0（wg）
  - r13 ＝ imadd(r12, 32, 0)
  - r9 ＝ imadd(r13, 1, r11)（gid）
  - …
  - r12 ＝ 0x00047000（buffer の下位）、r14 ＝ 0x80（上位）
  - r15 ＝ imadd(r11, 4, 0)（byte の offset）
  - r11 ＝ imadd(r15, 1, 0)
  - r6:r7 ＝ add64(r12, r14, r11)
  - r8 ＝ data
  - st r8 → [r6]
- fault の下位 0x00470010 ＝ (0x47000 ＋ 1)×16。正しい lane 1 の address は 0x47004 で、それを 16 倍した値ではない。**buffer の下位と lane（または gid）の和を 4 bit 左へずらした形**に見える。
  - compiler の命令の列には、16 倍や << 4 の operand は無い。host の simulator では 32/32。
  - 同じ add64（s2 が shift の結果）を使う dump と workgroup id の probe は、板で正しい address に書いた（cf0 の MASK f）。そのため、ずれは **imadd の結果（r15・r11）を add64 の s2 に渡す所**、つまり imadd の結果の形（幅・pair）か読み方が怪しい。
  - これは compiled fill の dump（prefix の r11・r15・r6 の値）で決まる。前回は lockup で走れなかった。fence の変種が通れば、次の起動で同じ check の中で dump が走る。fill-registers.sh の ADDRESS には、板の `run at`＋0x1c0000 を渡す。

| 確認 | 結果 |
| --- | --- |
| PDS の fence program の Mesa の encoder との比較（使い捨ての host） | 5 語とも同じ |
| VF2 の vmunix（`build/p2q1470/vf2/vmunix`） | rc 0、warning 0 |
| 板 | 未実施 |

- 片付け（Q1 に依頼）:
  - `build/p2q1470/`
  - scratchpad の `fence-cmp.c`・`fence-cmp`・`util/`・`ops.c`・`ops`・`wgtest*`・`wg.c`

## q1473-i01（P2、2026-10-21）: fence kernel を外す形を標準に、compiled fill の register の実測と simulator の照合

### (A) fence kernel を使わない形（`POWERVR_COMPUTE_FENCE_KERNEL`、compute-program.h、既定 0）

- 根拠（vf2-dmesg65）: 16 kernel の変種 a・b・d・e（fence の bit のある kernel を含む物）は全部、後の 1 kernel が (5) になった。c（fence 無し）だけが 32/32 (0) で、後の kick も 32/32 (0)。
- 既定 0 で、compute-check の stream から fence kernel を外す。各 USC program は、store の後に wdf を持ったまま end に至る。
  - 新しい関数 `put_fence_kernel`: 1 なら fence kernel を書く。0 なら何も書かない。
  - `write_kick`（全部の普通の kick、probe、grid）と `write_workgroup_kick`（compiled fill、dump、workgroup id）は、これを通す。
  - 「fence and end」の probe（`KICK_FENCE_ONLY`）は、1 の時だけ流す。
- fence kernel を明示する実験（`run_fence_variants`、`run_many_kernels` の既定の並び）は、そのまま残す。
- fence kernel・`powervr_compute_fence_program` を使う所の一覧:

| 場所 | 用途 | 今回 |
| --- | --- | --- |
| compute-check.c `setup` → `powervr_compute_fence_program`（PDS_FENCE の slot） | fence の PDS program | 書くのは残す（実験用） |
| compute-check.c `write_kick` | 全部の check の kick | `put_fence_kernel`（既定で無し） |
| compute-check.c `write_workgroup_kick` | 1 kernel の形の kick | `put_fence_kernel`（既定で無し） |
| compute-check.c `write_probe_streams` / `run_kernel` の「fence and end」 | probe | 既定で流さない |
| compute-check.c `run_many_kernels` / `run_fence_variants` | 実験 | 明示のまま |
| **vk-queue.c**（P1 の file、610 行の `powervr_compute_fence_program`、1031 行の `build_fence` の `kernel.fence = 1`） | Vulkan の submission の最後 | **P1 に要る変更**: `POWERVR_COMPUTE_FENCE_KERNEL` が 0 なら `build_fence` を呼ばず、stream を terminate の語だけで閉じる。compile された program は、store の後に wdf を持つ（back end の MACHINE_STORE の後の MACHINE_WAIT）ので、追加は要らない。 |

### (B) compiled fill の register: 板（vf2-dmesg65 256〜298 行、run at 0x8000042000）と simulator（`fill-registers.sh OUT 0x8000202000`）

- 照合は、simulator が書いた register（値が 0 でない物）だけを比べた。board の未初期化の register は比べない。
- prefix 4〜32 は全 lane で一致した。**最初の食い違いは prefix 36**:

| prefix | 食い違い（lane 1 / 31） | simulator | 板 |
| --- | --- | --- | --- |
| 36 | r12（op 32 ＝ imadd(r13, r1, r2)、r1＝3 は op 30、r2＝0 は op 31） | 3 / 0x5d（3·gid） | 7 / 0xd9（**7·gid**） |
| 40 | r12、r13（op 37 ＝ imadd(r12, r2, r1)） | r12 3·gid、r13 3gid＋7 | r12 21gid＋50（op 37〜39 に r12 を書く命令は無い）、r13 21gid＋57 |
| 52・55 | r8（store の data） | 3x＋7 | 21x＋57 |

- **板の compiled fill の結果は 3x＋7 ではない。** 書いた値は 0x39、0x4e、0x63… で、`+1c0000` は 57 ＝ 21·0＋57（正しくは 7）。address（r6 ＝ base＋lane×4）は正しいので、正しい場所に違う値を書いている。Q1 の「正しい」は読み違い。
- 読み取り:
  - op 32 の乗数 r1 は、3 group 後（op 35 の `r1 ＝ 7`）で書き換えられる。板は、その**新しい値の 7 を使った**。現行の hazard の規則（読みの後 2 group は書かない）は満たしているが、imadd の source は 3 group 以上遅れて読まれている。
  - その後の r12 の変化（7gid → 21gid＋50）も、op 37〜39 の読み書きの時期のずれでしか説明が付かない。host で source の読みの遅れ（0〜5 group）を全部試したが、単純な「遅れた読み」の模型では値を再現できなかった。imadd の結果を書く時期にも遅れがあると見ている（未確定）。
- **compiler の修正**（`shader-backend.c`、`shader-private.h`）: 間隔の規則を `required_gap(earlier, later)` にまとめた。
  - imadd（MACHINE_INTEGER）が読んだ register を後で書く時は、**`SHADER_INTEGER_SOURCE_GAP` ＝ 6 group** 空ける（従来は 2）。
  - imadd が、2 group 以内に書かれた register を読む時も 2 group 空ける（新規）。
  - 他の規則は従来どおり 2。
  - 分岐の後ろ向きの辺には、必要な数（gap−1）の no-operation を入れる。
  - fill は 456 byte・56 group から、584 byte・88 group になった。simulator では 32/32。
  - 6 は余裕を持たせた値。次の板の dump で、prefix ごとの一致を見て詰める。
- 試験の追従: compile-host-test の `test_hazard_spacing` を新しい間隔に合わせた。

### vk の fault（0x8000470010）の候補

| 項目 | compute-check の compiled fill（板で走った） | vk-queue.c の 1 kernel の run（fault） |
| --- | --- | --- |
| shader | vkcompute の fill（`vkcompute_fill_shader`） | vkcompute の fill（456 byte で同じ） |
| buffer の address | DESCRIPTOR の word 0・1 に run＋0x1c0000 を inline | `build_shared` が DESCRIPTOR の word 0・1（shared 3・4）に buffer＋offset を inline。形は同じ |
| workgroup id | cf0（P1 の PDS）、workgroup 1 つ（id 0） | cf0、workgroup 128（id 0〜127） |
| NUM_WORKGROUPS | 1 | 128（shader は使わない） |
| kernel の語 | workgroups[0] 1、usc_common 3 | workgroups[0] 128、usc_common 3 |

- **候補は 1 つ: imadd の読みの時期の食い違い**（上の (B)。executor の側ではない）。
  - 板では op 32 以降の imadd の値が狂う。gid の項（wg×32＋lane）は wg≠0 の時にだけ効くので、compute-check（wg 0）では address が偶然合い、data だけが狂った。vk（wg 0〜127）では、address の計算（r15 ＝ imadd(r11, 4, 0)、その r11 も imadd の結果）に狂った gid が入る。
  - 0x470010 ＝ (0x47000＋1)×16 のような値は、この種の取り違えで出うる。
  - 確認: 上の compiler の修正で、次の板の compiled fill が 3x＋7（32/32）になり、vk の fault が消えれば確定する。

| 確認 | 結果 |
| --- | --- |
| p010-compiler の host | 455/0、group 20/0 |
| p008 の host | encode 94/0、compute 76/0 |
| fill-registers.sh（0x8000202000） | 32/32、584 byte |
| p009 の host | 501/10。**main（変更前の stash）でも同じ 10 個**（POWERVR_EXECUTOR_ONE_KERNEL＝1 の merge 以後の試験の追従漏れ。P1 の範囲） |
| VF2 の vmunix（`build/p2q1473/vf2/vmunix`） | rc 0、warning 0 |
| 板 | 未実施 |

- 片付け（Q1 に依頼）:
  - `build/p2q1473/`
  - scratchpad の `ops2.c`・`ops2`・`ops65.txt`・`sim65.txt`

## q1476-i01（P2、2026-10-21）: 群の読み書きの時期を測る probe（`run_timing_probes`）

- 置き場所: fence の変種の直後（同じ session）。終わったら session を開き直し、残りの check へ進む。
- 形: 各 probe は 1 program・1 kick（1 kernel の形、fence 無し）。group は書いた順のまま encode し、compiler の間隔は付けない。`timing_build` で組み立てる。
  - set-up の operation は、どれも後ろに 8 群の spacing（TIMING_SETTLE）を置く:
    - r0 ＝ lane
    - r10 ＝ lane×4
    - r12:r13 ＝ 自分の語（run＋0x1e0000＋lane×4）
    - r20:r21 ＝ 別の場所（＋0x800）の語
    - r1 ＝ 3、r2 ＝ 5、r22 ＝ 0x100、r23 ＝ 0
  - 試す operation の後に N 群の空き（MACHINE_WAIT）を置き、続けて「もう一方の書き」を置く。その後を 8 群空ける。r4 を r14 へ写して store する（store の probe は store 自身が対象）。
- 行の形: `compute probes: <名前> after N groups: old MASK new MASK, lane 0 V lane 31 V (e, bytes)`。

| 名前 | N | 試す operation → N 群 → もう一方の書き | old（lane L の語） | new |
| --- | --- | --- | --- | --- |
| madd source read | 0〜7 | r4 ＝ imadd(r0, r1, r2) → r1 ＝ 7 | 3L＋5（rB の古い値を読んだ） | 7L＋5（新しい値を読んだ ＝ 読みが N＋1 群より遅い） |
| madd result written | 0〜7 | r4 ＝ imadd(…) → r4 ＝ 0xabc | 3L＋5（madd の結果が上書きより後に着いた ＝ 書きが N＋1 群より遅い） | 0xabc（順序どおり） |
| add64 source read | 0〜3 | r4:r5 ＝ add64(r22, r23, r0) → r22 ＝ 0x200 | 0x100＋L | 0x200＋L |
| add64 result written | 0〜3 | 同上 → r4 ＝ 0xabc | 0x100＋L | 0xabc |
| mov source read | 0〜3 | r4 ＝ mov r22 → r22 ＝ 0x200 | 0x100 | 0x200 |
| mov result written | 0〜3 | r4 ＝ mov r22 → r4 ＝ 0xabc | 0x100 | 0xabc |
| store data read | 0〜3 | st r14(＝0x100) → [r12] → r14 ＝ 0xabc | 0x100 | 0xabc |
| store address read | 0〜3 | st r14 → [r12] → r12 ＝ r20（別の場所） | 自分の語に 0x100 | 別の場所の語に 0x100 |

- host の simulator（順序どおりに動く模型）での期待（使い捨ての確認。kernel の `timing_build` をそのまま host で compile した）:
  - source read は全部 old（ffffffff）。
  - result written は全部 new（ffffffff）。
  - store address は、自分の語が old。
  - encode 0、629〜648 byte、129〜130 群。
- **required_gap の決め方**（板の結果から、op の種類ごとに）:
  - source read で new が出る最大の N を S とすると、その op の source の書き換えまでに要る群は **S＋2**（N 群の空き＋次の群）。今の `SHADER_INTEGER_SOURCE_GAP` 6 と `SHADER_HAZARD_GAP` 2 は、この値で置き換える。
  - result written で old が出る最大の N を W とすると、その op の結果を読む・上書きするまでに要る群は **W＋2**（今の `integer_result_read` の 2 を置き換える）。
  - lane ごとに混ざる場合（MASK が一部）は、全 lane が順序どおりになる N を採る。

| 確認 | 結果 |
| --- | --- |
| host の simulator での timing の program（使い捨て） | 8 種 × N 0〜3 で encode 0、期待どおり |
| VF2 の vmunix（`build/p2q1476/vf2/vmunix`） | rc 0、warning 0 |
| 板 | 未実施 |

- 片付け（Q1 に依頼）:
  - `build/p2q1476/`
  - scratchpad の `timing.c`・`timingtest.c`・`timingtest`

### q1476-i01 の追加（Q1 の 2 通目、板 69）

- 板 69（gap 6）の compiled fill は悪化した: `wrote +1c0000: 00202004`、`+1c000c: 00202004`、0/32。vk は `fault 0x000000f75c6758e0`。
- 変更:
  1. **filler の種類と madd の source の間隔を、実行時に変えられるようにした**（`shader-backend.c` の `shader_filler_kind`・`shader_integer_source_gap`、`shader-private.h` の extern）。
     - 既定は、filler が **MACHINE_NOP**（板で `nop between groups` が ffffffff だった control の no-operation）、間隔は **2** に戻した（`SHADER_INTEGER_SOURCE_GAP` ＝ 2）。
     - 間隔の探索の上限は `SHADER_GAP_MAX` 8。
  2. compute check は、compiled fill（dump の prefix を含む）を **gap 2 と gap 6 の 2 回**、どちらも nop の filler で、同じ起動で流す。行の前に `compiled fill with multiply-add source gap N, no-operation fillers` を出す。終わったら既定の 2 に戻す。
  3. timing の probe の空き（N 群）も nop にした。
- host の確認:
  - compiler 455/0、group 20/0（spacing の試験は、filler の種類と間隔の変数に追従させた）。
  - p009 は 502/0（main の追従の後）。
  - fill-registers.sh（既定の gap 2、nop）は 504 byte・68 群で 32/32。
  - VF2 の vmunix: rc 0、warning 0。

### 板 65（21x＋57）と板 69（0x00202004）を説明する model の候補（板の timing の probe で選ぶ）

確かな観察:
- 板 65: op 32（r12 ＝ imadd(r13, r1, r2)）の結果は 7·gid だった。r1 を 3 から 7 にする op 35 は 3 群後にある。r2 を 0 から 1 にする op 36 は 4 群後で、これは結果に入っていない（lane 0 の結果は 0）。
- 板 65: op 37〜39 に r12 を書く命令が無いのに、r12 が 7gid から 21gid＋50 に変わった。
- 板 69: store の data（r8）に、隣の lane の address の下位語（r6 の値 0x00202004）が入った形になった。

| model | 板 65 の説明 | 板 69 の説明 | timing の probe で出る形 |
| --- | --- | --- | --- |
| M1: imadd の source の読みが遅い（発行から D 群後の register の値を読む。乗数の場所の source は D≈3、加数の場所の source は ＜4） | op 32 の 7gid は説明できる。r12 の後の変化は説明できない | gap 6 では読みの遅れが間に合うはずなので、悪化は説明できない | `madd source read after N` で、N ＜ D の時に new |
| M2: imadd の結果の書きが遅い（L 群後に着き、その間に書かれた値を上書きする） | r12 が書く命令の無い所で変わったことは説明できる（遅れて着いた結果）。値の 21gid＋50 は、遅れて計算された op 37 の結果が r12 の場所に着いたとすれば近い | spacing の群が増えると、着く時期と後の命令（add64 の address の対、store の data の対）の重なり方が変わる。data の register に address の片割れが入る形も説明できる | `madd result written after N` で、N ＜ L の時に old（madd が後に着く） |
| M3: wdf（MACHINE_WAIT）の filler 自体が、register の内容を変えたり順序を崩したりする | 板 65 の filler も wdf だった | gap 6 で wdf が増えて悪化したことと整合する | gap 2 と 6 の nop 版の compiled fill の比較で分かる（nop で両方とも直れば M3） |
| M4: store の data ＝ address＋2 の規則と、imadd の結果の register の位置の衝突（r6:r7 の直後の r8 に、遅れて着いた値が入る） | 説明しない | 0x00202004（r6 の値）が data に出た形は、M2 の遅着と組み合わせれば説明できる | `store data read` と `store address read`、および M2 の形 |

- 判定の順序:
  1. 次の起動の `compiled fill with … gap 2`（nop）と `… gap 6`（nop）がどちらも 32/32 なら **M3**（wdf の filler が原因）。
  2. それ以外なら、`madd result written after N` の old が出る最大の N から **M2** の L を読む。`madd source read after N` の new から **M1** の D を読む。
  3. required_gap は、結果側が L＋2、source 側が D＋2 で決める。

- 片付け（Q1 に依頼）: `build/p2q1476/`、scratchpad の `timing.c`・`timingtest.c`・`timingtest`。

## q1479-i01（P2、2026-10-21）: filler の種類ごとの timing の probe、compiled fill の filler×gap、store address の判定

板 72（Q1 の読み）:
- nop を N 個入れても、mov・add64・store の source は N＝3 でも新しい値を読んだ。
- madd だけは N≥4 で古い値を読んだ（N＝0〜3 は lane 0 だけが古い）。
- 結果の遅れは、どの op でも無かった。
- compiled fill は gap 2 と gap 6（どちらも nop）で 0/32。
- 疑い: nop の群は実行の時間を持たない。

### 変更（compute-check.c）

1. **timing の probe を filler の 4 種で流す**。
   - filler: (a) `nop`（MACHINE_NOP）、(b) `wdf`（MACHINE_WAIT）、(c) `mov`（r30 ＝ r31、無関係な ALU）、(d) `madd`（r30 ＝ r31·r31＋r31）。
   - r31 ＝ 1 を set-up で置く。temps は 32。
   - N は 0〜5（TIMING_GAPS 6）。8 種の probe について、4 × 8 × 6 ＝ 192 kick。
   - 行: `compute probes: <名前> after N <filler>: old MASK new MASK, lane 0 V lane 31 V (e, bytes)`。
2. **kick ごとの tag**（(3) の修正）。各 kick の値（r2 ＝ 5、r22 ＝ 0x100、新しい値、0xabc）の上位 16 bit に、kick の番号を足した。
   - 板 72 の `store address read` は N＝1〜3 で old と new が両方立った。これは、前の kick（N＝0、別の場所に書いた）が残した 0x100 が、cache から遅れて書き戻された物と読んだ（CPU が書いた filler の後に現れた）。
   - tag があれば、前の kick の値はどちらにも数えない。
3. **compiled fill を filler 2 種（nop、wdf）× gap 3 種（2、4、6）の 6 回**、同じ起動で流す。
   - 行: `compiled fill with multiply-add source gap N, nop|wdf fillers`。
   - dump の prefix は最初（nop・gap 2）だけで流す（`run_compiled_fill(state, dumps)`）。
   - 終わったら既定（nop、2）に戻す。
   - ALU の filler（mov・madd）を compiler に入れるのは、timing の結果で効くと分かってからにする。scratch の register の予約と field の拡大が要るため。

### required_gap と filler の決め方（次の板の表から）

| 見る行 | 読み方 |
| --- | --- |
| `<op> source read after N <filler>` | new が消える（全 lane が old になる）最小の N を、filler ごとに見る。その filler で、その op の source を守るのに要る数 |
| `<op> result written after N <filler>` | old が出る N があれば結果が遅い。無ければ、結果の読みは間隔なしでよい |
| filler の比較 | N が小さい filler ほど時間を稼ぐ。nop で new が消えないなら nop は数えない |
| `compiled fill with gap N, <filler>` | 32/32 になった組み合わせを compiler の既定（`shader_filler_kind`・`shader_integer_source_gap`）にする |

| 確認 | 結果 |
| --- | --- |
| timing の program を host の simulator で（使い捨て。kernel の `timing_build`・`timing_expected` をそのまま host で compile） | 192 通りすべて encode 0・期待どおり（source は全 old、result は全 new、store address は自分の語） |
| VF2 の vmunix（`build/p2q1479/vf2/vmunix`） | rc 0、warning 0 |
| 板 | 未実施 |

- 片付け（Q1 に依頼）: `build/p2q1479/`、scratchpad の `timingtest.c`・`timingtest`。

## q1481-i01（P2、2026-10-21）: wdf の encoding、filler を実命令に、Mesa の compiler の実際の命令列

### (a) wdf の encoding

- 我々の wdf の group（MACHINE_WAIT）は `02 80 6a ff`。Mesa 26.2.5 の生成器（`pco_isa.h` の `pco_igrp_hdr_control_encode`）で、wdf（ctrlop 0b0101、cc e1_zx、miscctl ＝ drc 0、length 2）を encode すると `02 80 6a` になり、我々のものと同じ。続く 1 byte は word の padding の 0xff。
- 下の Mesa の compiler の実際の出力にも、この group（`02 80 6a ff`、group 16）がある。
- store は drc 0（Q1 の Mesa 25.0 の rogue_isa との照合とも一致）。
- **wdf の encoding に違いは無い。** 候補の encoding を板で比べる必要も無い。
- 板 75 の結果は「wdf は store の data の読みを待たない」とは読まない。wdf が待つのは data return counter（load の戻りなど）の完了で、store の source の読みの時期を保証する物ではない、と読む。

### (b) filler を実命令に、間隔を種類ごとに（shader-backend.c、shader-private.h）

- **filler**: `shader_filler_kind` の既定を MACHINE_MOVE にした（r11 ← r11、`TEMP_FILLER` 11 を予約し、値は r12 から）。MACHINE_MOVE_SPECIAL（r11 ← s0）も選べる。
  - `filler_operation()` は filler を見分ける。filler とは何も待たず、何にも待たれず、in-place の数にも入れない。
- **距離は時間を持つ群で数える**（`shader_backend_gap_needed`）。wdf と nop は数えない（板 75）。
- **間隔は読む側の種類ごと**:
  - madd（MACHINE_INTEGER）の source: `shader_integer_source_gap` ＝ 4
  - store の data・address: `shader_store_source_gap` ＝ 6
  - 他の ALU: `shader_alu_source_gap` ＝ 3
  - madd の結果の読み、madd の新しい値の読み: 2
  - 上の値は板 75 の mov／madd の filler の結果から決めた。探索の上限は `SHADER_GAP_MAX` 10。
- compiled fill（vkcompute）は 752 byte・83 群・17 temps。host の simulator では 32/32。
- **compute check の compiled fill**: filler（mov、mov-special）× 間隔（既定 4/3/6、大きめ 6/5/8）の 4 回を同じ起動で流す。dump の prefix は最初の回だけ。行の前に `compiled fill with gaps madd A alu B store C, mov|mov-special fillers`。

### (c) Mesa の compiler の実際の命令列（ユーザーの指摘への対応）

- **Mesa 25.0 の rogue の offline compiler**（`rogue_vk_compiler`）は scratchpad で build できた。ただし対応する stage は frag と vert だけで、compute は compile できない。Mesa 25.0 の driver の compute は rogue では無い。
- 代わりに、**Mesa 26.2.5 の Vulkan driver（pco の compiler）を host で build** して、**drm-shim**（`libpowervr_noop_drm_shim.so`、`PVR_SHIM_DEVICE_BVNC=36.50.54.182` ＝ BXE-4-32）の上で、userland の vkcompute（host 用に build）を走らせた。`PCO_DEBUG_PRINT=cs,binary` で IR と binary を取った。
  - build に要った host の package（sudo apt）: llvm-19-dev、libclang-19-dev、libclc-19(-dev)、libllvmspirvlib-19-dev、llvm-spirv-19、libdrm-dev、glslang-tools、spirv-tools。
  - meson: `-Dvulkan-drivers=imagination -Dtools=drm-shim,imagination -Dllvm=enabled -Dmesa-clc=enabled`。
  - 結果の全文（IR と binary）: `plan/ws269/tests/p008/mesa-pco-fill-bxe-4-32.txt`。
- Mesa の命令列（block B2、if (vi0 < 32) の中）:

| 群 | Mesa | 我々の同じ op の bytes |
| --- | --- | --- |
| 6 | `r0 = cf0`（mbyp）`35 82 00 87 c0 04 00 00 00 40` | 同じ（length・da の値は padding の分だけ違う） |
| 7 | `r0 = r0 << sc5`（bitwise）`55 d2 40 00 02 80 a0 80 85 40` | 同じ |
| 8 | `r0 = imadd32 r0, sc1, vi0`（**in-place、直前の群の結果を読む**） | 同じ |
| 9 | `r1 = r0 << sc2` | 同じ |
| 10 | `r0 = imadd32 r0, sc3, sc0`（in-place） | 同じ |
| 11 | `r4 = imadd32 r0, sc1, sc7`（直前の群の結果を読む） | 同じ |
| 12 | `r0 = sh1` | 同じ |
| 13 | `r0:r1 = add64_32.s sh0, r0, r1`（**in-place の pair**） | 1 bit だけ違う（byte 3 が e8 と e0。`.s`） |
| 14 | `r2:r3 = add64_32.s r0, r1, sh3` | 同じく `.s` |
| 15 | `st.lazywriteback r4 → [r2..3]`（data ＝ address＋2） | cache の mode の 1 bit が違う（byte 4 が 66 と 64） |
| 16 | `wdf drc0` | 同じ |
| 18 | `nop`（end） | 同じ形 |

- **Mesa の規則（この命令列から）:**
  - 群の間に **spacing は一つも無い**。直前の群の結果をそのまま読み、in-place（同じ群で読んで書く、64-bit の pair も）を使う。
  - imadd は special の定数（sc1・sc3・sc7）と vertex input（vi0）を直接読む。
  - store の後に wdf を置き、program の最後に nop.end を置く。
  - つまり、我々が板の結果から足してきた規則（in-place の禁止、2 群の間隔、imadd の special の禁止、filler）は、**Mesa は使っていない**。
- **板で確かめる probe（`run_mesa_form`）**: Mesa の群 6〜16 を、同じ register・同じ定数・spacing 無しで並べた program を 1 kernel 4 workgroup で流す。入力（vi0 → r11 の lane、sh0/sh1 → r8/r9、sh3 → r10 ＝ 0）は、前に置いて落ち着かせる。
  - 我々の encoder の bytes は、上の表のとおり Mesa と同じ（`.s` と lazywriteback を除く）。
  - 行: `compute probes: mesa-form fill N/128 (e), bytes, words 0 … 1 … 32 …`。
  - host の simulator: 128/128、424 byte・48 群（simulator の special の定数は Mesa の表のとおり、index 0〜31 ＝ 値にした）。
  - **判定**: 128/128 なら、Mesa の規則（spacing 無し）で hardware は正しく動く。我々の compiled fill の誤りは、hazard ではなく、Mesa と違う所（命令の選び方・register の配置・`.s`・cache の mode など）にある。その時は、back end を Mesa の列に合わせる。128 にならなければ、Mesa と我々の実行の環境の違い（PDS・kernel の語・shared の載せ方）を疑う。

### (d) fence

- fence variant a は毎回 check の最初に流れる（`run_fence_variants`）。wdf の encoding は Mesa と同じなので、wdf の修正は無い。板 75 では a・b・d・e の lockup が続いている。
- Mesa の stream（fence kernel を含む）の作りとの照合は、次の段にする。drm-shim で `PVR_DEBUG` の CSB dump が取れれば、Mesa の compute の control stream も取れる見込み。

| 確認 | 結果 |
| --- | --- |
| p010-compiler の host | 455/0、group 20/0 |
| p009 の host | 502/0 |
| mesa-form（host の simulator、使い捨て） | 128/128 |
| fill-registers.sh（既定、mov の filler） | 32/32、752 byte |
| VF2 の vmunix（`build/p2q1481/vf2/vmunix`） | rc 0、warning 0 |
| 板 | 未実施 |

- 片付け（Q1 に依頼）:
  - `build/p2q1481/`
  - scratchpad の `mesa25/`（rogue の build）、`mesa26/`（26.2.5 の build と vkcompute-host、run1.txt）。mesa26 は次の Mesa の参照に使えるので、残すかどうかは Q1 の判断。
  - scratchpad の `gen/`・`hdr*`・`wdf*`・`cmp*`・`mftest*`・`mesaform.c`・`mesa-groups.txt`・`timingtest*`

### q1481-i01 の追加（Q1 の 3 通目）: lockup する probe を opt-in に、compute の stage は手書きの kernel で決める

- **`POWERVR_COMPUTE_LOCKUP_PROBES`**（compute-program.h、既定 0）。0 の時、compute check は次を流さない:
  - fence の変種（`run_fence_variants`）
  - executor の並び（`run_layouts`。既定の並びは fence kernel 付き）
  - timing の probe（`run_timing_probes`。どれが lockup したかは板で分けていないので、全部止めた）
  - nop の probe（`run_nop_probe`）
  - 「fence and end」の probe（`POWERVR_COMPUTE_FENCE_KERNEL` が 1 の時だけ。今回も止めたまま）
- 既定の build で流す物:
  - 手書きの kernel
  - coherency の rounds
  - 整数の probe と short store
  - temporaries
  - mesa-form fill
  - compiled fill（1 kernel の形、4 通りの filler・間隔。dump は最初だけ）
  - workgroup id の probe
- **compute の stage は手書きの kernel だけで決める**。check の最後に、kernel が 0 で全語を書いていれば `powervr_stage_ok(POWERVR_STAGE_COMPUTE)`（記録を ok に上書きする）、そうでなければ `powervr_stage_fail(…, powervr_stage_reason_of(e))`。
  - compiled fill などの結果は、それぞれの行で報告するだけ。probe の reset が Vulkan の device を閉じたままにしない。
  - kernel が ETIMEDOUT で check を途中で止める経路は、従来どおり stage に触らない。
- VF2 の vmunix（`build/p2q1481/vf2/vmunix`）: rc 0、warning 0。板は未実施。

## q1486-i01（P2、2026-10-21）: (1) 既定を板で 32/32 の組に

- 板 79 の compiled fill:
  - mov の filler、間隔 4/3/6 → 0/32
  - mov の filler、6/5/8 → 32/32（920 byte）
  - mov-special の filler、4/3/6 → 32/32
  - mov-special の filler、6/5/8 → 32/32
- mesa-form は 0/128（何も書かない）。
- **既定を、filler `MACHINE_MOVE_SPECIAL`（r11 ← s0）、間隔 madd 6・alu 5・store 8 にした**（shader-private.h、shader-backend.c）。
  - compute check の compiled fill は、最初の回（dump 付き）がこの既定になる。続いて mov-special の 4/3/6、mov の 6/5/8、mov の 4/3/6 の順に流れる。
- host: compiler 455/0、group 20/0。fill-registers は 32/32（1,200 byte）。VF2 の vmunix（`build/p2q1486/vf2/vmunix`）は rc 0、warning 0。
- **p009 の host が 491/3 になった**（変更前は 502/0）。
  - 原因: vk-queue.c の `QUEUE_USC_ROOM` 0x400（1 KiB）に、p009 の fill の program が収まらない。`powervr: vk: the compute shader did not compile (28)`、28 ＝ ENOSPC。
  - 板の compute check の inline の fill は 920 byte で、1 KiB に近い。vkcompute でも溢れうる。
  - **P1 への依頼（vk-queue.c）**: `QUEUE_USC_ROOM` を 0x800 に上げる（`QUEUE_USC_BYTES` ＝ room × QUEUE_KERNELS_MAX。1 kernel の executor では kernel は少ないので、総量は問題にならない見込み）。

### q1486-i01 (2)(3): register の窓の重なりの probe、mesa-form の変種（既定の build で流れる。lockup の probe ではない）

- **(2) `run_field_probes`**: madd source read（nop の filler、N ＝ 0〜3、timing の probe と同じ program、32 temps）の kick の task field だけを変える。
  - 変える値は 5 通り: 計算どおり（`powervr_compute_task_field(32)` ＝ 9）、＋1、＋2、×2、×4（上限は 0x3f）。
  - 行: `compute probes: field <名前> (F) madd source read after N nop: old MASK new MASK, lane 0 V lane 1 V (e)`。
  - **読み方**: 計算どおりの field で new（lane 1〜31）が出て、広げた field で new が消えて全 lane が old になれば、instance の register の窓が重なっている（field が狭い）。
    - その場合は `powervr_compute_task_field` と、P1 の vk-queue の `powervr_compute_temporaries_field(temps, 2, …)` を直し、間隔を 0 に戻せる。
    - どの field でも同じなら、仮説は外れ。
- **(3) mesa-form の変種**（`mesa_form_build(program, variant, …)`、4 workgroup ずつ）。行: `mesa-form fill <変種> N/128 (e), bytes, words 0 … 1 … 32 … 127 …`。
  - `as is`: Mesa の群 6〜16 のまま（sc の定数、in-place、spacing 無し）。
  - `constants in temporaries`: sc0・1・2・3・5・7 を temps r12〜r17 から読む。
  - `fresh registers`: 上に加えて、どの群も読む register を書かない。store の pair は r20:r21、data は r22。
  - `spaced`: 上に加えて、各群の後に 8 群の filler。
  - 読み方:
    - `as is` だけ 0 で `constants in temporaries` が通る → sc の定数を読む形（imadd・shift の special の source）に問題がある。
    - `fresh` から通る → in-place（とくに add64 の pair）に問題がある。
    - `spaced` だけ通る → 時期の問題。
    - どれも 0 → 入力の代替（cf0・r11 の lane・r8/r9 の address）か、kick の形の側に問題がある。
  - 板 79 の `as is` が 0/128 で、words が deaddead のまま（どこにも書かない）だった。window の filler は変種ごとに値を変えた（deaddead＋変種の番号）。
- host の simulator（使い捨て）: 4 変種とも 128/128、880〜1,456 byte。
- main の一時的な static assert（vk-queue.c の QUEUE_USC_ROOM 0x800 × 128 kernel）は、main 2244b19ca（QUEUE_KERNELS_MAX 64）で直ったので、それを merge して build した。VF2 の vmunix（`build/p2q1486/vf2/vmunix`）は rc 0、warning 0。板は未実施。

### q1486-i01 の続き（板 81）

- 板 81: compute check の `mesa-form fill`（as is）が **fault 0x400000000**（context 0x2、dm 4）を出し、session が lost になった。以降の compiled fill の 4 通りは (5) で走れなかった。
- vkcompute: 3489/4096。word 0 が 0x00047000（buffer の address の下位語）。
- 変更（compute-check.c）:
  1. **mesa-form を opt-in にした**（`POWERVR_COMPUTE_LOCKUP_PROBES`。compute-program.h の注記にも fault を追記）。既定の build で流れる fault・lockup の probe は、これで無い見込み。
  2. **compiled fill を 1 kernel × 4 workgroup でも流す**（`run_compiled_fill(state, dumps, groups)`、`COMPILED_GROUPS` 4）。
     - 既定の filler・間隔の 4 通り（1 workgroup）の後に流す。workgroup id 1〜3 の instance が cf0 から address を作る道を見る。
     - 行: `compiled fill G workgroups N/M (e), bytes, temps, field, words 0 … 32 …`。
  3. **register の窓の probe（`run_field_probes`）は compiled fill の後**、workgroup id の probe の前へ移した（compiled fill の判定を先に守るため）。
- vkcompute の word 0 ＝ address の下位語について（静的な見立て、板の 4 workgroup の結果で確かめる）:
  - store は data を s0（r8）と s3 の範囲（address の pair r6:r7 と、その次の r8）の両方で渡す。Mesa も同じ形（`st s0=r4, s3=r2..4`）。
  - data の位置に address の下位語が入るのは、次のどちらかの場合: store が data を読む時期に、その register がまだ前の値（address の下位語を置いた時期の値）だった。または、別の instance・workgroup の窓の r6 を読んだ（窓の重なりの仮説、(2) の field の probe で分かる）。
  - 4096 語のうち 607 語だけが誤るのは、workgroup の重なり方（task に複数の workgroup が詰められる）とも整合する。
- VF2 の vmunix（`build/p2q1486/vf2/vmunix`）: rc 0、warning 0。板は未実施。

## q1488-i01（P2、2026-10-21）: 4 workgroup の compiled fill の順序と、compiled fill の cf0 の読みの probe

- 板 83:
  - compiled fill（1 workgroup）は、1,200・944・920 byte の 3 設定で 32/32。752 byte（mov、4/3/6）は 0/32。
  - **4 workgroup の版**は fault（0x8000a0a000、buffer の 0x9c8000 先）で 0/128。session が lost になり、field の probe と vkcompute（128 wg）は (5)・-4 になった。
- 変更（compute-check.c）:
  1. **順序**: compiled fill（1 workgroup × 4 設定）→ field の probe → 手書きの workgroup id の probe（変種ごとに session を開き直す）→ **新しい session で** `run_cf0_probe` → **新しい session で** compiled fill の 4 workgroup 版。field の probe が先に読める。
     - `fresh_session()` を足した（teardown して setup。開けなければ行を出して、その probe を飛ばす）。
  2. **`compile_fill()`** を `run_compiled_fill` から切り出した（buffer の address を inline、workgroup id は coefficient から、program と field を返す）。
  3. **`run_cf0_probe`**: compiler が出した fill の群を、最初の coefficient bank からの mov まで（その群を含む）そのまま使う。その後に、手書きの workgroup id の probe と同じ尾を付ける（値の下位 2 bit で窓を選び、値を lane の語に store）。
     - 1 kernel × 4 workgroup（`write_workgroup_kick`、field は 34 temps）で流す。
     - 行: `compute probes: compiled fill's workgroup id (its first K groups, rN) MASK (e), bytes, first words w0 w1 w2 w3`。期待は MASK f、0 1 2 3。
- 手書きの probe との比較（静的）:
  - **cf0 の mov の encoding は同じ**。どちらも `MACHINE_MOVE`、source は coefficient bank の index 0 で、`shader_backend_group` の同じ経路を通る。
  - **kick の形も同じ**（`write_workgroup_kick`。PDS は P1 の workgroup program、usc_common 3、pds temps 4）。
  - 違うのは次の 3 点:
    - fill では cf0 の mov の前に lane の mov（s51）がある。
    - task field: fill は 17 temps で 5、手書きは 15 temps で 4、cf0 の probe は 34 temps。
    - 群の間隔の付け方（compiler の filler・間隔）。
- 読み方:
  - cf0 の probe が f → compiler の cf0 の読みと kick の形は正しい。fill の後半（gid・address の計算の imadd の列）で wg≠0 の時に壊れている。field の probe と合わせて、窓の重なりかどうかを見る。
  - cf0 の probe が f にならない → field（temps が多い kick）か、compiler が並べた先頭の群の形が原因。
- VF2 の vmunix（`build/p2q1488/vf2/vmunix`）: rc 0、warning 0。板は未実施。
- 片付け（Q1 に依頼）: `build/p2q1486/`、`build/p2q1488/`、scratchpad の `cf0.c`。

## q1490-i01（P2、2026-10-21）: temporaries の field の単位、compiler の間隔を 0 に

- 板 84 の field の probe（32 temps の madd、nop）:
  - field 9・10・11・18 は lane 1〜31 が新しい値を読んだ。
  - field 36 は全 lane が古い値を読んだ（正しい）。
  - → **instance ごとの register の窓が重なっていた**。cf0 の probe も、wg 1 が 2 を読んだ（d）。
  - 「hazard」「nop・wdf が効かない」「lane 0 だけ正しい」「21x＋57」「mesa-form が動かない」は、同じ一つの原因と読む。
- **Mesa の定義**: `pvr_pds_setup_doutu`（pvr_pds.c）は temps を `DIV_ROUND_UP(usc_temps, PVR_ROGUE_PDSINST_DOUT_FIELDS_DOUTU_SRC0_TEMPS_ALIGNSIZE)`（＝4）にして、bit 35 から置く（pvr_rogue_pds_defs.h の `…TEMPS_ALIGNSIZE (4U)`、`…TEMPS_SHIFT (35U)`）。compute の pipeline は `cs_data->common.temps` を渡す（pvr_arch_pipeline.c）。
  - つまり **Mesa は 4 register 単位で、我々の従来の計算と同じ**。板では 4 倍が要った。
  - 単位そのものが違うのか、別の設定（core の USC の割当の単位、firmware の設定、kernel の語の何か）が違うのかは未確定。下の掃引で、要る値を板で決める。
- 変更:
  1. **`powervr_compute_task_field(temps)` ＝ temps＋1 を 1 register 単位**（compute-program.c。`powervr_compute_temporaries_field` が単位 1 も受ける。62 temps まで）。
     - p008 の host の期待値を 9・16・17 に直し、63 は拒否する試験を足した（77/0）。
  2. **compiler の間隔を全部 0、in-place の staging も無し**（Mesa と同じ）。
     - `SHADER_INTEGER/ALU/STORE_SOURCE_GAP` 0、新しい `SHADER_RESULT_GAP` 0、`SHADER_IN_PLACE_STAGING` 0。変数（`shader_result_gap`、`shader_in_place_staging` など）で入れ直せる。
     - compile-host-test の spacing の試験は、変数で間隔を入れて流し、終わったら既定に戻す（455/0）。
     - vkcompute の fill は **360 byte・34 群**（host の simulator で 32/32）。
  3. **compute check の掃引**（既定の build で流れる）:
     - compiled fill（1 workgroup）を field 計算どおり（18。dump 付き）、17、18、20、24 で。
     - field の probe（32 temps の madd、N＝0）を、計算どおり（33）、32、33、34、36、40、48 で。
     - 手書きの workgroup id の probe、cf0 の probe（field は計算どおり 35）。
     - compiled fill（4 workgroup）を、計算どおり、17、18、20、24 で、各々新しい session で。
     - 行には field の値を出す（`compiled fill G workgroups … field F`、`field F (as computed) madd source read after 0 nop: …`）。
- 読み方: 全 lane が old になる最小の field と、compiled fill が 32/32・128/128 になる最小の field から単位を決める（17 temps で 18 なら 1 register 単位で＋1）。
- **P1 への依頼（vk-queue.c）**: `powervr_compute_temporaries_field(temps, 2, …)` を `powervr_compute_task_field(temps, …)` に替える。今のままでは 17 temps で 9 になり、窓が重なるので、vkcompute は 4096/4096 にならない見込み。
- host: p008 は 94/0・77/0、compiler は 455/0・20/0、p009 は 502/0、fill-registers は 32/32。VF2 の vmunix（`build/p2q1490/vf2/vmunix`）は rc 0、warning 0。板は未実施。

## q1492-i01（P2、2026-10-21）: field を Mesa の計算に戻し、task size（KERNEL8 max_instances）を掃引

- 板 85:
  - compiled fill（1 wg）は field 17・18 で 0/32、20・24 で 32/32。
  - madd の probe（32 temps）は field 32〜48 で全部正しい。
  - 4 wg は、field を広げても直らない（17 → 33/128、24 → 64/128、18・20 → fault）。
- **Mesa（25.0.7 pvr_cmd_buffer.c）の task size の計算**:
  - `pvr_compute_update_kernel` は `work_size = MAX2(workgroup の大きさ, ROGUE_MAX_INSTANCES_PER_TASK 32)` にする。
  - その後 `info.max_instances = pvr_compute_flat_slot_size(pdevice, coeff_regs, false, work_size)` を呼ぶ。この関数は `total_workitems >= 32` の時、32 を返す。
  - KERNEL8 では 32 を 0 と書く（`if (info->max_instances == ROGUE_MAX_INSTANCES_PER_TASK) kernel8.max_instances = 0`）。
  - **つまり 32 instance の workgroup では、Mesa も max_instances 0**。我々（P1 の表）と同じ。
  - usc_common は coeff_regs（workgroup id の 3）、usc_unified は unified store の数（vi0 を使う時）。fence kernel の 8 は `flat_slot_size(0, false, 1)`。
  - Mesa の値と違う所は、kernel の語の上では見つからなかった。
- 変更:
  1. `powervr_compute_task_field` を **Mesa どおり ceil(temps/4)** に戻した（＋1 も無し。p008 の host の期待値を 2・4・4 に、253 の拒否に直した。77/0）。
  2. `check_state.max_instances`（既定 0）を足した。`write_workgroup_kick` の kernel 語の max_instances に入る。`run_compiled_fill(state, dumps, groups, field_override, max_instances)`。
  3. **compute check の掃引**（各々新しい session）:
     - compiled fill（17 temps、Mesa の field ＝ 5）を 1・4・128 workgroup × max_instances 0（Mesa）・16・8・4 で。
     - 対照として、field 24・max 0 を 1・4・128 で。
     - 先頭の 1 wg（dump 付き）は Mesa の field・max 0。
     - 行: `compiled fill G workgroups, max instances M: N/T (e), bytes, temps, field F, words 0 … 32 …`。
- 読み方:
  - max_instances を下げて 4・128 wg が通れば、1 task（slot）に詰める instance が register の予算を超えている（Q1 の見立て）。
  - どれも通らず、field 24 の対照だけが 1 wg で通るなら、予算の元（USC の register の総量や割当の単位の設定）が Mesa の driver と違う。firmware・core の設定の側を調べる。
- VF2 の vmunix（`build/p2q1492/vf2/vmunix`）: rc 0、warning 0。板は未実施。

## q1494-i01（P2、2026-10-21）: Mesa が実際に出す compute の stream・PDS と我々の bit 単位の比較

- 板 86: max_instances を変えても直らない。1 wg × field 24 だけが通る。
- **取り方**:
  - host の Mesa 26.2.5（scratchpad の mesa26、drm-shim、BVNC 36.50.54.182 ＝ BXE-4-32）で vkcompute を `PVR_DEBUG=cs` で走らせた。
  - Mesa の dumper は global offset が無くても KERNEL9〜11 を読むので、2 つ目以降の kernel の解読がずれる。scratch の Mesa の dumper だけを直した（offset がある時だけ読む）。
  - dispatch の PDS program は、scratch の Mesa の `pvr_arch_pipeline.c` に表示を足して取った。
  - 全文: `plan/ws269/tests/p008/mesa-pco-vkcompute-stream-bxe-4-32.txt`。
- **Mesa の vkcompute の compute stream ＝ kernel 3 つ＋終わり**:
  - K0（shared の確保）: usc_common 64 byte、pds_data 32 byte、usc_target ALL、sd NONE、usc_common_shared 1、1 wg、max_instances 8、size 1。
  - K1（dispatch）: 下の表。
  - K2（fence）: pds_data 16 byte、ANY、fence 1、sd PDS、shared 1、max 8。
- **K1（dispatch）の kernel の語**（我々の値は `powervr_compute_kernel_words`、workgroups 128、usc_common 3、pds temps 4、data 4 語）:

| 語 | Mesa | 我々 | 違い |
| --- | --- | --- | --- |
| KERNEL0 | 0x0004110a（common 1、**unified 1（16 byte）**、pds temp 1、**data 2（32 byte）**、ANY） | 0x00040106（unified 0、data 1） | unified size（Mesa は vi0 に local id）、data size |
| KERNEL1 | 0x00002648（data の場所、**sd_type USC（bit 3）**） | 0x00002640（sd NONE） | sd_type |
| KERNEL2 | 0x00002660 | 同じ形 | 無し |
| KERNEL3〜5 | 0x7f、0、0 | 同じ | 無し |
| KERNEL8 | 0x007c0000（max 0、size_x 31） | 同じ | 無し |

- **K1 の PDS program**:

| | Mesa | 我々（P1 の `powervr_compute_workgroup_program`） |
| --- | --- | --- |
| data 0〜1（DOUTU） | 0x00007a00、0x00000010（temps 2 ＝ ceil(6/4)、sample rate 0） | **同じ値**（同じ address・field の時） |
| data 2〜3（DOUTW の制御） | 0x10000002、0x90004001 | 同じ |
| data 4 | 0x80000000（local id を unified store へ書く DOUTW） | 無し |
| data 5〜7 | base workgroup の 0 | 無し |
| code | c2c00007（bra +7）、9001xxxx ×3（base workgroup の add32）、f0024001、f0034101（DOUTW 2 つ）、d3000000（halt）、**f0044001（local id の DOUTW）**、**fc000002（DOUTU、cc 1）**、d3000000 | c2c00004、f0024001、f0034101、d3000000、**f4000002（DOUTU、cc 0）**、d3000000 |

- **違う bit の一覧**:
  1. KERNEL0 の unified size（Mesa 1 block）。Mesa は local id を vi0 に置くため。
  2. KERNEL0 の pds data size（2 と 1。data の語数の違い）。
  3. KERNEL1 の sd_type（Mesa は USC。K0 が shared を確保する形）。
  4. PDS の instance の pass の local id の DOUTW（unified store へ）。
  5. DOUTU の cc bit（bit 27）。
  6. base workgroup の add32 ×3（値は 0）。
  7. stream の前の K0（shared の確保の kernel）。
  - USC の temps の field（DOUTU）は Mesa と同じ。KERNEL8 も同じ。
- **板の probe**（compute check、各々新しい session、compiled fill ＝ vkcompute の fill、Mesa の field、max 0）:
  - temps 16（nop の filler。r11 も値に使う。既定）と、temps 17（mov の filler 用に r11 を空ける）。
  - 各々、unified 0 と unified 4 register（Mesa の 16 byte）で。
  - **Mesa の PDS をそのまま**（data 8 語。DOUTU の語は我々の address と field にする。code 10 語、code は 32 byte 先、unified 16 byte）で、sd NONE（101）と sd USC（102）を流す。
  - これらを各々 1・4・128 wg で流す。
  - 行: `compiled fill G workgroups, max instances 0, unified U, mesa pds P: N/T (e), bytes, temps T, field F, …`。
- **compiler**: filler の既定を MACHINE_NOP にした（間隔 0 では filler は入らない）。値は r11 から使う（`TEMP_FIRST_VALUE` は filler が mov の時だけ r12）。vkcompute の fill は **16 temps**・360 byte。
- host: compiler 455/0・20/0（spacing の試験は mov の filler で流す）、p009 502/0、fill-registers 32/32。VF2 の vmunix（`build/p2q1494/vf2/vmunix`）は rc 0、warning 0。板は未実施。
- 読み方:
  - unified 4 や Mesa の PDS で 4・128 wg が通る → instance の register の割当は unified store の宣言・local id の DOUTW に依る。executor（P1）を Mesa の形にする。
  - 16 temps だけで通る → 1 instance あたりの上限の問題。
  - どれも通らない → K0（shared の確保）か、firmware・core の設定の側。
- 片付け（Q1 に依頼）: `build/p2q1494/`、scratchpad の `ours*`。mesa26 は改造済み（dumper と表示）。次の照合にも使える。

## q1496-i01（P2、2026-10-21）: workgroup の PDS program を Mesa の bytes に

- 板 87: Mesa の PDS をそのまま使うと、sd NONE（`mesa pds 1`）でも sd USC（`mesa pds 2`）でも、1 wg 32/32、4 wg 128/128、128 wg 4096/4096 になった（16 temps、field 4）。
  - 補足: Q1 の要約では unified 0 だが、この変種は check の側で usc_inputs 4（unified 16 byte）を立てていた。
  - 我々の PDS（mesa pds 0）は、どの設定でも fault または一部だけ。
  - → **原因は PDS program の違い**。kernel 語の sd_type はどちらでも通る。
- 変更:
  1. **`powervr_compute_workgroup_program`（compute-program.c）が Mesa の bytes を出す**。
     - data は 8 語。0・1 が task word（我々の address・field）、2〜7 は Mesa の値（workgroup id の DOUTW の制御 2 つ、local id の unified store への DOUTW の制御 0x80000000、base workgroup の 0 が 3 つ）。
     - code は 10 語（Mesa の値。bra +7、base workgroup の add32 ×3、DOUTW ×2、halt、local id の DOUTW、DOUTU（cc 1）、halt）。
     - code は data の語の位置だけを名指すので、address・field に依らない。
     - 新しい定数（compute-program.h）: `POWERVR_COMPUTE_PDS_GROUP_DATA_WORDS` 8、`POWERVR_COMPUTE_PDS_GROUP_CODE_WORDS` 10、`POWERVR_COMPUTE_PDS_GROUP_CODE` 0x20（code は data の 32 byte 先）、`POWERVR_COMPUTE_GROUP_USC_INPUTS` 4（unified store 16 byte ＝ KERNEL0 の unified 1）。
     - `POWERVR_COMPUTE_PDS_CODE_MAX` は 10。
  2. KERNEL0 の data size（8 語 → 2）と unified size（4 register → 1）は、`powervr_compute_kernel_words` が `data_words`・`usc_inputs` から出す。helper の計算は変えず、呼ぶ側が新しい定数を渡す。
  3. compute check の `write_workgroup_kick` は、新しい program・code の位置・usc_inputs 4 を使う。check の既定の掃引は、**compiled fill の既定の設定で 1・4・128 wg（各々新しい session）だけ**にした。
     - field の probe、cf0 の probe は opt-in（`POWERVR_COMPUTE_LOCKUP_PROBES`）にした。
     - mesa-pds の比較の変種は消した（既定の program が Mesa の bytes になったため）。
  4. 差の切り分け（cc の bit だけ、local id の書きだけ）は今回は入れていない。
- **P1 への依頼（vk-queue.c の build_kernel、1 kernel の executor）**。今の vk-queue.c は data を 4 語だけ書き、code を slot＋0x10 に置き、slot は 0x40 なので、新しい program（data 8 語＋code 10 語）と合わない。p009 の `the kernel's workgroup program` が FAIL になる（501/1）。直す点:
  - `QUEUE_PDS_SLOT` を 0x80 にする（data 32 byte＋code 40 byte ＝ 0x48 が入るように）。`QUEUE_PDS_BYTES` が `POWERVR_EXECUTOR_PDS_BYTES`（0x4000）に収まるか確かめる: 0x80 ×（64＋1）＝ 0x2080 なので可。
  - data は `POWERVR_COMPUTE_PDS_GROUP_DATA_WORDS`（8）語を全部書く。code は slot＋`POWERVR_COMPUTE_PDS_GROUP_CODE`（0x20）から。
  - kernel の `code_offset` を slot＋`POWERVR_COMPUTE_PDS_GROUP_CODE` にし、`usc_inputs` を `POWERVR_COMPUTE_GROUP_USC_INPUTS` にする。
  - p009 の vk-host-test の期待（0x40＋0x10 の code の位置）も同じに直す。
- host:
  - p008 は 94/0・77/0。
  - p010-compiler は 455/0・20/0。
  - **p009 は 501/1**（上の vk-queue の依頼。P1 の範囲）。
  - p010 は全 14 本が 0 failed。
- VF2 の vmunix（`build/p2q1496/vf2/vmunix`）: rc 0、warning 0。板は未実施（vkcompute は vk-queue の修正の後）。

## q1499-i01（P2、2026-10-21）: 手書きの kernel の field を戻す

- 板 88: compiled fill は 1・4・128 wg で全部通った。一方、手書きの kernel が `6944/16384`（これまでは常に 16384/16384）。
- 原因の見立て: q1492 で `powervr_compute_task_field` を Mesa どおりの ceil(temps/4)（＋1 無し）にした。手書きの kernel は旧の kick program（`powervr_compute_kick_program`。local id の DOUTW が無い形）で issue される。その形では、temps が 4 の倍数ちょうどの時に instance の窓が重なる（2026-10-20 の 8 temps・2 block の観察と同じ）。
  - 例: store の program は 16 temps で field 4。＋1 なら 5。
  - q1496 の変更（workgroup の program）は、手書きの kick（slot 0〜7、data 2 語、code＋0x10、usc_inputs 0）を変えていない。
- 変更: **`powervr_compute_task_field` を「1 register 多く、4 register 単位」に戻した**（compute-program.c、注記に板 87・88 の経緯）。
  - workgroup の program（Mesa の形）は、どちらの field でも通る（1 block 多いだけ）。
  - p008 の host の期待値を 3・4・5 と、252 の拒否に戻した。
- host: p008 は 94/0・77/0、p009 は 502/0（P1 の vk-queue の追従を merge した後）。VF2 の vmunix（`build/p2q1499/vf2/vmunix`）は rc 0、warning 0。板は未実施。
