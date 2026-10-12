<!-- awesome-plan project=zedbsd record=ws211-p011 -->

# ws211-p011: present の copy の command buffer の使い回しと、compositor の窓の tile あたりの GPU の費用

Parent: [WS211](../ws.md)
Status: in-progress（設計の第 1 版、2026-10-12 P3）
Phase disposition: normal
Queue: P3（2026-10-12 ユーザー「進め方はおすすめでいいです」、Q1 の割り当て。[p010](../phase010/phase.md) §2.3 の P4 の残りの提案の 1 つと、p008 の残り）

## 由来

- [p010](../phase010/phase.md) §2.3: libvulkan の WSI の present は、1 コマに copy の command buffer の Begin・End・Submit の 3 往復をする。Begin・End（2 往復）は、同じ中身を記録し直しているだけ。
- [p008](../phase008/phase.md) の残り: compositor の窓の領域は place22 の log で約 13 万 cycles/tile、全面 1 draw は約 1.1 万。p008・p009 の後も効くかを調べ、原因を数字で示し、直せる所を直す。

## 範囲

- (1) libvulkan の WSI（`userland/desktop/libvulkan/wsi-swapchain.c`、WS141 の範囲だが Q1 の割り当て）。libvulkan と kernel の間の wire は変えない。cursor の lane（WS141 p016 B-u）と ticket の挙動を変えない。
- (2) compositor（`userland/desktop/wayland/`）の shader と描き方。kernel・HAL・UAPI・toolchain は変えない。compose.c は触らない（P4 の WS213 p005 と重なるため）。

## 1. present の copy の command buffer の使い回し

### 1.1 今

`present_record` は present ごとに、slot（`struct wsi_present_fence`、worker ごとの cache）の command buffer を `vkBeginCommandBuffer`（ONE_TIME_SUBMIT）→ copy の記録 → `vkEndCommandBuffer` し、`present_submit` で submit する。bcm2711 では Begin と End がそれぞれ kernel への往復で、submit を入れて 3 往復。slot の選び方は「busy でなく、payload の種類（shared・sent）が合う最初の slot」。

### 1.2 記録の中身が何で決まるか

`present_copy`・`present_copy_shared` の命令は、次だけで決まる。

- chain（application の image、native の `shared_image`・`readback`、extent、`gpu_present`）。native の image は chain の作成の時だけ作られる（`swapchain_native_images_create` の呼び出しは `swapchain_create` の 1 か所）。
- image の index。
- `shared_presented`（最初の present の前の `UNDEFINED` か、後の `GENERAL`・外の queue family か）。一度 TRUE になれば戻らない。
- display の合成（`has_display` で cursor でない時の srcRect・dstRect）。
- queue family（slot は worker の物、worker は queue の物なので一定）。
- route（cursor の lane の `WSI_ROUTE_EMPTY` は空の記録）。

damage（`VkPresentRegionsKHR`）は記録に入らない。

### 1.3 設計

- **鍵**: slot に「最後に成功した記録の鍵」`struct wsi_present_recording`（valid、chain の serial、image の index、route が copy か empty か、合成の有無と srcRect・dstRect、`shared_presented`）を持たせる。chain の serial は `swapchain_create` で付ける 64 bit の単調な番号（file-scope の atomic な counter）。chain の pointer は解放の後に再利用され得るので鍵にしない。
- **使い回せる job**: `job->count == 1` で、その target の `results[0] == VK_SUCCESS` の job だけ。複数の swapchain の present と、前段で失敗した target を持つ job は今どおり毎回記録する（鍵を無効にする）。
- **slot の選び方**（`present_fence_prepare`、swapchain_mutex の下）:
  1. busy でなく payload の種類が合い、鍵が等しい slot があれば使う（記録を使い回す）。
  2. 無ければ: 鍵の無効な空き slot、次に（種類の合う slot の数が `WSI_PRESENT_SLOTS`＝8 未満なら）新しい slot、最後にどれかの空き slot。新しい slot も無い busy ばかりの時は今どおり新しい slot を足す。
  - 使い回しの無い job（鍵を持てない job）は今どおり最初の空き slot。
  - 3 枚の swapchain の image を順に present する compositor では、slot が image ごとに 1 つずつ（最大 3、cursor の lane は別の worker）に落ち着く。
