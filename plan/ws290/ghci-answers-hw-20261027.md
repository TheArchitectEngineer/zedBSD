# GHCI の疑問への hardware の担当の一次回答（2026-10-27）

- 出典: 2026-10-27 ユーザーが伝えた hardware の担当の回答（下の「原文」はユーザーの貼った文のまま）。ユーザー「ハードウェア担当からの一次回答です。このあと資料全般を修正するので、それまで待ってください。回答は修正されるかもしれません。」
- 状態: **一次回答**。ghci.md の資料の修正を待つ。修正の後に ghci.md の版を読み直して design に反映する（それまで driver の設計・実装に使わない）。
- 疑問の本体は [ghci-questions.md](ghci-questions.md)（G01〜G39）。G01 の回答（単一の GHCI 1.0）は [ws.md](ws.md)。

## 原文

G02: Register の窓（BAR0）の大きさと EXTCAP の配置

* BAR0 物理デコードサイズ: 128 KiB（17-bit アドレス空間 `0x00000`〜`0x1FFFF`）。KC705 PCIe IP（`CONFIG.BAR0_SIZE 128`）および atg_top.v のレジスタポート `s_araddr[16:0]` と一致。未定義空間はリードで 0、ライト無視。
* アクティブ論理範囲: 先頭 4 KiB（1 ページ）で十分。すべての実レジスタは先頭 1 KiB（`0x000`〜`0x3FF`）に収まっています：
   * `0x000`〜`0x048`: 基本 HCR（19 レジスタ、76 バイト）
   * `0x04C`〜`0x0AC`: 拡張診断・リング・性能レジスタ
   * `0x100`: EXTCAP リスト先頭（`EXTCAP_PTR` が指す）
   * `0x140`: FMT（フォーマット機能）レジスタ
   * `0x1D0`: PROT（セッション保護）レジスタ
   * `0x1E0`: MEM（ローカルメモリ窓）レジスタ
   * `0x200`〜`0x2BC`: DISP2（ディスプレイコントローラ）レジスタ
* ドライバへの指針: OS は BAR0 の先頭 4 KiB を 1 回 `ioremap` するだけで、基本制御および全 EXTCAP（表示・メモリ等）をすべて操作可能です。

G03: RING_RPTR / RING_WPTR の単位

* 正解: dword index（32-bit ワードインデックス、4 バイト刻み）。
* 根拠: atg_cp_ring.v L309 `mem_raddr = {base[31:2], 2'b00} + (cur << 2)` および L361 `cur <= cur + h_n`（パケット長 dwords の加算）により、完全に dword 単位で動作します。§3.1 の「byte offset」は仕様書の誤記であり、dword index に訂正します。

G04: アドレス空間とセッション解決

* `RING_BASE`: ホスト DMA / PCIe バスアドレス（またはホスト IOMMU の IOVA）。GPU 内部 VA ではありません。
* `fence_addr`: ホストメモリ空間のアドレス（PCIe バスアドレス / IOVA）。
* `CMD_DRAW` 等の解決: 現在バインド中のセッションの GPU VA（32-bit） で解決されます。セッションは `CMD_BIND_SESSION [session_id]`（またはキュー/PSOバインド）により CP 内部の `active_session` に設定され、MMU（`atg_mmu.v`）の `session_root_pt[active_session]` を介してハードウェアウォーカーが解決します。

G05: VM_MAP / VM_UNMAP の符号と dwords

* Opcode（8-bit）:
   * `CMD_VM_MAP`: `0x10`
   * `CMD_VM_UNMAP`: `0x11` （`0x10110` は `ghci_frame.h` 内のカテゴリプレフィックス付 32-bit 定数であり、リングヘッダの 8-bit opcode 部は下位 8-bit の `0x10`, `0x11` です）
* パケットサイズ（dwords）:
   * `CMD_VM_MAP`: 10 dwords（40 bytes: header, session_id, flags, rsvd, gpu_va[63:0], iova[63:0], size[63:0]）
   * `CMD_VM_UNMAP`: 8 dwords（32 bytes: header, session_id, flags, rsvd, gpu_va[63:0], size[63:0]）

