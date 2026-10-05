<!-- awesome-plan project=zedbsd record=ws128-p012 -->
# ws128-p012: App Home（Apps の menu）の app の icon をデザインした物に差し替える

Parent: [WS128](../ws.md)
Status: in-progress（2026-10-06 ユーザーが montage-4 に決定、compositor に実装済み。UAT の image での確認待ち）
Disposition: normal

## 由来

ユーザー（2026-10-05 夜）「AppsのメニューでAppアイコンをちゃんとデザインしたものし差し替えたい」

## 範囲（案）

- App Home に出る全ての標準 app の icon（Files・Settings・Terminal・Text Editor・Notes・PDF Viewer・Image Viewer・Browser・Calendar・Phone・Mailer・Music・Video・System Monitor ほか）を、統一したデザインの icon に差し替える。
- 置き場所と形式は今の icon の仕組み（WS094 の desktop の file の icon）に合わせる。大きさの段（bar・App Home・Wiseview）。
- light と dark（WS089 p017）の両方で読めること。

## ユーザーの決定（2026-10-06）

「アイコンはラウンドでカラーをベースにする意見を反映してほしいです。」→ **角の丸い四角の色の地に白い記号**（ws099-p034 の bar-1 の画像の形）。誰が描くか（エージェントの SVG か画像の生成か）は mock で示して確かめる。

## 未決（ユーザー、以前）

デザインの方向（例: カレンダーで決めた「落ち着いた知性」の glass・3D 寄りか、平らな線画か）、誰が描くか（エージェントが SVG で描いて見せるか、画像の生成か）。

## 案（2026-10-06 P2、ユーザーの確認待ち）

[montage-1.png](images/montage-1.png)（明るい地・暗い地、bar の 26 px・Wiseview の 32 px・App Home の 64 px・大きい 112 px）。

- 形は今の仕組みのまま: 角の丸い色の四角（半径は辺の 0.3）に白い線画（辺の 0.7）。絵は [icons.c](../../../userland/desktop/wayland/icons.c) の
  24 単位の格子の vector の部品（線・円・弧・点・角の丸い箱・穴、新しく右向きの三角）で書き、compositor の rasterizer がその大きさで描く
  （外の icon の集まり・font・画像の生成は使わない）。
- 今まで頭文字だけだった 5 つに絵を足した: Video Player（画面と再生の三角）、Phone（受話器）、Calendar（綴じ輪・月の帯・日の点）、
  Mail（封筒）、System Monitor（台の上の画面と脈の線）。既存の 13 の絵（Files・Notes・Settings・Terminal・PDF・Image・Text Editor・
  Browser・X terminal・Model・Gears・Lock・Log Out）はそのまま。
- 色: Calendar を Files と同じ青（2f7cf6）から赤（e8483f）に変えた（並べた時に Files と見分ける）。他は apps.conf の色のまま。
- 窓の app ID（videoplayer・phone・calendar・mailer・monitor）で bar・Wiseview の mark に絵が出る（`icon_app_ids`）。App Home は
  apps.conf の picture の名前（video・phone・calendar・mail・monitor）。
- atlas: app の絵 18 個 × (40+1) と小さい絵 18 × (14+1) で 1008 px（幅 1024 以内）。

確かめたいこと（ユーザー）: (1) この線画の方向でよいか（立体・gradient にはしない）、(2) Phone の受話器の形、(3) Calendar の赤、
(4) 他に絵を変えたい app。

道具: `plan/ws128/tests/icon-montage.sh [OUT.png]`（host で icons.c をそのまま build した icon-dump と icon-montage.py）。

## 2026-10-06 ユーザーの回答（montage-1 への）

参考の画像: reference-circle.png（2026-10-06 ユーザーの指示で著作権の関係で削除）（ユーザーの添付。SNS の logo を円の単色の地に白抜きにした集）。

- 方向: 「ちょっとGoogleっぽすぎますね。かといってAppleにもあまり寄せたくないんですよね。添付の1つめみたいなデザインに寄せられますか？ラウンドスクエアでなく、サークルで、単色の背景、白抜きのアイコンです。」→ **円**の**単色の地**に**白抜き（塗りの白の形）**の icon。角の丸い四角はやめる。線画でなく塗りの白の形（参考の画像の形）。
- Phone の受話器: 「だめですね……。なんのアイコンかマジでわからないｗ」→ 一目で電話と分かる受話器に描き直す。
- 色: 「カレンダーは青にします。FilesをWindowsに寄せてオフイエローっていうですか、フォルダアイコンの色にしてみましょう。」→ **Calendar は青**、**Files は Windows の folder の色（くすんだ黄、off-yellow）**。
- 「上のルールで再度デザインをお願いします。」→ 全 18 の icon を円・単色・白抜きで描き直し、montage-2 をユーザーに見せる。
- bar の左の app の pill（ws099-p034）の icon もこの形に合わせる。

