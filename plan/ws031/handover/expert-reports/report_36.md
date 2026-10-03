# WS031 第36報 — VGA登録 → 表示系電源管理(構造)へ。実機 reached=P3 BLOCKED intel_power_domains_init_hw

第35報後のご指示に沿い、P3 主実装を **intel_vga_register → intel_power_domains_init(xelpd power-well map)→ intel_pmdemand_init_early** まで進めました。実機(参照条件、host FLR 後 一回起動)で **reached=P3 BLOCKED intel_power_domains_init_hw**、selftest **ktest 93/0**、`source=pit`、CPUs 4、panic 0。per-pipe vblank / 時間 error 伝播 / BIOS 既定生成は保持。GPU=vfio-pci、drm 非 blacklist、attach 先行維持。

ご指示のうち **完了**:log 分離修正、attach 先行の言い回し是正、intel_vga_register、intel_power_domains_init(構造+xelpd map)、intel_pmdemand_init_early、KVM clock-feature 採取。**次 increment へ明示的に送った**もの(理由付き):§7。

---

## 1. log 分離(即修正・完了)

`parity_result` に `last_completed` を追加し、runner を分離しました。
- `last_completed_op` = 実際に完了した関数(`pr.last_completed`)
- `blocked_or_failed_op` = 停止点(`pr.where`)

実機:`last_op=intel_pmdemand_init_early blocked_at=intel_power_domains_init_hw`(以前は両方同一)。未実行の関数を「最後に完了」として記録しません。

## 2. attach 先行(維持)— 言い回しの是正

ご指摘どおり断定を避け、記録を次の表現に改めました:

> **selftest 後の probe で config 読出し不成立(command=0xffff)を観測。attach 先行で回避できた。電源遷移との関係は未確認。**

- 「D3 自動 suspend が原因と確定」「parity コードは正しい」とは結論しません(失敗時の電源状態遷移や host/guest 両側の同時点状態は未取得)。
- ホストの `disable_idle_d3=Y` / `power/control=on` / FLR→D0 でも guest 側 config 不成立は不変でした(=単純な D3 遷移では説明しきれない)。
- **重要**:attach 先行は当座の回避であり、電源管理の代わりにはしません。速さに依存しない所有権(PCI/runtime PM 契約)は §7 の次 increment で実装します。今回はそれを attach 先行で「間に合わせ」たままにしています。

## 3. intel_vga_register(vga.{c,h}・完了)

正本 intel_vga_register を移植:`vga_client_register(pdev, intel_gmch_vga_set_decode)`。
- **-ENODEV の扱い**:GPU の PCI class が VGA(0x030000)なら arbiter client 登録、非 VGA(secondary controller)なら `-ENODEV` を**許容**し、その他 error は返す。`return 0`/plane 無効化/偽 -ENODEV にはしていません。
- **decode callback**:`intel_gmch_vga_set_decode` → `intel_gmch_vga_set_state`。**host bridge 00:00.0(i915->gmch.pdev = `drv_pci_find_device({0,0,0,0})`)の GMCH_CTRL(display ver≥6 は 0x50)を 16-bit masked read-modify-write**(GPU function ではない)。enable 時は LEGACY|NORMAL の IO/MEM、disable 時は NORMAL の IO/MEM のみを返す。既に要求状態なら書かない。
- unregister で callback 解除。
- 実機:`intel_vga_register: client=registered gmch_bridge=found ret=0`(GPU=VGA class ゆえ登録、bridge 00:00.0 検出)。
- **台帳分離**:intel_vga_disable/redisable と /dev/vga_arbiter ユーザ IF は現 P3 経路に不要のため未実装。

## 4. intel_power_domains_init(power_domains.{c,h}・構造完了)

- 設定正規化(`sanitize_disable_power_well`)、**`allowed_dc_mask = get_allowed_dc_mask`(ADL-P=display ver13≥12 → DC9|DC3CO|UPTO_DC6 = 0xe)**、`target_dc_state = sanitize(UPTO_DC6)= 0x4`、`mutex`、`async_put_work`。
- **intel_display_power_map_init**:ADL-P=display ver13 → **xelpd descriptor 群を正本順・属性で構成**:
  - always_on / PW_1(DMC 制御・always_on・has_fuses) / DC_off / PW_2(**has_vga**・has_fuses) / PW_A..D(per-pipe・irq_pipe_mask) / DDI_IO_A..E,TC1..4(icl_ddi ops) / AUX_A..E,TC1..4(icl_aux ops) = **計 26 wells**。
  - 各 well の power-domain 所属(PW_A={PIPE_A,PANEL_FITTER_A,INIT} 等、PW_2/DC_off は pipe/transcoder/DDI-lanes 等)から **domain→wells map** を構成。
  - **refcount / hw_enabled(-1=未 sync)を descriptor と別管理**(refcount 非ゼロ=HW ON とは読み替えない)。この関数内で全電源を ON にはしません。
- 実機:`intel_power_domains_init: wells=26 allowed_dc=0xe target_dc=0x4 disable_pw=1 (xelpd map)`。

## 5. intel_pmdemand_init_early(完了)

`mutex` + `waitqueue` をこの位置で初期化(display version に関係なく、正本の noirq 順)。実機 trace に `intel_pmdemand_init_early` acquire。

## 6. KVM clock-feature 採取(track C・完了)+ 言い回し是正

CPUID **0x40000001 EAX** を一回採取・記録しました。実機:

