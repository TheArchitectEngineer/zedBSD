# WS031 第35報 — 時間基盤の是正(T)+ vblank per-pipe worker(D)+ intel_bios_init 実処理(P)

第34報後の専門家是正を **T/D/P 並行**で実装しました。GPU-free selftest は **ktest 83 checks / 0 failures**、selftest 内 probe は **reached=P3**(per-pipe vblank + intel_bios_init 実行)。build は 0 error / 0 warning、HAL timecounter は `source=pit` 継続。

**重要な保留(下記 §6):** 実機の *実デバイス attach* は P0 の最初の実 config 読取で **`command=0xffff`(config 全 1 = GPU 無応答)** となり P1 `pci_bar` で EINVAL。全変更点は P3(この失敗点より後)で、`pci` は static ローカル(スタック非依存)ゆえ真のデバイス読値です。**パススルー GPU が wedged 状態**で、host 側 PCI reset / 再起動が必要ですが本 session は sudo-free で実施できません。停留 qemu プロセスは無し。**コードは完成し selftest で実証済み**のため、GPU reset 後の一回起動で attach P0→P3 を再確認したく、reset をお願いします。

---

## 1. T① — last_sample を競合安全な CAS 単調最大へ(最優先, 実装済)

`amd64_timecounter_read_guarded` の clamp を、単発 `atomic_store`(compare-and-write 全体を atomic にしない=2CPU が last=100 を読み 120 と 110 を書いて共有値が後退し得る race)から、**64-bit compare-exchange retry ループ**へ置換しました。

```c
/*
 * This sample-and-update runs under state->lock (a global exchange lock the HAL
 * wrapper enters with interrupts disabled), so it is already serialized and
 * IRQ-safe.  The compare-exchange loop also makes "advance to the maximum"
 * correct on its own: a concurrent later-but-smaller sample can never push the
 * shared value backwards (a plain compare-then-store could).
 */
uint64_t last = __atomic_load_n(&state->last_sample, __ATOMIC_RELAXED);
for (;;) {
    if (raw > last) {
        if (__atomic_compare_exchange_n(&state->last_sample, &last, raw, 0,
            __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            break;        /* advanced the shared maximum */
        continue;         /* last reloaded; re-evaluate */
    }
    if (raw < last) {     /* clamp to the published maximum */
        static uint32_t regress_logged;
        if (__atomic_exchange_n(&regress_logged, 1U, __ATOMIC_RELAXED) == 0U)
            hal_printf("A64 TIMECOUNTER REGRESS ...");
    }
    raw = last;
    break;
}
```

- **層の分離を明示**:(a)共有 last_sample の非後退 = CAS で保証(外側 lock が無くても後退しない)。(b)時計妥当性 = capability 選択 / CPU 間換算・検証 / 進行異常処理(別レイヤ、`read_counter_consistent` ほか)で保証。外側 `state->lock`(IRQ 無効下の exchange spinlock)が直列化している事実はコメントで確認・明示し、「permanent-disable をやめた=以後常に有効」にはしていません(候補選択と guard は独立)。

## 2. T — KVM 時計 feature の扱い(**未着手・次バッチ**)

今回のバッチでは **CPUID 0x40000001 EAX 診断(CLOCKSOURCE bit0 / CLOCKSOURCE2 bit3 / STABLE_BIT bit24)追加**と、CLOCKSOURCE2 利用可時の **pvclock backend(kvmclock.c/pvclock.c 準拠)接続**は未実装です。現状は従来どおり **PIT 較正 + CPU 間検証を「検証条件付き共通 backend」**として採用(`source=pit`)。「KVM signature ゆえ raw TSC 信頼」分岐は元々正式選択に無く、`source=pit` は「PIT で較正した TSC 値」であって毎待機で PIT を読みません。この項目は次報で、候補選択(counter=tsc / calibration=pit)と読出しの区別を含め実装・報告します。

## 3. T — wait.c + delay 呼出元(実装済)

**(3.1) waitq_sleep() 戻り値を実処理**(従来 `(void)waitq_sleep`):

```c
drc = waitq_sleep(&wq, &lk, waitq_sequence(&wq), sleep_deadline, 0u);
spin_unlock(&lk);
/*
 * 0 / ETIMEDOUT / EAGAIN -> 一tick間隔満了 or (spurious)起床: ループしてレジスタ
 *   条件と全体期限を再確認(per-interval 満了ではなくループ自身の now-base>=target
 *   が HW timeout を決める)。それ以外 -> wait-API 異常: latch して -EIO 伝播。
 */
if (drc != 0 && drc != ETIMEDOUT && drc != EAGAIN) {
    parity_time_base_set_fault(PARITY_TB_WAIT_API);
    if (out != 0) *out = v;
    return -EIO;
}
```
waitq_sleep 実装を確認:flags=0(非interruptible)では **0 か ETIMEDOUT のみ**返し、EINVAL/EBUSY は誤用時の API 異常。非ゼロを一律 HW timeout(-ETIMEDOUT)に変換していません。

