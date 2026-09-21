# WS031 i915リファクタリング案 — モジュール契約と関数移動

作成日: 2026-09-21。状態: **共同レビュー用の草案**。
調査基準: centris `/home/awe/zedBSD-gpu`、commit
`7e7ff337c7f145e5fd79e4daaafa05a1cd4a4bcc`（調査開始時clean）。

ユーザー方針: Linux互換層という区分を設計の中心に置かず、一つのi915ドライバとして整理する。
描画、ディスプレイ制御、コンパイラは分ける。
本書のファイル名・関数名・署名・統合判断は、その方針を具体化した**提案**であり、実装済みAPIではない。
ソース移動、動作変更、Phase clearance、Queue更新は行っていない。

背景は[レポート35](handover/expert-reports/ws031-report-35.md)、
実測は[結果台帳 E-127以降](results-ws031.md)。
現行WS/Queueの古い状態表を本書で上書きしない。
これはローカルのレビュー資料であり、GitHub上の合意済み計画・Master・Issue・Projectの更新ではない。

敵対的レビュー反映版: 初稿をそのまま移動手順にすると、起動runnerの除去、表示側blitのVM欠落、
wire transportの不完全な分離、状態・定数の脱落が起こり得た。
[指摘・libvulkanワークフロー照合](i915-refactoring-review.md)と
[全378ファイルの保全台帳](i915-refactoring-assets.md)を追加した。
**責務分割は実現可能な方向だが、全関数のexport/署名が確定した実装可能仕様ではない。**
特にI候補、生成display glue、停止契約は実装前に閉じる必要がある。

## 1. 資料の読み方とレビュー範囲

| 資料・節 | 答える問い |
| --- | --- |
| 本書 §2 | どのファイル・型が何を所有するか |
| 本書 §3 | どのモジュールのどのヘッダに、どの関数を宣言するか |
| 本書 §4 | エラー、参照寿命、GPU完了、表示の契約をどう渡すか |
| 本書 §5 | 重要な現行関数をどこへ移し、何を統合・分割するか |
| [関数移動台帳](i915-refactoring-functions.md) | 現行関数名から移動先を検索する。ヘッダinline、生成`.inc`、試験も含む |
| [敵対的レビュー](i915-refactoring-review.md) | どこで起動・描画・共有・停止が破綻するか。現行制限と設計欠陥を区別 |
| [全ファイル保全台帳](i915-refactoring-assets.md) | 関数を持つファイル内の状態・定数も含め、元ファイル全体を移行の会計に残す |
| 本書 §6–§8 | データ・試験・生成器の扱い、未決事項、移行確認の順序 |

以下のパスは、特記しない限り `src/drivers/gpu/i915/` 相対。
ソースの行番号は上記commitに対する位置であり、移動後は更新する。
現行関数名と提案名は区別する。新設する上位入口は `drv_i915_` 接頭辞とする。

全関数の機械抽出はGNU Emacs ctagsのC parserと、宣言後に関数本体が続くことの検査を組み合わせた。
これはpreprocess/link後のsymbol一覧ではない。条件付き実装を含み、関数マクロの展開結果や間接呼出しの完全性は証明しない。
別紙の一括配置案は最終の意味論レビュー済み差分ではなく、移行対象の取りこぼしを防ぐ索引である。
特に重複する旧実装の削除は、呼出元・試験・実際の処理の照合後に判断する。

## 2. 目標構成と所有権

### 2.1 ディレクトリ

```text
i915/
  i915.c, i915.h                 登録、公開、デバイスの根
  device.c, device-info.c        readinessを待つ起動・停止、機種情報
  session.c, resource.c          sessionとdrv_gpu資源操作
  command.c, job.c               native/Vulkan入口、共通job契約への接続
  memory.c, ggtt.c, ppgtt.c       backing、参照、GPUアドレス空間
  mmio.c, irq.c, power.c          MMIO、割込み、共通電源
  engine.c, context.c, request.c 実行機構と完了
  reset.c, workarounds.c         回復、workaround
  firmware.c, sync.c, trace.c    firmware取得、時間・同期、診断
  render/                       Vulkan executor、state/batch、draw/blit
    instance.c, transport.c     instance/device/queue命令、共有reply/external stream
  display/                      modeset、出力、scanout、flip
  compiler/                     SPIR-V、IR、EU codegen
  data/                         出典付き定数・表・生成codec・firmware
  tests/                        host/kernel試験、fake HW、参照資材
```

前回の構成案を関数単位で調べ、`command.c`・`job.c`・`sync.c`・`trace.c`、
`render/batch.c`・`math.c`・`wsi.c`、`display/color.c`・`diagnostics.c`を補った。
これは既存処理の置き場所であり、機能追加の約束ではない。
`render/wsi.c`は旧Vulkan WSIコードの照合先。現在のdirect-displayはlibvulkanからdrv_gpu display opsへ進むので、
旧WSI経路を新たに必須にする意味はない。

### 2.2 状態の所属

| 型・状態（提案） | 定義・所有 | 主な参照者 | 統合する現行状態 |
| --- | --- | --- | --- |
| `struct i915_device` | `i915.h`、`device.c`が生成・停止 | 各部は所属deviceを明示して参照 | legacy device、parity probeのdevice全体の状態 |
| `struct i915_device_info` | `device-info.h`、読取専用 | device、compiler条件作成、display | PCI世代判定、display version、固定thread数等 |
| `struct i915_session` | `session.h`、session.c所有 | resource、command、render | open単位のcontext、object表、予約、表示所有者 |
| `struct i915_memory` | `memory.h`、memory.c所有 | GGTT/PPGTT、resource、render、scanout | GEM backing、parity objectの実メモリ、export保持 |
| `struct i915_vm` / `i915_ggtt` | `ppgtt.h` / `ggtt.h` | context、resource、scanout | 二系統のpage table・範囲allocator |
| `struct i915_context` / `i915_request` | `context.h` / `request.h` | command、job、render | legacyとparityのLRC/ring/request、shim context |
| `struct i915_render_session` | `render/internal.h` | render内部 | Vulkan object表、wire reply状態、gfx session |
| `struct i915_blit_context` | `render/blit.c`（不透明型はblit.h） | display/present、render/command | rect用state/batch、shader保持。Vulkan object表を持たない |
| `struct i915_ops_storage` | `ops.h`、device寿命 | 各bindは自分のfieldだけ初期化 | top-level opsとdisplay等のsub-ops実体 |
| `struct i915_display` / `i915_pipe` | `display/internal.h`、deviceが所有 | display内部 | static `rd`、選択中screen、modeset pool、resident表示状態 |
| `struct i915_scanout` | `display/scanout.h` | displayのpresent/plane | buffer、mapping、scanout保持・停止未確認の保持 |
| `struct i915_shader_ir` / `i915_shader_binary` | `compiler/ir.h` / `compiler/compiler.h` | compiler、render pipeline | 現在の`i915_vk_shader_*` |

GGTTのGT予約域、表示予約域、firmware framebufferの保持を消して一括再初期化しない。
一つの管理主体に統合しても、予約とcache属性の意味を保持する。
PPGTTのCPU物理アドレスとdevice-visible DMAアドレスも型統合の際に同一視しない。

sessionは**GPU nodeのopen単位**であり、VkDevice単位ではない。
一つのlibvulkan contextに複数VkDevice/queueが属し得る。object keyは
`(render_session, kind, wire_id)`、memory/blobの照合も同じsession内とする。
別の表示openへ共有するのはbackingと画像descriptorであって、Vulkan object表ではない。
現行device-global表からこのモデルへの変更は、ファイル移動とは別の機能差分として検証する。

### 2.3 includeと呼出しの向き

