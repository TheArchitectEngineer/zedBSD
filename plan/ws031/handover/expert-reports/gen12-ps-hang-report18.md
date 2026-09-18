# Gen12 PS ハング 第18報 — zedBSD 基盤監査 第1ラウンド。複数除外、PAT 修正も C2 は停止

ご指示の 3 監査範囲を調べ、**forcewake / ring prologue / ctx WA 内容 / PAT** を確認しました。
いくつかは正しく、PAT には差がありましたが、それを修正しても C2 は停止したままです。次の具体的リードを伺います。

## 監査結果

### 除外できたもの
- **forcewake**: `drv_i915_write32` は auto-forcewake しませんが、`drv_i915_engines_start` が
  **I915_FORCEWAKE_ALL をデバイス寿命の間 常時保持**（"taken for the life of the device"）。
  → 0xe000 系（ROW_CHICKEN2/4, SAMPLER_MODE）の WA 書込は着地しています。
- **ring prologue（gen12_emit_flush_rcs 相当）**: request.c の prologue は
  `PREPARSER_DISABLE + PIPE_CONTROL(invalidate) + PREPARSER_ENABLE`。invalidate は COMMAND_CACHE / TLB /
  **INSTRUCTION_CACHE** / TEXTURE / VF / CONST / STATE_CACHE を含み包括的。C0 も通るので健全。
- **ctx WA 内容**: apply_engine_workarounds が ROW_CHICKEN2/4, SAMPLER_MODE, CS_DEBUG_MODE1,
  FF_THREAD_MODE, PSMI_CTL, FF_SLICE_CS_CHICKEN1, CMD_CCTL を適用。**draw は全 ctx WA
  （COMMON_SLICE_CHICKEN3, CS_CHICKEN1, HIZ_CHICKEN, COMMON_SLICE_CHICKEN4）を適用してもハング**したので、
  ctx WA レジスタ内容は差ではありません。

### PAT に差 — ただし THE fix ではない
zedBSD は **Gen12 PAT テーブル(0x4800..0x481c)を programming していませんでした**（Linux tgl_setup_private_ppat
相当が欠如）。実機デフォルトを読むと:
```
pat default 0x4800=0x00000003(WB)  0x480c=0x00000003(WB)
```
→ **index 0（object/instruction ページ）は元から WB で正しい**。index 3（scratch/page-table、本来 UC）が
WB で誤り。Linux 値 {WB,WC,WT,UC,WB,WB,WB,WB} で programming し index 3 を UC に修正。
```
pat set     0x4800=0x00000003(WB)  0x480c=0x00000000(UC)
```
**それでも C2(walker) は同一署名でハング**（row=0x8610e87f）。→ object ページの caching は元から正しかったため、
PAT は EU ハングの原因ではありません（index 3 修正は Linux 一致の正しい修正なので保持）。

## 現状の切り分け
除外済み: windower / sample-mask / store / 命令 VA / batch content（E-25）+ forcewake / ring prologue /
ctx WA 内容 / PAT。C2/C1 の state は Linux で完走（E-25、EU 書込確認）。問題は zedBSD 基盤のどこか一点ですが、
コード監査では未特定です。

## 伺いたいこと
1. **動作する Linux(c2replay 成功環境)と zedBSD の GPU レジスタ状態を直接 diff** するのが決定的と考えます。
   igt-gpu-tools の intel_reg か GPU MMIO BAR の mmap で、両側のレジスタをダンプして差分を取り、
   「Linux が設定し zedBSD が設定していないレジスタ」を実験的に特定する方針でよいでしょうか。
   その場合、**EU スレッド実行に直結する範囲として優先的にダンプ・比較すべきレジスタ群**（例: GT_MODE,
   GAMTARBMODE, L3 config(0xB100 群), RPCS/EU enable, thread-dispatch, GEN12 の特定 GT init レジスタ）を
   ご教示ください。全レジスタ dump は広すぎるので、EU dispatch/実行に効く範囲に絞りたいです。
2. あるいは、残る最有力候補として **golden-context（__engines_record_defaults）で初期化される、
   ring では適用できない context state**（fresh context のデフォルトレジスタ値のうち EU が必要とするもの）が
   考えられます。ここを「診断で確認」する具体的方法（Linux の LRC image と zedBSD の LRC image を
   同じ context register 範囲で比較する等）はありますか。zedBSD の init_regs は offsets table の
   サブセットのみ設定しています。
3. c2replay を使った限定的な逆向き試験として、「Linux の context に、zedBSD が設定しない register を
   意図的に load せずに」EU を止められるかを見る方法は、この段階で有効でしょうか（一つの register 差を
   仮説として持ってから、という前回のご方針を踏まえつつ）。

（PAT index3 修正は保持。Linux dev VM(c2replay) 維持。GPU vfio-pci。default ビルド warning 0。
 これまでの除外で、残るは「Linux が通す EU 実行前提のうち zedBSD に欠けている一点」の特定です。）
