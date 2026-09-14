# WS031 外部設計書: i915ネイティブVulkan実行器

この文書は WS031 の**外部設計**であり、WS単体で自足する。全体アーキテクチャ、モジュール分解、モジュール間インタフェース契約（関数の大枠）、ファイル境界を規定する。**細かい内部設計は各 Phase の計画時に Phase 計画 doc へ書き、実装はそれに従う**。設計の背景・段階・受け入れは [native-vulkan-design.md](native-vulkan-design.md)、WS の管理情報は [ws.md](ws.md)。

対象: Dell Latitude 5330 / Alder Lake-P（Gen12 Xe-LP、`8086:46a8`）。前提: WS029 i915 driver（実機動作済み）、WS030 libvulkan.so。

---

## 1. 委譲モデルと文書規約

大量のコーディングを下位モデルへ委譲する。委譲の単位は **Phase = 1 モジュール**。各 Phase は「実装すべきインタフェース（関数）」を規約として受け取り、そのインタフェースを満たすようにモジュール内を自由に実装する。

### 文書の階層

1. **外部設計書（本書、WS所有）**: モジュールの責務・所有ファイル・**モジュール間の公開インタフェースの大枠**・ファイル境界・下位（WS029 core）への呼び出し許可・Phase 対応を規定する。
2. **Phase 計画 doc（`plan/ws031/phaseNNN/phase.md`、各 Phase 計画時に作成）**: そのモジュールの**確定インタフェース（正本）**、内部の関数構成ガイド、触れるファイル／触れないファイル、依存する前段 Phase doc、受け入れ条件、試験を書く。ここで「あとはコーディングするだけ」の状態にする。
3. **実装（委譲）**: Phase 計画 doc に従いモジュール内を実装する。公開インタフェースは変更しない（変更が要る場合は Phase 計画 doc を先に改訂し、依存する後段へ通知）。

### インタフェースの正本と参照方向

- 本書はインタフェースの**大枠**（関数名・目的・引数/返り値の意味）を与える。
- 各モジュールの**確定インタフェース**は、そのモジュールの Phase 計画 doc（ヘッダ `*.h` の内容として）で正本化する。
- **後段 Phase は前段 Phase doc の確定インタフェースを参照**して実装する。前段が未確定の関数は本書の大枠に従って仮結線し、前段確定後に整合させる。
- 公開ヘッダ `*.h` が唯一の契約面。`.c` の内部関数・データ構造は各モジュールの自由。

### Phase 計画 doc の必須構成（テンプレは §10）

module 名 / 実装する公開インタフェース（`*.h`） / 内部関数構成ガイド / 触れるファイル / 触れないファイル / 依存（参照する前段 doc と WS029 core 関数） / 受け入れ条件 / 試験（host fixture）/ 見積。

---

## 2. アーキテクチャ全体像

```
userland
  vkdemo → libvulkan.so     Vulkanコマンドを直列化（Venus op番号を再利用）。無改造〜capset整合のみ。
     │  drv_gpu UAPI: GPU_GET_INFO / GPU_GET_CAPSET / GPU_BLOB_CREATE / GPU_RESOURCE_MAP / GPU_COMMAND_SUBMIT
=====│=========================================================== kernel境界（UAPI不変）
kernel: src/drivers/gpu/i915/vk/  … native Vulkan実行器（本WSで新設）
     [cmd] decoder+object table ──→ [res] 資源/画像/sampler/descriptor
        │                              │
        ├──→ [spirv] SPIR-Vパーサ→IR ──→ [compile] IR→GEN ──→ [eu] EU命令エンコーダ
        │                              │
        ├──→ [pipe] 3Dパイプラインstate ←── compile結果/res
        ├──→ [cmdbuf] command buffer→GENバッチ、draw
        ├──→ [sync] fence/semaphore/query
        └──→ [wsi] KMS/modeset/scanout/swapchain present
                             │  下位呼び出し（許可された関数のみ）
i915 core（WS029）: engine(RCS0/BCS0)/LRC/execlists/request/GGTT/PPGTT/GEM/uncore/irq
```