| 呼出側 | 利用できる境界 | 持ち込まないもの |
| --- | --- | --- |
| PCI/kernel外部 | `include/drivers/i915.h` | driver内部構造体 |
| GPU core | 既存`struct drv_gpu_ops`と各ops | i915固有のwire/object型 |
| device/session/resource/command/job | 各共通ヘッダ、`render/render.h`、`display/display.h` | render/displayの内部構造の直接操作 |
| render | memory/VM/request、compiler公開契約 | panel、DP、LCD callback |
| display | memory/GGTT/request、`render/blit.h` | Vulkan handle、descriptor、wire reader |
| compiler | compiler内型、必要最小限のallocator/整数処理 | device MMIO、session、display、実行待ち |
| tests | 本番の各入口、試験用fixture | 本番からtestsを呼ぶ逆依存 |

`render/internal.h`と`display/internal.h`は各ディレクトリ内専用。
全処理をrootの`i915.h`へ集めず、前方宣言と所有ヘッダで依存を限定する。

`display/internal.h`にも全compat headerを連結しない。DP固有は`dp-internal.h`、
modeset固有は`modeset-internal.h`へ配分する。watermark/takeover/hotplug/opregionの
同名iterator macroはそれぞれのprivate header/TUに隔離してから、意味別の明示ループへ置換する。
現`for_each_intel_crtc_in_pipe_mask`はmodesetで空走査、watermarkで選択pipe、
takeoverでregistry走査という別処理であり、共通macro一本への統合は禁止する。
これは恒久的なLinux互換層を残す方針ではなく、消す前に意味を保存する移行条件である。
private header同士を同じTUへincludeすれば衝突は再発する。
同名inline/型は意味・layoutを照合して単一定義へ統合するかowner固有名へ改名し、
両方が必要な呼出元のpreprocess結果を検査する。配置先を分けただけで解決済みとはしない。

## 3. ヘッダに宣言する関数

### 3.1 公開範囲の凡例

| 区分 | 宣言場所 | 用途 |
| --- | --- | --- |
| E | `include/drivers/i915.h` | ドライバ外への公開。登録とkernel readiness通知 |
| H | 下表の所有ヘッダ | ドライバ内の明示的なモジュール間契約 |
| I | 別紙の所有ヘッダ候補 | 移行途中の内部関数。現名で索引化し、統合先Hへ吸収するか内部名を確定する |
| O | 関数自身のヘッダ宣言なし | static ops callback。所有ファイル内のops表またはbind関数を通じて登録 |
| S | ヘッダ宣言なし | 同一translation unitだけで使うstatic補助関数 |
| T | `tests/`内だけ | 試験入口・model・参照コード |

H表はレビュー対象の入口案であり、全署名の確定を意味しない。Iは既存の外部linkageを無根拠にstatic化しないための候補であり、
Linux/parityの公開APIを丸ごと永続化する提案ではない。
IからHへの吸収やstatic化を決めたら、別紙の行へ対応を追記する。
下表の関数名にワイルドカードは用いず、括弧を省略したものも個別の関数名として列挙する。

### 3.2 ドライバ共通部

| 所有ソース | 公開ヘッダ | Hとして宣言する関数名（提案） | 呼出側・役割 |
| --- | --- | --- | --- |
| `i915.c` | `include/drivers/i915.h`（E） | `drv_i915_pci_driver_register` | PCI backend登録。既存名維持 |
| `device.c` | `include/drivers/i915.h`（E） | `drv_i915_runtime_ready` | 現`drv_i915_parity_runner_start`の本番readiness責務。kernelから通知、二重起動防止 |
| `device.c` | `device.h` | `drv_i915_device_start`, `drv_i915_device_stop`, `drv_i915_device_schedule_start` | attach/detach。登録とreadinessの到着順に依存しない遅延起動 |
| `device-info.c` | `device-info.h` | `drv_i915_device_info_lookup` | PCI IDから能力・世代情報を取得 |
| `session.c` | `session.h` | `drv_i915_session_bind_ops`, `drv_i915_session_object_lookup` | open/close/info登録、session所有資源の照合 |
| `resource.c` | `resource.h` | `drv_i915_resource_bind_ops`, `drv_i915_resource_lookup_id`, `drv_i915_resource_view_put` | callback設定、wire resource_idからsession所属の保持付きviewを取得/解放 |
| `command.c` | `command.h` | `drv_i915_command_bind_ops`, `drv_i915_stream_parse` | command/capset callback、既存native streamの検査 |
| `job.c` | `job.h` | `drv_i915_job_bind_ops` | reserve/commit/cancel/capacityの既存契約を実装 |
| `memory.c` | `memory.h` | `drv_i915_memory_create`, `drv_i915_memory_get`, `drv_i915_memory_put`, `drv_i915_memory_cpu`, `drv_i915_memory_page_dma` | backingの取得・参照・CPU/DMA view |
| `ggtt.c` | `ggtt.h` | `drv_i915_ggtt_start`, `drv_i915_ggtt_stop`, `drv_i915_ggtt_alloc`, `drv_i915_ggtt_free`, `drv_i915_ggtt_insert`, `drv_i915_ggtt_clear` | 共通GPU VAとmapping。予約域・firmware保持を含む |
| `ppgtt.c` | `ppgtt.h` | `drv_i915_vm_create`, `drv_i915_vm_destroy`, `drv_i915_vm_alloc`, `drv_i915_vm_bind`, `drv_i915_vm_unbind`, `drv_i915_vm_invalidate` | context側VA。cache属性はbind条件で明示 |
| `mmio.c` | `mmio.h` | `drv_i915_read32`, `drv_i915_write32`, `drv_i915_wait32`, `drv_i915_forcewake_get`, `drv_i915_forcewake_put` | 範囲・forcewake・timeoutを保持したMMIO |
| `irq.c` | `irq.h` | `drv_i915_irq_start`, `drv_i915_irq_stop` | IRQ登録・解除。handler自体はO |
| `power.c` | `power.h` | `drv_i915_power_init`, `drv_i915_power_fini`, `drv_i915_pcode_read`, `drv_i915_pcode_write` | device共通の電源/PCODE。display powerとは別所有 |
| `engine.c` | `engine.h` | `drv_i915_engines_start`, `drv_i915_engines_stop`, `drv_i915_engine_for_timeline`, `drv_i915_engine_interrupt` | 実行機構の開始・停止・選択・完了処理 |
| `context.c` | `context.h` | `drv_i915_context_create`, `drv_i915_context_destroy` | VMとengineに属するLRC/ringを管理 |
| `request.c` | `request.h` | `drv_i915_request_submit`, `drv_i915_request_wait`, `drv_i915_request_put`, `drv_i915_request_drain` | 実batch投入、完了、資源保持。同期helperもここに集約 |
| `reset.c` | `reset.h` | `drv_i915_recovery_bind_ops`, `drv_i915_engine_reset`, `drv_i915_gt_reset` | recovery登録と実停止確認。未実装を成功にしない |
| `workarounds.c` | `workarounds.h` | `drv_i915_workarounds_init`, `drv_i915_workarounds_apply`, `drv_i915_workarounds_verify` | 機種条件・初期化順に従うworkaround |
| `firmware.c` | `firmware.h` | `drv_i915_firmware_get`, `drv_i915_firmware_put` | firmware bytesと寿命の提供。DMC制御はdisplay |
| `sync.c` | `sync.h` | `drv_i915_wait_until`, `drv_i915_delay_us`, `drv_i915_error_from_reference` | 有限待機・時間変換・移行中のerrno変換 |
| `trace.c` | `trace.h` | `drv_i915_trace_record`, `drv_i915_trace_error` | deviceに属する診断。判定をログ容量に依存させない |

