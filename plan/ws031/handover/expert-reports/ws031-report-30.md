# WS031 第30報 — C3 MSI分離 + 実WC/PAT 実装完了・実機検証、P2 frontier を intel_dram_detect へ前進

対象: ADL-P (8086:46a8) VFIO passthrough、参照条件(q35 / -cpu host,host-phys-bits-limit=39 / 4 GiB / 4 vCPU /
memory-backend-memfd 4G / vfio-pci x-igd-opregion=on,rombar=0)。build は parity ON/OFF とも 0 error / 0 warning。
drm 非blacklist、GPU は vfio-pci 維持。

---

## 1. サマリ

承認済みの HAL 追加(MSI分離4関数 + `HAL_SPACE_WC`)を実装し、**実機で end-to-end 検証まで完了**しました。

- **C3 MSI分離**(alloc/attach/detach_sync/free)を amd64 HAL に実装。既存 `hal_irq_register_msi/unregister_msi` は
  シグネチャ不変で温存(最終クリーンアップで wrapper 化予定)。他アーキは `HAL_ERR_UNSUPPORTED` stub。
- **実 WC mapping**を CPU PAT 再プログラム(index4=WC)+ `space.c` の属性変換で実装。per-mapping MSR write ではなく
  CPU init での PAT 管理。既存 index0-3/5-7 は reset 既定を維持。
- **GPU-free テスト**(ktest)に MSI 分離ライフサイクル(M0-M4)+ WC 属性契約を追加。
- **parity probe P2 を新API/WC apertureへ接続**。MSI blocker 解消、WC 正経路を実HWで実証、opregion 分岐を実装。

**実機結果(参照条件):**

| 項目 | 結果 |
|---|---|
| boot(PAT 全CPU 再プログラム後) | **CPUs ready 4 / memory 4089MB / panic・triple-fault 無し** |
| ktest | **41 checks, 0 failures**(MSI M0-M4 + WC 契約 + 従来 K0-K5 / cross-CPU 64/64) |
| WC aperture(GMADR 先頭ページ) | **ioremap_wc 相当マップ成功 va=0xffffffffe1000000** |
| pci_enable_msi | **ok(vector programmed, msi_enabled=1, handler unattached)→ torn down(source停止+vector解放)** |
| opregion | **ASLS=0x00000000 → 不在(Linux -ENOTSUPP 分岐)** |
| probe frontier | reached=P2, BLOCKED, where=**intel_dram_detect**(MSI/opregion を通過) |
| VFIO/DMA | -22 等エラー無し |

---

## 2. 実装詳細

### 2.1 include/hal/hal.h(追加のみ、既存不変)
専門家指示の契約コメント込みで4宣言 + WC flag を追加:
- `hal_irq_alloc_msi(source, *irq, *addr, *event)` — handler 無しで vector/routing/message 確保。PCI MSI Enable は
  触らない。HAL_OK まで使用可能リソースを公開しない。未接続 vector への到着は mask+EOI(NULL handler 経由で
  dispatch しない)。`mapped_addr` はメッセージ宛先アドレス。
- `hal_irq_attach_msi(irq, handler, arg)` — handler 接続(handler は接続前に初期化済であること)。
- `hal_irq_detach_msi_sync(irq, handler, arg)` — handler の**実行のみ** drain(queue した work/timer は待たない)。
  sleepable 文脈から、自 handler からは呼ばない。
- `hal_irq_free_msi(irq)` — handler detach 済 **かつ source 停止済**が前提。
- `#define HAL_SPACE_WC (64)` — 無音 fallback 無し(互換 fallback は呼び側が明示選択)。

### 2.2 amd64 実装(src/hal/amd64/irq.c)
既存の `irq_service[]` プール・NULL-handler-safe な `irq_handler()`・`irq_set_handler()` の drain 機構へ写像:
- alloc = リソース確保のみ(mode=NONE, handler=NULL)。
- attach = `irq_set_handler`(mode NONE→REALTIME)。
- detach_sync = removing フラグ + `irq_set_handler(NULL)` で全CPUの in-flight を drain し、**allocated は保持**。
- free = allocated/msi/mode 解放(handler!=NULL または in_handler/in_flight!=0 は `HAL_ERR_STATE`)。
- 未接続 vector への到着は既存の no-consumer 経路(hardware_mask + EOI)がそのまま吸収 = **二重 EOI 無し**。
- 他アーキ(arm64/sparcv9/m68k/i386)は4関数を `HAL_ERR_UNSUPPORTED` stub、既存 MSI は不変。

