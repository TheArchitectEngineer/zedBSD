<!-- awesome-plan project=zedbsd record=ws306-design -->

# WS306 設計: GPU の「ゲーム最適化」の UAPI と driver の識別（p001、第 1 版）

Parent: [WS306](ws.md)。作成 2026-10-29 P5（q1666、設計だけ）。照合した tree: main 61630bb0b。
ユーザーの決定の原文は ws.md の「ユーザーの決定」。ここでの「推し」はユーザーが決めていない点（§9 の問い）。

## 第 2 版の改訂（2026-10-29 P5、[design-review.md](design-review.md) の blocker 1・major 8・minor 13 を反映。この節が下の本文に優先する）

| # | 改訂 | review |
| --- | --- | --- |
| X1 | **option を `ngg` と `ngg_cull` に分ける**。ゲーム最適化が ON の session は option の bit だけで決め（`ngg` off → 従来の vertex、`ngg` on・`ngg_cull` off → passthrough の NGG、両方 on → culling の NGG）、OFF の session は boot の行 `amdrdna.vk.ngg`（0/1/2、`vk-node.c:149`）で決める。boot の行が 0 でも 2 でも ON の session の option が名前どおりに効く。ユーザーの原文「このゲーム最適化がかかっているときだけ、NGGをmesh shader以外でも有効にする」は NGG そのものを指す | B1 |
| X2 | **専用の latch**: `native_submitted` の意味を借りない。session に `game_frozen` を持ち、最初の GPU に届く ioctl（`GPU_COMMAND`・`GPU_COMMAND_SUBMIT`・`SUBMIT_SYNC`・`GPU_JOB_COMMIT` など、admission の外の経路 gpu.c 2016・2071 を含む）が admission の lock の下で 1 回だけ `game_apply` を呼んで凍らせる。以後の `GPU_GAME_SET` は EBUSY。backend の compile（`vk-pipeline.c:691`）と executor（`vk-execute.c:610`）は凍った値だけを読む | M1 |
| X3 | **名前と版は libvulkan で上書きしない**: native の driver では kernel の executor（`vk-device.c`）が既に deviceName・driverVersion を返す。各 driver が 1 つの識別（`game_identity`）を持ち、executor も `GPU_GAME_INFO` もそれを使う。venus は host の値のまま（kernel の venus は host の名前を知らない）。driverVersion の数の読み方は vendor ごとに違うので、vendorID を変えずに zedBSD の版の数を入れない（`driver_version` の文字列と `driver_version_number` は zedBSD の driver の版で、Vulkan の driverVersion には写さない） | M2 |
| X4 | **ゲームの手順と関数**: libvulkan の zedBSD の関数を 2 つ: 一覧（名前・既定・説明）と識別（GPU の名前・driver の名前・版の文字列・今効いている option）。関数は instance と physical device の上で動き、ゲームは「探りの instance → 読む → 消す → 環境変数を設定 → 本番の instance」の 2 段で選ぶ（試験に入れる）。名前は `vk` で始めない（Khronos の名前空間を避ける）ので、libvulkan の `exports.map`（`global: vk*; local: *;`）に名前を足す | M3 |
| X5 | 安全の規則に足す: session をまたいで共有する画像・buffer（export・import の allocation、scanout）の中身の形を変える option は作らない（例に書いた `fast_clear` は消す。共有の画像の圧縮の形が相手の session と食い違う） | M4 |
| X6 | **WS291 の要素と OFF の時の速さ**: WS291 が culling を既定にした後では「OFF＝今と同じ」と「OFF の時は最適化なし」が両立しない。基準の案: 正しさに影響しうる・ゲームの不具合を起こしうる最適化は option（既定 ON）、desktop を含む全員に安全な物は常に入れる。→ Q306-6 | M5 |
| X7 | 版の規則: `driver_version` は driver の挙動（option の意味・既定）が変わるたびに上げる。libvulkan の版は環境変数の解読の版として別に出す（識別の関数に入れる） | M6 |
| X8 | p007（文書）は WS009 の ws009-p009（`docs/reference/gpu.md`）の完了に依存。file の所有は Q1 が調整 | M7 |
| X9 | 試験に足す: SET と投入の同時（X2 の latch）、boot の行と option の組の表（X1、0/1/2 × ON/OFF × option）、venus の名前・版が変わらない回帰、fork・dup の後の session、display の session に効かない、新しい struct の `_Static_assert`（info 160・option 176・set 40 byte、review が x86_64 と -m32 で確かめた）、解読の負の例、OFF の時の速さの回帰（WS291 の測りの script） | M8 |
| X10 | minor: ops の新しい欄は struct の最後、登録の検査（gpu.c の validate）に足す。`GPU_CAP_GAME` は render の node だけ。SET は writable を求める（推しの判断を変える）。文法に driver ごとの区切りの記号（`:`）を予約。環境変数の値は `0`・`1` 以外を OFF にし stderr に 1 行。`VK_KHR_driver_properties` は WS226（Vulkan 1.2 以上）の後。p003 の試験は p004 に依存。vulkaninfo は 8 LW の前に下見の段（soname・C++17・header の版）。amdrdna の製品名は表が無いので出典と license を決める段が要る。pipeline cache を出す時は効いている option を key に入れる。ws.md の「process ごと」は「open ごと」と読む | minor |

