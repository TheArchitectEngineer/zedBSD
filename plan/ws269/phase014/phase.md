<!-- awesome-plan project=zedbsd record=ws269-p014 -->

# ws269-p014 SPIR-V から USC の compiler（出力 O-SHADER-ABI・O-VSFS）

- 親: [ws269](../ws.md)、設計: [design.md](../design.md) §5、[design-p010.md](../design-p010.md) 版 2 §5・§5.1・§5.2、レビュー: [design-p010-review-2.md](../design-p010-review-2.md)（Minor 8）
- 担当: P2
- Queue: 最初の試行は q1363 / q1363-i01（P2、2026-10-20、[p010-compiler](../phase010-compiler/phase.md) として記録。この Phase の範囲を含む）。q1369 / q1369-i01 でこの phase.md を作った
- Status: in-progress（2026-10-20 P2: q1363 で compute・vertex・fragment の compile、key（format・pack・blend）、compute の executor への差し替え、host 試験 PASS。q1372 で VertexIndex・InstanceIndex、shader-spirv.c の分割、要否の表）
- 依存: p009。**O-SHADER-ABI は O-ENC に依らない**（review 2 の Minor 8）。O-VSFS は O-ENC（[p013](../phase013/phase.md)）に依る
- 見積もり: 残り 4〜8 LW（gpu-selftest の shader と `shader.h` の合意による。推測）

## 出力

| 出力 | 中身 | 依存 | 状態 |
| --- | --- | --- | --- |
| **O-SHADER-ABI**（最初の出力） | `src/drivers/gpu/powervr/shader.h`: §5.1 の compile の入力（`struct powervr_shader_key`）と出力（stage、USC の code、temps、shared register の要求の表、vertex の入力・varying・fragment の出力の表、性質）、§5.2 の要求の種類。P1 と合意して main に merge | p009 | q1371 で `shader.h` に改名。q1372: Q1 の指示で P1 と合意済みとして扱う。vertex・instance index の欄を足した。§5.1 との食い違い 4 点を下に |
| **O-VSFS** | vkdemo・gpu-selftest の vertex・fragment が compile でき、host 試験 PASS で main に merge | O-SHADER-ABI、O-ENC | vkdemo・compositor の shader は host 試験 PASS（q1363）。gpu-selftest の shader は未作成 |

### O-SHADER-ABI の file の名前（q1369 で決めた事）

- 名前は design-p010.md §5.1・§5.4 の文面に合わせて **`shader.h`** にする（p010h の依存の書き方・P1 の文書と揃う）。
- 今の `shader-compile.h` の型と宣言をそのまま `shader.h` に移す（中身を変えない）。旧い file を消す改名は削除を伴うので、`git mv` を Q1 に頼む（subagent は rm をしない）。改名の後、`shader-spirv.c`・`shader-backend.c`・`vk-*.c`・試験の `#include` を直す。
- 合意の手順: 型の一覧（p010-compiler の「境（合意の案）」の表）を Q1 経由で P1 に送り、design §5.1 と違う所（vertex の入力の表に format の欄を置かない事、`LITERAL` を今は出さない事、discard・depth・shared memory が未実装）を決めてもらう。合意したら O-SHADER-ABI として merge を依頼し、p010h が始められる。

## 完了の条件

1. O-SHADER-ABI: `shader.h` が P1 と合意され main に merge 済み。
2. O-VSFS: vkdemo・gpu-selftest の vertex・fragment が compile でき、host 試験（interpreter・simulator・group）PASS で main に merge 済み。
3. build（VF2・riscv64-virt・amd64）warning 0。QEMU は起動しない（VF2 の GPU は QEMU に無い）。

## 最初の試行（q1363 / q1363-i01、2026-10-20 P2）

詳細の記録は [p010-compiler](../phase010-compiler/phase.md)（SPIR-V の op の一覧、設計、境、確かめ、仮定と危険、残り）。要約: 読み（shader-spirv.c）・中間形・back end（shader-backend.c）・key・execution mask counter の制御。host 試験 compiler 298/0・group 20/0、p008 encode 91/0・compute 70/0、p009 executor 112/0。VF2・riscv64-virt・amd64 の build warning 0。結果: 未判定。

