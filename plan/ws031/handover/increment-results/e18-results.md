
## p011 増分E-18 (2026-09-16): 低位 VA でも同一ハング — bit32/高位 VA 説を棄却

専門家 §4 の低位/高位 VA テストを実装。命令ページを `drv_i915_ppgtt_insert` で明示 VA にマップし、
SBA Instruction Base をそこへ向ける。surface/dynamic base は現行の高位 VA のまま（C0 で健全確認済み）、
**命令フェッチのアドレスだけ**を動かす。C2(store なし)カーネルを使用。

### 結果
```
compute C0     inst_base=0x100400000  ready=✓ eu=初期 done=✓ cs=✓ completed=3 seqno=3   （完走）
compute C3-low inst_base=0x00800000   ready=✓ eu=dead done=dead cs=dead completed=3 seqno=4
  HANG: ipehr=0x70040000(post-walker MEDIA_STATE_FLUSH) row_instdone=0x8610e87f （PS/C1/C2 と完全同一）
```
電源 ACK/MCR は C0/C3-low とも baseline と同一（健全）。fault=0。

### 結論
- **4GB 超 Instruction Base（bit32）は原因ではない**。命令ベースを低位 0x800000 に変えても同一ハング。
  smoking gun と見た「全 VA が 4GB 超」は、少なくとも命令フェッチ経路の直接原因ではなかった。
- C0(walker なし)だけ完走、C1/C2/C3-low/PS(walker/primitive で EU スレッド dispatch)はすべて同一署名でハング。
  → **EU スレッドが dispatch された瞬間、命令ベース VA・kernel 内容・pipeline(3D/compute) に依らず
  無条件に同一ハング**。windower/PS 固定機能/store も無関係。

### caveat（要確認）
- C3-low の低位マッピングが実際に有効だったかは、C3-low での GPU 読み戻しを入れていないため未検証
  （fault=0 なので未マップ scratch フェッチの可能性は残るが、C0 の高位読み戻し一致と同一署名から、
  「VA 無関係」が最尤）。次イテレーションで C3-low の低位 VA からの MI_COPY 読み戻しを追加して確定可能。

### 残る仮説（有力順）
1. **EU の send メッセージ（hdc1 store / ts EOT / render RT-write）が完了しない** — 共通の shared-function
   dispatch / message gateway 経路の故障。C1 の無条件 store 未着地、C2 の EOT 未完了、PS の RT-write 未完了が
   すべて「send が完了しない」で一貫説明できる。thread は fixed-function から見て永遠に未 retire。
2. EU スレッド起動環境（kernel_context の LRC/state に、EU スレッド実行に必要な共通設定の欠落）。
3. 命令フェッチが VA 非依存で共通失敗（I-cache/L3 経路、ただし CS 読みは成功）。

### 次
- 専門家の外部対照（同一 GPU/VFIO の Linux ゲストで EU workload 成功可否）で「物理/パススルー」vs
  「自作 init/context/memory」を切り分けるのが有力。
- または C3-low 低位読み戻しで VA 説を完全排除 → send 完了経路 / LRC EU 設定へ。
default ビルド warning 0。
