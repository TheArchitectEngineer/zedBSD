
## p011 増分E-25 (2026-09-16): ★★linux-c2-replay 全 PASS — C2/C1 の state は正しい、問題は zedBSD 基盤に確定

専門家の主作業 linux-c2-replay を実装・実行。zedBSD の compute バッチ+kernel+IDD+state を、Linux i915 の
直接 ioctl（GEM_MMAP_OFFSET+mmap, softpin EXEC_OBJECT_PINNED, EXECBUFFER2 に I915_CONTEXT_PARAM_ENGINES
{RENDER,0}, GEM_WAIT）で、**zedBSD と同一 GPU VA（shared@0x100400000, batch@0x100600000）に softpin して
同一バイトで実行**。

### 結果（Linux i915, enable_guc=0=execlists, 同一 VFIO）
```
L-MI: waited=1 ready=0xc0ffee10 done=0xc0ffee20                          （MI/softpin/PPGTT 動作）
L-C0: waited=1 ready=✓ done=✓ cs=✓                                       （compute 初期化列 完走）
L-C2: waited=1 ready=✓ done=✓ cs=✓  eu=dead(store-less で正常)            （walker 完走・ハングなし）
L-C1: waited=1 ready=✓ done=✓ cs=✓  eu=0xc0ffee02                        （★EU が A64 store を実行・書込）
GPU hang/reset/wedged: なし
```

### 結論（決定的）
- **zedBSD でハングする同一の C2（store-less）/C1（A64 store）バッチ+kernel+IDD+VFE+walker+state が、
  Linux i915 では完走**。L-C1 では **EU スレッドが実際に実行しメモリに 0xc0ffee02 を書き込んだ**。
- 実装のポイント: 現代 i915 は GEM_PWRITE 非対応 → GEM_MMAP_OFFSET(WB)+mmap でアクセス。それ以外は
  zedBSD の emit をそのまま移植（PIPELINE_SELECT/SBA/VFE/MEDIA_STATE_FLUSH/MIDL/GPGPU_WALKER/post-sync）。
  VA は zedBSD と同一に softpin したので**バッチバイトは同一**（移植による DWORD 変更なし。max_threads=559 も同一）。
- → 専門家判定「L-C1 も成功 → EU 書込までの強い対照成立 → Linux と zedBSD の基盤差分を主対象に」。
  **C2/C1 のコード・投入 state・walker/VFE/IDD 設定はすべて正しい（Linux で EU が動く）。問題は zedBSD の
  GT/engine/context/LRC 初期化に決定的に確定**。windower/sample-mask/store/VA/batch content はすべて除外。

### これで確定した切り分け
| 層 | 判定 |
|---|---|
| C2/C1 のバッチ・kernel・IDD・VFE・walker・state | **正しい**（Linux で完走・EU 書込） |
| 物理 GPU・VFIO パススルー・GPU 能力 | 健全（Linux で EU 動作、E-23/E-25） |
| zedBSD の submission/PPGTT/SBA/MI/post-sync | 健全（C0 成功） |
| **zedBSD の GT/engine/context/LRC 初期化** | **← 原因はここ** |

### 次段（専門家の 3 監査範囲、zedBSD 側改修は replay 確定後＝今）
A. __engines_record_defaults / intel_engine_emit_ctx_wa（golden context に ctx WA を焼く機構）— 最優先。
   zedBSD は golden default_state 記録機構を持たない（E-24）。
B. intel_gt_init / intel_gt_init_hw（forcewake, GT WA, PPGTT, MOCS）。
C. intel_execlists_submission_setup / execlists_resume（MOCS 再初期化等）。
Linux が execlists 経路で EU workload 実行前に必ず通す処理のうち、zedBSD に欠落しているものを特定。

インフラ: Linux dev VM（/home/awe/linuxvm, boot-dev.sh, SSH:2222, libdrm-dev, c2replay）— 今後 zedBSD state を
Linux で比較検証できる強力な対照環境。GPU は vfio-pci 維持。default ビルド warning 0。