q1369（2026-10-20 P2）: p008 の `compute-program.c` の instance number の読みを compiler と同じ movs1 の形に直した（[p008](../phase008/phase.md)）。compiler の host 試験は変わらず 298/0・20/0。

## q1371 / q1371-i01（2026-10-20 P2）

- 改名: Q1 の許可で `git mv src/drivers/gpu/powervr/shader-compile.h src/drivers/gpu/powervr/shader.h`（include guard を `KERN_DRIVERS_GPU_POWERVR_SHADER_H` に）。`shader-spirv.c`・`shader-backend.c`・`shader-private.h`・`vk.h`・試験（p009 `vk-host-test.c`、p010-compiler `compile-host-test.c`・`machine-run.h`）の include を直した。`plan/ws236/tests/run-shader-compile-test.sh` は別の driver の物で無関係。
- O-VSFS: vertex の出力・varying の表、fragment の補間、SMP、EMITPIX と pack・blend、§5.1 の表は q1363 で実装済み（host 試験 PASS）。補間は BXE の形の `ditr` を使い、backend の形の FITR/FITRP は使わない（Mesa の pco も BXE では ditr）。design-p010 版 2.1（O-ENC の一覧）はまだ main に無いので照合は次の merge の時に。gpu-selftest（`userland/tests/gpu-selftest`）には shader がまだ無い。
- host 試験: p008 encode 91/0・compute 70/0、p009 executor 160/0、p010-compiler 298/0・group 20/0、p010 selftest-host 0 failed（`sh plan/ws269/tests/<t>/run.sh build/ws269-p014/<t>`）。
- build: `make ZEDBSD_CONFIG=config/ci/config-{visionfive2,riscv64-virt,amd64}.mk BUILD=build/ws269-p014-{vf2,virt,amd64} -j16 vmunix`、3 つとも rc=0・`warning:` 0（log `build/ws269-p014/*.log`）。

## q1372 / q1372-i01（2026-10-20 P2）

### 未実装の機能の要否（数えた結果）

数えた shader: vkdemo（`userland/tests/vkdemo/shaders/cuboid.{vert,frag}`、属性 R32G32B32・R32G32 SFLOAT、depth は D32 の attachment で固定の機能の test、shader は depth を書かない）、Keiland の compositor（`userland/desktop/wayland/shaders.h` の kwl の 4 組と panel の 8 つの variant、`userland/desktop/libkeiland/ui/shaders/canvas.{vert,frag}`、属性は `present.c` の R32G32B32A32_SFLOAT 1 つ）、vkcompute（`fill.comp`）。gpu-selftest（`userland/tests/gpu-selftest`）には shader がまだ無い（design-p010 §8.2 の draw・sample・depth は glslc で作る予定）。

| 機能 | vkdemo | Keiland | vkcompute・gpu-selftest の compute | gpu-selftest の draw・sample・depth（予想） | 扱い |
| --- | --- | --- | --- | --- | --- |
| VertexIndex・InstanceIndex | 使わない | 使わない | — | vertex buffer なしの三角形・2 つの三角形で使う見込みが高い | **実装**（q1372） |
| FragCoord | 使わない | 使わない | — | 中心・角の pixel は readback で比べるので要らない見込み | p011 へ（x・y は special register、z・w は TA の coefficient を要し driver と組む） |
| discard | 使わない | 使わない | — | 使わない見込み | p011 へ |
| depth の書き（FragDepth） | 使わない | 使わない | — | depth の段は vertex の z で足りる | p011 へ |
| 8・16 bit の vertex の属性（UNORM・SNORM 等） | 使わない（32 bit float） | 使わない（R32G32B32A32_SFLOAT） | — | 不明（P1 の shader 次第） | p011 へ（P1 が使うなら戻す） |
| compute の shared memory・barrier・atomic | — | — | 使わない（fill） | — | p011 へ |
| compute の executor の push constant | — | — | 使わない（fill に push constant は無い） | — | p011 へ（command buffer の push の記録から要る） |
| 整数の除算・剰余（OpSDiv・OpSMod 等） | 使わない | 使わない | 使わない | `% 3` で使う形もあり得る | p011 へ（試験の shader は & と >> で書いた） |