### 第 2 版の Phase の表（§11 を置き換える。LW は ±50%）

| Phase | 内容 | 依存 | LW |
| --- | --- | --- | --- |
| p002 | UAPI の header、核（ioctl・latch・解読・ops）、host 試験 | p001 | 11 |
| p003 | libvulkan（変数・SET・2 つの関数・exports.map） | p002、p004 | 6 |
| p004 | 全 driver の識別（`game_identity`、executor も使う、amdrdna の製品名の出典） | p002 | 6 |
| p005 | amdrdna の option（`ngg`・`ngg_cull`、WS291 の要素の移し替え）、実機の測り | p002、WS291 の RDNA の完了、Q306-6 | 10（WS291 の後に見積もり直す） |
| p006 | vulkaninfo の下見と移植 | p003 | 10 |
| p007 | 文書 | p003、ws009-p009 | 2 |
| p008 | 規約の全文の見直し | 全部 | 3 |

計 48 LW。

### 第 2 版のユーザーに聞く点（Q1 へ）

- Q306-6 WS291 の最適化の扱い: 推し「正しさやゲームの互換に影響しうる物は option（ゲーム最適化が ON の時だけ、既定 ON の option）、desktop を含む全員に安全な物は常に入れる。OFF の session は boot の行の既定のまま」。選択肢: (b) WS291 の要素を全部 option にし、OFF の session は最適化なし（desktop が遅くなりうる）。

## 0. 要点

- `/dev/gpuN` の open（session）ごとに、ゲーム最適化の ON/OFF と option の組を持つ。option の表は driver が持ち、文字列の解読と検査は共通の層（`gpu.c`）が一様に行う。
- 新しい ioctl 3 つ（'G' 46〜48）と capability の bit 1 つ。`GPU_ABI_VERSION`（1）と既存の struct は変えない（追加だけ）。
- libvulkan が環境変数を読み、render の context を開いた直後（最初の投入の前）に UAPI で渡す。Vulkan の独自の拡張は作らない（ユーザーの決定）。
- GPU の名前と driver の版は UAPI で取れ、libvulkan は `VkPhysicalDeviceProperties.deviceName`・`driverVersion` に写す（`VkPhysicalDeviceDriverProperties` は WS226 の Vulkan 1.2 以上の後）。
- option の一覧は vulkaninfo（Vulkan-Tools、package に移植）に出す。vulkaninfo は Vulkan の API しか読まないので、libvulkan の zedBSD の C の関数（Vulkan の拡張ではない、`vkGetInstanceProcAddr` に名前を出さない）を patch で呼ぶ（推し、Q306-4）。
- 最初の option は amdrdna の NGG の culling（今は boot の行 `amdrdna.vk.ngg` で node ごと、`vk-node.c:149`）を session ごとにする物。WS291 で入れる他の最適化もこの表に移す（ws.md の順の決定: WS291 の後）。

## 1. 今の code の事実