## montage-2（2026-10-06 P2、ユーザーの回答を反映、確認待ち）

[montage-2.png](images/montage-2.png)。全 18 を**円・単色の地・白抜きの塗りの形**に描き直した（[icons.c](../../../userland/desktop/wayland/icons.c) の vector の部品）。

- 部品の仕組みを広げた: 部品に `ICON_CUT` の印を付けると、それまでに塗った白からその形を抜く（部品は順に適用）。三角は任意の 3 点（部品に 6 つ目の値 `f`）。
  1 つの icon の部品の上限 12 → 16。titlebar の操作の icon（0〜20 番）の raster は前と 1 bit も変わらないことを確かめた（icon-dump の PGM を比較）。
- 形: Files は folder（tab の下の線を抜く）、Notes は notepad と鉛筆、Settings は 3 本の slider（Gears の歯車と区別）、Terminal・X terminal は白い画面に
  prompt・X を抜く、PDF は角を折った頁に行を抜く、Image は絵に山と太陽を抜く、Video は再生の三角を抜いた画面、**Phone は受話器を描き直した**（太い弧の
  柄の両端に、内へ向いた耳と口）、Calendar は綴じ輪と日の点、Mail は封筒、System Monitor は台の上の画面に脈、Browser は地球、Model は立方体、
  Gears は歯車、Lock は錠、Log Out は扉と矢印、Text Editor は大きな T と cursor。
- 色: **Files を Windows の folder の色（くすんだ黄 e8b53e）**、**Calendar を青（2f7cf6）**。並べた時に分けるため Notes をオレンジ（ff8a3d）、
  Image を桃色（ec5f9a）、Mail を空色（19a1e6）、Text Editor を藍（5c6bc0）、System Monitor を青緑（13a89e）、Browser を濃い青緑（0e7490）、
  Gears を茶（a0522d）に変えた。他は前のまま。
- 白い形は円の直径の 0.68 の格子（形は格子の約 3/4 で、直径のおよそ半分）。
- compositor の mark（bar・Wiseview・App Home・title bar）を円にする統合は ws099-p034 の実装と一緒に行う（この commit の時点では mark はまだ角の丸い四角）。

## 2026-10-06 ユーザーの回答（montage-2 への）

「アイコンのレビューですが、まだGoogleっぽさが強いので、だめです。添付の2つめのように、おもいきって白地に中抜き透過にするアイコンのモンタージュと、添付の1つめのように背景をラウンドスクエアにしつつ斜めにベルト状に量子化グラデーションをいれて色味もパステル寄りのやや鮮やかめ、みたいなモンタージュ、2種類作れますか？」

montage-2（円・単色・白抜き）は不採用。2 案の montage を作ってユーザーが選ぶ:

- **案 W**（参考 reference-white.png（2026-10-06 ユーザーの指示で著作権の関係で削除））: **白の角の丸い四角の地**に、記号を**中抜き（透過）**にする。記号の形の所が透けて、下の色（bar・App Home の地、壁紙）が見える。記号の線は太めの塗り。
- **案 B**（参考 reference-band.png（2026-10-06 ユーザーの指示で著作権の関係で削除））: **角の丸い四角**の地に、**斜めの帯状の量子化した gradient**（2〜3 段の色の帯が斜めに入る）。色は**パステル寄りのやや鮮やか**。記号は白（一部は色つきの絵、参考の天気・地図の pin のような）。
- どちらも Phone の受話器は分かる形、Files は off-yellow・Calendar は青の指定は案 B の色の割り当てに引き継ぐ（案 W は地が白なので記号の透過の色は下の地による）。

## 2026-10-06 ユーザーの選択（montage-3W・3B への）

「3Wの背景色（斜めベルト）を採用しつつ、ピクトグラム部分は3Bの透過を採用し、モンタージュを作成してください。」（ラベルは入れ違いだが意味は明らか）→ **地は案 B の斜めの帯の量子化 gradient（パステル寄りの鮮やか）**、**記号は案 W の中抜き（透過）**。montage-4 で確かめる。
## montage-3W・montage-3B（2026-10-06 P2、ユーザーの選択待ち）

- [montage-3W.png](images/montage-3W.png): 案 W。白の角の丸い四角（半径は辺の 0.24）から記号の形を抜き、下の地が透ける。暗い bar（26 px）、App Home の地、
  壁紙 Birch Lake・Lakeside（64 px）、大きい 112 px。記号は montage-2 の塗りの形（icons.c の同じ vector の部品）をそのまま「抜く形」に使う
  （中の細部（端末の prompt など）は白く残る）。明るい壁紙の上では抜いた所が淡く、読みやすさは地に依る。
