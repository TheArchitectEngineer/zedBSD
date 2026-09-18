# WS031 回帰基準

**位置付け（2026-09-18 専門家の計画更新）**: E-104 は「オフスクリーン描画の基準が完成した」段階で、ここから表示・アプリ接続へ進む。基準は段階ごとに追加する: **オフスクリーン基準（本書 §1〜§2、E-99〜E-105）** → LCD 基準（表示 A〜D、未着手）→ Vulkan アプリ基準（未着手）。大きなリファクタ（著作権整理・`osdep_` 改名・`parity/` 整理）は最後で、そのときの比較元がこの一連の基準になる。出典・元表示の保持だけは今から進める。

全体の順序: 残り 4 モードの回帰 → bilinear → LCD 参照確定・native 事前確認 → 通常稼働の寿命管理＋ディスプレイ／LCD＋hotswap の入口 → libvulkan 接続・Vulkan オフスクリーン → Vulkan アプリから LCD 表示 → native 総合受入 → 著作権整理・改名・配置整理 → 整理後の回帰。

受入済み回帰モードの再実行は、その都度の確認待ちにせず進めてよい（専門家 2026-09-18）。ただし、最初の異常で追加提出を止める、ハング後は reset・回収前に次を投入しない、HAL の契約変更や新しいハードウェア操作は別扱い、は維持する。

固定日: 2026-09-18（台帳 E-99〜E-104）。差分の基準: `plan/ws031/handover/increment-results/e97-e105-changes.patch`（base commit 2bf790a4）。

機能追加中も最後のリファクタでも、合格の基本は次の三つで、「ビルドが通った」では足りない。
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
| bilinear | `-DPARITY_BL_TEST=1` | `BL PASS: … steps=4/4 passed=4`、各 step `max_channel_diff=0` | nearest→linear→nearest、新規 context で linear。期待値は厳密（画像 3） | E-105 |

**E-105 で確定**: commit f4dba354（E-104 と同じソース、作業ツリー差分なし）から C1 単独＋反復、単色 PS、R1、テクスチャ初回を clean build し、各 1 起動で実機 PASS。T3 は E-104 で同じソースにて PASS 済み。提出 bytes は C1＝`e99-c1-*`、単色 draw＝`e101-draw-*`、texture＝`e103-tex-*`（RT まで）と host の `cmp` で byte 一致。bilinear はその上に fixture／harness を足した増分（E-105）で、既存 5 モードの提出 bytes は不変（PIN-C1／PIN-DRAW／TEX-BATCH／TEX-AB が GPU なしで保証）。

全モード共通: `attach end: … outcome=STOPPED where=i915_driver_probe complete err=0`、`ktest … N checks, 0 failures`（GPU なし・実機とも。E-104 のソースで 383、E-105 で 385）、`runner-result: … probe=COMPLETE … cleanup=1`、wedged=0、reset なし。

### 表示系の試験モード（E-108 追加、GPU 投入なし）

| flag | 内容 | 合格行 |
|---|---|---|
| `-DPARITY_AUX_TEST=1`（明示 VBT も要求する） | eDP の PPS 初期化 → VDD → 実 AUX で DPCD／EDID → late init → 停止（VDD off、参照返却） | `AUX-TEST verdict: PASS (acquire=0 late=0 end=0 dpcd_match=1 edp_dpcd_match=1 edid_match=1 wells_balanced=1)` と `runner-result … probe=COMPLETE … cleanup=1` |

**E-108 の現ソース回帰（2026-09-19）**: 6 モード（EU／DRAW／R1／TEX／T3／BL）を同一ソースで 6/6 PASS（`handover/increment-results/e108-run-parity-hw-*.log`、道具 `sweep` は台帳 E-108 §5）。

GPU-free ktest は 407 checks（E-107 で +8 = VBT、E-108 で +14 = eDP）。host 試験: `plan/ws031/tests/vbt-host-test.c`（19/0）、`sh plan/ws031/tests/run-dp-host-test.sh`（63/0）、`sh plan/ws031/tests/run-vk-host-tests.sh`（9 fixture）。

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
| 画像 3 の期待 RT: nearest／bilinear | — | e7f55b015cd6c325／c1e2712dfe1a4125 | `e105-bl-last-manifest.txt`（bilinear） | `BL-EXPECT`、host `verify-t3` |
| state 頁 binding A: nearest／bilinear sampler | 1024 | a3483d031b86a404／52014046cb0bdf51 | 同上 | `BL-STATE`（差は SAMPLER_STATE の 2 dword） |

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
