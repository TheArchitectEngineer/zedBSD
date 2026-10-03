# WS031 E-121 報告 — native 第一枠の結果、N0 記録の改善、N1 と OpRegion の準備

日付: 2026-09-20　HAL・既存 UAPI の変更はありません。commit／push もしていません。

## 0. 要約

| 項目 | 結果 |
|---|---|
| native 第一枠（E-120 イメージ、利用者が実行） | **想定どおり N0 で「display へ書く前に STOP」**（active pipe A）。teardown が完了し「runner thread end」に到達、表示は firmware のまま。写真: `e120-photos/native-n0-first.jpg` |
| native で得た値 | VBT は OpRegion の RVDA から取得でき、valid=1、sha256 は明示 pin と同じ先頭 3bff4a09…。VT-d の GSTS=0x40000000（TES=0、RTPS のみ）、PMEN=0。firmware の framebuffer は 0x4000000000（640×480、GGTT page 0..300、driver が書く範囲と重ならない）。pipe A: TRANSCONF 0xc0000000、**TRANS_DDI_FUNC_CTL 0x8a210102**（利用者が確認。Linux 値との違いは bit8 VC_PAYLOAD_ALLOC だけで、正本はこの bit を MST の経路でしか使わない）、PLANE_CTL 0x94000008、SURF 0、stride 2560。**DPLL1 が有効で lock 済み（0xcc000000）**。PP は ON、backlight PWM は有効 |
| N0 記録の改善（レビュー §3.2／§4／§3.3） | pipe を 4 区分で記録（READ_ERROR は inactive に丸めない）。GSTS を全 bit 復号し、IRES は MSI 経路の条件として記録。主な停止理由と観測した全条件を分けて記録（後に控える `intel_opregion_register` の壁も含む）。N0 が観測した VBT と parser が消費した VBT を、sha と「同じ byte か」で対応付け。N0 より前の PCI COMMAND（firmware の値と現在の値）と、実施済みの操作を記録 |
| 非整列の OpRegion mapping（§3.1） | HAL の `device_window_map` が、両端の部分 page を含めて offset を保ったまま map する（`hal/amd64/space.c` 2536 行付近）。native でも ASLS …018 から署名・version・VBT hash が正しく読めた |
| N0 より前の副作用と STOP 後の出口（§2） | 調査結果は `notes/pre-n0-side-effects.md`。display register、GGTT PTE、D-state への書込みはない。GT 側と PCI 側の操作（GT reset、fence clear、BME／MSI など）はある。STOP 後の teardown は未初期化の display 停止処理に入らず、PCI COMMAND を firmware の値に戻す |
| **DPLL の危険の除去（N1 の最初の一歩）** | encoder の get_config（combo PHY の clock select）→ PLL に active pipe を帰属させる readout を移植。DPLL sanitize は active な pipe を持つ PLL を切らない。PLL を特定できない（TC）場合は何も切らない。GPU なし試験に 3 件追加（native と同じ状態＝DPLL1 を保持、使われていない PLL は正本どおり停止、TC は保持） |
| OpRegion の登録（§7） | 範囲の整理は `notes/opregion-register-scope.md`。mboxes 0x1d では SWSCI は対象外。**ACPI notifier chain が zedBSD にないため、`drdy=1` を忠実に扱えない**（判断依頼、§3） |
| N1 の依存表（§6） | `notes/n1-takeover-plan.md`。正本の順序、移植状況、4 つの区分（表示を変えずに準備／旧状態を保つ／readout の後／停止を確認した後）、実行位置（P5c〜P5e）、firmware の fb の予約 |
| 回帰 | **13/13 PASS**（`sweep_e121.sh`、各 GPU なし試験 539/0、全 run で N0 は PROCEED、HW 由来の TLB timeout は 0 件） |
| E-121 の USB イメージ | `zedbsd-native-e121.img`（sha256 82fbd045…a93e）。次の native 採取では、新しい記録（区分、IRES、VBT の対応、DPCLKA、条件の全体）が 1 回で得られる |