| 物 | 事実 |
| --- | --- |
| session | `struct gpu_session`（`gpu.c:219-262`）は open ごと。`native_submitted`（`:228`）が「GPU に stream が届いた」の印 |
| info | `GPU_GET_INFO`（`include/uapi/gpu.h`）の `struct gpu_info` は `driver_name[32]` だけ（GPU の名前・版の欄は無い）。driver が埋める: `nvrtx/node.c:502-503`「nvrtx」、`amdrdna/node.c:419-420`「amdrdna」、`bcm2711/render-device.c:343`「bcm2711-v3d42」、`ghci/ghci.c:827` |
| capability の bit | 使用は 1〜65536（`gpu.h`、`GPU_CAP_HOST_BLOB 65536U` が最大）、`gpu-allocation.h` 1024、`gpu-fence.h` 2048、`gpu-display.h` 128・4096・32768、`gpu-job.h` 8192・16384。次の空きは 131072 |
| ioctl の番号 | 'G' 0〜45 を使用（抜け無し、2026-10-29 に数えた）。46 から空き |
| libvulkan の open | `vulkan_context_open(context, path)`（`userland/desktop/libvulkan/context.c` 63 行付近）が `open(path, O_RDWR)`（`:104`）の後 `GPU_GET_INFO`（`:115`）。display の session（`wsi-display.c:1738`）は別の open |
| libvulkan の環境変数の名前 | `VULKAN_ZEDBSD_SUBMIT_TIMING`（`sync.c:27`）、`VULKAN_ZEDBSD_CURSOR_LOG`（`wsi-swapchain.c:43`）、`VULKAN_ZEDBSD_OVERLAY_LOG`（`wsi-display.c:795`）。どれも debug の物 |
| Vulkan の版 | libvulkan は `apiVersion` を 1.0 にする（`instance.c:1072`）。`VkPhysicalDeviceDriverProperties` は出していない |
| amdrdna の NGG | `amdrdna_vk_geometry_mode(owner)`（`vk-node.c:439-449`）は session を受け取り、node の `vk_geometry`（boot の行 `amdrdna.vk.ngg`、`vk-node.c:149`）を返す。`AMDRDNA_VK_GEOMETRY_CULLING 2U`（`vk/amdrdna-vk.h:200-203`）。session ごとにする口は既にある |
| vulkaninfo | tree に無い（ws.md の 2026-10-27 の確かめ） |

## 2. 責務と境界

- 共通の層（`gpu.c`）: ioctl の検査、option の文字列の解読（文法・長さ・名前の照合）、session の状態（enabled・mask）、投入の後の変更の拒否。
- driver: option の表（名前・説明・既定・印）を静的に持ち、session の mask を自分の判断（pipeline の compile・状態の選び方）に使う。文字列は見ない。
- libvulkan: 環境変数の読み、`GPU_GAME_SET` の呼び、名前と版の Vulkan への写し、一覧の C の関数。
- vulkaninfo（package）: 一覧の表示だけ。
- 範囲の外: Vulkan の拡張・layer、process の名前で driver が自動で profile を当てる仕組み（Windows の driver の app profile に当たる物。将来の案として §8）。

## 3. UAPI（`include/uapi/gpu-game.h`、新しい header。追加だけ）

