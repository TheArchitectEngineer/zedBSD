# Gen12 PS ハング 追加報告（専門家の助言に基づく監査結果）

前回レポートへの助言、ありがとうございました。指摘いただいた監査項目を実機で実施し、生 DWORD を
ビット単位で照合しました。結論から言うと、**設定・カーネルの両方が正しいと確認**でき、症状は不変です。

## 1. 「排除済み」を戻した件 — 了解し、再検証しました

- **PS_INVOCATION_COUNT=0 の解釈**: ご指摘どおり単純なリタイアカウンタと断定していました。
  なお読み出しは、ハングした RCS とは別に投入した MI_STORE_REGISTER_MEM リクエストで、
  seqno/breadcrumb で完了を確認した上でサンプルしています（読み出しリクエスト自体は完走）。
  ただし「0 = 未ディスパッチ」とは断定していません。
- **bare send.ts EOT**: PS の正規終了の対照試験にならないというご指摘、承知しました。以降の判断材料から外します。
- **IC not-done / RCC done の解釈**: 「命令フェッチ停止の確定診断」にはせず、あくまで観測値として扱います。

## 2. 設定監査（生 DWORD、実機ダンプ）— すべて正常

### STATE_BASE_ADDRESS（22 DW 生値）
```
[00]=61010014 [01]=00000061 [02]=00000000 [03]=00060000   ; header, general(mod,MOCS6,base0), -, stateless MOCS6
[04]=00400061 [05]=00000001                                ; surface base = 0x1_00400000, modify, MOCS6
[06]=00400061 [07]=00000001                                ; dynamic  base = 0x1_00400000, modify, MOCS6
[08]=00000061 [09]=00000000                                ; indirect base = 0, modify, MOCS6
[10]=00400041 [11]=00000001                                ; instruction base = 0x1_00400000, modify, MOCS4(WB idx2)
[12]=fffff001 [13]=fffff001 [14]=fffff001 [15]=fffff001    ; general/dynamic/indirect/INSTRUCTION size = modify+0xfffff
[16]=00400061 [17]=00000001 [18]=0003f000                  ; bindless surface base/size
[19]=00000061 [20]=00000000 [21]=00000000                  ; bindless sampler
```
- **A（Instruction Buffer Size Modify Enable）**: `[15]=0xfffff001` → **modify bit=1**（0xfffff000 ではない）。正常。
- decode: instruction base = `0x1_00400000`, MOCS=4(WB index2), modify=1。

### 3DSTATE_PS（12 DW 生値）
```
[00]=7820000a [01]=00000400 [02]=00000000   ; header, KSP0=0x400(=1024), KSP0 hi=0
[03]=00040000                               ; BindingTableEntryCount=1 (bit18)
[04]=00000000 [05]=00000000                 ; scratch
[06]=1f800001                               ; 8PixelDispatch=1(bit0), MaxThreadsPerPSD=63(bits31:23)
[07]=00020000                               ; DispatchGRFStart0=2 (bits22:16)
[08..11]=0                                  ; KSP1/KSP2 unused
```
- **decode: instruction_base(0x1_00400000) + KSP0(0x400) = 0x1_00400400 = カーネル配置アドレスと一致** ✓
- CPU からの読み戻しでも `kernel@0x100400400 [0]=0x80030061 ...` を確認済み（GPU に渡したフィールドから
  構成されるアドレス == CPU が書いた場所）。

### CTX_CONTEXT_CONTROL（LRC image, draw 時点）
```
= 0xffff0008   ; mask=0xffff, value: bit3(INHIBIT_SYN_CTX_SWITCH)=1, bit0(RESTORE_INHIBIT)=0
```
- draw は初回投入ではない（前に rcs/rt selftest あり）ため、HW が保存・復元した正常値。
- **B（初回 LRC restore inhibit）の確認**: 初期化時は `CTX_CONTEXT_CONTROL=0x00090009`
  （RESTORE_INHIBIT=1, INHIBIT_SYN=1）で Linux の inhibit=true と一致。default_state が無い場合の
  初回 inhibit を実装済み。RPCS も `ctx=0x80041000 == reg=0x80041000` で一致 → **LRC restore は機能**。