bind関数は `void drv_i915_<owner>_bind_ops(struct i915_ops_storage *storage)` とする案へ訂正する。
`ops.h`のstorageは`drv_gpu_ops`に加え、command/job/recovery/share/display/scanoutのsub-ops実体を持つ。
`i915.c`がdevice寿命を持つstorageを未公開状態でゼロ初期化し、各bindは担当fieldだけを設定する。
最後にtop-levelのconst sub-opsポインタをそれぞれの実体へ接続し、version/size/capabilityを検証して登録する。
公開後は不変。stack一時表、const表のcastによる書換え、別deviceとの可変storage共有は禁止する。
部分失敗では未登録storageを回収し、unregisterがEBUSYならstorageもdeviceも保持する。
capabilityはdeviceが実提供できるcallback/機能と照合し、bindだけで自動的に全機能を宣言しない。
これは既存`drv_gpu_ops`を使う内部構成で、GPU core/UAPIへの新しいservice API追加ではない。

### 3.3 描画部

| 所有ソース | 公開ヘッダ | Hとして宣言する関数名（提案） | 呼出側・役割 |
| --- | --- | --- | --- |
| `render/vulkan.c` | `render/render.h` | `drv_i915_render_attach`, `drv_i915_render_detach`, `drv_i915_render_open`, `drv_i915_render_close`, `drv_i915_render_execute`, `drv_i915_render_get_capset`, `drv_i915_render_blob_attach`, `drv_i915_render_blob_detach` | 共通部から使うexecutor入口 |
| `render/instance.c` | `render/instance.h` | `drv_i915_render_instance_dispatch` | instance/physical-device/device/queueのwire操作。vulkan.cへdispatchの逆呼出しを戻さない |
| `render/transport.c` | `render/transport.h` | `drv_i915_render_transport_execute`, `drv_i915_render_transport_dispatch` | 178/179/180/137、reply view保持、external streamのcopyと境界検査 |
| `render/dispatch.c` | `render/dispatch.h` | `drv_i915_render_dispatch` | opcodeの所有先を一意に選択、reply制御 |
| `render/codec.c` | `render/codec.h` | `drv_i915_wire_read_u32`, `drv_i915_wire_read_u64`, `drv_i915_wire_read_array`, `drv_i915_wire_read_bytes`, `drv_i915_wire_reply_u32`, `drv_i915_wire_reply_u64`, `drv_i915_wire_reply_bytes`, `drv_i915_wire_create_tail`, `drv_i915_wire_create_reply`, `drv_i915_wire_result` | wire reader/writer、共通作成reply。型別codecはdata参照 |
| `render/object.c` | `render/object.h` | `drv_i915_object_table_create`, `drv_i915_object_table_destroy`, `drv_i915_object_insert`, `drv_i915_object_lookup`, `drv_i915_object_remove`, `drv_i915_object_destroy_dispatch` | Vulkan objectのsession所属、型検査、破棄 |
| `render/memory.c` | `render/memory.h` | `drv_i915_render_memory_dispatch`, `drv_i915_render_memory_cpu`, `drv_i915_render_memory_va`, `drv_i915_render_memory_blob_attach`, `drv_i915_render_memory_blob_detach`, `drv_i915_render_buffer_alloc` | memory/buffer処理。blob通知はrender.hの入口から委譲 |
| `render/image.c` | `render/image.h` | `drv_i915_render_image_dispatch`, `drv_i915_render_image_surface` | image/view/sampler、描画・転送用surfaceへの変換 |
| `render/descriptor.c` | `render/descriptor.h` | `drv_i915_render_descriptor_dispatch` | descriptor/layout/pool更新 |
| `render/pipeline.c` | `render/pipeline.h` | `drv_i915_render_pipeline_dispatch`, `drv_i915_render_pipeline_prepare`, `drv_i915_render_pipeline_release` | shader module・pipeline、compileとcode配置 |
| `render/render-pass.c` | `render/render-pass.h` | `drv_i915_render_pass_dispatch` | pass/framebufferの作成・検査 |
| `render/command.c` | `render/command.h` | `drv_i915_render_command_dispatch`, `drv_i915_render_command_execute` | pool/buffer記録、submit時の操作実行 |
| `render/state.c` | `render/state.h` | `drv_i915_render_state_emit`, `drv_i915_render_surface_write`, `drv_i915_render_sampler_write` | state packet、surface/samplerのpacking |
| `render/draw.c` | `render/draw.h` | `drv_i915_render_draw`, `drv_i915_render_draw_session_close` | draw batch生成とrequestへの投入、作業資源回収 |
| `render/blit.c` | `render/blit.h` | `drv_i915_blit_context_create`, `drv_i915_blit_context_destroy`, `drv_i915_blit_prepare`, `drv_i915_blit_build`, `drv_i915_blit_submit` | Vulkan型を含まないsurface/rect契約。VM/contextを指定した作業資源。displayからも使用 |
| `render/batch.c` | `render/batch.h` | `drv_i915_batch_emit`, `drv_i915_batch_zero`, `drv_i915_batch_words`, `drv_i915_batch_pointer`, `drv_i915_batch_pipe_control` | draw/state/blit共通の有限長packet出力 |
| `render/math.c` | `render/math.h` | `drv_i915_float_half`, `drv_i915_float_add`, `drv_i915_float_sub`, `drv_i915_float_from_u32`, `drv_i915_float_ratio` | 現行soft-float helper。kernelでFPUを使わない |
| `render/sync.c` | `render/sync.h` | `drv_i915_render_sync_dispatch`, `drv_i915_render_sync_complete` | Vulkan同期objectと実request完了を対応付ける |
| `render/wsi.c` | `render/wsi.h` | **完成形の追加公開なし（R4）** | 旧WSI関数の照合先。別紙では内部Iとして追跡 |

`vkc`生成関数147件は現状のようにstatic生成物として各必要translation unitからincludeする案を維持する。
`data/vulkan-codec.inc`へ移すだけで147件をglobal exportしない。
encoder/decoderの入力はlibvulkanのcodecが正本であり、generatorの出力先・includeも一緒に更新する。

### 3.4 ディスプレイ部