**(3.2) parity_udelay を error 返却化**(`void`→`int`, -EIO):

```c
int parity_udelay(unsigned us) {
    if (!kern_rtc_read_counter(&base,&freq) || freq==0u) {
        parity_time_base_set_fault(PARITY_TB_NO_COUNTER);
        return -EIO;    /* 呼出元は lock/forcewake を解放して停止、続行しない */
    }
    ...
    if (!read_counter_consistent(freq,&now)) return -EIO;
    ...
}
```
唯一の呼出元 **reset.c** は udelay 失敗で uncore lock を解放し reset を中断、retry に入りません(forcewake は共通 exit で解放):
```c
rc = parity_udelay(RESET_SETTLE_US);
if (rc != 0) { spin_unlock_irqrestore(uncore_lock, flags); ret = rc;
    kern_logf("... time-base fault rc=%d", rc); break; }
```

**(3.3) parity_time_base_fault を atomic/共有・初回原因保存**:`volatile int` + CAS で初回のみ latch(NO_COUNTER/READ_FAIL/WAIT_API)、以後上書きしない。reader は atomic load。

**(3.4) 待機毎に (counter,frequency) を一組保持**:`read_counter_consistent(base_freq,&now)` が freq 変化(単位変更)or 読取失敗を READ_FAIL 検出し差分計算を続けない。fast/slow 各 stage が自 stage の freq を基準に一貫性検査。

**wait.c の 4 fault 試験(時間源失敗/競合/delay 失敗で呼出元停止/slow 異常返却)は未追加**:HAL 側の時間源・waitq に fault-injection seam が要り、次バッチで seam と共に追加します(この不足で D/P は止めていません)。

## 4. D — drm_vblank_init を per-pipe worker で完成(実装済)

- **device 単一 vblank_wq を撤去**し、`parity_drm_vblank_crtc` 各々に `struct parity_kworkqueue worker` + `worker_created` を埋込(正本 drm_vblank_worker_init は CRTC 毎生成)。
- **正本安全順**(各 pipe):lock/waitq/pipe#/count → **disable timer → seqlock → cleanup action 登録 → worker 生成**。cleanup を worker より先に登録するので、worker 生成失敗も dev_fini が回収(cleanup は `worker_created==0` を許容)。

```c
for (i = 0; i < num_crtcs; i++) {
    spin_init(&vc->lock,...); waitq_init(&vc->queue,...);
    vc->pipe=i; vc->count=0; vc->worker_created=0;
    vc->disable_timer_inited=1; vc->seqlock=0; vc->inited=1;      /* timer/seqlock 先 */
    rc = parity_drmm_add_action_or_reset(ddev, parity_drm_vblank_crtc_cleanup, vc);
    if (rc) return rc;                       /* 満杯: contract が当該cleanup即実行, 先行pipeはdev_finiで回収 */
    rc = parity_kworkqueue_create(&vc->worker, "parity-vblank-crtc");  /* worker 後 */
    if (rc) { ddev->num_crtcs=i; return -1; }/* cleanup登録済 → dev_fini 全pipe逆順回収 */
    vc->worker_created=1; ddev->num_crtcs=i+1;
}
```
- **cleanup(per-CRTC)**:worker 破棄(stop+join)→ timer 同期削除 → seqlock/inited クリアの順。正常・途中失敗の両方で worker/timer を使用可のまま free しません。ログ `vblank_slots=4`(mode-setting CRTC と区別)。
- 実機 log: `drm_vblank_init: vblank_slots=4 (per-pipe worker+timer+seqlock)`。

## 5. P — intel_bios_init 実処理(実装済)

正本 intel_bios_init を構造忠実に移植(bios.{c,h}):

1. display_devices / bdb_blocks list init → **HAS_DISPLAY(ADL-P=true)**(不在なら skip return)→ init_vbt_defaults。
2. **VBT 取得元の順**:OpRegion(P2 の同起動状態を反映。今起動は ASLS==0 ゆえ `opregion_vbt_present=0` = **真の不在**、固定 NULL ではない)→ **not IS_DGFX(ADL-P)ゆえ SPI flash path は実行しない** → **PCI ROM を実読取**。
3. **PCI ROM 実読(pci_map_rom 相当)**:config 0x30 の base 読取。`base==0` は ROM BAR 未割当=**真の不在で即 return**。非 0 なら size probe(1s 書込→読戻→復元)+ `hal_space_map_device` + "$VBT" scan + `intel_bios_is_valid_vbt`(header/vbt_size/bdb_offset/bdb_size の範囲検査)+ copy。ROM decode は退出時 disable。→ **「未実装で常に NULL」ではなく「実読取した結果としての不在」**。
4. 有効 VBT: get_bdb_header + version + **BDB block walk**(id/size を辿り計数、general features/definitions/driver features を記録)。
5. 不在: **init_vbt_missing_defaults**。PORT_A..F 反復、ADL-P(DISPLAY_VER 13)phy 写像で **A/B/C=PHY_A/B/C(combo)→ 生成、D/E/F=TC1..3=PHY_F..H → intel_phy_is_tc で skip**。生成子device: A=INTERNAL_CONNECTOR|DP, B/C=TMDS_DVI|DP、dvo_port=HDMIA+port。**version=155**。
6. parse_sdvo_device_mapping(DDI では no-op)/ parse_ddi_ports(parsed-or-default 子device を処理)。**VBT 創作・QEMU ROM 設定変更は無し**。

