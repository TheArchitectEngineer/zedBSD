<!-- awesome-plan project=zedbsd record=ws279-p004 -->

# ws279-p004 QPU 7.1 の encoder・scheduler・register の割り当て・固定の shader

- 親: [ws279](../ws.md)、Queue: q1545 / q1545-i01（P2、2026-10-21）
- Status: in-progress（host の受け入れの条件 1〜4 を満たした、2026-10-21 P2。Q1 の判定待ち。実機の確認は p005 の最初の三角形で）
- 依頼の元: 2026-10-21 ユーザー「Pi5のdecisionは推しでOKです」（design §9 の D1 = 7.1 の file を `src/drivers/gpu/bcm2711/` に足し rpi5 の build だけで選ぶ、Pi 4 の kernel は不変）、「Pi5の作業は続けていいです」。Fable の review は使わない（「Pi5の対応は、Fableによるレビューは不要です。Opus 5.5で対応しましょう。」）。board の違いは `#if` で分けず写して書き直し、本当に同じ物だけ共通にする（「無理に共通化しないで rpi4のコピーでrpi5を作って、共通部分をまとめるのが私の好みです。」）。

## 目的と受け入れの条件

[design.md](../design.md) §3 の QPU 7.1 の差から、Pi 5 の shader compiler の版の部分を作る。host だけで確かめる（実機・QEMU は無い）。

1. 7.1 の命令の encoder（64 bit の形、signal・op・small immediate・条件の field）。Mesa の 7.1 の packer・decoder を oracle にして、全部の semantic な op と signal・small immediate の組を照合する。
2. scheduler と register の割り当て（accumulator 無し、4 つの読み口、small immediate は 1 語に 1 つで signal と組めない、ldvary の rf0 の 1 語遅れの書き込み、SFU は add の op、thrsw・thrend の 7.1 の決まり）。
3. 固定の shader（Vulkan の実行器の texture の copy の vert・frag、Keiland の shader 一式）が 7.1 で compile でき、7.1 の lane の model の上で source の program と bit 単位で一致する。
4. build の warning 0（kernel の flag の clang で 7.1 の file を compile）。Pi 4 の file は変えない（振る舞いと object）。

## 作る物（設計）

### 何を共有し何を写すか

zedBSD の Pi 4 の compiler は、共有の SPIR-V の前段（`src/drivers/gpu/compiler/`）→ 検査と生存区間（`shader-analyze.c`）→ lowering（`shader-lower.c`・`shader-output.c`、register はここで割り当てる）→ 記録（`shader.c`、意味の命令 `struct bcm2711_qpu_instruction` を 1 つずつ記録）→ scheduler（`shader-schedule.c`、fragment だけ）→ encoder（`qpu.c`）。

- **共有（そのまま link する）**: `qpu.h`（意味の命令の語彙、版に依らない）、`shader.h`（binary・key・uniform の形、Vulkan の実行器が使う）、`shader-private.h`、`shader.c`（記録・register の割り当て・constant の cache・在順の出力・preflight）、`shader-analyze.c`。どれも版の field を持たない。`shader.c` の在順の出力の間隔（SFU・VPM の後 2 語）は 7.1 でも安全な値。
- **7.1 用に写して書き直す（同じ関数名、board の source の一覧で選ぶ。design §5 の方針）**:
  - `qpu71.c`（← `qpu.c`）: 7.1 の encoder。`bcm2711_qpu_encode`・`_encode_bundle`・`_small_constant`・`_operation_unit`・`_operation_sources` を 7.1 の形で定める。
  - `shader71-lower.c`（← `shader-lower.c`）: varying の C 係数は rf0、W は rf3。同じ small constant を 2 つ読む op は 1 つを register に移す。選択の 0 の判定は MOV の flag の push（定数の条件でも 1 つの読み口）。
  - `shader71-output.c`（← `shader-output.c`）: vertex の最後の VPMWT は 4.2 の GFXH-1684 だけ（M `nir_to_vir.c` 2075〜2084 行）なので 7.1 では出さない。
  - `shader71-schedule.c`（← `shader-schedule.c`）: accumulator への昇格を外し（7.1 に無い）、7.1 の決まりで word を詰める。
- **固定の shader**: `meta/texture-words.inc` は SPIR-V の word（機械語ではない）なので版に依らず共有。design §3.7 の「機械語は版で違う」は誤りだった（実行時に compile する）。7.1 の compiler で compile できる事を試験で確かめる。

### 7.1 の encoder（`qpu71.c`）

出典（値だけ）: Mesa 25.3.6 `src/broadcom/qpu/qpu_pack.c`（field の位置 46〜110 行、`v3d71_sig_map` 164〜197 行、`small_immediates` 239〜264 行、`v3d71_add_ops` 585〜745 行、`v3d71_mul_ops` 747〜790 行、`v3d71_qpu_add_pack` 1772〜2041 行・`v3d71_qpu_mul_pack` 2160〜2285 行の unpack の field の置き方）、`qpu_instr.h`（magic の waddr の番号）。

