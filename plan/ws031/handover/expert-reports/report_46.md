# WS031 第46報 — D0 残(fuse 単位是正・DC_off・VGA 所有権)を本番経路へ接続 + 実 sleep backend 実装。GPU-free 検証(ktest 156/0)

ご指示の第一着手 = **fuse 待機の引数を正本へ合わせ、DC_off・VGA を D3 へ接続、その後 実 sleep を完成**を実施しました。親関数は作り直さず、残っていた依存を閉じました。build 0 error/warning。**実 GPU は渡さない構成で検証**。

実機(`run-parity-nogpu.sh`、chaos、image 2a38e46c):`CAS-SELFTEST PASS` / **ktest 156/0**(153→156)/ **probe=NOT_RUN** / selftest=PASS。ご指示の次報最低成果(**D0 残の完了**と**実 sleep backend の実装・実行結果**)を満たしています。

---

## 1. fuse 待機の単位是正(コメントの 5µs/1µs を timeout にしない)

ご指摘どおり実コードを正本へ合わせました。`gen9_wait_for_power_well_fuses()` → `intel_de_wait_for_set(SKL_FUSE_STATUS, …, 1)` の末尾 1 は timeout_ms で、`intel_wait_for_register_fw(…, timeout_ms)` は `__intel_wait_for_register_fw(…, fast_timeout_us=2, slow_timeout_ms=timeout_ms)` を呼びます。したがって **全 PG 共通で fast_timeout_us=2 + slow_timeout_ms=1**(PG0 を 5ms へ延ばしません)。`pw_wait_fuse` の `parity_wait_reg` 引数を `2u, 1u` へ修正。PW1 の処理順は正本対応(WA → PG0 fuse 前 → REQ → ACK → PG1 fuse 後 → post-enable)を保持しています。

## 2. DC_off・VGA を本番経路へ(模型・省略を介さず D3 から)

### DC_off(二つの入口を同一便利関数にまとめない)
- D3 先頭は `gen9_set_dc_state(DC_STATE_DISABLE)`。後半の POWER_DOMAIN_INIT 取得からは DC_off well の enable → `parity_dc_off_enable()`=`gen9_disable_dc_states()`:target≠DC3CO → DC 無効化 → **CDCLK を一時構造体へ読み保存済み状態と比較**(`intel_cdclk_init_hw()` を呼び直さない)→ **DBUF 実 mask を保存 mask と照合**(`gen9_dbuf_enable()` を呼び直さない)→ combo PHY 本体で復元(簡略版を作らず本体共有)。disable 側は `intel_dmc_has_payload` の有無で分岐(未初期化なら何もしない)、enabled はレジスタ(DC_STATE_EN)読。
- 共有:pwc に同じ device(cd / dbuf_slices / target_dc / allowed_dc)を接続。DC_off の確認に DBUF 初期化を呼ばないので、domain lock 下から同じ lock を取り直しません(親に巨大 lock を足していません)。
- D-NORMAL に確認を追加:INIT 取得で **実際に DC_off の ops へ入り**(DC 状態操作・CDCLK/DBUF 読出し・PHY 処理が走る)、`dc_state=0` だけで判定していません。

### VGA(登録済みフラグでなく操作期間の所有権)
`intel_vga_reset_io_mem()` = **LEGACY_IO 資源 get → VGA_MIS_R(0x3CC) 読 → 同値を VGA_MIS_W(0x3C2) へ書 → put**。client 登録とは分離(登録に依存せず実行)。取得失敗なら I/O へ進まず未取得資源を put しません。GPU-free 試験では arbiter を含む本体を実行し、**I/O accessor だけ**を fake へ向けます(`parity_vga_io_test_set`)。D-NORMAL で対象 device・get/put 対応・read 値の write を検査。

## 3. 実 sleep backend(kern_usleep_range、並行から主作業へ繰上げ)

tick = KERN_CLOCK_HZ = 100(10ms)。**絶対期限を単調 counter で一度だけ計算**(earliest = base + min_us)。1 scheduler tick 以上残る間は private wait queue に tick 期限で登録し、**`waitq_sleep()` → `sched_sleep_locked()` で CPU を明け渡します**(同一 CPU の別 thread が走る)。**早期起床は同じ絶対期限へ再待機**(延長しません)。sub-tick 残(PCODE の `usleep_range(10,20)` 等)は busy-wait で、**µs 要求を 10ms tick へ丸めません**。時間源異常で停止。`waitq_sleep()` が戻り時に wait token を除去するので、timer 状態が frame 外に残りません(取消と終了の同期は既存 waitq 契約が担保)。

