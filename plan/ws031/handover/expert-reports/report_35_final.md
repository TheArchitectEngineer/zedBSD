# WS031 第35報 — 時間基盤の是正(T)+ vblank per-pipe worker(D)+ intel_bios_init 実処理(P)。実機 attach が **P3(frontier=intel_vga_register)** へ到達

第34報後の専門家是正を **T/D/P 並行**で実装し、実機(参照条件)で **実デバイス attach が P0→P3** に到達しました(reached=P3, BLOCKED where=intel_vga_register)。GPU-free selftest は **ktest 83 checks / 0 failures**、build 0 error / 0 warning、HAL timecounter `source=pit`。boot CPUs 4 / 4089MB / panic 0 / cleanup=1 published=0。**GPU=vfio-pci 維持、drm 非 blacklist。**

> 途中、実機 attach が P0 `command=0xffff`(config 全 1)→ pci_bar EINVAL となりましたが、**コードではなく実行順の問題**と特定・解消しました(§6)。パススルー IGD が idle 時に D3 自動 suspend し config が 0xffff を返すため、所要時間の伸びた selftest の後に走る attach が suspend 窓を超えていました。**runner を attach 先行へ並べ替え**て解決。host デバイスは終始健全(uptime 2 日=E-65 と同一環境、setpci COMMAND=0x0003、FLR 可)。

---

## 1. T① — last_sample を競合安全な CAS 単調最大へ(最優先, 実装済)

`amd64_timecounter_read_guarded` の clamp を、単発 `atomic_store`(compare-and-write 全体を atomic にしない=2CPU が last=100 を読み 120 と 110 を書いて共有値が後退し得る race)から **64-bit compare-exchange retry ループ**へ置換:

```c
/* state->lock(IRQ無効下の exchange spinlock)で直列化済=IRQ安全。CAS ループは
 * 外側 lock 無しでも「最大へ前進」を保証(後着の小さい値が共有値を後退させない)。*/
uint64_t last = __atomic_load_n(&state->last_sample, __ATOMIC_RELAXED);
for (;;) {
    if (raw > last) {
        if (__atomic_compare_exchange_n(&state->last_sample, &last, raw, 0,
            __ATOMIC_RELAXED, __ATOMIC_RELAXED))  break;   /* 共有最大を前進 */
        continue;                                          /* last 再読込→再評価 */
    }
    if (raw < last) { /* published 最大へ clamp、初回のみ REGRESS 診断 */ }
    raw = last;  break;
}
```
層の分離を明示:(a)共有 last_sample の非後退 = CAS。(b)時計妥当性 = 候補選択 / CPU 間換算・検証 / 進行異常処理(別レイヤ)。「permanent-disable をやめた=以後常に有効」にはしていません。

## 2. T — KVM 時計 feature(**未着手・次バッチ**)

CPUID 0x40000001 EAX 診断(CLOCKSOURCE/CLOCKSOURCE2/STABLE_BIT)と CLOCKSOURCE2 時の pvclock backend は未実装。現状は **PIT 較正+CPU 間検証を「検証条件付き共通 backend」**として採用(`source=pit`=PIT で較正した TSC 値、毎待機で PIT を読まない)。「KVM signature ゆえ raw TSC 信頼」分岐は正式選択に無し。次報で候補選択(counter=tsc / calibration=pit)の区別を含め実装します。

## 3. T — wait.c + delay 呼出元(実装済)

- **(3.1) waitq_sleep() 戻り値を実処理**: `0/ETIMEDOUT/EAGAIN`=一tick間隔満了 or 起床→レジスタ条件+全体期限を再確認(ループ継続、per-interval 満了を HW timeout に変換しない)。それ以外(EINVAL/EBUSY 等 API 異常)=`WAIT_API` fault latch → `-EIO`。waitq_sleep 実装確認:flags=0 は 0 か ETIMEDOUT のみ返す。
- **(3.2) parity_udelay を error 返却化**(`void`→`int`, -EIO)。唯一の呼出元 **reset.c** は失敗で uncore lock 解放+reset 中断、retry へ入らない(forcewake は共通 exit で解放)。
- **(3.3) parity_time_base_fault を atomic/初回原因保存**(`volatile int`+CAS で初回のみ NO_COUNTER/READ_FAIL/WAIT_API を latch)。
- **(3.4) 待機毎に (counter,frequency) 一組保持**(`read_counter_consistent`:freq 変化 or 読取失敗を READ_FAIL 検出し差分計算を続けない)。
- wait.c 4 fault 試験(時間源失敗/競合/delay 失敗で呼出元停止/slow 異常返却)は **次バッチ**(HAL 側 fault-injection seam と共に)。

## 4. D — drm_vblank_init を per-pipe worker で完成(実装済)

device 単一 worker を撤去し、`parity_drm_vblank_crtc` 各々に worker を埋込(正本 drm_vblank_worker_init は CRTC 毎)。**正本安全順**(各 pipe):lock/waitq/pipe#/count → disable timer → seqlock → **cleanup 登録 → worker 生成**。cleanup を worker より先に登録するので、drmm 満杯 or worker 生成失敗でも dev_fini が全 pipe 逆順回収(cleanup は worker_created=0 を許容、worker 破棄→timer 同期削除→seqlock/inited クリアの順)。ログ `vblank_slots=4`。

## 5. P — intel_bios_init 実処理(実装済)

