# WS031 E-102／E-103 報告: R1（描画反復・compute↔3D 切替）PASS、テクスチャ付きオフスクリーン描画 成功

日付: 2026-09-18 / 対象: zedBSD parity（Linux 6.8.12 正本、execlists）/ 実機: ADL-P 8086:46a8（VFIO）

## 結論

| 段階 | 結果 |
|---|---|
| R1：PS 描画の反復、同一／別 context、compute↔3D 切替 | **12/12 PASS**（1 起動） |
| T1：texture fixture（GPU なし） | 生成・検査完了、GPU なし 381 checks / 0 failures |
| T2：最初のテクスチャ描画（独立起動） | **PASS**：sampler を使う PS が実行され、1024 画素すべてが期待画像と一致、request 完了、guard 無傷、reset なし |

T2 は 1 回目が不合格で、原因は私の generator の誤りでした（GPU 側の問題ではありません）。原因を特定して直した 2 回目で合格しています。§4 に経緯を書きます。

現行の execlists、参照起動条件、10 ms タイマー、HAL 非変更は維持しています。git commit / push はしていません。

## 1. R1 の結果（E-102）

1 回の P0〜P7 の後、同じ request 基盤で 12 提出を順に実行しました。単独 EU 試験と単独 draw 試験のフラグは保存し、R1 は別モードです。混在用の MMIO 修復や追加の初期化 batch はありません。

| step | context | 種類 | seqno | 判定 |
|---|---|---|---|---|
| 1〜4 | A（新規） | draw ×4 | 2、4、6、8 | 各回 CS marker 3 種、PS marker、1024/1024 画素、stale=0 |
| 5〜6 | B（新規） | draw ×2 | 2、4 | 同上 |
| 7〜9 | C（新規） | draw → C1 → draw | 2、4、6 | draw は同上、C1 は marker 4 種と読み戻し一致 |
| 10〜12 | A → B → A | C1 → draw → C1 | 10、6、12 | すべて一致 |

- 全 step で HWSP の生値が seqno と一致し、park まで完了しました。timeout、wedged、reset はありません。
- 各 draw の前に RT を 0x5a5a5a5a で初期化し、state 頁を書き直して marker も初期値へ戻しています。前回の画像が残っていただけの成功ではありません。
- CPU の書換えは、前 request の完了と park を確認した後だけです。
- draw の state 頁と C1 の shared 頁は同じ VA です（どちらの kernel も marker を絶対アドレスへ書くため）。そのため PS kernel と CS kernel が同じ VA で入れ替わります。この条件で compute→3D、3D→compute の両方向が、同一 context 内と別 context 間で通りました。
- 提出 batch は E-99 と E-101 の実機 PASS bytes と同一 hash です。draw の state 頁も E-101 と同一 hash です。

**1 回目の起動について**：step 1〜4 が PASS した後、context B の生成で試験側の object 台帳（64 枠）が尽きて停止しました。提出前の停止でハングではなく、teardown も正常でした。枠を 128 に広げ、別 context 切替は A／B を再利用する形にして再実行したのが上の結果です。

## 2. T1 の実装

### 2.1 仕様

形状は既存 RECTLIST、RT は既存の 32×32 B8G8R8A8_UNORM。入力 texture は 8×8、2D、R8G8B8A8_UNORM、1 mip、linear、圧縮なし。sampling は nearest、明示 LOD 0、clamp-to-edge、sRGB 変換と blend なし。提出経路は現行 parity（新規 context、default_state 継承、既存 request 形）です。

### 2.2 生成元

1 本の generator（`tools/reftex.c`、固定 Mesa tree @ab691a1c 内でビルドする NIR builder プログラム）から、次をまとめて出力します。値は単色 PS から固定コピーしていません。

| 出力 | 生成手段 |
|---|---|
| PS bytes と prog_data | `brw_compile_fs`（device 0x46a8） |
| texture layout、RENDER_SURFACE_STATE | `isl_surf_init`（linear 要求が通り pitch 32／size 256／alignment 1）、`isl_surf_fill_state` |
| SAMPLER_STATE | genxml gen120 の packer（nearest／clamp／LOD 0／正規化座標、LOD pre-clamp は anv・iris と同じ） |
| 単色 draw から変わる packet 語 | genxml gen120 の packer |

