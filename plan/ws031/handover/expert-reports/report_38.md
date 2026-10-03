# WS031 第38報 — 単位 A(PCI probe runtime PM)+ B(時間 fault 4試験)+ C(power-well 操作本体)を完成。ktest 119/0、実機 reached=P3 BLOCKED intel_power_domains_init_hw

作業順 A→B→C→D のうち **A・B・C を実装し実機検証**しました。build 0 error/warning、GPU=vfio-pci、attach 先行維持、電源表(30 wells / allowed_dc=0x4000000a / target=0x2)保持。**残る D(intel_power_domains_init_hw(false))は §5**。

実機(参照条件、host FLR 後 一回起動、image b36f8a5d):
```
A64 TIMECOUNTER CAS-SELFTEST 120-then-110=PASS 110-then-120=PASS   (B 試験1)
A64 TIMECOUNTER KVM clock-features eax=0x1007efb (cs2=1) -> PIT-calibrated TSC
A64 TIMECOUNTER READY source=pit / boot CPUs ready 4 / panic 0
i915: parity PM probe get_sync: cfg_vendor=0x8086 usage=1 active=1 resume_rc=0  (A)
i915: parity attach end: reached=P3 outcome=BLOCKED where=intel_power_domains_init_hw
i915: parity teardown: PCI probe runtime PM released (usage=0)                  (A)
i915: parity ktest: 119 checks, 0 failures   (A×4 + B×4 + C×14 追加)
runner-result: selftest=PASS last_op=intel_pmdemand_init_early
               blocked_at=intel_power_domains_init_hw cleanup=1 published=0
```

---

## 1. 単位 A — PCI probe を囲む runtime PM 契約(完成)

既存 osdep_rpm を runner/probe の間へ接続(新 PM 実装は作らず):
- **local_pci_probe 相当**:attach 冒頭(P0 の前、osdep_pci_init 直後)で **osdep_rpm を get_sync → device を D0 へ resume**、teardown の最後(全 MMIO/PCI 解放後)に put。P3 の中で遡って取得はしません。
- **3 参照を別管理**:① device 生存参照(runner の g_runner.device)② PCI probe runtime PM 参照(今回新規、get_sync/put)③ i915 runtime_pm(P0.3 の init_early)。
- **resume は実処理**:新 backend の resume = `osdep_pci_set_power_state(D0)`(PM cap 有→PMCSR RMW / 無→正当な no-op)。空 stub ではありません。逆に既に D0 のとき(osdep_rpm の active=1)は resume を skip し、**余分な D0 書き込みを加えません**。
- **get_sync と resume_and_get を区別**:get_sync は resume 失敗でも usage を残す(teardown で解放)、resume_and_get は失敗で usage を巻き戻す — 契約どおり。継続(publish)経路では参照を次所有者へ渡す旨をコメント化(診断は常に teardown ゆえ put)。
- 実機:`PM probe get_sync: cfg_vendor=0x8086 usage=1 active=1 resume_rc=0` / teardown `PCI probe runtime PM released (usage=0)`。
- 試験(fake PM backend、+4):正常 get_sync(usage++/active)/ put / **get_sync は resume 失敗で usage 残す** / **resume_and_get は resume 失敗で usage 巻き戻す**(区別)。失敗経路は active=0 を与え backend を実際に呼びます。
- 注:command=0xffff の解消は予告しません(ゲスト側 PM 契約の再現であって host VFIO 状態を直接保証しません)。selftest 順序比較や host 電源設定比較の試験は追加していません。

## 2. 単位 B — 時間 fault の注入口と既決 4 試験(完成)

HAL 全体の改造待ちにせず、小さな注入境界を作り、**実 wait/udelay/reset 本体**で試験しました。
- **試験1(CAS)**:guarded reader が実使用する更新 helper `amd64_timecounter_monotonic_max(slot, raw)` を抽出し、boot self-test を追加。**120→110 で共有最大が後退せず(PASS)、110→120 で前進(PASS)**。実機ログに出力。
- **注入境界**:wait.c に `parity_time_test_ops{read, slow_sleep_override, slow_sleep_rc}` + `parity_wait_test_set()`。本番は NULL で実 HAL counter、試験は別 context の応答列。試験後 set(0) で **実 probe の時間源を壊しません**。
- **試験2〜4**(fake MMIO + scripted time):
  - counter 読出失敗 → **-EIO**(正常完了/HW -ETIMEDOUT に化けない)。
  - frequency 変化 → **-EIO**。
  - reset 後の udelay で counter 失敗 → `parity_gt_reset_all` が **-EIO で中断、lock/forcewake 解放、通常 timeout retry せず**。
  - slow-stage の wait API 異常(EINVAL)→ 無視せず **-EIO 伝播**。
- PIT 較正 TSC は保持。pvclock backend(cs2=1 確認済)と高分解能 sleep-range(clocksource≠clockevent)は正本移植範囲に残置(§5、pvclock と 4 試験は別記録)。

## 3. 単位 C — power-well / domain 操作本体(完成)

