
## p011 増分E-27 (2026-09-16): レジスタ diff — GLOBAL は全一致、差は CONTEXT/LRC に局在

専門家 §1-2 の指示で、動作 Linux(c2replay 成功)と zedBSD の実効レジスタ値を diff。

### 手法
- Linux: ゲストに intel-gpu-tools 導入、`intel_reg read`（forcewake+MCR steering を適切に処理）で採取。
  （mmap /sys resource0 は forcewake なしで全 0 になり不可。intel_reg が正解。）
- zedBSD: compute selftest の baseline に regdump probe を追加、同一アドレスを MCR multicast で読む。

### 比較結果（zedBSD vs Linux, C2 投入前）
| Reg | zedBSD | Linux | 一致 |
|---|---|---|---|
| CMD_BUF_CCTL 0x2084 | 0x100 | 0x100 | ✓ |
| CMD_CCTL 0x20c4 | 0x306 | 0x306 | ✓ |
| GLOBAL_MOCS2/3 0x4008/0x400c | 0x37/0x05 | 0x37/0x05 | ✓ |
| LNCFCMOCS1 0xb024 | 0x00100030 | 0x00100030 | ✓ |
| MI_MODE 0x209c | 0x200 | 0x200 | ✓ |
| CS_CHICKEN1 0x2580 | 0x01 | 0x01 | ✓ |
| GFX_MODE 0x229c | 0x08 | 0x08 | ✓ |
| MISCCPCTL 0x9424 | 0xfffffffe | 0xfffffffe | ✓ |
| DFR_CHICKEN 0x9550 | 0x3ff | 0x3ff | ✓ |
| L3ALLOC 0xb134 | 0xd0000020 | 0xd0000020 | ✓ |
| L3SQCREG1 0xb100 | 0xb3400000 | 0xb3400000 | ✓ |
| L3SQCREG4 0xb118 | 0x40 | 0x40 | ✓ |
| ROW_CHICKEN2 0xe4f4 | 0xffff4100 | 0xffff4100 | ✓ |
| ROW_CHICKEN4 0xe48c | 0xffff0200 | 0xffff0200 | ✓ |
| **SAMPLER_MODE 0xe18c** | **0xb021** | **0x3020** | **✗** |
| CONTEXT_CONTROL 0x2244 | 0x08 | 0x00 | (context 依存の global read、無効) |

### SAMPLER_MODE の差を検証 — 原因でない
zedBSD は bit0(INDIRECT_STATE_BASE_ADDR_OVERRIDE)+bit15(ENABLE_SMALLPL)を追加、Linux は default(0x3020)。
→ zedBSD の SAMPLER_MODE 書込を default(0x3020, Linux 一致)に変更して C2 テスト → **regdump で 0x3020 を確認
したが C3-low は依然ハング**（同一署名）。→ SAMPLER_MODE の差は EU ハングの原因でない。revert（元の WA 保持、
Linux は per-context で適用している可能性があるため）。

### 帰結
**比較した GLOBAL GT/engine レジスタは全て一致**（MOCS/CMD_*/L3/ROW_CHICKEN/DFR/MISCCPCTL 等）。
SAMPLER_MODE の差も原因でない。→ **差は GLOBAL 状態ではなく CONTEXT/LRC 状態に局在**。
唯一 CONTEXT_CONTROL(0x2244) が zedBSD=0x08 vs Linux=0x00 だが、これは現在ロード context の値を global read
したもので context 依存。context 実効状態(CONTEXT_CONTROL, RPCS, ctx WA)は SRM か LRC 比較で採取が必要。

### 次段（専門家 §4 の LRC/context 比較）
- C0 後に switch-out した context の LRC を採取（context A→B→A 手順）、Linux と構造(LRI layout)＋値(mask 付き)を比較。
- または context 実効レジスタ(CONTEXT_CONTROL, RPCS, MI_MODE, CS_CHICKEN1)を対象 context の ring から SRM。
- 特に RPCS(EU/subslice enable)が候補。zedBSD=0x80041000、Linux 値との比較が必要。
インフラ: Linux dev VM(intel_reg 導入済み), c2replay, regdump probe。GPU vfio-pci。default ビルド warning 0。