G06: Session 生成・破棄コマンドと未対応通知

* コマンド:
   * `CMD_SESSION_OPEN`（opcode `0x12`, 4 dwords: header, session_id, flags, root_pt_phys）
   * `CMD_SESSION_CLOSE`（opcode `0x13`, 2 dwords: header, session_id）
* 未対応時の通知:
   1. 事前に `DEVICE_CAPS.CAP_HW_WALKER` / `CAP_PROT` を読み取り、0 の場合はドライバが API レベルで `VK_ERROR_FEATURE_NOT_PRESENT` を返してリング投入を抑止。
   2. 未対応のまま投入された場合、CP デコーダが即座に停止し、`FAULT_INFO`（kind 1: 不正opcode）、`STATUS.FAULT = 1`、`IRQ_STATUS.IRQ_FAULT` をアサート。
* セッション数上限: ハードウェア MMU（`atg_mmu.v`）は現在 最大 16 セッション（0〜15）。PTE の session tag は最大 64 まで拡張可能です。

G07: アドレスのビット幅

* GPU VA（内部仮想アドレス）: 32-bit 固定（10-bit PDI + 10-bit PTI + 12-bit offset = 4 GiB 空間/セッション）。シェーダ、頂点、テクスチャ、ディスクリプタ等はすべて 32-bit GPU VA です。
* ホスト / PCIe バスアドレス: 64-bit（`RING_BASE_LO/HI`, `fence_addr`, `VM_MAP` の `iova`）。
* `VM_MAP` の `gpu_va`: 将来の拡張性のため 64-bit スロット（lo/hi）ですが、GHCI 1.0 では `gpu_va_hi == 0`（32-bit）のみを受理します。
* PTE の物理アドレス: `[31:12]`（20-bit PFN = 4 GiB 空間）。

G08: ページテーブルの配置・容量・不足時・リセット

* 配置: DRAM 上（ローカル VRAM または UMA 共有メモリ）。
* 容量確保: CP ファームウェア（`CP_HEAP_BASE/LIMIT`）またはホスト OS が 4 KiB 境界のページテーブル実体を確保して CP に渡します。
* メモリ不足時: CP がマッピングを中断し、`FAULT_INFO` に `FAULT_MMU_OOM` をセットして `IRQ_FAULT` を発行。
* リセット時の保持: `CTRL.RESET` はコア論理回路と TLB を初期化しますが、DRAM 内容は保持されます。ただしドライバまたは CP は起動時に `session_root_pt` を再設定します。

G09: CAP_HW_WALKER = 0 の挙動と CAP_IOMMU

* `CAP_HW_WALKER = 0` の場合: 仮想記憶はバイパスされ、フラットな物理アドレス（1:1 直接マッピング） として動作します。
* `CAP_IOMMU`: ボードトップ／ファームウェアがブートストラップ時に設定し、ホスト側 IOMMU（VT-d / AMD-Vi / ARM SMMU）経由の IOVA を要求するか、直接物理アドレスを要求するかをドライバに示します。

G10: TLB 無効化と CTRL.TLB_FLUSH / CMD_BARRIER

* 通常動作: `VM_UNMAP` 実行時に CP がインフライト完了待ち（ドレイン）後に自律的に TLB を無効化するため、ドライバの手動フラッシュは不要です。
* `CTRL.TLB_FLUSH`: ドライバ終了処理、プロセス強制終了、またはデバッグ時の非常用ハードウェアフラッシュです。
* `CMD_BARRIER`（TLB/Cache フラグ）: マッピング解除を伴わず、同一ページ内容を CPU や他エンジンが書き換えた後のコヒーレンシ同期に使用します。

G11: Fence のビット幅・書き込みバイト数・揃え

* 標準 `CMD_FENCE`: 32-bit（4 バイト） を `fence_addr` に書き込みます（4 バイト揃え）。
* Timeline Semaphore 拡張（`flags[1] == 1`）: 64-bit（8 バイト） をアトミックに書き込みます（8 バイト揃え）。

G12: IRQ マスクと IRQ_SET.SOFT

