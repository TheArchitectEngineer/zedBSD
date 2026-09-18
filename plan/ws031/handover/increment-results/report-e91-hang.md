# WS031 E-91 報告: P1 `intel_gt_init_mmio` → P3 ハングの根本原因と修正

日付: 2026-09-18 / 対象: zedBSD parity (Linux 6.8.12 正本) / 実機: ADL-P 8086:46a8 rev 0x0c (VFIO)

## 結論

ハングの原因は **i915 側ではなくカーネル側の欠陥 2 件**でした。どちらも修正済みで、`intel_gt_init_mmio` は参照どおり **P1 に戻した**まま P6-b まで実機完走しています。

| # | 欠陥 | 所在 | 修正 |
|---|---|---|---|
| RC-1 | **128 KiB の `struct osdep_trace` をスタックに置いていた**(16 KiB カーネルスタックに対し) | `parity/display_core.c`(2 箇所)、`parity/ktest.c`(11 箇所) | static 化。再発防止に `-Wframe-larger-than=8192` を AMD64_CFLAGS に追加 |
| RC-2 | **`sched_sleep_locked()` 系が IRQ 状態を復元しない** | `kern/sched.c`(3 変種) | `sched_sleep()` と同じ規約で保存・復元 |

| RC-3 | **ビルド依存追跡の穴**: `Makefile` の `.d` 取り込みが `$(BUILD)` 配下 7 階層までで、`parity/osdep/*.d`(8 階層)が対象外。ヘッダ変更後も `osdep/trace.o` 等が再ビルドされず旧レイアウトのまま link | `Makefile`(1 行追加) + クリーン再ビルド |

`gt_init_mmio` を P1 に置いたことは**トリガーであって原因ではありません**(ヒープ配置が数百バイトずれ、RC-1 のゼロ埋めの被害対象が変わっただけ)。RC-3 は RC-1 の修正を検証する過程で露見したもので、`trace.h` の容量を変えた直後の GPU-free テストで DMC 3 件が「テスト用ノブが worker に見えない」形で失敗し、旧レイアウト(128 KiB)の `osdep_trace_init` が 32 KiB リングの直後にある `g_fw_test`/ノブを上書きしていたことを、シンボルアドレスとオブジェクトのタイムスタンプで確認しました。

## 何が起きていたか

```
P3 intel_power_domains_init_hw (フレーム 131,176 B)  ← 16 KiB スタックを ~115 KiB 突き抜ける
  └ osdep_trace_init(&tr)  → スタック下のヒープ ~128 KiB をゼロ埋め
      ├ 他スレッドの struct thread / スタック   → 戻り先が 0 になり ret で #PF(rip=0)
      └ 起床に必要な構造体                       → PW_1 STATE 待ちの 1-tick sleep が永久に戻らない(元のハング)
```

さらに RC-2 により、wait-queue で一度でも眠ったスレッドは以後 **IF=0 で走り続け**、それが BSP(CPU 0、グローバル tick を進める唯一の CPU)上だと周期割込みが消失してグローバル時計が止まります(実測: 0.7 s あたり CPU0 は +4 tick、他 CPU は +72)。tick 期限のスリープが全て「たまたま割込みが入るまで」待たされる状態でした。

## 証拠(全て実機ログ)

1. **停止点の特定**: `parity_wait_reg` slow 段に計測を入れ、`SLEEP reg=0x45404 (PW_1 STATE) tick=599 dl=600 cpu=0 if=1` の後 `WOKE` が出ない。2 ms で ACK timeout になる経路なので HW ではない。
2. **tick 枯渇の実測**: 別 CPU のウォッチドッグ(自 CPU の LAPIC tick を時間源)で CPU 0 だけ tick が止まることを確認。`WOKE ... if=0` から RC-2 を特定(`dispatch.S` が RFLAGS を pushfq/popfq で保存復元するため IF=0 が持ち越される)。
3. **RC-2 修正後の別停止**: QEMU モニタで 4 vCPU 全てが `asm_hlt`(IF=0) = パニック。スクリーンダンプに `amd64 fault v=14 rip=0 err=0x10 cr2=0`。fault ハンドラ診断で **tid 2 = `wlan_retirement_worker`**(i915 無関係の周期スレッド)が壊れた戻り先へ `ret`。
4. **`-fstack-usage` で全カーネル測定**: parity の 3 関数だけが 131–137 KiB、他は全て ≤ 4.6 KiB。`AMD64_SYS_STACK_SIZE = 16384`。

## 変更ファイル

- `src/kern/sched.c` — `sched_sleep_locked` / `_interruptible` / `_notify`: `enabled = hal_irq_disable()` … 復帰後(早期 return 含む) `if (enabled) hal_irq_enable()`。
- `platform/amd64/vmunix.mk` — `AMD64_CFLAGS += -Wframe-larger-than=8192`(`-Werror` 下でビルドエラー化。現状の最大は devfs の 4,568 B)。
- `src/drivers/gpu/i915/parity/display_core.c` — 共有 static `dc_trace`。
- `src/drivers/gpu/i915/parity/ktest.c` — trace リング/fake MMIO 等の大きなローカルを static(テストは単一スレッドで直列)。
- `src/drivers/gpu/i915/parity/probe.c` — `intel_gt_init_mmio` を **P1 に復元**(参照位置)。
- 調査用の診断コード(ウォッチドッグ、sleep 計測、fault ハンドラ診断、CPU 別 tick 計数、ウェル毎ログ)は**全て除去済み**。

HAL インタフェース(10 ms tick / `kernel_timer_handler` / `KERN_CLOCK_HZ` / waitq 期限単位)は不変。git commit/push はしていません。

## 検証

クリーン再ビルド(`build/amd64/kern64` 削除後、vmunix `8c8bf007`、.bss 4.67 MB、stale object 0)で:

- **GPU-free ktest: 273 checks / 0 failures**(P60-FAULT の期待値を正本の rmw 挙動に合わせて修正済み)
- **実機(P1 配置のまま)**: P1 `gt_init_mmio`(engines=5, engine_mask=0x10503, l3bank=0x7)→ P3 `init_hw` 完了(cdclk=179200)→ `probe_noirq COMPLETE` → P5d → **P6-b `hw: pat=1 gt_wa ok=4 mismatch=0 mocs 64/32 whitelist_writes=60`** → 設計上の P6-c 停止点(`BLOCKED where=intel_engines_init`)。693 行、パニック無し。post-attach ktest も 273/0。
- ログ: chaos `~/bigbang/run-parity-hw-e91.log` / `run-parity-nogpu-e91.log`。

参考(修正の効果の直接証拠、計測版での比較): 修正前は PW_1 の 1-tick スリープが戻らず停止 / IF 復元後は `WOKE ... if=1` で毎回 1〜3 tick で復帰、CPU 0 の tick 消失も解消。

## 補足(スコープ外だが記録)

- RC-2 は i915 に限らず全カーネルスレッドに効いていた欠陥です(wait-queue を使う全スレッドが以後 IF=0)。修正により本来の割込み応答・プリエンプション挙動に戻るため、他サブシステムで新たに露出する問題があれば別途扱いが必要です(本セッションの範囲では P6-b まで問題なし)。
- 同じ理由で `wlan_retirement_worker` のクラッシュは RC-1 の被害であり、WLAN 側に修正は入れていません。

## 次

P6-c `__engines_record_defaults`(最初の GPU コマンド実行 = null request、承認③済み)→ P7 前半 → 実機 EU 試験(要・明示解除)。