データフロー（1 submit）: libvulkan が `GPU_COMMAND_SUBMIT` で wire を送る → `cmd` がデコードし op を dispatch → 生成系は `res`/`compile`/`pipe` を呼んで i915 資源・GEN state を作る → `vkQueueSubmit` 時に `cmdbuf` が記録済み command buffer を GEN バッチへ変換し WS029 の RCS0 request 経路へ投入 → 完了は `sync` が seqno/breadcrumb から `drv_gpu_complete` へ → present は `wsi` が scanout。

---

## 3. モジュール分解

| Module | 責務 | 所有ファイル | Phase |
| --- | --- | --- | --- |
| **top** | drv_gpu ops から vk 実行器への入口、vk 全体の attach/detach、共有型 | `vk/vk.h`, `vk/vk.c`, `vk/vk-internal.h` | p002（p001でheader枠） |
| **cmd** | wire decoder、object/handle table、op dispatch、反射的 get 系 | `vk/cmd.c`, `vk/cmd.h` | p002 |
| **res** | memory/buffer/image/image view/sampler/descriptor を i915 資源・surface/sampler state へ | `vk/res.c`, `vk/res.h` | p003 |
| **spirv** | SPIR-V パーサ → baseline IR | `vk/spirv.c`, `vk/spirv.h` | p004 |
| **eu** | Gen12 EU 命令・message エンコーダ（bit layout） | `vk/eu.c`, `vk/eu.h`, `vk/linux/eu-*.inc` | p005 |
| **compile** | IR → GEN codegen（素朴レジスタ割当、命令選択、`eu` 利用） | `vk/compile.c`, `vk/compile.h` | p006 |
| **pipe** | Gen12 3D パイプライン state emission（`3DSTATE_*`、URB、binding/sampler、RT、depth） | `vk/pipe.c`, `vk/pipe.h`, `vk/linux/3dstate-*.inc` | p007 |
| **cmdbuf** | command buffer 記録の GEN バッチ変換、draw、WS029 request へ投入 | `vk/cmdbuf.c`, `vk/cmdbuf.h` | p008 |
| **sync** | fence/semaphore/query/timeline を WS029 seqno/completion へ | `vk/sync.c`, `vk/sync.h` | p009 |
| **wsi** | KMS/modeset、display plane、scanout、swapchain present | `vk/wsi.c`, `vk/wsi.h`, `vk/display.c`, `vk/display.h`, `vk/linux/display-*.inc` | p010 |

各モジュールは自分の `*.h` を公開し、他モジュールの `*.h` のみを include する（`.c` を跨いで include しない）。`vk-internal.h` は共有型（handle、object kind、error 変換、vk device 文脈）を持ち全モジュールが include する。

---

## 4. モジュール間インタフェース契約（大枠）

各モジュールが公開する関数の**大枠**。確定シグネチャは各モジュールの Phase 計画 doc で正本化する。命名は `i915_vk_<module>_<verb>`。全て `struct i915_vk_device *` 文脈を第一引数に取り、`int`（0/errno）を返すことを既定とする（値返しは out param）。

### 4.1 top（`vk.h` / `vk-internal.h`）
- `int drv_i915_vk_attach(struct i915_device *dev, struct i915_vk_device **out);` — vk 実行器を i915 device に接続。capset/capability を用意。
- `void drv_i915_vk_detach(struct i915_vk_device *vk);`
- `int drv_i915_vk_command(struct i915_vk_device *vk, struct i915_vk_session *s, const void *wire, size_t bytes);` — drv_gpu の command 経路から呼ばれる入口。`cmd` へ渡す。
- `vk-internal.h`: `struct i915_vk_device`（i915 device 参照、object table、global state）、`struct i915_vk_session`（per-open。command buffer/descriptor heap 等）、`enum i915_vk_object_kind`、handle 型、`i915_vk_errno(VkResult)` 変換。

