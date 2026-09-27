<!-- awesome-plan project=zedbsd record=ws035-glass-design -->

# WS035 設計: 窓の中のすりガラスの card（`zed_glass_v1`）と see-through の Vulkan swapchain

Parent: [WS035](ws.md) / Phase: ws035-p083（ws071-p015 と一緒に実装）
Status: 実装済み（2026-09-27、サブエージェント）。背後の窓のぼかし（本当の backdrop blur）は ws035-p057 の残り。

## 0. 出どころ

- 2026-09-27 ユーザー（ws071-p015）:「ファイルマネージャですが、左ペイン、メインペイン、右ペインを分けてくれていますよね。これらはウィンドウボディの中で
  フローティングの見た目にして、それぞれが付箋のように浮いて見えようにしてほしいです。…ウィンドウ内の要素が付箋のように浮いていて、すりガラスの
  エフェクトでデスクトップが透けている、と言いたいだけで、こういうレイアウトにしてほしいという意味ではないです。」
- 同日:「左側のペインと右側のペインで、背景をなくして、付箋メモのようなフローティングにして、すりガラスエフェクトで合成する」
- 同日（タブの見直しと一緒に）:「右側のペインは、すりガラスで透過するので、白く背景を塗りつぶす必要はないです。」
- 参考画像は著作権のためどこにも保存しない。言葉だけで表す。
- [compositing-design.md](compositing-design.md) D10: 効果は窓の「描き方」、すりガラスは「窓の範囲の背後をぼかし、その上に窓を alpha で重ねる。窓は
  半透明（ARGB）で描く」。

## 1. 決定の要約

| 論点 | 決定 | 理由 |
| --- | --- | --- |
| client の窓の alpha | Vulkan の `VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR` を WSI（libvulkan の wsi-wayland）が広告し、選ばれたら buffer ごとに `zed_gpu_buffer_v1.set_alpha(buffer, premultiplied)`（revision 3）で zdesktop に伝える | Vulkan の swapchain の標準の意味（OPAQUE なら alpha を無視）を守る。uapi（`include/uapi/gpu.h` の format）を変えない。Wayland の shm の ARGB と同じ premultiplied の約束 |
| どこをすりガラスにするか | zdesktop の新しい拡張 `zed_glass_v1`: client は「この surface の中の card（角丸の矩形）はすりガラスの上に立つ」と言う | titlebar の仕様と同じく client は**意味**を渡し、ガラスの見た目（ぼかし・白さ・縁・影）は zdesktop が決める。窓ごとに見た目がばらつかない。後の本当の backdrop blur（p057）で client を変えずに見た目が上がる |
| 既存の仕組みの再利用 | `wl_surface.set_opaque_region` は「不透明」を言うだけで「ガラス」を言えない。wayland-protocols の ext-background-effect（staging）は矩形の region だけで角丸を言えず、ここで版を確かめられない（offline）。**採らない**（Future Work: 標準の拡張が固まれば zdesktop が両方を受ける） | 角丸の card と意味（card の影）を 1 request で言える方が小さい |
| ガラスの中身 | 今は zdesktop が起動時に作る**ぼかした壁紙**（title bar と同じ `MODE_GLASS`）。背後の他の窓はぼけて見えない | ws035-p057（背後のぼかし）を分けた: p083 が要る部分（client の card とガラスの合成）を先に。p057 の残りで `MODE_GLASS` の標本を毎 frame の backdrop に替えると title bar と card の両方が良くなる |
| 影 | card の影は zdesktop が描く（`MODE_SHADOW`、全部の card の影を先に、ガラスを後に） | 影が隣の card のガラスに落ちない。client は影を描かない（半透明の card の下の影は透けて汚れる） |
| 窓全体の影 | glass の panel を持つ窓は、body の影と see-through の body のガラスを描かない | 窓は一枚の板ではない（card の間はデスクトップがそのまま見える） |
| タブ | protocol の kind は `card` だけ（2026-09-27、ユーザーのタブの指示でタブは content の card の中の行になった） | 使わない kind を持たない |

## 2. Protocol

### 2.1 `zed_gpu_buffer_v1` revision 3

| opcode | request | 引数 | 意味 |
| --- | --- | --- | --- |
| 3 | `set_alpha` | `object<wl_buffer> buffer`、`uint alpha` | `0`: opaque（alpha を無視、buffer の既定）、`1`: premultiplied alpha で blend |

- zdesktop は global の version を 3 に。version < 3 の binding、shm の buffer、範囲外の値は protocol error（EPROTO）。
- libvulkan: `wayland_capabilities` は factory の version ≥ 3 のとき `supportedCompositeAlpha` に PRE_MULTIPLIED を足す。`wsi-swapchain.c` は
  「advertise された 1 bit」の alpha を受け、OPAQUE 以外は platform の新しい任意の op `composite_alpha(lease, alpha)` を呼ぶ（display の platform は NULL
  で、今まで通り OPAQUE だけ）。wayland の lease は `premultiplied` を覚え、`wayland_import` が buffer を作るたびに `set_alpha(…, 1)` を送る。