- [montage-3B.png](images/montage-3B.png): 案 B。角の丸い四角に、2 色の間を 3 段に量子化した斜めの帯と細い明るい筋。色はパステル寄りのやや鮮やか
  （Files は off-yellow、Calendar は青）。記号は白、Notes・Mail・Model は淡いクリーム色（色つきの絵の例）。明るい地と暗い地、26・64・112 px。
- 道具: `plan/ws128/tests/icon-montage-3.py DUMP_DIR OUT_W.png OUT_B.png`（DUMP_DIR は `icon-dump 448`）。compositor の mark の形（W か B か）は
  ユーザーの選択の後に入れる。それまで icon の code は merge を依頼しない。

## 2026-10-06 ユーザーの選択と montage-4（P2）

ユーザーの選択（Q1 の伝達）: **3B の背景（パステル寄りの鮮やかな色の斜めの量子化 gradient の帯、角の丸い四角）に、3W の中抜きの記号（記号の形が透けて下の地が見える）**。
[montage-4.png](images/montage-4.png): 暗い bar・App Home の地・Birch Lake・Lakeside・明るい地・暗い地、26・64・112 px
（`plan/ws128/tests/icon-montage-3.py DUMP_DIR x x OUT_4.png`）。

再開の地点（ラップアップ 2026-10-06）:
- ユーザーが montage-4 を確かめたら、compositor に入れる: 地の帯は compositor の shape（solid の帯を角の丸い四角の clip で 3 つ、または小さな
  texture）で描き、記号は atlas の coverage を「抜く」形で描く（今の `glass_draw_icon` は塗るだけなので、抜くには mark を offscreen で合成するか、
  帯を記号の coverage の逆で描く shader の mode が要る。設計を先に）。色は icon-montage-3.py の `APPS` の 2 色。
- icons.c の記号（塗りの形、`ICON_CUT`・任意の三角・16 部品）と Wi-Fi の扇は agent/p2 の 7d79d7b0・b095413c にあり、merge は保留（Q1）。
- 円の mark（7d79d7b0・b095413c の `APP_MARK_PICTURE`・home.c の円の tile）は不採用なので、統合の時に角の丸い四角（帯）に置き換える。

## 2026-10-06 ユーザーの決定（montage-4）

「モンタージュ4は気に入りました。これで行きましょう！アイコン一式を更新してください。UATイメージのビルドが終わったら、rootfsツリーのアイコンだけアップデートして、UATイメージを更新してください。」→ **montage-4 の形（斜めの帯の量子化 gradient の角の丸い四角、記号を中抜き）に決定**。compositor の icon 一式（icons.c の app の記号と描き方、中抜きの描画の経路）を更新し、UAT の image を更新する。bar の配置（ws099-p034、b095413c）は別。

## 設計: montage-4 の compositor への実装（2026-10-06 P2）

- **tile を CPU で完成した絵として作る**（shader は変えない）。icons.c に `zwl_icon_tile(icon, pixels, argb, stride)` を足す: 1 つの app の tile を
  premultiplied の ARGB（0xAARRGGBB）で描く。
  - 地: 斜めの帯（"/" の向き、`along = (x + (side - y)) / (2 side)`）を 3 段に量子化し、左下から濃い色・2 色の中間・淡い色。中の帯に明るい筋
    （`|along - 0.58| < 0.035` を白へ 0.2 寄せる）。色は 4×4 の subsample の平均（帯の境が滑らか）。2 色は記号ごとの表 `icon_app_bands`
    （icon-montage-3.py の `APPS` と同じ値。Files は off-yellow、Calendar は青）。
  - 形: 角の丸い四角（半径は辺の 0.24）、縁は距離で anti-alias。
  - 中抜き: 記号（`zwl_icon_raster`、辺の 0.66 を丸め、余りが偶数になるよう 1 足して中央に置く）の coverage を alpha から引く:
    `alpha = 形の coverage × (1 − 記号の coverage)`。記号の所は alpha 0 で、後ろ（bar・App Home の地・壁紙・窓）がそのまま見える。
- **合成**: glass に tile の image（host image、1024 幅、linear sampler）を 1 枚足し、glass が開く時に 18 の記号を 20・28・48・72 px
  （title bar の mark と Wiseview の label・bar・Alt+Tab・App Home）で 1 対 1 に描いて置く（2 px の隙間）。`glass_draw_app_tile(server, command,
  icon, x, y, pixels, opacity)` が `MODE_IMAGE`（premultiplied の over、今の blend のまま）で描く。他の大きさは、それ以上で最小の物を縮めて
  （72 より大きい App Home の起動の拡大は 72 を広げて）描く。dark の外観で `MODE_IMAGE` の色は変わらないので tile は同じ。
