<!-- awesome-plan project=zedbsd record=ws269-p008 -->

# ws269-p008 Rogue の USC・PDS の encoder、kernel の中の試験の道、data master の道の coherency

- 親: [ws269](../ws.md)、設計: [design.md](../design.md) 改訂 3.1 の §5（D6 A: 自前の USC の backend）・§6 の p008・§3.2 の 4（data master の道）
- Queue: q1345 / q1345-i01（P2、2026-10-20、前半: encoder と host 試験）
- Status: in-progress（2026-10-20 P2: 前半（USC・PDS の encoder、host 試験、VF2 の build）まで。後半（kernel の中の試験の道、data master の道の coherency）は実機が要るので未着手。clearance は Q1 の判定）
- 前提: p006（実装済み・実機待ち）。HAL の API は変えない。toolchain は変えない。

## 目的と受け入れの条件

前半（q1345、この記録の範囲）:

1. Rogue の USC（unified shading cluster）の命令 group と各 phase の operation の encoder。BXE-4-32 の feature に合わせる。
2. PDS（programmable data sequencer）の命令と、その出力命令が取る data の word の encoder。BXE-4-32 の feature（PDS_DDMADT あり、SLC_MCU_CACHE_CONTROLS なし）に合わせる。
3. host 試験: Mesa の disassembler などに頼らず、Mesa の field の表から手で組んだ期待の bit 列と比べる。
4. 関係する platform（VF2）の build が warning 0。QEMU は起動しない。

後半（未着手、実機が要る）: kernel の中の試験の道（compute の kernel を直接 kick して buffer を読み返す、Venus を通さない）、GPU の data master の道の coherency の確かめ（§3.2 の 4 と同じ手順、p006 の MIPS の道の試験の data master 版）。

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

- riscv64-virt・amd64 の build は流していない（変更は `CONFIG_DRIVER_POWERVR` の VF2 の source の list だけで、他の platform は build しない file）。
- QEMU・実機の試験は無い（encoder は hardware に触れない。実機の確かめは後半の kernel の中の試験の道で行う）。

## 残り（後半、実機が要る）

1. kernel の中の試験の道: この encoder で「buffer の address を受け、64 KiB を書く」compute の USC の program と、それを issue する PDS の program（DOUTD で定数を common store へ、DOUTU で task）を組み、p006 の compute の FW の context に CDM の kick を出し、buffer を読み返す（Venus を通さない）。CDM の control stream の形は Mesa の `cdm.xml`（p002 の blob の記録あり）から取る。
2. data master の道の coherency: §3.2 の 4 と同じ手順（64 KiB × 8 回、2 MiB の中で 128 KiB ずつずらす、SLC の flush なし、0b の対照、INCONCLUSIVE なら切り替えない）を compute の書き込みで流し、1 行を出す。
3. 実機の試験の文面（T1 向け）は後半で書く。
4. 2 byte の main header・DOUTU の temps の単位は、後半の実機の compute の kick で確かめられる範囲で確かめる（前者は使わないので必須ではない）。
5. Q1 への片付けの依頼: `build/ws269-p008/`（`mesa/` は Mesa の raw の file、`gen/`・`dev/` は開発中の照合の道具、`host/` は試験の出力）と `build/ws269-p006-vf2/`（この worktree の build の出力）。
