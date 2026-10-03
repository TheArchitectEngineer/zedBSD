# WS031 第41報 — D1 是正:PCODE 再試行の待機・preemption 契約を正本へ(適合層に preempt 制御 + usleep_range を追加)。ktest 128/0、boot 健全

ご指示の「今すぐの一手」= **PCODE の追加 50ms を preemption 有効の busy poll のままにしない**を実施しました。適合層に **preemption 制御**と **usleep_range(高分解能待機)** を追加し、PCODE 再試行を正本の三層へ分けました。combo PHY・CDCLK は次の主成果(§4)。

実機(参照条件、FLR 後 一回起動、image 812b7333):**CPUs ready 4 / panic 0 / ktest 128/0** / reached=P3 BLOCKED intel_power_domains_init_hw。scheduler 変更が boot を壊さないことを確認。

---

## 1. 適合層(kernel)に追加した二つの機構

これは「sleep-range 未実装だから preemption 制御も省く」ではなく、**別々の依存**として実装しました。

### kern_preempt_disable / kern_preempt_enable(src/kern/sched.c, sched.h)
- `sched_cpu` に **preempt_count**(nesting 深さ)を追加。disable=count++、enable=count--(0 かつ resched 保留なら sched_yield)。
- **scheduler tick の preempt 判定を `preempt_count != 0` で defer**(need_resched を残し、enable 時に解消)。入れ子を扱い、**全出口で入口の状態へ復元**します。
- **IRQ 禁止とは別**(preemption のみ無効化。この区間のために IRQ を長時間無効化していません)。
- boot 健全(CPUs 4、panic 0)で scheduler 変更の回帰なしを確認。

### kern_usleep_range(min_us, max_us)(src/kern/clock.c, clock.h)
- monotonic counter による **min_us の bounded 高分解能待機**。µs 規模の usleep_range に供します(10ms tick 待ちにも、無間隔 busy loop にもしていません)。
- **真の yielding hrtimer sleep-range は HAL の保留項目**であり、そこへ接続する µs 待機の入口としています(PCODE 内へ新しい TSC 処理は追加していません)。

## 2. PCODE 再試行を正本の三層へ(pcode.c)

| 層 | 実装 |
|---|---|
| 一回の mailbox 取引 | 現行 500µs / slow=0 の取引処理を保持 |
| 通常再要求区間(_wait_for) | `parity_pcode_poll(sleep_between=1)`:未成立時に **kern_usleep_range(10,20)** で sleep 可能待機 |
| 追加再要求区間(preempt off, 50ms) | **kern_preempt_disable() → poll(sleep なし, 50ms) → kern_preempt_enable()** |

- 追加区間は **genuine な preemption 無効**(scheduler preempt-count)で、sleep しません。**IRQ を無効化していません**。全出口で preempt 状態を復元し sb_lock を解放します。
- 期限に達しても条件を確認してから timeout を確定する順序を保持。**時間源異常(-EIO)は即停止**(通常 PCODE timeout に化けさせない、preempt と mutex を復元)。**PCODE status errno はそのまま伝播**(C の「ACK 警告継続」を PCODE 全体へ広げていません)。
- `snb_pcode_write(mbox,val)=snb_pcode_write_timeout(mbox,val,500,0)` は変更していません。

## 3. 試験(ktest 128/0 維持)

D1 の 4 試験は通常/追加区間を経由します:**最初の要求で承認(再要求なし)/ 数回後に承認(毎回正しい request)/ 恒常 PCODE エラー→errno 伝播**(通常区間 timeout → **追加区間 preempt-off 50ms** → errno 返却)/ **時間基盤異常→-EIO 停止**(preempt と mutex を復元)。追加区間 + preempt disable/enable の balance は PCODE-error 試験が実際に実行(50ms の preempt-off 区間を通過、boot 健全 = scheduler の defer が正しく動作)。fake の応答列で確認し、実時間の 50ms を各試験で消費する構成にはしていません(PCODE-error 試験のみ追加区間を通ります)。

## 4. 次の主成果:combo PHY と CDCLK

- **combo PHY**:`intel_combo_phy_init → icl_combo_phys_init`(対象 PHY 列挙 → `icl_combo_phy_verify_state` → 不一致なら procmon 参照値・PHY_MISC・lane/group・master・COMP_INIT を正本の順序で初期化。適切なら再初期化を省く。enable bit だけで全体正常判定せず、毎回全 PHY 書き直しにもしない。master 判定/lane・group reg を保持)。fake 3 ケース(全適切=書込みなし / COMP_INIT 立つが procmon 不一致=初期化実行 / 初期化要=正本の対象・mask・順序)。
- **CDCLK** 三段:
  1. hook・table:`intel_init_cdclk_hooks`(ADL-P stepping 別 table/callback、PCI revision 生値を display stepping にそのまま使わない、table を選ぶだけで PLL を前倒ししない)。
  2. 現値取得・sanitize:`intel_cdclk_init_hw → bxt_cdclk_init_hw → bxt_sanitize_cdclk`(現在値取得 → table/VCO/divider と照合 → 適切なら変更せず終了)。読み値・sanitize 変更・要求値を区別保存(table を構造体へ代入しただけで HW readback にしない)。
  3. 変更:計算 → **PCODE 変更準備要求(§2 の request、失敗なら後続 PLL へ進まない)** → PLL/divider 設定 → PCODE 変更後通知 → cdclk.hw 保存。変更後通知の失敗と準備失敗を同一の「何も変更しなかった」にまとめない。読み戻せない voltage level は要求値保持を区別。
  - fake 4 ケース:変更不要 / 変更成功 / 準備要求失敗(PLL へ進まない、書込み記録で確認)/ 変更後通知失敗。
- **D0 補完**(fuse 実 poll / DC_off gen9_set_dc_state・gen9_dc_off ops / VGA vga_get・vga_put)と **D3**(icl_display_core_init(false) 親順 + POWER_DOMAIN_INIT 参照保持 + 全 well 個別 sync_hw + cleanup driver_remove 対応)は既定範囲で続行。D0/D2 が互いに呼ぶ共通処理(DC_off→CDCLK/DBUF/combo PHY)は一つの本体へ接続(簡略版を二重に作らない)。

**受入**:上記を fake で結線後に、参照条件で **一回** 実 GPU 実行(init_hw(false) 完了 + INIT 参照保持のまま intel_dmc_init 入口。DMC 未実装ならそこで正確停止。P3 全体完了/描画ではない)。**D 未完中は GPU を渡さず GPU-free 試験のみ**(同じ停止点までの定期 attach 確認起動は省きます)。

**保持**:MSI 分離 / runner / 共有 backend / 既存試験、per-pipe vblank、時間 error 伝播、VGA 登録、電源 map(30 wells)+是正 ops、PCI probe PM、時間 fault 試験、共通 PCODE。GPU=vfio-pci、drm 非 blacklist、attach 先行。描画再試験・GPU-hang 探索へは戻りません。

`last_completed_op` = intel_pmdemand_init_early、`blocked_or_failed_op` = intel_power_domains_init_hw、cleanup=1 / published=0。次報に combo PHY 本体 + CDCLK の実装差分を添えます。
