# WS211 p001 review-2（design-reviewer、fd0d61862）— 要約（全文は Q1 の会話の記録から）
結論: 実装はまだ不可。N1・N2・N3 を直すか計測に入れてから。
可否: 段 0（計測）可（N3 の awaiting・wait_ms・shown→draw と KWL SCENE・KWL DAMAGE を log に入れ plan に置く）。段 0b は N1 を直せば可（段 0 の直後でよい）。クロスフェード（段 1・2）は N2 を設計し直し（後ろの焼き直しを present しない主のコマとして出すのを推奨）、N4・N5・N6・N9・N10・N8 の無限ループを直し、N3 を段 0 の結果で決めてから。client の画像を使わない Home の絵（段 1）は N5・N8 を直せば先に進めてよい。p003 は N11・N12 を足せば大筋で可、段 0 と p002 の計測の後。
review-1 の判定: H1 解決（寸法の box と quad の混在、矩形は描く関数を寸法だけ測る形で quad の和から）、H2 一部（N8、N2）、H3 一部（N6）、H4 一部（N1・N3・N4）、H5 解決（N12）、H6 解決（面積は quad で判定、64×64 だと tile・穴・光は束にならない、440 形全部束は過大）、M1 ほぼ（N11）、M3 一部（N10）、M4 一部（N5）、M5・M6・M8・M10 解決、M7 一部、M9 一部（N14）、L5 は新しい見た目の差（N13）。

High
N1 段 0b のままだと Pi の全コマが全画面の描き直し: compose.c:2259 `if (scene.buffer && !scene.valid) region = NULL`、valid は capture したコマの完了で 1（655-657）。直し: capture = scene.buffer && !cursor_plane.shown を 1 つ求め 2259・2269・2382 を同じ値で。plane が閉じた最初のコマは今のまま。試験: Pi で段 0b の前後に KWL DAMAGE の部分のコマが残る。
N2 後ろの desktop の絵の submit が client の buffer と host 画像の寿命と競合を守らない: 寿命は主のコマの in_flight・held（compose_hold compose.c:2747-2767）、quiesce は主の fence だけ（730-745）、objects.c:550・shm.c:187 はこれ頼み。kwl_shm_upload は in_flight だけ見て staging に書く（shm.c:146、619）。shm の画像も host_images（shm.c:797）で kwl_host_images_record が全部写して written=0（327-343）。新しい import の layout の移行は主のコマだけ（compose.c:2295）。→ release 後の上書き中の sample、裂けた画像、解放済みの読み、layout 未移行。直し（推奨）: 焼き直しを present しない主のコマとして出す（command・fence・in_flight=1・compose_hold（held だけ、frame callback なし）・kwl_import_layouts_record）。別案: layer の fence を全部の待ちに足し held を持つ。Home の絵は独立に進められる。
N3 アニメの各コマの後の最大 50ms の待ち: display.c:200-204、compose.c:678-682（前のコマの半分、上限 50）、transition の免除は最初のコマだけ（compose.c:605-609）、awaited は client の commit まで（display.c:396-399）。log で 51ms が並ぶ。→ Q1 が main で直した（自動のアニメ（anim・home_moving・wiseview_moving・desktop_moving）の間は待たない）。段 0 の log に awaiting・wait_ms を足して確かめる。

Medium
N4 時計の起点を最初のコマの submit にすると進みが戻る（home.c:351、2369）、0 にすると Home を描かない（shell.c:633）。起点は最初のコマの record の開始、そのコマは小さな正の値、または progress を単調に。
N5 cache の pass の状態: viewport（compose.c:2340）と corners の bind（2351）を pass の中で設定。desktop の絵は keep_colours=0（glass.c:1743-1753）、Home は 1。kwl_home_draw は自分で layer_on=1 と変換を置く（home.c:650-660）→ stage を含まず変換も置かない content だけの関数を分ける。desktop の絵は home>0 の規則で（home<=0 だと draw_desktop_layer が draw_backdrop shell.c:10149、10158 → backdrop.c:89 で pass が終わり 126 で framebuffer_now に開き直す）。framebuffer_now = VK_NULL_HANDLE で防御（backdrop.c:82）。
N6 layer_desktop_changed の漏れ: anim の終わり（shell.c:3301、6534、objects.c、protocol.c）、最小化・dock・undock・layout_hides、desktop の icon、窓の飾りの hover・押下、出力の大きさ、言語。推奨: 毎コマ CPU で desktop の層の署名を作って比べる（窓の並び・id・位置・大きさ・buffer の version・title・最小化と dock・icon の世代・飾りの hover・壁紙の version・暗い外観・accent）。
N7 後ろの焼き直しが Pi で引っかかりを生む可能性（同じ queue、cursor の写し cursor-plane.c:468、Home の全体の焼きは今の 1 コマとほぼ同じ重さ）。段 1・2 で焼き直しの間の入力のコマの input→shown・cursor の at・焼き直しの回数と GPU 時間を測る。細切れか回数の上限、100ms の timer。
N8 字が欠けた印の無限ループ（glass_glyph_of は制御文字・cell を超える大きさ glass.c:3070・font 無し 2989 で常に NULL）→「追い出せなかった」時だけ印。layer が飛んでいる間に主のコマの字が NULL（glass.c:1885）→ layer の fence 完了で dirty。守る期間は記録の開始から fence まで。cache の数（約 200〜300、glass.c:1355）を超えた時。
N9 開く最初のコマの待ちが延びる場合（2 枚とも焼き直しが要る時）→ 焼き直しが要るコマは今の経路で出し、焼き直しは次の submit に。KWL FIRST_FRAME ms で比べる。
N10 絵を焼いた後に始まった anim の窓が二重（shell.c:2784、6118、6189、9651）→ クロスフェードの間に anim が始まったら今の経路、または 1 回だけ焼き直す。

Low
N11 head の pass は同じ layout で大きさが違う（heads.c:1262-1283）→ pass ごとか大きさが変わるたびに screen.xy を押し直す。N12 kernel は 16KiB を超える頂点の詰め直しを別確保に（vulkan-native-draw.c:31-32、1234-1242）、solid 1 形 288B で 56 形を超える束は遅い経路 → 束の上限を詰めた後 16KiB 以下に。N13 L5 の影を縁の帯だけにすると、layer が薄くなる間に全面の影の内側が透けて見えるのが消える（edge.c:212-218、shell.c:649-660）、角の内側も → ユーザーに確かめるか全面の影を残す。N14 2 枚を起動時に作る失敗の扱い（片方だけ、出力の開き直し、descriptor の pool KWL_DESCRIPTOR_MAX compose.c:1290-1294、scene の buffer 8MiB の解放）。N15 266 コマ全部 reuse=0 と prep 2.3〜10ms は plan の log で確かめられない。N16 試験に N1・N2・N3・N5 を検出する場面（段 0b の前後の KWL DAMAGE、焼き直しの最中の窓の大きさ変え・終了・shm の描き続け、Venus の検証 layer）。