## 1. N0 記録の具体形（VM での出力例）
```
N0 vt-d GSTS decoded: TES=0 RTPS=0 … IRES=0 … | PMEN EPM=0 PRS=0 (not register values)
N0 VBT: observed none 0 bytes | adopted by the parser: EXPLICIT_BLOB 8704 bytes sha256=3bff4a0920d55c9a.. | same bytes=0
N0 pipe A: POWER_OFF -- not_readable (…)
N0 decision: PROCEED -- …
N0 conditions: primary=0x0 observed=0x0 []
N0 before-N0: PCI COMMAND firmware=0x0007 now=0x0007 | done already: GT reset …, fence clears (0..31), … | not done: any display register, any GGTT PTE, any D-state change
```
native の予想（host 試験 NATIVE-1）: `primary=ACTIVE_PIPE observed=[ACTIVE_PIPE OPREGION_REGISTER]`。VBT の行は「observed OPREGION(RVDA) … same bytes=1」の見込み（今の build は明示 blob を parser に渡しているため）。

host 試験: native-decide **14/0**（+6: 複数の壁、IRES は停止にしない、READ_ERROR、POWER_OFF、VBT の不一致）、opregion 11/0、lcd-modeset 123/0、lcd 56/0、dp 72/0。

## 2. DPLL の readout（`display_nogem.c` の `parity_intel_dpll_readout`）
正本: `icl_ddi_combo_get_config` → `intel_ddi_get_clock(icl_ddi_combo_get_pll())` = `_icl_ddi_get_pll(ICL_DPCLKA_CFGCR0, CLK_SEL(phy))` で `crtc_state->shared_dpll` を決め、`readout_dpll_hw_state` で、それを使う active な crtc ごとに `pipe_mask |= BIT(pipe)`、`active_mask = pipe_mask`。`intel_dpll_sanitize_state` は `on && !active_mask` の PLL だけを止める。
- 以前は pipe_mask が常に 0 で、native では DPLL1 を切ってしまう経路でした（N0 がその手前で止めていました）。
- PLL の wakeref（`pll->info->power_domain`）はまだ取っていません（N1 の区分 B で扱う）。

## 3. 判断依頼: OpRegion の登録
`intel_opregion_register` は `register_acpi_notifier` の後で `drdy=1` を立て、firmware からの ACPI video event（0x80）に `csts` で応答する契約です。zedBSD には AML と notifier chain がありません。
- **A（調査の推奨）**: ACPI event を受ける手段ができるまで、今の BLOCKED を保つ。その間に、setup（実 mapping の保持、`chpd=1`、`ardy=NOT_READY`）、DIDL／CADL、ASLE の処理（GSE → asle_work）、正しい順序の unregister を準備する。
- **B**: 「ACPI video notifier なし: 0x80 event の csts を処理しない」をレビュー済みの適応として記録し、`drdy=1` を立てる（firmware 側の契約上安全かは未確認）。

native で LCD-D まで進むには、N1 に加えてこの判断が必要です。

## 4. 次の作業
1. native 第二の採取（E-121 イメージ）: 新しい記録を得る。特に DPCLKA（DPLL1 の見込み）、IRES、条件の全体。
2. N1 の区分 A／B: 初期 plane の readout（`skl_get_initial_plane_config` の寸法規則）、GGTT page 0..300 の予約、encoder と crtc の power domain の参照、PLL の wakeref、active な pipe の vblank_on。区分 C／D へ進むのはその後です。
3. OpRegion は §3 の判断に従います（A なら準備部分から）。
4. 同じ context を再利用する renderer は、引き続き未着手です。

## 5. 成果物
- 報告: `expert-reports/report-e121-n0-n1-prep.md`
- notes: `pre-n0-side-effects.md`、`opregion-register-scope.md`、`n1-takeover-plan.md`
- 作業 script: `handover/tools/lcd-e121/`（round61〜64）、`sweep_e121.sh`
- 実機 log: `e121-run-parity-hw-*.log`、native の写真は `e120-photos/native-n0-first.jpg`
- USB: `C:\Work\qemu-work\usb\zedbsd-native-e121.img`、`README-native-e121.md`
