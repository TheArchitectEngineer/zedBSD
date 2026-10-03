# WS031 第49報 — 通常 sleep を既存 10ms tick+waitq で完成(HAL 非変更)+ probe 接続。**実 ADL-P GPU で電源 HW 初期化を完了し、intel_dmc_init 入口へ到達**

ご指示どおり **HAL は変更せず**、通常 sleep を既存の 10ms 周期 tick + waitq で完成させ、probe を実本体へ接続し、**実 GPU で通常 probe を一回**実行しました。build 0 error/warning。

**到達目標を達成**しました:実 ADL-P(8086:46a8 rev 0c)上で `intel_power_domains_init_hw(false)` が完了し、**INIT 参照を保持したまま intel_dmc_init 入口で正確に停止**しています。

---

## 1. 通常 sleep の完成(HAL 非変更、既存 tick + waitq)

`kern_usleep_range()` の「残り 10ms 未満は busy-wait」分岐を撤去し、**未満了なら常に既存 waitq で次の tick を待つ**構成にしました(one-shot / dynticks / 新 timer API / HAL 変更なし)。

- 最小待機期限 T を**開始時に一度だけ**計算。早期起床でも `now + min_us` へ延長せず**同じ T** で再確認。時間源・wait API 異常は既存 fault として伝播。
- **次の tick が来ただけで return しない**:要求直後に tick が来ても、単調 counter で最小待機時間を満たしたか確認してから return。
- **10ms 粒度で要求範囲より遅れうる**ことを仕様・診断に明記(高分解能実装完了とは記録せず、低分解能実装を未実装として止めもしません)。
- **短い atomic 待機は不変**:udelay / fast poll / 単一 mailbox 取引の atomic 待機は現行 counter ベース維持。**PCODE 追加 50ms 区間(preemption 禁止・sleep なし)も不変**。
- 利用者:PCODE 通常再要求区間 `usleep_range(10,20)` と `parity_wait_reg()` slow 側が本 backend を使用。要求値と粒度を分けて記録(引数を 10000 等へ書換えず、CDCLK の 3ms prepare も保持)。起床後は必ず条件を再確認します。
- 第48報の純関数 `timer_calc`(次イベント計算)は保存のみで**本番非接続**。実 preemption / PCODE 二試験 / fuse 遅延 / D3 四ケースは回帰として保持(GPU-free **ktest 169/0** 維持、probe=NOT_RUN)。

## 2. probe 接続(UNIMPL → 実 `intel_power_domains_init_hw(false)`)

probe.c の未実装停止を、試験済み本体への呼出しへ置換:
- frontier で**実 device 状態**を構築 — `mmio` / `power_domains` / `cdclk`(PCI revision → `parity_adlp_display_step` → STEP_D0 で hook 選択)/ `pwc`(vga, irqs=0)/ `dram_info`(P2 由来)。同一 device 状態を子で共有。
- `parity_intel_power_domains_init_hw(&dcore, false)` を呼び、`fault_stop` なら FAILED(停止子名)、成功で `last_completed=intel_power_domains_init_hw` → **`intel_dmc_init` を UNIMPL で BLOCKED(INIT 参照保持)**。
- teardown に `driver_remove`(init rpm wakeref 解放・well 維持)を map cleanup の前へ追加。
- **本番入口に試験 override なし**(fake MMIO / PCODE / 時間 / VGA I/O の注入、`parity_wait_test_reset_fault` は ktest 専用で probe では未使用)。通常 probe で fault を消して先へ進む処理も入れていません。

## 3. 実機 ADL-P 実行(参照条件、image c35a1a4f)

`run-parity-ref.sh`(4 GiB / 4 vCPU / host-phys-bits-limit=39、parity 単独、attach 先行、x-igd-opregion=on、rombar=0)で一回実行:

```
CAS-SELFTEST PASS
attach P0 pci_enable_device ok (command=0x0007)
P2 pci_set_master ok / pci_enable_msi ok (msi_kept=1)
P2 hw_probe: dram_detect rc=0 bw_init rc=0 sagv=2 (QGV4点/PSF3点)
P3 drm_vblank_init: vblank_slots=4
P3 intel_bios_init: VBT無 → 既定 (version=155 child_devices=3)
P3 intel_vga_register: client=registered gmch_bridge=found ret=0
P3 intel_power_domains_init: wells=30 allowed_dc=0x4000000a target_dc=0x2 (xelpd)
P3 intel_combo_phy_init: combo PHYs A/B (0 initialised)   ← BIOS が既に適切 → 再init不要
P3 cdclk: sanitizing cdclk programmed by pre-os            ← pre-os 状態が要再設定
P3 intel_power_domains_init_hw(false) done:
     cdclk=179200 vco=537600 dbuf=0x7 init_ref=1 sync_hw=1
teardown: driver_remove (init rpm wakeref cancelled, wells kept)
        → power domains map → VGA → DRM → MSI → WC unmap → PCI probe PM (usage=0)
attach end: reached=P3 outcome=BLOCKED where=intel_dmc_init err=0
```

**実ハードウェアで実際に成立した処理**:
- **combo PHY**:verify_state が既に適切と判定 → **0 本再初期化**(正しい:既に正しい PHY を書き換えない)。
- **PW1 enable**:実 fuse status を読み、PG0 前 / PG1 後の待機を通過。
- **CDCLK**:sanitize が pre-os 状態を要再設定と判定 → 完全再設定。**実 PCU へ PCODE prepare/notify、実 DE PLL 設定**を経て `cdclk=179200 / vco=537600`。
- **DBUF**:実 slice 状態を読み、`dbuf=0x7`(S1|S2|S3)。
- **BW_BUDDY**:P2 の実 DRAM 情報から設定。
- **DC_off enable**(INIT 取得経由):DC 無効化 + CDCLK/DBUF 比較 + combo PHY 復元。
- **INIT 参照保持 + 全 30 well sync_hw**。
- 以後 **intel_dmc_init 入口で BLOCKED**(err=0、INIT 参照保持)。
- teardown 逆順成立、`driver_remove` は rpm wakeref を解放し well は有効維持(規定どおり)、PCI probe PM usage=0。CPUs 4 / panic 0。

到達点の記録:`last_completed_op=intel_power_domains_init_hw` / `blocked_or_failed_op=intel_dmc_init` / `init_wakeref=保持`。診断終了後 cleanup は取得段階に対応して逆順実行、PM 参照解放、published=0。**これは電源 HW 初期化の受入であり、P3 全体の完了や描画成功ではありません**。ログ補助:`sleep_backend=periodic_tick` / `nominal_tick_us=10000` / `HAL_timer_interface_changed=0`。

## 4. 次

**新しい frontier = `intel_dmc_init`(DMC firmware load)**。ここから先は次段の判断を仰ぎます。今回の timer 作業は完了(one-shot / 1ms モードを必須条件に加えません)。保持:D0 / CDCLK / D3 親 / 実 sleep(periodic) / 実 preemption / PCODE 追加区間 / fuse・DC_off・VGA / GPU-free 169/0。GPU=vfio-pci、drm 非 blacklist、attach 先行。描画再試験・GPU-hang 探索・baremetal へは戻りません。

次報:ご指示に応じ、intel_dmc_init(DMC)着手の方針確認、または現到達点の追加検証。実 GPU は必要時のみ一回。