### 4.2 cmd（`cmd.h`）
- `int i915_vk_cmd_dispatch(struct i915_vk_device *vk, struct i915_vk_session *s, struct i915_vk_reader *r);` — 1 コマンドをデコードし該当モジュールを呼ぶ。
- object table: `i915_vk_obj_insert/lookup/remove(vk, kind, handle, ptr)`。
- reader/writer primitive（wire の u32/u64/handle/配列読み、reply 書き）: `i915_vk_read_*`, `i915_vk_reply_*`。
- op → module の対応表（dispatch table）は cmd 内部。生成系は res/pipe を、記録系は cmdbuf を、submit は cmdbuf+sync を、present は wsi を呼ぶ。

### 4.3 res（`res.h`）
- memory: `i915_vk_memory_alloc/free`（GEM object を確保、host-visible は CPU view）。
- buffer: `i915_vk_buffer_create/destroy/bind_memory`。
- image: `i915_vk_image_create/destroy/bind_memory`（tiling、`RENDER_SURFACE_STATE` 相当を保持）、`i915_vk_image_view_create/destroy`。
- sampler: `i915_vk_sampler_create/destroy`（`SAMPLER_STATE`）。
- descriptor: `i915_vk_dsl_create`（set layout）、`i915_vk_dpool_create`、`i915_vk_dset_alloc/update`、`i915_vk_build_binding_table`（surface/sampler heap を作り pointer を返す）。
- 返す実体は GEM object・GGTT/PPGTT オフセット・state dword 群。GEM/GGTT/PPGTT の操作は §6 の WS029 core 関数のみを使う。

### 4.4 spirv（`spirv.h`）
- `int i915_vk_spirv_parse(const uint32_t *code, size_t words, struct i915_vk_shader_ir **out);` — SPIR-V を baseline IR へ。
- IR 型（`struct i915_vk_shader_ir`）: stage、entry、input/output/uniform（location/binding）、命令列（SSA、算術・swizzle・load/store・image sample・組込み関数）。制御流れは当初 straight-line のみ（vkdemo に分岐なし）。未対応 opcode は明示的に EINVAL。
- `void i915_vk_spirv_free(struct i915_vk_shader_ir *);`

### 4.5 eu（`eu.h` + `linux/eu-*.inc`）
- Gen12 EU 命令の 1 命令ぶんを byte 列へ encode する low-level emitter。`i915_vk_eu_mov/add/mul/sel/math/send/...`（必要最小集合）。SIMD8 固定を既定。
- `linux/eu-*.inc`: 命令の bit layout・opcode・message descriptor を**出典付きで転記**（Intel PRM / Mesa MIT、`gen-inc.py` 規律）。値はここ、ロジック（どの命令を出すか）は compile。

### 4.6 compile（`compile.h`）
- `int i915_vk_compile(struct i915_vk_device *vk, const struct i915_vk_shader_ir *ir, enum i915_vk_stage stage, struct i915_vk_shader_binary **out);`
- baseline: SSA 値ごとに GRF を素朴割当（不足時に拡張、spill は最小）、IR 命令を `eu` 呼び出し列へ 1 対 1 で lower。push constant/varying/sampler を register/URB/binding へ配置。
- 返り値 `i915_vk_shader_binary`: GEN バイナリ（GEM に置く）、thread/GRF 数、URB/varying レイアウト、使用 binding/sampler index、entry offset。

### 4.7 pipe（`pipe.h` + `linux/3dstate-*.inc`）
- `int i915_vk_pipeline_create(vk, const VkGraphicsPipelineCreateInfo*, ..., struct i915_vk_pipeline **out);` — VS/FS の compile 結果と固定機能状態から、draw 時に発行する `3DSTATE_*` dword 群（`3DSTATE_VS/PS/VF/URB_*/SBE/WM/PS_EXTRA/DEPTH_BUFFER/PS_BLEND/VIEWPORT` 等）を構築・保持。
- `linux/3dstate-*.inc`: 各 `3DSTATE_*` の dword layout・enum を出典付き転記。
- `int i915_vk_pipeline_emit(vk, struct i915_vk_pipeline*, struct i915_vk_batch*);` — 保持した state を batch に書く。