- 入力は全て register（rf0〜rf63）か small immediate。add の a・b は raddr_a・raddr_b、mul の a・b は raddr_c・raddr_d。accumulator の source は拒む。
- 0・1 入力の op は raddr_b（add）・raddr_d（mul）の値で種類を選ぶ（MOV 249/3、FMOV の L・H の unpack 249/8・12、SFU 188/32〜36、FROUND 245/4・FTRUNC 245/20・FFLOOR 245/36・FCEIL 245/52・FTOIZ 245/23・FTOUZ 245/39、ITOF 246/32・UTOF 246/36、FDX 246/4・FDY 246/20、NOT 186/0・NEG 186/1、TIDX 等 187/n、FXCD 187/32・FYCD 187/36、LDVPMV_IN 188/0、STVPMV 190、mul の MOV 14/3・FMOV 14/8・12、mul の NOP 14/63）。
- FADD・FMIN・FMAX の区別は 2 つの入力の順（small immediate の印 × 256 + unpack × 64 + raddr の大小）で、4.2 と同じ考え。
- small immediate は 1 語に 1 つ（signal の SMIMM_A/B/C/D、14・15・30・31）で、他の signal・thrsw と組めない（7.1 の表に組が無い）。
- MOV・FMOV（HALF_LOW/HIGH）と整数の ADD・SUB は add と mul のどちらでも動く（mul の ADD 1・SUB 2）。
- 条件の field と addressed な signal の field は 4.2 と同じ。

### 7.1 の scheduler（`shader71-schedule.c`）

出典（決まりの事実）: Mesa `compiler/qpu_schedule.c`・`qpu_validate.c`（design §3.5 の行）。

- 資源: rf0〜rf63 と lane の flag。ldvary は signal の書き先に 0 語後、rf0 に 1 語後に書く。SFU の結果は 1 語後に着く（次の語の読みは hardware が待たせるが、確かな側に倒して 2 語空ける）。
- 1 語の peripheral: TMU の書き込み・wrtmuc・TLB の読み・SFU・VPM の読み書きのうち 1 つ（wrtmuc と TMUC 以外の TMU の書き込みの組だけ可）、ldtmu は 1 つ、TLB は 1 つ（M `qpu_compatible_peripheral_access` の 7.1 の枝）。
- thread switch の窓: flag・rf0・SFU・ldvary は switch を越えない（4.2 の accumulator の扱いの代わり）。
- thread end の窓: end の語は ALU で register に書かない、end と 2 つの遅延の語は rf0〜rf3 を読まず register に書かない（7.1 の決まりは rf2・rf3 だが、確かな側）、uniform は end の語だけ。
- 完成した schedule は、4.2 と同じく値の版を語ごとに追う独立の検査を通してから出す。通らなければ在順の出力。

### 試験（`plan/ws279/tests/`）

- `qpu71-format-host-test.sh`・`.c`: WS141 の oracle の作り（`plan/ws141/temp/mesa` の Mesa の `qpu_pack.c`・`qpu_instr.c` を変えずに host で compile）を使い、ver 71 で、全部の op を decode して op・raddr・waddr・flag・signal を確かめ、Mesa の packer で作り直した語と一致する事を確かめる。拒む組（small immediate 2 つ、small immediate と signal、accumulator、addressed な signal と条件）も確かめる。
- `qpu71-machine.c`・`.h`: WS141 の 4.2 の lane の model（`plan/ws141/tests/qpu-machine.c`）を写して 7.1 の決まりにした物。
- `shader71-host-test.sh`・`.c`: WS141 の `shader-host-test.c` を写し、7.1 の compiler と 7.1 の lane の model で、木の全部の shader（Keiland・app・texture の copy）を source の program と bit 単位で比べる。

## 記録

