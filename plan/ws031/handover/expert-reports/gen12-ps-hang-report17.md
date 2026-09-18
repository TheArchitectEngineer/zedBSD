# Gen12 PS ハング 第17報 — ★★linux-c2-replay 全 PASS。C2/C1 state は正しい、問題は zedBSD 基盤に確定

ご指示の linux-c2-replay を実装し、L-MI → L-C0 → L-C2 → L-C1 まで**すべて完走**しました。
ご判定表の「L-C1 も成功 → EU 書込までの強い対照成立 → Linux と zedBSD の基盤差分を主対象に」に到達しました。

## 実装（ご仕様どおり、直接 ioctl）
- Linux に任せる: GT/engine init, LRC 作成保存復元, PPGTT, execlists 提出, 完了管理。
- 持ち込む: zedBSD の C2/C1 の**全命令バイト・IDD・SBA・VFE・walker・marker**。
- DRM renderD128 → GEM context（**I915_CONTEXT_PARAM_ENGINES に {RENDER,0} のみ, exec selector=0**）→
  GEM object → **softpin（EXEC_OBJECT_PINNED + SUPPORTS_48B_ADDRESS + WRITE、zedBSD と同一 GPU VA
  shared@0x100400000, batch@0x100600000）** → EXECBUFFER2 → GEM_WAIT(3s) → marker 確認。
- 移植で変更した DWORD: **なし**（VA を zedBSD と同一に softpin したのでバッチバイトは同一）。max_threads=559 も同一。
- 一点だけ実装差: 現代 i915 は **GEM_PWRITE 非対応**（Operation not supported）→ GEM_MMAP_OFFSET(WB)+mmap で
  コード/state 書込・marker 読出。C2/C1 のコード・state 自体は不変。

## 結果（Linux i915, enable_guc=0=execlists, 同一 VFIO パススルー）
```
L-MI: waited=1 ready=0xc0ffee10 done=0xc0ffee20                      （MI/softpin/PPGTT 動作）
L-C0: waited=1 ready=✓ done=✓ cs=✓                                   （compute 初期化列 完走）
L-C2: waited=1 ready=✓ done=✓ cs=✓  eu=dead（store-less で正常）      （walker 完走・ハングなし）
L-C1: waited=1 ready=✓ done=✓ cs=✓  eu=0xc0ffee02                    （★EU が A64 store を実行・書込）
GPU hang/reset/wedged: なし
```

## 結論（決定的）
**zedBSD でハングする同一の C2/C1 バッチ+kernel+IDD+VFE+walker+state が、Linux i915 では完走し、
L-C1 では EU スレッドが実際に実行してメモリに 0xc0ffee02 を書き込みました。**
→ **C2/C1 のコード・投入 state・walker/VFE/IDD 設定はすべて正しい。問題は zedBSD の
GT/engine/context/LRC 初期化に決定的に確定**しました。以下がこれで確定した切り分けです。

| 層 | 判定 |
|---|---|
| C2/C1 のバッチ・kernel・IDD・VFE・walker・state | **正しい**（Linux で完走・EU 書込） |
| 物理 GPU・VFIO・GPU 能力 | 健全（Linux で EU 動作） |
| zedBSD の submission/PPGTT/SBA/MI/post-sync | 健全（C0 成功） |
| **zedBSD の GT/engine/context/LRC 初期化** | **← 原因はここに局在** |

## 次段の照会
ご提示の 3 監査範囲（A: __engines_record_defaults/intel_engine_emit_ctx_wa、B: intel_gt_init/init_hw+
forcewake、C: execlists_resume/submission_setup）で zedBSD の欠落を特定し、**replay 結果が確定したので
zedBSD 側の改修に進みます**。伺いたい点:

1. **最優先の A（golden context の ctx WA）から着手する理解で良いでしょうか。** zedBSD は
   __engines_record_defaults 相当（restore-inhibit context で ctx WA を LRI 適用 → 別 context へ切替えて
   default_state を保存 → 継承）を持たず、ctx WA を per-request の ring LRI で適用しています。
   この「golden 保存機構の欠如」が、EU スレッド実行を妨げる典型として最有力でしょうか。
2. あるいは、動く Linux 環境を活かし、**c2replay を計測に使って zedBSD の欠落を絞る**方法（例: Linux で
   context を意図的に劣化させて zedBSD の状態に近づけ、どの init を外すと L-C2 が止まるかを二分探索する）は
   有効でしょうか。もし有効なら、まず外すべき init（GT WA / ctx WA / forcewake / MOCS / RC6 等）の
   優先順位をご教示ください。
3. B/C（GT init / execlists resume）で、EU 実行に直結する典型（forcewake ドメインの保持、GT WA の適用時点、
   execlists resume 時の MOCS 再初期化）のうち、zedBSD の C0 成功と両立しつつ EU だけ止める要因として
   優先すべきものはありますか。

（インフラ: Linux dev VM 一式（SSH:2222, c2replay, libdrm-dev）を対照環境として維持。zedBSD 実装・
 refcs/gentool 維持。GPU は vfio-pci。default ビルド warning 0。）