| 所有ソース | 公開ヘッダ | Hとして宣言する関数名（提案） | 呼出側・役割 |
| --- | --- | --- | --- |
| `display/display.c` | `display/display.h` | `drv_i915_display_init`, `drv_i915_display_fini`, `drv_i915_display_bind_ops`, `drv_i915_display_session_close`, `drv_i915_display_interrupt` | 共通部から使う表示入口 |
| `display/state.c` | `display/state.h` | `drv_i915_display_state_check`, `drv_i915_display_panel_mode`, `drv_i915_display_panel_size_mm` | mode・link・plane条件の検査と取得 |
| `display/modeset.c` | `display/modeset.h` | `drv_i915_modeset_enable`, `drv_i915_modeset_disable` | 点灯/停止。serving loopをcallbackとして抱えない完成形 |
| `display/pipe.c` | `display/pipe.h` | `drv_i915_pipe_configure`, `drv_i915_pipe_enable`, `drv_i915_pipe_disable`, `drv_i915_pipe_readout` | pipe/transcoderの設定・読取り |
| `display/plane.c` | `display/plane.h` | `drv_i915_plane_update`, `drv_i915_plane_disable`, `drv_i915_plane_readout` | scanoutレジスタの実設定 |
| `display/scanout.c` | `display/scanout.h` | `drv_i915_scanout_create`, `drv_i915_scanout_get`, `drv_i915_scanout_put`, `drv_i915_scanout_map`, `drv_i915_scanout_unmap`, `drv_i915_scanout_constraints` | buffer、GGTT/PPGTT mapping、保持、配置制約 |
| `display/present.c` | `display/present.h` | `drv_i915_present_bind_ops`, `drv_i915_present`, `drv_i915_present_wait`, `drv_i915_present_release` | copy/blit後のflip、完了とlease終了 |
| `display/vblank.c` | `display/vblank.h` | `drv_i915_vblank_get`, `drv_i915_vblank_put`, `drv_i915_vblank_wait`, `drv_i915_vblank_interrupt` | vblank参照、待機、通知 |
| `display/hotplug.c` | `display/hotplug.h` | `drv_i915_hotplug_init`, `drv_i915_hotplug_interrupt`, `drv_i915_hotplug_poll` | 接続検出。現行未接続機能の完成を意味しない |
| `display/power.c` | `display/power.h` | `drv_i915_display_power_init`, `drv_i915_display_power_get`, `drv_i915_display_power_put`, `drv_i915_display_power_fini` | power well/domain、wakeref |
| `display/clock.c` | `display/clock.h` | `drv_i915_display_clock_compute`, `drv_i915_display_clock_enable`, `drv_i915_display_clock_disable` | CDCLKと共有DPLLの計算・設定 |
| `display/watermark.c` | `display/watermark.h` | `drv_i915_watermark_compute`, `drv_i915_watermark_apply` | 帯域、DBUF/DDB、watermark |
| `display/ddi.c` | `display/ddi.h` | `drv_i915_ddi_enable`, `drv_i915_ddi_disable` | 出力ポートsequence |
| `display/phy.c` | `display/phy.h` | `drv_i915_phy_init`, `drv_i915_phy_configure`, `drv_i915_phy_fini` | PHYとbuffer translation |
| `display/dp.c` | `display/dp.h` | `drv_i915_dp_probe`, `drv_i915_dp_train`, `drv_i915_dp_link_status` | DP/eDP linkとsink状態 |
| `display/aux.c` | `display/aux.h` | `drv_i915_aux_transfer`, `drv_i915_dpcd_read`, `drv_i915_dpcd_write`, `drv_i915_dpcd_read_caps` | AUX/I2C-over-AUX、DPCD |
| `display/hdmi.c` | `display/hdmi.h` | `drv_i915_hdmi_detect`, `drv_i915_hdmi_mode_check`, `drv_i915_hdmi_configure` | HDMIのmode/出力制約 |
| `display/gmbus.c` | `display/gmbus.h` | `drv_i915_gmbus_transfer` | GMBUS転送 |
| `display/panel.c` | `display/panel.h` | `drv_i915_panel_power_on`, `drv_i915_panel_power_off`, `drv_i915_panel_backlight_set` | PPS、遅延順序、backlight |
| `display/edid.c` | `display/edid.h` | `drv_i915_edid_read`, `drv_i915_edid_modes` | EDIDとmode列挙 |
| `display/vbt.c` | `display/vbt.h` | `drv_i915_vbt_parse`, `drv_i915_vbt_release` | VBTからdevice-owned設定を作る |
| `display/opregion.c` | `display/opregion.h` | `drv_i915_opregion_init`, `drv_i915_opregion_fini`, `drv_i915_opregion_notify` | firmware情報交換 |
| `display/dmc.c` | `display/dmc.h` | `drv_i915_dmc_load`, `drv_i915_dmc_enable`, `drv_i915_dmc_disable` | DMC処理、機種別選択 |
| `display/takeover.c` | `display/takeover.h` | `drv_i915_display_readout`, `drv_i915_display_takeover` | firmwareのactive表示を読取り引継ぐ |
| `display/color.c` | `display/color.h` | `drv_i915_color_check`, `drv_i915_color_apply` | LUT・色設定 |
| `display/diagnostics.c` | `display/diagnostics.h` | `drv_i915_display_snapshot`, `drv_i915_display_trace` | 状態診断。試験scenarioはtests側 |

`display_bind_ops`はstorage内のquery/mode/claim/eventsとscanout sub-opsを組み立て、
`present_bind_ops`を通じてpresent/wait/releaseを設定する。
共有するdisplay sub-opsのstorageはdevice-ownedとし、公開後の差替えは行わない。
rd系callbackは所属ファイルでstaticのまま表へ接続する。現在のdisplay opsを変更せず実装できる内部構成として検討する。

### 3.5 コンパイラ部

| 所有ソース | 公開ヘッダ | 宣言する関数名（提案） | 公開先 |
| --- | --- | --- | --- |
| `compiler/spirv.c` | `compiler/compiler.h` | `drv_i915_shader_parse`, `drv_i915_shader_ir_free` | render pipeline |
| `compiler/compile.c` | `compiler/compiler.h` | `drv_i915_shader_compile`, `drv_i915_shader_binary_free` | render pipeline、blitの内部shader生成 |
| `compiler/eu.c` | `compiler/eu.h` | `drv_i915_eu_init`, `drv_i915_eu_free`, `drv_i915_eu_data`, `drv_i915_eu_grf`, `drv_i915_eu_grf_ud`, `drv_i915_eu_grf_scalar`, `drv_i915_eu_negate`, `drv_i915_eu_imm_f`, `drv_i915_eu_imm_d`, `drv_i915_eu_null`, `drv_i915_eu_mov`, `drv_i915_eu_alu2`, `drv_i915_eu_mad`, `drv_i915_eu_math`, `drv_i915_eu_send`, `drv_i915_eu_nop` | compiler内部と単体試験のみ |
| 型定義のみ | `compiler/ir.h` | 関数宣言なし | scalar IR、stage、入出力宣言 |

`compiler.h`は`vk-internal.h`や`i915.h`をincludeしない。
parse/compileにはtarget infoとoptionsを値または読取専用構造体で渡し、
コードをGPU memoryへ配置する責任はrender pipelineに残す。

## 4. 境界の具体的な署名・寿命案

以下の型は提案する契約名であり、現行ヘッダへそのまま貼れる完成コードではない。
既存ops callbackの引数・戻り値は既存`include/drivers/gpu*.h`の契約を維持する。

| 関数宣言案 | 所有権・戻り値 |
| --- | --- |
| `int drv_i915_device_start(struct i915_device *device);` | 成功0、失敗errno。部分初期化の回収はdevice側 |
| `int drv_i915_device_stop(struct i915_device *device);` | 停止未確認なら失敗し、DMA対象を保持。unpublishのEBUSYを成功へ変換しない |
| `void drv_i915_runtime_ready(void);` / `void drv_i915_device_schedule_start(struct i915_device *device);` | kernel readinessと登録を各々記録し、両方成立時だけ本番workerを一度開始。旧runnerの試験実行とは分離 |
| `int drv_i915_resource_lookup_id(struct i915_session *session, uint32_t resource_id, uint64_t offset, uint64_t bytes, struct i915_resource_view **out);` | wire namespace内の範囲検査と参照取得。UAPI handleやVk wire_idを代入しない。view_putまで保持 |
| `int drv_i915_memory_create(struct i915_device *device, const struct i915_memory_desc *desc, struct i915_memory **out);` | 成功時に参照1を返す。失敗時はoutを公開しない |
| `void drv_i915_memory_get(struct i915_memory *memory);` / `void drv_i915_memory_put(struct i915_memory *memory);` | backing寿命の参照。mapping、request、scanoutの保持を別途数える |
| `int drv_i915_vm_bind(struct i915_vm *vm, struct i915_memory *memory, const struct i915_mapping_desc *desc, struct i915_mapping **out);` | cache/PAT、範囲、権限を明示。mappingはbackingを保持 |
| `int drv_i915_vm_unbind(struct i915_mapping *mapping);` | GPU利用終了が前提。確認できない場合は保持して失敗 |
| `int drv_i915_request_submit(struct i915_context *context, const struct i915_batch_desc *batch, struct i915_request **out);` | 受理と実完了を区別。GPU完了までbatch/state/shader資源を保持 |
| `int drv_i915_request_wait(struct i915_request *request, uint64_t deadline);` | 絶対monotonic期限。timeoutは停止完了の証明ではない |
| `void drv_i915_request_put(struct i915_request *request);` | caller参照を解放。pending実行資源の解放許可とは別 |
| `int drv_i915_render_execute(struct i915_render_session *session, const void *bytes, size_t size, struct i915_decode_result *out);` | transportを含む一取引。outは未実行/副作用あり/返信完了を区別し、command側のexactly-once通知に使う。VkResultはwire、errnoはtransport失敗 |
| `int drv_i915_blit_context_create(struct i915_context *context, struct i915_blit_context **out);` | contextのVMにstate/batch/shaderを配置。表示openにも作れる。GPU停止確認までcontextとmappingを保持 |
| `int drv_i915_blit_context_destroy(struct i915_blit_context *blit);` | pendingならEBUSYで保持。Vulkan sessionのcloseとは独立 |
| `int drv_i915_blit_submit(struct i915_blit_context *blit, const struct i915_blit_desc *desc, struct i915_request **out);` | descは同じVMにbind済みの保持付きsurfaceと矩形。Vulkan handleを受けない |
| `int drv_i915_present(struct i915_display *display, struct i915_session *owner, const struct i915_present_desc *desc);` | 転送完了後にplane更新、flip完了後に旧buffer参照を解放 |
| `int drv_i915_modeset_disable(struct i915_display *display);` | 実停止確認後にpower/bufferを解放。失敗時は表示保持 |
| `int drv_i915_shader_parse(const uint32_t *words, size_t word_count, const struct i915_compile_options *options, struct i915_shader_ir **out, struct i915_compile_diagnostic *diagnostic);` | shader bytesからIR。未対応命令は明示拒否 |
| `int drv_i915_shader_compile(const struct i915_shader_ir *ir, const struct i915_shader_target *target, struct i915_shader_binary **out, struct i915_compile_diagnostic *diagnostic);` | codeとpayload/GRF/SIMD/URB等のmetadataを返す。GPUには触れない |