PS の内容：`uv = (floor(gl_FragCoord.xy) + 0.5) / 32`、`colour = txl(texture BTI 1, sampler 0, uv, lod 0)`、`RT0(BTI 0) = colour`。単色 PS と同じ A64 entry marker を保持しています。

compiler の出力（生成 metadata）：

```
PS size=640  SIMD8 @0 / SIMD16 @320  grf_start8=4 grf_start16=6  num_varying=0
uses_src_depth=1 uses_src_w=1 uses_pos_offset=0 uses_kill=0  total_scratch=0 push=0 barycentric=0
```

逆アセンブルで確認した命令：subspan 座標から画素 X/Y を導出する `add … r1.4`、`send.smpl … sample_lz … bti(1) using sampler index 0`、`sendc.render … rt_write last_rt bti(0)`。

### 2.3 surface／binding／sampler の差分（単色 draw 比）

- batch は「単色 draw batch ＋ `3DSTATE_SAMPLER_STATE_POINTERS_PS(896)` の 2 dword」で、変更 dword は 3 個だけです。

| dword | 値 | 意味 |
|---|---|---|
| 3DSTATE_PS DW3 | 0x08080000 | sampler count 1、binding table 2（BLORP と同じ） |
| 3DSTATE_PS DW7 | 0x00040000 | GRF start 4（prog_data） |
| 3DSTATE_PS_EXTRA DW1 | 0x81800004 | valid、UAV、source depth、source W（prog_data） |

- state 頁：BT[1]=128 → texture の RSS（isl 生成、address を VA で patch）、SAMPLER_STATE @+896、sampling PS @+1024（640 B）。既存 4 KiB 頁に収まることを、サイズ・alignment・重なりの静的検査で確認しています。新しい object は texture 1 頁（VA 0x100404000）です。
- builder は引数 NULL のとき単色 draw と byte 同一です（GPU なし検査で E-101 の hash に pin）。

### 2.4 期待画像

- テスト画像：texel(u,v) は R=16+32u、G=16+32v、B=16+32((u+3v)&7)、A=255。位置を識別でき、x／y で非対称です。
- 規約：原点は左上、y は下向き、行 0 が memory 先頭。texture の memory は R,G,B,A 順。pixel centre は半整数（PS 側で +0.5）。
- 期待値：`expected(x,y) = texel(x/4, y/4)` を B,G,R,A の little-endian dword に pack。入力 format、出力 format、CPU byte 列を分けて定義しました。
- 比較は 1024 画素すべてで、最初の不一致の座標・期待値・観測値を記録します。

### 2.5 GPU なし検査（+5、合計 381）

実機 PASS bytes の hash pin（C1、単色 draw）、textured batch の差分が「2 dword 追加＋3 dword 変更」だけであること、state 頁の要所、PS 640 bytes の hash（generator の生 .bin から host で計算した値との照合＝.inc 転記の独立検査）、期待値の固定点と非対称性。

## 3. T2 の結果（E-103、独立起動、1 提出）

C1 も単色 draw も先に流していません。texture 頁は画像の後ろを guard で埋め、RT は 0x5a5a5a5a で初期化しました。RT を期待値で先埋めする処理や CPU コピーによる代替はありません。

image: vmunix `48bb8b71…` / hdd-image `2e5113fb…`、flags `-DCONFIG_DRIVER_PCI_I915_PARITY=1 -DPARITY_TEX_TEST=1`。

```
TEX-TEST fixture: batch_dwords=355 mocs=6 pdp0_matches_top=1 | pipeline_select rc=0 3d=1@6 gpgpu=0 bad=0
EU-TEST record(completed): rq seqno expected=2 hwsp_observed=2 request_seqno_reached=1
TEX-TEST PASS: completed=1 parked=1 timed_out=0 wedged=0 polls=17 | before/middraw/after/ps_marker すべて一致
  | pixels match=1024/1024 stale=0 | texture changed_bytes=0 guard_bad_bytes=0
attach end: STOPPED (i915_driver_probe complete) / teardown 正常・reset なし / ktest 381/0
```

