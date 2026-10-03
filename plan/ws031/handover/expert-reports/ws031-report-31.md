# WS031 第31報 — P1 reset完全移植 / WC全範囲 / 共通PCODE / DRAM・bandwidth を移植し STOPPED_AT_P2 到達(実機)

方針(a: ハング原因特定は保留し Linux 通常初期化経路を**実装として完成**)に従い、指示順
**P1 reset → WC全範囲 → 共通PCODE → DRAM/bandwidth → P2受入 → GPU-free試験** を実施。すべて実機(参照条件)で検証。
MSI分離・runner・共有同期backend は保持(作り直し無し)。drm 非blacklist、GPU vfio-pci 維持。build 0 error/warning。

添付: `reset.c` / `pcode.c` / `dram_bw.c`(移植した関数本体)。本文に HAL/probe の主要 hunk を掲載します。

---

## 1. 実機結果(参照条件 4GiB / 4vCPU / host-phys-bits-limit=39、GPUあり通常probe 一回)

```
boot: CPUs ready: 4                      (panic/triple-fault 無し)
parity ktest (…): 56 checks, 0 failures  (従来41 + 新規15)
parity P1 gt_reset attempt=0 passes=2 settle=64 rc=0        ← 実P1 reset(初回成功)
parity P2 ggtt_init_hw: WC aperture mapped base=0x7000000000 requested_size=0x10000000
        mapped_size=0x10000000 va=0xffffffffe1000000 attr=RW|WC
parity P2 dram: raw=0x00003420 type=0(DDR4) channels=2 qgv_points=4 psf_gv_points=3
parity P2 bw: QGV0 DCLK=2134 … QGV3 DCLK=2668 / PSF 32,48,48 / deratedbw・peakbw 算出
parity P2 hw_probe tail: dram_detect rc=0 bw_init rc=0 sagv=2(ENABLED)
parity teardown: WC aperture unmap va=0xffffffffe1000000 size=0x10000000 rc=0
parity attach end: reached=P2 outcome=STOPPED where=end_of_P2 err=0
runner-result: selftest=PASS probe=STOPPED_AT_P2 last_op=end_of_P2 blocked_at=- cleanup=1 published=0
VFIO/DMA: -22 等エラー無し
```

**hw_probe 相当(P0→P2末尾)が対象経路に未実装の省略なく成立し、診断停止と後始末まで到達しました。**
ハング改善は採点対象外(指示どおり)。

---

## 2. 移植内容(関数本体・hunk)

### 2.1 P1 reset 完全移植 — `parity/reset.c`(新規、probe.c から抽出=試験可能化)

正本 `__intel_gt_reset(ALL_ENGINES)`→`gen8_reset_engines`→`__gen11_reset_engines`→`gen6_hw_domain_reset` を、
**P1時点の空engine集合**(engine未生成=prepare/cancel iteration は空、prepare 目的の前倒し生成はしない)で移植。
FORCEWAKE_ALL 保持 / ETIMEDOUT時のみ retry(最大3) / ADL-P(IP<12.70)は **write+ack待ちを2回** / 50µs settle。
ack待ちは **fast atomic poll(非sleep)**。

```c
int
parity_gt_reset_all(struct osdep_mmio *m, unsigned timeout_ms)
{
	const uint32_t reset_mask = PARITY_GRDOM_FULL;   /* GEN11_GRDOM_FULL=0x1 */
	unsigned attempt;
	int reset_err = 62;                              /* -ETIMEDOUT until a pass succeeds */
	int rc;

	rc = osdep_fw_get(m, OSDEP_FW_RENDER);            /* FORCEWAKE_ALL */
	if (rc != 0) return rc;
	rc = osdep_fw_get(m, OSDEP_FW_GT);
	if (rc != 0) { osdep_fw_put(m, OSDEP_FW_RENDER); return rc; }

	for (attempt = 0u; reset_err != 0 && attempt < RESET_MAX_RETRIES; attempt++) {
		unsigned loops = 2u;                         /* ADL-P: two passes on success */
		unsigned settle;
		int loop_err = 0;
		do {                                         /* gen6_hw_domain_reset */
			osdep_mmio_raw_write32(m, PARITY_GDRST_REG, reset_mask);
			loop_err = parity_wait_reg_clear_fw(m, PARITY_GDRST_REG, reset_mask, timeout_ms);
		} while (loop_err == 0 && --loops);
		for (settle = 0u; settle < GDRST_SETTLE_READS; settle++)  /* udelay(50) 相当 */
			(void)osdep_mmio_raw_read32(m, PARITY_GDRST_REG);
		reset_err = loop_err;
		kern_logf("i915: parity P1 gt_reset attempt=%u passes=2 settle=%u rc=%d\n",
			attempt, GDRST_SETTLE_READS, reset_err);
	}
	osdep_fw_put(m, OSDEP_FW_GT);
	osdep_fw_put(m, OSDEP_FW_RENDER);
	return reset_err;
}
```

