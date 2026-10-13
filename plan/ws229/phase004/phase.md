<!-- awesome-plan project=zedbsd record=ws229-p004 -->
# ws229-p004: 3D と compute の state を Gen9 に

Status: in-progress（2026-10-14 P3: host の受け入れを満たした。clearance は Q1 の判定と、実機の確かめの p005 を待つ）
Disposition: normal
Parent: [WS229](../ws.md)
Queue: q979
依存: p001（設計 §2.1・§5）、p002 の契約（`gt->graphics_ver`、4bc6381c0）、p003 の `drv_i915_shader_compile_gen()`（13a8fd635）

## 範囲

[p001 の設計](../phase001/phase.md) §2.1 と §5 の p004 の行、[review-1](../phase001/review-1.md) の M9・中 8・video の gate。
Gen12 の state の byte を変えないことが最優先（2026-10-14 Q1）。

## 実装（2026-10-14 P3）

版の選び方: `struct i915_gfx_batch` に `graphics_ver` を足した（9 なら Gen9 の形、他の値（12、test が手で作る batch の 0）は Gen12 の形）。
draw の path が session の batch を作る時に `gt.graphics_ver` を入れ、draw・rectangle・dispatch は同じ batch を使う。
batch を取らない state の書き手は `_gen` の版を足し、古い名前は Gen12 の wrapper のまま残した（呼び手の test を変えない）:
`drv_i915_gfx_write_state_gen()`・`drv_i915_gfx_surface_write_gen()`・`drv_i915_gfx_instruction_heap_clear_gen()`・
`drv_i915_gfx_dispatch_write_gen()`・`drv_i915_gfx_mocs_uncached()`。

| 何 | Gen9 で変えた所 | file |
| --- | --- | --- |
| Gen9 の値 | 新しい `intel/genxml-gen9.h`（gen90.xml・gen80.xml・intel_device_info.c・intel_l3_config.c・anv・isl・Linux intel_mocs.c の sha256 付き） | intel/genxml-gen9.h |
| PIPE_CONTROL | HDC pipeline flush（Gen12 だけ）の代わりに DC flush（dw1 bit 5、anv の GFX_VER < 12） | render/batch.c |
| PIPELINE_SELECT | mask 0x3、DOP clock gate 無し | state.c・compute.c |
| STATE_BASE_ADDRESS | 19 dword（bindless sampler 無し） | state.c |
| context setup | CACHE_MODE_1 の LRI（anv の Gen9 の初期化）、VF invalidate の前の null PIPE_CONTROL と post-sync の write（slot の scratch へ、SKL PRM）、DEPTH_BOUNDS を出さない | state.c |
| vertex・index buffer | L3 bypass disable を立てない、VF_SGVS_2 を出さない | state.c・blit.c |
| URB | 3DSTATE_URB_VS/HS/DS/GS（0x7830〜）。push constant の後の 16 chunk（GS 有りは 10+6）、GT1 の上限（VS 928、GS 256）。push constant の配分は Gen12 と同じ（helper に移した、byte 不変） | state.c |
| 3DSTATE_VS | max threads 336、bit 23 から | state.c |
| 3DSTATE_SF | deref block size（Gen12 だけ）を 0 | state.c |
| depth・stencil | Gen9 の DEPTH_BUFFER の field 位置、stencil write enable を DEPTH_BUFFER に、stencil だけの attachment は 2D D32、STENCIL_BUFFER 5 dword（isl の GFX_VER < 12） | state.c・blit.c |
| CPS_POINTERS・PRIMITIVE_REPLICATION | 出さない | draw.c・blit.c |
| RENDER_SURFACE_STATE | unorm path bit（Gen12 だけ）を立てない | state.c |
| MOCS | uncached は Gen9 の index 0（skl_mocs_table）、write-back は両方 index 2 | state.c・draw.c・blit.c・compute.c・heap.h |
| 空の instruction window | Gen9 の EOT の send（brw_asm --gen=skl で作り、brw_disasm で確かめた） | state.c |
| compute | IDD の preemption disable を立てない、VFE は 56 thread/subslice と reset gateway timer、shared memory の kernel の前後で L3CNTLREG（SLM 32・URB 16・All 48 ↔ 既定 0x60000060、anv の flush の手順）、GPGPU の前に CC_STATE_POINTERS を 0、read-only cache の invalidate を分けて出す、3D に戻る前に MEDIA_VFE_STATE を出し直す。MEDIA_CURBE_LOAD は毎回出している | compute.c |
| compile の版 | pipeline と blit の kernel を `drv_i915_shader_compile_gen(…, graphics_ver, …)` で（Gen9 の GT だけ 9、他と session 無しは 12） | pipeline-prepare.c・blit.c |
| video | Gen9 では video を出さない | vulkan.c |
| device 名 | Gen9 は "zedBSD i915 (Gen9)" | instance.c |