`i915_batch_desc`と`i915_blit_desc`は裸のGPU VAだけを持たず、対応する資源参照と範囲を保持できる形にする。
blob_attach/detachとobject操作の第一引数は`struct i915_render_session *`とし、
現行のdevice-global検索をそのまま残さない。これは別processのwire_id衝突を防ぐ機能変更である。
Vulkan VkResult、zedBSD errno、現行移植コードの負のLinux errnoを同じintの意味として混用しない。
`error_from_reference`は移行期の変換点であり、最終的には各内部処理のerrno規約を統一して廃止候補とする。

現行正常系の同期実行を維持したまま、`submit → wait → put`へ切り分けられる。
本書の署名案だけで非同期GPU実行やrecoveryが完成したことにはならない。

### 4.1 wire・job・表示の必須契約

| 境界 | 必須の順序・状態 | 移行時に禁止する短絡 |
| --- | --- | --- |
| transport | session resource_idでreplyを保持 → 178 select → inlineまたは180 external stream → 179 seek → 137の20-byte trailer → decoder通知 | int成功だけ返す、4096-byte固定replyへの退行、通知だけでreplyを有効扱い |
| external stream | offset/length/dependency/ネスト上限を検査、可変user mappingを私有copyしてdecode。arenaは命令単位、保存するpointerはdeep-copy | unknown opcodeのpayload長を推測して次へ進む、arena pointerをobjectに残す |
| job | CAPACITY → RESERVE → native wire submit → COMMIT marker → 実GPU完了。失敗時CANCEL/FAULT | decoder trailer、ioctl受理、予約だけをVkFence成功にする |
| timeline | 0はdecoder、residentで公開するqueue slot 1はRCS0。engine enumと別対応表 | SHIMマクロを消すだけでslot 1を旧BCS0へ戻す |
| display | 別openへbacking import → **表示側VM**でsrcとpanel dstをbind → blit → GPU完了 → flip → 表示完了 | 描画側VAの再利用、表示openにVkDeviceがある前提、flip成功で全backing解放 |
| close/stop | admission停止、job/callback/同期blitをdrain、scanout停止、mapping/export保持を照合 → destroy | pending_requestsだけでshimの別sync queueまで停止したとみなす、timeout/EBUSYを解放許可とする |

`request.c`はGPU requestだけを処理する。shimのBATCH/PRESENT/RELEASE共用queueを機械移動せず、
display側workerがrequestへsubmit/waitする構成とし、request workerからdisplayへ呼び戻さない。
LCD enableが返っても`lk`、scanout、forcewake/power、retained stateはdevice/pipe寿命で保持する。
GPU完了を処理するworker自身が、そのworkerの仕事を同期waitする設計は禁止する。

readinessの「両方成立」は新設計の要件。現`try_launch`はreadyのみを検査し、
GPU未登録ならSYNC_ONLY試験を開始できる。既存コードが順不同の本番起動を保証するとは扱わず、
test無効・ready先行・複数deviceの登録で新しい開始条件を検証する。

### 4.2 compilerと描画のbinary契約

code bytesだけでなく`entry_offset`, `grf_used`, `simd`, `thread_count`, `sampler_count`,
`dispatch_grf_start`, `push_regs`, `input_count`, `input_locations`, `varying_count`を保持する。
VS/FSのvarying順序と数、push領域、instruction heap容量はpipeline側で照合する。
ADL-Pの`GFX_MAX_VS_THREADS=546`を汎用GPU能力と誤認せずdevice-infoからtargetへ渡す。
`gfx_eot_only`は現行rect batchでも使う**本番のEU命令列**であり、selftest由来という理由でtestsへ落とさない。

## 5. 重要な関数の移動・統合表

### 5.1 i915.cを分ける

| 現行ファイル | 現行関数 | 移動先 | 公開方法・変更 |
| --- | --- | --- | --- |
| `i915.c` | `drv_i915_pci_driver_register`, `i915_attach`, `i915_detach`, `i915_publish`, `i915_unpublish` | `i915.c` | 登録だけE。他はO/S |
| `i915.c` | `i915_start`, `i915_stop` | `device.c` | `drv_i915_device_start/stop`へ統合 |
| `i915.c` | `i915_open`, `i915_close`, `i915_get_info` | `session.c` | O。`session_bind_ops`で登録 |
| `i915.c` | `i915_session_contexts_destroy`, `i915_session_batches_destroy`, `i915_session_object` | `session.c` | 破棄helperはS、lookupは`session_object_lookup` |
| `i915.c` | `i915_resource_create`, `i915_resource_destroy`, `i915_resource_read`, `i915_resource_write`, `i915_blob_create`, `i915_resource_map`, `i915_share_export`, `i915_share_release`, `i915_share_import` | `resource.c` | O。`resource_bind_ops`で登録 |
| `i915.c` | `i915_command`, `i915_command_submit`, `i915_command_drain`, `i915_get_capset` | `command.c` | O。native/Vulkan分岐を一箇所に保持 |
| `i915.c` | `drv_i915_stream_parse`, `i915_submit_stream`, `i915_submit_marker`, `i915_batch_acquire`, `i915_vk_command_reply` | `command.c` | parseはH。他はS、render入口へ委譲 |
| `i915.c` | `i915_job_reserve`, `i915_job_commit`, `i915_job_cancel`, `i915_job_capacity`, `i915_reservation` | `job.c` | job callbackはO、lookupはS |
| `i915.c` | `i915_stop_begin`, `i915_stop_poll`, `i915_fault`, `i915_reset_device`, `i915_isolate` | `reset.c` | O。共通GPU recovery契約を保持 |
| `i915.c` | `i915_engine_for_timeline` | `engine.c` | `drv_i915_engine_for_timeline`、command/jobから利用 |
| `i915.c` | `drv_i915_resident_publish`, `drv_i915_resident_unpublish` | `i915.c` | 通常publish/unpublishへ統合。第二の公開経路を残さない |

### 5.2 legacyとparityの実行・メモリ統合

