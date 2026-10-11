# ws210-p001 設計 1.1 の再確認（H1〜H3・M3・polygon の cache、2026-10-12 design-reviewer、Q1 が要約して写した）

総評: H1〜H3・M3 の大筋は直った。1.1 で新しく入った誤り 3 つ（atlas の key の衝突、round_border の帯の幅、SUBOPTIMAL の扱い）。**p002（spike）は条件付きで入ってよい**: register の計測は `registers_used`・`threads`（常に 64・2）ではなく割り当てた register の最大（`shader.c:325` の上書き前の値）で、角丸の内側を分けた時と分けない時の両方の頂点の費用を測る。他は文書の直し: p003 の前に round_border の帯・atlas の頁の形式、p004 の前に atlas の key・画像の cache の退役・版・polygon の LRU と network の graph、p005 の前に TRANSFER_DST・extent・SUBOPTIMAL。HAL・UAPI の変更は不要。ユーザーの判断 1 点: Settings の network の graph を「sample ごとに一度だけ CPU で用意する」例外に入れてよいか。

| 重さ | 箇所 | 問題 | 推しの直し |
| --- | --- | --- | --- |
| H | B4 の atlas の key、text.c:648、B7、chooser.c:176・picker.c:182・notes/box.c:329-335 | glyph の key（glyph・大きさ・face の番号・太さ）は `kl_text` ごとに閉じた値、atlas は app 全体で共有 → 別の font の `kl_text` で違う文字を描く（chooser・picker・Notes・Text Editor、close の後の同じ番地） | key を (`kl_text` の通し番号, glyph の key)、通し番号は open で振り close で消す private な表、G に 2 つの font の場面 |
| M | B4 の「頁は RGBA8」と B5、text.c:283-289 | 色の glyph（memory で B,G,R,A）を R8G8B8A8 の頁に写すと R と B が入れ替わる | atlas の頁を B8G8R8A8 と明記、G に R と B が違う emoji |
| M | B6 (3)、present.c:555・520-531 | swapchain に TRANSFER_DST の usage が無い（規則違反）、swapchain の大きさが window と違いうる → `copy_whole`（vulkan-native-image.c:526）が通らない | imageUsage に TRANSFER_DST（supportedUsageFlags を確かめ、無ければ quad の経路）、W の大きさは swapchain の imageExtent、形式と extent の変化で作り直し |
| M | B6 の OUT_OF_DATE・SUBOPTIMAL、present.c:255-256 | SUBOPTIMAL でも acquire は成功、present を省くと image と semaphore が返らず数 frame で acquire が止まる | OUT_OF_DATE の時だけ省く、SUBOPTIMAL は copy と present の後に EAGAIN |
| M | B1 round_border の帯、canvas.c:445-455・1064-1070 | 帯（幅 太さ＋1）の外に角の弧がはみ出し角の枠が描かれない（半径 12・太さ 1 で 45 度の所は約 3.5 px 内側） | 上下の帯は高さ 半径＋太さ＋1 の全幅、左右の帯は幅 太さ＋1 でその間、fragment の見積も直す |
| M | B1・B2 の角丸の内側の分割、vulkan-native-draw.c:803-829 | 分けると round 1 つが fill 1＋round 4、頂点の scalar 294（分けない時 66 の約 4.5 倍）、pipeline の交互で draw も増える | 内側の面積が閾値（p002 で決める）を超える形だけ分ける、p002 で両方を測る |
| M | B2・p002・G、shader-lower.c:64、shader-analyze.c:89 | `registers_used`（64 に上書き）・`threads`（2 固定）は常に一定で余裕が分からない | 割り当てた register の最大を host の build で出す計測（driver の中の診断） |
| M | C2 polygon、settings/page-network.c:1118・1036・1052 | Settings の network の graph はデータで形が変わる半透明の polygon、幅 254 px 超、sample ごとに CPU の rasterize・texture・upload・UIF 変換、画像の LRU（Pi 16 MiB）で thumbnail を追い出す、Settings は最初に移す app | polygon の texture は別の小さな LRU、graph は「sample ごとに一度だけ CPU」の例外としてユーザーに確かめるか列の帯の quad で描く、`kl_frame_times` の rasterize の数を p005 の確かめに |
| M | A5 の世代、files/thumb.c:340・634、photos/decode.c:82、settings/look.c:931 | `kl_image_create` を通らない thumbnail、release で表から外すと世代が既定に戻り、同じ番地・大きさで古い texture が引かれる | `kl_image_release` は pointer を key に持つ cache の項目を必ず退役の列へ、世代は進めたまま持つ、`kl_image_scale` は書いた先の世代を進める |
| L | A5 の退役の列 | 別の thread の release で free が先になる窓 | 列に積み、epoch を release の順序で進めてから free、lookup は acquire の順序 |
| L | B4・B7 | window 2 つ（本体と chooser・picker）で頁の pin が window ごとだと片方が空にできる | pin と汚れた頁を context 全体で |
| L | B2 | shadow・ring・capsule は頂点 9 scalar で 54（60 ではない） | 表を直す |
| L | B1 色を float の bit で | 0xFFFFFFFF などが NaN・denormal の形、正規化されると色が崩れる（推測） | G・p002 に試験の値、代わりに 16 bit ずつ 2 scalar の形 |
| L | A2、canvas.c:69-91 | `kl_canvas_init` は NULL を拒まない（記述の誤り）、CPU の canvas を cast して `&->tag` は object の外の pointer の計算 | tag は static な 1 つの番地、container_of、library の canvas への init・release と frame の外の描画の動き（EINVAL か 1 回 log） |
| L | Phase の p004・p005、keiland.h:48 | p004 で公開の関数が入るのに版を上げるのは p005 | p004 で上げるか p005 まで export しない |
| L | G | 版 88 の header で build した binary を新しい .so で描く試験が無い | host の試験に足す |
| L | A5 | Files の hero の `view` は `kl_image_scale` の元だけ（ui-home.c:466-473） | 記述を消す |