### 実装

- VertexIndex・InstanceIndex（`shader-spirv-interface.c`）: 属性の register の後に 1 つずつ vertex input register を置き、`powervr_shader_program` の `vertex_index_reg`・`instance_index_reg` で driver に知らせる（読まない時は `POWERVR_SHADER_NO_REG`）。driver の PDS の vertex の program がそこへ書く（Mesa 26.2.5 `pvr_arch_pipeline.c` の `pvr_alloc_vs_sysvals`・PDS の `VERTEX_ID_REQUIRED`・`INSTANCE_ID_REQUIRED` の手順。vertex index は base を含み、instance index は first instance を含む、`pco_nir.c` の option）。Mesa は sysval を属性の**前**に置くが、ここは後に置いて既存の属性の番号を変えない（番号は表で渡すので driver はどちらでも同じ）。
- `shader.h` に `vertex_index_reg`・`instance_index_reg`・`POWERVR_SHADER_NO_REG` を足した（O-SHADER-ABI の追加。p010h の PDS が使う）。
- 分割: `shader-spirv.c`（約 3,630 行）を意味で 4 つに分けた: `shader-spirv.c`（入口・宣言の pass・共有の小さな手順、約 1,000 行）、`shader-spirv-interface.c`（interface の配置・入口の読み・出口の受け渡し・pixel の pack と blend）、`shader-spirv-control.c`（関数の inline・構造化の制御・phi）、`shader-spirv-instruction.c`（memory・composite・算術・GLSL.std.450・sample）。共有の定数と関数は `shader-spirv.h`（private）に、file を越える関数は `shader_spirv_` を前に付けた。動作は変えていない（host 試験の数は同じ）。`platform/riscv64/vmunix.mk` と p009・p010-compiler の `run.sh` の source の一覧を直した。p012 で: 非 static の共有の関数が static の後に来る file がある（並びは規約の全文の見直しで）。

### O-SHADER-ABI の照合（design-p010 版 2.1 §5.1 と shader.h、Q1 の指示で合意済みとして扱う）

食い違い（P1 への写し、決めるのは P1・Q1）:

1. vertex の入力の表に format の欄が無い（format は key の `attribute_formats` で compiler に渡り、変換は shader の中）。
2. 「性質」の discard の有無・depth の書き・shared memory の大きさの欄が無い（どれも ENOTSUP で拒むので常に「無し」。実装する時に欄を足す）。
3. §5.1 に無い `vertex_index_reg`・`instance_index_reg` を足した（q1372）。
4. `REQUEST_LITERAL` は今は出さない。

### 確かめ

- host 試験: p010-compiler 318/0（index.vert の -O・-O0 で register の置き場と 32 lane の position、interpreter と simulator の一致を足した、+20）・group 20/0、p008 encode 94/0（SR の 3 つの group を足した、[p013](../phase013/phase.md)）・compute 70/0、p009 executor 160/0、p010 stage 24/0・image 55/0・selftest-host 0 failed。
- build: `make ZEDBSD_CONFIG=config/ci/config-{visionfive2,riscv64-virt,amd64}.mk BUILD=build/ws269-p014-{vf2,virt,amd64} -j16 vmunix`、3 つとも rc=0・`warning:` 0（log `build/ws269-p014/{vf2,virt,amd64}.log`、分割後の 4 つの object を VF2 で確認）。QEMU・実機は未実施（範囲外）。
- Mesa 26.2.5 の source: `build/ws269-p014/mesa/mesa-26.2.5.tar.xz`（archive.mesa3d.org から取得、`src/imagination` だけ展開）。

## 残り

1. gpu-selftest の shader ができたら host 試験に足す（O-VSFS）。8・16 bit の属性・整数の除算などを使うなら、この Phase に戻す。
2. 上の表の p011 へ回した物（記録だけ）。
3. 片付け（Q1 へ、rm は Q1）: `build/ws269-p014/sr/`（SR の確かめの probe と分割の script・元の file の写し）。