- **低水準 ops と refcount ops を分離**:`enable/disable/is_enabled/sync_hw` と `get/put` を別関数。**get() は 0→1 で enable() を呼び、明示 enable()(display-core 用の別入口)は refcount を触りません**。
- **ops 本体(MMIO)**:driver 制御レジスタ族別(hsw=0x45404 / icl_aux=0x45444 / icl_ddi=0x45454)、REQ=`0x2<<(idx*2)` / STATE=`0x1<<(idx*2)`。enable=REQ set → **ACK(STATE)待ち=parity_wait_reg** → post_enable。disable=REQ clear → STATE clear 待ち。is_enabled=STATE 読取。**sync_hw は is_enabled を呼んでから記録**(bare readback でない)。hw_enabled=-1(未同期)を真偽扱いしません。
- **省略しない 3 点**:
  1. post_enable が **has_vga で intel_vga_reset_io_mem を呼出**(vga.c に追加。legacy VGA IO 本体は模型・呼出は接続済=vgacon 対応を落とさない)、pipe mask で IRQ post-enable。
  2. IRQ post-enable は **intel_irqs_enabled() gate**(P4 前=0 で guarded no-op、guarded entry は存在、**P4 handler は前倒ししない**)。
  3. **fixed_enable_delay の固定待機は IS_DG2() 条件** — ADL-P は非 DG2 ゆえ ACK 待ち(AUX_USBC の enable_timeout=500 は反映)。
- domain get(所属 well 昇順)/ put(逆順)、途中失敗で unwind。
- 試験(fake MMIO に PW 制御レジスタ + ACK 模型/no_ack 追加、+14):single get→put / nested get 二重・put で一度 disable / **明示 enable は refcount 不変** / sync_hw が状態記録 / **PIPE_A と PANEL_FITTER_A が PW_A を共有→refcount 2、個別 put** / post_enable が PW_2(has_vga)で reset_io_mem 呼出・PW_A で IRQ gate off / **ACK 欠落= -ETIMEDOUT と time-base 異常= -EIO を区別**。

## 4. 実機・試験まとめ

ktest 97 → **119 / 0**(A+4、B+4、C+14)。CAS self-test は HAL ログ(PASS)。実機は上記のとおり P3(intel_power_domains_init_hw 直前)到達、PM 参照の取得・解放が成立、CPUs 4 / panic 0 / cleanup=1 / published=0。

## 5. 単位 D(次)— intel_power_domains_init_hw(false) 一式

正本順で実装します(実 MMIO の子処理を移し、INIT 参照・状態同期・cleanup まで一単位):
```
initializing=true
 → icl_display_core_init(resume=false):
     DC 状態無効化 → PCH 種別+reset handshake → combo PHY(intel_combo_phy_init)
     → Power Well 1(明示 enable) → CDCLK(ADL-P stepping table) → DBUF slice
     → MBUS → BW_BUDDY(P2 保存 DRAM) → 末尾 WA
 → POWER_DOMAIN_INIT 参照 取得・保持(P3 末で落とさない)
 → 追加参照 → intel_power_domains_sync_hw → initializing=false
```
- ADL-P で早期 return する関数(`gen12_dbuf_slices_config` / `icl_mbus_init` は IS_ALDERLAKE_P で early-return)はその分岐をそのまま移します(名前から新規レジスタ書き込みを作らない。ただし DBUF 有効化全体の省略ではない)。
- combo PHY / CDCLK(stepping 別 table)/ BW_BUDDY は正本の関数本体・対応表を移し、TGL 固定値へ短縮しません。resume=false は途中 DMC 再ロードを行わず、DMC 初回は親関数の後。
- **未実装子処理は先に実装してから実 GPU 接続**(半分変更後に未実装 callback へ到達する試験手順にしない。code+fake backend で呼出関係を先に閉じる)。
- **cleanup 状態機械**:power_map_initialized / power_hw_init_started / power_hw_init_completed / init_wakeref_owned / dmc_initialized。正常 HW 後の終了は `intel_power_domains_driver_remove()` 対応(well を有効のまま残す+runtime PM 参照処理、全 enable 逆順 disable の独自手順にしない)。途中適合層 error は最後完了操作+保有参照を記録、実 HW timeout と未実装/時間基盤異常を区別。
- **並行**(C を待たせない範囲で):ROM resource(PCI_ROM_RESOURCE、guest drv_pci 層拡張)、pvclock backend(cs2 確認済)、高分解能 sleep-range、VGA legacy-IO 本体、有効 VBT parse 深化。

到達目標:intel_power_domains_init_hw(false) 完了 + INIT 参照を保持したまま intel_dmc_init 入口へ。停止時は親関数名でなく正確な子関数を停止点にします。

**保持**:MSI 分離 / runner / 共有 backend / 既存試験、per-pipe vblank、時間 error 伝播、VGA 登録、電源 map(30 wells)+操作本体、PCI probe PM 契約、時間 fault 試験。GPU=vfio-pci、drm 非 blacklist、attach 先行。描画再試験・GPU-hang 探索へは戻りません。

`last_completed_op` = intel_pmdemand_init_early、`blocked_or_failed_op` = intel_power_domains_init_hw、保持 wakeref = PCI probe PM(取得→解放を実機確認、init 後の POWER_DOMAIN_INIT wakeref は D で発生)、cleanup=1 / published=0。