- 2026-10-21 P2: 設計（上）。design §3.5 の「4 thread なら 16」は誤り（M `vir_register_allocate.c` は thread の index で 64 を半分にするだけで、4 thread は rf0〜rf31。zedBSD の 4.2 と同じ）。design §3.5・§3.7 に訂正を書いた。
- 2026-10-21 P2 実装（commit b54b15cc4 = encoder と試験、Q1 が main 1fac4c55e に merge 済み。lowering・出力・scheduler・lane の model・shader の試験・mk は 562e610bb）:
  - `src/drivers/gpu/bcm2711/qpu71.c`（新規）: 7.1 の encoder。
  - `shader71-lower.c`・`shader71-output.c`・`shader71-schedule.c`（4.2 の file の写しを 7.1 に書き直した物）。
    - lowering: C 係数は rf0、W は rf3、2 つの small constant は等しくても 1 つを register に、選択の 0 の判定は MOV の pushz（M `nir_to_vir.c` 458・1271 行と同じ形）。
    - 出力: vertex の最後の VPMWT を出さない。XY の .8 固定小数は floor でなく最近接偶数への丸め（M `v3d_nir_lower_io.c` 528〜541 行: floor は 4.2 だけの二重丸めの対策、7.x は fround_even）。fragment の色の 2 つの定数は等しくても 1 つを register に。
    - scheduler: accumulator への昇格を外し 1 回の試み。資源は rf0〜rf63 と flag。ldvary は rf0 に 1 語後、SFU は 1 語後に着き、SFU の開始は 2 語空ける。rf0 の読みは switch を越えない。peripheral は 7.1 の restricted の規則（TLB の書き込み・ldtmu は SFU・TMU と同じ語に置ける）。thread end の窓は 4.2 の厳しい規則のまま（register に書かない、rf0〜rf3 を読まない）。
  - 共有して変えない file: `qpu.h`・`shader.h`・`shader-private.h`・`shader.c`・`shader-analyze.c`（Pi 4 の file は 1 行も変えていない）。
  - `platform/arm64/vmunix.mk`: `CONFIG_DRIVER_BCM2712_GPU` が y の時に 7.1 の compiler の source を足す block。switch の定義（Makefile・menuconfig）は p002 で他の driver の部分と一緒に足す。今は何も選ばない。
- 試験（host、2026-10-21 19:50 までに流した）:
  - `sh plan/ws279/tests/qpu71-format-host-test.sh`: PASS。全部の semantic な op を add・mul の各 ALU で 64 通りの register の組で encode し、Mesa 25.3.6 の 7.1 decoder で op・unpack・raddr・waddr を確かめ、Mesa の packer が decode した命令からも semantic な記述だけからも同じ語を作る事を確かめた。idle の語 0x38003186bb03f000、全 signal と addressed な書き先、thrsw との組、48 個の small immediate を 4 つの読み口で、条件の全部の組、拒む組（small immediate 2 つ・signal や thrsw との組・accumulator・7.1 で予約の magic・addressed な signal と条件・両 ALU の push・FMAX の同じ入力）。
  - `sh plan/ws279/tests/shader71-host-test.sh`: PASS。WS141 の試験の写しで、木の全部の shader（Keiland・app・driver の texture の copy の meta の vert/frag）を 7.1 の compiler で compile し、7.1 の lane の model（`qpu71-machine.c`、WS141 の 4.2 の model を 7.1 の決まりに書き直した物）で走らせ、source の program の解釈と bit 単位で比べた。fragment は全部 schedule され（4 thread が多い）、program の長さは 4.2 と同じか短い（例 kwl panel 254→229 語、monitor shape 175→147 語、kwl panel 7 98→95 語）。拒む shader は 4.2 と同じ（mview cutout・pixel、libGL の frag、vkdemo の vert）。
  - 試験の試験（変異）: W を rf0 に戻す、SFU の間を 1 にする、rf0 の着きを 0 語にする、rf0 の読みを switch で守らない、の 4 つの変更はどれも試験が落とした。
  - Pi 4 の `sh plan/ws141/tests/shader-host-test.sh`: PASS（共有の file を変えていない事の確かめ）。
  - kernel の flag での compile: `build/llvm/bin/clang --target=aarch64-unknown-zedbsd` に `platform/arm64/vmunix.mk` の ARM64_CPPFLAGS・ARM64_CFLAGS（`-Os -Wall -Wextra -Werror`、rpi5 の define）と kernel の object の `-fno-builtin -fno-strict-aliasing` で qpu71・shader71-lower・shader71-output・shader71-schedule・shader・shader-analyze を compile し、warning 0。rpi5 の kernel の全体の build と link はしていない（switch が無いので）。
- 規約: 新しく書いた所（qpu71.c、試験、各 file の 7.1 の差の部分）は coding-style の全文で書いた。4.2 の file から写した本体は元の file の書き方のまま。全文の見直しは p009。
- 未実施: 実機・QEMU（Pi 5 の V3D の emulation は無い）。lane の model は test の model で、cycle の model ではない。7.1 で緩められる決まり（thrsw の遅延の語の ldvary、thread end の遅延の語の register の書き込み、SFU の次の語の読み）は使っていない（確かな側）。

## 再開の条件

- Q1 の判定。p005（描画の経路）で 7.1 の shader state record から呼ぶ。最初の三角形（実機）で TLB の書き込みの形（design §8 U5）と payload の rf3 を確かめる。
- p002 で `CONFIG_DRIVER_BCM2712_GPU` を定義した時、rpi5 の kernel の build と link で warning 0 を確かめる。
