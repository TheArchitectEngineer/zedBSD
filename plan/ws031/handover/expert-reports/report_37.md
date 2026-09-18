# WS031 第37報 — 電源管理の表と定義を正本へ是正(作業順(1))。実機 wells=30 / allowed_dc=0x4000000a / target_dc=0x2、ktest 97/0

ご指示の作業順「(1)電源管理の表と定義を完成 →(2)PCI／時間基盤の依存を閉じる →(3)intel_power_domains_init_hw(false) 一式」のうち、**今すぐの最初の作業=30-well 表(AUX_TBT1〜4 を含む)と正しい DC 定義**を実施し、実機で検証しました。第36報の成果(per-pipe vblank / 時間 error 伝播 / BIOS 既定 / VGA 登録 / pmdemand 早期初期化 / log 分離)は保持。GPU=vfio-pci、drm 非 blacklist、attach 先行維持。

**(2)〜(5)は本増分の続きとして staged**(理由・計画は §5)。今回は「先へ進めた対処」でなく、後続の HW 操作へ**正しい定義と依存を渡すための移植是正**です。

---

## 1. DC 定義 — register-bit 表現へ統一(独自抽象 0xe/0x4 を廃止)

正本 v6.8.12 の bit エンコーディングに統一しました。

```
DC_STATE_EN_UPTO_DC5 = 0x00000001
DC_STATE_EN_UPTO_DC6 = 0x00000002
DC_STATE_EN_DC9      = 0x00000008
DC_STATE_EN_DC3CO    = 0x40000000
```
ADL-P(display ver 13、disable_power_well=1、enable_dc=-1)条件で:
- **allowed_dc_mask = DC9 | DC3CO | UPTO_DC6 = 0x4000000a**
- **target_dc_state = UPTO_DC6 = 0x00000002**

`get_allowed_dc_mask` / `sanitize_target_dc_state` を正本ロジックへ。これらは許可方針/目標状態であり、直ちにレジスタへ書く値ではありません(HW 初期化は DC 状態の無効化から始まる、を維持)。

## 2. xelpd well 表 — 26 → 30(AUX_TBT1〜4 の欠落を修正)

前回は AUX を A〜E(5)+ TC1〜4(4)= 9 として **AUX_TBT を欠落**していました。正本の xelpd_power_wells は AUX = **A〜E(5)+ USBC1〜4(4)+ TBT1〜4(4)= 13**、全体 **30 wells** です。全 instance を正本の順序・属性で構成しました:

| 群 | wells | ops | 主な属性 |
|---|---|---|---|
| always_on | 1 | always_on | 所属=**長さ0 list=全domain** |
| PW_1 | 1 | hsw | always_on・has_fuses・**所属=NULL(none)**・id=SKL_DISP_PW_1(8)・idx=0 |
| DC_off | 1 | gen9_dc_off | id=SKL_DISP_DC_OFF(11) |
| PW_2 | 1 | hsw | **has_vga**・has_fuses・id=SKL_DISP_PW_2(9)・idx=1 |
| PW_A〜D | 4 | hsw | has_fuses・irq_pipe_mask(BIT(PIPE_x))・idx=5/6/7/8 |
| DDI_IO_A,B,C,D,E,TC1〜4 | 9 | icl_ddi | idx=0/1/2/7/8/3/4/5/6 |
| AUX_A〜E | 5 | icl_aux | **fixed_enable_delay**・idx=0/1/2/7/8 |
| AUX_USBC1〜4 | 4 | icl_aux | fixed_enable_delay・**enable_timeout=500(WA_14017248603)**・idx=3/4/5/6 |
| AUX_TBT1〜4 | 4 | icl_aux | **is_tc_tbt**・idx=9/10/11/12 |

- **配列位置・論理 ID・hsw.idx は別物**として、id / hsw.idx を i915_reg.h の実値で保存(順序から推測していません)。
- **NULL list=所属なし** と **長さ0 list=全domain** を区別(always_on=all、PW_1=NULL=none)。bitmap 幅 = POWER_DOMAIN_NUM。
- 集約 domain mask を正本 macro 展開で構成:`xelpd_pwdoms_pw_2 = PW_B+PW_C+PW_D+DC_OFF_PORT(DDI lanes C/D/E/TC1〜4)+INIT`、`xelpd_pwdoms_dc_off = DC_OFF_PORT+PW_C+PW_D+PORT_DSI+AUDIO_MMIO+AUX_A+AUX_B+DC_OFF+INIT`。domain→wells map を構成、`parity_power_well_by_id` を追加。
- refcount / hw_enabled(-1=未 sync)は descriptor と別管理(refcount≠HW ON)。

## 3. 試験 — 期待値を正本から再生成(誤った 26/0xe/0x4 を正解に残さない)

ktest 93 → **97 / 0**。GPU-free。生成元=正本、検査対象=移植先。
- `wells==30 && allowed_dc_mask==0x4000000a && target_dc_state==0x2`。
- AUX_TBT4 `is_tc_tbt==1 && hsw_idx==12`、AUX_TBT1 `is_tc_tbt==1`。
- AUX_USBC1 `fixed_enable_delay==1 && enable_timeout==500 && hsw_idx==3`。
- `by_id(PW_1)==1 && by_id(PW_2)==3 && PW_2.hsw_idx==1 && PW_A.hsw_idx==5`。
- `always_on.domains_all==1 && PW_1.domains_all==0`(NULL≠zero-length)、`domain_wells[PORT_DDI_LANES_TC4]!=0`。
- 既存: PIPE_A→always_on+PW_A、DC_OFF→always_on+DC_off、PW_2 has_vga(非 always_on)、refcount/hw_enabled 分離。