実機 log(真の不在→既定): `intel_bios_init: source=0 vbt_found=0 version=155 bdb_blocks=0 child_devices=3 missing_defaults=1`。**frontier は `intel_vga_register` へ前進**。

## 6. 試験(GPU-free)と実機

**ktest 73 → 83 / 0 failures**(+10):
- D(per-pipe 更新):正常 4CRTC+per-pipe worker / dev_fini が全 worker 破棄 / CRTC 数 0・過大拒否 / add_action 満杯で即実行 / **cleanup 登録不可で unwind** / **per-pipe worker 生成失敗で unwind**(fault 注入 `parity_drm_vblank_test_fail_worker_at(2)` → num_crtcs=2、先行 worker 回収)。
- P(bios, +8):is_valid_vbt 受理 / NULL・短小・不正署名を拒否 / process_vbt が BDB header+block walk(version=200, blocks=2)/ missing_defaults 3 子device・version155 / PORT_A=internal 非TMDS / **top-level intel_bios_init が fake-pci(ROM BAR=0)+OpRegion 不在で既定 fallback**(child=3, source=NONE)。

**実機(参照条件 4GiB/4vCPU/host-phys-bits-limit=39/vfio-pci x-igd-opregion=on,rombar=0):**
- `A64 TIMECOUNTER READY cpus=4 source=pit hz≈2.497GHz` / **ktest 83/0** / selftest-probe `vblank_slots=4` + `intel_bios_init … child_devices=3 missing_defaults=1` **reached=P3** / boot CPUs 4 / panic・fatal・triple 無し / time-base anomaly 無し。
- **実デバイス attach**: `parity P0 pci_enable_device ok (command=0xffff)` → tr[0..9] 正常 → `attach end: reached=P1 outcome=FAILED where=pci_bar err=3`。runner-result: `selftest=PASS probe=FAILED last_op=pci_bar blocked_at=pci_bar cleanup=1 published=0`。**2 起動で再現、stale qemu 無し**。
- 診断:`command=0xffff` は `osdep_pci_read16(&pci, COMMAND)`(pci は **static** ローカル)の値=真のデバイス config が全 1(無応答)。BAR も 0xffffffff となり `drv_pci_device_bar` が EINVAL。全変更点は P3(この点より後)。**原因はパススルー GPU の wedged 状態**(第34報後、実機に host reset 相当が入った可能性)。IGD は FLR 困難で、無応答が後続起動へ持続します。

**予防的修正**:per-pipe worker で `parity_drm_device`(4×64-slot workqueue)が attach スタックローカルを ~1.9KB 増やすため、`drm_dev`/`vbt_state` を **static 化**(既存 `static trace`/`static pci` と同型、attach は一度に一つ)。これは `command=0xffff`(pci=static)の原因ではありませんが、**健全デバイスで attach が P3 まで進む経路のスタック肥大**を排除します。static-fix image(sha 先頭 `86cae223`)を `10.0.10.25:~/bigbang/guest-parity.img` に staged 済み。

## 7. お願いと次バッチ

- **お願い**:パススルー GPU(0000:00:02.0)を host 側で reset(`echo 1 > /sys/bus/pci/devices/0000:00:02.0/reset`、または vfio-pci 再バインド、または host 再起動)してください。staged image で **一回起動**すれば attach P0→P3(per-pipe vblank + intel_bios_init 実行、frontier=intel_vga_register)を再確認できます。GPU-hang 原因探索・描画再試験には戻りません。
- **次バッチ(T/D/P 継続)**:KVM CPUID 0x40000001 feature 診断 + CLOCKSOURCE2 時 pvclock backend / wait.c 4 fault 試験 + CAS race 試験(HAL host-mock に 120/110 後退不発)+ 高分解能 sleep-range / P 続行 `intel_vga_register` → power domains(+_hw)→ pmdemand → DMC。

**保持**:MSI 分離 / runner / 共有 backend / 既存試験、GPU=vfio-pci、drm 非 blacklist。`last_completed_op` = intel_bios_init(selftest-probe, P3)、実デバイス `blocked_or_failed_op` = pci_bar(GPU 無応答)、cleanup=1 / published=0。
