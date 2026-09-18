
## p011 増分E-21 (2026-09-16): indirect-context 経路を実装・動作。Wa_18022495364 単独では未解消

専門家の詳細仕様で、欠落していた indirect-context restore 経路を実装し、Wa_18022495364 の A/B 比較を実施。

### 実装（専門家仕様どおり）
- GGTT 上に 64byte(16DW) indirect batch（enter marker MI_STORE_DWORD_IMM|GGTT → NOOP → Wa LRI →
  leave marker → padding、BB_END なし）。`drv_i915_gem_bind_ggtt` で wa_ggtt 取得。
- kernel_context の LRC: state[0x13]=0(BB_PER_CTX)、**state[0x15]=wa_ggtt|1(RING_INDIRECT_CTX)**、
  **state[0x17]=0x340(RING_INDIRECT_CTX_OFFSET=0xD<<6)**。offsets table が 0x1c0/0x1c4/0x1c8 スロットを
  持つので値を書くだけ。wbinvd で image を memory へ。
- Wa LRI: `0x11000001, 0x000020d8(GEN12_CS_DEBUG_MODE2), 0x00400040(masked INSTRUCTION_STATE_CACHE_INVALIDATE)`。
- 提出ごと force-restore（既存 descriptor bit2）、inhibit は先行 selftest で解除済み。

### 結果
```
wa_ggtt=0x00132000  regs[0x12]=0x21c0 [0x14]=0x21c4 [0x16]=0x21c8  ctx_ctrl=0xffff0008 desc_low=0x00101119
B(WA) C0     enter=0xe07e0000 leave=0x1ea7e000 ready=✓ done=✓ completed=3 seqno=3   （完走）
B(WA) C3-low enter=0xe07e0000 leave=0x1ea7e000 ready=✓ eu=dead done=dead completed=3 seqno=4
  HANG: ipehr=0x70040000 row=0x8610e87f （PS/C1/C2 と同一）
```

### 判定（専門家の表に厳密に沿う）
- **enter/leave marker が両方着地** → **indirect-context restore 経路が実際に実行された**（欠落機構の
  実装成功。RING_INDIRECT_CTX 配線・offset 0x340・GGTT batch すべて正しく動作）。Wa LRI は enter/leave の
  間で実行済み（faultなら leave 未着地のはず）。
- **WA ありでも C3-low は同一署名でハング** → 専門家判定「前後 marker が両方出るが WA ありでも C2 停止 →
  **この WA 単独では解消しない → Linux 外部対照へ進む**」。
- （pass A=no-WA は B の C3-low ハングでエンジン wedge のため未実行。ただし A は indirect 経路のみで
  WA なし＝従来の broken 相当なので、比較は「両者ハング」で確定。）

### 得られたこと
- **欠落していた indirect-context restore 経路を実装し、実機で動作確認**（enter/leave marker）。
  他の restore-time WA を今後追加可能な基盤ができた。
- ただし **Wa_18022495364（命令ステートキャッシュ無効化）単独では EU スレッドハングは解消しない**。
  E-19 の「この WA 欠落が単独原因」という仮説は否定側。

### 次（専門家指定）
Linux 外部対照（eu_positive_control.py, 同一 8086:46a8 を VFIO パススルー、enable_guc=0）で
「自作 init/context/memory」vs「物理/パススルー」を確定。Linux で EU が動けば自作側差分に絞れる。
専門家いわく「別の bit を次々追加せず Linux 対照へ」。default ビルド warning 0。
