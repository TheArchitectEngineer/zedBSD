# WS031 第53報 — DMC F3(非同期 worker / 電源参照 / fini)+ flush_work。実共有 worker で DMC-NORMAL/NO-FW 成立。F2 試験を生 blob 独立照合へ補強。GPU-free 178/0

ご指示どおり **F3(worker・電源参照・fini)を実装**し、**F2 試験をチェックサム一致から生 blob 独立照合へ補強**しました。DMC のロードが**実共有 workqueue 経由で端から端まで**動作しています。build 0 error/warning。GPU-free **ktest 178/0**。

---

## 1. F2 試験の補強(試験側のみ、ロード本体は不変)

- 期待 payload を**生 blob(fw.data)から独立に読み**照合しました(移植先 parser が保存した copy に依存せず、copy バグを検出)。blob 内 offset = readcount(CSS 128 + package 400 = 528)+ dmc_offset×4 + v3 header 256。
- 累積式は**順序依存の多項式ハッシュ**(`psum = psum*1000003 + addr + val*7`)で、単純な Σ(addr+val) ではありません。addr/値/順序/件数を捕捉し、fake 総書込 13,141 で過不足ゼロ。
- **値変異検出器**:MAIN payload の 1 DWORD を反転すると psum が固定基準から乖離することを確認(照合器が no-op でない実証)。

## 2. F3:DMC 非同期ライフサイクル(dmc.{c,h}:parity_dmc_dev)

- **flush_work 追加**(backend_sync `parity_kflush_work`):対象 work を**取消さず実行完了を待つ**(cancel_work_sync とは別。PENDING は実行を待ち、RUNNING は callback 終了まで、戻り値 false=idle は失敗ではない)。既存共有 workqueue を再利用(第二基盤を作らず)。
- **init**:DMC 固有の POWER_DOMAIN_INIT 参照を取得(親と別の domain get)→ state/work/参照を準備 → **queue 前に全公開**(別 CPU worker が return 前に開始しても成立)→ `parity_kqueue_work`。worker を直呼びしません。
- **worker(dmc_load_work_fn)**:`osdep_request_firmware`(default path)→ -ENOENT かつ path 未指定で **legacy adlp_dmc_ver2_16 へ fallback**(解析失敗を fallback 扱いにせず)→ 本番 parser → MAIN payload 成立時に本番 load → **load_seq_completed で成功時に DMC 参照を domain put、未完(fault)は保持** → provider 解放。診断は分離(main_payload_present と load_seq_completed を兼用しない)。
- **電源参照 3 者分離**:PCI probe / 親 INIT(driver_remove で rpm-only 解放)/ **DMC ロード(domain get → 成功時 domain put / 不在・fault で保持)**。driver_remove の rpm-only 解放を DMC の通常 domain 参照解放へ流用しません。DMC 状態は device 所有(dd)で、参照所有を obj 内だけに置きません。
- **fini**:**flush_work(cancel でない)→ 残存 DMC 参照処理 → arena 一括解放(個別 free 後に arena を free しない)→ parse reset**。worker 使用中メモリを free しません。

## 3. 試験(GPU-free、実共有 worker 経由)

| 試験 | 合格 |
|---|---|
| **DMC-NORMAL** | init→queue→**実 kworker が worker を実行**→provider→parser→load(13019 書込)→**DMC 参照解放**。全 well refcount 合計が前後不変(DMC get+put=net 0、親 INIT 参照保持、leak なし)。work_submitted/worker_started/firmware_acquired/main_payload_present/load_seq_completed=1、dmc_wakeref_held=0 |
| **DMC-NO-FW** | 不在 path → default+**fallback 要求** → payload 無 → **DMC 参照保持**(rpm ブロック)。fini が保持参照を解放(dmc_wakeref_held 1→0) |

## 4. 実機(GPU 渡さず、image ab10df39)

`CAS-SELFTEST PASS` / **ktest 178/0**(174→178 = F2 補強 2 + DMC-NORMAL 1 + DMC-NO-FW 2)/ probe=NOT_RUN / selftest=PASS。F1/F2/firmware provider/電源 HW 初期化は回帰保持。

## 5. 次(残り 2 試験 → F4 → 実 GPU)

- **DMC-BAD-FW**:firmware provider の試験 override で、試験用コピーの MAIN ヘッダを破損 → MAIN payload 不成立 → program write へ進まず、途中の arena 使用分・参照を回収。
- **DMC-FINI**:worker を preemption 禁止区間へ入る前の試験 hook で停止 → 別 thread から fini → 完了前に arena/device/MMIO を破棄せず、解除後に同期・回収。MMIO fault を入力 variant に含める。override 解除も worker 完了後。
- **F4**:probe.c の `intel_dmc_init` UNIMPL を実 `parity_intel_dmc_init` へ置換(電源 HW 初期化の後、queue して次へ、通常 probe に DMC 完了待ち障壁を足さず、後続(modeset/flip wq)未実装を停止点、診断終了で DMC work を同期し採取、DMC fini は power-domain remove より先)。**実 GPU 一回**(参照条件)。

**保持**:F1 parser(v2.20/5 payload)、F2 load(13019 書込)、firmware provider、電源 HW 初期化(実機到達)、10ms tick/HAL 非変更。GPU=vfio-pci、attach 先行。描画・EU・baremetal・ハング比較は追加しません。

次報:DMC-BAD-FW/DMC-FINI の結果、F4 probe 接続、実 GPU での DMC ロード結果(queued/worker/load_seq_completed/参照 get-release/work 同期/last_completed_op/blocked/cleanup/published=0)。