```c
#define GPU_CAP_GAME			131072U

#define GPU_GAME_NAME_MAX		32U	/* NUL を含む */
#define GPU_GAME_DESCRIPTION_MAX	128U
#define GPU_GAME_OPTIONS_MAX		64U	/* driver の表の上限（mask が 64 bit） */
#define GPU_GAME_STRING_MAX		512U	/* option の文字列の上限（NUL を含まない） */

/* gpu_game_info.flags（出力） */
#define GPU_GAME_ENABLED		1U	/* この session でゲーム最適化が ON */
#define GPU_GAME_LOCKED			2U	/* 最初の投入の後: GPU_GAME_SET は EBUSY */

/* gpu_game_option.flags（出力） */
#define GPU_GAME_OPTION_DEFAULT		1U	/* ON の時に文字列が触れなければ有効 */

#define GPU_GAME_INFO	_IOWR('G', 46, struct gpu_game_info)
#define GPU_GAME_OPTION	_IOWR('G', 47, struct gpu_game_option)
#define GPU_GAME_SET	_IOWR('G', 48, struct gpu_game_set)

struct gpu_game_info {			/* 入力は version と size だけ */
	uint32_t version;
	uint32_t size;
	uint32_t flags;
	uint32_t option_count;
	uint64_t active;			/* 今効いている option の bit（index の順） */
	char gpu_name[64];			/* 例 "NVIDIA GeForce RTX 2070"、"AMD Radeon RX 6600" */
	char driver_name[32];			/* gpu_info.driver_name と同じ */
	char driver_version[32];		/* driver が決める版の文字列 */
	uint32_t driver_version_number;		/* Vulkan の driverVersion に写す数（VK_MAKE_VERSION の形） */
	uint32_t reserved;
};

struct gpu_game_option {		/* 入力は version・size・index */
	uint32_t version;
	uint32_t size;
	uint32_t index;
	uint32_t flags;
	char name[GPU_GAME_NAME_MAX];
	char description[GPU_GAME_DESCRIPTION_MAX];
};

struct gpu_game_set {			/* 入力は version・size・enable・bytes・address */
	uint32_t version;
	uint32_t size;
	uint32_t enable;			/* 0 OFF、1 ON */
	uint32_t bytes;				/* 文字列の長さ（0 は既定のまま） */
	uint64_t address;			/* user の文字列（NUL は要らない） */
	uint64_t active;			/* 出力: 効いた bit */
	uint32_t unknown;			/* 出力: 表に無い名前の数 */
	uint32_t reserved;
};
```

- 権限: 自分の session だけを変えるので、open できる者（root か node の owner、`gpu.c:1390-1406`）なら誰でも。write の権限（open の mode）は要らない（ゲーム最適化は資源を作らない）。推しの判断。
- 時: `native_submitted` が立った後（`gpu.c:228`）の `GPU_GAME_SET` は EBUSY。pipeline の compile は投入の前に起きる driver があるので、driver は compile の時に mask を読み、ON/OFF の途中の変更を見ない（LOCKED の後は変わらない）。
- OFF の時（`enable = 0`）は文字列を見ない（bytes が 0 でなくても EINVAL にしない、active は 0）。
- `GPU_CAP_GAME` を出さない driver の node では 3 つとも EOPNOTSUPP（`gpu.c:2199-2201` の知らない request と、capability の無い ioctl（`gpu.c:2336` など）の今の扱いに揃える）。
- 名前と版は全 driver が埋める（option の無い driver も `GPU_GAME_INFO` は答え、`option_count = 0`）。だから `GPU_CAP_GAME` は「名前と版が取れる」の意味も持つ。

## 4. option の文字列の文法（推し、Q306-2）

```
string := token ( "," token )*
token  := [ "+" | "-" ] name
name   := [a-z] [a-z0-9_]{0,30}
```

- `+name`（または `name`）で有効、`-name` で無効。書かれない option は既定（`GPU_GAME_OPTION_DEFAULT`）のまま。同じ名前が 2 度出れば後が勝つ。空白は許さない。
- 文法の誤り・長さの超過は EINVAL（session は変わらない）。**表に無い名前は無視して数える**（`unknown`）。理由: 1 つの環境変数が全 GPU に渡るので、他の driver の option の名前を含むことが普通にある。ゲームは一覧（§5）で確かめられる。
- 例: `VULKAN_ZEDBSD_GAME=1 VULKAN_ZEDBSD_GAME_OPTIONS=-ngg_cull,+fast_clear`。
- 手本の慣習（未確認・実装の段で出典を確かめる）: Mesa の RADV の `RADV_PERFTEST`・`RADV_DEBUG` は「,」区切りの名前の並び。DXVK の `DXVK_CONFIG` は「;」区切りの `key = value`。名前の並びの形は RADV に近く、値を持たない ON/OFF だけなので `key=value` の形は採らない。

## 5. libvulkan