kernel 内の比較に加えて、ログから抽出した RT を host 側で独立に計算した期待画像と照合し、1024/1024 でした。目視用の画像を添付します（R が右へ、G が下へ増える 8×8 パターン）。合格根拠は全画素比較です。

| object | bytes | SHA-256 |
|---|---|---|
| batch | 1420 | `d853b63dcd5e466be4e5686040a770a55f615d4700229467a02b153c99892265` |
| state 頁（実行後） | 4096 | `ded3157bd1e411c3e31b4815c95d678acd0f010de6d2314b0a8c7178e8456fb5` |
| texture | 256 | `899e10ea961999253ca7c084565952814318a25cf22469c2badfe46eeced291c` |
| RT（読み戻し） | 4096 | `cccce0a20b92e3bb07d3fd4f70ccb61c110253273f211e486acd806eb7496599` |

未確認のまま広げていないもの：bilinear、mip、頂点 UV の補間、sRGB、圧縮、実モニタ表示。

## 4. T2 の 1 回目（不合格）の経緯

- 観測：request 完了、PS marker 着地、sampler 動作、reset なし。ただし 1024 画素すべてが texel(0,0) の色で、一致は 16 画素だけでした。
- 原因：generator で PS を「画素座標の内部命令を直書き」する形にしていました。固定 Mesa の compiler は、shader が gl_FragCoord か入力を読むときだけ、subspan 座標から画素 X/Y を導出するコードを出します。そのため導出コードが無く、kernel は未書込みのレジスタを座標として読み、UV が常に 0 付近でした。逆アセンブルに導出命令が無いことでも確認しました。
- 修正：gl_FragCoord を使う front-end 形に直し、compiler が要求する payload（source depth、source W、GRF start 4）を genxml で pack して state に反映しました。GPU 側の設定を推測で足したものはありません。
- 教訓：単色 texture なら見逃していました。位置を識別できる非対称パターンと全画素比較が効きました。1 回目のログも保存してあります。

「R1・T1 が通れば T2 を一回」の枠に対して、T2 は 2 起動になりました。1 回目はハングではなく、原因が generator 側と特定できたため、直して再実行しています。

## 5. 出典記録

`plan/ws031/provenance-ledger.md` を新設しました。区分（コピー／改変／生成物／独立実装）、元 project・path・revision、元の license、未監査の明記を 1 行ずつ残します。

| 今回の新規 | 区分 | 元・license |
|---|---|---|
| `src/drivers/gpu/i915/tex_fixture_gen.inc` | 生成物 | Mesa main @ab691a1c（brw compiler、isl、genxml gen120）、MIT。冒頭に generator、入力 revision、PS の sha256 を記載 |
| `tools/reftex.c` | 独立実装 | Mesa の公開 API を呼ぶ試験用ツール。Mesa のコードは複製していません |
| `draw_fixture.h`、試験 harness | 独立実装 | テスト画像の式は今回のご提案による |
| `.inc` の PIPELINE_SELECT 定数 | 改変（定数 1 個） | Linux `gt/intel_gpu_commands.h`（SPDX MIT）、Mesa genxml（MIT）との照合 |

DMC firmware は driver source とは別管理として記載し、ライセンス全文と配布条件は未監査と明記しました。Linux の補助コード（workqueue 等）に対応する zedBSD 側実装も、関数本体の複製が無いことは未監査と書いています。

## 6. 提出物（`plan/ws031/handover/increment-results/`）

`e97-e103-changes.patch`（base 2bf790a4）、`e102-run-parity-hw-r1.log`、`e103-tex-*`（batch、state、texture、RT の bin と manifest、期待画像と RT の ppm、`e103-tex-rt.png`）、`e103-texfix-*`（PS の bin、逆アセンブル、generator の manifest）、`e103-run-parity-hw-tex.log`、1 回目のログ 2 本。ツールは `tools/reftex.c`、`tools/eu_artifact.py`、`tools/ppm2png.py`。台帳は E-102、E-103。

## 7. 次（T3）

同じ texture object の内容更新、texture A／B の binding 切替、同一 context の再描画、新規 context で同じ fixture を 1 起動にまとめます。順序は「完了待ち → CPU 更新 → 次の提出」です。更新用の第 2 パターンは定義済みです。その後、著作権・ライセンス整理と動作を変えないリファクタへ入ります。
