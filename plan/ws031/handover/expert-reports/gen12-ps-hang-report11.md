# Gen12 PS ハング 第11報 — C2(store なし)も同一ハング。EU 命令フェッチが prime suspect、smoking gun あり

ご指示の ①C0 に GPU 読み戻し、②C2(store なし・正規 EOT)、③電源 ACK/MCR を実装・投入しました。

## ① GPU 読み戻し（MI_COPY_MEM_MEM, PPGTT→PPGTT）= 完全一致
```
IDD readback    = 00000400 0 00100000 0 0 0 00000001 0   （期待値と完全一致）
kernel readback = store カーネル 144byte と完全一致
```
→ **GPU(CS) は IDD もカーネルも正しい PPGTT VA から読める**。heap 書き込み・アドレス変換・公開は正常。
（ご指摘どおり、これは EU の I-cache 経路まで保証するものではありません。）

## ③ 電源 ACK / MCR（baseline/C0/C2 すべて同一・健全）
```
eu_dis(0x9134)=0x0   slice_ack(0x804c)=3   ss01_eu_ack(0x805c)=3   ss23_eu_ack(0x8060)=3   mcr(0xfdc)=0x80000000
```
EU 無効なし、EU 給電済み、MCR は multicast 復元済み（単一 instance 固着なし）。

## ② C2（store なし・正規 EOT）= C1・PS と完全同一署名でハング【核心】
refcs_empty（brw_compile_cs, 32byte）= `(W)mov r127 r0; (W)send.ts {EOT}`（R0 由来 payload の**正規の
compute 終了処理**、bare send.ts ではない、gentool 確認済み）。
```
compute C2 ready=0xc0ffee10  eu=0xdead0000  done=0xdead0000  cs=0xdead0000  completed=3 seqno=4
HANG: ipehr=0x70040000(post-walker MEDIA_STATE_FLUSH)  row_instdone=0x8610e87f（PS/C1 と完全同一）
```
- **メモリ store が一切ないのに、C1(store)・PS と完全同一署名でハング**。
- markerReady 更新（walker 発行）→ post-walker MEDIA_STATE_FLUSH でスレッド完了待ち停止。
- ご判定表の「C2 が C1 同様に停止 → A64 store なしでも停止 → compute dispatch・命令取得・EU 実行環境・
  終了処理を調べる」に該当。

## 収束した結論
- **原因でないと確定**: windower dispatch / sample-mask / PS 固定機能 / A64 store / RT write（compute に無い）。
- **健全と確定**: submission / PPGTT / SBA / VFE / MIDL / walker 発行 / post-sync（C0 成功 + GPU 読み戻し一致）、
  EU 電源 / fuse / MCR。
- **残る**: dispatch された EU スレッドが命令を実行/完了しない。

## smoking gun — Instruction Base が常に 4GB 超
自作 PPGTT アロケータは `I915_PPGTT_VA_START = 0x100000000`（= 4GB）から bump 割当てします。
そのため **全オブジェクトの VA が 4GB 超（bit32 セット）**で、Instruction Base も常に
`0x1_004xxxxx` です。実効 KSP = Instruction Base + 0x400 = `0x1_00400400`（bit32 セット）。
CS のメモリ読み（MI_COPY）はこの VA で成功しますが、**EU の命令フェッチ（Instruction Base 相対、
I-cache 経由）が 4GB 超で失敗している**可能性が最有力になりました。

## 次段（ご指定 §4 の低位/高位 VA 比較）と伺いたいこと
実装方針: `drv_i915_ppgtt_insert(vm, va, phys, ...)` で命令ページを**明示 VA にマップ**できます。
- L: 命令ページを **0x00800000（4GB 未満）**にマップ、SBA Instruction Base=0x00800000、KSP=0x400。
- H: 命令ページを **0x1_00800000（4GB 超）**にマップ、Instruction Base=0x1_00800000、KSP=0x400。
- ご注意どおり、**選択した側だけにコードをマップ**（両方に置くと bit32 落ちを隠す）。surface/dynamic base は
  現行の高位 VA のまま（C0 で健全確認済み）、命令領域だけ動かします。C2 コードで比較します。

伺いたい点:
1. **Gen12 で EU の命令フェッチ（STATE_BASE_ADDRESS の Instruction Base 相対 KSP）が 4GB 超 VA を
   正しく扱うために、自作側で必要な設定**はありますか。Instruction Base は SBA DW10/11 に lo/hi 64bit で
   与えています（DW10=`1|(mocs<<4)|(va&0xfffff000)`, DW11=`va>>32`）。EU/I-cache が上位 32bit を
   見るための追加要件（別レジスタ、PTE 属性、あるいは Instruction Base に固有の制約）はありますか。
2. L だけ成功した場合、次に監査すべきは自作 PPGTT の **PTE の bit32 以上の物理/仮想の扱い、TLB/I-cache
   同期**という理解で良いですか。
3. この VA テストでも両方失敗する場合の、ご提案の外部対照（同一 GPU/VFIO の Linux ゲストで EU workload）
   の最小手順があればご教示ください。

（C0 完全成功で submission/PPGTT/state 経路は健全と確定済み。障害は EU スレッド実行に局在。
 refcs/refcs_empty/gentool 構築済み。実機 run 再起動不要。default ビルド warning 0。）