* マスク: `IRQ_ENABLE`（0x01C） で行います（`CTRL.IRQ_GLOBAL` は RTL 上存在せず、`IRQ_ENABLE` のみでゲートされます）。
* 事象の記録: `IRQ_ENABLE` が 0 でも `IRQ_STATUS` のビットは立ちます（ピン出力のみがマスクされる）。
* `IRQ_SET.SOFT`: 有効に残ります（`0x040` へのライトで `IRQ_STATUS.SOFT`（bit 5）を任意にアサート可能。ドライバの疎通確認用）。

G13: CP 状態の読み取り

* 正解: `STATUS[19:16]`（4-bit: 0=IDLE, 1=RUN, 2=HALTING, 3=HALTED, 4=FAULTED, 5..7=RESET）。§3.3 の reserved 記述は古いドラフトの誤りです。

G14: Fault kind の統一とフォールトセッション

* 統一 Kind 番号（`FAULT_INFO[15:12]`）:
   * `1`: 不正 opcode / 未実装ユニット
   * `2`: 不在ユニットへの KICK
   * `3`: 不正レジスタオフセット
   * `4`: ウォッチドッグタイムアウト
   * `5`: フェンス／結果書き込みエラー
   * `6`: セッション衝突 / 保護違反
   * `7`: アパーチャ／未オープン書き込み違反
   * `9`: MMU ページフォールト（未マップ）
   * `10`: MMU パーミッション違反
* フォールトを起こした Session: `FAULT_INFO[31:24]`（および MMU の `fault_session`）に記録されます。

G15: Fault の回復と CTRL.STEP

* 回復範囲: セッション保護（`CAP_PROT`）有効時は対象セッションのみを破棄（ドレイン＆TLBフラッシュ）して他セッションは継続可能。無効時またはハードウェア障害時は `CTRL.RESET` による全体復元となります。
* `CTRL.STEP`: ハードウェアデバッグ用に有効（HALTED 状態から 1 パケットずつステップ実行可能）。

G16: Watchdog レジスタと AXI 状態

* Watchdog 設定: `CTRL[4]`（WDT_EN） で有効化、`WDT_CYCLES`（0x044） でタイムアウトサイクル数を設定。
* AXI 状態: `STATUS[13]`（axi_stuck）、`STATUS[14]`（quiesce） で読めます。

G17: Vendor / Device / Clock の識別

* Vendor / Device ID: PCIe コンフィグ空間（オフセット `0x00`, `0x02`）および EXTCAP の `VEND`（ID 4）から取得。
* バージョン: `GHCI_VERSION`（0x004）から major 1, minor 0 を取得。
* Clock / timestampPeriod: コアクロック `GCLK_HZ`（KC705 は 100 MHz →\to→ `timestampPeriod = 10.0` ns）。

G18: アーキテクチャの制限値（Limits）

* `RESIDENT_WARPS`（0x04C）: `[15:0]` 同時実行ワープ数（`NSU * NWARPS`）、`[23:16]` レーンあたりレジスタ数（32 または 64）。
* `DEVICE_CAPS`（0x008）: `[15:8]` 物理レーン数（`SU_NPHYS`）、`[23:16]` SU 数（`NSU`）。
* アタッチメント数: EXTCAP `FMT_LIMITS`（+0x10）の `[3:0]` で読み取り（最大 4 または 8）。
* LDS / Varying: 標準プロファイル値（LDS 4 KiB、Varying 16 ワード）。

G19: PSO の `*_code_va` と GSI1

* ポインタの指す先: GSI1 バイナリの先頭（Header）。ヘッダにエントリポイント、定数数、LDS サイズが含まれます。
* 検査失敗時: CP がロード時にヘッダ不正を検知し `FAULT_KIND = 1` でフォールト。
* シェーダユニット識別: `NSU > 0` であれば `CAPS[2]`（`CAP_SU`）と `DEVICE_CAPS[7]`（`CAP_GSI1`）が共に 1。0 であれば非シェーダ（Blit専用等）構成。

G20: バッファディスクリプタ・Push Constant・ディスクリプタテーブル

