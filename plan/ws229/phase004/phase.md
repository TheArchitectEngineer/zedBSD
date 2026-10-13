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
