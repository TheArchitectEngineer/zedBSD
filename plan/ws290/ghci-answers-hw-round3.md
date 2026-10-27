# GHCI の H01〜H23 への hardware の担当の三次回答（2026-10-27）

- 出典: 2026-10-27 ユーザーが伝えた回答（ユーザー「下記の通りだそうです。疑問が解決するかレビューしてください。P1でやりましょう。」）。amaterrace-gpu の commit **7fc707d**（2026-10-27 14:37、docs/ghci.md・include/ghci_cmd.h・ghci_regs.h・ghci_frame.h・rtl の atg_cp_regs.v）。
- 質問は [ghci-questions-round2.md](ghci-questions-round2.md) の H01〜H23。下は貼られた文のまま（表と式の崩れだけ直した）。

## 原文

ドライバ担当者様からの追加質問（H01〜H23）について、RTL実装およびGHCI 1.0仕様に基づき仕様を確定させ、docs/ghci.md およびヘッダ群（include/ghci_cmd.h, include/ghci_regs.h, include/ghci_frame.h）、RTL（atg_cp_regs.v）の更新・整合コミット（commit 7fc707d -m WIP）を完了いたしました。

以下に全項目（H01〜H23）の確定仕様を回答いたします。

### H01: コマンド一覧表（Opcode・dwords・Capabilityガード）

全コマンドの符号体系を整理し、docs/ghci.md §7.2 および include/ghci_cmd.h に正式追加しました。

| Opcode | コマンド名 | サイズ(dw) | ガード / Capability | 概要 |
| --- | --- | --- | --- | --- |
| 0x00 | CMD_NOP | 可変 | 必須 | パディング / No-op |
| 0x10 | CMD_VM_MAP | 10 | CAP_HW_WALKER | 仮想メモリマッピング |
| 0x11 | CMD_VM_UNMAP | 8 | CAP_HW_WALKER | 仮想メモリアンマップ＆TLB無効化 |
| 0x12 | CMD_SESSION_OPEN | 4 | CAP_HW_WALKER / PROT | セッション生成・ルートPT初期化 |
| 0x13 | CMD_SESSION_CLOSE | 2 | CAP_HW_WALKER / PROT | セッション破棄・TLB一括フラッシュ |
| 0x14 | CMD_BIND_SESSION | 2 | 必須 | コマンド解決対象セッションの設定 |
| 0x20 | CMD_BIND_PIPELINE | 3 | NUM_SU > 0 | 不変 PSO バインド（128B） |
| 0x21 | CMD_BIND_VERTEX_BUFFERS | 2+4N | NUM_SU > 0 | 頂点バッファ配列バインド |
| 0x22 | CMD_BIND_INDEX_BUFFER | 4 | NUM_SU > 0 | インデックスバッファバインド |
| 0x23 | CMD_BIND_DESCRIPTOR_TABLE | 4 | NUM_SU > 0 | ディスクリプタテーブルバインド |
| 0x24 | CMD_SET_VIEWPORT | 7 | 必須 (Raster) | 動的ビューポート設定 |
| 0x25 | CMD_SET_SCISSOR | 5 | 必須 (Raster) | 動的シザー短形設定 |
| 0x26 | CMD_SET_DYNAMIC_STATE | 6 | 必須 (Raster) | 動的デプスバイアス・ブレンド定数 |
| 0x27 | CMD_DRAW | 5 | NUM_SU > 0 | 直接描画（非インデックス） |
| 0x28 | CMD_DRAW_INDEXED | 6 | NUM_SU > 0 | インデックス付き描画 |
| 0x29 | CMD_DRAW_INDIRECT | 6 | CAP_INDIRECT_DRAW | 間接描画（stride/maxCount対応） |
| 0x2A | CMD_DRAW_MESH_TASKS | 5 | CAP_MESH_SHADER | タスク/メッシュシェーダ起動 |
| 0x2B | CMD_BEGIN_RENDER_PASS | 10 | 必須 (Raster) | レンダーパス開始・Attachment設定 |
| 0x2C | CMD_END_RENDER_PASS | 1 | 必須 (Raster) | レンダーパス終了・StoreOp実行 |
| 0x2D | CMD_LOAD_CONST | 3+N | NUM_SU > 0 | Push Constants 直接ロード |
| 0x30 | CMD_DISPATCH | 4 | NUM_SU > 0 | コンピュートワークグループディスパッチ |
| 0x31 | CMD_BARRIER | 4 | 必須 | パイプラインバリア・同期 |
| 0x32 | CMD_EXECUTE_COMMANDS | 4 | CAP_SECONDARY_RING | セカンダリコマンドバッファ実行 |
| 0x33 | CMD_FENCE | 5 | 必須 | 64-bit メモリフェンス・完了通知 |
| 0x34 | CMD_WAIT_FENCE | 4 | 必須 | フェンス完了待ち |
| 0x35 | CMD_WRITE_TIMESTAMP | 4 | 必須 | 64-bit タイムスタンプ記録 |
| 0x36 | CMD_BEGIN_QUERY | 3 | 必須 (Raster) | オクルージョンクエリ開始 |
| 0x37 | CMD_END_QUERY | 2 | 必須 (Raster) | オクルージョンクエリ終了 |
| 0x40 | CMD_COPY_BUFFER | 7 | CAP_BLT | バッファ間 2D DMA コピー |
| 0x41 | CMD_COPY_IMAGE | 9 | CAP_BLT | 画像間コピー |
| 0x42 | CMD_BLIT | 10 | CAP_BLT | 画像ブリット（スケーリング/変換） |
| 0x43 | CMD_FILL_BUFFER | 5 | CAP_BLT | パターンクリア（ClearBuffer） |
| 0x44 | CMD_RESOLVE_IMAGE | 9 | CAP_BLT | MSAA リゾルブ |
| 0x50 | CMD_DISP_SET | 2+2N | CAP_DISP | 表示モード・タイミング設定 |
| 0x51 | CMD_FLIP | 4 | CAP_DISP | 次フレーム VSYNC フリップ要求 |