- 環境変数（推し、Q306-1）: `VULKAN_ZEDBSD_GAME`（`1`・`0`、無い時は OFF）と `VULKAN_ZEDBSD_GAME_OPTIONS`（§4 の文字列）。既存の libvulkan の変数と同じ接頭辞（§1）。EGL・GL（WS068）は libvulkan の上なので同じ変数が効く。
- `vulkan_context_open` の `GPU_GET_INFO` の後、`GPU_CAP_GAME` があれば `GPU_GAME_SET`。EINVAL（文法の誤り）なら OFF のまま続け、stderr に 1 行（`libvulkan: VULKAN_ZEDBSD_GAME_OPTIONS ignored: ...`）。unknown は黙る（§4 の理由）。
- display の session（`wsi-display.c`）には渡さない（render の context だけ）。
- 名前と版: `GPU_GAME_INFO` の `gpu_name` を `deviceName` に、`driver_version_number` を `driverVersion` に写す（今の値は Venus の host の答えか driver の capset。どちらを優先するかは実装の段で `instance.c:1060-1075` を読んで決める、Venus（QEMU）では host の値を保つ）。
- 一覧の C の関数（推し、Q306-4）: `<vulkan/vulkan_zedbsd.h>`（libvulkan の install する header）に
  `VkResult vkZedbsdGetGameOptionsEXP(VkPhysicalDevice, uint32_t *count, struct zedbsd_game_option *options)` 型の関数（名前は実装で coding-style に合わせる）。
  Vulkan の拡張ではない（`vkGetInstanceProcAddr` で引けない、`VkExtensionProperties` に出さない）。ゲームと vulkaninfo が直に link か `dlsym` で使う。

## 6. driver

| driver | option（第 1 版） | 名前と版 |
| --- | --- | --- |
| amdrdna | `ngg_cull`（既定 ON）: ON の session の pipeline を `AMDRDNA_VK_GEOMETRY_CULLING` で、OFF は boot の行の値（`vk-node.c:149`）。`amdrdna_vk_geometry_mode(owner)` が session の mask を見る。WS291 で入れる他の要素も名前を付けて足す | 製品名は PCI の id から（amdrdna の表の有無は実装で確かめる） |
| nvrtx | 無し（WS291 の NVIDIA の段の後に足す） | `products.c` の製品名（WS242） |
| i915・powervr・bcm2711・ghci・venus | 無し | 各 driver の今の識別から。venus は host の名前 |

- driver の内部の口（`include/drivers/gpu/gpu.h` の ops、kernel の内部で HAL ではない）: `const struct drv_gpu_game_option *game_options; unsigned game_option_count;`、`int (*game_identity)(void *, struct gpu_game_info *)`、`void (*game_apply)(void *backend_session, unsigned enabled, uint64_t active)`。核が `GPU_GAME_SET` の成功で `game_apply` を呼ぶ。
- **安全の規則**（この設計の制約として phase の受け入れに入れる）: option は自分の session の速さと互換だけを変える。memory の隔離・取り消しと復帰（WS250）・他の session の結果を弱める option を作らない。

## 7. vulkaninfo の移植（package）

- `userland/packages/` に Vulkan-Tools の vulkaninfo を tarball と patch で（AGENTS.md §7: source tree に取り込まない、license を監査。Apache-2.0 の見込みは未確認、移植の段で COPYING を読む）。
- patch: zedBSD の時に §5 の関数を呼び、device ごとに「Game optimization options」の節（名前・既定・説明・今の session で効くか）を出す。
- vulkaninfo が要る header・build の依存（Vulkan-Headers の版、WS225・WS226 との関係）は移植の段の最初に確かめる。合わなければ Q1 に返す。

## 8. 選ばなかった案

- **Vulkan の拡張（`VK_ZEDBSD_game_options`）**: ユーザーの決定（環境変数、独自の拡張を作らない）に反する。
- **`VK_EXT_layer_settings` で渡す**: layer 向けの仕組みで、driver の option の渡し方として標準でない。環境変数の決定にも合わない。
- **文字列を driver に渡して driver が解読**: driver ごとに文法と誤りの扱いが分かれる。共通の層で一様にする。
- **知らない名前を EINVAL**: 1 つの環境変数が複数の GPU に渡るので、片方の driver の option が他方で必ず失敗する。
- **process の名前で自動の profile**: ゲームの側で制御するというユーザーの意図（「互換性をゲーム側が制御」）と逆。将来の案。
- **option を boot の行（`amdrdna.vk.ngg`）のまま**: session ごとに選べない。boot の行は「OFF の時の既定」として残す。