### 4.8 cmdbuf（`cmdbuf.h`）
- `i915_vk_cmdbuf_begin/end`、`i915_vk_cmd_bind_pipeline/bind_vertex/bind_descriptor/push_constants/begin_render_pass/end_render_pass/draw` — 記録時に GEN 3D command（VF、binding/sampler pointer、`3DPRIMITIVE`、clear）を `struct i915_vk_batch`（GEM 上の GEN バッチ）へ蓄積。
- `i915_vk_queue_submit(vk, s, cmdbufs, fence)` — batch を WS029 の RCS0 request として投入（§6）。RCS の 3D 有効化（`PIPELINE_SELECT`、必要な context state）はここと pipe が担う。

### 4.9 sync（`sync.h`）
- fence/semaphore/query/timeline: `i915_vk_fence_create/destroy/reset/wait/get_status`、`i915_vk_semaphore_*`、`i915_vk_query_*`。WS029 の seqno/HWSP breadcrumb と `drv_gpu_complete`/`retire_waitq` へ結線。

### 4.10 wsi（`wsi.h` / `display.h` + `linux/display-*.inc`）
- display: `i915_vk_display_init`（EDID 読取、mode 列挙、pipe/transcoder/PLL/DDI で eDP を modeset）、`i915_vk_display_flip`（plane surface を GGTT オフセットへ設定して scanout）。
- swapchain/WSI: `i915_vk_swapchain_create/destroy/acquire/present` — swapchain image を GGTT 可視 surface として確保し、present で `display_flip`。
- `linux/display-*.inc`: display register/PLL/DDI の layout を出典付き転記。WS029 f003 の framebuffer 保持（GMADR→GGTT）を土台にする。

---

## 5. 段階（増分）とモジュールの充足順

- **増分A（三角形1枚・定数色）**: top/cmd（最小 op）、res（buffer/RT image）、spirv/compile/eu（passthrough VS＋定数 FS）、pipe（最小 3DSTATE）、cmdbuf（bind+draw+clear）、wsi（modeset+flip）。sync は fence 待ちのみ。
- **増分B（texture＋depth）**: res（image/sampler/descriptor/binding table）、compile（sampler send、UV 補間）、pipe（depth buffer、SBE）。
- **増分C（vkdemo 実 shader）**: spirv/compile の対応命令拡張（sin/cos math、push constant、行列演算）、sync（複数 fence/timeline）。

各 Phase はモジュールを実装するが、増分Aを最初の実機到達点として全モジュールの**最小経路**を先に貫く。Phase 計画時に「増分Aで必要な関数」を先行実装対象として明示する。

---

## 6. 下位（WS029 core）への呼び出し許可インタフェース

vk 層は WS029 core の**次の関数のみ**を呼んでよい。これ以外の core 内部関数・データ構造へは触れない。必要な新規 hook は Phase 計画 doc で提案し、承認後に該当 core ファイルへ最小追加する（§7 の例外扱い）。

- GEM: `drv_i915_gem_create/destroy/bind_ggtt/unbind_ggtt/bind_vm/unbind_vm/read/write`。
- GGTT: `drv_i915_ggtt_alloc/free/insert/clear`。
- PPGTT: `drv_i915_ppgtt_insert/clear`（session の vm を使う）。
- request/engine: RCS0 への batch 投入 API（WS029 の native stream 投入経路を vk 用に呼ぶ。関数名は p008 計画で確定。既存 `i915_submit_stream` 相当を再利用または薄い wrapper）。
- uncore: `drv_i915_read32/write32/wait32`、`drv_i915_forcewake_get/put`。
- 完了: `drv_gpu_complete`、`drv_gpu_capacity_changed`、`retire_waitq`。