### H02: Capability レジスタのビットマップ（衝突解消）

0x020 / 0x024 は RING_BASE_LO / RING_BASE_HI 固定とし、全 Capability を DEVICE_CAPS（0x008） に統合しました：

- [0]: CAP_UMA
- [1]: CAP_IOMMU
- [2]: CAP_HW_WALKER
- [3]: CAP_INDIRECT_DRAW
- [4]: CAP_SECONDARY_RING
- [5]: CAP_DUAL_SOURCE
- [6]: CAP_FP16
- [7]: CAP_GSI1
- [15:8]: PHYS_LANES (4, 8, 16, 32)
- [23:16]: NUM_SU (Shader Unit 数。0 ならシェーダ非搭載、≥1 で CAP_SU 成立)
- [24]: CAP_PROT
- [25]: CAP_TEX_FFILT
- [26]: CAP_ROP_PACK
- [27]: CAP_BLT
- [28]: CAP_DISP
- [29]: CAP_MSAA
- [30]: CAP_TASK_SHADER
- [31]: CAP_MESH_SHADER

詳細パイプライン機能（サブグループ、タイリング等）は EXT_CAP_FEATURE（0x084）で読み取ります。

### H03: FMT の位置と EXTCAP_PTR の修正

- FMT レジスタの位置: EXTCAP リスト内では 0x140（FMT_LIMITS は 0x150）、Slim MMIO 内の直接ショートカットとしては 0x080（EXT_CAP_FMT0）/ 0x084（EXT_CAP_FEATURE） の両方でアクセス可能です。
- RTL の修正: atg_cp_regs.v において Slim MMIO モードでも EXTCAP_PTR（0x00C）が 0x100 を返し、0x100〜0x3FF の拡張 Capability（MEM, FMT, DISP2）を正常にリードできるよう RTL を修正しました。

### H04: Session 切り替えの確定

- 確定仕様: CMD_BIND_SESSION（Opcode 0x14, 2 dw） に一本化します。
- Session 0: カーネルドライバ自身（Root コンテキスト）専用です。ブート時およびリセット時の初期値は 0 であり、フラット物理アドレスへの直接アクセス権を持ちます。

### H05: ページテーブルメモリと CP_HEAP の割り当て

- 割り当て主体:
  - ホストは CMD_SESSION_OPEN で 4 KiB 境界の ルート Page Directory（Level 1）を確保して渡します。
  - Level 2 ページテーブル（1024 エントリ = 4 KiB）は、CP ファームウェアが CMD_VM_MAP 時に CP_HEAP から自律的に割り当て・解放 します。
- CP_HEAP_BASE / LIMIT（0x0B0 / 0x0B4）:
  - 32-bit の GPU 物理 DRAM アドレス（ローカル VRAM または UMA カーブアウト）。
  - 4 KiB 境界揃え、推奨サイズは最小 2 MiB（最大 512 テーブル = 2 GiB 分の VA 空間をカバー）。
- 設定タイミング: 初期化手順の Step 4.5（RING_BASE 設定直後、CTRL.RUN = 1 を書き込む前の IDLE 状態）で設定します。

### H06: セッション数と Capability 依存関係

- セッション数の取得: EXT_CAP_FEATURE[7:4] または DEVICE_CAPS2 から読み取り（KC705 実装は 16 セッション）。
- 依存関係: セッション管理コマンド（CMD_SESSION_*, CMD_VM_*, CMD_BIND_SESSION）は、CAP_HW_WALKER（bit 2）または CAP_PROT（bit 24） が有効な場合にサポートされます。両方 0 の場合は単一コンテキスト（Session 0）固定となります。