### 2.3 CPU PAT / WC(src/hal/amd64/{defs.h,asm.c,space.c})
- `amd64_cpu_init()`(BSP + 各AP)で **IA32_PAT(0x277)を再プログラム**し index4=WC、他は reset 既定(WB/WT/UC-/UC)。
  手順 = wbinvd → wrmsr → wbinvd → flush_tlb。BSP は kernel PT 構築前・AP は bring-up 中に実行し、index4 を選ぶ live
  PTE が存在しないため aliasing 無し(実機で全CPU boot 健全を確認)。
- `HAL_SPACE_WC` は直接 PTE bit ではなく **4KiB leaf の PAT ビット(bit7)= index4** を選択(leaf size ごとの PAT index)。
  WC と NOCACHE/WRITETHRU/DEVICE の同時指定は `HAL_ERR_INVALID`(競合 cache policy)。

### 2.4 parity probe 接続(backend_pci.c / probe.c)
- osdep vtable の `alloc/free_msi_vector`(従来 NULL=BLOCKED)を HAL 分離へ接続。source は device address から
  canonical 形式("PCI ssss:bb:dd.f")生成。P2 は handler 未接続、P2末に PCI MSI 無効化(source停止)→ HAL free。
- ggtt_init_hw の「WC は pending」NOTE を **実 WC map**(GMADR 先頭ページを `hal_space_map_device(HAL_SPACE_WC)`)へ置換。
- intel_opregion_setup を実装(ASLS 読取→map→署名/version 検証)。

---

## 3. 判断いただきたい点(P2 の残りと主軸)

opregion 通過後の frontier は **intel_dram_detect** です。hw_probe 残りの `intel_dram_detect` /
`intel_bw_init_hw`(`tgl_get_bw_info`)は次の性質を持ちます:

1. **PCODE mailbox 経路**(GT MMIO の pcode read/write)を要する。
2. **display 帯域計算**であり、**GT command streamer / EU-thread 状態には触れない**(= 今回の EU-hang の臨界経路上に無い)。

そこで方針をご相談させてください:

- **(a) P2 を形式的に閉じる**: PCODE mailbox を port し dram_detect + bw_init_hw を faithful に再現してから
  STOPPED_AT_P2 を宣言。hw_probe 完全一致を優先。
- **(b) 臨界経路へ主軸を移す**: hw_probe の GT 関連部は P2 完了扱いとし、**i915_gem_init / intel_gt_init /
  __engines_record_defaults**(= EU-hang が顕在化する GT/context/submit 経路)を次の主対象(P3)にする。
  display 帯域は EU-hang と無関係なので後回し。

私見では、WS031 の目的(EU-thread hang の Linux 経路上の分岐点特定)からは **(b)** が費用対効果が高いと考えます
(dram/bandwidth は hang に無関係、gem_init/GT init こそ hang 直前の経路)。ただし「hw_probe を完全一致させてから
先へ進む」規律を優先されるなら (a) を採ります。ご指示ください。

## 4. その他の保留事項

- **C1 full `__intel_gt_reset`**: 現状 P1 の sanitize は単発 write の gt_reset(実機で polls≈38 で成立)。専門家指示の
  「forcewake→callback→prepare/cancel→domain reset ×2 write/poll + 50µs settle→最大3回 retry」の完全版へ差し替えは
  未着手(P1 は現状動作、優先度は (a)/(b) 決定後に調整)。
- **最終クリーンアップ**(将来): `hal_irq_register_msi/unregister_msi` 除去 + 全ドライバ更新(ユーザ承認済の予定)。

以上、ご確認と (a)/(b) のご指示をお願いします。
