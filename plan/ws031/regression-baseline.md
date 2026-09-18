# WS031 回帰基準（リファクタ前の「動作する変更前」）

固定日: 2026-09-18（台帳 E-99〜E-104）。差分の基準: `plan/ws031/handover/increment-results/e97-e104-changes.patch`（base commit 2bf790a4）。

リファクタ中の合格基準は次の三つで、「ビルドが通った」では足りない。
1. **固定した入力に対する GPU command／state が同じ**（下表の hash。VA や seqno など変動する項目があるときは、その項目だけ明示して比較する。opcode、長さ、mask、同期命令は正規化して消さない）。
2. **出力が同じ**（C1 marker、単色 RT、texture の全画素、更新後画像、request 完了）。
3. **寿命が同じ意味で成立**（GPU 参照中の object を解放しない、worker 終了前に状態を壊さない、teardown が実際の所有参照を処理する）。

期待値の変更を伴わない構造整理で試験が落ちたら、まずリファクタ側を調べる。試験の期待値を新しい出力へ追従させて通さない。

## 1. 試験モード（1 起動 1 モード、フラグは排他）

ビルド: `make -j$(nproc) BUILD=build/<name> CONFIG_DRIVER_PCI_I915_PARITY=y ZEDBSD_TEST_CPPFLAGS=<flag> disk-image`（clean な BUILD dir）。実行条件は引き継ぎ README §4（q35/KVM、`-cpu host,host-phys-bits-limit=39`、4 GiB、4 vCPU、vfio-pci）。GPU なしで ktest 完走（`checks, 0 failures`）を確認してから同一 image を実機へ渡す。

| モード | flag | 合格行 | 内容 | 実機 PASS の台帳 |
|---|---|---|---|---|
| 既定（試験なし） | なし | `attach end: … i915_driver_probe complete`、`runner-result: … probe=COMPLETE cleanup=1` | P0〜P7 と teardown | E-96 |
| C1 単独＋反復 | `-DPARITY_EU_TEST=1` | `EU-TEST PASS`、`EU-REPEAT PASS: … rounds=5 passed=5` | compute 陽性対照、同一／新規 context | E-99、E-100 |
| 単色 PS 描画 | `-DPARITY_DRAW_TEST=1` | `DRAW-TEST PASS: … pixels match=1024/1024` | RECTLIST＋const-colour PS | E-101 |
| R1 | `-DPARITY_R1_TEST=1` | `R1 PASS: … steps=12/12 passed=12` | draw 反復、compute↔3D 切替 | E-102 |
| テクスチャ初回 | `-DPARITY_TEX_TEST=1` | `TEX-TEST PASS: … pixels match=1024/1024 … changed_bytes=0 guard_bad_bytes=0` | 8×8 texture、nearest | E-103 |
| T3 | `-DPARITY_T3_TEST=1` | `T3 PASS: … steps=9/9 passed=9` | 内容更新、binding 切替、context 再利用 | E-104 |

注: 最終ツリー（E-104 時点）で実機実行したのは T3 だけ。EU 単独・単色 draw 単独・R1・テクスチャ初回の実機 PASS は各増分時点のツリーでのもので、その後の変更（request builder の batch VA 引数、draw builder の texture 引数、object 台帳 128 枠）を含む最終ツリーでは GPU なしの確認まで（PIN-C1／PIN-DRAW で提出 bytes の同一性は保証、同じ request 経路は R1／T3 が実機で通過）。**リファクタ着手前に、最終ツリーで全モードを実機で 1 巡して基準を確定するのが望ましい（要解除）。**

全モード共通: `attach end: … outcome=STOPPED where=i915_driver_probe complete err=0`、`ktest … 383 checks, 0 failures`（GPU なし・実機とも）、`runner-result: … probe=COMPLETE … cleanup=1`、wedged=0、reset なし。

## 2. pin した入力（FNV-1a 64、提出 object から計算）

| 入力 | dwords | FNV-1a 64 | SHA-256（artifact） | GPU-free の pin |
|---|---|---|---|---|
| C1 batch（max_threads 559） | 322 | 5dfb47d3c10b0560 | `e99-c1-manifest.txt` | `PIN-C1` |
| C1 shared 頁（IDD〜kernel） | — | 444e3a7a4e9c1abd | 同上 | — |
| 単色 draw batch（MOCS 6） | 353 | 241f478201bb3a81 | `e101-draw-manifest.txt` | `PIN-DRAW` |
| 単色 draw state 頁（提出時） | 1024 | 26a08d52909ca9a4 | 同上 | — |
| textured batch | 355 | b6d8a3b470c3e1de | `e103-tex-manifest.txt`（d853b63d…） | `TEX-BATCH`（単色比「2 dword 追加＋3 dword 変更」） |
| textured state 頁（E-103、提出時） | 1024 | c9d58cc3cc87e581 | 同上 | `TEX-STATE` |
| T3 state 頁 binding A／B | 1024 | a3483d031b86a404／160d7483ee5e7cc4 | `e104-t3-last-manifest.txt`（B） | `TEX-AB`（差は 1 dword） |
| sampling PS | 640 bytes | 20ff9c926f6324c1 | 2500bd58…（`e103-texfix-ps.bin`） | `TEX-STATE` 内 |
| テスト画像 0／1／2（256 bytes） | — | cb665371e404a925／3a9b188c339de1a5／e734e6033a2f4925 | — | `TEX-EXPECT`、`TEX-VARIANTS` |
| 期待 RT 画像 0／1／2（4096 bytes） | — | d41b57080b44d325／41960fd9cbbacb25／9c358a31cb6ae325 | — | host `verify-t3` |

## 3. 道具

- `handover/tools/eu_artifact.py`: `extract`（C1）、`extract-draw`、`extract-tex`、`extract-t3-last`、`verify-t3`、`diff`。ログから提出 bytes を抽出し、host 側で独立に計算した期待値と照合する。
- `handover/tools/reftex.c`: texture fixture の generator（固定 Mesa @ab691a1c）。再生成して `tex_fixture_gen.inc` と byte 一致することもリファクタの確認項目にできる。
- `handover/tools/build_verify.sh <name> <flag>`（clean build＋GPU なし起動）、`handover/tools/hw_run.sh <name> <log> <pattern>`（GPU なしが完走した image だけ実機で 1 回）。
- `plan/ws031/provenance-ledger.md`（出典台帳）、`plan/ws031/license-inventory.md`（ファイル別の header 事実の棚卸し、`tools/license_inventory.py` で再生成）。

## 4. 今の構造でリファクタ時に壊しやすい所（メモ）

- 試験 harness 6 本が `parity/eu_test.c` に同居し、`probe.c` に試験ごとのログ出力ブロックがある（重複多数）。分離するときは合格行の文言を変えない。
- draw／texture の fixture は legacy 側の `selftest.c` にあり（同じ builder を使うため）、parity 構成でも `selftest.c` をビルドしている。
- CPU アドレス／DMA アドレス／GPU VA の区別、request 完了 → park → CPU 書換えの順序、`eu_hang_dump_reset` 後は提出しない規則。
- forcewake 全保持、polling による完了待ち、runtime suspend 無効は「挙動」なので、リファクタとは別の受入で変える。