## 9. ユーザーの回答（2026-10-29、クリック、Q1 の中継。原文は ws.md）

Q306-1「VULKAN_ZEDBSD_GAME (推し)」、Q306-2「+name/-name (推し)」、Q306-3「無視して数を返す (推し)」、Q306-4「libvulkan の C の関数 (推し)」、Q306-5 は推しのまま（ユーザーは Other を選ばなかった）。→ 下の問いは全部推しで決まった。本文の「推し」はこの決定として読む。

### 9.1 問い（記録として残す）

- Q306-1 環境変数の名前: 推し `VULKAN_ZEDBSD_GAME`（0/1）と `VULKAN_ZEDBSD_GAME_OPTIONS`。他の案: `ZEDBSD_GAME_OPTIMIZE`・`ZEDBSD_GAME_OPTIONS`（OS の変数として）。
- Q306-2 文字列の書式: 推し「,」区切りの `+name`／`-name`（§4）。他の案: `name=0|1`。
- Q306-3 知らない option: 推し「無視して数える」（文法の誤りだけ拒否）。他の案: 拒否。
- Q306-4 一覧の出し方: 推し「libvulkan の zedBSD の C の関数（Vulkan の拡張ではない）を patch した vulkaninfo が呼ぶ」。他の案: base に別の command（例 `gpuinfo`）を作り vulkaninfo は変えない。
- Q306-5 OFF の時の既定: 推し「OFF（今と同じ挙動）。ゲームが変数で ON にする」。他の案: 既定 ON。

## 10. 試験

| 試験 | 形 | 合格 |
| --- | --- | --- |
| 文字列の解読 | host（解読を純粋な関数に切り出す、`plan/ws306/tests/host-game-parse.c`） | 文法の受け・拒否、長さの境、重複、unknown の数、64 の境 |
| 核の ioctl | QEMU（venus の node、option 0 個）と host の模型 | INFO・OPTION・SET、投入の後の EBUSY、capability の無い node |
| libvulkan | QEMU の試験の program（変数あり・なし・誤り） | SET が呼ばれる、誤りで OFF と stderr の 1 行 |
| amdrdna | 実機 10.0.10.3（RDNA、P2 の機械、Q1 の割当）、WS291 の測りの script | `ngg_cull` の ON/OFF で pipeline の形が変わる（driver の log）、desktop の frame-ops と scene の fps を ON/OFF で比べる |
| vulkaninfo | QEMU | option の節が出る、license の監査の記録 |

## 11. Phase と LW（±50%、1 LW ≈ 実時間 15〜25 分）

| Phase | 内容 | 依存 | LW |
| --- | --- | --- | --- |
| p002 | UAPI の header、核（ioctl・session の状態・解読・ops）、host 試験 | p001 | 8 |
| p003 | libvulkan（変数・SET・名前と版の写し・一覧の関数と header）、QEMU の試験 | p002 | 6 |
| p004 | 全 driver の名前と版（`game_identity`） | p002 | 4 |
| p005 | amdrdna の option（`ngg_cull`、WS291 の要素の移し替え）、実機の測り | p002、WS291 の RDNA の完了 | 10 |
| p006 | vulkaninfo の package と patch、license の監査 | p003 | 8 |
| p007 | 文書（DOC-54 の `docs/reference/gpu.md` への追記、WS009） | p003 | 2 |
| p008 | 規約の全文の見直し | p002〜p007 | 3 |

計 41 LW。p002〜p004・p006 は WS291 を待たずに作れるが、ws.md の順の決定（WS291 の後に始める）に従い、始める時は Q1 が決める。

## 12. 危険

- `driverVersion` を driver の値に変えると、Venus（QEMU）で host の版が見えなくなる。venus は host の値を保つ（§5）。
- 環境変数は子の process に継がれる（ゲームが起こす helper にも効く）。session ごとなので害は無いが、文書に書く。
- option の数が 64 を超える driver は mask の形を変える要がある（今の見込みは数個）。