- **記録**: 使い回す時は Begin・End も copy の記録もしない（`job->command` に slot の command buffer を入れるだけ）。cursor の lane の `copy_recorded` は「この job の submit が copy を含むか」なので、使い回しの時も route で同じに立てる。記録し直す時は Begin の前に slot の鍵を無効にし、End の成功の後に鍵を入れる。Begin の flag は ONE_TIME_SUBMIT をやめて 0 にする（使い回す記録は submit の後も executable のまま残る必要がある）。
- **submit の失敗**: 鍵を無効にする（安全な側）。
- **再 submit の正しさ**:
  - Vulkan: ONE_TIME_SUBMIT でない primary は、実行が終われば executable に戻り、再 submit してよい。slot は worker が fence の完了を見て `present_finish` で busy を外すまで他の job に渡らないので、pending の間の再 submit は無い。
  - bcm2711: `vulkan-draw.c` の walk は `state == EXECUTABLE` と `pending == 0`（SIMULTANEOUS でない時）を要る。ONE_TIME でなければ実行の後も EXECUTABLE のまま（`vulkan-queue.c:750` は ONE_TIME の時だけ INVALID にする）。pending は native job の dispose（`execution_step`）で 0 に戻り、fence の公開（`execution_end` の `publish_completion`）はその後なので、fence を見た後の再 submit で pending は 0。submit ごとに `bcm2711_vulkan_prepared_create` が記録から snapshot を作り直すので、wire と kernel は変わらない。
  - i915: 記録（op の list）は Begin・reset まで残り、submit ごとに list を実行する（`render/command.c`）。flag は見ない。
  - Venus: 標準の Vulkan の再 submit。
  - libvulkan: submit は command buffer の identity を送るだけ（`queue.c` の `queue_write_submit`）。`content_images`（cursor の中身の serial の表）は Begin で空にされ、WSI の copy の宛先は `swapchain_owned` でない native の image だけなので空のまま。再 submit でも同じ。
- **cursor の lane と ticket**: fast path（`present_fast`）は slot を使わないので変わらない。ticket・`present_reserve`・`present_cursor_show` は触らない。変わるのは worker の job の記録の有無だけ。
- 見込み: compositor の primary の present は 1 コマ 3 往復 → 1 往復（p010 の place23 で queued→submit の p50 4〜5ms のうち約 2/3）。Wayland の client の present も同じに減る。

### 1.4 試験

`plan/ws141/tests/vulkan-cursor-host.sh`（本物の wsi-swapchain.c を fake の core で動かす）。fake の command buffer に「記録した copy の数」を持たせ、`fx.wsi_copies` は submit の時に足す（今の assert の意味「copy を含む submit の数」を保つ）。記録の数（Begin の数）を別に数え、使い回しを確かめる場面を足す:

- u17: 同じ chain の 3 枚を 4 周 present する。copy を含む submit は 12、記録は 6（各 image の最初の 1 回（`shared_presented` が偽）と 2 回目（真）だけ）。
- u18: chain を作り直すと（oldSwapchain）、同じ index でも記録し直す。

## 2. compositor の窓の tile あたりの GPU の費用

### 2.1 測った事（Pi の kernel.log の `v3d: gpu`、2 秒の窓ごと）

counter の意味は `src/drivers/gpu/bcm2711/v3d-job.c` の `counters_prepare`（V3D 4.2 の event の番号: cycles=32、qidle=13、qfrag=15、qvalid=16、tmu=24、tmumiss=25、tmustall=54、quads=3、written=9）。集計の script は scratchpad の `p3-p011/tiles.py`（commit しない）。draw の pass（`pass->draws != 0`）の窓のうち 100 tile 以上の窓（lock 画面の blur の窓を除く）:

| log | 窓の数 | draw の cycles/tile p50（最小〜最大） | quads/tile p50 | cycles/quad | qvalid/quad | QPU の使用（qfrag ÷ (8 × cycles)） | tmu/quad | TMU の miss |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| place22（p008 の前、1280x800） | 87 | 53k（11k〜110k） | 3173 | 15.3 | 30.4 | 0.237 | 5.5 | 2.1% |
| place23 res480（p008 の後、800x480） | 31 | 54k（10k〜89k） | 3442 | 15.6 | 30.4 | 0.237 | 5.8 | 2.0% |
| place23 p23b（p008 の後、1024x600） | 22 | 62k（14k〜77k） | 3746 | 15.9 | 31.3 | 0.236 | 6.4 | 2.4% |

