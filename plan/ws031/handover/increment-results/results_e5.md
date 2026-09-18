
## p011 増分E-5 (2026-09-15): STATE_BASE_ADDRESS ハングの根本原因 — 3D state はリングではなく PPGTT batch から

### 症状
実 heap を STATE_BASE_ADDRESS に結線すると、base の値次第で以降のメモリ書き込み（PPGTT の
MI_STORE、GGTT の MI_STORE、breadcrumb の PIPE_CONTROL post-sync）が全て消え、context complete
も来ない。CS は head==tail/MODE_IDLE まで走り切り、RING_FAULT/EIR/ESR は 0、ホスト側 DMAR fault も無し。

### 切り分け（全て実機、1 ブート 1 失敗）
| 実験 | 結果 |
|---|---|
| base=0x100400000 / 0x100600000（rt/surface obj） | 通る |
| base=0x100800000 / 0x100a00000（dynamic/instruction obj） | 落ちる |
| 同じ VA に plain MI_STORE | 通る（PPGTT マッピングは正常、CPU 側 PTE も正常） |
| object と slot の入れ替え、bind 順逆転（VA と paddr の分離） | **VA 値だけが効く**（paddr・slot・回数は無関係） |
| base を obj 無しで掃引: 4G+7M ✓ / 4G+16M ✓ / 8G ✓ / **4G+8M ✗ / 4G+10M ✗** | 閾値ではなく特定範囲 |
| clflush / wbinvd | 効果なし（キャッシュ・コヒーレンシではない） |
| GGTT 全エントリ読み戻し | 不一致 0（GTT ウィンドウのマッピングは正常） |
| CSB レジスタミラー | 失敗 request は promote のみで complete 無し |

### 根本原因
selftest が SBA を `request->extra[]`（=**リング直置き**）で流していた。リング／secure batch から
実行される 3D state 命令のステートフェッチは **GGTT アドレス空間**で行われる（Linux i915 の
golden renderstate が GGTT に pin した batch を SECURE dispatch し、SBA base を GGTT オフセットに
reloc しているのがその実証）。MI_STORE は自前の GGTT/PPGTT ビットを持つため PPGTT に書けていた
のが「CS の書き込みは通るのに state fetch は死ぬ」の正体。PPGTT VA を GGTT として解釈した結果、
4GB 超の未定義アドレスで 3D パイプライン（state ユニット）が固まり、後続の pipelined 書き込みと
breadcrumb が滞留した（CS パーサ自体は tail まで進むので "idle" に見える）。
なぜ 0x800000〜0xa00000 だけ固まり 0x700000/0x1000000 は無事かは未解明（4GB 超の GGTT
アドレスの扱いはハード定義外）。**リングに 3D state を置かない**ことが正解であり、実 executor は
最初から PPGTT batch（cmdbuf の GEM batch）なので設計変更は不要。

### 修正
- `drv_i915_draw_selftest`: 3D 命令を kernel_vm PPGTT の batch（MI_BATCH_BUFFER_START non-secure）
  から実行。surface/dynamic/instruction の全 base 同時設定まで通過:

      i915: sba none       markerA=0xa5a50001 markerB=0xd7a3f00d hwsp=3 completed=3 seqno=3
      i915: sba surf<-surf markerA=0xa5a50001 markerB=0xd7a3f00d hwsp=4 completed=4 seqno=4
      i915: sba dyn<-dyn   markerA=0xa5a50001 markerB=0xd7a3f00d hwsp=5 completed=5 seqno=5
      i915: sba all        markerA=0xa5a50001 markerB=0xd7a3f00d hwsp=6 completed=6 seqno=6
      i915: draw step1 passed (real-heap SBA parses)

- PIPELINE_SELECT: Gen12 は mask bits (15:8) を立てないと選択が無視される。selftest と executor
  （pipe.c）の両方が mask 無し（no-op）だったので `GEN12_PIPELINE_SELECT_DWORD()`（mask 0x13 +
  media sampler DOP gate、Mesa と同形）に統一。
- 副次確認: 非特権 batch 内の `MI_STORE | MI_USE_GGTT` は着地しない（GGTT 書き込みはリング専用）。

### 教訓
- 「CS は走り切ったのに書き込みが消える」= パイプライン側のストール。CS レジスタ（IPEHR/ACTHD/
  MODE_IDLE）と CSB（promote のみ）を読めば区別できる。
- 3D state のデバッグはリングでやらない。selftest も executor と同じ batch 経路を使う。
