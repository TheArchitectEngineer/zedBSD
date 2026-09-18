# Gen12 PS ハング 第14報 — indirect-context 経路を実装・動作。Wa_18022495364 単独では未解消 → Linux 対照へ

ご指示の詳細仕様どおり、欠落していた indirect-context restore 経路を実装し、Wa_18022495364 を検証しました。

## 実装（ご仕様どおり）
- GGTT 上に 64byte(16DW) indirect batch:
  `MI_STORE_DWORD_IMM|GGTT(enter marker) → NOOP → [LRI: 0x11000001, 0x000020d8, 0x00400040] → MI_STORE_DWORD_IMM|GGTT(leave marker) → padding`。BB_END なし。
- kernel_context LRC: `state[0x13]=0(BB_PER_CTX)`, `state[0x15]=wa_ggtt|1(RING_INDIRECT_CTX)`,
  `state[0x17]=0x340(RING_INDIRECT_CTX_OFFSET=0xD<<6)`。offsets table に 0x1c0/0x1c4/0x1c8 スロットが
  既にあり、値を書くだけ。wbinvd で image を memory へ。提出ごと force-restore（既存 descriptor bit2）。

## 結果
```
wa_ggtt=0x00132000  regs[0x14]=0x21c4 [0x16]=0x21c8  ctx_ctrl=0xffff0008 desc_low=0x00101119
B(WA) C0     enter=0xe07e0000 leave=0x1ea7e000  ready=✓ done=✓  completed=3 seqno=3   （完走）
B(WA) C3-low enter=0xe07e0000 leave=0x1ea7e000  ready=✓ eu=dead done=dead  completed=3 seqno=4
  HANG: ipehr=0x70040000  row=0x8610e87f （PS/C1/C2 と同一）
```

## 判定（ご提示の表に厳密に沿う）
- **enter/leave marker が両方着地** → **indirect-context restore 経路が実際に実行された**。
  Wa LRI は enter と leave の間で実行済み（fault なら leave 未着地のはず）。欠落していた機構の
  実装に成功し、実機で restore 経路が動くことを確認しました。
- **WA を適用しても C3-low は同一署名でハング** → ご判定「前後 marker が両方出るが WA ありでも
  C2 停止 → **この WA 単独では解消しない → Linux 外部対照へ進む**」。
- E-19 の「この WA 欠落が単独原因」という仮説は否定側。ただし context-restore 初期化全体を正常とは
  判定しません（他の restore 処理は未移植）。ご指示どおり、別の bit を次々足さず Linux 対照へ移ります。

## 次段: Linux 外部対照（実現可能を確認）
テストホスト（Debian 13, 8086:46a8 が vfio-pci 常時バインド）で、内蔵ネット・KVM・vfio を確認しました。
いただいた eu_positive_control.py を使い、Ubuntu ゲストを **enable_guc=0** で起動、同一 8086:46a8 を
VFIO パススルーして 1 work-item の OpenCL store を走らせます。判定:
- Intel GPU で tag 一致・正常終了・reset なし → 自作側の init/context/memory 差分に絞る。
- GPU 未列挙 / i915 未バインド / build 失敗 → 準備段階（陰性結果としない）。
- 投入後 hang/reset → 双方ログと引き渡し/reset 履歴を調べる。

構築を開始します（Ubuntu cloud image + cloud-init で OpenCL 導入 → vfio パススルー起動）。結果が出たら
linux-eu.log と前後カーネルログを添えて報告します。

（今回の副次成果: 欠落していた indirect-context restore 経路を実装・動作確認。今後の restore-time WA 追加に
 再利用可能。default ビルド warning 0。）