RCS の 3D 有効化（`CTX_R_PWR_CLK_STATE`、`PIPELINE_SELECT`、3D 用 LRC state）は WS029 の `lrc.c`/`engine.c` に**最小の拡張 hook**が要る。これは p007/p008 の計画で差分を提示し承認を得てから触る（core の既存挙動は不変に保つ）。

---

## 7. ファイル境界（グローバル規則）

### 触れてよい（WS031 全体）
- 新規: `src/drivers/gpu/i915/vk/**`（全モジュール）、`src/drivers/gpu/i915/vk/linux/*.inc`（転記）。
- 追加配線: `platform/amd64/vmunix.mk`（vk source を `AMD64_I915_SOURCES` へ）、`Makefile`/`config/*`（vk のビルドスイッチ）、`plan/ws031/tests/**`（fixture・生成器・remote harness）。
- 各 Phase は**自分のモジュールの `.c`/`.h`＋その `linux/*.inc`＋その fixture**のみ。他モジュールの `.h` は include するが編集しない。

### 承認を得てから触れる（core 拡張 hook）
- `src/drivers/gpu/i915/{lrc.c,engine.c,i915.c,internal.h}` — RCS 3D 有効化と drv_gpu command 経路から vk 入口を呼ぶ結線のみ。p002（入口結線）と p007/p008（3D 有効化）の計画 doc に差分を明記し承認後に最小変更。

### 触れてはいけない（WS031 全体）
- `include/hal/**`、HAL 実装（HAL 責務・`hal.h` 変更は別承認）。
- `include/drivers/gpu.h`（UAPI layout/ioctl 番号は不変）。
- WS029 core の上記以外のロジック、他 driver、libc、kern の無関係部分。
- `userland/base/libvulkan/**`（原則無改造。capset 整合が必要なら p002 または p011 の計画で対象化し、その Phase のみが最小変更）。
- `.internal/`、無関係な作業ツリー。

---

## 8. Phase → モジュール委譲仕様

各 Phase の委譲要点。確定インタフェースと内部ガイドは各 Phase 計画 doc（§10 テンプレ）で詰める。

| Phase | Module | 実装インタフェース（大枠は §4） | 触れるファイル | 依存（前段 doc / core） | 受け入れ |
| --- | --- | --- | --- | --- | --- |
| p001 | 設計固め | vk 全 `*.h` の枠（空実装）、file layout、license 監査、capset 方針 | `vk/*.h`（枠）、`plan/ws031/**` | 本書 / WS029 | header がコンパイル、監査 exit 0、方針確定 |
| p002 | top+cmd | §4.1/4.2 | `vk/vk.*`,`vk/cmd.*`,`vk-internal.h`＋core 入口 hook（承認） | p001 / drv_gpu command 経路 | fixture で op dispatch・object table |
| p003 | res | §4.3 | `vk/res.*` | p002 / GEM,GGTT,PPGTT | fixture で buffer/image/sampler/descriptor 生成と state dword |
| p004 | spirv | §4.4 | `vk/spirv.*` | p001 / なし | fixture で vkdemo SPIR-V → IR |
| p005 | eu | §4.5 | `vk/eu.*`,`vk/linux/eu-*.inc` | p001 / なし | fixture で既知命令 → 既知 GEN byte 列 |
| p006 | compile | §4.6 | `vk/compile.*` | p004,p005 / GEM | fixture で IR → GEN、passthrough VS/定数 FS |
| p007 | pipe | §4.7 | `vk/pipe.*`,`vk/linux/3dstate-*.inc`＋core 3D hook（承認） | p003,p006 / lrc,engine | fixture で 3DSTATE dword 照合 |
| p008 | cmdbuf | §4.8 | `vk/cmdbuf.*`＋core submit hook（承認） | p002,p003,p007 / request,engine | fixture で command buffer → batch、submit |
| p009 | sync | §4.9 | `vk/sync.*` | p008 / seqno,complete,retire_waitq | fixture で fence/semaphore/query |
| p010 | wsi | §4.10 | `vk/wsi.*`,`vk/display.*`,`vk/linux/display-*.inc` | p003 / ggtt,uncore,WS029 f003 | fixture で state、実機で modeset/flip |
| p011 | 統合 | 増分A→B→C の結線、config、必要なら capset 整合 | `plan/ws031/tests/**`,`config/*`,（libvulkan 最小） | p002–p010 | 実機で vkdemo 描画・oracle 照合 |
| p012 | review | なし（レビュー） | doc のみ | 全 Phase | 静的解析0、規約確認、回帰 |