正本を構造忠実移植:list init → HAS_DISPLAY(ADL-P) → init_vbt_defaults → **OpRegion(P2 同起動状態; 今起動 ASLS=0 ゆえ真の不在、固定 NULL でない)** → **not IS_DGFX ゆえ SPI 実行せず** → **PCI ROM 実読**(config 0x30、base==0 は真の不在で即 return、非 0 なら size probe+`hal_space_map_device`+"$VBT" scan+`intel_bios_is_valid_vbt`+copy)→ 有効なら BDB header/version/block walk / 不在なら **init_vbt_missing_defaults**(ADL-P: PORT A/B/C=PHY_A/B/C→生成、D/E/F=TC=PHY_F..H→skip、3 子device、A=INTERNAL|DP・B/C=TMDS|DP、version=155)→ parse_ddi_ports。**VBT 創作・QEMU ROM 変更なし。未実装でなく実読取結果としての不在**。frontier→`intel_vga_register`。

## 6. 実機検証(参照条件)と 途中ブロッカーの解消

### 6.1 GPU-free selftest: ktest 73 → 83 / 0
- D(per-pipe):正常 4CRTC+per-pipe worker / dev_fini 全 worker 破棄 / CRTC 数 0・過大拒否 / add_action 満杯即実行 / cleanup 登録不可 unwind / **per-pipe worker 生成失敗 unwind**(fault 注入 pipe2→num_crtcs=2、先行 worker 回収)。
- P(bios, +8):is_valid_vbt 受理/NULL・短小・不正署名拒否 / process_vbt が BDB header+block walk(version=200, blocks=2)/ missing_defaults 3 子device・version155 / PORT_A=internal 非TMDS / top-level が fake-pci(ROM BAR=0)+OpRegion 不在で既定 fallback。

### 6.2 途中ブロッカー(command=0xffff)の根因と解消
実機 attach が当初 P0 `command=0xffff` → pci_bar EINVAL。診断:
- host デバイスは健全(`setpci COMMAND=0x0003`, `reset_method=flr pm`, FLR/D0 可, **host uptime 2 日=E-65 と同一環境で再起動なし**)。→ wedged ではない。
- **診断ビルド(runner の selftest を一時 skip=attach 先行)で attach 単独が P0 `command=0x0007`→P1→P2→P3 BLOCKED intel_vga_register に到達**。→ **parity コードは正しい**。
- 差分は selftest の所要時間のみ。パススルー IGD は **idle 時 D3 自動 suspend**(config→0xffff)。E-65 は 73 checks で attach が suspend 窓内、E-66 は 83 checks+per-pipe kthread churn で所要時間が伸び窓超え。selftest は全 fake backend で実デバイス非接触ゆえ、時間だけが差。`disable_idle_d3=Y`/`power/control=on`/FLR→D0 では guest 実行中の suspend を防げず不変。
- **解消 = runner を「attach 先行 → selftest 後」へ並べ替え**(device-sensitive な attach を起動直後 D0 のうちに実行、selftest は GPU-free ゆえ後段)。parity 初期化は不変、順序のみ。

### 6.3 実機再確認(image sha 4efb69c7, host FLR 後 一回起動)
```
A64 TIMECOUNTER READY cpus=4 source=pit hz≈2.497GHz
boot: HAL cpu 4, memory 4089MB / CPUs ready 4 / panic 0
--- 実デバイス attach ---
P0 pci_enable_device ok (command=0x0007)
P1 uncore BAR mapped (8388608 bytes)         ; 実 BAR0 半分
P1 device_info: slice_fuse=0x1 dss_fuse=0x1f(5 DSS)  ; 実 fuse
P2 pci_set_master ok (command=0x0007)
P2 opregion: ASLS=0x00000000                 ; 真の不在
P2 hw_probe tail: dram_detect rc=0 bw_init rc=0 sagv=2 ; 実 DDR4/帯域
P3 drm_vblank_init: vblank_slots=4 (per-pipe worker+timer+seqlock)
P3 intel_bios_init done: source=0 vbt_found=0 version=155 child_devices=3
attach end: reached=P3 outcome=BLOCKED where=intel_vga_register err=0
teardown: DRM+vblank released / MSI released / WC unmap rc=0
--- selftest ---
ktest (completion+workqueue): 83 checks, 0 failures
runner-result: selftest=PASS probe=BLOCKED last_op=intel_vga_register
               blocked_at=intel_vga_register cleanup=1 published=0
```
→ **実機 attach が P0→P3(per-pipe vblank + intel_bios_init 実処理)に到達し、frontier が intel_bios_init から `intel_vga_register` へ前進**。teardown 逆順成立、time-base anomaly 無し。

## 7. 次バッチ(T/D/P 継続)

- **P 続行**:`intel_vga_register` → `intel_power_domains_init`(+_hw)→ `intel_pmdemand_init_early` → `intel_dmc_init` …(GPU freq 制御/GT init 前倒しなし)。
- **T**:KVM CPUID 0x40000001 feature 判定 + CLOCKSOURCE2 時 pvclock backend / wait.c 4 fault 試験 + **CAS race 試験**(HAL host-mock に 120/110 後退不発)/ 高分解能 sleep-range。
- 描画再試験・GPU-hang 原因探索へは戻りません。**保持**:MSI 分離 / runner / 共有 backend / 既存試験、GPU=vfio-pci、drm 非 blacklist。

`last_completed_op` = intel_vga_register(実デバイス attach, P3 到達直前の frontier)、`blocked_or_failed_op` = intel_vga_register、cleanup=1 / published=0。
