# ws210-p001 設計 第 1 版の敵対的 review（2026-10-12 design-reviewer、Q1 が要約して写した）

総評: このままでは実装に入らない。直しのほとんどは文書の修正で、済めば p002 へ。p002 の最初に Pi で shader の受理と時間を測る短い spike を置く（H3・M11）。HAL・UAPI の変更は要らない。G1(a)（M3 の直しが前提）・G2(a)・G4(a)・G3(a)（M2・L7 を見積に）・G5(a)（M8・M9 を足す）は妥当。

source と合っていた: 頂点の入力（instancing 無し・triangle list・float の属性）、premultiplied の blend だけ・colorWriteMask 全部、image は RGBA8/BGRA8・4096・mip 1・LINEAR も OPTIMAL も raster、discard・loop・整数の比較・sin/cos の拒否、atan 無し、入力の location 16 未満・fragment の出力 location 0 だけ・8192 命令、`if` は両側を実行。
不正確・不足: register は 4〜59 の 56 本で varying は全体で pin（24 の varying で計算に約 32 本）、TFU の code はあるが `hardware.tfu_uif_ready` を立てる所が無い、kernel は draw ごとに頂点の全 scalar を CPU で写す（vulkan-native-draw.c:803-829）、incremental な TLB copy は image ごとの世代で 3 枚でも正しい（実機は未確認）。