## 確かめ（host）

- `GENXML=<mesa>/src/intel/genxml BRW_TOOLS=<mesa build>/src/intel/compiler sh plan/ws229/tests/host/run.sh`（新しい試験）→ PASS:
  - Gen12: draw（depth・stencil・blend・scratch）、indexed draw（GS・SGVS）、fill と copy の rectangle、shared memory と indirect の dispatch、surface state・sampler・IDD・空の window の 14 file が、03dee2287（p004 の前）の tree で同じ試験を `-DTEST_BASELINE` で build した物と byte で同一。
  - 12 batch（736 命令）と 14 構造体が、Gen12 は gen120.xml、Gen9 は gen90.xml（gen80 → gen75 … の継承、`plan/ws083/tests/genxml-decode.py` の解決と sha256 の pin）で全 dword が命令に入り、field の外の bit が無い。両版を命令ごとに並べ、同じ名の field は同じ値（違ってよい物は理由付きの表、`gen9-check.py`）。
  - 空の window: brw_disasm で Gen9（skl）4 個、Gen12（tgl）4 個の EOT の send。
- `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws229-p004/k64 vmunix` → PASS（-Werror、warning 0、amd64 vmunix check PASS）。
- `plan/ws083/tests/genxml-decode.py --root gen90.xml`（新しい選択肢）、`--self-test`（gen120）0 failures。`plan/ws101/tests/host/genxml-check.py` に省略できる第 3 引数 ROOT（既定 gen120.xml）。

## 未実施・残り

- 実機（Gen9 の GT で描けるか）は p005。QEMU は使っていない。
- `plan/ws101/tests/host/run.sh`・`plan/ws031/tests/run-vk-host-tests.sh`・`plan/ws075/tests/run-host-layered.sh` は走らせていない（ws101 の script は `rm -rf` の trap を持つので担当は走らせない）。Gen12 の byte の同一は上の試験で draw・rectangle・dispatch・state の全部の書き手について確かめた。kernel（EU）の byte は `drv_i915_shader_compile_stage()` が `compile_gen(…, 12, …)` の wrapper なので同一。
- review-1 M9 のうち: GPGPU の post-sync の CS stall は、zedBSD の post-sync の PIPE_CONTROL（fence.c・query）が 3D でしか出ないので不要と判断し変えていない。VF cache の 4 GiB の別名は、毎 operation の context setup で VF cache を invalidate しているので追加の追跡はしていない。
- `tests/fixtures/draw-fixture.c`（kernel の試験の Gen12 の draw）は Gen9 の形にしていない（p005 で Gen9 の機械で使うなら直す）。
- scratch の thread ID の数（heap.h）は Gen12 の大きい値のまま（Gen9 の必要数より大きい）。
- Gen9 の 3DSTATE_PS・WM の Gen9 固有の WA（anv の GFX_VER == 9 の残り）と HiZ・MSAA の Gen9 の確かめは p005 の実機で。

## p005 のための fixture（2026-10-14 P3、ws229-p005 の一部。phase005/phase.md は P1 が書く）

範囲: review-1 の中 7 と §5（p004 の所有に足された `tests/fixtures/draw-fixture.c`）。eu-test.c・image・手順は P1。

