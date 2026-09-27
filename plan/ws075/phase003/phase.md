<!-- awesome-plan project=zedbsd record=ws075p003 -->

# ws075-p003: 実行器の primitive topology

Phase ID: `ws075-p003`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-27。実機の vkx 9/9 PASS（TOPOLOGY を含む）、boot test PASS）
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

| 確認 | 結果 |
| --- | --- |
| build | `make vmunix`（-Werror、`plan/ws031/tests/config-zdesktop-hw.mk` と I915_TESTS=y の両方）PASS、warning 0。amd64 の image（`plan/ws068/tests/build-glsl-image.sh build/amd64`）PASS |
| style-check | 変えた file の数は前と同じ（state.c 7、draw.c 10、executor.c 26。新しい行に 0） |
| 実機（i915、5330 の passthrough）の `vkx` | **PASS 9/9**（`VKX-TOPOLOGY PASS`: strip の赤と fan の緑の四角が INDEX32 と同じ画素。INDEX16・INDEX32・VIEWPORT・COPY・GRID・MIP の 3 つも PASS）。`build/ws075-p003/vkx.log` |
| boot test（`build/ws075-p003/boot/login.png`） | PASS |
| Venus の回帰 | 未実施（i915 の driver だけの変更。Venus の guest は i915 を使わない） |

実機の test build は kernel の console を serial に出す構成が要る（worktree に `config.mk` が無く、zdesktop の構成は serial を
出さないので verdict の行が読めなかった）。`plan/ws075/tests/config-test-hw.mk`（zdesktop の構成 + `CONFIG_PCAT_SERIAL_MIRROR`）を
足した: `BUILD=build/resident-vkx ZEDBSD_CONFIG=plan/ws075/tests/config-test-hw.mk flock /tmp/i915-hw.lock
plan/ws031/tests/vkloop-hw.sh test vkx`。

### 制限・移管

- line・point の描画は実機で未確認（vkx は三角形の strip と fan だけ。line の画素は rasterization の規則で端が揺れるので
  step にしなかった）。GL の client の実機の run（p004 以降）で確かめる。
- 幅 1 以外の線、PointSize、primitive restart（Vulkan の primitiveRestartEnable、3DSTATE_VF の cut index）は無い
  （libGLESv2 は restart を CPU で展開する）。