### H07: 64-bit ホストバスアドレスと 32-bit PTE

- IOMMU 有効時（CAP_IOMMU = 1）: OS 側 IOMMU（VT-d / AMD-Vi）が 64-bit ホスト物理アドレスを 32-bit IOVA（< 4 GiB）に変換するため、4 GiB 以上のホストメモリも渡せます。
- IOMMU 無効時（CAP_IOMMU = 0）: ドライバはリングバッファやステージングメモリを 4 GiB 未満のホスト物理メモリ（Linux の GFP_DMA32 等）から確保 してください。

### H08: ローカル VRAM とホストバスアドレスの区別（VM_MAP）

- 判別方法: CMD_VM_MAP のフラグに GHCI_VM_FLAG_LOCAL（bit 5 / 0x20） を追加しました。
  - 0: ホスト PCIe バスアドレス / IOVA（HOST_LIMIT 未満、AXI ID 1 でアクセス）
  - 1: ローカル VRAM アドレス（LOCAL_BASE + offset、AXI ID 0 で MIG DDR3 に直接アクセス）
- HOST_LIMIT: メモリコントローラがホスト PCIe ブリッジ側へルーティングする物理アドレスの上限境界です。

### H09: FAULT_MMU_OOM の Kind 番号とヘッダ不正の区別

- FAULT_KIND = 8: FAULT_MMU_OOM（CP_HEAP の容量枯渇による Level 2 ページテーブル確保失敗）として正式定義しました。
- シェーダヘッダ不正の区別:
  - FAULT_KIND = 1 + SOURCE_BLOCK = 0 (CP): リングコマンドの Opcode 不正
  - FAULT_KIND = 1 + SOURCE_BLOCK = 4 (SU) または SYNDROME = 0x0001: GSI1 シェーダプログラムヘッダ不正

### H10: 単一 GHCI 1.0 における KICK とアパーチャ

- KICK（0xF00）: GHCI 1.0 では完全廃止（リングバッファによる一本化）。
- FAULT_KIND = 2: 不在ユニットに対する描画・ディスパッチ要求時に発行。
- FAULT_KIND = 7: FAULT_KIND_LOCKED_REG（RUNNING 状態における RING_BASE や CP_HEAP 等の保護レジスタへの不正書き込み）として定義。

### H11: CAP_PROT によるセッション単体破棄時の通知

- 通知: IRQ_STATUS.IRQ_FAULT が立ち、STATUS[9]（FAULTED）がセットされますが、STATUS[19:16]（CP_STATE）は S_RUN（1）を維持 します（全体停止しません）。
- 特定と回復: ドライバは FAULT_INFO[31:24] で落ちた SESSION_ID を特定し、CMD_SESSION_CLOSE を発行してリソースを解放、IRQ_STATUS を W1C でクリアすることで、他のセッションを止めることなく安全に処理を継続できます。

### H12: WDT_CYCLES のオフセットと STATUS.mem_ready

- レジスタ配置:
  - 0x040: DIAG_CP_PC（RO）/ IRQ_SET（WO）
  - 0x044: WDT_CYCLES（RW）
  - 0x048: SCRATCH（RW）
  - 0x04C: RESIDENT_WARPS（RO）/ DIAG_CP_STATUS
- STATUS.mem_ready: RTL（atg_cp_ctl.v）の実装通り、厳密に STATUS[12] です。

### H13: VEND (ID 4) の配置と GCLK_HZ

- VEND Capability（0x180）:
  - +0x0: VENDOR_ID（0x414D4154 "AMAT"）
  - +0x4: DEVICE_ID（0x41544731 "ATG1"）
  - +0x8: SUBSYSTEM_VENDOR_ID
  - +0xC: SUBSYSTEM_DEVICE_ID
  - +0x10: GCLK_HZ（コアクロック周波数。KC705 では 100000000 = 100 MHz）
- タイムスタンプ周期: timestampPeriod = 1.0e9 / GCLK_HZ ns（100 MHz 時は 10.0 ns）。

### H14: Vulkan Limits の標準プロファイル値

docs/ghci.md §8.6 に VkPhysicalDeviceLimits の完全な標準値を表として新設しました：

- maxVertexInputAttributes: 16
- maxVertexInputBindings: 16
- maxVertexInputAttributeOffset: 65,535
- maxPerStageDescriptorSamplers / UniformBuffers / SampledImages: 16
- maxBoundDescriptorSets: 4
- maxUniformBufferRange: 65,536 bytes
- maxStorageBufferRange: 2,147,483,648 bytes (2 GiB)
- maxPushConstantsSize: 128 bytes (32 dwords)
- maxComputeWorkGroupInvocations: 256 (8 warps × 32 lanes)
- maxComputeSharedMemorySize: 4,096 bytes (LDS)

