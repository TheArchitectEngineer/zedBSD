# WS031 第40報 — D1:共通 PCODE の skl_pcode_request / snb_pcode_write を実装(CDCLK 変更の通信経路)。ktest 128/0

ご指示の D 実装順のうち、まず **D1(共通 PCODE の要求・書込み処理)**を実装しました(D0 の fuse/DC_off/VGA 補完は §3 の残作業として D2 と結線して進めます)。C 是正(第39報)・A(PCI probe PM)・B(時間 fault)は保持。build 0 error/warning、reached=P3 BLOCKED intel_power_domains_init_hw 維持。

実機(参照条件、FLR 後 一回起動、image 35d88cb4):`CAS-SELFTEST PASS` / **ktest 128/0** / reached=P3 BLOCKED intel_power_domains_init_hw / CPUs 4 / panic 0。実 GPU への接続は、ご指示どおり **D 全体を fake で結線してから一回**にまとめます(PCODE のためだけの起動はしていません。上記は既存の統合 GPU-free 試験＋attach の定期確認です)。

---

## 1. 共通 PCODE の拡張(pcode.c/h、新 lock / 別 mailbox を作らず既存を拡張)

- **snb_pcode_write_timeout / snb_pcode_write**:既存 `parity_snb_pcode_rw(is_read=0)` を sb_lock 下で。`snb_pcode_write(mbox,val)` = `snb_pcode_write_timeout(mbox,val,500,0)`。
- **skl_pcode_try_request**:`__snb_pcode_rw(mbox, &request, NULL, 500, 0, true)`(atomic=fast 段のみ)→ 読戻し値で **`(reqval & reply_mask) == reply`** を判定、`*status` に txn 結果(0 / PCODE errno / -ETIMEDOUT / -EIO)。
- **skl_pcode_request**:
  ```
  sb_lock
   → 最初の要求を明示送信(prime; _wait_for が最初の評価時点を保証しないため)
   → 一致すれば 0
   → timeout_base_ms の間、reply mask を確認しながら再要求(poll)
   → 不成立なら 50ms の追加 poll
   → すべての出口で lock 解放
   → return status ? status : ret   (PCODE status 優先、無ければ待機結果)
  ```
  - 追加 poll は正本の preemption 無効相当ですが、**IRQ を無効にした長時間 spin には置き換えていません**(高分解能 sleep-range が未実装のため bounded busy poll、preemption は残す)。
  - **時間源異常(-EIO)は即停止し lock を解放**(通常の PCODE timeout に化けさせない)。PCODE status の errno はそのまま伝播。
  - **C で採用した「ACK 警告継続」を PCODE 全体へ広げていません**(エラーの伝播・終了は各呼出元の正本に従う)。

## 2. 試験(GPU-free scripted PCODE、ktest 124 → 128/0)

fake MMIO に `pcode_sticky_status`(恒常ステータス)/ `pcode_no_ready`(READY 維持)を追加。4 ケース:
| ケース | 確認 |
|---|---|
| 最初の要求で承認 | 再要求せず(txn=1) |
| 数回後に承認 | 毎回正しい request を送り、応答値を取り違えない(txn=3) |
| 恒常 PCODE エラー | **errno を伝播**(-EIO/-ETIMEDOUT に化けない) |
| 時間基盤異常 | **-EIO で停止・lock 解放**(READY を維持して poll を fault へ入れる) |

## 3. D の残作業(D0 補完 + D2 + D3)— 実 GPU 接続前に fake で結線

