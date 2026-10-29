<!-- awesome-plan project=zedbsd record=ws313-p010 -->

# ws313-p010: Mali-G310（Valhall v10）の shader compiler

Status: in-progress（実装済み・実機未試験）
Queue: q1675-i02（P7 の配下 C）
WS: [WS313](../ws.md)、設計 [gpu.md](../gpu.md) §11・§12

## 目的

Keiland の desktop が描く shader の全部（compute・vertex・fragment）を Valhall の program にする。共通の front end
（`src/drivers/gpu/compiler/`）の IR から、backend `src/drivers/gpu/mali/compiler/` で encode・register の割当・wait
の scheduling・message 命令・IDVS・ATEST と BLEND まで。口と shader の ABI は `compiler/shader.h`（配下 D が使う）。

## ユーザーの指示（原文、2026-10-29、Q1 経由）

「Maliのドライバは、このGPUでとりあえずデスクトップを動かせるように、シェーダーコンパイラ、コマンド投入、シェーダ実行などを、一通り実装して、UATとデバッグに備えてください。デスクトップが動かせる範囲の実装でよいですが、余裕があればきちんと実装の対応範囲を他のGPUと同様に広げてください。」
「実機テストなしでコードだけ書いて揃えておいてください。今日の夜、まとめてテストします。」

## 実装（2026-10-29）

| file | 中身 |
| --- | --- |
| `compiler/shader.h` | 口（`mali_shader_compile`・`_compile_ir`・`_program_free`、`struct mali_shader_program`）と shader の ABI |
| `compiler/va-isa.h` | 命令の field の位置、使う命令の opcode、enum の値（ISA.xml から block ごとの出典） |
| `compiler/va-private.h`・`va-encode.c` | 命令の list、source・destination・staging の encode、定数の表 |
| `compiler/va-schedule.c` | slot の割当、wait（scoreboard）、ATEST の前の wait0126・BLEND の前の wait、branch の offset と reconverge、end、padding |
| `compiler/va-compile.c` | IR からの lowering と register の割当（下記） |

lowering が持つ物: 浮動小数の四則・FMA・min/max・比較、rcp・rsq・sqrt・exp2・log2・floor・trunc・round・fract・
sin・cos（範囲を 1/4 回転に畳んだ Taylor の多項式）、整数の四則（除算は float の逆数の推定と補正）・shift・論理・比較、
変換、half の pack・unpack、SELECT（MUX）、KILL（DISCARD）、loop（後ろへの BRANCHZ）、push constant（FAU）、
uniform buffer（FAU の address からの LOAD）、storage buffer と shared memory の load・store・atomic（SHADDX で address、
predicate は前への branch で迂回）、barrier、compute の local id・workgroup id、vertex attribute（LD_ATTR_IMM）、
IDVS の position program（viewport の変換、1/w、point size）と varying program（LEA_BUF_IMM と STORE）、
fragment の varying（LD_VAR_BUF_IMM、flat、NoPerspective）、FragCoord・FrontFacing・PointCoord・PrimitiveID、
texture（TEX_SINGLE・TEX_FETCH、2D・3D・array、bias・explicit lod、定数の texel offset、compute の sampled image）、
texture の大きさと level 数・storage buffer の長さ（driver が FAU に書く語から）、微分（CLPER、coarse・fine）、
ATEST と各 render target への BLEND。

## 判断（推しで進めた所、夜の試験で確かめる）

- **shader の ABI**（`shader.h` の先頭の comment が本体）: FAU は push constants → vertex は viewport の 6 float → buffer の
  64 bit の address。resource table は 0 = texture、1 = sampler、2 = vertex attribute、3 = vertex buffer（compiler が番号を
  決め、driver が descriptor を書く。amdrdna の形に倣った）。panvk は set ごとの table だが、compiler が pipeline layout を
  知らずに済むよう番号を compiler が決める形にした。
- **IDVS は 2 program**（position と varying）を 1 つの code に（Mesa の bi_compile_variant の 2 回 compile と同じ形）。
  v12 の shader_output の 1 program の形は使わない。
- **varying の配置は location L が byte 16L**（vertex と fragment が location だけで一致する。LD_VAR_BUF_IMM の index は
  v10 で 8 bit の byte offset なので L < 16）。
- **vertex の position は screen 空間**（Mesa の nir_lower_viewport_transform と同じ: x/w・y/w・z/w に scale と offset、
  w は 1/w を ±2^15 に clamp）。viewport は FAU。
- **NoPerspective** は vertex で w を掛け、fragment で LD_VAR_SPECIAL の w（1/w）を掛ける（Mesa の
  pan_nir_lower_noperspective と同じ）。
- **fixed function の blend だけ**（blend shader は無い）。BLEND は f32 の v4、最後の BLEND が end。color の無い fragment は
  ATEST と end だけ。
- **texture の大きさ・level 数、storage buffer の長さ**は descriptor を shader で読まず、driver が FAU に書く
  （`size_fau_word`、読む物だけに語を割り当てる）。Mesa は descriptor を読むが、口を簡単にするため。