## 4. 実機(参照条件 4GiB/4vCPU/39-bit、image 9df2873f、host FLR 後 一回起動)

```
intel_power_domains_init: wells=30 allowed_dc=0x4000000a target_dc=0x2 disable_pw=1 (xelpd map)
attach end: reached=P3 outcome=BLOCKED where=intel_power_domains_init_hw
ktest 97/0 / runner-result last_op=intel_pmdemand_init_early
             blocked_at=intel_power_domains_init_hw cleanup=1 published=0
CPUs ready 4 / panic 0 / source=pit
```

## 5. 次(本増分の続き・staged と理由)

作業順(2)〜(5)を続けます。分量・依存を踏まえ、正本手順で補完します。

| 順 | 作業 | 状況・依存 |
|---|---|---|
| (2) | **PCI probe PM 契約**(local_pci_probe 相当:probe 前に runtime PM 参照取得→継続で次所有者へ/停止・失敗で解放。device 生存参照・PCI 電源状態・i915 wakeref を別記録)。既存 osdep_rpm(get_sync/resume_and_get/put/usage)+ runner へ接続。command=0xffff 解消は予告せず、selftest 順序比較試験は追加しない。 | osdep_rpm は既存。runner/probe を囲む契約が未実装。 |
| (2) | **時間 fault 4 試験を閉じる**(CAS 120/110 後退不発 / 読出失敗・freq 変化検出 / udelay 失敗で reset 非続行 / slow API 異常伝播)。fault は fake backend へ注入、実 probe の時間源を壊さない。電源 HW 初期化の待機 API 受入試験として。 | HAL/kern 側の時間源・waitq に fault-injection seam が必要。 |
| (2) | **pvclock backend**(CLOCKSOURCE2=1 確認済ゆえ CPUID 再採取不要):CPU 毎情報領域確保→MSR 登録→version 確認 snapshot→換算→flag 単調性→共通 API。生 TSC clamp の係数流用でなく正本読出しへ。**pvclock≠高分解能 sleep**(clocksource≠clockevent、timer 基盤に sleep-range 経路)。PIT 較正 TSC は保持。 | 共通 HAL の新規実装(大)。 |
| (2/5) | **ROM resource 取得**(PCI_ROM_RESOURCE:有無/長さ/割当/shadow→割当→decode/map→VBT→unmap/復元)。生 ROM BAR base==0 ≠ resource 不在。取得不可なら既存 missing-defaults。 | **guest drv_pci に ROM resource API が無く**、PCI 層の拡張が必要。 |
| (3) | **well ops**:domain get(所属 well 正順)/put(逆順)→refcount 更新→遷移で ops(enable/disable/is_enabled/sync_hw)。省略しない 3 点:①post-enable(has_vga→intel_vga_reset_io_mem、pipe mask→IRQ post-enable)②post-enable の IRQ lock+intel_irqs_enabled() guard(P4 handler は前倒ししない)③fixed_enable_delay の固定待機は IS_DG2() 条件付き(ADL-P へ無条件適用しない、timeout 値/単位保持)。hw_enabled=-1 を真偽で「有効」と解釈しない。 | ops 本体は MMIO=init_hw と同単位。 |
| (4) | **intel_power_domains_init_hw(false)** 一式:initializing=true→icl_display_core_init(resume=false)→**POWER_DOMAIN_INIT 参照 取得保持**→追加参照→intel_power_domains_sync_hw→initializing=false。display core 順:DC 無効化→PCH+reset handshake→combo PHY(intel_combo_phy_init)→PW1→CDCLK(ADL-P stepping table)→DBUF slice→MBUS→BW_BUDDY(P2 保存 DRAM)→末尾 WA。resume=false は途中 DMC 再ロードしない。未実装子処理は先に実装(半分変更後に未実装 callback 到達を試験手順にしない)。INIT 取得≠30 wells 全 ON。 | 実 MMIO=大。待機契約の時間層依存を先に閉じる。 |
| (5) | **cleanup 状態機械**(power_map_initialized/pmdemand_early_initialized/power_hw_init_started/completed/init_wakeref_owned/dmc_initialized)。HW 後の終了は intel_power_domains_driver_remove() 対応(well 有効のまま+runtime PM 参照)、全 enable 逆順 disable の独自手順にしない。途中 error は最後完了操作+保有参照記録、実 HW timeout と未実装/時間基盤異常を区別。 | (4) と同増分。 |

到達目標は intel_power_domains_init_hw(false) の完了と、INIT 参照を保持したまま DMC 初期化入口へ進むこと。停止時は親関数名でなく正確な子関数を停止点にします。**保持**:MSI 分離 / runner / 共有 backend / 既存試験、per-pipe vblank、時間 error 伝播、VGA 登録、電源 map(30 wells)。GPU=vfio-pci、drm 非 blacklist、attach 先行。描画再試験・GPU-hang 探索へは戻りません。

`last_completed_op` = intel_pmdemand_init_early、`blocked_or_failed_op` = intel_power_domains_init_hw、保持電源参照 = なし(HW 電源 get は init_hw で発生)、cleanup=1 / published=0。