- **D0-fuse**:hsw_power_well_enable の対象 well 別 fuse 待ち(PW1=要求前 PG0 + ACK 後 PG1、他=ACK 後 PG)を実レジスタ poll で。REQ+STATE 一致で代用しません。
- **D0-DC_off**:`gen9_set_dc_state` / `gen9_disable_dc_states` / `gen9_dc_off_power_well_{enable,disable,enabled}`(enable=disable_dc_states、is_enabled=DC_STATE_EN 読、disable=DMC payload 有無確認 → 成立時のみ目標 DC 状態)。put で無条件書戻ししません。`gen9_disable_dc_states` の CDCLK/DBUF/combo PHY 確認は D2 と同実装へ接続。DMC 未初期化を偽 payload で埋めません。
- **D0-VGA**:登録フラグ=所有権にせず、`vga_get_uninterruptible(LEGACY_IO)` → inb(VGA_MIS_R) → outb(VGA_MIS_W) → `vga_put` の **get/put を arbiter へ接続**(外側取得済なら二重取得しない)。fake に IO accessor を接続し、取得→read→write→解放を検査。
- **D2-combo PHY**:`intel_combo_phy_init` + ADL-P 子関数/表(PHY 列挙 → 現設定検査 → 正しければ変更しない / 不一致なら procmon 値・順序で初期化。全書直しでも enable bit で全省略でもない)。master 判定/lane・group reg を保持。
- **D2-CDCLK**:early probe hook(`intel_init_cdclk_hooks`、ADL-P stepping 別 table/callback、PCI revision 生値を display stepping にそのまま使わない、table を選ぶだけで PLL を前倒ししない)+ `intel_cdclk_init_hw`(現状態読 → sanitize → 有効ならそのまま終了 → 必要なら計算 → **PCODE 変更準備要求(失敗なら後続 PLL へ進まない)** → PLL/divider 設定 → PCODE 変更後通知 → cdclk.hw 更新)。読み戻せない voltage level は要求値保持を "HW readback" と記録しません。
- **D2-DBUF/MBUS/BW_BUDDY**:`gen12_dbuf_slices_config`/`icl_mbus_init` は ADL-P early-return(その分岐を移す)だが `gen9_dbuf_enable`(既存有効 slice 読 → 必要 slice 有効)は省略しません。BW_BUDDY は P2 保存 DRAM + 正本表。
- **D3-親**:`intel_power_domains_init_hw(false)` = initializing=true → icl_display_core_init(false)[DC 無効化 → PCH+reset handshake → combo PHY → PW1 明示 enable → CDCLK → DBUF → MBUS → BW_BUDDY → 末尾] → **POWER_DOMAIN_INIT 参照 取得・保持**(disable_power_well=1 では disable_wakeref 分岐なし)→ **全 well 個別 sync_hw** → initializing=false。PW1 ACK/CDCLK 成功=親完了にしません。INIT 取得で再度呼ばれる処理(DC_off enable→CDCLK/DBUF/combo PHY)を重複と思って削除しません。全 well 同期後も refcount と物理 ON の完全一致を独自合格条件にしません(BIOS 保持 well を強制 OFF しない)。
- **cleanup**:全 well OFF/refcount ゼロ代入にせず `intel_power_domains_driver_remove()` 対応(well 有効+runtime PM 参照)。最後の PM put が使う PCI device/backend の寿命(BAR 解放と PCI オブジェクト破棄の区別)を確認。
- **並行**:PCODE/well 待機が使う delay/poll/sleep-range はその関数の依存として完成(未実装 sleep を一律 busy loop/1 tick 固定に変換しない)。pvclock は共通 HAL 側で並行(D 関数に新 TSC 較正/CPU 命令を埋めない)。ROM/VBT 未了は残置。

**受入**:上記を fake で結線後、既定の 4 GiB/4 vCPU/39-bit、parity 単独、attach 先行で **一回**実行。到達目標=intel_power_domains_init_hw(false) 完了 + INIT 参照保持のまま **intel_dmc_init 入口**(DMC 未実装ならそこで正確停止。P3 全体完了/描画ではない)。失敗時は親関数名でなく最初の具体的な子処理を記録。

**保持**:MSI 分離 / runner / 共有 backend / 既存試験、per-pipe vblank、時間 error 伝播、VGA 登録、電源 map(30 wells)+是正 ops、PCI probe PM、時間 fault 試験、共通 PCODE。GPU=vfio-pci、drm 非 blacklist、attach 先行。描画再試験・GPU-hang 探索へは戻りません。

`last_completed_op` = intel_pmdemand_init_early、`blocked_or_failed_op` = intel_power_domains_init_hw、cleanup=1 / published=0。次報には共通 PCODE(本報)+ combo PHY/CDCLK の実装差分を添えます。
