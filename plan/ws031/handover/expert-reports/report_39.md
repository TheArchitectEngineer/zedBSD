# WS031 第39報 — 単位1:C(power-well ops)を正本の個別 ops へ是正。ktest 124/0、実機 reached=P3 BLOCKED intel_power_domains_init_hw

D の前に、ご指示のとおり **C(power-well 操作)を正本の個別 ops へ是正**しました。A(PCI probe PM)・B(時間 fault 試験)は保持し、やり直していません。build 0 error/warning、GPU=vfio-pci、attach 先行維持、電源表(30 wells / 0x4000000a / 0x2)保持。実機 reached=P3 BLOCKED intel_power_domains_init_hw(電源 HW 初期化は未実行=fake MMIO 試験)。

実機(参照条件、FLR 後 一回起動、image a5c2397b):`CAS-SELFTEST PASS` / **ktest 124/0** / `PM probe get_sync cfg_vendor=0x8086 usage=1 active=1` / `PCI probe runtime PM released usage=0` / CPUs 4 / panic 0 / cleanup=1 / published=0。

---

## 1. sync_hw — 状態読出しだけで終わらせない(BIOS→driver 引継ぎ)

`intel_power_well_sync_hw()` に合わせ、**ops->sync_hw() を実行してから is_enabled() を読み保存**へ。HSW 系 `hsw_power_well_sync_hw()` の **BIOS 要求引継ぎ**を移植:
- BIOS が driver REQ を保持しているとき、**driver 側 REQ を先に立て(未設定時のみ)、その後 BIOS REQ を解除**。逆順にせず、software refcount が 0 でも省略せず、sync 中に refcount を変更しません。
- BIOS レジスタ = driver レジスタ − 4(CTL1 = CTL2 − 4:hsw 0x45400/0x45404、aux 0x45440/44、ddi 0x45450/54)。

## 2. 有効判定と disable の完了条件

| 対象 | 是正 |
|---|---|
| is_enabled(ADL-P) | driver reg の **REQ と STATE の両方**が立つことを確認(STATE だけではない) |
| disable の待機 | **自分が最後の要求元のときだけ** STATE clear を待つ。BIOS/KVMR が REQ 保持なら **STATE 残置を許容**(無条件 STATE=0 待ちにしない)。hw_enabled は is_enabled(driver 所有)を反映 |
| enable 前後 | fuse 待ち(模型)+ ADL-P WA + post-enable |
| DC_off / always-on | 汎用 REQ/STATE 式へ流さず各 ops(DC_off=自 state、always_on=1) |
| AUX | descriptor 名でなく ops-kind(icl_aux)で判定、fixed_enable_delay/enable_timeout を保持 |

three アドレス(0x45404/44/54)は公開定義と一致。是正対象は BIOS/driver 要求元の構造と操作手順で、各 reg 群に正本の存在メンバーのみ持たせています。

## 3. ACK timeout を一律 unwind にしない

enable/disable ops は void 相当:
- **実 HW の ACK timeout(-ETIMEDOUT)は警告して継続**(AUX は timeout 予期条件)。`ack_timeouts` に記録し、**呼出元を失敗させません**(domain get の unwind を起こさない)。
- **時間源異常/不正 access(-EIO)のみ停止**(適合層異常として後始末)。B の時間 fault 伝播は保持。

「エラーを隠さない」と「全部を致命的にする」を分け、正常な参照経路に余分な巻戻しを足していません。

## 4. timeout の単位 / VGA 実 I/O

- **enable_timeout=500** は `parity_wait_reg(…, slow_ms=enable_timeout)`(ミリ秒・slow 待機)へ渡しており、fast_us には渡していません(単位・待機文脈を保持)。
- **intel_vga_reset_io_mem を実 I/O 本体**へ(模型を実機経路から外す):arbiter client 登録時のみ **`kern_io_out8(0x3C2 /*VGA_MIS_W*/, kern_io_in8(0x3CC /*VGA_MIS_R*/))`**。GPU MMIO 書込や VGA plane 無効化で代用していません。未登録(GPU-free 試験は client 無し)は no-op ゆえ stray IO を出しません。

