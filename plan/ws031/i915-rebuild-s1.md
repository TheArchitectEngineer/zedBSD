# i915 再構築 S1 — 共通部の分担

[実施計画](i915-rebuild-plan.md)、[共通規則](i915-rebuild-rules.md)。

## 1. 本番経路で使われている旧コード

E-130 の構成（resident + display）で実行される旧コードは次のとおり。

- ソフトウェア側（legacy）: `i915.c` の ops（session、resource、blob、share、command、job、recovery）、
  `gem.c`（`drv_i915_gem_bind_ggtt`/`unbind_ggtt` を除く）、`ppgtt.c`、`request.c`（待ち行列、完了通知）。
- 実行側（parity）: `parity/gt_*.c`、`reset.c`、`pcode.c`、`pxp.c`、`irq.c`（GT と表示の両方）、
  `legacy_shim.c`（context、request の実行、serving thread、表示の同期窓）。
- 共通の OS 層: `parity/osdep/*`、`parity/backend_*.c`、`wait.c`、`timer_calc.c`。

次の旧ファイルは本番から到達しない（resident では `i915_start()` が parity 登録で return し、他の呼出元は
同じ集合の中だけ）。**移植せず廃止**とし、移行表に理由を残す。

| 旧ファイル | 到達しない理由 |
| --- | --- |
| `uncore.c` | forcewake/reset/register は parity の osdep mmio と `reset.c` が担う。呼出元は `engine.c`・`ggtt.c`・`irq.c`・`lrc.c` と非 resident の `i915_start()` だけ |
| `ggtt.c` | GGTT は parity の `gt_mem.c` が管理。`drv_i915_ggtt_*` の呼出元は `gem.c` の `bind_ggtt`（`engine.c`/`lrc.c` からだけ呼ばれる）と非 resident の start/stop |
| `engine.c` | engine の HW 初期化・reset・割込みは parity 側。resident では `drv_i915_resident_publish()` が engine を記録として作るだけ |
| `lrc.c` | context は `PARITY_SHIM_REDIRECT` で parity の LRC に置換済み |
| `irq.c`（legacy） | 割込みは parity の `irq.c`。`drv_i915_irq_start/stop` の呼出元は非 resident の start/stop だけ |
| `selftest.c` | 試験（S5 で扱いを決める） |

## 2. 担当とファイル

名前は共通規則 §2 の機械的な置換（`parity_x` → `drv_i915_x`、`struct parity_x` → `struct i915_x`）で決まるので、
並行して書いても互いの名前を予測できる。最終的な H 契約名（設計 §3）への改名は統合時に行う。

| 担当 | 旧ソース | 新ファイル |
| --- | --- | --- |
| 基盤（済/作業中） | osdep mmio/trace、pci、dma、runtime_pm、firmware、sync、backend_*、wait、timer_calc | `mmio`, `trace`, `pci`, `dma`, `runtime-pm`, `firmware`, `sync`, `workqueue` |
| GT 情報・reset・PCODE | `gt_mmio.c`、`reset.c`、`pcode.c` | `device-info.c`（fuse/SSEU/engine 表）、`reset.c`（GT reset）、`power.c`（PCODE） |
| workaround・電源 | `gt_init_base.c`、`gt_wa_adlp.c`、`gt_verify_wa.c`、`gt_init.h` | `workarounds.c`（WA/whitelist/MOCS/L3CC、検証）、`power.c`（RC6/RPS、PCODE と同居）、`ppgtt.c` の PAT は memory 担当へ関数を渡す |
| memory | `gt_mem.c`、`gt_tlb.c`、`pte.c`、legacy `gem.c`、legacy `ppgtt.c` | `memory.c`、`ggtt.c`、`ppgtt.c`、`tlb.c` |
| 実行 | `gt_engine.c`、`gt_lrc.c`、`gt_request.c`、`gt_submit.c`、`gt_resume.c`、`gt_defaults.c`、`gt_migrate.c`、`pxp.c` | `engine.c`、`context.c`、`request.c`（実行側）、`submit.c`（execlists）、`defaults.c`、`migrate.c`、`pxp.c` |
| 割込み | `irq.c`（parity） | `irq.c`（最上位 handler と GT）。表示の DE/PCH/vblank は S4 で `display/` へ。S1 では hook |
| ops | legacy `i915.c` の ops、`request.c`（待ち行列）、`legacy_shim.c` | `session.c`、`resource.c`、`command.c`、`job.c`、`reset.c`（recovery ops）、`request.c`（待ち行列）、`worker.c`（serving thread）、`i915.c`（publish） |
| 起動列 | `probe.c`、`runner.c` | `device.c` |

同じ新ファイルに二つの担当が書く場合（`power.c`、`reset.c`、`request.c`）は、先の担当が作り、後の担当が追記する。