| 現行ファイル | 現行関数（代表入口を具体名で記載） | 移動先・統合先 | 保持する意味 |
| --- | --- | --- | --- |
| `gem.c` / `parity/gt_mem.c` | `drv_i915_gem_create`, `drv_i915_gem_destroy` / `parity_gt_object_create`, `parity_gt_object_destroy`, `parity_gt_object_page_dma` | `memory.c`、memory create/get/put/page_dma | DMA backing、共有alias、orphan、retained object |
| `parity/gt_mem.c` | `parity_gt_ggtt_bind`, `parity_gt_ggtt_unbind`, `parity_gt_ggtt_read_pte`, `parity_gt_ggtt_flush` | `ggtt.c`、legacy GGTTと統合 | PTE属性、flush、予約域 |
| `parity/gt_mem.c` | `parity_gt_display_window_init`, `parity_gt_display_bind`, `parity_gt_display_bind_foreign`, `parity_gt_display_unbind_foreign`, `parity_gt_display_unbind` | `ggtt.c` | 表示VA予約とguard。表示buffer所有はscanout側 |
| `ppgtt.c` / `parity/gt_mem.c` | `drv_i915_ppgtt_create`, `drv_i915_ppgtt_destroy` / `parity_gt_ppgtt_create`, `parity_gt_ppgtt_destroy`, `parity_gt_ppgtt_alloc_range`, `parity_gt_ppgtt_insert_page`, `parity_gt_ppgtt_insert_scratch` | `ppgtt.c`、VM APIへ統合 | scratch tower、DMA address、cache/PAT、table write可視化 |
| `lrc.c` / `parity/gt_lrc.c` | `drv_i915_lrc_create`, `drv_i915_lrc_destroy` / `parity_lrc_alloc`, `parity_lrc_init_regs`, `parity_lrc_init_state`, `parity_lrc_descriptor`, `parity_lrc_release` | `context.c` | 実機で確認したcontext layout、workaround、default state |
| `parity/gt_engine.c` | `parity_engine_setup_common`, `parity_execlists_submission_setup`, `parity_execlists_enable`, `parity_engine_release` | `engine.c` | engine起動・停止の実手順 |
| `parity/gt_request.c` / `parity/gt_submit.c` | `parity_request_create`, `parity_request_add` / `parity_execlists_submit`, `parity_execlists_process_csb`, `parity_request_completed` | `request.c` | request作成、ELSP投入、CSB処理、実完了 |
| `parity/gt_init_base.c` | `parity_wa_list_apply`, `parity_engine_apply_whitelist`, `parity_get_mocs_settings`, `parity_intel_mocs_init`, `parity_init_l3cc_table` | `workarounds.c` | WA/MOCSの機種選択と適用順 |
| `parity/gt_init_base.c` | `parity_tgl_setup_private_ppat` | `ppgtt.c` | memory attributeの唯一の設定元 |
| `parity/gt_init_base.c` | `parity_intel_rc6_init`, `parity_gen11_rc6_enable`, `parity_intel_rps_init`, `parity_intel_rps_enable` | `power.c` | 初期化・無効化順、未対応条件 |
| `parity/probe.c` | `drv_i915_parity_attach` | `device.c` + tests側scenario | 正常初期化と段階停止・試験分岐を分割（R1） |
| `parity/runner.c` | `ensure_lock`, `try_launch`, `runner_thread`, `drv_i915_parity_runner_register`, `drv_i915_parity_runner_start` | `device.c`を主担当 | readiness/一度だけのworker起動は本番。runner_thread内のktest・結果報告だけtestsへ。新E/Hは§3参照 |

同名に近い処理を一方へ機械置換しない。特に`gem`とparity objectはallocation方式・固定pool・DMA mappingが異なる。
別紙の「統合」は同一動作が確認されたという意味ではなく、統合先の責務を指定する。

### 5.3 legacy_shim.cの全責務

| 現行関数 | 移動先 | 完成形の扱い |
| --- | --- | --- |
| `parity_shim_lrc_create`, `parity_shim_lrc_destroy`, `shim_find` | `context.c` | context APIへ吸収。旧/new contextを結ぶ表は統合後に不要化 |
| `parity_shim_request_kick`, `shim_run`, `shim_execute`, `parity_shim_run_sync` | `request.c` | submit/wait処理へ統合 |
| `shim_sync_do`, `shim_finish`, `shim_serve` | `request.c`を主担当 | 汎用GPU仕事の待機/完了はrequest。PRESENT/RELEASEの分岐はdisplayへ分割 |
| `parity_shim_engine_reset`, `parity_shim_engine_recover`, `parity_shim_gt_reset` | `reset.c` | 未実装拒否を保持し、実resetとの接続は別の機能差分として扱う |
| `parity_shim_display_present`, `parity_shim_display_present_blob`, `parity_shim_display_release` | `display/present.c` | present/release入口へ統合 |
| `shim_present`, `shim_present_blob` | `display/present.c` | CPU fallbackとGPU blitの選択。GPU命令生成はrender/blitへ委譲 |
| `shim_map_panel`, `shim_unmap_panel` | `display/scanout.c` | panel bufferのPPGTT mappingを管理 |
| `parity_shim_display_deps` | `display/display.c` | device-owned display参照へ置換、getter廃止候補 |
| `shim_display_window` | `display/present.c`を主担当 | 内包serving loopを展開し、enable/present/releaseに分割（R2） |
| `parity_resident_serve` | `device.c`を主担当 | 公開・実行・表示・試験期限を分割。期限停止はtests側 |
| `to_errno` | `sync.c` | `drv_i915_error_from_reference`に集約、移行後廃止候補 |

`PARITY_SHIM_REDIRECT`の6個の名前差替えは通常の関数参照へ移す。
最後まで両実装がある間は選択箇所を一箇所に限定し、単にマクロを消して旧HW経路へ戻らない。

### 5.4 Vulkan object・記録・描画

