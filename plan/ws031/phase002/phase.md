# WS031 p002 計画: top + cmd — 入口・wire decoder・object table・dispatch routing

native 実行器の入口と、wire を読んで op を各モジュールへ振り分ける routing、object/handle table を実装する。全モジュールの dispatch 規約をここで確定する（後段はこれを参照）。おおまかな設計。精度は実装・差し戻しで上げる。

## Module と所有ファイル
- `src/drivers/gpu/i915/vk/vk.c`, `vk.h`（入口・attach/detach・session）
- `src/drivers/gpu/i915/vk/cmd.c`, `cmd.h`（decoder・object table・routing）
- `vk-internal.h`（共有型を確定。p001 の枠を実体化）
- 承認要: `src/drivers/gpu/i915/i915.c`,`internal.h` に drv_gpu command 経路 → `drv_i915_vk_command` の結線（最小 hook、差分を本 doc に添付）。
- fixture: `plan/ws031/tests/i915-vk-cmd-test.c`, `i915-vk-fixture.inc`。

## 実装する公開インタフェース（規約・正本）
`vk.h`:
- `int drv_i915_vk_attach(struct i915_device *dev, struct i915_vk_device **out);`
- `void drv_i915_vk_detach(struct i915_vk_device *vk);`
- `int drv_i915_vk_open(struct i915_vk_device *vk, struct i915_vk_session *ppgtt_session, struct i915_vk_session **out);` — drv_gpu open に対応。
- `void drv_i915_vk_close(struct i915_vk_session *s);`
- `int drv_i915_vk_command(struct i915_vk_session *s, const void *wire, size_t bytes, void *reply, size_t *reply_bytes);` — drv_gpu の command 経路の入口。

`cmd.h`（**routing 規約 = 全モジュール共通**）:
- `int i915_vk_cmd_dispatch(struct i915_vk_session *s, struct i915_vk_reader *r, struct i915_vk_writer *reply);` — 1 command をデコードし op で routing。
- **各モジュールは `int i915_vk_<mod>_dispatch(struct i915_vk_session *s, uint32_t op, struct i915_vk_reader *r, struct i915_vk_writer *reply);` を公開し、cmd が opcode 範囲で呼ぶ**（mod = res/pipe/cmdbuf/sync/wsi）。instance/device/queue と反射的 get 系は cmd 内で処理。
- object table: `int i915_vk_obj_insert(struct i915_vk_device*, enum i915_vk_object_kind, i915_vk_handle, void *ptr);` / `void *i915_vk_obj_lookup(...kind, handle);` / `void i915_vk_obj_remove(...);`
- reader/writer prim: `uint32_t i915_vk_read_u32(r);` `uint64_t i915_vk_read_u64(r);` `i915_vk_handle i915_vk_read_handle(r);` `const void *i915_vk_read_array(r, size_t n, size_t elem);` `void i915_vk_reply_u32/u64/blob(...)`。範囲外は reader を error 状態にし dispatch が EINVAL。

`vk-internal.h`（確定）: `struct i915_vk_device`（`struct i915_device *i915`、object table、global state state heap 参照、capset）、`struct i915_vk_session`（WS029 の drv_gpu session/PPGTT vm 参照、command pool/descriptor pool リスト、per-session object）、`enum i915_vk_object_kind`、`typedef uint64_t i915_vk_handle;`、`struct i915_vk_reader/writer`、`enum i915_vk_stage`、`struct i915_vk_batch`（GEN バッチ: GEM object＋cursor。cmdbuf/pipe が使う）、`int i915_vk_errno(int vk_result);`。

## 内部関数構成ガイド
opcode→handler の routing table、instance/device/queue の生成・破棄、`vkGetPhysicalDevice*`（features/properties/memory/format/queue family）を i915 能力から返す、object table（ハッシュまたは配列）、reader/writer の境界検査。

## 依存
- 前段: p001（`vk/*.h` 枠、capset 方針、symbol 台帳）。
- WS029 core: drv_gpu open/close の session、`drv_gpu_complete` は未使用（p009）。command 経路の hook（承認）。
- 後段はこの doc の routing 規約と object table・reader/writer を参照。

## 触れるファイル / 触れないファイル
- 触れる: `vk/vk.*`,`vk/cmd.*`,`vk/vk-internal.h`,`plan/ws031/tests/*`＋承認済み core hook。
- 触れない: 他モジュールの `.c`、HAL、UAPI、libvulkan、WS029 core の hook 以外。

## 受け入れ条件と試験
- host fixture: 既知 wire を流し、instance/device/queue/memory/buffer の create→lookup→destroy、反射 get の値、未対応 op の EINVAL、reader 境界。
- build: i915 config warning 0、GPU なし build で symbol 0。
- 実機なし。

## 見積・制限
300 分。生成系の image/sampler/descriptor/pipeline/cmd/draw は stub routing（各モジュール未実装なら ENOSYS）。

## 設計調整（p002 実行時）
WS029 i915 backend は libvulkan 必要な `get_capset`/`blob_create`/`resource_map`/`present` と `GPU_CAP_CAPSET|BLOB|MAPPING` を欠く。よって top は libvulkan 必要な drv_gpu op 一式を供給し、`i915.c` の `i915_publish` ops テーブルと `I915_CAPABILITIES` を拡張する（external-design 補遺A）。native stream client は既存経路を維持。capset の具体内容は本 Phase で確定。

## 進捗（framework 検証済み）
コマンドフレームワーク中核を実装・検証。`vk/vk-internal.h`（device/session/batch 実体化）、`vk/cmd.c`（object table insert/lookup/remove/grow/replace、LE wire reader/writer と境界、opcode 数値レンジ routing→各モジュール dispatch、builtin version）、`vk/vk.c`（attach/detach/open/close、capset≥156B、command 入口、i915_vk_errno）。host fixture `plan/ws031/tests/i915-vk-cmd-test.c`＋runner `run-vk-host-tests.sh` が通常＋ASan/UBSan で PASS、リーク無し、warning 0。
残: i915.c の drv_gpu ops テーブル拡張（get_capset/blob_create/resource_map/command 配線＋capabilities）と vmunix.mk 配線で kernel link、per-command handler は各モジュール Phase で肉付け。

## 進捗（kernel link 済み）
vk を kernel に統合・link 確認。モジュール dispatch stub（res/pipe/cmdbuf/sync/wsi、各 Phase で置換）、`vmunix.mk` に vk 7 source 追加、`internal.h` に `i915_device.vk`、`i915.c` で attach 後に `drv_i915_vk_attach`/stop で `drv_i915_vk_detach`。i915 config kernel build PASS（vmunix check PASS、vk 7 obj link、warning 0、`-mgeneral-regs-only` の FPU 不使用制約クリア）。WS029 fixture は i915.c 変更で vk シンボル未定義になったため、WS029 fixture に `drv_i915_vk_attach/detach` stub を追加（WS031 統合の最小変更）、WS029 host fixture 全 PASS（回帰なし）。
残（p002 clear まで）: drv_gpu ops テーブルに get_capset/blob_create/resource_map/command を配線＋capabilities 拡張、i915_open/close で vk session 生成（`i915_session.vk`）、command の native/Venus 判別（native magic 0x31394958）。