> `A64 TIMECOUNTER KVM clock-features 0x40000001 eax=0x1007efb clocksource=1 clocksource2=1 stable_bit=1 -> PIT-calibrated TSC (pvclock backend TODO)`

- **CLOCKSOURCE2(bit3)= 1** を確認 → ご指摘のとおり **pvclock backend(kvmclock.c/pvclock.c 準拠)が正しい将来 path** です(次 increment)。
- 「KVM signature ゆえ raw TSC 信頼」の文言・分岐説明を除去。**signature ≠ clock feature**。現状は **PIT 較正 TSC を検証済み backend として採用**(`source=pit` 維持、cross-CPU 検証あり)し、pvclock 利用済みとは扱いません。

## 7. HW 検証と試験

### GPU-free selftest:ktest 83 → 93 / 0
- vga:decode enable=LEGACY+NORMAL / disable=NORMAL / bridge 無で set_state=-ENODEV。
- power:domains_init 26 wells + DC mask、PIPE_A→always_on+PW_A、DC_OFF→always_on+DC_off、PW_2 has_vga(非 always_on)、refcount/hw_enabled 分離、cleanup。
- pmdemand:init_early。

### 実機(参照条件 4GiB/4vCPU/39-bit、image d64c01c4、host FLR 後 一回起動)
```
KVM clock-features eax=0x1007efb (cs=1 cs2=1 stable=1) -> PIT-calibrated TSC
TIMECOUNTER READY source=pit hz≈2.497GHz / CPUs ready 4 / panic 0
--- 実デバイス attach ---
P0 command=0x0007 → P1(BAR/fuse) → P2(pci_set_master/DRAM rc0/BW rc0/ASLS0)
P3 intel_vga_register: client=registered gmch_bridge=found ret=0
P3 intel_power_domains_init: wells=26 allowed_dc=0xe target_dc=0x4 disable_pw=1
P3 intel_pmdemand_init_early (acquire)
attach end: reached=P3 outcome=BLOCKED where=intel_power_domains_init_hw
teardown: power domains map → VGA client → DRM+vblank → MSI → WC unmap rc=0
--- selftest ---
ktest 93/0 / runner-result last_op=intel_pmdemand_init_early
             blocked_at=intel_power_domains_init_hw cleanup=1 published=0
```

## 8. 次 increment(明示的に staged・理由付き)

今回は最低到達(vga 越え + power_domains_init/pmdemand_init_early を実処理完了)を確実に達成し実機検証しました。以下は分量・依存の都合で次 increment に送ります(いずれも「先へ進めた対処」でなく Linux 手順で補完):

| 項目 | 内容 | 理由 |
|---|---|---|
| **PCI probe PM 契約**(作業3) | local_pci_probe 相当:probe 前に runtime PM 参照取得→成功/失敗/削除経路で解放、device 生存参照と HW 電源参照を別記録、pci_enable_device の D0 移行。probe 前後の PM 参照/resume/PMCSR trace。 | osdep/runtime_pm + PCI 適合層の拡張が必要。attach 先行は当座維持しつつ、これで速さ非依存の所有権に置換。 |
| **ROM resource 取得**(作業3) | PCI_ROM_RESOURCE 相当を経由(有無/長さ/割当/shadow 確認→必要なら割当→decode→map→VBT→unmap+復元)。生 ROM BAR=0 ≠ resource 不在。 | 現 bios.c は生 BAR base==0 を即「不在」扱い。PCI 資源側にサイズ/割当責務を移す層拡張が必要。 |
| **intel_power_domains_init_hw**(5.2) | icl_display_core_init 経路(DC state/PCH handshake/combo PHY/PW1/CDCLK/DBUF/MBUS/BW)、power-well ops(hsw/icl_ddi/icl_aux/dc_off)の MMIO 本体、hsw.idx/SKL_DISP_PW id 実値、POWER_DOMAIN_INIT 参照を P3 末で落とさない、状態フラグ(power_map_initialized/pmdemand_early_initialized/power_hw_init_completed/dmc_initialized)。実 delay/poll は時間層依存を閉じてから接続。 | 大規模。実 MMIO は待機契約の未実装項目に依存。 |
| **時間層 検証**(track C) | 既決の 4 fault 試験(120/110 CAS 後退不発 / 読出失敗・freq 変化 / udelay 失敗で reset 非続行 / slow API 異常伝播)+ pvclock backend(CLOCKSOURCE2 確認済)+ 高分解能 sleep-range。 | HAL 側 fault-injection seam が必要。既存修正(CAS/udelay/fault/freq)は保持、検証を閉じる。 |
| **有効 VBT parse 深化** | general features/definitions/driver features/compression の実 parse+状態保存。 | 現状は header + block walk まで。無 VBT 経路(既定生成)は保持。 |
| **集約 well domain mask** | PW_2/DC_off の網羅的 per-domain mask(深い nest macro 展開の残余)。 | 現状は主要 pipe/DDI/AUX/transcoder domain を忠実移植済。 |

**保持**:MSI 分離 / runner / 共有 backend / 既存試験、per-pipe vblank、時間 error 伝播、BIOS 既定生成。GPU=vfio-pci、drm 非 blacklist、attach 先行。描画再試験・GPU-hang 原因探索へは戻りません。

`last_completed_op` = intel_pmdemand_init_early、`blocked_or_failed_op` = intel_power_domains_init_hw、保持電源参照 = なし(HW 電源 get はまだ・init_hw で発生)、cleanup=1 / published=0。