| 現行ファイル | 現行関数 | 移動先・契約 |
| --- | --- | --- |
| `vk/cmd.c` | `i915_vk_object_table_create`, `i915_vk_object_table_destroy`, `i915_vk_obj_insert`, `i915_vk_obj_lookup`, `i915_vk_obj_remove` | `render/object.c`、object Hへ改名 |
| `vk/cmd.c` | `i915_vk_read_u32`, `i915_vk_read_u64`, `i915_vk_read_handle`, `i915_vk_read_array`, `i915_vk_reply_u32`, `i915_vk_reply_u64`, `i915_vk_reply_blob` | `render/codec.c`。handleはu64 readerへ統合候補 |
| `vk/cmd.c` | `i915_vk_cmd_dispatch`, `i915_vk_route`, `i915_vk_cmd_builtin`, `i915_vk_cmd_set_reply`, `i915_vk_cmd_seek_reply` | `render/dispatch.c`。opcodeごとの単一所有先へ |
| 上行の補正 | `i915_vk_cmd_set_reply`, `i915_vk_cmd_seek_reply` | `render/transport.c`。builtin内137処理も移し、dispatchからtransport入口へ委譲 |
| `vk/inst.c` | `inst_execute_streams` | `render/transport.c`。180のdecoder再入は明示的な同一取引の子streamとして上限を設ける |
| `vk/inst.c` | `i915_vk_inst_dispatch`, `inst_create_instance`, `inst_enumerate_physical_devices`, `inst_properties`, `inst_features`, `inst_memory_properties`, `inst_queue_families`, `inst_format_features`, `inst_format_properties`, `inst_image_format_properties`, `inst_create_device`, `inst_get_device_queue2`, `inst_destroy`, `inst_wait_idle`, `set_float` | `render/instance.c`。instance_dispatchをH化、180枝だけtransportへ移す |
| `vk/gfx-obj.c` | `gfx_allocate_memory`, `gfx_free_memory`, `gfx_create_buffer`, `gfx_bind`, `gfx_requirements`, `drv_i915_vk_blob_attach`, `drv_i915_vk_blob_detach` | `render/memory.c`。image枝はimage担当へ委譲 |
| `vk/gfx-obj.c` | `gfx_format_bytes`, `gfx_create_image`, `gfx_create_image_view`, `gfx_create_sampler`, `gfx_subresource_layout` | `render/image.c` |
| `vk/gfx-obj.c` | `gfx_create_dsl`, `gfx_create_dpool`, `gfx_allocate_dsets`, `gfx_update_dsets` | `render/descriptor.c` |
| `vk/gfx-obj.c` | `gfx_create_pipeline_layout`, `gfx_create_shader`, `gfx_decode_pipeline`, `gfx_create_pipelines`, `gfx_destroy_pipeline`, `gfx_float_bits` | `render/pipeline.c` |
| `vk/gfx-obj.c` | `gfx_create_render_pass`, `gfx_create_framebuffer` | `render/render-pass.c` |
| `vk/gfx-obj.c` | `gfx_create_semaphore` | `render/sync.c` |
| `vk/gfx-obj.c` | `gfx_destroy_plain` | `render/object.c`、型ごとのdestructorへ委譲 |
| `vk/gfx-obj.c` | `gfx_create_tail`, `gfx_create_reply`, `gfx_result` | `render/codec.c`。複数ownerから利用するHへ |
| `vk/gfx-rec.c` | `rec_create_pool`, `rec_destroy_pool`, `rec_reset_pool`, `rec_allocate`, `rec_free`, `rec_begin`, `rec_end`, `rec_command`, `exec_cmdbuf`, `rec_submit`, `i915_vk_gfx_rec_dispatch` | `render/command.c`。他のrec補助関数も別紙に列挙 |
| `vk/gfx-rec.c` | `exec_clear`, `exec_copy`, `exec_image`, `image_surface` | `render/blit.c`を主担当。Vulkan handle解決はcommand/image側に残し、surfaceへ変換してから呼ぶ |
| `vk/gfx-draw.c` | `gfx_compile_stage`, `i915_vk_gfx_pipeline_prepare`, `i915_vk_gfx_pipeline_release`, `gfx_kernels` | `render/pipeline.c`。compile結果のGPU配置を所有 |
| `vk/gfx-draw.c` | `emit_sba`, `emit_vertex_input`, `emit_urb`, `emit_constants`, `emit_raster`, `emit_depth`, `emit_shader_state`, `gfx_write_state`, `gfx_write_rss`, `gfx_write_sampler`, `gfx_write_surface` | `render/state.c`。state_emit内部に集約 |
| `vk/gfx-draw.c` | `gfx_rect_compile`, `i915_vk_gfx_rect_prepare`, `i915_vk_gfx_rect_build`, `i915_vk_gfx_rect` | `render/blit.c`。compilerで作るfill/copy shaderもここが保持 |
| `vk/gfx-draw.c` | `gfx_build_batch`, `i915_vk_gfx_draw`, `gfx_session`, `i915_vk_gfx_session_close` | `render/draw.c` |
| `vk/gfx-draw.c` | `emit`, `emit_zero`, `emit_words`, `emit_pointer`, `emit_pc` | `render/batch.c`、batch Hへ改名 |
| `vk/gfx-draw.c` | `sf_half`, `sf_add`, `sf_sub`, `sf_from_u32`, `sf_ratio` | `render/math.c`、float Hへ改名 |
| `vk/gfx-draw.c` | `gfx_census`, `gfx_fnv1a` | `tests/render/readback.c`, `tests/render/reference-shaders.c`。本番からの診断/参照shader呼出しも同時に取り除く |

旧`vk/res.c`・`pipe.c`・`cmdbuf.c`・`sync.c`・`wsi.c`・`display.c`の全抽出関数も別紙に記載する。
新経路がないopcode、fixtureの直接呼出し、型・allocation契約を確認するまでは一括削除しない。
重複dispatchを恒久的に残す案ではなく、最終的にopcodeの処理先を一つにするための統合台帳である。

### 5.5 表示の統合関数と移植本体

| 現行ファイル | 現行関数 | 移動先・扱い |
| --- | --- | --- |
| `parity/resident_display.c` | `rd_init`, `rd_panel`, `rd_query`, `rd_mode`, `rd_claim`, `drv_i915_resident_display_close` | `display/display.c`。rd状態はdevice所有へ |
| 同上 | `rd_device_query`, `rd_constraints` | `display/scanout.c`、scanout sub-ops |
| 同上 | `rd_present`, `rd_wait`, `rd_release`, `rd_release_locked`, `rd_blit_build` | `display/present.c`。blit_buildはrender/blit契約呼出しへ |
| 同上 | `rd_events` | `display/hotplug.c`。現行固定値と未対応状態を保持して移す |
| 同上 | `parity_shim_blit_source` | `tests/render/readback.c` |
| `parity/lcd/parity_lcd_kernel.c` | `parity_lcd_kernel_panel_mode`, `parity_lcd_kernel_panel_size_mm` | `display/state.c` |
| 同上 | `parity_lcd_resident_buffer`, `parity_lcd_resident_back` | `display/scanout.c` |
| 同上 | `parity_lcd_resident_flip` | `display/present.c` |
| 同上 | `parity_lcd_kernel_resident_run`, `resident_window`, `resident_verify`, `fill_cfg`, `preflight` | `display/modeset.c`を主担当。設定、enable、disable、診断へ分割 |
| 同上 | `k_read32`, `k_write32`, `k_rmw32`, `k_posting_read`, `k_wait_reg` | `mmio.c`へ吸収。callback接着だけなら削除候補 |
| 同上 | `k_dpcd_read`, `k_dpcd_write`, `k_read_dpcd_caps` | `display/aux.c`へ吸収 |
| 同上 | `k_power_get`, `k_power_get_if_enabled`, `k_power_put`, `k_power_put_async` | `display/power.c`へ吸収。非同期putの寿命は保持 |
| 同上 | `k_vblank_get`, `k_vblank_put`, `k_vblank_sleep`, `k_arm_event`, `k_wait_event`, `k_cancel_event` | `display/vblank.c`へ吸収 |
| 同上 | `parity_lcd_kernel_lcdb_run`, `parity_lcd_kernel_lcdc_run`, `parity_lcd_kernel_lcdd_run`, `parity_lcd_kernel_lcdo_run`, `parity_lcd_kernel_hdmib_run`, `parity_lcd_kernel_dual_run`, `parity_lcd_kernel_dual_share_run`, `parity_lcd_kernel_n1_run` | `tests/display/kernel-scenarios.c`。共有の本番処理はdisplay各部へ抽出 |
| `parity/lcd/parity_lcd_modeset.c` | `parity_lcd_modeset_prepare`, `parity_lcd_modeset_enable`, `parity_lcd_modeset_disable`, `parity_lcd_modeset_commit_enable`, `parity_lcd_modeset_commit_disable` | `display/modeset.c`、device/pipe引数を明示 |
| 同上 | `parity_lcd_modeset_select`, `parity_lcd_modeset_selected`, `bind_current` | `display/modeset.c`で型統合時に廃止候補。選択中globalを引数へ |
| 同上 | `parity_lcd_modeset_flip` | `display/present.c` |
| 同上 | `parity_lcd_modeset_brightness`, `parity_lcd_modeset_backlight_acpi`, `parity_lcd_modeset_backlight` | `display/panel.c` |

大量の`intel_*_port.c`・`drm_*_port.c`・`*_glue.inc`の関数は別紙の各行で配置を指定する。
例: DPLL/CDCLK→clock、PPS/backlight→panel、watermark/BW→watermark、AUX→aux、EDID→edid。
関数順序・posting read・待機・power参照を保存して統合し、Linuxとの対応情報は出典記録に残す。

### 5.6 分割で新たにファイルをまたぐstatic helper

