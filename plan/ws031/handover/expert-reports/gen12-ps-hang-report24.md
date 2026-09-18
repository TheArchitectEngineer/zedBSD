# Gen12 PS/compute ハング 第24報 — M1 完了。OS 適合層 4 層を実装、GPU 無し契約試験 118 checks 全 pass

固定順（M1-A 型/返却値/所有権/計測 → M1-B DMA/PCI/MMIO/同期の自動試験）に従い、**M1 を完了**しました。
次報 3 点のうち①適合層の実装 と②自動試験の結果 を本報で提出します（③P0〜P2 実行記録は M2）。
実装は `src/drivers/gpu/i915/parity/{osdep,tests}`（1426 LOC）。**portable-core + backend-vtable + mock** で、
kernel build には未 wire（M2 で実 backend 接続）。全て `-Wall -Wextra -Werror` clean。

## 1. アドレスの 3 分離（ご最重要点）
現 `gem.c` は `kern_pmem_alloc` の `run.paddr`（CPU/guest-phys）を `ggtt_insert`/`ppgtt_insert` に**直渡し**でした。
現 QEMU/VFIO は guest 仮想 IOMMU 無し→ dma == guest-phys（偶然一致）で通っていただけです。
`address_types.h` で `osdep_cpu_phys_t` / `osdep_dma_addr_t` / `osdep_gpu_vaddr_t` を**別 struct 型**にし、
数値一致に依存させず、**GPU PTE へは `dma_addr_t` のみ到達可能**（pin が返すのも device アドレスのみ）にしました。

## 2. 返却契約（API ごとに保持、汎用 non-zero 判定なし）
- `dma_set_info` → 0 / -errno（mask_bits を device cap として検証。host-phys-bits を mask に代入しない）
- `dma_map_sg` → mapped segment count(>=1) / 0（dma_map_sg 契約）
- `dma_map_sgtable` → 0 / -errno（別契約を別 API で保持）
- `dma_map_page` → `osdep_dma_addr_t`（専用 `osdep_dma_mapping_failed()` で判定、0 や phys と比較しない）
- 未実装 backend op は `UNIMPL` を記録し `-EOPNOTSUPP`（HW 非対応と偽らない。開発版はその段で停止）

## 3. 所有権と寿命
DMA mapping は `resource_id` + pin count を持つ所有資源。**pin 中（GPU page table へ bind 中）の unmap は -EBUSY で拒否
し、解放しない**（利用中メモリを free しない）。scatter は `orig_nents`（unmap 用）と coalesce 後 `nents`
（HW walk 用）を構造的に分離（dma_unmap_sg は orig を使う）。

## 4. MMIO / forcewake / PCI / 同期
- **MMIO/uncore**: 通常 access は該当 forcewake domain 保持を要求（未保持は sentinel+FAIL 記録）、raw access は
  init/reset 用に分離。forcewake は refcount+ACK（0→1 で wake+ACK 待ち、1→0 で sleep、nested get、**ACK timeout で
  ref 巻戻し**、over-put 検出）。posting read / masked RMW / MCR steering 排他 lock。
  → **forcewake-ALL 常時保持の是正基盤**（是正自体は P7.8 gt_resume の get/release/PM とセットで実施、解放先行しない）。
- **PCI**: config 幅別 access、capability walk（不在 cap は**操作しない**）、`enable_device`=D0 遷移+IO/MEM decode の
  **16bit RMW（隣接 STATUS を汚さない）**、bus master、MSI enable（**IRQ handler 設置と分離**、MSI 不在は -ENODEV=
  非 fatal で INTx へ、**無書込**）。PM cap 不在時の set_power_state は無書込 no-op（捏造しない）。
- **同期/deferred work**: completion（wait 前 complete=即時、mid-wait の IRQ race=観測、timeout）、`queue_work` は
  **遅延実行（inline で呼ばない）**、ordered FIFO、cancel（実行前/実行中）、二重 queue は idempotent。