ack待ち poll(`parity_wait_reg_clear_fw`)は sched_ticks deadline + **spin hang-guard**。後者は
「bring-up が IRQ無効(IF=0)で走る」場合に never-clear + tick停止で無限ループするのを防ぐ安全網
(実機happy pathは value-match で即抜けるため未到達。実MMIO readは約1µsで正当resetのµsを遥かに超える範囲)。

### 2.2 WC 全範囲 aperture — `probe.c ggtt_init_hw`(先頭1ページ → mappable_end 全範囲)

`io_mapping_init_wc` 相当。base=gmadr.start、size=mappable_end 全体を HAL_SPACE_WC で map し P2寿命保持、
teardown で同範囲 unmap(**BAR範囲の CPU view。aperture相当RAMは確保しない**)。

```c
void *wc_va = NULL;
int wc_rc = hal_space_map_device((hal_physaddr_t)gmadr_start,
	(size_t)mappable_end, HAL_SPACE_READ | HAL_SPACE_WRITE | HAL_SPACE_WC, &wc_va);
if (wc_rc != HAL_OK || wc_va == NULL) { …FAILED "ggtt_aperture_wc"; goto teardown; }
wc_aperture_va = wc_va; wc_aperture_size = mappable_end;   /* teardown で unmap */
```

### 2.3 CPU PAT / WC 属性変換(HAL amd64)

`amd64_cpu_init()`(BSP+各AP)で **IA32_PAT を index4=WC** に再プログラム(既存 index0-3/5-7 は reset既定を維持)。
per-mapping WRMSR ではなく CPU init。手順は wbinvd→wrmsr→wbinvd→flush_tlb。

```c
/* amd64_cpu_init() 先頭 */
__asm__ volatile("wbinvd" : : : "memory");
asm_write_msr(AMD64_MSR_IA32_PAT, 0x0007040100070406ULL);  /* idx4: WB->WC, 他は既定 */
__asm__ volatile("wbinvd" : : : "memory");
asm_flush_tlb();
```

`space.c` の device-window leaf は WC 時 **PAT index4(bit7=PAT, PCD/PWT clear)**、非WC時は従来 NOCACHE:

```c
flags = AMD64_PTE_PRESENT | AMD64_PTE_NX;
if ((mapping->attributes & HAL_SPACE_WC) != 0)
	flags |= AMD64_PTE_PAT_4K;    /* PAT index 4 = WC */
else
	flags |= AMD64_PTE_NOCACHE;
```

`hal_space_map_device` は WC を受理し、WC と NOCACHE/WRITETHRU/DEVICE の同時指定は HAL_ERR_INVALID(競合)。

> PAT手順の照合について: 現 `amd64_cpu_init()` は wbinvd→wrmsr→wbinvd→flush_tlb です。index4 を選ぶ live PTE が
> この時点で存在しない(BSPは kernel PT 構築前、APは bring-up 中)ため aliasing 無しで、実機で全CPU boot 健全を確認。
> Linux `cache_cpu_init()`(IRQ保存/CD/PGE/MTRR 枠組)相当の CD=1・PGE操作は、既存 index を変更しない本ケースでは
> 未実装のままにしています。**「使用中 index を書き換える将来変更」時には CD/PGE/TLB の完全手順が必要**という理解で、
> 必要なら次段で `amd64_cpu_init()` 側に補完します(probe/mapping 毎の WRMSR は追加しません)。ご確認ください。

### 2.4 共通 PCODE — `parity/pcode.c`(新規)