* テーブル指定: `CMD_BIND_DESCRIPTOR_TABLE`（0x23）でディスクリプタテーブルの GPU VA を設定。
* ディスクリプタ形式: 16/32 バイト標準形式（Base VA, Size/Stride, Format/Flags）。
* Push Constant: `CMD_SET_DYNAMIC_STATE`（または `CMD_LOAD_CONST`）で投入。

G21: カラー・デプス Attachment と Load/Store/Clear

* 指定方法: `CMD_BEGIN_RENDER_PASS`（または PSO 内のアタッチメント配列）で各カラー（0〜7）およびデプス/ステンシルの GPU VA、Stride、Format をバインド。
* Load/Store/Clear: RenderPass の LoadOp（CLEAR/LOAD/DONT_CARE）および StoreOp（STORE/DONT_CARE）フラグに基づき、レンダリング開始時/終了時に ROP/Blitter が処理。

G22: 転送コマンド（Copy, Fill, Blit, Clear, Resolve）

* ハードウェア Blitter（`atg_blt`）が担当し、以下のコマンドを用意：
   * `CMD_COPY_BUFFER`: バッファ間転送
   * `CMD_COPY_IMAGE` / `CMD_BLIT`: 画像コピーおよびフォーマット変換/スケーリング
   * `CMD_FILL_BUFFER`: バッファクリア
   * `CMD_RESOLVE_IMAGE`: MSAA リゾルブ

G23: クエリ・イベント待ち

* `CMD_WRITE_TIMESTAMP`: 64-bit サイクルカウンタをメモリに記録。
* `CMD_BEGIN_QUERY` / `CMD_END_QUERY`: ROP サンプル数集計（オクルージョンクエリ）。
* `CMD_WAIT_FENCE` / `CMD_WAIT_EVENTS`: 条件成立までリング実行をブロック。

G24: PSO サイズと頂点・ブレンド設定

* PSO サイズ: 128 バイト（MRT、ブレンド、デプスステンシル、頂点属性を包含）。
* 頂点 Stride: 動的コマンド `CMD_BIND_VERTEX_BUFFERS` で渡す stride が静的 PSO 設定を上書き優先。
* 属性オフセット: 16-bit オフセットに拡張。
* フォーマット・係数符号: `VkFormat`, `VkBlendFactor`, `VkStencilOp` と直接 1:1 対応。

G25: Indirect Draw と Count バッファ

* Indexed / 非 Indexed 判定: `CMD_DRAW_INDIRECT` のフラグ bit `[0]` で区別（0: 非Indexed, 1: Indexed）。
* Count バッファ: ペイロード内の `draw_count_va`（0 の場合は固定カウント）。

G26: CMD_EXECUTE_COMMANDS（セカンダリバッファ）

* アドレス空間: 親リングと同一のセッション GPU VA 空間。
* 入れ子: 1 階層のみ（再帰禁止）。
* 制限: 内部での `CMD_VM_MAP`, `CMD_VM_UNMAP`, `CMD_SESSION_*` は禁止。

G27: 暗黙のキャッシュ無効化と CMD_BARRIER

* 暗黙無効化: パイプラインバインドや Draw では暗黙のキャッシュフラッシュは行いません（性能低下を防ぐため）。
* `CMD_BARRIER`: `ghci_cmd.h` に定義されたステージビット（`STAGE_VERTEX`, `STAGE_FRAGMENT`, `STAGE_COMPUTE`）およびアクセスビット（`ACCESS_COLOR_ATTACHMENT_WRITE`, `ACCESS_SHADER_READ` 等）を用いて明示的に同期します。

G28: ディスプレイ制御とアドレス

* コマンド: `CMD_DISP_SET` および `CMD_FLIP` はリングパケットとして発行可能（CP 停止時は MMIO から直接設定可能）。
* IRQ 区分: `IRQ_STATUS[9]` = VBLANK, `[10]` = FLIP, `[11]` = DISP_ERROR, `[12]` = HOTPLUG。
* アドレス: `FB_BASE`, `FB_NEXT`, `CURSOR_BASE` はローカル VRAM 物理アドレス（KC705 では DDR3 アドレス）。

G29: 有効な EXTCAP