| 重さ | 箇所 | 問題 | 推しの直し |
| --- | --- | --- | --- |
| H1 | W・B6・C1、wsi-wayland.c:349-356、present.c:494、vulkan-native-image.c:522-523 | swapchain は R8G8B8A8 が選ばれるのに W は B8G8R8A8 固定 → 標準の driver で R と B が入れ替わり、bcm2711 は形式の違いで `copy_whole` を拒んで毎 frame CPU の変換の経路に落ちる | W・pipeline を swapchain の形式に合わせる（か B8G8R8A8 を優先して選ぶ）、host の試験で一致 |
| H2 | A5・B4〜B6、text.c:283-289/656/807-809、settings/widgets.c:213-231、files/ui-home.c:604-625、picker-model.c `model_make` | 記録して frame_end で実行する間に呼び手の memory が変わる・解放される（text.c の cache の全 free、stack の色の glyph の `kl_image`、static な mark の layer、worker thread からの `kl_image_release`）→ use-after-free と古い絵 | 不変条件「mask と image は cache に無ければ呼んだ時に atlas か staging へ写し、pointer を return の後に持たない」、cache の key に大きさ・stride・世代、`kl_image_release` は queue で main の frame_begin で回収 |
| H3 | B1・B2、shader-lower.c:184-192/735-740、spirv.c:20-31、vulkan-native-draw.c:803-829 | 9 種類の形を 1 本の shader → if-conversion で全種類の計算を毎 fragment、24 の varying の pin で register 不足、頂点 6×24 scalar を kernel が draw ごとに CPU で写す | pipeline を形ごとに分ける（矩形の塗り／角丸・枠・影／ring／capsule／texture／edge）、一定の値は flat、clip の varying は CPU で quad を切り詰めて外す、角丸の内側は塗りの pipeline、p002 の最初に Pi で compile と時間 |
| M1 | B6・C1、vulkan-native-pass.c:449-457/484-491、vulkan-native-execute.c:103-109/291-325 | bcm2711 は renderArea の tile を全部 load・store、scissor だけだと毎 frame W 全体、部分の renderArea の CLEAR は kernel が CPU で塗る | renderArea を draw（64 px に揃える）に、loadOp は LOAD、W の初期化は全体の CLEAR だけ |
| M2 | D 表・p004/p005、settings/ui.c:1877、files/ui.c:1411、files/ui-desktop.c:827、calendar/view.c:634/1539、ime/popup.c | Settings・Files が `canvas->pixels` を直に memset、Calendar の `view_keep_under` が普段の描画で画素を読み書き、IME の popup は自前の wl_shm | D 表に足す、(1) は clip_push と `kl_canvas_clear` に（p004）、Calendar は G3 まで CPU か under を p005 に、IME の popup は別の項目 |
| M3 | A2・B4、keiland.h:2891-2905/2914-2923、Makefile.linux:69、canvas.c:76 | 公開の `struct kl_canvas` が大きくなり、版 88 の binary と新しい .so で memset が越える | 公開の struct を変えない、GPU の canvas は library が確保し private な struct の先頭に `kl_canvas`、glyph の atlas の位置は private な表、KL_VERSION 89 は新しい関数のためだけ |
| M4 | A5、keiland.h:2877-2882、files/ui-home.c:642、calendar、settings/look.c | `kl_image` に世代が無い、その場で書き換える使い方の列挙が無い、呼び忘れで古い texture | 世代は private な表、書き換える所を D 表に、debug の build で内容の hash で検出 |
| M5 | B4、text.c:656 | frame の途中で古い頁を空にすると先に記録した glyph が新しい内容を指す、16 MiB 超・512 px 超の glyph、cache を空にした後の atlas | その frame の頁は pin、空きが無ければ pass を分けるか単独の texture、頁を超える glyph は単独、cache を空にした時に対応を作り直す |
| M6 | A3・B6、window.c:670-679、present.c:241-244、settings/main.c:637 | frame_begin の acquire で CPU の記録が待つ、OUT_OF_DATE・SUBOPTIMAL・device lost の戻り値が未定義、新しい W の中身が不定 | acquire は frame_end、戻り値（EAGAIN・EIO と「全面を描き直す」）を定義、W の作成・resize・device lost で透明に clear、CPU に切り替わっても pointer は不変 |
| M7 | C2、canvas.c:670-708/765、icons-line.c:373 | 凹の polygon で縁が隣の三角形に重なり半透明が 2 回 blend、許容 32 が隠す | 半透明の凹の polygon を厳しい許容で試験、重ならない分け方か制限、許容の根拠 |
| M8 | G5、vulkan-native-draw.c:402/465、v3d-hardware.h:103 | WS141 との依存の持ち主と記録が無い、incremental な copy と snapshot は実機で未確認 | 依存（案 (A)・TFU の gate・実機の確認）を持ち主付きで記録、中間の案「書き込みの記録で変わった tile だけを UIF に変換」を WS141 へ |
| M9 | G 節 | lavapipe の比較は bcm2711 の compile を通らない、Pi の正しさは目視だけ、性能の基準値が無い、T1 の元の PNG を撮る段が無い | Pi で offscreen に描いて readback し CPU の canvas と比べる試験、p004 の前に Pi の基準値と QEMU の基準の PNG |
| M10 | A5・B4・B7 | Pi の memory の予算が無い（app ごとに atlas 16＋staging 16＋画像 64 MiB、kernel の snapshot 128 MiB、窓ごとの W と swapchain 3 枚、bcm2711 の Vulkan の上限 256 MiB（BUG-290）） | 全体の予算、頁を 256×256、upload の後に staging を解放 |
| M11 | B3、vulkan-native-draw.c:100-120 | shape と text が交互で draw が widget の約 2 倍、draw ごとの kernel の CPU の用意が未計測 | 重ならない形を pipeline ごとに並べて batch、p002 で draw あたりの費用 |
| L1 | B1、canvas.c:1137-1140 | quad を浮動小数の辺ちょうどにすると AA の画素が落ちる | floor/ceil の外接矩形と clip の交わり |
| L2 | B5、vulkan-native-image.c:701-706 | buffer→image の GPU の copy は行が 64 の倍数・offset 64 揃えの時だけ | staging の行を 16 px の倍数、offset 64 |
| L3 | B4 | atlas の sampler と glyph の余白が未定義 | mask は NEAREST、1 px の余白 |
| L4 | B2、canvas.c:297-299/375-377/1012-1043/1217-1254 | gradient の式と `kl_color_mix` の切り捨て | shader で同じ式、重ねた場面を試験 |
| L5 | 全体 | DPI の倍率を掛ける所が無い（Guardrail の寸法の単位） | canvas の変換 1 箇所、glyph は device の px |
| L6 | E1 | llvmpipe で GPU の canvas が遅い恐れ（推測） | `VK_PHYSICAL_DEVICE_TYPE_CPU` の時の選び方 |
| L7 | Phase の表 | struct の変更（p002）と KL_VERSION（p004）の時期のずれ、`kl_canvas_gpu_record` の費用が p006 に無い | 時期をそろえる、G3 の見積に |
| L8 | 見積 | p005 の 4 LW は少ない、Pi の性能の調整の Phase が無い | Pi の計測と調整の Phase（2〜3 LW）、計 約 30 LW |