---

## 9. 試験の枠

- host fixture（実機不要）: 各モジュールを単体で。decoder は既知 wire、spirv は vkdemo の `.spv`、eu は既知命令→既知 byte、pipe は既知入力→既知 dword、compile は IR→GEN、cmdbuf は記録→batch。WS029 と同じく通常＋ASan/UBSan。i915 core は WS029 の fixture stub を流用・拡張。
- 実機（Latitude 5330 ネイティブ）: 増分A/B/C の描画と scanout を目視＋独立 oracle。ホスト（QEMU/Venus/ANV）を使わない。
- build: `make -j16` i915 config、warning 0、`vmunix check` PASS、GPU なし build で symbol 0。

---

## 10. Phase 計画 doc テンプレート（`plan/ws031/phaseNNN/phase.md`）

```
# WS031 pNNN 計画: <module> — <一行責務>

## Module と所有ファイル
実装対象の .c/.h/.inc/fixture を列挙。

## 実装する公開インタフェース（規約・正本）
このモジュールの *.h の確定内容（関数シグネチャ・目的・引数/返り値・エラー）。
本書 §4 の大枠を確定する。以後、後段 Phase はこの節を参照する。

## 内部関数構成ガイド
.c 内の想定関数分割（あくまでガイド。委譲先は内部を自由に変えてよい）。

## 依存
- 参照する前段 Phase doc（その確定インタフェース）。
- 呼んでよい WS029 core 関数（§6 の範囲）。
- 承認が要る core 拡張 hook があれば差分を明記。

## 触れるファイル / 触れないファイル
このモジュール分を §7 に沿って具体列挙。

## 受け入れ条件と試験
host fixture の項目、build、（該当すれば）実機。

## 見積・制限
```

各 Phase の計画時にこの doc を作り「あとはコーディングするだけ」の状態にしてから委譲する。実装後は同 Phase の `results.md` に確定インタフェースと結果を記録し、後段はそれを正本として参照する。

---

## 補遺A: drv_gpu op 統合面の確定（p002 実行時の設計調整）

p002 着手時に判明: WS029 の i915 backend は libvulkan が要求する `get_capset`/`blob_create`/`resource_map`/`present` を実装しておらず、capabilities も `GPU_CAP_CAPSET|BLOB|MAPPING` を持たない。したがって §6 の「最小 hook」ではなく、**vk 実行器（top）が libvulkan 必要な drv_gpu op 一式を供給し、i915 の drv_gpu_ops テーブルと capabilities を拡張する**のが正しい統合面。

確定（§4.1 top と §6/§7 を上書き）:
- top が次の drv_gpu op 実装（`drv_i915_vk_*`）を供給する: `get_capset`（Venus 互換 capset、libvulkan の 156B 閾値を満たす）、`blob_create`（Vulkan の memory/blob を GEM object へ）、`resource_map`（host-visible の CPU view）、`command`（wire を decoder へ）、`present`（wsi、p010）。
- `src/drivers/gpu/i915/i915.c` の `i915_publish` の ops テーブルにこれらを配線し、`I915_CAPABILITIES` に `GPU_CAP_CAPSET|GPU_CAP_BLOB|GPU_CAP_MAPPING` を足す。これは**定義された core 統合面**（arbitrary な core 改変ではない）。native stream client（gpu-i915-test）は既存経路のまま（command の内容で vk/native を判別、または capset 照会の有無で判定）。
- この配線は p002 が行う（承認済み core hook として本 doc に明記）。3D 有効化 hook（lrc/engine）は従来どおり p007/p008。
