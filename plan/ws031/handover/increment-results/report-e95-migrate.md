# WS031 E-95 報告: `intel_migrate_init` 実機成功 — intel_gt_init の GPU 側処理を完走

日付: 2026-09-18 / 対象: zedBSD parity（Linux 6.8.12 正本）/ 実機: ADL-P 8086:46a8（VFIO）

## 結論

`intel_migrate_init`（コピー用 pinned context と専用 ppgtt の構築）を実装し、実機で成功しました。これで `intel_gt_init` の GPU に触れる処理はすべて完走し、`i915_gem_init` に残るのは `intel_engines_driver_register`（uabi 名付けとエンジン一覧＝簿記）だけです。

```
P6c migrate_init: rc=0 engine=bcs0 tables=11 windows=16 MiB pte_window=0x1000000 exposed_pts=8
                  ring=524288 state=16384 | ggtt ring=0xfff34000 state=0xfff30000 hwsp=0xfff13108
                  lrc PDP0=00000001:00291000 RING_CTL=0007f001 top_pd=0x100291000
teardown: … GT objects released (live=0) … PM released
attach end: BLOCKED where=intel_engines_driver_register
ktest: 354 checks, 0 failures（GPU-free / 実機とも）
```

- migrate 専用 ppgtt は正本の古典配置（ADL-P は 64K ページ無し）：[0, 8 MiB) 転送元窓 / [8, 16 MiB) 転送先窓 / [16 MiB, +32 KiB) PTE 窓。PTE 窓には窓自身のページ表 8 枚を UC で写像し、後に GPU が blit と同時に PTE を書き換えられるようにします（正本 `insert_pte`）。
- bcs0 上に 512 KiB ring の pinned context を作り、LRC の PDP0 が migrate ppgtt の最上位表を指すこと、RING_CTL、timeline（エンジン status page の 0x108）を確認。正本どおり init 段階では何も投入しません。
- teardown で全 GT object が解放（live=0）。

## 実装

- `gt_migrate.{c,h}`：`first_copy_engine` → ppgtt 生成 → `allocate_va_range(0, 2·CHUNK_SZ + PTE 窓)` → `foreach` + `insert_pte` → `intel_engine_create_pinned_context(bcs0, vm, SZ_512K, HWS_MIGRATE)`。fini は unpin/put と vm 解放。
- `gt_mem` 拡張：`__gen8_ppgtt_alloc` / `__gen8_ppgtt_foreach` / `gen8_ppgtt_insert_entry` に対応する `alloc_range` / `foreach_pt` / `insert_page`（正本の index 計算 `gen8_pd_range` / `gen8_pt_count` をそのまま）。
- **記録した適応**：この kernel の DMA ベクタは 64 KiB 上限のため、64 KiB 超の object（512 KiB ring）は `drv_dma_alloc_coherent` 1 本で確保します（i915 の DMA device は max_segment_size=UINT_MAX なので許容範囲）。汎用層は変更していません。ページ表 stash の事前確保は固定プールからの逐次確保に置換。
- GPU-free ktest +7（表の木構造、PTE 窓、pinned context、解放漏れ）。338 → 354/0。

## 正本で確定した事実（抜粋）

- `intel_migrate_init` の戻り値は `intel_gt_init` で検査されない（失敗時は TTM 移動が memcpy に退避）。
- pinned context は CONTEXT_BARRIER_BIT 付き、timeline はエンジン status page 上（initial breadcrumb 無し）、`intel_context_pin` で `lrc_init_state`。

## 次（P7）

`i915_driver_probe` の続き：`intel_pxp_init`（ADL-P で PXP 対応 GT の扱いを正本で確認）→ `intel_display_driver_probe`（GEM 後の表示初期化）→ `i915_driver_register`。同じ手順（正本再導出 → GPU-free → 実機）で進めます。着手してよければ一言ください。

git commit / push はしていません。HAL インタフェースは不変です。