| 何 | 内容 | file |
| --- | --- | --- |
| fixture の版 | `drv_i915_draw_fixture_write_state_gen()`・`_build_batch_gen()`・`_mocs_gen()`（9 で Gen9、他は Gen12）。古い名前は Gen12 の wrapper（byte 不変）。内部の batch に `graphics_ver` | draw-fixture.[ch] |
| Gen9 の batch | render/state.c の Gen9 の形に合わせた: PIPE_CONTROL の HDC flush の代わりに DC flush、PIPELINE_SELECT 0x69040300、STATE_BASE_ADDRESS 19 dword、DEPTH_BOUNDS・VF_SGVS_2・CPS_POINTERS・PRIMITIVE_REPLICATION を出さない、VB の L3 bypass を立てない、3DSTATE_URB_VS/HS/DS/GS（VS は chunk 4 から 928 entry、他は chunk 20 で 0）、SF の deref 0、DEPTH_BUFFER の format を 20:18、STENCIL_BUFFER 5 dword、RSS dw1 bit 31（unorm path）を立てない、uncached MOCS は index 0。invalidate の PIPE_CONTROL（CS stall と invalidate だけ）に Gen9 では stall at scoreboard を足した（SKL PRM の CS stall の制約、iris と同じ） | draw-fixture.c |
| Gen9 の pixel shader | Gen12 の SIMD8 版と同じ手順（f1 に pixel mask、A64 untyped write で marker 0xc0ffee01 を 0x100400c10 へ、g112-g115 の赤を headerless の RT write と EOT）。Mesa 25.0.7 の `brw_asm --gen=skl` で組んだ 192 byte。後ろの 832 byte は Gen9 の EOT（GEN9_EOT_ONLY）で敷く | draw-fixture.c、plan/ws229/tests/fixtures/draw-fixture-ps-gen9.asm |
| Gen12 だけの binary の拒否 | `drv_i915_tex_fixture_supported()`（Gen9 で 0）。draw-test.c の tex・t3・bl はこれで、r1 は compute の半分が eu-test の Gen12 の kernel と batch なので Gen9 で拒否。log「not run: … Gen12 only (graphics version 9)」を出し `I915_TEST_SKIPPED`（scenarios.h、-1）を返し、runner は `SKIP` と log（PASS にも FAIL にもしない）。draw は Gen9 の fixture で走り、Gen9 の select の確かめ（0x69040300 が 1 つ、他の select 無し）を draw-test.c に置いた | draw-test.c・runner.c・scenarios.h・README.md |
| `vkref-generated.inc` | どの source からも include されていない（`git grep vkref`）。拒否する呼び手が無いので変えていない | — |
| ついで（Q1 経由の P1 の報告） | `plan/ws101/tests/host/genxml-check.py` の要約の行が xml の名前の代わりに命令の名前を出していた（`name` の使い回し）。`root_xml` に分けた | genxml-check.py |

確かめ（host、QEMU は使っていない）:
- `GENXML=<p1>/build/mesa-tools/mesa-25.0.7/src/intel/genxml BRW_TOOLS=<p1>/build/mesa-tools/build-asm/src/intel/compiler sh plan/ws229/tests/host/run.sh` → PASS。
  - fixture の Gen12 の batch・state page・RSS の 3 file が 03dee2287 の tree の古い API の出力と byte で同一（計 17 file）。新しい tree の中でも古い名前と `_gen(…, 12)` の出力が同一（assert）。
  - 12-fixture-draw と 9-fixture-draw を gen120.xml・gen90.xml で decode（全 dword が命令、field の外の bit 無し）、両版を命令ごとに並べ field を比べて PASS（14 batch・882 命令・16 構造体）。gen9-check.py の変更: MI_STORE_DATA_IMM の 64 bit の Immediate Data が 4 dword の命令で切れる分を読む（以前は field ごと捨てて stray と判定）、Stall At Pixel Scoreboard の版の差を理由付きで表に。
  - Gen9 の shader が .asm の `brw_asm --gen=skl` の 192 byte と同一、後ろが Gen9 の EOT の send 52 個（brw_disasm）。
- `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws229-p005f/k64-exec I915_TESTS=y I915_TEST_SET=execution ZEDBSD_TEST_CPPFLAGS=-DI915_TEST_SCENARIO=draw vmunix` と `I915_TEST_SET=display`（BUILD=build/ws229-p005f/k64-disp）→ どちらも warning 0、amd64 vmunix check PASS。
- genxml-check.py: ws229 の 12/9-dispatch-shared を `PIPELINE_SELECT` の 1 行の expect で流し、要約が「(gen120)」「(gen90)」になることを確かめた。

未実施・残り:
- 実機（Gen9 で draw が描けるか、Gen12 の 5330 で draw・tex・r1・t3・bl が変わらず PASS か）は p005 の T1。fixture の Gen12 の byte は同一なので 5330 の回帰は fixture の面では変化なし（draw-test.c の経路の変更は select の確かめの wrapper だけ）。
- Gen9 の draw は eu-test の helper（request・park・hang・forcewake）を使う。P1 の eu-test の Gen9 化（`struct i915_test_eu` の `graphics_ver` など）が merge されたら、draw-test.c の draw の `t->graphics_ver` を env から入れる 1 行が要る（今の main に field が無いので入れていない。0 のままだと hang の dump が Gen12 の ring fault register を読む）。
- r1 の Gen9 化（eu-test の Gen9 の kernel と batch を使う）は P1 の eu-test の後。
- Gen9 で CACHE_MODE_1 を fixture は書かない（anv の context 初期化の値。単色の draw には効かない判断）。
- `build/ws229-p005f/`（kernel 2 本・log・逆アセンブルの作業物）と `build/ws229-p004/host.run.*` の古い run は Q1 が消してよい。