| 現行helper | 分割後の利用者 | 解決案 |
| --- | --- | --- |
| `gfx_create_tail`, `gfx_create_reply`, 複数の`gfx_result` | memory/image/descriptor/pipeline/sync | codec.hの単一wire helperへ統合。reply形式が等しいことを先に確認 |
| `emit`, `emit_zero`, `emit_words`, `emit_pointer`, `emit_pc` | state/draw/blit | batch.hでHとして公開 |
| `sf_half`, `sf_add`, `sf_sub`, `sf_from_u32`, `sf_ratio` | state/blit | math.hでHとして公開 |
| `gfx_object` | draw/pipeline作業資源 | render memoryのbuffer allocationへ統合 |
| `gfx_compile_stage` | pipeline | pipeline.c内へ呼出側ごと移しSを維持 |
| `gfx_write_surface` | blit/state | state.hのsurface_writeへ統合 |
| `shim_find` | context/request | requestへcontext参照を渡し、shim lookupを不要化する |
| `to_errno` | context/request等 | sync.hの移行用errno変換へ統合 |
| `bind_current` | 複数display関数 | 明示的なdisplay/pipe引数へ置換 |
| `kind_name` | `log_trace` | `display/diagnostics.c`へ同居。試験側への誤配置を訂正 |

別紙には同一旧ファイル内の直接呼出しを使った「分割で境界を越えるhelper」の表も付ける。
その表は変更が必要な依存の検出であり、検出した全helperを恒久公開する指示ではない。
同名static helperを同じ新ファイルへ集めると衝突するため、別紙の衝突表を照合する。

## 6. ヘッダ・生成物・試験の扱い

| 現行のもの | 移動/統合先 | 方針 |
| --- | --- | --- |
| root `internal.h` | `i915.h`と各所有header | 全型を一個の巨大headerへ移すだけにしない |
| `vk/vk-internal.h`, `vk/gfx.h` | `render/internal.h`とobject/image/pipeline等 | compiler IR・binaryはcompilerへ分離 |
| `parity.h`, `*_ref_types.h`, `*_compat.h` | device/common/displayの所有header | Linux名の型を必要なdriver型へ統合。layout依存は検証して変更 |
| `linux/*.inc`, `vk/linux/*.inc`, register header、enum/table片 | `data/`の機能別inc | 原著作権・ライセンス・出典SHA・変換規則を維持 |
| 関数本体を含む`*_glue.inc` | 対応する`.c` | データとしてdataへ隠さず、通常関数へ統合 |
| `vk/codec-generated.inc` | `data/vulkan-codec.inc` | generatorも出力先変更。現時点の147関数はstatic生成物 |
| `vk/vkref-generated.inc`, `tex_fixture_gen.inc`, `tex_fixture_fhd_gen.inc`, `draw_fixture.h` | `tests/fixtures/` | Mesa参照shader・固定batch/texture等 |
| `firmware_adlp_dmc.c`, `firmware_tgl_dmc.c`, `firmware_vbt_dell_latitude_5320.c`, `firmware_vbt_dell_latitude_5330.c` | `data/firmware/` | 当面は配置整理。外部file配布方式への変更はR6 |
| `parity/osdep/`・`backend_*.c` | common各部、試験代替実装はtests | 範囲検査、deadline、DMA、同期の機能を保持して中継を整理 |
| ktest、fake HW、frame census、実機scenario | `tests/` | production object listから分離。試験は本番処理を呼ぶ |
| `plan/ws031/handover/tools/`の生成器・manifest | 既存の所在を保持して更新 | 出力先だけでなく型名・include・generator再実行による復元を確認 |
| `platform/amd64/vmunix.mk` | production/testの明示source listへ更新 | 実コード・試験の両方を常時linkする現状を整理 |
| `parity/lcd/port_lcd_calc.manifest.json` | `data/provenance/port_lcd_calc.manifest.json` | 生成器・元ソースhash・出力一覧の対応も更新。単に消さない |
| `parity/tests/run.sh` | `tests/contracts/run.sh` | 同居する6種のcontract fixture、mock、変更後の本番source pathに追従 |
| `include/drivers/i915-parity.h`, `src/kern/main.c` | public readiness宣言と呼出先の追従 | ツリー外の本番呼出元。testsを外しても起動できるよう新Eへ接続 |
| `plan/ws031/handover/tools/gen_vk_server_codec.py` | 現位置維持、出力先変更 | kernel codec生成器の実体。libvulkanのmaintain-codec.noctだけの更新では不足 |

現行generated sourceを直接書き換えるだけだとgeneratorが旧構成へ戻す。
移植本体を今後手管理へ切り替える場合は、生成を止める範囲と出典manifestをセットで記録する（R5）。
試験を分離する際も、fake HWが実装と同じ誤った定数を共有するだけの検証へ戻さない。

## 7. レビューで決める事項

| ID | 決めること | 本草案の置き方 |
| --- | --- | --- |
| R1 | legacy/parityのメモリ・context・device状態の統合単位 | 一つの所有モデルへ統合。初期化・実行手順はE-130で動いた経路を追跡して維持 |
| R2 | LCD callback内serving loopをいつ解体するか | 完成形はenable→present→disable。移動だけで直せないため独立した変更段階 |
| R3 | H入口とI内部候補の境界、bind_ops方式 | public Hを少数にし、Iは所有部内に限定。既存GPU core APIは維持 |
| R4 | 旧Vulkan moduleの残す処理・試験・旧WSI | 全関数を照合対象として配置。未使用認定と削除は呼出し/fixture確認後 |
| R5 | Linuxからの関数抽出generatorを継続するか | runtimeの互換層は統合。生成継続の可否は別判断、出典は保持 |
| R6 | DMC/VBTの埋込みを維持するか | 今回の配置案ではdataへ移動。firmware loader/配布変更は未決 |
| R7 | 同期実行・能力制限・未実装エラーの扱い | まず現在の正常系を保持。非同期化/recovery実装を単純移動に混ぜない |
| R8 | compilerの完全署名とallocator/diagnostic契約 | kernel内で独立したcompile入力/outputへ。device/MMIO依存なし |
| R9 | flags7と停止・回復・session隔離をどこまで受理するか | 現行正常系の互換と完全契約は別。削ると現libvulkanの初期化が失敗し、残すだけでは契約未達。レビューA06参照 |

レビュー順は、§2の所有権 → §3の公開境界 → §5の分割対象 → 別紙の個別配置を推奨する。
本書はWS031の完了判定、未実装機能の免除、次のQueue選択を代行しない。

## 8. 移行時に確認すること

| 段階 | 確認の対象 |
| --- | --- |
| 配置とheaderの整理 | 全定義の移動先、staticの可視性、同名衝突、include、source list、生成器 |
| 二系統の統合 | backing/DMA/cache、GGTT予約域、LRC/requestの実体、session参照寿命、停止失敗時の保持 |
| wire/描画の統合 | opcode所有表とreply、libvulkan無改造の往復、compiler gentool照合、独立画素oracle |
| 表示の統合 | modeset順序、power参照、GPUコピー→flipの順序、release時の実停止 |
| 試験分離後 | 同じ本番処理をhost fixture/実GPUで確認。旧fixtureのPASSだけで新経路を受理しない |

本書作成ではbuild/実機を実行しない。文書内の参照先・関数名・抽出範囲・リンクを検査する。
実装段階の具体的な有限試験範囲と実行Queueは別途定める。

### 文書検査の記録

- C/H/INC 376ファイルから2,988件の関数定義を抽出し、別紙の2,988行と対応させた。
- 各旧関数名が、基準ソースの記載行に存在することを照合した。
- 分割後にファイル境界を越える同一旧ファイル内の直接呼出し201辺と、同一配置先へ集まる同名定義6組を列挙した。
- 生成codecの147関数を通常のglobal APIへ誤って昇格させないよう区別した。
- 文書内リンク・表の列数・空白を確認した。関数マクロ、間接呼出し、全構成のlink依存の完全性は未検証。

初稿の196辺・7組から配置を訂正した。再集計の詳細は別紙B–Dとレビュー結果を参照する。
378ファイルのファイル単位保全と、2,988個の明示関数定義の対応を確認したが、
全マクロ展開・全構成のlink成功・全libvulkan API互換まで証明したとは扱わない。
