
## p011 増分E-7 (2026-09-15): Mesa の EU アセンブラ導入と、ピクセルシェーダ投入（draw step 2b、未完）

### 成果1: gentool（Mesa の Gen12 EU アセンブラ／逆アセンブラ）をビルド

`plan/ws031/mesa-refs/mesa` を最小構成で configure してビルド:

    python3 -m venv /tmp/mesa-venv && /tmp/mesa-venv/bin/pip install Mako PyYAML packaging setuptools
    PATH=/tmp/mesa-venv/bin:$PATH meson setup build-gentool -Dtools=intel \
      -Dgallium-drivers= -Dvulkan-drivers= -Dplatforms= -Dglx=disabled \
      -Degl=disabled -Dgbm=disabled -Dopengl=false -Dllvm=disabled -Dbuildtype=release
    ninja -C build-gentool src/intel/compiler/gen/gentool

    gentool asm -p tgl -o out.bin shader.asm
    gentool disasm -p tgl out.bin

これは**再利用可能な大きな資産**: 自前コンパイラ（vk/eu.c, vk/compile.c）の出力を逆アセンブルして
検証できるようになる。ユーザ環境は汚していない（venv と mesa-refs 配下のみ）。

実際に初回から効果があり、`ERROR: send with EOT must use g112-g127` を**ビルド時に**指摘した。
実機なら原因不明のハングになっていた類のミス。

### 成果2: Gen12 render target write の符号化を確定

    (W) mov (8|M0)  r112:f 0x3f800000:f   /* red   */
    (W) mov (8|M0)  r113:f 0x0:f          /* green */
    (W) mov (8|M0)  r114:f 0x0:f          /* blue  */
    (W) mov (8|M0)  r115:f 0x3f800000:f   /* alpha */
        send.render (8|M0) null r112 null 0x0 0x08031400 {EOT,@1}
    // wr:4+0, rd:0; simd8 rt_write last_rt (8) bti(0)

- SFID = GEN_SFID_RENDER_CACHE = 5
- desc: bti bits7:0 / msg_control bits13:8 (SIMD8 single source subspan01 = 4) /
  last_rt bit12 / msg_type bits17:14 = 12 / **mlen bits28:25 / rlen bits24:20**
  （mlen/rlen は 24:20・19:16 ではない。gentool の `wr:`/`rd:` 表示で確定）
- Gen12 は ex_desc に Render Target Index（bits 12+）と null_rt（bit20）
- EOT する send のペイロードは **g112-g127 でなければならない**
- ソースは `plan/ws031/shaders/const_color_ps.asm`

なお自前コンパイラの `i915_vk_eu_send()` は mlen/rlen/EOT/descriptor をすべて `(void)` で捨てる
プレースホルダのままで、実 RT write は出せない。まず手書きで正解を確立し、後でコンパイラに教える方針。

### 実機で発見・修正した実バグ（3件）
1. **MOCS フィールドは index を1ビット左シフトして入れる**。生の index を書くと1つ右の
   エントリが選ばれ、Gen12 の index 0/1 は**予約**なので未定義動作になる。
   根拠: Linux i915 の MOCS テーブル（index2=WB, index3=uncached）と Mesa isl の TGL 分岐
   （`internal = 2 << 1`, `uncached = 3 << 1`）が一致。`GEN12_MOCS(index)` を追加し、
   STATE_BASE_ADDRESS / RENDER_SURFACE_STATE / VERTEX_BUFFER_STATE / XY_FAST_COLOR_BLT を修正。
   clear selftest は新エンコーディングでも通過（修正が安全であることの確認）。
2. **`CULLMODE_BOTH = 0`, `CULLMODE_NONE = 1`**。3DSTATE_RASTER を全ゼロで出すと
   **全プリミティブが culling される**。BLORP も anv も CULLMODE_NONE を明示している。
3. **null depth buffer も型を持つ**。`SURFTYPE_NULL` には `D32_FLOAT(1)` を組み合わせる
   （Mesa isl と同じ）。フォーマット 0 は深度ステンシル書式で、存在しないステンシルを待たせる。

### 現在の到達点と残課題
ジオメトリは**クリッパまで完全に通っている**（実機カウンタ: ia_vert=3, ia_prim=1, vs=3,
**cl_inv=1, cl_prim=1**）。しかし **PS スレッドが一度も起動しない**（ps=0, ps_depth=0）まま、
3DPRIMITIVE 直後の PIPE_CONTROL（CS_STALL + RT flush）でパイプラインがドレインせず停止する。
ACTHD = batch->va + 0x3c0 = dword 240（バッチ全246 dword の末尾側）で、IPEHR = 0x7a000004 = PIPE_CONTROL。

**シェーダコードは無関係であることを証明済み**: mov を一切含まない 1 命令
（`send.render null_rt {EOT}`、ループ不可能）でも同一箇所で同一のハング。つまり停止点は
CL→SF→WM→PSD の固定機能ステートであって、EU プログラムではない。

未検証の Mesa との差分（次に当たる候補）:
- **3DSTATE_SBE `Number of SF Output Attributes` = 0**。Mesa の経路（BLORP の clear、anv の
  simple shader）は常に 1 以上。属性ゼロの PS が許されない可能性。
- **3DSTATE_WM `Barycentric Interpolation Mode` = 0**。
- **3DSTATE_DEPTH_BOUNDS を出していない**（anv の simple shader は出す）。
- 3DSTATE_VF_SGVS の InstanceIDEnable（BLORP/anv は立てる）。