`__snb_pcode_rw`/`snb_pcode_read` を移植。共通 sb_lock(spinlock)で全PCODE取引を直列化、MAILBOX busy→-EAGAIN、
DATA/DATA1書込→READY|mbox書込→READY down poll→応答read→gen7 status→errno。MAILBOX=0x138124 / DATA=0x138128 /
DATA1=0x13812c / READY=1<<31。PCODE は forcewake domain外=raw access。read API も要求 write を伴う。fast=500µs/slow=20ms。
poll は非sleep(spinlock保持安全)+ 上記 hang-guard。詳細は添付 `pcode.c`。

### 2.5 DRAM/bandwidth — `parity/dram_bw.c`(新規)

- `intel_dram_detect`→`gen12_get_dram_info`→`icl_pcode_read_mem_global_info`: 要求 0x0d、`parity_dram_decode`(純粋)で
  type/channels/qgv/psf を復号し `dram_info` へ保存(後段 bandwidth が参照)。取得失敗は許容(void経路、BLOCK化しない)。
- `intel_bw_init_hw`→**`tgl_get_bw_info(&adlp_sa_info)`**(deburst16/deprogbwlimit38/displayrtids256/derating20)→
  `icl_get_qgv_points`: QGV要求(pt<<16)|0x10d ×N、PSF要求 0x20d。deinterleave/t_bl/channel_width を DRAM type で決定、
  bandwidth(deratedbw/peakbw/psf_bw/num_planes、丸め・配列上限)を6群で算出、SAGV 判定。
  **PSF取得失敗時は num_psf_points=0 で計算除外**(参照fallbackを移植。未実装で0にするのではない)。詳細は添付 `dram_bw.c`。
- **実処理エラー vs 未実装の区別**: PCODE を実装実行した上での失敗は参照どおり許容/継続。未実装で bogus error を
  通常fallbackへ隠す事はしていません。

---

## 3. GPU-free 試験(ktest、既存 runner へ追加。新規汎用基盤なし)

既存 `osdep_mmio_backend` vtable を使う fake backend(GDRST / PCODE mailbox をscript)で決定的に検証。**56 checks 0 fail**:

| 群 | 検証 |
|---|---|
| reset 制御フロー | 成功(rc0)/ timeout(3 retry 全 rc62)/ retry回復(fail→rc0) |
| PCODE 取引 | 成功(応答返却)/ busy(READY→-EAGAIN)/ status error(→-ETIMEDOUT) |
| DRAM decode(純粋) | 0x3420→DDR4/2/4/3 / 不明type→-EINVAL |
| DRAM detect+bandwidth | dram rc0 + bw rc0 + SAGV=ENABLED / PSF失敗→points0 fallback でも rc0 |
| WC 全範囲(実HAL) | window超過→拒否(出力不変)/ 直後の正当map成功(回収)→unmap |

reset を試験可能にするため `parity_gt_reset_all(mmio,timeout_ms)` を `reset.c` へ抽出、dram decode を
`parity_dram_decode` に分離しました(いずれも実経路が呼ぶ本体)。抽出後も P2受入(STOPPED_AT_P2)不変を実機確認済。

---

## 4. 未了・ご確認いただきたい点

1. **実IRQ文脈 completion 試験(one-shot)**: 以前決めた one-shot 方式。現状、driver 試験から使える
   **one-shot timer→IRQ→completion 機構が未露出**(kern timer は process itimer 中心)。IRQ 試験harness の新設は
   「新規汎用基盤を作らない」方針と相反します。**(a)** 最小の one-shot hook を新設して実装、**(b)** 保留、の
   いずれにするかご指示ください(completion backend 自体は K3 thread-wakeup / cross-CPU 64/64 で検証済)。
2. **PAT の CD/PGE 完全手順**(2.3 の注記): 使用中 index を変更しない今回は未実装。将来 slot 再配置時に補完する理解で
   よいかご確認ください。
3. **次段 P3〜P5**: 台帳どおり実機呼出順(display noirq → IRQ設置 → nogem → GEM init)を維持して進めます。
   旧MSI API(register_msi/unregister_msi)撤去は本件の完成条件外(新経路の非依存は確認済)。

以上、STOPPED_AT_P2 到達のご報告と、上記 3 点(特に 1)のご指示をお願いします。
