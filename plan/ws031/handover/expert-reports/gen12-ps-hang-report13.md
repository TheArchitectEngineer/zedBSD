# Gen12 PS ハング 第13報 — C3-low 読み戻し=有効、VA 説を確定棄却。最有力容疑: context-restore WA の完全欠落

ご指示の ①C3-low 全バイト読み戻し、②context-restore WA 監査を実施しました（③Linux 対照は下記に方針）。

## ① C3-low の低位読み戻し = 有効。VA 説を確定的に棄却
walker なしの C3-low-rb（命令ページを低位 0x800000 にマップ、Instruction Base=0x800000）で、
低位 VA から MI_COPY 読み戻し:
```
C3-low-rb IDD readback    = 00000400 0 00100000 0 0 0 00000001 0   （一致）
C3-low-rb kernel_rb(0x800400) = 80030061 7f050220 00460005 0 / 80030131 00000004 70007f0c 0 / 0...
   → C2 empty カーネル 8word と完全一致、以降ゼロ。CS は低位 VA から正しく読める（低位 mapping 有効）
C3-low-rb: seqno 完走
C3-low(walker あり): 同一署名でハング（row_instdone=0x8610e87f）
```
→ ご判定表「全バイト一致し C3-low が停止 → bit32 単独原因説を外す」。**命令ベース VA(高位/低位)は
EU スレッドハングと確定的に無関係**。低位 VA テストを有効な negative として閉じました。

## ② context-restore WA 監査 = 完全欠落【最有力容疑】
ご指定の `gen12_emit_indirect_ctx_rcs()` / Wa_18022495364 を監査した結果:
- `RING_INDIRECT_CTX(0x1c4)` / `RING_INDIRECT_CTX_OFFSET(0x1c8)` / `RING_BB_PER_CTX_PTR(0x1c0)` は
  **レジスタ定義のみ存在**。
- **LRC に indirect context batch / per-ctx WA batch の設定が一切ありません**（INDIRECT_CTX, BB_PER_CTX,
  wa_bb, CTX_INDIRECT すべてコード上ヒット 0）。
- `CS_DEBUG_MODE2`, `Wa_18022495364`, `INSTRUCTION_STATE_CACHE_INVALIDATE`,
  `gen12_emit_indirect_ctx_rcs` は**コード全体に存在しません**。
- → **kernel_context の context restore 時に、Wa_18022495364（命令ステートキャッシュ無効化）を含む
  indirect-ctx WA が一切適用されていません**。

これは E-16〜E-20 の観測を完全に説明します:
- context restore の間接バッチが無い → EU の命令ステートキャッシュが stale のまま。
- **dispatch された EU スレッドが stale をフェッチ → 一切実行しない → 全スレッド同一ハング**。
- C0(EU スレッドなし)成功、MI/CS/post-sync 成功（EU 命令キャッシュ非依存）、VA 無関係。

## 伺いたいこと — 順序の相談
ご提示の順序は「Linux 外部対照 → 差分に基づき context-restore init path を実装」でした。一方、今回
**indirect context batch が完全に欠落**という具体的なギャップが確定し、これが症状を一意に説明します。

そこで伺います:
1. この確定した欠落を踏まえ、**先に indirect context batch（RING_INDIRECT_CTX 設定 + WA batch ページに
   Wa_18022495364 の MI_LOAD_REGISTER_IMM GEN12_CS_DEBUG_MODE2 | INSTRUCTION_STATE_CACHE_INVALIDATE）を
   context-restore init path として実装**し、直接テストする方が効率的では、と考えますが、いかがでしょうか。
   fix すれば診断確定、しなければ Linux 対照へ、という段取りです。
2. もし Linux 対照を先にすべき理由（この WA を入れても物理/パススルー側の問題が残り得るため等）があれば
   ご教示ください。その場合は eu_positive_control.py での Linux 陽性対照を先に取ります（ホストの vfio 設定は
   変えず、Linux ゲストを enable_guc=0 で起動、同一 8086:46a8 パススルー）。
3. indirect context batch を実装する場合の Gen12 の要点（RING_INDIRECT_CTX_OFFSET の単位/エンコード、
   WA batch のサイズ/配置、context image のどのオフセットに結線するか、restore batch の終端規約）で
   注意点があればご教示ください。参照は Linux の gen12_emit_indirect_ctx_rcs / lrc_setup_indirect_ctx を
   想定しています。

（C3-low readback で VA 説は確定棄却。EU 電源/fuse/MCR 健全、submission/PPGTT/MI/post-sync 健全。
 障害は「dispatch された EU スレッドが命令を実行しない」に完全局在し、context-restore WA 欠落が最有力。
 default ビルド warning 0、実機 run 再起動不要。）
