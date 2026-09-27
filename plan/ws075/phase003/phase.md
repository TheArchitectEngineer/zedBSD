<!-- awesome-plan project=zedbsd record=ws075p003 -->

# ws075-p003: 実行器の primitive topology

Phase ID: `ws075-p003`
Parent: [WS075](../ws.md)
Status: in-progress（2026-09-27 着手）
Phase disposition: normal
承認: 2026-09-27 ユーザー「…i915の高度化に進んでください。」、WS075 の計画（main の登録、p003 の計画は main が了承）。

## 範囲

p001 で、実行器は triangle list だけを描き、他の topology の pipeline の draw を拒むことが分かった（`render/state.c`）。
GL の app（libGLESv2 は GL の mode を Vulkan の topology へそのまま渡す）の大半が strip・fan・line・point を使う。

1. point list、line list・strip、triangle strip・fan を描く（adjacency と patch は拒んだまま）。
2. triangle fan の provoking vertex を Vulkan の first-vertex の約束に合わせる（fan の三角形の 2 番目の頂点、anv と同じ）。
3. 試験: 実機の `vkx` の suite に TOPOLOGY の step（strip と fan で四角）。

範囲外（後の Phase へ）: 幅 1 以外の線（今も 1 pixel で描き XXX を 1 度出す）、PointSize（compiler、p004）、index の型の追加、
vkFreeDescriptorSets（使う client が今は無い）。

## 設計

- `intel/genxml.h`: 3D_Prim_Topo_Type の POINTLIST 1・LINELIST 2・LINESTRIP 3・TRISTRIP 5・TRIFAN 6 と、3DSTATE_SF（dword 3、
  bits 26:25）・3DSTATE_CLIP（dword 2、bits 1:0）の Triangle Fan Provoking Vertex Select（Mesa 25.0.7 の gen70.xml・gen120.xml・
  gen80.xml、sha256 を header の出典に書いた）。
- `render/state.c`: `drv_i915_gfx_topology()` が Vulkan の topology を GEN の値へ（取らないものは 0）。`3DSTATE_VF_TOPOLOGY` は
  その値、0 は今まで通り `XXX unimplemented path: primitive topology` で ENOTSUP。raster の SF と CLIP の fan の provoking vertex を 1。
- `render/draw.c`: 3DPRIMITIVE の topology を同じ関数から（今までは TRILIST 固定）。
- `tests/render/executor.c`（`vkx`）: 色の pipeline の strip 版と fan 版、index buffer の byte 512・528 に 4 つの index、
  TOPOLOGY の step（INDEX32 と同じ赤と緑の四角を strip と fan で）。

## 判断が要る点（既定を選んで進める）

（なし）

## 検証

未実施。build（`make vmunix`、-Werror）PASS。実機の `vkx`。
