# WS031 第52報 — DMC F2(program load):固定 blob の解析結果で 13019 payload + 35 aux + 80 evt-disable を逐次照合。GPU-free 174/0

ご指示の第一着手 = **intel_dmc_load_program 相当を実装し、五つの payload を 13,019 回の DWORD 書込みとして逐次照合、イベントレジスタは payload と別基点から生成**を完了しました。build 0 error/warning。GPU-free **ktest 174/0**。

---

## 1. F2 program load(parity_intel_dmc_load_program、正本 intel_dmc_load_program 準拠)

正本の順序で接続:

```
MAIN payload 存在確認
  → pre clock-gating WA (pipe A..D: CLKGATE_DIS_PSL_EXT |= PIPEDMC_GATING_DIS)
  → 全 present DMC の event handler 無効化 (8 handler × CTL=FALSE(0x30100)/HTP=0)
  → preemption 禁止
  → 全 ID payload 書込 (DMC_PROGRAM = start_mmioaddr + i*4, write_fw)
  → preemption 復元
  → 全 ID 付随 MMIO (dmc_mmiodata() 変換)
  → dc_state = 0
  → DC_STATE_DEBUG (0x45520) rmw(CORES|MEMORY_UP) + posting read
  → post clock-gating WA (pipe C..D のみ clear)
```

- **payload 領域とイベント制御領域を別基点で生成**:event base = MAIN 0x8f000 / pipe DMC 0x5f000+0x400*(id-1)、`_DMC_REG(id,reg)=reg-0x8f000+base(id)`、EVT_CTL=base+0x34+4h / HTP=base+4+4h。PIPEA の payload が 0x90000 でも、イベントは 0x5f000 系から生成(近傍と推測しません)。
- **entry offset を load でもう一度足さない**(parser が使用済み。load は保存 payload を読み `start_mmioaddr + 4*i` へ、DMC_PROGRAM の 4byte 刻み)。**ID 走査順を保持**(PIPEC/D の宛先が低位でも物理アドレス昇順に並べ替えない)。
- **付随 MMIO は dmc_mmiodata() 変換**:pipe DMC の EVT_CTL レジスタは disable 値(0x30100)、MAIN と非 EVT_CTL は raw。raw 値そのままの全件 write にしていません。
- **clock-gating WA は非対称**:pre は A〜D、post は C・D のみ(前処理で立てた bit を末尾で全解除しません)。
- payload ループ内に sleep・通常ログ・常時 forcewake を追加せず、既存 preemption/MMIO 適合層を使用。診断は payload_writes / aux_writes / evt_disable_writes / **load_seq_completed(末尾処理まで完了で 1)** を分け、`has_payload=true` だけを完了証拠にしません。

## 2. 書込み逐次照合(GPU-free、実 blob → 本番 parser → 本番 load、fake MMIO)

| 検査 | 期待 | 結果 |
|---|---|---|
| payload 書込 | 13,019(MAIN6274+PIPEA2547+PIPEB3066+PIPEC566+PIPED566) | ✓ |
| 付随 MMIO | 35(7+9+9+5+5) | ✓ |
| event-disable | 80(5 DMC × 8 handler × CTL+HTP) | ✓ |
| dc_state | 0 | ✓ |
| load_seq_completed | 1 | ✓ |
| **payload 値/宛先/順序** | test 側で**独立に再計算**した checksum(同順 addr+val*7)と一致 | ✓ |
| fake 総書込 | 13,141(WA4 + evt80 + payload13019 + aux35 + dbg1 + WA2)= 過不足 0 | ✓ |

payload の checksum は**移植先の load とは独立に、parser が保存した payload と start_mmioaddr から test 内で再計算**して比較しています(一件ごとの一致・過不足ゼロ、二重コピー誤りの検出)。全書込みが fake へ到達した件数(13,141)も確認しました。

## 3. 実機(GPU 渡さず、image b2ca92b8)

`CAS-SELFTEST PASS` / **ktest 174/0**(173→174)/ probe=NOT_RUN / selftest=PASS。F1 parser・firmware provider・電源 HW 初期化(実機到達)は回帰保持。

## 4. 次(F3/F4、同一単位で継続)

- **F3 worker / 電源参照 / fini**:`intel_dmc_init` は DMC 固有電源参照を取得し state/work を準備、**queue して return**(worker 直呼びせず、別 CPU worker が init return 前に開始しても成立するよう queue 前に引数・参照先を初期化)。worker = provider 取得 → 本番 parser → 本番 load → DMC 固有参照処理 → provider ハンドル解放。**電源参照 3 者分離**(PCI probe / 親 INIT / DMC ロード、正常後に解放するのは DMC 自身の参照のみ、不在・解析失敗で保持、**DMC オブジェクト確保前に参照取得がありうる**、MMIO fault 中断時は payload 存在でも DMC 参照が残りうる→`has_payload` だけで解放済と判定しない)。不在 fallback は 既定 -ENOENT かつ path 未指定で legacy `adlp_dmc_ver2_16`(解析失敗を fallback 扱いにせず、alias 偽装せず、正常系は adlp_dmc.bin)。arena は**個別 free 後に arena free しない=一括解放**。fini は **flush_work 相当(cancel_work_sync で代用しない、false=idle を失敗にしない、callback 実行終了確定後に payload 解放)** → DMC 参照処理 → arena 一括+state 解放 → power-domain driver_remove → 下位資源。worker 使用中メモリを free しない。既存共有 workqueue 再利用。
- **F4**:probe.c の `intel_dmc_init` UNIMPL を実入口へ置換(電源 HW 初期化の後、通常 probe に DMC 完了待ち障壁を足さず queue して次へ、後続未実装を停止点、診断終了で DMC work を同期し採取)。GPU-free 4 試験(DMC-NORMAL/NO-FW/BAD-FW/FINI)後、実 GPU 一回。

**保持**:F1 parser(v2.20/5 payload)、firmware provider、電源 HW 初期化(実機到達)、10ms tick/HAL 非変更。GPU=vfio-pci、attach 先行。描画・EU・baremetal・ハング比較は追加しません。

次報:F3 worker/参照/fini の diff、GPU-free 4 試験結果、可能なら実 GPU での DMC ロード結果(queued/worker/program_load_sequence_completed/参照 get-release/work 同期/last_completed_op/blocked/cleanup/published=0)。