### H15: Buffer Descriptor・(set, binding)・Push Constants

- GhciBufferDescriptor（16 bytes）:
  - 0x00: base_address[63:0]（GPU VA）
  - 0x08: size_bytes[31:0]
  - 0x0C: stride_bytes[15:0]
  - 0x0E: format[7:0]（Texel buffer 用 VkFormat）
  - 0x0F: flags[7:0]（[0] ReadOnly, [1] Coherent）
- テーブルインデックス計算: Descriptor VA = table_va + (binding × 16 または 32)
- Push Constants: CMD_LOAD_CONST（Opcode 0x2D） を使用。最大 32 dword（128 bytes）をオンチップ定数バンク c[0..511] に 1 サイクルでロード。

### H16: Attachment 指定・Color 数・Render Pass 終了

- 指定方法: CMD_BEGIN_RENDER_PASS（Opcode 0x2B, 10 dw） でカラー/デプスのアタッチメント、Clear 値、LoadOp を指定。
- 終了コマンド: CMD_END_RENDER_PASS（Opcode 0x2C, 1 dw） で StoreOp（MSAA リゾルブ、L2 へのフラッシュ）を実行。
- Color Attachment 数: FMT_LIMITS[3:0] でハードウェア合成値を動的取得（KC705 デフォルトは 4、MRT8 構成時は 8）。

### H17: Blitter の Capability と転送アドレス空間

- Capability フラグ: DEVICE_CAPS[27] = CAP_BLT。
- 転送アドレス空間: アクティブな Session の GPU VA。ホストステージングバッファも CMD_VM_MAP でセッション VA にマップされるため、統一的な VA 間コピーとして実行されます。

### H18: 128-byte PSO の新配置とブレンド係数

- 属性設定の拡張:
  - attr_formats[0..15]: 1 属性あたり 8-bit VkFormat を直接格納（16 バイト）。
  - attr_bindings[0..15]: 属性ごとのバッファバインディング番号（16 バイト）。
  - attr_offsets[0..15]: 1 属性あたり 16-bit オフセット（0〜65535、32 バイト）。
- ブレンド係数: include/ghci_cmd.h の定数を VkBlendFactor（0〜18）と完全一致 するよう修正しました。

### H19: CMD_DRAW_INDIRECT の Stride と Max Count

- CMD_DRAW_INDIRECT（6 dw）:
  - dword 1..2: args_buffer_va[63:0]
  - dword 3..4: draw_count_va[63:0]（または直接 maxDrawCount）
  - dword 5: [15:0] stride_bytes, [16] is_indexed, [17] count_from_buffer

### H20: UMA での表示と IRQ_DISPLAY の関係

- UMA での表示: FB_BASE は共有 DRAM の物理アドレスを直接指定。Display Controller は AXI DMA で直接スキャンアウトします。
- IRQ の関係: IRQ_STATUS[3]（IRQ_DISPLAY）は、[9] VBLANK, [10] FLIP, [11] DISP_ERROR, [12] HOTPLUG の論理和（OR） です。簡易ハンドラは bit 3 のみ、詳細ハンドラは bit 9〜12 を判定できます。

### H21: ホストメモリの COHERENT 指定と PCIe Snoop

- 結論: ホストメモリを CMD_VM_MAP する際は 必ず GHCI_VM_FLAG_COHERENT（0x08）を指定してください。
- PCIe Snoop: GPU の PCIe ブリッジは No Snoop 属性をクリア（No Snoop = 0）して送信し、ホスト側 PCIe Root Complex のキャッシュスヌープを強制します。

### H22: 64-bit Timeline Fence のレイアウト

- Capability: GHCI 1.0 では 全 GPU 必須（Mandatory）。
- CMD_FENCE（Opcode 0x33, 5 dw）:
  - dword 1..2: fence_addr[63:0]（8 バイト揃え）
  - dword 3: fence_seq_lo[31:0]
  - dword 4: fence_seq_hi[31:0] ホストメモリへ 64-bit 値がアトミックに書き戻されます。

### H23: 全フォーマットと Vulkan Format Feature 対応表

docs/ghci.md §8.5.1 に、ETC2/EAC（10 種類）、BC1〜BC7（7 種類）、UNORM/SNORM/SRGB/PACK16、FP16/FP32、UINT/SINT、Depth/Stencil の 全 47 フォーマットに対する Vulkan Feature 対応一覧表（Sampled, Linear Filter, Color Attachment, Blend, Depth/Stencil, Vertex Buffer, Storage Image） を完全網羅いたしました。