### C（HDC Pipeline Flush）
- pre-SBA flush の PIPE_CONTROL DW0 に `PIPE_CONTROL0_HDC_PIPELINE_FLUSH`(bit9) を追加。**症状不変**。

### その他 config
- BindingTableEntryCount 0→1（BLORP 準拠）に変更。**症状不変**。

## 3. カーネル構造の再確認 — Mesa の SIMD8 single-source と一致

`brw_lower_logical_sends.cpp` の FB write lowering を精読しました:
- Gen12 (ver>=11) は **headerless**（header_size=0）
- `send->mlen = regs_written(load)`（色ペイロードのレジスタ数）
- SIMD8 RGBA → 4 GRF → **mlen=4 の single send**

gentool の tgl 実データでも `sendc.render (16) null r119 null ... wr:8+0`（SIMD16 single）が存在し、
私の `sendc.render (8) null r112 null ... wr:4+0`（SIMD8 single）はその類推で正しい構造です。
（先に「Mesa は split send」と述べたのは誤りで、`wr:2+2` は src0_alpha 等を持つ別ケースでした。）

## 4. リファレンスコンパイラのビルド状況

`brw_compile_fs` を叩く standalone ハーネス（定数色 FS を NIR で構築 → コンパイル → プログラム bytes を
hex 出力 → gentool で逆アセンブル）を作成し、**コンパイルは成功**。しかしリンクで NIR の生成シンボル
（nir_op_infos, nir_opt_algebraic 等）が未解決。原因は mesa の build gating:
```
with_nir_headers_only = not with_gfx_compute
with_gfx_compute = [with_any_opengl, with_any_vk, with_clc, ...].contains(true)
```
フル NIR は「実ドライバ（OpenGL/Vulkan/CLC のいずれか）」を有効化しないと生成・コンパイルされず、
各ドライバは glslang/LLVM 等の重い依存を要求します（anv は glslangValidator が必要でブロック）。
環境を汚さずに通すのは大きな回り道になっています。

## 5. 現時点の確定事項と、伺いたいこと

**確定**: GPU に適用される state（SBA 全 base/size、instruction base+KSP のアドレス整合、
3DSTATE_PS、CTX_CONTEXT_CONTROL、RPCS、L3、URB）と、PS カーネルの send 構造の両方が正しい。
それでも PixelShaderValid=1 で描画すると:
- ジオメトリはクリッパ通過（cl_prim=1）
- RT write は RCC に届かない（RCC done、pixel 未書き込み）
- 3DPRIMITIVE 後の PIPE_CONTROL(drain) で CS 停止、GPU fault 無し
- WMFE と PSS のみ not-done

### 質問
1. **設定もカーネルも正しく見えるのに PS だけがこの止まり方をする場合、Gen12 で次に疑う具体箇所**は
   どこでしょうか。EU の per-thread IP（どの命令で止まっているか）を読むべきでしょうか。読むなら、
   Gen12 で thread の IP/停止理由を得る MCR レジスタ（EU_ATT 等）をご教示いただけますか。
2. 参照カーネルを glslang/LLVM 無しで得る現実的な方法はありますか（例: 生成 NIR .c を手動生成して
   ハーネスに直接コンパイル、intel_clc の利用、既存のプリコンパイル済み Gen12 PS バイナリの所在など）。
3. あるいは、**Gen12 SIMD8 定数色 PS の正解カーネル bytes を直接ご提示**いただけると、私の手書き
   （`80030061 70054aa0 00000000 3f800000` ×4色 + `00030132 00000004 58007024 00c40000`）と
   逐一比較でき、最短で決着します。

（補足: 提出は PPGTT batch を non-secure MI_BATCH_BUFFER_START で RCS0 kernel context のリングから起動。
同じ PPGTT への CS の MI_STORE は成功。instruction/surface/dynamic base は同一 4KB ページ、別ページでも同一ハング。）