## 5. 計測（trace.c）
固定長 ring に entry/exit/acquire/release/map/unmap/sync/worker/unimpl/fail を記録。**overflow を dropped で計上**
（gap≠未実行）。静的な呼出確認と実機通過は別記録（renderstate/clock-gating の読み違い再発防止の要）。

## 6. 自動試験結果（GPU 無し、mock backend）
```
dma_contract_test : 36 checks, 0 failures   (DMA-1 非identity / DMA-2 非線形 / DMA-3 coalesce 分離 /
                                             DMA-4 失敗時無 leak / DMA-5 pin 中 unmap 拒否 / DMA-6 sync 順序)
mmio_contract_test: 35 checks, 0 failures   (FW get/nested/put/put / ACK timeout / over-put / MMIO no-fw /
                                             masked RMW / posting read / MCR 排他 / balance)
pci_contract_test : 27 checks, 0 failures   (幅別 cfg / cap walk / enable STATUS 非汚染 / idempotent /
                                             bus master / MSI present / MSI 不在=非fatal無書込 / PM 不在 no-op / restore)
sync_contract_test: 20 checks, 0 failures   (complete前/timeout/mid-wait race / queue 遅延 / flush / cancel /
                                             FIFO / idempotent / 実行中 cancel)
合計 118 checks, 0 failures
```
**これは mock 契約試験（mock-VERIFIED）であり、HW-VERIFIED ではありません**（ご指示どおり区別）。
mock は test 専用の非 identity 変換等で契約を検証するもので、実機 DMA 成功の証明ではありません。

## 7. 次（M2）
1. **実 backend** を各 osdep 契約層の vtable に接続（drv_pci_config_*/bar、uncore forcewake/read/write、drv_dma_*、
   waitq/workqueue）。空成功 stub を作らず、未実装は UNIMPL 記録で公開前停止。
2. **probe.c** で P0→P1→P2 を単一 attach 経路として接続（legacy と混在させず、最上位で一回だけ選択、途中失敗で
   legacy へ fallback しない）。順序: DMA mask→GGTT probe/init→memory region→GGTT enable→bus master→MSI。
   MSI 設置と IRQ handler を分離。既存 PAT は P2 でなく setup_private_pat 位置(P6)へ据え置き（前倒ししない）。
3. **診断停止** `parity_stop_after=P0/P1/P2`（Linux 成功返却と混ぜず外側の診断結果として、正常到達/未実装到達/
   実失敗 を区別。いずれも driver-ready/公開へ進めない）。
4. **fault injection を GPU 無しで先行**（各資源取得を順に失敗→取得済のみ解放/二重解放無/後続不呼/解放後アクセス無/
   worker・IRQ 残留無/失敗理由を cleanup で上書きしない）。その後、実機で P0〜P2 到達+安全 teardown を確認して停止。

## 8. 伺いたいこと
1. **契約の形**（返却規約・所有権 pin・trace の粒度・3 アドレス型）をこの形で M2 の土台にして良いでしょうか。
   実 backend と driver 全体を上に載せる前に、契約 shape へのご指摘があれば今のうちに反映します。
2. **driver 基盤層（runtime PM）**: M2 の P0〜P2 が使う範囲（例: attach 中の PM get/put、GT 活動期間の
   POWER_DOMAIN_GT_IRQ 所有）はこの段で契約層に足すべきか、P3/P7 で足すべきか、切り分けのご助言をお願いします。
3. **実機到達点**: M2-B の「P2 到達+安全 teardown で停止」を実機で示す際、現 selftest 経路（legacy attach）は
   保持し、parity attach は診断選択で別に起動する構成（GPU は同一 VFIO）で問題ないでしょうか。

（成果物: `plan/ws031/linux-parity/`{ledger.md(rev2, M1 状態反映), M1-test-results.txt} と
 `src/drivers/gpu/i915/parity/{osdep,tests}`。git 操作なし。kernel build 未変更（standalone test のみ）。GPU vfio-pci 維持。）
