# Gen12 PS ハング 第19報 — レジスタ diff 完了。GLOBAL 全一致、差は CONTEXT/LRC に局在

ご指示の MOCS 3値・LRC 確保・CMD_BUF_CCTL を確認し、さらに EU/L3/GT の実効レジスタを Linux と diff しました。
**GLOBAL レジスタは全て一致**し、唯一の差(SAMPLER_MODE)も原因でないことを実測しました。差は CONTEXT/LRC 側です。

## 確認（ご指示の3点）
- **MOCS 3値**: 実測で `0x4008=0x37, 0x400c=0x05, 0xb024=0x00100030` = ご提示値と完全一致。
  （zedBSD は Global MOCS(0x4000 系)全 64 entry + L3CC(GEN9_LNCFCMOCS)を programming 済み。0xc800 系ではない。）
- **LRC**: RCS render context = `GEN11_LR_CONTEXT_RENDER_SIZE = 14 * PAGE = 0xe000`（14 ページ）を drv_i915_gem_create で
  確保し drv_i915_gem_bind_ggtt で GGTT に連続 bind。基本サイズは正しい。
- **CMD_BUF_CCTL(0x2084)**: zedBSD=0x100、**Linux も 0x100**（intel_reg 実測）→ 一致。zedBSD は CMD_CCTL(0x20c4)のみ
  明示設定だが、CMD_BUF_CCTL の実効値は Linux と同じでした。

## レジスタ diff（zedBSD regdump vs Linux intel_reg、C2 投入前）
`intel_reg`（forcewake と MCR steering を適切に処理）で Linux 実効値を採取、zedBSD は selftest に regdump probe を追加。
```
一致: CMD_BUF_CCTL 0x100 / CMD_CCTL 0x306 / MOCS 0x37,0x05,0x00100030 / MI_MODE 0x200 / CS_CHICKEN1 0x01 /
      GFX_MODE 0x08 / MISCCPCTL 0xfffffffe / DFR_CHICKEN 0x3ff / L3ALLOC 0xd0000020 / L3SQCREG1 0xb3400000 /
      L3SQCREG4 0x40 / ROW_CHICKEN2 0xffff4100 / ROW_CHICKEN4 0xffff0200
差:   SAMPLER_MODE 0xe18c  zedBSD=0xb021  Linux=0x3020
```
SAMPLER_MODE の差 = zedBSD が bit0(INDIRECT_STATE_BASE_ADDR_OVERRIDE)+bit15(ENABLE_SMALLPL)を追加。
→ **zedBSD を 0x3020(Linux 一致)に変更して C2 テスト → regdump で 0x3020 を確認したが C3-low は依然ハング**
（同一署名 row=0x8610e87f）。→ **SAMPLER_MODE の差は EU ハングの原因でない**（revert、Linux は per-context 適用の
可能性があるため元の WA を保持）。

## 帰結
**比較した GLOBAL GT/engine レジスタは全て一致**（MOCS/CMD_*/L3/ROW_CHICKEN/DFR/MISCCPCTL）。
SAMPLER_MODE の差も原因でない。→ **差は GLOBAL 状態でなく CONTEXT/LRC 状態に局在**しました。
唯一の context 手がかり: CONTEXT_CONTROL(0x2244) が zedBSD=0x08 vs Linux=0x00（ただし現在ロード context の
global read なので context 依存）。

## 伺いたいこと
1. GLOBAL が全一致した以上、次は **CONTEXT/LRC 状態の比較**が本命と理解しています。ご提示の
   「context A→B→A で switch-out 後の LRC を採取し、Linux と layout+値を比較」を進めますが、
   **zedBSD 側で C0 後の context 実効状態を SRM で採取する際、優先的に読むべき context レジスタ**
   （CONTEXT_CONTROL, RPCS, MI_MODE, CS_CHICKEN1, ctx WA 群）と、**Linux 側の同じ context 状態を
   採取する方法**（live_lrc_layout/live_lrc_fixed 相当の i915 内部診断、あるいは他の手段）をご教示ください。
2. **RPCS** は候補と考えます。zedBSD は LRC に RPCS=0x80041000（enable+slice count=1）を設定していますが、
   ADL-P(5 DSS)で Linux が設定する RPCS（subslice/EU enable 含む)との差が EU dispatch を妨げる可能性は
   ありますか。RPCS は context 内(CTX_R_PWR_CLK_STATE)なので、両 OS の LRC から採取して比較すべきでしょうか。
3. CONTEXT_CONTROL の diff（zedBSD bit3=1 vs Linux 0）は、現在ロード context の値の global read です。
   これを C0 後の対象 context の実効値として正しく採取するには、SRM(MI_STORE_REGISTER_MEM)で
   0x2244 を読むのが適切でしょうか。bit3 の意味と、EU 実行への影響の有無をご教示ください。

（インフラ: Linux dev VM に intel-gpu-tools 導入済み(intel_reg)、c2replay、zedBSD regdump probe。
 PAT 修正保持。GPU vfio-pci。default ビルド warning 0。GLOBAL 状態の除外により、残りは CONTEXT/LRC の一点です。）