## 5. 試験(fake MMIO を BIOS/driver REQ 分離へ改修、ktest 119 → 124/0)

fake を「driver REQ を書けば STATE が即同一」だけの模型から、**BIOS REQ / driver REQ を別管理し STATE=任意要求元**へ変更(旧単純化では落ちる入力を与える):
- is_enabled 00 / STATE-only(BIOS)/ REQ+STATE の区別。
- **sync_hw が BIOS 要求を引継ぎ**(driver REQ 立て → BIOS 解除、refcount 不変)、handoff 不要時は is_enabled 記録。
- **disable が BIOS 保持 well を許容**(STATE 残・driver 所有 off)。
- **ACK 欠落=警告継続**(enable が 0、ack_timeouts=1)、時間源異常=-EIO。
- get/put refcount、明示 enable は refcount 不変、2 domain の PW_A 共有、post_enable の VGA(PW_2)/IRQ gate off。

## 6. 次(単位 2〜4:D)

C の是正が済んだので、実 GPU へ接続する前に D の子依存を実装し、fake backend で呼出関係を閉じてから接続します(半分変更後に未実装 callback へ到達する手順にしません)。
- **combo PHY**:intel_combo_phy_init + 対象 PHY 列挙・世代・状態。
- **CDCLK**:early probe hook(intel_init_cdclk_hooks、ADL-P stepping 別 table)+ intel_cdclk_init_hw(現状態読 → sanitize → 必要時のみ変更 → cdclk.hw 保存。適切なら変更しない経路を保持、毎回 PLL 再設定/table を構造体へ書くだけ=完了 にしない)。
- **PCODE**:CDCLK 変更前の **skl_pcode_request()** + 変更後 PCODE write を共通 PCODE へ接続(sb_lock → 送信 → reply mask 確認 → 再要求 → timeout 再試行)。単発 mailbox write にせず、別 lock/簡略 poll を足さず既存共通実装を拡張。
- **DBUF / MBUS**:ADL-P は `gen12_dbuf_slices_config` / `icl_mbus_init` が early-return。その分岐を移す(名前から新規レジスタ書込を作らない。ただし DBUF 有効化全体の省略ではない)。**BW_BUDDY**:P2 保存 DRAM + 正本表。
- **intel_power_domains_init_hw(false)**:initializing=true → icl_display_core_init(false)[上記順] → **POWER_DOMAIN_INIT 参照 取得・保持**(disable_power_well=1 では disable_wakeref 分岐を実行しない)→ **intel_power_domains_sync_hw(全 well に是正済の個別同期 ops)** → initializing=false。
- **cleanup**:全 well OFF/refcount ゼロ代入にせず `intel_power_domains_driver_remove()` 対応(well 有効のまま+runtime PM 参照)。最後の PM put が使う PCI device/backend の寿命(BAR 解放と PCI オブジェクト破棄の区別)を確認。継続は INIT 参照保持で DMC へ、診断停止は実行済段階に対応。
- 並行:pvclock(cs2 確認済)/ 高分解能 sleep-range(clocksource≠clockevent、fault 試験成功=sleep-range 完成ではない)/ ROM resource / 有効 VBT parse。D が実 GPU で使う待機経路は未実装代用で通しません。

到達目標:intel_power_domains_init_hw(false) 完了 + INIT 参照保持のまま intel_dmc_init 入口へ(成功でも P3 全体完了/描画ではない。DMC 未実装ならその入口で正確停止)。

**保持**:MSI 分離 / runner / 共有 backend / 既存試験、per-pipe vblank、時間 error 伝播、VGA 登録、電源 map(30 wells)+是正 ops、PCI probe PM、時間 fault 試験。GPU=vfio-pci、drm 非 blacklist、attach 先行。描画再試験・GPU-hang 探索へは戻りません。

`last_completed_op` = intel_pmdemand_init_early、`blocked_or_failed_op` = intel_power_domains_init_hw、保持 wakeref = PCI probe PM(取得→解放を実機確認)、cleanup=1 / published=0。