- **描く所**: App Home（`home_draw_icon`）、bar の app の icon と Alt+Tab（`zwl_glass_draw_app_mark`、今の bar の配置のまま）、title bar の mark と
  Wiseview の tile の label（`draw_picture_mark`）。記号の無い app（頭文字の tile）は今のまま。
  - App Home の記号のある tile は影を描かない（穴から影が見えて濁る。montage-4 に影は無い）。pointer の下では tile の alpha を coverage にした
    白（0.15）を重ねる（`MODE_TEXT`、`light` で dark の外観でも白）。
  - 色は記号に付く（apps.conf の色は頭文字の tile だけに使う）。`zwl_icon_for_app_id` の色の引数は使い道が無くなるので外す。
- atlas の app の記号（40 px と 14 px の 2 組）は使わなくなるので消し、atlas の cache の行を返す。`glass_draw_icon` は titlebar の icon（0〜20）だけ。
- titlebar の操作の 21 の icon の raster は変えない（main の icons.c と icon-dump の PGM を 16・20・32・64・448 px で比べる）。
- 確認: zedBSD と Linux の compositor の build（warning 0）、icons-host・icon-dump の PASS、style-check、host の PNG（App Home と bar の並び、
  icons.c の `zwl_icon_tile` そのもので描く、`plan/ws128/tests/tile-host.c`）。QEMU は使わない（UAT の image は Q1 が作り直す）。

## 実装と結果（2026-10-06 P2、branch agent/p2-icons）

上の設計のとおり実装した。

- `icons.c`: `zwl_icon_tile`（帯・筋・角の丸い四角・記号の中抜き、premultiplied ARGB）、`icon_app_bands`（montage-4 の 2 色）。
  `zwl_icon_for_app_id` は色を返さない（`icon_app_ids` の色の列を消した）。
- `glass.c`: tile の image（1024×256、linear sampler、20・28・48・72 px × 18、2 px の隙間で 250 行）を `zwl_glass_open` で作る
  （失敗しても look は開き、tile の無い mark は描かない）。`glass_draw_app_tile`（`MODE_IMAGE`、lighten は `MODE_TEXT` の白、`light`）。
  atlas の app の記号（40・14 px）を消した。`glass_draw_icon` は titlebar の icon（0〜20）だけ。
- `home.c`: 記号のある app は `glass_draw_app_tile`（影なし、pointer の下で 0.15 白く）。頭文字の tile は `home_draw_letter` に分けて今のまま。
- `shell.c`: `draw_picture_mark`（title bar・bar の docked の題・Wiseview の label、20 px）と `zwl_glass_draw_app_mark`（bar の app 28 px・
  Alt+Tab 48 px）が tile を描く。bar の配置は今のまま（ws099-p034 の b095413c は含まない）。
- 触った file の style-check の既存の指摘（glass.c の `S_ISREG` の条件・close の後の空行・`(void)argument` の comment、home.c の
  `kl_backend_session_managed` の条件）も直した（動作は同じ）。

確認（host、QEMU は使っていない）:
- zedBSD の compositor `make -j16 ZEDBSD_CONFIG=/home/awe/zedBSD-claude1/config.mk BUILD=build/p2-icons build/p2-icons/bin/wayland`: exit 0、warning 0。
- Linux の compositor `make -j16 keiland-linux KEILAND_LINUX_BUILD=build/p2-keiland-linux`: exit 0、warning 0。FreeBSD は未実施。
- `plan/tools/titlebar/icons-host.c`: PASS。`plan/ws128/tests/icon-dump.c`: PASS（first_app=21 count=39）。
- titlebar の 21 の icon: main（e0085cd9）の icons.c と icon-dump の PGM を 16・20・32・64・448 px で `cmp`、全て同じ（bit 単位）。
- `plan/ws128/tests/tile-dump.c`（新規）: 20・28・48・72 px で PASS（角が透明、帯の不透明な画素がある、記号の中抜きがある、premultiplied、
  titlebar の icon には tile が無い）。
- style-check: icons.c・icons.h・glass.c・glass.h・home.c・shell.c・tile-dump.c で指摘 0。
- host の画像（`plan/ws128/tests/tile-screens.sh [OUT_DIR]`、tile は icons.c の `zwl_icon_tile` の画素そのもの、地の glass・blur は近似）:
  [tiles-home.png](images/tiles-home.png)（App Home、home.c の格子、Settings は pointer の下の明るさ）、
  [tiles-bar.png](images/tiles-bar.png)（bar の app 28 px（Video は最小化で薄い）、title bar の mark 20 px、Alt+Tab 48 px、bar の 3 倍）。

未実施・残り: QEMU・実機での表示（Q1 が UAT の image を作り直して確かめる）、dark の外観の実画面、Wiseview の実画面、FreeBSD の build。