- TEX の register type の field（bits 27:26）は float（1）にした。Mesa 26.2.6 の va_pack.c は書かず 0 のまま、
  assembler-cases（DDK の出力）は 1。DDK の形に合わせた（夜に TEX が壊れたら最初に疑う所）。
- **定数**は register を持たず、ISA の定数表にある値は表から、他は読む命令ごとに IADD_IMM で作る（register の圧迫を避ける）。
- register は stage の preload の下（compute r0〜r54、fragment r0〜r56、vertex r0〜r58）。r48（blend shader の link）は
  blend shader を使わないので使う。spill は無い（足りなければ ENOSPC）。
- wait は Mesa の va_insert_flow の規則（message の slot の結果を読む・書く前に wait、memory と varying の load は直列、
  ATEST の前 wait0126、BLEND の前 wait、barrier の後 wait）を、前の命令の flow に載せる（載らなければ NOP）。
- log2・exp2 は Mesa の bi_flog2_32・bi_fexp_32 の形（FREXPE・FADD_LSCALE・FLOGD・FEXP）。この 4 命令の意味は文書が無く、
  host の模型の意味は推定（下の「未確認」）。

## 確かめ（host だけ。GPU の証拠ではない）

- `sh plan/ws313/tests/mali-compiler-host-test.sh 200`（2026-10-29）: PASS。ISA.xml から生成した表が tree の物と一致、
  encoder の既知の語 24 個が Mesa の `valhall/test/assembler-cases.txt` と一致、assembler-cases の 246 語を自前の decoder が
  全部名前どおりに decode、16 module（`plan/ws313/tests/mali-compiler-shaders/` の 3、nvrtx の sm75 の 8、gpu-compute の
  3、compositor の quad の 2）が compile・decode・1 lane の模型と IR の参照で 200 試行ずつ一致（2026-10-29 の最後の回は
  `plan/ws313/tests/mali-compiler-shaders/` が 4 本で 17 module）。
- `sh plan/ws313/tests/mali-compiler-desktop-test.sh 200`（2026-10-29）: PASS、desktop の全 60 module（vert 19・frag 41）。
- 模型が誤りを捕まえる事の確かめ: FSUB の符号・SELECT の入れ替え・wait の削除・varying の offset・viewport の word の
  5 つの突然変異が全部 FAIL になった（2026-10-29、手で流した。script には入れていない）。
- kernel の object としての compile: odroid-c5-keiland の kernel の flag（`build/llvm/bin/clang --target=aarch64-unknown-zedbsd
  … -mgeneral-regs-only -ffreestanding -Os -Wall -Wextra -Werror -fno-builtin -flto`、`make -n` から取った）で
  `va-compile.c`・`va-encode.c`・`va-schedule.c` と共通の `spirv.c`・`optimize.c` が warning 0。link は未実施（mali.mk に未登録）。
- 実機: 未実施。QEMU: 未実施（Mali の machine が無い）。

## 未確認・やっていない事

- 実機で 1 語も走らせていない。encode は ISA.xml と assembler-cases に合うが、v10 の G310 の実際の挙動は未確認。
- 模型の意味の推定: FREXPE（sqrt の modifier）・FADD_LSCALE・FLOGD・FEXP、FRCP・FRSQ の精度（模型は正確な値）、
  TEX の結果（模型は決まった関数）、CLPER（模型は 1 lane なので微分は 0 でしか試していない）、DISCARD が lane を終える事。
- preload の register 番号（r55〜r61）は Mesa の bi_preload_reg（v9 以降）の値で、v10 の CSF の driver 側の設定（shader
  program descriptor の preload の mask）と合わせる必要がある（D）。
- 無い物: cube・buffer・multisample の texture、gather・compare、storage image・texel buffer・input attachment、
  geometry shader、dual source の第 2 color、FragDepth・
  SampleMask、spill、UBO の vector の load（今は 1 語ずつ）、helper の lane の discard の flow（最適化）、register の
  last-use の bit（hint）。

## 他への案

- `platform/arm64/mali.mk` の `ARM64_MALI_SOURCES` に足す file（D か P7）: `src/drivers/gpu/mali/compiler/va-compile.c`・
  `va-encode.c`・`va-schedule.c`。共通の `src/drivers/gpu/compiler/spirv.c`・`optimize.c` も要る（odroid-c5 の build に
  既に入っていなければ）。
- 共通の front end（`src/drivers/gpu/compiler/`）への差分は要らなかった。

## 再開点

- 夜の実機の結果で直す（GPU の fault・CS の fault の値と、どの program か）。疑わしい順: wait の flow（message の結果の
  待ち）、preload の register、varying の offset、viewport の FAU の位置、TEX の handle の table と index。
- 余裕があれば: cube texture（CUBEFACE1/2）、gather・compare、UBO の vector load。
- 規約: 明らかな違反（条件の中の関数呼び出し、まとめた確保の検査、三項演算子）は直した。全部の if の前の comment など
  規約の全文の見直しは WS の最後の Phase に残す（va-compile.c が大きい）。