- この cycles/tile は compositor の pass と client の pass（1 draw の全面 quad、約 11k）の平均。compositor だけでは 2 倍近い（p008: Calendar の窓で約 133k）。
- **QPU の使用率はどの窓でも 0.236〜0.237 で一定**、cycles/quad は qvalid/quad のちょうど半分（1 draw の全面の solid で 11.1 と 21.9、lock 画面の glass で 26.7 と 52.7）。V3D の QPU は 16 lane の命令を 4 clock で出すので、counter が命令の数を数えるなら 8 QPU の上限は cycles × 8 ÷ 4 で、0.24 × 4 ≈ **96% が使われている**。GPU は **QPU の演算で詰まっている**（ALU bound）。qidle は cycles の 1〜2%。
- **TMU は原因でない**: miss 0.5〜5%、tmustall（event 54）は cycles の 0.3% 以下。texture の形式・UIF・cache の miss を直しても費用はほぼ変わらない。
- **費用 ≒ Σ（層の画素の数 × その shader の命令の数）÷ 32 cycles**（命令 N の shader は 16 画素に N 命令、8 QPU が 4 clock に 1 命令）。1 tile（64×64）を 1 層が覆うと 128N cycles。
- quads/tile p50 3200〜3750 は **1 画素あたり平均 3.1〜3.7 層**（4096 画素 = 1024 quad）。p008 の Calendar の窓の compositor の pass は 7.8 層、平均 132 命令。

### 2.2 shader の命令の数（kernel の compiler の出力、host の probe）

`build/p3-p011/shader-size`（`build/p3-probe/src/shader-size.c` を今の source で作り直した host の probe、commit しない）。全部 4 thread。

| shader | 命令 | うち NOP | texture | 1 tile を覆う費用（128N） |
| --- | --- | --- | --- | --- |
| glass（mode 0） | 241 | 69 | 1 | 30.8k |
| blur（mode 6、5 tap） | 154 | 42 | 5 | 19.7k |
| shadow（mode 1） | 92 | 29 | 0 | 11.8k |
| image（mode 2、窓の絵） | 91 | 22 | 1 | 11.6k |
| ring（mode 4） | 87 | 25 | 0 | 11.1k |
| solid（mode 3） | 81 | 24 | 0 | 10.4k |
| text（mode 5） | 47 | 11 | 1 | 6.0k |
| quad（壁紙・client の絵） | 29 | 5 | 1 | 3.7k |

- glass の 241 命令の内訳: NOP 98 行（純粋な NOP 69）、mul の側だけの命令 79（fmul 57・mov 22）、add の側だけ 81（fadd 28・fsub 25・fmax 12・fmin 9 ほか）、recip 5、rsqrt 1。add と mul を 1 命令に組んだ物はほとんど無い。
- 1 つの層の中で、draw ごとに一定の値（`white`＝`step(0.9, min(r,g,b))`、`dark`＝`step(mode, -0.5)`、sheen の `0.05 × (1 − clamp(shape.z))`、`1 / max(box.w, 1)`）を画素ごとに計算している。

### 2.3 窓の中の層（source を読んだ結果）

- **panel（card）を持つ窓**（Keiland の app、`panels.c`）: 壁紙（quad）→ card の外接の箱の glass（gap、不透明）→ card ごとの影（card ＋ 周り 32px）→ card ごとの glass（不透明）→ 窓の絵（ARGB、半透明）。card の中の 1 画素は 壁紙 29 ＋ gap glass 241 ＋ 影 92（隣の card の影が重なれば ×2〜3）＋ card glass 241 ＋ 絵 91 = **694〜878 命令**。見える層は card glass と絵だけ（壁紙・gap glass・影は不透明な card glass の下）。
- **panel の無い窓**（`shell.c` の `draw_body_part`）: 壁紙 → 影（窓 ＋ 周り 44〜60px、窓の内側も覆う）→（window_opacity < 1 の時）glass の body（不透明）→ 絵。絵が不透明（XRGB・set_alpha 0）で window_opacity が 1 の時、影の内側は絵の下。glass の body がある時、影の内側は glass の下。
- 不透明の理由: glass の色は `vec4(glass * cover, cover) * opacity` で、rounded rect の内側（radius ＋ 1 だけ内）は cover＝1、opacity が 1 なら alpha は 1。image は `opaque=1` で alpha＝cover。

### 2.4 原因のまとめ

1 tile あたりの費用は、**見えない層の重なり**（card の窓の中で 694〜878 命令のうち 380〜570 が不透明な層の下）と、**長い shader**（glass 241 命令、うち draw ごとに一定の計算と NOP）の積。glass の blur の sample（`backdrop.c` の 5 tap は 1/8 の大きさの別の pass、lock 画面の窓だけ tmu/quad 12.9）、texture の形式・UIF・TMU の miss は窓の費用の主因ではない。

### 2.5 直し（compositor の中、compose.c は変えない）

#### D1 glass の shader の draw ごとの値を CPU で（`shaders/panel.frag`、`glass.c`）

