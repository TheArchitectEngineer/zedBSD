
## p011 増分E-17 (2026-09-15): C2(store なし)も同一ハング — EU 命令フェッチが prime suspect

専門家指定の ①C0 に GPU 読み戻し追加、②C2(store なし・正規 EOT) 実行、③電源 ACK/MCR 記録を実装・投入。
順序を C0→C2→C1 にして C2 を clean engine で先に走らせた（C1 が wedge する前に）。

### GPU 読み戻し（C0, MI_COPY_MEM_MEM PPGTT→PPGTT）= 完全一致
- IDD readback = `00000400 0 00100000 0 0 0 00000001 0` = 期待値と**完全一致**
  （KSP=0x400, ThreadPreemptionDisable, NumThreads=1）。
- kernel readback（36 DW）= store カーネルのバイト列と**完全一致**。
→ **GPU(CS) は IDD もカーネルも正しい PPGTT VA から読める**。heap 書き込み・上書き・アドレス変換・
  公開はすべて正常。専門家の分岐「一致 → CS はその VA を読める（ただし EU I-cache 経路は別）」。

### 電源 ACK / MCR（baseline/C0/C2 すべて同一・健全）
`eu_dis(0x9134)=0x0`（EU 無効なし）、`slice_ack(0x804c)=3`、`ss01_eu_ack(0x805c)=3`、
`ss23_eu_ack(0x8060)=3`（EU 給電済み）、`mcr(0xfdc)=0x80000000`（multicast 復元済み、単一 instance 固着なし）。

### C2（store なし・正規 EOT）= C1 と完全同一署名でハング【核心】
refcs_empty（brw_compile_cs, 32B）= `(W)mov r127 r0; (W)send.ts {EOT}`（R0 由来 payload の正規 compute
終了。bare send.ts ではない、gentool 確認済み）。
```
compute C2 ready=0xc0ffee10 eu=0xdead0000 done=0xdead0000 cs=0xdead0000 completed=3 seqno=4
HANG: ipehr=0x70040000(post-walker MEDIA_STATE_FLUSH) row_instdone=0x8610e87f instdone=0xffdeffff sc=0xffffffff
```
- **メモリ store が一切ないのに、C1(store)・PS と完全同一署名でハング**。
- markerReady 更新（walker 発行）だが done/cs 未更新（post-walker flush でスレッド完了待ち停止）。
→ 専門家判定「A64 store なしでも停止 → EU 実行環境の共通故障」。ハングは store・windower・PS 固定機能とも無関係。

### 収束した結論
- windower dispatch / sample-mask / PS 固定機能 / A64 store / RT write: **すべて原因でない**（compute にこれらは無い）。
- submission / PPGTT / state / SBA / VFE / MIDL / walker 発行 / post-sync: **すべて健全**（C0 成功 + GPU 読み戻し一致）。
- EU 電源・fuse・MCR: **健全**。
- **残る: dispatch された EU スレッドが命令を実行/完了しない**。C1 の無条件 store（早い命令）が未実行だった
  ことから「EU が命令を実行していない」が有力 → **EU 命令フェッチ（Instruction Base 相対 KSP=0x400、
  実効 VA=0x1_00400400、4GB 超）が成立していない**が prime suspect。CS のメモリ読みは通るが EU I-cache 経路は別。

### 次（専門家 §4）
命令領域だけを低位 VA(0x00800000) vs 高位 VA(0x1_00800000) にマップして C2 を走らせ、
L だけ成功なら高位 VA を含むアドレス経路（自作 SBA/PTE/変換）の不具合を確定。
両方失敗なら命令フェッチ以外へ。要 VA 配置制御（GEM allocator or bind-at-VA）。
default ビルド warning 0。i915.c は draw スキップ中、compute を rt の後に配置。