* 有効: `DISP` (1), `DBG` (2), `PERF` (3), `VEND` (4), `MEM` (8), `PROT` (9), `FMT` (10), `DISP2` (11)。
* 予約／内部統合: `MMU` (5, コア統合), `CPFW` (6), `SA` (7)。

G30: 初期化手順

1. `GHCI_MAGIC` および `GHCI_VERSION` を確認。
2. `DEVICE_CAPS` および `EXTCAP` を読み取り機能確認。
3. `STATUS.mem_ready == 1`（DRAM 初期化完了）を待機。
4. `RING_BASE_LO`, `RING_BASE_HI`, `RING_SIZE` を設定。
5. `IRQ_ENABLE` および `CTRL.WDT_EN = 1` を設定。
6. `CTRL.RUN = 1` を書き込んで RUNNING 状態に移行。

G31: MEM capability の LOCAL_BASE / HOST_LIMIT

* `LOCAL_BASE`: ローカル DRAM の先頭物理アドレス（KC705: `0x8000_0000`）。
* `VM_MAP` 時: ローカルメモリをセッション VA にマップする際、物理アドレス引数には `LOCAL_BASE + offset` を指定します。

G32: ページテーブルの構成

* セッションごとに独立したページディレクトリルート（`session_root_pt[session_id]`）を持ちます。PTE 内のセッションタグと合わせてセッション間のメモリ汚染を完全に防止します。

G33: IDLE 状態での書き込み

* `RING_WPTR`（Doorbell）: IDLE 状態でも書き込み可能。`CTRL.RUN == 1` であれば直ちに RUNNING へ移行して処理を開始します。
* `IRQ_STATUS`（W1C）: IDLE, RUN, HALTED, FAULTED の全状態で書き込みクリア可能です。

G34: リセット時の IRQ 保持と未マスク事象

* 保持: `IRQ_ENABLE` は `CTRL.RESET` ではクリアされず保持されます（コールドリセット `rst_n` でのみクリア）。
* 未マスク事象: `IRQ_ENABLE` の該当ビットが 0 であっても、事象が発生すれば `IRQ_STATUS` に記録されます（割り込みピン出力のみが抑止される仕様です）。

G35: CP の内部メモリ確保

* CP のファームウェアヒープおよびスクラッチ領域は、MMIO の `CP_HEAP_BASE` / `CP_HEAP_LIMIT`（`0x0B0`/`0x0B4`）で指定された専用 DRAM 領域から確保されます。

G36: VM_MAP / VM_UNMAP の規約

* 揃え: 4 KiB 境界。
* 二重マップ: 既にマップされている VA に対する `VM_MAP` はエラー（フォールト）となります（再マップ前に UNMAP が必要）。
* 部分アンマップ: 4 KiB 単位で任意の部分範囲のアンマップに対応。
* サイズ 0: 無視（No-op）。

G37: パイプライン状態のスコープ

* `CMD_BIND_*` / `CMD_SET_*` で設定されたパイプライン状態はアクティブなセッションにスコープされます。セッション切り替え時や GPU リセット時は無効化されます。

G38: COHERENT / WC フラグ

* PCIe プラットフォームにおいて、`COHERENT` フラグは GPU の L2 キャッシュに対して「同期ポイントで無効化/バイパスを行う」属性を指定します。`WC` はライトコンバイニング属性を指定します。

G39: CMD_FENCE の値の任意性

* `CMD_FENCE` はペイロードで指定された任意の 32-bit / 64-bit 値を書き込みます。ただし、ドライバ側で `WAIT_FENCE` を用いる場合は、判定条件（`mem - value >= 0`）を満たすため単調増加カウンタとして運用する必要があります。

ホスト AXI 64-bit 固定化について
先ほどご依頼いただいた「ホストに接続する AXI の 64-bit 固定化」は、本仕様の `DATA_W = 64`（BAR0 スレーブおよび DMA マスタ）として完全に整合いたします。現在実行中の KC705 ビットストリーム合成が完了次第、仕様書および RTL への反映を実施いたします。

テクスチャ圧縮やイメージタイプの一覧もghci.mdに含まれていますか？ドライバ担当が認識できていないようですが

