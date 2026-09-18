# WS031 E-94 報告: `__engines_verify_workarounds` 実機成功

日付: 2026-09-18 / 対象: zedBSD parity（Linux 6.8.12 正本）/ 実機: ADL-P 8086:46a8（VFIO）

## 結論

承認②の常時診断 `__engines_verify_workarounds` を実装し、**実機で全エンジン合格**しました。GPU が SRM（MI_STORE_REGISTER_MEM）でエンジン WA レジスタをメモリへ書き出し、CPU 側で期待値と照合した結果、**不一致 0** です。これは parity 経路で初めて GPU にレジスタを読ませてメモリへ書かせた実行です。

```
P6c verify_workarounds: rc=0 polls=23 timed_out=0 | gt irq during: user=9 ctx_switch=11 error=0
P6c rcs0  verify_wa: list=8 emitted=5 mcr_skipped=3 verified=5 mismatched=0 | submits=4 completes=4 errors=0
P6c bcs0/vcs0/vcs2/vecs0 verify_wa: list=1 emitted=1 verified=1 mismatched=0 | errors=0
attach end: BLOCKED where=intel_migrate_init   (次段の設計上の停止点)
ktest: 347 checks, 0 failures（GPU-free / 実機とも）
```

- rcs0 の 8 件のうち 5 件（RING_CMD_CCTL、Wa_1606700617、Wa_14010919138 ほか）を検証・全一致。残り 3 件（GEN8_ROW_CHICKEN2 / GEN10_SAMPLER_MODE / GEN9_ROW_CHICKEN4）は MCR 域（0xde80–0xe8ff）で、正本どおり CS 経路では検証しません（MCR セレクタは CPU の MMIO しか制御しないため）。
- 4 つの XCS は RING_CMD_CCTL の 1 件のみ（正本の xcs_engine_wa_init は ADL-P に該当項目なし）。
- 各エンジンで SRM request → park 切替の 2 投入が完了し、全エンジンが idle（PARKED）に戻りました。kernel ring の消費は rcs0 608 B / XCS 400 B（4 KiB、wrap なし）。

## 実装（`gt_verify_wa.{c,h}`）

正本 `engine_wa_list_verify()` の流れをそのまま：scratch 1 頁を GGTT へ pin → **engine->kernel_context** に request（request_alloc の invalidate）→ `wa_list_srm()`（`MI_STORE_REGISTER_MEM_GEN8 | GLOBAL_GTT`、宛先は scratch + 4×リスト index、MCR 域は発行しない）→ breadcrumb → 投入 → `i915_request_wait(HZ/5)` → `wa_verify()`（`(cur ^ set) & read`、read mask 0 は常に合格）→ `intel_engine_pm_put()` の park 切替、最後に `intel_gt_wait_for_idle`。どれかが失敗すれば正本と同じく -EIO に集約し、probe は FAILED（teardown でリセット）。

記録した適応: 待ちは CSB/HWSP ポーリング（≥200 ms）、park 切替は SRM request の完了後に投入（1 in-flight）、タイムアウトしたエンジンには park を投入しない、結果頁は固定プール。ETIME がこの errno 集合に無いので `ETIME=ETIMEDOUT` を定義。

GPU-free ktest を 9 件追加（発行語の位置、MCR 除外、read-mask-0 除外、-ENXIO、park 投入、-ETIME→-EIO、空リスト無投入）。338→347/0。

## 次

intel_gt_init の残り：`intel_uc_init_late`（GuC 無効で何もしない）→ **`intel_migrate_init`**（コピー用の pinned context 生成。まず正本から再導出し GPU-free で固めます）→ その先が P7（i915_gem_init の続き）。このまま進めてよければ着手します。

git commit / push はしていません。HAL インタフェースは不変です。
