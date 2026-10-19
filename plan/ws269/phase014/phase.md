<!-- awesome-plan project=zedbsd record=ws269-p014 -->

# ws269-p014 SPIR-V から USC の compiler（出力 O-SHADER-ABI・O-VSFS）

- 親: [ws269](../ws.md)、設計: [design.md](../design.md) §5、[design-p010.md](../design-p010.md) 版 2 §5・§5.1・§5.2、レビュー: [design-p010-review-2.md](../design-p010-review-2.md)（Minor 8）
- 担当: P2
- Queue: 最初の試行は q1363 / q1363-i01（P2、2026-10-20、[p010-compiler](../phase010-compiler/phase.md) として記録。この Phase の範囲を含む）。q1369 / q1369-i01 でこの phase.md を作った
- Status: in-progress（2026-10-20 P2: q1363 で compute・vertex・fragment の compile、key（format・pack・blend）、compute の executor への差し替え、host 試験 PASS）
- 依存: p009。**O-SHADER-ABI は O-ENC に依らない**（review 2 の Minor 8）。O-VSFS は O-ENC（[p013](../phase013/phase.md)）に依る
- 見積もり: 残り 4〜8 LW（gpu-selftest の shader と `shader.h` の合意による。推測）

## 出力

| 出力 | 中身 | 依存 | 状態 |
| --- | --- | --- | --- |
| **O-SHADER-ABI**（最初の出力） | `src/drivers/gpu/powervr/shader.h`: §5.1 の compile の入力（`struct powervr_shader_key`）と出力（stage、USC の code、temps、shared register の要求の表、vertex の入力・varying・fragment の出力の表、性質）、§5.2 の要求の種類。P1 と合意して main に merge | p009 | q1371 で `git mv` により `shader.h` に改名（中身は不変）、include と試験を直した。P1 の合意が残り |
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

## 残り

1. O-SHADER-ABI: P1 との合意（Q1 経由、型の一覧は p010-compiler の「境（合意の案）」）。合意で merge。
2. gpu-selftest の shader ができたら host 試験に足す（O-VSFS）。
3. p010-compiler の「残り」の 2〜4（未実装の機能、長い file の分け方）。