ご指摘の通りです。ドライバ担当者様が認識できていなかった原因は、RTL実装（atg_tu_etc.v）や内部ヘッダ（include/atg_fmt_info.vh, include/ghci_layout.h）には完全な仕様とデコーダが存在するものの、docs/ghci.md の本文中にフォーマット一覧・圧縮フォーマット・イメージタイプの明示的な表が不足していたためです。

現在 docs/ghci.md §8.3 では、ディスクリプタ構造体の説明として以下の 1 行のみが記載されており、具体的な VkFormat の値や対応する圧縮規格が一覧化されていませんでした：

- format: RGBA8, BGRA8, RGB565, D24S8, D32F, BC1..BC7, ETC2（概要文字列のみ）
- dimension: 0=1D, 1=2D, 2=3D, 3=CubeMap

ハードウェア（RTL）の実装実態

ハードウェアのテクスチャユニット（TU: rtl/gpu/tu/atg_tu_etc.v）およびフォーマット制御部（include/atg_fmt_info.vh）には、Vulkan 1.0 必須の圧縮規格である ETC2 / EAC の全 10 フォーマット と 全イメージタイプ が完全に実装・検証済みです。

1. テクスチャ圧縮フォーマット（ETC2 / EAC 全 10 種類）

Vulkan 1.0 標準規格に準拠した 4×4 ブロック圧縮：

| VkFormat 番号 | フォーマット名 | ブロックサイズ | 内部デコード方式 |
| --- | --- | --- | --- |
| 147 | VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK | 8 bytes | Individual, Differential, T, H, Planar |
| 148 | VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK | 8 bytes | sRGB 非線形ガンマ逆変換付き |
| 149 | VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK | 8 bytes | 1-bit Punch-through Alpha |
| 150 | VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK | 8 bytes | Punch-through Alpha + sRGB |
| 151 | VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK | 16 bytes | 8-byte EAC Alpha + 8-byte ETC2 RGB |
| 152 | VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK | 16 bytes | EAC Alpha + ETC2 RGB + sRGB |
| 153 | VK_FORMAT_EAC_R11_UNORM_BLOCK | 8 bytes | 11-bit 単一チャンネル非負正規化 |
| 154 | VK_FORMAT_EAC_R11_SNORM_BLOCK | 8 bytes | 11-bit 単一チャンネル符号付き正規化 |
| 155 | VK_FORMAT_EAC_R11G11_UNORM_BLOCK | 16 bytes | 11-bit 2 チャンネル（法線マップ等） |
| 156 | VK_FORMAT_EAC_R11G11_SNORM_BLOCK | 16 bytes | 11-bit 2 チャンネル符号付き |

フィルタ機能: バイリニア補間（VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT）および点サンプリング（Nearest）の両方に対応（atg_tu_ffilt 経由）。

2. イメージタイプ（Image Dimensions & Views）

ディスクリプタの dimension フィールド（0x19）およびレイアウト計算（include/ghci_layout.h）：

| コード | Vulkan イメージ種別 | 説明 |
| --- | --- | --- |
| 0 | VK_IMAGE_TYPE_1D | 1次元テクスチャ（幅 width, height=1, depth=1） |
| 1 | VK_IMAGE_TYPE_2D | 2次元標準テクスチャ（幅 width, 高さ height） |
| 2 | VK_IMAGE_TYPE_3D | 3次元ボリュームテクスチャ（幅・高さ・奥行き depth） |
| 3 | VK_IMAGE_VIEW_TYPE_CUBE | キューブマップ（6 プレーン、atg_tu_cube によるシームレス補間対応） |
| 配列 | VK_IMAGE_VIEW_TYPE_*_ARRAY | 1D / 2D 配列テクスチャ（depth にレイヤー数を設定、slice_pitch_bytes でストライド指定） |

ミップマップ: 1〜15 レベル（mip_levels）

マルチサンプリング (MSAA): 1, 2, 4 サンプル（sample_count）

仕様書への反映対応

ドライバ開発者が迷わず実装できるよう、docs/ghci.md の §8.4「Image Formats, Texture Compression & Dimensions」 として、上記のフォーマット一覧表・ブロックサイズ・VkFormat 値・イメージタイプ対応表を追記・拡充いたします。