## p005 の続き（2026-10-14 P3 の次の世代、ws229-p005 の一部）

範囲: 上の残り 2 つ（draw の `t->graphics_ver`、r1 の Gen9 化）。

| 何 | 内容 | file | commit |
| --- | --- | --- | --- |
| hang の dump の版 | `i915_draw_find_engine()` で `t->graphics_ver = env->graphics_ver`。draw・tex・r1・t3・bl の hang の dump が Gen9 では Gen9 の ring fault register（0x4094）を読む。draw-test.c の中で `t->graphics_ver` を読むのは eu-test.c の `drv_i915_test_eu_hang_dump_reset()` だけ（他の helper は読まない）ので、Gen12 の振る舞いは変わらない。eu-internal.h の注記を直した | draw-test.c・eu-internal.h | e8a67ef52 |
| r1 の Gen9 化 | 拒否（SKIP）を外し、両方の半分を GT の版で作る: compute は `drv_i915_test_eu_build_batch_gen()`・`_check_pipeline_select_gen()`・`drv_i915_test_eu_fill_idd()`・Gen9 の store kernel（`drv_i915_test_eu_kernel_gen9`）、thread の上限は Gen9 で 56×subslice−1（eu-test と同じ）、draw は `_mocs_gen`・`_write_state_gen`・`_build_batch_gen` と draw-test.c の版つきの select の確かめ。kernel の読み戻しの比較も版の kernel で。Gen9 で拒否するのは tex・t3・bl だけになった | draw-test.c | c7c80bf82 |

確かめ（host と build。QEMU・実機は使っていない）:
- `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws229-p005g/k64-exec I915_TESTS=y I915_TEST_SET=execution ZEDBSD_TEST_CPPFLAGS=-DI915_TEST_SCENARIO=draw vmunix`（1 行の後と r1 の後）→ rc 0、warning 0、`amd64 vmunix check: PASS`（draw-test.o が作り直されたことを確かめた）。
- r1 の compute の state page: draw-test.c を host で丸ごと compile する使い捨ての harness（scratchpad、commit しない）で、
  `i915_r1_write_c1_state(page, 12)` の 4096 byte が変更前の手書きの値（kernel・IDD の dw2=1<<20・marker 0xdead0000）と同一、
  `(page, 9)` が Gen9 の kernel と `fill_idd(9)`（dw2=0）を持つ。Gen12 の c1 の batch は `build_batch_gen(…, 12)` そのもの（旧名の wrapper の中身）、
  draw の batch・state は run.sh が旧名と `_gen(…, 12)` の同一を確かめている。
- `GENXML=<p1>/build/mesa-tools/mesa-25.0.7/src/intel/genxml BRW_TOOLS=<p1>/build/mesa-tools/build-asm/src/intel/compiler sh plan/ws229/tests/host/run.sh` → PASS（Gen12 の 17 file が 03dee2287 と同一、14 batch・882 命令）。
- 同じ環境で `sh plan/ws229/tests/eu-test-gen9-host.sh` → PASS（eu-gen12 の batch の SHA-256 09786119…0fe6 が Gen9 化の前と同一）。

取りやめた物: Q1 経由のユーザーの指示で一度「全部入り」の scenario `gen9`（eu → vkcs → vkx → vke1 → vke2 → vkc を 1 回の起動で）と
1 つの USB の image を作りかけた（337700822・077df9995・1bed58a61）が、同じ日のユーザーの指示「全部入りの Gen9 の imageはいらないです。」で
取りやめ、d63e083b9 で 3 つとも戻した（tree は c7c80bf82 と同一）。試験は SV7 の passthrough で試験ごと（P1）。

未実施・残り:
- 実機: Gen9 の draw・r1（SV7 の passthrough の試験ごと、P1・T1）、Gen12 の 5330 で draw・r1・tex・t3・bl が変わらず PASS か（T1）。
- `build/ws229-p005g/`（kernel 4 本と log）と `build/ws229-gen9/`（止めた image の build の途中物）は Q1 が消してよい。
