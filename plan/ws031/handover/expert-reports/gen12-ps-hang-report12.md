# Gen12 PS ハング 第12報 — 低位 VA でも同一ハング。bit32 説を棄却、EU の send 完了が新たな主容疑

ご指示 §4 の低位/高位 VA 比較を実装・投入しました。

## 実装
`drv_i915_ppgtt_insert(vm, va, phys, pages)` で命令ページを明示 VA にマップし、SBA Instruction Base を
そこへ向けました。surface/dynamic/IDD は現行の高位 VA のまま（C0 で健全確認済み）、**命令フェッチの
アドレスだけ**を動かしました。C2(store なし)カーネルを使用。ご注意どおり、命令コードは選択した VA だけに
マップ（C3-low では shared 高位ページに命令を置いていません）。

## 結果
```
C0     inst_base=0x100400000  ready=✓ eu=初期 done=✓ cs=✓  completed=3 seqno=3   （完走）
C3-low inst_base=0x00800000   ready=✓ eu=dead done=dead cs=dead  completed=3 seqno=4
  HANG: ipehr=0x70040000(post-walker MEDIA_STATE_FLUSH)  row_instdone=0x8610e87f （PS/C1/C2 と完全同一）
  fault=0  電源ACK/MCR は baseline と同一
```

## 結論
- **4GB 超 Instruction Base（bit32）は原因ではありません**。命令ベースを低位 0x800000 に移しても、
  dispatch された EU スレッドは同一署名でハングします。第11報の smoking gun（全 VA が 4GB 超）は、
  少なくとも命令フェッチの直接原因ではありませんでした。
- これまでの全ハング（C1 store / C2 store-less / C3-low / PS）が **completely 同一署名**（row_instdone=
  0x8610e87f, IPEHR=post-thread-completion-flush）。C0(walker/primitive なし)だけ完走。
- → **EU スレッドが dispatch された瞬間、命令ベース VA・kernel 内容・pipeline(3D/GPGPU) に依らず
  無条件に同一ハングする**。windower / PS 固定機能 / A64 store は無関係と確定。

## 新たな主容疑: EU の send メッセージが完了しない
無条件 compute store(C1)が未着地、compute EOT(C2, send.ts)が未完了、PS の RT-write/EOT(sendc.render)が
未完了 —— これらは **「EU が send を発行しても、その shared function（Data Cache / Thread Spawner /
Render Cache）からの完了が返らない」** で一貫して説明できます。fixed-function（VFE/windower）は
スレッド完了を永遠に待ち、post-thread flush でハング。EU は not-done。

## 伺いたいこと
1. **Gen12 で、EU の send メッセージ（shared function への message）が一切完了しない場合に疑うべき
   共通要因**をご教示ください。message gateway、shared function の enable、あるいは kernel_context の
   LRC に EU スレッド実行/メッセージ完了に必要な設定（例: 特定の chicken bit、URB/SLM、TS 設定）の
   欠落など。C0 で MI レベルの PPGTT 書き込み・post-sync・MIDL は成立していますが、EU send は別経路です。
2. ご提案の**外部対照（同一 GPU 8086:46a8 を同一 VFIO 条件で Linux ゲストにパススルーし、簡単な
   compute/EU workload を走らせて成功するか）**の最小手順をご教示ください。Linux で成功すれば物理故障・
   パススルー一般ではなく、自作側の init/context/memory に原因を絞れます。ホストは既に vfio-pci 常時
   バインド済みで、Linux ゲストのパススルー起動は可能です。どの workload（例: 単純な OpenCL/Level-Zero、
   あるいは i915 の既知テスト）が最小の陽性対照になりますか。
3. C3-low の低位マッピング有効性を、C3-low での低位 VA からの MI_COPY 読み戻しで確定すべきですか
   （fault=0 と同一署名から VA 無関係が最尤ですが、念のため）。

（C0 完全成功で submission/PPGTT/state/MI/post-sync は健全。EU 電源/fuse/MCR も健全。障害は
 「dispatch された EU スレッドの命令実行 or send 完了」に完全に局在。refcs/refcs_empty/gentool 構築済み。
 実機 run 再起動不要。default ビルド warning 0。）