- 接続範囲:fuse/well/PLL の slow(slow_ms=1)は wait.c 側で既に waitq を用いて yield 済み。**PCODE の通常再要求区間が本 backend の主消費者**です。追加区間(preempt-off)は `sleep_between=0` で sleep を呼ばない安全区間のままです(1 tick 固定待機を残す完了判定にはしていません)。udelay/atomic poll は別経路として維持。
- 受入試験(実行結果):20–25ms を co-runner(別 thread)と同時に実行 → **elapsed ≥ 19ms / sched_ticks が ≥1 前進(sub-tick busy-spin でない=yield した)/ co-runner が進行**。加えて **boot 健全**(yielding する PCODE 経路で hang せず ktest 完走)。`max_us 以内に必ず return` を要求する試験にはしていません。

## 4. 試験(ktest 153 → 156/0)

| 追加/更新 | 内容 |
|---|---|
| D-NORMAL +2 | INIT 取得で DC_off 本体へ入る(DC 無効化+CDCLK/DBUF 比較+PHY)/ VGA が LEGACY_IO を get→MIS_R 読→MIS_W 書→put |
| D-PRESERVE | 入力を S1+S2 の 2 slice にし、両方が保持される(常に S1 だけにしない)ことを確認 |
| usleep_range +1 | 20–25ms で CPU を明け渡す(≥19ms・tick 跨ぎ・co-runner 進行) |

D3 四ケース(D-NORMAL/PRESERVE/FAULT/REMOVE)は DC_off・VGA・fuse 是正込みで再通過しています。

## 5. 次(preemption/PCODE、probe 接続、実 GPU)

- **preemption 実 scheduler 試験**:同一 CPU の A/B、A 二重禁止中に実 IRQ から切替要求 → IRQ 処理は進むが切替は延期 → 内側 enable では切替らず → 最外側 enable で保留処理。**A は B の通知を blocking wait しない**(禁止したまま待つと自分で切替を止める)ため、非 blocking な診断状態と有限期限で確認。実 IRQ・切替要求・延期・最外側解除・実切替の trace を残します。
- **PCODE 追加区間 2 試験**:通常区間で未承認・追加区間で承認 → 成功、preempt/mutex を入口状態へ復元 / 追加区間で counter 読出失敗 → 通常 timeout に化けず preempt/mutex を復元して時間源異常を返す(scripted time)。
- **D3 四ケース最終再実行** → **probe.c の UNIMPL を実 `intel_power_domains_init_hw(false)` 呼出へ置換**(P0→P1→P2→P3 前半 → 同じ device で init_hw(false) → 適合層 fault なしを確認 → INIT 参照保持のまま intel_dmc_init 入口、DMC 未実装ならそこで正確に停止、先に cleanup しない)。`parity_wait_test_reset_fault()` は試験専用のまま(通常 probe で fault を消して続行しません)。
- **実 GPU 一回**:上記完了後、参照条件(4 GiB / 4 vCPU / 39-bit、parity 単独、attach 先行)で通常 probe を一回。到達目標は電源 HW 初期化完了 → INIT 参照保持 → DMC 入口(P3 全体/描画ではない)。

**保持**:MSI 分離 / runner / 共有 backend / 既存試験、per-pipe vblank、時間 error 伝播、VGA 登録、電源 map(30 wells)+是正 ops、PCI probe PM、時間 fault 試験、共通 PCODE+三層 retry、combo PHY、CDCLK、D3 親、fuse/DC_off/VGA、実 sleep。GPU=vfio-pci、drm 非 blacklist、attach 先行。描画再試験・GPU-hang 探索・baremetal へは戻りません。

次報:preemption 実 scheduler 試験と PCODE 追加区間 2 試験の関数本体・実行結果、probe.c の実接続、D3 四ケースの最終結果。実 sleep は本報で実行結果済(別記録)。`last_completed_op` は attach 未実行のため該当なし(GPU-free)。