### 2.2 `zed_glass_manager_v1`（global name 17、version 1）/ `zed_glass_v1`

| interface | opcode | request | 引数 |
| --- | --- | --- | --- |
| manager | 0 | `destroy` | — |
| manager | 1 | `get_glass` | `new_id<zed_glass_v1> id`、`object<wl_surface> surface` |
| glass | 0 | `destroy` | —（surface の次の commit から panel 無し） |
| glass | 1 | `set_panels` | `array panels`: panel ごとに int32 × 6（x, y, width, height, radius, kind）、surface の座標 |

- **double-buffered**: `set_panels` は pending を替え、`wl_surface.commit` が適用する（viewporter と同じ）。panel は描いた frame と一緒に動く
  （Vulkan の present が commit）。
- 範囲: 32 panel まで、width・height > 0、radius 0〜64、kind は `card = 0` だけ。
- error: manager `already_exists = 0`（surface に生きた glass がある）。glass `bad_panels = 0`（長さが 24 の倍数でない、多すぎる、範囲外）、
  `no_surface = 1`（surface が消えた後の set_panels）。
- 寿命: glass が消えると surface の pending は空（次の commit で panel 無し）。surface が消えると glass は不活性、panel の記録（surface が持つ
  `struct zwl_panels`、最初の get_glass で確保）を解放。
- log（試験が読む）: `ZWL GLASS client=C surface=S panels=N card:x,y,w,h,r ...`（commit で変わったとき）。

### 2.3 libzdesktop（`ZDESKTOP_VERSION` 5）

```c
struct zdesktop_glass_panel { int32_t x, y, width, height, radius; unsigned kind; };   /* ZDESKTOP_GLASS_CARD */
struct zdesktop_glass *zdesktop_glass_create(struct wl_display *display, struct wl_surface *surface);  /* NULL・ENOTSUP */
int zdesktop_glass_set_panels(struct zdesktop_glass *glass, const struct zdesktop_glass_panel *panels, size_t count); /* EINVAL・E2BIG */
void zdesktop_glass_destroy(struct zdesktop_glass *glass);
```

局所の検査で protocol error になる list を送らない（menu・titlebar の API と同じ）。header は非公開（`zed-glass-v1-client-protocol.h`）。

## 3. zdesktop の描画（`panels.c`、`shell.c`）

- `draw_body`: panel を持つ窓は (1) 全 card の影（soft 16、下へ 6、色 0.10/0.18/0.35 の α 0.16）、(2) 各 card の `MODE_GLASS`（白 0.34、縁 0.70、
  **flat**）、(3) sub-surface と窓の image（`set_alpha` の buffer は alpha で blend）。body の影・see-through の body のガラスは描かない。
  panel の座標は body の矩形へ scale（resize 中の伸縮）。
- Wiseview の tile: panel のガラスを tile の縮尺で（影なし）。tile の影は panel の無い窓か、current・hover の glow のときだけ。
- shader（`panel.frag`）: glass mode の上からの sheen（`0.05 × (1 − depth)`）に `(1 − clamp(shape.z))` を掛けた。title bar 等は `soft = 0` のままで
  変わらない。panel は `soft = 1`（flat）。`shaders.h` は `regenerate.py` で作り直し（panel.frag だけ変わる）。

## 4. zdesktop-files（ws071-p015）

- `present.c`: surface が PRE_MULTIPLIED を持てばそれで swapchain を作る（`present->premultiplied`）。canvas の shader は texel の alpha もそのまま
  出す（以前は 1.0）。
- `glass.c`（新）: 窓が see-through で zdesktop に glass があれば `app->glass = 1`（`ZFILES GLASS on`）。各 frame の後、present の前に
  `fm_ui_panels`（sidebar・content の card（タブの行を含む）・preview）を比べ、変わったときだけ送る（`ZFILES GLASS panels count=N`）。
- glass のとき: 地は透明（`fm_canvas_clear`）、sidebar は白 40、content と preview は白 60 の薄い tint だけ（影・縁は描かない）。sidebar の節の
  題は少し濃く（TEXT_SECONDARY）。glass でないとき（host の既定、glass の無い compositor）は今までの不透明な見た目。
- host の試験: `files-render --glass=WALLPAPER` が zdesktop の合成を CPU で真似る（`plan/tools/files/host-glass.c`: 壁紙、card の影、縮めて
  拡げたぼかし＋白＋縁、frame を alpha で）。

## 5. 起動の遅れ（Venus の READY）

zdesktop の起動の仕事は増やしていない（global の表に 1 行、panel の記録は最初の get_glass で確保）。

## 6. 後回し（Future Work の候補）

- G-a: 背後の窓のぼかし → **ws035-p057 で実装（2026-09-27）**: 下の scene を 1/8 に描き直して 2 回ぼかす（[phase057](phase057/phase.md)）。damage で広げるのは p055。
- G-b: wayland-protocols の ext-background-effect を確かめて受ける（標準の toolkit の窓のため）。
- G-c: 他の kind（例: 窓の中の浮いた toolbar、popover）と、kind ごとの白さ・影の深さ。
- G-d: zdesktop-terminal 等の他の client の see-through。
