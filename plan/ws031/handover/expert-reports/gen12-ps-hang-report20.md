# Gen12 PS ハング 第20報 — 戦略転換を受領。golden-context lifecycle 欠如を確認、資料添付、実装計画

ご方針（最後の候補に賭けず、Linux 準拠の context ライフサイクル一式を累積移植単位として実装、状態採取を
組み込む）を受領しました。ご指摘の解釈修正も反映します。別添 `zedbsd-materials.txt` に、ご要望の
関数本体（context 生成/初期化/保存/復元、request emitter、PTE 処理）をまとめました。

## 解釈修正の反映
- **「残りは LRC の一点」は未確定**として扱います。GLOBAL レジスタ一致は「同じ init/保存/同期を経た」証明では
  ないこと、Linux に任せた範囲に PPGTT/メモリ/提出完了管理も含むことを、除外に使いません。
- **CS_CHICKEN1=0x01**: ご指摘どおり global read では ctx WA(GPGPU preempt thread-group, bits2:1)が見えません。
  0x2580 の global 値は現在ロード context の値で、ctx WA は context 内。zedBSD の **compute selftest は
  context WA を L3ALLOC+FF_MODE2 しか ring 適用しておらず、CS_CHICKEN1 等を欠いています**（draw は ring で
  適用したがハング）。→ 差の本質は「値」でなく「ring-per-request 適用 vs golden-context 保存」の機構。
- **SAMPLER_MODE**: 0x3020 再試験はしません。ENABLE_SMALLPL(bit15) は Linux 経路に根拠があるので保持。
  0x3020 変更は revert 済み。SAMPLER_MODE 変更単独で停止不変、を「原因の一部でない」の除外にはしません。
- **bit3(INHIBIT_SYN_CTX_SWITCH) はクリアしません**（Linux init_common_regs も設定）。**RPCS=0x80041000 は
  維持**（1 slice の正規値、ご計算と一致）。単独変更試験はしません。

## 確認した gap（別添の関数本体より）
zedBSD の現状（`drv_i915_lrc_create` → `i915_lrc_init_regs`）:
- 単一 kernel_context を engine ごとに1つ作成、14 ページ image を GGTT bind。
- init_regs は **offsets table(gen12_rcs_offsets) のサブセット**を LRI 展開 + CONTEXT_CONTROL(初回 inhibit)
  + RPCS + PDP0(PML4) + ring レジスタを設定するのみ。
- **golden-context bootstrap（context A で ctx WA 適用→切替保存→default_state 継承）が皆無**。
- ctx WA は per-request の ring extra[]（compute は L3ALLOC+FF_MODE2 のみ）+ apply_engine_workarounds(MMIO global)。
- 提出は force-restore 付き ELSP（`drv_i915_lrc_submit`）。
→ **Linux の __engines_record_defaults / emit_ctx_wa（前後 barrier 込み）/ default_state 継承の経路が丸ごと欠如**。

## 実装計画（ご指定の単位、累積保持）
既存の累積修正(PAT, indirect-ctx, 全 GT/engine 初期化)の上に:
```
bootstrap context A を正当な初回 restore-inhibit で開始
  → 対象 Linux 経路の ctx WA を前後 barrier 込みで batch 適用（emit_ctx_wa 相当）
  → MI-only 完了確認
  → 別 context B へ切替、A の switch-out/保存完了を確認（context_flush 相当: 別 kernel request に依存させ待機）
  → A の保存済み engine-state を default_state として保持
  → test context C へ継承（engine-state 領域コピー、HWSP/ring/PPGTT root/WA 参照先は C 固有に初期化、memset で消さない）
  → C を restore して既存の C0 → C2 → C1
```
状態採取（受入試験として組込）:
- **S1**(ctx WA 適用後) / **S2**(保存・復元後、per-request WA 再適用より前) / **S3**(C2 起動直前) を、対象 context の
  kernel-owned ring から SRM(0x2244 CONTEXT_CONTROL, 0x20c8 RPCS, 0x2580 CS_CHICKEN1, 0x2084 CMD_BUF_CCTL,
  0x209c MI_MODE, 0x2270/0x2274 PDP0)で診断領域へ保存。MCR 対象は一律 SRM しない。FF_MODE2 は readback 判定外。
- 保存済み LRC は別 context 切替完了後に1つ採取、register-state ページの LRI 構造 + 意味既知の設定値のみ比較
  （live_lrc_layout/live_lrc_fixed 参照、address/counter は別分類）。
- Linux 側は c2replay が使う実 intel_context 紐付き request で wa_list_srm 相当を採取（kernel context 別読みにしない）。

## 伺いたいこと
1. 別添の関数本体を踏まえ、**この実装単位の依存関係・順序**（特に「A の save 完了確認」の同期、default_state の
   継承範囲=engine-state のどのバイト範囲か、C 固有初期化で上書きしてよい/いけない領域）にご助言をお願いします。
2. ctx WA batch（emit_ctx_wa 相当）の**前後 barrier(EMIT_BARRIER)の実パケット**（Gen12 RCS の emitter が出す形）を、
   ご提示の gen12_emit_flush_rcs / emit_flush の観点でご教示ください。zedBSD の既存 prologue(別添)は
   PREPARSER_DISABLE + PIPE_CONTROL(invalidate) + PREPARSER_ENABLE です。
3. restore-inhibit の初回 A と、継承先 C の inhibit 扱い（C は通常 restore、A は初回 inhibit）の区別で、
   別添 init_regs の CONTEXT_CONTROL 設定（MASKED_ENABLE(INHIBIT_SYN_CTX_SWITCH) | MASKED_DISABLE + set
   ENGINE_CTX_RESTORE_INHIBIT）をどう分けるべきかご教示ください。

（別添: zedbsd-materials.txt。Linux dev VM/c2replay/regdump 維持。GPU vfio-pci。default ビルド warning 0。
 S1/S2/S3・保存 LRC・提出 descriptor/ring 列は、この lifecycle 実装と同時に採取して次報で提出します。）