- glass（mode 0、dark glass の mode −1）は texcoord を使わない。push constant の texture の 4 つ（constants[4..7]）に、`glass_shape_draw` が draw ごとに計算した `white`（0 か 1）・`dark`（0 か 1）・sheen の量 `0.05 × (1 − clamp(shape.z, 0, 1))`・`1 / max(box.h, 1)` を入れ、shader はそれを読む。計算の式は同じ（sheen の積の順と、割り算が逆数の掛け算になる所だけ 1 ulp 程度変わる）。
- **light と dark の variant を分ける**: dark glass（constants[17] が −1）は新しい variant（`KWL_PANEL_MODE=7`、lift と shade）、light glass（mode 0）は shade の計算を持たない（dark＝0 なら `mix(glass, 0, 0)` は glass そのものなので、結果は同じ）。`KWL_PANEL_MODES` を 8 にする（compose.h、compose.c の pipeline の loop はこの数で回るので compose.c は変えない）。`glass_shape_draw` は constants[17] が −1 の時 `panel_pipelines[7]` を使う。generic の shader（mode の値で分ける）は両方を持つ。
- host の probe: light glass 241 → 178 命令（−26%）、dark glass 241 → 202（−16%）。
- i915・Venus も同じ SPIR-V と同じ constants を使う（`panel.vert` は texture を texcoord に入れるだけで、glass では使わない）。

#### D2 不透明な層の下を描かない（`damage.c` の切り、`glass.c`・`panels.c`・`shell.c`）

- `kwl_damage_rects_cut`（damage.c、純粋な関数、host 試験あり）: 四角 quad から穴の四角の list を引き、互いに素な四角（最大 `KWL_CUT_PIECES` = 8）を返す。穴は重なりの面積が `KWL_CUT_AREA`（96×96 画素）以上の物だけ、面積の大きい順に引き、引くと 8 を超える穴は引かない。穴は内側へ整数の画素に丸める（隣の piece と縁を共有し、隙間も重なりもない）。
  - 閾値の根拠: 穴 1 つで draw が最大 3 つ増える（kernel の準備は draw あたり約 10〜50µs、CPU 側）。96×96 の影（92 命令）で 26k cycles（約 53µs の GPU）、gap glass（178 命令）で 51k cycles。
- `glass_shape_draw_except(server, command, shape, holes, count)`（glass.c）: 切った piece ごとに shape の quad を piece にし、uv を quad の中の位置で比例させて（image でも正しい）`glass_shape_draw` を呼ぶ。layer の不透明度が 1 未満の時（App Home の後ろの desktop）は穴を使わない（上の層が半透明になるため）。
- 穴 = 不透明な上の層の rounded rect の内側（box を radius ＋ 1 だけ内へ）:
  - `kwl_panels_draw`: opacity が 1 以上の時、各 card の glass の内側を穴にし、gap glass と全部の card の影をその穴を除いて描く（card の glass は影と gap の後に描かれる）。
  - `draw_body_part`: BODY_WHOLE で、glass の body を描く時はその内側、描かず絵が不透明（`KWL_DRAW_OPAQUE`）で window_opacity が 1 以上の時は絵の内側を、窓の影の穴にする。
- 見込み（命令の数の比、card の中の 1 画素）: 694〜878 → 298（壁紙 29 ＋ card glass 178 ＋ 絵 91）、**−57〜66%**。gap の所（card の間）は gap glass 241 → 178。panel の無い glass の窓の中: 影 92 が消える。
- 残り（範囲外、提案）: 壁紙と下の窓は、上の窓の不透明な所の下でも描かれる（壁紙 3.7k/tile、下の窓は層の数だけ）。draw の list の全体での遮蔽（後の不透明な draw の四角で前の draw の scissor を削る）は compose.c・draw-list.c の replay の変更なので WS213 の後に別の Phase。kernel の compiler の add と mul の組（glass の 241 命令の約 1/3 が組める）と NOP の削減は WS141 p013 の続き（全部の shader に効く、Q1 に提案）。

### 2.6 試験

- `plan/ws211/tests/damage-host-test.sh` に `kwl_damage_rects_cut` の場面（穴なし、真ん中の穴で 4 piece、縁に触れる穴、小さな穴は引かない、8 を超える時、piece の和が quad − 穴の面積と等しく互いに素）。
- shader の命令の数は host の probe で（commit しない）。shaders.h は `regenerate.py` で作り直す。
- build: rpi4 の wayland・libvulkan、amd64 の wayland。既存の ws211・ws213 の host 試験。
- Pi・T1: 見た目（glass の色、影、card の窓、dark の外観）が今と同じこと、`v3d: gpu tiles` の draw の cycles/tile と quads/tile が下がること。

## 確かめ

（実装の後に書く）
