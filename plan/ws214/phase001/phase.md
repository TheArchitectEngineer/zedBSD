<!-- awesome-plan project=zedbsd record=ws214-p001 -->

# ws214-p001: 調べと設計（HVS の plane の能力と制約、口、compositor の載せ方、fallback、試験）

Parent: [WS214](../ws.md)
Status: planning（設計の第 1 版、2026-10-12 P2。design-reviewer の review の前。コードは書いていない）
Phase disposition: normal
Queue: P2（2026-10-12 Q1 の割り当て）

## 由来

- [WS214 の由来](../ws.md#由来): 2026-10-12 ユーザー「画面更新の分割って、もっと高度化できそうですか？」→ Q1 の案 2（窓を HVS の plane に載せる）に「ぜんぶやりましょう。」。
- 目標（ws.md）: Pi 4 で、窓の drag・dock と undock の拡大縮小・前後の入れ替えの間、GPU が窓の中身を描き直さない。HVS の plane に窓の絵を載せ、位置・大きさ・順だけを変える。
- 関係: [WS141 p014](../../ws141/phase014/phase.md)（cursor の plane、ユーザーの決定「VK_KHR_display＋cursor 専用の UAPI」「vendor の Vulkan 拡張は作らない」）、[WS141 p016](../../ws141/phase016/phase.md)（(B) B-u の libvulkan の fast path・ticket・cursor の lane、段 B-k2）、[WS213](../../ws213/ws.md)（M・R・C・I の thread）、[WS211](../../ws211/ws.md)（p006 drag の案 4 がこの WS、p007 dock・undock の絵、p008 damage）。

## 0. 結論（要約）

1. **載せる物**: 常時ではなく、**操作の間だけ**（drag、dock・undock・pull back のアニメ）、操作中の窓 1 枚を「窓の絵」（影・title bar・body・panel・sub-surface を焼いた premultiplied の画像）にして overlay の plane に載せる。primary（今の合成の frame）はその窓を除いて描き、操作の間は変えない。system bar は窓の上に要るので、もう 1 枚の overlay（上の帯）に載せる。
2. **口**: compositor は今どおり標準の Vulkan だけ（guardrail）。libvulkan が **VK_KHR_display の overlay の plane** を列挙し、位置と拡大縮小は `VkDisplayPresentInfoKHR` の `srcRect`・`dstRect`、順は `planeStackIndex`。libvulkan は同じ display の複数の swapchain の 1 回の `vkQueuePresentKHR` を kernel の **1 回の commit（1 つの display list）** にする。kernel には新しい UAPI（overlay の plane の commit と能力の問い合わせ）が要る → **ユーザーの承認が要る**（差分の案は 4.2、適用しない）。HAL は変えない。
3. **kernel（bcm2711）**: display list を「状態（primary・overlay・cursor）から毎回作る」形に一般化する（今の cursor の `publish_cursor_locked` の一般化）。unscaled と scaled（PPF・TPZ、LBM の割り当て）の plane、slot ごとの buffer の hold、帯域と core clock の予算、console の plane を opaque な全画面の frame の下では省く。
4. **compositor**: 移動は C（cursor の thread）が pointer の箱から窓の plane と cursor を**同じ present** で動かす（窓と cursor が同じ refresh で動く）。焼きと切り替え（primary の描き直し）は R。条件を外れたら今どおりの合成（fallback）。i915・Venus・Linux・FreeBSD は overlay の plane が無いので今と同じ。
5. **見た目の妥協（ユーザーの判断が要る）**: 操作の間、窓の glass（title bar・半透明の body・panel）の blur は**始めの場所の壁紙のまま**（p007 と同じ種類）、窓の中身は止まる（drag の間の client の更新を drop まで止める）。drop で正しい blur・中身に戻る（継ぎ目）。HVS には拡大だけをさせ、undock は終わりの大きさの絵を拡大で動かす（始めが少し柔らかい、1080p の帯域のため）。

## 1. 調べ: bcm2711 の HVS（HVS5）の plane

出典: Linux 6.19 の vc4（`drivers/gpu/drm/vc4/vc4_plane.c`・`vc4_hvs.c`・`vc4_kms.c`・`vc4_regs.h`、`plan/ws141/temp/linux`、GPL のため理解だけに使い、文書と code に写さない＝WS141 のライセンスの規則）、zedBSD の `src/drivers/gpu/bcm2711/display-*.c`（main bf3dbac6d）、Vulkan の公開の reference page（`VkDisplayPresentInfoKHR`）。Pi 実機での確かめはしていない（読むだけの SSH も今回は使っていない）。

### 1.1 plane の数と display list

- HVS には固定の数の plane の register は無い。plane は display list（SRAM、HVS の register の窓の後半 16 KiB＝4096 word、zedBSD は `0x4000` から）の中の「plane の word の列」で、数の上限は SRAM の大きさと、1 line の間に HVS が処理できる量（core clock）と、下の帯域で決まる。Linux は任意に overlay を 16 枚＋primary＋cursor として見せている（`VC4_NUM_OVERLAY_PLANES`、「16k の SRAM と 1 plane の最小 28 byte が限り」の注記）。
- list は plane の列と終わりの word。順は list の中の順（後ろほど上）。channel（3 本）ごとに「次の list」の register（channel 0 は `0x20`）に先頭の位置を書くと、HVS は次の frame の始めにそれを「今の list」（`0x30`）にする。plane の word の一部（context、`0xc0c0c0c0`）は HVS が走査中に書くので、動いている list を書き換えずに新しい list を空いた場所に作る（zedBSD の今の方式。Linux の async update は動いている list の位置と pointer の 3 word だけを直に書き換えるが、走査の途中に当たると 1 frame の中で上下に食い違う）。
- zedBSD の今の SRAM の使い方: 0〜31 firmware、32〜42 拡大縮小の filter（Mitchell-Netravali、R0 で書いて常駐）、43〜51 console の list、64・80 primary だけの list の 2 slot、128・160 合成の list の 2 slot（各 32 word: console 8＋frame 8＋cursor 8＋終わり 1）。

### 1.2 format・tiling

- RGB の 32 bit（zedBSD は BGRA8888・RGBA8888 の 2 つ、`order` の 2・3）、linear（raster）。HVS5 は VC4 の T-format と SAND（YUV）の tiling も読むが、V3D 4.2 の UIF は読めない。V3D が描く絵を plane に出すなら linear で描く（今の出力の swapchain と同じ）。[WS141 p015](../../ws141/phase015/phase.md)（GPU が書いて GPU が読む画像を UIF に）は、plane に出す画像を linear のままにする必要がある（この WS の制約として p015 に伝える、Q1）。
- 1 行の pitch は 65535 byte まで、幅・高さは 4096 まで（今の driver の検査）。pointer は byte 単位、zedBSD は 4 byte の整列を求めている。

### 1.3 拡大縮小

- plane ごとに独立に拡大縮小できる（Intel のような数の限られた scaler ではない）。縦横それぞれ、同じ大きさなら無し、縮小が 2/3 より緩いか拡大なら PPF（多相の filter、SRAM の kernel を使う）、2/3 より強い縮小なら TPZ（台形、box に近く画質は落ちる）。倍率の上限・下限は Linux では制限していない（`drm_atomic_helper_check_plane_state` に 1〜INT_MAX）。
- 拡大縮小する plane は list に word が増える: 拡大後の大きさ 1、LBM の位置 1（縦に拡大縮小する時）、PPF・TPZ の係数（軸ごとに 1〜2）、PPF の時は kernel の位置 4。unscaled の 8 word に対して最大で約 18 word。
- 縦の拡大縮小は LBM（line buffer memory）を使う。HVS5 の LBM は 60K word（1 word に 12 bit の成分の 4 pixel）。RGB の PPF で 1 plane「幅（TPZ の横縮小なら拡大後の幅）× 16 byte を 128 に切り上げて 4 で割った word」。幅 2048 で 8192 word、つまり 7 枚分ほど。Linux は plane ごとに LBM を割り当てる。
- HVS の処理量: unscaled は 1 clock に 4 pixel、拡大縮小する plane は 2 pixel（`vc4_plane_calc_load`）。帯域の見積もりでは、縦に縮小する plane は読みが「縮小の比の切り上げ」倍になると見る。

### 1.4 alpha

- plane ごとに per-pixel の alpha（premultiplied か否か）か固定の alpha。HVS5 は control word 2 に 12 bit の plane 全体の alpha と、per-pixel と plane 全体を掛け合わせる MIX の bit がある。zedBSD の今の符号: frame は固定（`0x4000fff0`＝不透明）、cursor は per-pixel premultiplied（`0x2000fff0`）。
- VK_KHR_display では alpha の種類と `globalAlpha` は surface を作る時に決まり、present ごとには変えられない。plane 全体の alpha を動かす（fade）には標準の口が無い。

### 1.5 list の更新と vblank

- 今の driver: 新しい list を空いた slot に書き、「次の list」に載せ、選んだ pixelvalve の vblank の IRQ で「今の list」が一致したら採用（`bcm2711_display_flip_vblank_locked`）。採用までの間は次の publish をしない（pending）。cursor の変更は待たずに publish し、pending の間の変更は採用の IRQ か submitter の終わりで次の list に入れる（coalesce、`publish_cursor_locked`）。
- primary の present（`output_present`→`bcm2711_display_flip_compose`）は採用を最大 100ms 待つ同期で、その間 controller の mutex を持つ。cursor の SET・MOVE も同じ mutex を取る（`output_cursor_move`）ので、primary の present の採用の待ちの間は待たされる。WS141 p016 の段 B-k2（cursor の op をこの mutex から外す）は「おそらく必要」として設計だけ置かれている。
- 採用は「frame の始め」なので、list を publish してから画面に出るまでは次の vblank まで（最大 1 refresh）。

### 1.6 帯域・clock・memory

- 読みの帯域: Linux は全 plane の和を 1.5 GB/s までに制限する（「絶対の上限は 2 GB/s、他の block のために余裕」）。HVS は隠れた plane も読む（重なりで省かない）ので、opaque な全画面の frame の下の console の plane も毎 frame 読まれている。
- 処理量（core clock）: Linux は 250 MHz で 240M clock/s を上限にする。zedBSD は合成の list で「全 plane の pixel×refresh÷4 の 60%」を core clock の下限にし、firmware に頼む（`submit_frame`→`raise_clock`）。core clock は R0 で 500 MHz 以上の上限を持つ。
- 走査が間に合わないと underrun（HVS の IRQ 97 で報告、今の driver は publish の前に underrun を mask し採用で消して有効にする）。underrun の数は今の log に出ていない。
- memory: HVS には MMU が無く、bus address は CPU の物理＋`0xC0000000` の別名で、物理の最初の 1 GiB だけが届く。plane の画像は物理で連続し 1 GiB より下に要る。zedBSD の bcm2711 では Vulkan の native の allocation が全部「連続・1 GiB より下・uncached の Normal」（`vulkan-memory.c` 417 行）なので、swapchain の画像も client の画像もこの条件を満たす。大きな連続の領域を後から取ると断片化で失敗し得る。

### 1.7 今の driver でできている事・足りない事

| 項目 | 今 | 足りない事 |
| --- | --- | --- |
| plane の数 | console・frame・cursor の 3 つ固定（合成の list 32 word） | 状態から任意の数の plane を並べた list（slot の大きさ・数の見直し） |
| 位置 | frame は (0,0) の全画面、cursor は任意（左上の clip は画像の開始をずらす） | overlay の任意の位置と、右下・左上の clip |
| 大きさ | unscaled だけ | scaled（PPF・TPZ の word、LBM の区画、filter は SRAM に常駐済み） |
| alpha | 固定（frame）、per-pixel premultiplied（cursor） | overlay は per-pixel premultiplied（今の cursor と同じ符号で足りる） |
| 更新 | primary は同期（採用を待つ）、cursor は待たない＋coalesce | overlay の位置・画像の変更を待たない＋coalesce、primary と overlay を 1 つの list で（原子的） |
| buffer の hold | slot ごとに frame 1 つ（`frames[]`・`holds[]`）、cursor は 4 枚の kernel の画像に写す | slot ごとに複数の借りた画像（primary＋overlay） |
| 帯域・clock | 2 plane（console＋frame）の見積もり | 全 plane の見積もり、縦の縮小の倍、予算を超える commit の拒否 |
| console | 合成の list の一番下に常に置く | opaque な全画面の frame の下では省く（帯域の 1 全画面分） |
| underrun | 消して有効にするだけ | 数を v3d.perf の flip の report に出す（試験で 0 を確かめる） |
| lock | primary の present は採用の待ちの間 controller の mutex を持つ | overlay の位置の変更はこの待ちに並ばない（B-k2 と同じ問題） |

## 2. 今の経路（この設計に関係する所、source で確かめた事実）

- compositor の出力: `kwl_glass_draw`（shell.c 608 行〜）の順は、壁紙 → desktop の icon と窓（下から、窓ごとに `draw_window`＝影・body・title bar）→ 窓の popup → system bar（上端、高さ `KWL_GLASS_BAR` 44）→ 各種の popup・menu・通知・keyboard → cursor（plane が無ければ）。system bar は窓の上に描かれる。
- 窓（`draw_body_part`・`draw_title_bar`）: 影（`MODE_SHADOW`、柔らかさ 22・focused で 30、窓の外へ 2×soft）、半透明の body の下の glass（`window_opacity` < 1）、client の宣言した glass の panel、sub-surface、client の画像（角の丸め `GLASS_RADIUS`）、title bar（影、glass、文字、button）。glass（`MODE_GLASS`）は通常は blur した壁紙を**画面の座標で** sample する。docked の中央の窓・`set_blur`・arrange の menu・keyboard の blur の時は backdrop（窓の下の場面を描いて blur）を使う。
- drag（WS211 p006）: title の press で `window_raise`（窓は一番上になる）、移動は M が `server->drag` の窓の位置を pointer から更新し、`drag_damage`（前と今の矩形＋`DAMAGE_REACH` 96）を描き直す。1 コマずつ、GPU が 1 refresh を超えると 30fps。Pi で drag は約 30fps。
- dock・undock（WS211 p007）: アニメ 120ms（`DOCK_MS`）、configure はアニメの終わり。最初のコマで desktop の絵（`KWL_LAYER_DESKTOP`、アニメの窓を除く）と窓の絵（`KWL_LAYER_WINDOW`、影を除く、画面と同じ座標で描く）を焼き、以後は 2 quad＋影＋title bar（滑って fade）＋bar（`bar_dock` で pill がずれる）。条件（`kwl_glass_anim_picturable`）を外れたら全部を描く。
- cursor の plane（WS141 p014、WS213 p004 の段 C）: libvulkan は `GPU_DISPLAY_HAS_CURSOR` の出力で plane を 1 つ多く列挙（最後、stack の一番上、64x64、per-pixel premultiplied、負の位置を許す）。compositor（`cursor-plane.c`）は能力で plane を探し、C（`cursor-thread.c`）が受け箱（I が pointer を書く）から present する。primary の claim を借りる（`display_claim_native`）。
- WS213: M（main、scene の状態と walk）、R（`render-thread.c`、出力の acquire・一覧の記録・submit・present、want の種類にコマと「layer の絵」）、C（cursor の present、park で止まる）、I（入力）。M は R・C を park でき、R の Vulkan は job の外では acquire だけ。
- guardrail: compositor は GPU を libvulkan（Vulkan の API と拡張）だけで扱い、GPU の UAPI を ioctl で直に呼ばない。

## 3. 方式（何を、いつ plane に載せるか）

| 案 | 内容 | 判断 |
| --- | --- | --- |
| A 常時 | 全部の窓をそれぞれの plane に。中身が変わった窓だけ GPU で焼き直す | 採らない。帯域（1080p で窓 3〜4 枚で 1.5 GB/s を超える、1.6）、窓ごとの絵の memory（1 枚 5〜10 MB×2）、VK_KHR_display の stack の順が surface で固定（前後の入れ替えで絵を別の plane へ写し直す）、glass が窓の場所に依る（毎回の移動で焼き直し）。 |
| B 操作の間だけ（採る） | 操作中の 1 枚だけを plane に。操作の始めに焼き、終わりに合成へ戻す | drag・dock の間は primary も窓の絵も変わらず GPU は 0。帯域は 1 枚分。 |
| C client の画像を直に | client の buffer をそのまま plane に（装飾は別） | 採らない。角の丸め・opacity・影・panel の glass が HVS で作れない。 |

B の plane の構成（下から）: stack 0 primary（今の frame、操作中の窓と bar を除く）→ stack 1 窓の plane（窓の絵）→ stack 2 上の帯の plane（system bar と、操作中に窓の上に要る物）→ 一番上 cursor。console の plane は opaque な全画面の primary の下では省く。

## 4. 口（kernel・libvulkan・compositor のつなぎ方）

### 4.1 選択肢

| 案 | 判断 |
| --- | --- |
| (a) libvulkan が VK_KHR_display の overlay の plane を列挙し、新しい UAPI の commit で kernel へ（採る） | compositor は標準の Vulkan だけ。位置・拡大縮小は `srcRect`・`dstRect`（`VkDisplayPresentInfoKHR`、「dstRect が srcRect と違えば拡大縮小」「dstRect は visibleRegion の中」）、順は `planeStackIndex`、alpha は `VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR`。cursor の plane（p014）と同じ考え方。 |
| (b) compositor から kernel への private の ioctl | 採らない。guardrail（compositor は libvulkan だけ）に反する。 |
| (c) vendor の Vulkan 拡張 | 採らない。p014 でユーザーが「vendor の Vulkan 拡張は作らない」。 |
| (d) cursor と同じ形（overlay ごとに SET（kernel が写す）・MOVE） | 採らない。5〜10 MB の絵を写すのは重く、primary と同じ refresh に揃わない（継ぎ目で窓が 2 重か消える）。位置だけの変更が待たない点は (a) に取り入れる。 |

- 原子性: 窓を plane に載せる時は「primary から窓が消える」と「plane に窓が出る」を同じ refresh にする必要がある。Vulkan の仕様は、別の swapchain の present を同じ refresh に出す約束を（調べた reference page の範囲では）していない。zedBSD の libvulkan が「同じ display の plane の swapchain を 1 回の `vkQueuePresentKHR` で出したら同じ refresh から見せる」と定め、文書（`userland/desktop/libvulkan/README.md`）に書く。仕様が禁じない実装の振る舞いで、Mesa（Linux・FreeBSD）では overlay の plane を開かないので関係しない。

### 4.2 UAPI の差分の案（`include/uapi/gpu-display.h`、追加だけ。**ユーザーの承認の前は適用しない**）

```c
/* gpu_display_info.flags: planes 1 to plane_count - 1 are overlays (GPU_DISPLAY_COMMIT). */
#define GPU_DISPLAY_HAS_OVERLAYS	2048U
#define GPU_DISPLAY_OVERLAY_MAX		4U

/* gpu_display_plane_state.flags */
#define GPU_DISPLAY_PLANE_SHOWN		1U	/* clear: hidden */
#define GPU_DISPLAY_PLANE_IMAGE		2U	/* handle..format name a new image; clear: keep */

/* gpu_display_commit.flags */
#define GPU_DISPLAY_COMMIT_PRIMARY	1U	/* primary is a present as GPU_DISPLAY_PRESENT */

#define GPU_DISPLAY_COMMIT		_IOWR('G', 44, struct gpu_display_commit)
#define GPU_DISPLAY_OVERLAY_INFO	_IOWR('G', 45, struct gpu_display_overlay_info)

/*
 * One overlay plane's new state.  The image is session storage with
 * per-pixel premultiplied alpha (GPU_PIXEL_BGRA8888 or RGBA8888), borrowed
 * until a later commit replaces it and the list without it is adopted.
 * src is a rectangle of the image, dst one of the output; they may differ
 * in size (scaled) within the plane's limits.
 */
struct gpu_display_plane_state {
	uint32_t plane;
	uint32_t flags;
	uint64_t handle;
	uint64_t offset;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t format;
	int32_t src_x;
	int32_t src_y;
	uint32_t src_width;
	uint32_t src_height;
	int32_t dst_x;
	int32_t dst_y;
	uint32_t dst_width;
	uint32_t dst_height;
	uint64_t reserved;
};

/*
 * Changes the primary plane and overlays of the output this open leases in
 * one display list, shown from one refresh boundary.  Planes not named keep
 * their state.  With COMMIT_PRIMARY it waits for the adoption as
 * GPU_DISPLAY_PRESENT does; without, it does not wait, and a later commit
 * before that boundary replaces it.  sequence (output) counts commits;
 * GPU_DISPLAY_WAIT reports the adopted one, after which images no longer
 * named are free.  ENOSPC: the planes would exceed the output's bandwidth.
 */
struct gpu_display_commit {
	uint32_t version;
	uint32_t size;
	uint64_t lease;
	uint64_t generation;
	uint32_t flags;
	uint32_t count;
	struct gpu_display_present primary;
	struct gpu_display_plane_state planes[GPU_DISPLAY_OVERLAY_MAX];
	uint64_t sequence;
	uint64_t reserved;
};

/* One overlay plane's limits, for vkGetDisplayPlaneCapabilitiesKHR. */
struct gpu_display_overlay_info {
	uint32_t version;
	uint32_t size;
	uint32_t display_id;
	uint32_t plane;
	uint64_t generation;
	uint32_t max_src_width;
	uint32_t max_src_height;
	uint32_t max_dst_width;
	uint32_t max_dst_height;
	uint32_t flags;		/* scaling allowed, downscale limit, ... */
	uint32_t reserved;
};
```

- 形の要点: 配列は struct の中の固定長（user の pointer を持たない）。overlay は primary の lease に付く（cursor と同じ、別の claim は作らない）。`GPU_DISPLAY_PRESENT` は「primary だけの commit」として今と同じ意味で残す（overlay は前の状態のまま）。位置だけの commit は待たない（cursor の MOVE と同じ）。
- 番号（44・45）・flag の bit・errno・`gpu_display_info.plane_count` の意味（今 1、overlay ありで 1＋K）は p003 の前に確かめ直す。kernel の内部の口（`include/drivers/gpu/gpu-display.h` の `struct drv_gpu_display_ops`）に任意の `commit`・`overlay_info` を足す（NULL なら core が EOPNOTSUPP、i915・Venus は NULL）。HAL（`include/hal/hal.h`・`src/hal/`）は変えない。

### 4.3 kernel（bcm2711、`display-flip.c`・`display-device.c`）

1. **状態から list を作る**: flip の状態に「primary（今の frame）・overlay[K]（画像・src・dst・shown）・cursor」を持ち、どの変更も「状態の全部から空いた slot に list を書いて publish、pending の間は coalesce」にする（`publish_cursor_locked` の一般化）。今の `base_words` の写しは不要になる。
2. **slot**: 合成の list の slot を大きくする（例: 256 word × 2、SRAM の 256 から）。1 つの list は最大で console 8＋primary 8＋overlay K×18＋cursor 8＋1。`check_pipeline` の回復の時に許す list の位置も直す。
3. **scaled の plane**: PPF・TPZ の選び方、係数の word、LBM の区画（overlay ごとに固定の区画、幅 2048 まで 8192 word を K 枚分）、filter は SRAM の常駐の物を指す。register の名前と符号は WS141 の規則（値は事実、名前は独自、GPL の文と code を写さない、最後に類似の監査）。
4. **hold**: slot ごとに「その list が名指す借りた画像」の集合（primary＋overlay）。採用で前の slot の集合を解く（`retire_holds` の一般化）。overlay の画像は blob の共有の allocation（primary の BLOB present と同じ）。
5. **帯域と clock**: 全 plane の「src の幅×高さ×4×refresh×縦の縮小の倍」の和が予算（Linux と同じ 1.5 GB/s を初期値、Pi の underrun で直す）を超える commit は ENOSPC で何も変えない（原子的に拒否）。core clock の下限は全 plane の処理量（scaled は 2 pixel/clock）から今の式を一般化。overlay の能力（`max_src` は出力の大きさ）は「primary＋全部の overlay を能力の最大で、縦の縮小なし」で予算に収まるように決める。compositor は縦の縮小を頼まない（5.3）ので、ENOSPC は compositor の誤りの時だけになる。libvulkan はその present 全体を VK_ERROR_OUT_OF_DATE_KHR にし、compositor は plane を閉じて合成に戻る。
6. **console の省略**: primary が opaque（固定の alpha）で出力の全体を覆う時は console の plane を list に入れない。console への復帰（`restore`、fault）は今どおり console だけの list（43）。
7. **lock**: overlay だけの commit は primary の present の採用の待ちの間も controller の mutex を待たない（WS141 p016 B-k2 と同じ変更。cursor・overlay の状態を専用の lock と flip の guard に移す）。B-k2 を先に行うか、この段で一緒に行うかは Q1（kernel は WS141 の source で、P3 の範囲と重なる）。
8. **計数**: v3d.perf=1 の flip の report に overlay の commit の数・coalesce の数・underrun の数を足す。

### 4.4 libvulkan（`userland/desktop/libvulkan/wsi-display*.c`・`wsi-swapchain.c`）

1. `GPU_DISPLAY_HAS_OVERLAYS` の出力では plane を 1＋K＋（cursor）列挙。stack は primary 0、overlay 1〜K、cursor が一番上。`vkGetDisplayPlaneCapabilitiesKHR`（overlay）: supportedAlpha は OPAQUE と PER_PIXEL_PREMULTIPLIED、src・dst の範囲は `GPU_DISPLAY_OVERLAY_INFO`、minDstPosition は (0,0)（`dstRect` は visibleRegion の中、clip は compositor が `srcRect` で行う）。cursor の plane の見分け（最大 64、負の位置）と重ならない。
2. overlay の display surface の swapchain は primary の claim を借りる（cursor と同じ）。画像は primary と同じ native の共有の画像（linear、連続、1 GiB より下）。
3. present: 1 回の `vkQueuePresentKHR` の swapchain のうち同じ display の connection の物を 1 つの `GPU_DISPLAY_COMMIT` にまとめる（primary を含めば COMMIT_PRIMARY で今の PRESENT_SYNC と同じく worker が GPU の完了を待ってから）。`srcRect`・`dstRect` を plane の状態に。
4. **位置だけの fast path**: 画像の中身が前の present から変わっていない（B-u の `content_serial` と同じ判定）overlay だけの present は、GPU と worker を通らず commit の ioctl だけ（cursor の fast path と同じ）。ticket（B-u）を overlay の chain にも使い、後の present が先に出た古い present を DROPPED にする。
5. **acquire**: overlay の画像は commit が名指している間は返さない。commit の `sequence` と `GPU_DISPLAY_WAIT` の採用の sequence で、置き換えた commit が採用された画像を空きにする。
6. swapchain の画像は 2 枚（FIFO）、overlay の surface の minImageCount は 1（2 枚を同時に acquire できる）。位置だけの present は 2 枚を交互に出すので、compositor は 2 枚に同じ絵を入れる（5.1）。代わりに VK_KHR_shared_presentable_image（1 枚を出し続ける）も標準だが、libvulkan に新しく要り、焼き直しで走査中の画像に描くと裂けるので、第 1 版は 2 枚にする。

### 4.5 HAL・UAPI の承認

- HAL の API・責務: 変更なし。
- UAPI（`include/uapi/gpu-display.h`）: 追加が要る（4.2）。p014 で cursor の UAPI をユーザーが承認したのと同じく、**overlay の plane の UAPI の追加にユーザーの承認が要る**（9 の未決 1）。承認の前は p003 以降の実装に入らない。

## 5. compositor（`userland/desktop/wayland/`）

### 5.1 状態（窓 1 枚ごと、M が持つ）

`COMPOSED`（今どおり）→ `BAKING`（R が絵を焼く。画面は今どおり合成）→ `ON_PLANE`（primary は窓を除く、窓は plane、移動は C）→ `RETURNING`（R が窓を含む primary を描く。plane は出たまま）→ `COMPOSED`。どの状態からも条件を外れたら `RETURNING` へ。

- 焼き（R の job、want の種類に「plane の絵」を足す）: 窓の plane の swapchain の画像（出力と同じ大きさ、2 枚）を R が両方 acquire し（overlay の surface の minImageCount は 1、4.4）、1 枚目に窓（影・title bar・body・panel・sub-surface）を**画面と同じ座標で**描き（glass が正しい場所の blur を取る、p007 と同じ）、同じ job で 2 枚目へ窓の矩形を copy する。renderArea は窓の矩形（影を含む）。上の帯の plane の画像にも bar を描く。入るの present は 1 枚目を出し、2 枚目は acquire したまま C に渡す（C の最初の移動がそれを出す）。
- 切り替え（入る）: R が primary を「窓と bar を除いて」描き（damage は窓の矩形＋reach と bar の帯）、[primary、窓の plane（shown、src＝窓の矩形、dst＝同じ）、上の帯（shown）] を **1 回の present** で出す。primary の walk は窓を飛ばす（p007 の `layer_skip` の印を使う）。
- 切り替え（戻る）: M が C を park（C の最後の present が返った後）→ R が primary を窓と bar を含めて描き（damage は窓の今の矩形＋reach と bar）、[primary、窓の plane（hidden）、上の帯（hidden）] を 1 回の present で出す。libvulkan の ticket で C の遅れた present は DROPPED。

### 5.2 drag

- 始め: 移動が始まった時（`server->drag` の窓が初めて動いた pass）に焼きを頼む。焼けるまでの数コマは今の合成の drag で、焼けた所で `ON_PLANE` へ。title の press で焼く案は click だけ（移動しない）の時に GPU の窓 1 回分が無駄になるので採らない（Pi で始めの遅れが目立てば比べる）。
- 移動: M は窓の位置（`surface->x/y`）を今どおり pointer から更新するが、`drag_damage` を出さない。C は受け箱に「窓の plane が pointer に従う: 掴んだ点からのずれ、絵の矩形、出力の矩形、世代」を M から受け取り、pointer の動きごとに [窓の plane（`dstRect`＝pointer − ずれ、出力の端で clip した `srcRect`）、cursor] を 1 回の present で出す（位置だけの fast path）。窓と cursor が同じ refresh で動く（今の合成の drag では窓が cursor に遅れる）。present の間引き（C0、refresh − 2.5ms）はそのまま。
- 中身: drag の間はその窓の client の更新を見せない（frame callback を drop まで止め、client は描かずに待つ。Pi の 1 core では client の描画が止まる分も効く）。**ユーザーの判断が要る**（9 の未決 3）。代わりの案: commit ごとに R が焼き直し、C に新しい画像を渡す（R と C が同じ swapchain を使うので受け渡しが要り、焼き 1 回は窓 1 回分の GPU）。
- 終わり（button を離す）: `RETURNING`。bar への drop（dock）は 5.3 の dock のアニメへ続く（窓は plane のまま。dock の絵は title bar を除くので、アニメの始めに焼き直す。p007 の最初のコマと同じ費用）。

### 5.3 dock・undock・pull back のアニメ

- アニメの間、窓の plane の `dstRect` をアニメの矩形に（HVS が拡大縮小）。**HVS には拡大だけをさせる**: dock・pull back（小→大）は始めの絵を拡大、undock（大→小）は終わりの大きさ（浮いた窓の大きさ）で焼いた絵（client の docked の大きさの画像を GPU で縮めて 1 回焼く）を、大きな矩形から縮む dst へ拡大で出す。縦の縮小は帯域を倍に見る（1.6）ので、1080p の大きな窓でも予算に収めるため（6）。undock の始めの数コマは拡大で少し柔らかい（dock の終わりと同じ種類、9 の未決 5）。primary は窓を除いた場面で止まる（p007 の desktop の絵の代わり）。title bar は窓の位置から bar の slot へ滑って fade する（`draw_window` の anim の部分）ので、窓の絵には title bar を入れず、上の帯の plane に bar と滑る title bar をコマごとに描く（R、damage は title bar の前と今の矩形と `bar_dock` で動く pill。小さな GPU の仕事）。上の帯の plane の矩形は bar の帯と title bar の通り道の外接。
- 影は窓の絵に入れて一緒に拡大縮小する（p007 はコマごとに今の矩形で描く）。角の丸めも拡大縮小で伸びる（p007 で受け入れた種類）。
- configure はアニメの終わり（p007 のまま）。終わった後、client が新しい大きさを描くまで（最大 1 秒）は p007 と同じく伸ばした絵を出し続けてよい（plane のまま、`RETURNING` は client の新しい絵の commit の後）。
- p007 の絵の経路（GPU で 2 quad）は、plane の無い出力と条件を外れた時の fallback として残す。

### 5.4 上の帯の plane

- 中身: system bar と、操作の間に窓の上に要る物（dock のアニメの title bar）。premultiplied、透明な所は下が見える。bar の glass は（窓の popup より後の glass と同じく）blur した壁紙を sample するので、下の窓に依らず合成と同じ画素になる見込み（`kwl_glass_draw` の「the glass from here is on the blurred wallpaper」の注記。bar の pill の描き方は p006 で読み直して確かめる、10）。
- primary は `ON_PLANE` の間 bar を描かない（2 重に描くと影が濃くなる）。bar の中身の変化（時計・status）はこの plane の画像を描き直す（帯 1 本、小さい）。
- 上の帯の plane が無い（overlay が 1 枚）時は、窓の plane の矩形（影を含む）が bar の帯と交わらない間だけ `ON_PLANE`（交われば `RETURNING`）。

### 5.5 条件（`ON_PLANE` にしてよい時、毎 pass 確かめ、外れたら `RETURNING`）

- 出力に窓の plane（能力: PER_PIXEL_PREMULTIPLIED、src が出力の大きさまで、dst が出力の中）がある。dock のアニメは拡大縮小も要る。
- 出力が 1 つ（heads が無い）、窓がその出力の上で、始めの矩形（影を含む）が出力の中に全部入る（外の部分は絵に無い）。
- 窓が一番上（drag は raise の後）、server の装飾の窓（CSD でない）、sheet・popup が開いていない、fullscreen でない。
- 見えている窓に backdrop を使う物（docked の中央の窓、`set_blur`、並べの glide）が無い。arrange の menu・keyboard の blur・App Home・Wiseview・desktop の slide・screen off・greeter・lock・dnd が無い。
- 窓の上に描く物（bar 以外: 窓の popup、menu、通知、status panel、volume、switcher、power dialog、keyboard、corner の hint）が見えていない。途中で出たら `RETURNING`（例: drag の途中の通知）。
- `--no-planes` の起動の選択（試験と比べのため）が無い。

### 5.6 glass・影・角・blur・重なり・damage

| 物 | 扱い |
| --- | --- |
| 影 | 窓の絵に入れる（premultiplied の alpha）。HVS の premultiplied の over は GPU の blend と同じ式で、差は丸めの ±1。 |
| 角の丸め | 絵の alpha 0 の角。drag では変わらない、dock では拡大縮小で伸びる。 |
| title bar・半透明の body・panel の glass | 始めの場所の blur のまま絵に焼かれ、窓と一緒に動く（**ユーザーの判断、9 の未決 2**）。drop で正しい場所の blur に戻る（5.7）。 |
| backdrop の blur | 使う窓・画面がある時は plane にしない（5.5）。 |
| 重なり | 窓は一番上、下の窓は primary に今どおり。上に来る物は bar（上の帯）だけで、他は条件で除く。 |
| damage（WS211 p008） | `ON_PLANE` の間、窓の移動は primary の damage を出さない。入る・戻るの時だけ窓の矩形＋reach と bar の帯。他の物（時計、他の窓の commit）の damage は今どおり R が primary に描く。hover の記録は今の `drag_damage` と同じく無効にする。 |
| 窓の中身の commit | drag の間は見せない（5.2）。dock のアニメの間は p007 と同じく始めの絵。 |

### 5.7 継ぎ目（plane と合成の切り替えの見た目）

- 入る時: 絵は合成と同じ draw で同じ座標に焼くので、画素は合成と同じ（HVS と GPU の blend の丸めの差だけ）。1 回の present で primary と plane が同じ refresh に替わるので、窓が 2 重・消える frame は無い（4.1 の原子性）。
- 戻る時: 窓の glass が今の場所の blur に替わる（壁紙の色が違う所まで動かした時に見える）。drag の間に止めた中身が最新に替わる。title bar の button の hover の光は drag の間は変わらない。
- dock の終わり: title bar の glass が bar の中に消え、窓の影は合成の影（今の矩形の柔らかさ）に替わる。
- 間に合わない時（焼きが遅い）: 合成の drag のまま始まり、焼けた所で切り替わる。切り替えの frame で窓の位置は同じなので見た目の跳びは無い。

### 5.8 WS213 の R・C との関係

- R: 焼き（plane の絵、上の帯の絵）と、入る・戻るの present（primary＋overlay の 1 回の present）、上の帯の描き直し。want の種類を足す。
- C: `ON_PLANE` の drag の間、窓の plane と cursor を 1 回の present で動かす。窓の plane の swapchain は、入るの present の後に R から C へ、戻るの前に park で C から R へ渡す（Vulkan の swapchain の外部同期。同時に 2 つの thread が同じ swapchain を使わない）。受け箱に窓の plane の欄（ずれ・矩形・世代・follow の on/off）を足す。
- dock のアニメの間の窓の plane の `dstRect` はアニメの時計で決まり pointer に従わないので、R が上の帯と一緒にコマごとに present する（C は cursor だけ）。
- libvulkan の安全: C と R が同じ VkQueue を同時に使うことは WS141 p016 B-u の契約（9 節）に従う。overlay の fast path も queue の mutex を取らない。
- 依存: WS213 p004（段 C・R）が main にあること。M・R・C の park・want の仕組みを使う。

### 5.9 前後の入れ替え

- 今の compositor の前後の入れ替え（click で raise、triple click で lower）にはアニメが無く、1 回の合成（入れ替わった窓の矩形の damage）で済む。focus が変わると影の深さと title bar の白さが変わるので、plane でも絵の焼き直し（GPU）が要る。VK_KHR_display の stack の順は surface で固定で、順を変えるには別の plane へ絵を写し直す。
- よって前後の入れ替えはこの WS の第 1 版の段に入れない（効果が小さい）。並べ（WS181）の窓の glide のアニメを plane にするのは後の候補。ws.md の目標の「前後の入れ替え」をこの範囲に狭めてよいかは **9 の未決 4**。

### 5.10 i915・Venus・Linux・FreeBSD

- overlay の plane は bcm2711 の kernel だけが見せる（`GPU_DISPLAY_HAS_OVERLAYS`）。i915・Venus の libvulkan は plane を今どおり 1 つ（＋cursor の無い）列挙し、Mesa（Linux・FreeBSD）の build は display の plane を開かない（cursor と同じ、WS213 3.3）。compositor は窓の plane が見つからなければ今どおり合成する。変わるのは compositor の分岐と libvulkan の列挙の code だけで、見た目と経路は同じ（T1 で確かめる）。

## 6. 効果の見積もり

Pi の今の数字（Q1、2026-10-12）: submit→shown の p50 は 800x480 で 11ms、1024x600 で 26ms、1920x1080 で 61ms。drag は約 30fps。

| 場面 | 今 | plane の後（見込み、Pi での確かめ前） |
| --- | --- | --- |
| drag の移動 | 毎コマ窓の前と今の矩形を合成、約 30fps、窓は cursor に遅れる | GPU 0（submit 無し）。C の present（fast path、ioctl だけ）＋次の vblank。解像度に依らず 60fps、窓と cursor が同じ refresh。入力→表示は cursor の plane と同じ程度（今 cursor は中央値 8ms＋最大 1 refresh） |
| drag の始め・終わり | — | 1 回ずつ窓の矩形の合成（今の drag の 1 コマと同じ程度の GPU）と絵の焼き 1 回。焼けるまでの始めの数コマは今の合成の drag（5.2） |
| dock・undock（120ms） | 2 コマ（p007 の前）。p007 の後は 2 quad の全画面のコマ（Pi 待ち） | primary は止まり、窓の拡大縮小は HVS。コマごとの GPU は上の帯（title bar と bar の pill）の小さな描き直しだけで、120ms に 6〜7 コマ |
| 中身の変化 | 今どおり | 今どおり（plane は中身の変化を速くしない） |

帯域（読み、60Hz、1 pixel 4 byte。全画面 1 枚: 800x480 で 92 MB/s、1024x600 で 147 MB/s、1920x1080 で 498 MB/s）:

| 1920x1080 の場合 | console あり | console 省略 |
| --- | --- | --- |
| primary＋窓（1280x800 の窓＋影の余白、約 1400x928、312 MB/s）＋上の帯（1920x96、44 MB/s）＋cursor（1 MB/s） | 1.35 GB/s | 0.86 GB/s |
| （採らない形）primary＋docked の大きな窓の undock を HVS で縦に縮小（約 1900x1000、倍 2 で 912 MB/s）＋上の帯 | 1.95 GB/s（予算超え） | 1.45 GB/s（予算のぎりぎり） |
| （採る形、5.3）同じ undock を終わりの大きさの絵（約 1400x928）から拡大＋上の帯 | 1.35 GB/s | 0.86 GB/s |
| 能力の最大: primary＋窓の plane（出力の大きさ）＋上の帯（出力の大きさ、dock のアニメの最悪） | 1.99 GB/s | 1.49 GB/s |

- 800x480・1024x600 では、console を残し縦の縮小をしても 0.62 GB/s 以下（予算の半分未満）。
- 1080p は console の省略が前提。能力の最大の組み合わせ（上の帯が出力の大きさになる dock のアニメの最悪）は予算の際なので、Pi の underrun の数で確かめる。超えるなら上の帯の能力（高さ）を狭める。
- 処理量: 1080p で primary 31M＋窓 20M（scaled の全画面でも 62M）＋帯 3M clock/s で、core clock 500 MHz に十分収まる。
- memory: 窓の plane の画像 2 枚（出力の大きさ、1080p で 8.3 MB×2）、上の帯 2 枚（1080p で 0.7 MB×2）。plane を開く時（出力の open）に 1 回取り、出力の close まで持つ。p007 の `KWL_LAYER_WINDOW`（8 MB）は plane のある出力では使わない。
- LBM: scaled の overlay 1 枚に幅 2048 まで 8192 word（60K word のうち）。

## 7. 段（p003 以降の Phase の案、Queue は Q1）

番号: p002 は ws.md の「規約の全文の見直し」のまま（最後に行う）。実装は p003 から。

| Phase | 内容 | 依存 | 試験 |
| --- | --- | --- | --- |
| p003 | kernel: 状態から作る list、overlay（unscaled・scaled・LBM）、slot ごとの hold、帯域・clock の予算と ENOSPC、console の省略、overlay の commit が primary の採用の待ちに並ばない lock（B-k2 と合わせる）、UAPI（承認の後）と core の検査、underrun の計数 | 9 の未決 1（UAPI の承認）、Q1 の割り当て（WS141 の source） | host: list の符号（overlay の位置・clip・scaled の word・LBM・順・console の省略）、hold の解き方、予算、coalesce、UAPI の struct の大きさと ioctl の番号、core の検査（ASan・UBSan）。build warning 0 |
| p004 | libvulkan: overlay の plane の列挙と能力、overlay の surface と swapchain、複数の swapchain の 1 回の present を 1 つの commit に、位置だけの fast path と ticket、acquire と採用の sequence | p003、WS141 p016 B-u | host: WSI の fixture（偽の display の transport）で列挙・能力・まとめ・fast path・ticket の DROPPED・acquire の待ち |
| p005 | compositor: drag（焼き、入る・戻るの present、C の follow、条件と fallback、中身の止め方、`--no-planes`、log `KWL WPLANE ...`）。上の帯の plane はまだ使わず、窓が bar の帯と交われば合成に戻る（5.4 の最後） | p004、WS213 p004 の main、9 の未決 2・3 | host: 状態の遷移と条件（fake の Vulkan、TSan）。T1（i915・Venus で画が前と同じ）。Pi（8） |
| p006 | compositor: 上の帯の plane（bar への drag も plane のまま）と dock・undock・pull back の拡大縮小（p007 の絵の経路を plane に） | p005 | host・T1・Pi（8） |
| p002 | 規約の全文の見直し（WS が変えた C の全部）と、新しい HVS の code の GPL との類似の監査（WS141 の規則） | p003〜p006 | style-check、監査の記録 |

- 各段で build（rpi4・amd64・keiland-linux、warning 0）と変えた所の短い host 試験。QEMU と実機は T1 と Q1 経由（担当は QEMU を起動しない）。
- p003 の kernel は WS141 の bcm2711 の source に触れる。P3（WS141）の作業と重なるので、担当と順序は Q1 が決める（B-k2 を WS141 で先に行うなら p003 はそれに依存）。

## 8. 試験

- host（各段、上の表）。kernel の list は `plan/ws141/tests/display-host-test.sh` と同じ形の試験を `plan/ws214/tests/` に置く。回帰として残す物は WS の終わりに master.md の Tools に登録するか tests/ のシナリオへ（AGENTS.md の基準）。
- T1（WS の終わりにまとめて）: i915（5330）・Venus（QEMU）で drag・dock・undock の shot が前と同じ、`KWL VULKAN_ERROR`・`KWL FAILED` なし、`KWL WPLANE` が出ない（plane が無い）。Linux の TSan の guest（WS213 の受け入れ 3 と同じ手順、C の follow の部分が Linux では動かないことの確かめ）。
- Pi（1 boot に場面をまとめる、v3d.perf=1、`--log-frames`）:
  1. 窓（Settings・Files・Monitor）を 10 秒ずつ drag（800x480・1024x600・1920x1080 のどれかの今の mode、可能なら 2 つの mode）。見る物: `KWL WPLANE move` の間隔の中央値 ≤ 17ms・60/s 近く、drag の間の `v3d: perf` の submission が 0（焼きと入る・戻るを除く）、flip の report の underrun 0、`KWL WPLANE enter/return` の時刻と理由。
  2. bar へ drag して dock、double click で dock・undock、pull back。アニメのコマ数（`KWL WPLANE anim` の行）が 120ms に 6 以上、上の帯の描き直しの時間、underrun 0。
  3. drag の途中で通知を出す（条件を外れて戻る）。
  4. ユーザーの目視: 窓と cursor が一緒に動くか、入る・戻るの継ぎ目（glass の色の替わり、中身の替わり）が気になるか。
- `--no-planes` で同じ場面を流し、今との比べにする。

## 9. 未決（Q1・ユーザーの判断が要る物）

1. **UAPI の追加**（4.2、`GPU_DISPLAY_COMMIT`・`GPU_DISPLAY_OVERLAY_INFO`・`GPU_DISPLAY_HAS_OVERLAYS`）の承認。p014 でユーザーは present に x,y を足すのを嫌い、cursor と明示した plane を承認した。この案は「overlay と明示した plane の commit」で、primary の present には足さない。承認が無ければ p003 以降に入れない。
2. **glass の blur を操作の間は始めの場所のままにする**見た目（drag の間、title bar・半透明の body・panel の glass が窓と一緒に動く。drop で正しい blur に替わる）を受け入れるか。受け入れないなら、title bar を別の plane にして止まった時に焼き直す案（overlay を 1 枚増やす）を次の版で設計する。
3. **drag の間、その窓の中身の更新を止める**（drop で最新に）を受け入れるか。動画などを drag しながら見たい時に止まる。受け入れないなら、commit ごとの焼き直しと R→C の画像の受け渡しを p005 に足す（GPU は窓 1 回分ずつ）。
4. **「前後の入れ替え」を範囲から外す**（5.9: 今はアニメが無く効果が小さい）。ws.md の目標の文を直すのは Q1。
5. **undock の始めが少し柔らかい**（5.3: HVS に縮小をさせず、終わりの大きさで焼いた絵を拡大で出す。1080p の帯域を予算に収めるため）を受け入れるか。受け入れないなら、縦の縮小を許す mode（800x480・1024x600）だけ縮小し、1080p は p007 の GPU の経路にする（Q1・ユーザー）。予算（1.5 GB/s）は Pi の underrun の実測で直す。
6. kernel の段（p003）の担当と、WS141 p016 B-k2 との順序（Q1）。
7. libvulkan の「同じ display の swapchain の 1 回の present は同じ refresh」の約束を README に書くこと（実装の定め、仕様の外。Mesa には無い）。

## 10. 確かめていない事（この版の限り）

- Pi の実機・QEMU は何もしていない。HVS が隠れた plane を読む（console の省略で帯域が減る）こと、帯域の予算、underrun は Pi の計測で確かめる。
- Vulkan の仕様の全文（VK_KHR_display_swapchain の本文）で、複数の swapchain の present の同時性の記述は確かめていない（reference page の範囲には無かった）。zedBSD の実装の定めとして扱う。
- HVS5 の scaled の plane の word の正確な並び（PPF・TPZ の係数の word の数）は p003 で文書（temp）から確かめる。LBM の式は Linux の source の読みで、Broadcom の公開の文書では確かめていない。
- 窓の popup・sheet・sub-surface の条件（5.5）、bar の pill の glass が下の窓に依らないこと、上の帯の矩形は p005・p006 で source を読み直して確かめる。
- p007（dock の絵）と p008（damage）の Pi の結果は未着（Pi の place16・place23 待ち）。数字は Q1 の 2026-10-12 の値。
