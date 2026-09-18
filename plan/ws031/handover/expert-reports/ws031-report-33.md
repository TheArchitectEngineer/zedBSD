# WS031 第33報 — ① legacy PS描画一回(hang再現・記録) / ② completion 同一CPU IRQ安全化 / ③ wait.c 修正(+TSC実機知見)。④ は着手中

ご指示の作業順に沿って ①②③ を完了し、実機(参照条件 4GiB/4vCPU/host-phys-bits-limit=39)で検証しました。**④(DRM device 管理 + drm_vblank_init)は着手中**です。③ で **時間源に関する重要な実機知見**があり、方針を確認いただきたく先に報告します。build 0 error/warning、drm 非blacklist、GPU vfio-pci 維持。台帳 E-60〜E-62。

添付: 変更した `wait.c`（③ の本体、TSC 自前較正を含む新版）。

---

## ① legacy PS描画 一回（現ビルド・独立起動）= timeout（PS段 stall 再現）

現ソースのビルド（現 HAL/CPU init/PAT完全手順込）で **CONFIG_DRIVER_PCI_I915_PARITY=n（parity probe 非同時実行）**、`CONFIG_DRIVER_PCI_I915_SELFTEST=y`、既存 `drv_i915_draw_selftest`（RCS0 VS+PS オフスクリーン描画）を **一回だけ**。EU compute 試験は skip（ハング前に到達させないため）、store/clear/rcs/rt は非EU/render prep として保持。

- **test_name=legacy_ps_once_current_build / parity_path_executed=false**。build id: vmunix 先頭 623df460b0edb359。
- prep PASS: bcs0 store / clear(px 全て 0xffff0000) / rcs / rt sba。
- **draw = timeout（提出後 期限まで未完）**: markerA=0xa5a50001, markerMid=0xc5c50003 **到達**、**markerPS=0x00000000, markerB=0x00000000**（PS段が完了せず）、completed=2 / seqno=3。RT pixels 全て 0x00000000（未書込）。stats readback=0xffffffff。停止状態: fuses slice_en=0x1 dss_en=0x1f eu_dis=0x0 rpcs=0x80041000、idle ss0-4 row_instdone=0xffffffff / ss5=0。記録保存 10.0.10.25:~/bigbang/legacy_ps_once_current_build.log。VM は timeout で終了。

→ **判定=timeout**。**現ビルド（T1-T5/PAT完全手順等 全部込）でも既知の PS段 stall は不変**。原因探索/リバートへは戻らず、②③④ の移植へ。

## ② 共有 completion を同一CPU IRQ で安全化（backend_sync）

avoid_cpu 回避を安全性の代わりにしない。**共有 backend を直接修正**（新 completion は作らない）。
- `parity_kcomplete/kcomplete_all/kreinit/kwait` を **spin_lock_irqsave/spin_unlock_irqrestore** 化。lost-wakeup race は既存機構（c->lock を token登録〜state=SLEEPING 間保持 + waitq sequence）で処理済。`sched_sleep_locked` は内部で hal_irq_disable + context-switch により IRQ 状態を扱う契約。deadlock 窓（同一CPUで waiter が c->lock 保持中に IRQ が complete）を **保持中 IRQ 無効**で解消（naive な irqsave 包囲ではなく、この契約に接続）。
- 受入: hook に **require_cpu** を追加し、**waiter 自身の CPU の timer IRQ から共有 completion を通知して完了**（同一CPU）を検証。既存 K1-K5/counting/timeout/cancel・requeue/cross-CPU も継続 pass。4vCPU 維持。**ktest 65/0**。avoid_cpu/require_cpu は診断条件へ（安全性要件ではない）。

## ③ wait.c 修正 — と、時間源に関する実機知見

指示の 3 点（slow待機を参照 sleep へ / 時間源不在を隠さない / us_to_ticks の overflow・切上げ）を実施。加えて **重要な実機知見**を発見・解決しました。

### 知見: `kern_rtc_read_counter`（validated TSC timecounter）は KVM 4vCPU で不安定
- 診断ログで、probe 開始時 `time-base probe: ok=0 freq=0`。一方 ktest 早期の reset 試験では時間源が有効。= **HAL の validated timecounter は較正が遅く、かつ multi-vCPU/IRQ stress 後に guard が自己無効化**し、freq=0/false を返す時がある（同一 thread でも呼ぶ時点で結果が変わる）。
- そのままだと「時間源不在」を毎回検出して probe が停止してしまう。

### 解決: 自前 TSC 較正 + 生 rdtsc
- runner thread 開始時（**IRQ 有効・sched_ticks 進行・IRQ-off wait より前**）に、生 `rdtsc` を **scheduler tick 10 ticks（100ms）**にわたり測定して **TSC 周波数を算出・キャッシュ**（`parity_wait_init`）。以後の wait は **生 rdtsc（lfence;rdtsc;lfence、常時読取可・IRQ非依存・guard無効化の影響なし）+ キャッシュ freq**。実機 **freq=2,501,787,070 Hz（≈2.5GHz）安定**。
- reset は spin_lock_irqsave（IRQ off = CPU pin）ゆえ rdtsc delta に cross-CPU rebase 不要。PCODE fast は短時間。
- **slow 側 = sleep 可能待機**（sched_yield 代用を撤去。`kern_deadline_after` + `waitq_sleep` の 1 tick 間隔 sleep。mutex 保持中 sleep 可・spinlock 不可。reset は slow_ms=0 で sleep 経路に入らない）。
- **us_to_ticks**: 乗算 overflow は飽和、変換は **切上げ**（要求より短い待ちにしない）。
- **時間源不在を隠さない**: freq 未較正は **-EIO（time-base anomaly）**（通常成功/-ETIMEDOUT に混ぜない）。probe 開始前に検査し、不在なら FAILED where=time_base_anomaly。pcode は wait の戻り値を**そのまま伝播**（下位 -EIO を一律 -ETIMEDOUT へ変換しない）。
- **dram_bw**: ct<=0（算出不能）を追跡して **bw->valid=0** に反映（正常入力の式は不変）。

### 実機結果
`TSC self-calibrated freq=2501787070 Hz` → **ktest 65/0**、reset attempt=0 passes=2 rc=0、dram rc=0 / bw rc=0 sagv=2、**reached=P3 BLOCKED drm_vblank_init**、boot CPUs 4、time-base anomaly 無し、cleanup=1 published=0。

**ご確認**: HAL の validated timecounter が本環境で不安定なため、**parity 内で TSC を自前較正（sched_tick 基準）して rdtsc を用いる**方針に変更しました。HAL 側の timecounter は変更していません。この自前較正でよいか、あるいは HAL timecounter 側の改善を別途行うべきか、ご指示ください。

## ④ DRM device 管理 + drm_vblank_init（案A）— 着手中

参照 `intel_display_driver_probe_noirq` の最初の依存 `drm_vblank_init` に向け、**作業A（drm_dev_init 相当の device 管理基盤を元の生成位置へ、P2 の device 状態は保持）→ 作業B（drm_vblank_init + drm_vblank_worker_init を一単位で）→ 作業C（BIOS/VGA/power domain へ）** を、正本の関数単位で移植中です。次報で drm_vblank_init の実処理完了と BIOS/power domain 到達を報告します。

以上、①②③ 完了のご報告と、③ の TSC 自前較正方針のご確認をお願いします。④ は継続します。
