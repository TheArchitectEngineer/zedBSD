# WS211 p001 review-1（design-reviewer、7ef76d086）
結論: 実装は不可。High 6 件（特に H1・H2・H3）と M1・M3・M4 を設計に反映してから。p002 段 0 の計測だけは H4 の計測項目を足せば先に始めてよい。設計の Pi の実測値（記録 17〜64ms、prep 2.3〜10ms 等）は plan に log が無く確かめられない（log を plan に置く）。

High
H1 cell・時計の帯の部分の描き直しで古い画素が消えない。1.2 は load の pass・scissor を cell に、だけで消さない。合成は over（compose.c:1636-1641）。Pi の decoder は vkCmdClearAttachments を受けない。→ hover の白い tile（glass.c:2434-2440）・光・時計の前の分・動作中の印が残る。直し: loadOp CLEAR・initialLayout SHADER_READ_ONLY・renderArea＝矩形の pass（Pi は部分の CLEAR を CPU で消してから tile を読む vulkan-native-pass.c:494-497）、または blend を切って透明を書く pipeline。消した矩形と交わる形（隣の icon も、光は ±73.6px で cell の半分 72px より 1.6px 広い）を全部描き直す。
H2 後ろの submit が未転写の glyph・壁紙を焼き込む。atlas・大きな字・壁紙は host の staging に書き（glass.c:2879-2901、shm.c:315-345）主のコマの copy_command の中でだけ写す（compose.c:2633-2654）、字の cache は追い出して上書き（glass.c:2992-3005、3074-3088）。鍵に atlas の generation を入れると全体の描き直しが繰り返す。直し: layer の command buffer の先頭で kwl_host_images_record、その submit が終わるまで staging への書き込み（atlas_put 等）と主のコマの記録を止める（layer の submit も in_flight）。鍵から atlas の generation を外し「焼いた時に字が欠けた」印を鍵に。壁紙は kwl_import.version を鍵に。
H3 drag の間の desktop の絵の描き直し条件が毎コマ真。server->dirty は drag が立てる（home.c:517）、scene.changed は hover の probe（damage.c:305-318）・cursor を含むあらゆる commit（damage.c:129-130）でも立つ。直し: kwl_damage_commit で表示中の desktop の窓（cursor を除く）の commit の時だけ立つ専用の印、map・unmap・前面化・desktop の切り替え・壁紙・外観でも立てる。
H4 見積もりの律速の取り違え。設計の submit→shown 53〜171ms に対し prep＋exec 11〜45ms で 40〜120ms が未説明。アニメの各コマでも scene の取り込み（全画面の copy）と cursor のための LOAD pass の再開が走る（compose.c:2381-2393）、アニメ中は scene を再利用しない（compose.c:2269）。アニメは壁時計（home.c:348-357）。最初のコマ（desktop の絵、2 枚の TFU 約 7ms）が 280ms の多くを食う。推測で 5〜8 コマ。直し: p002 段 0 で kernel の perf の rec・tex・tfu・copy・xfer・bin・render、bcm2711: flip perf、client の GPU job との待ち合わせを記録。アニメ中は scene の取り込みを止める。アニメの時計を最初のコマを出した時から、または desktop の絵を閉じている間に先に撮る。受け入れの数値は計測の後に。
H5 束ねると kernel の頂点の詰め直しが約 9 倍。prepare_fetch は shader の入力の scalar を頂点ごとに 1 つずつ写す（vulkan-native-draw.c:822-848）、元は uncached（vulkan-memory.c:417、buffer.c:118-148）。1 形 12→108 scalar、Home 全体 約 5.4k→48.6k 回。直し: 段 1 で perf の rec を前後で比べる。頂点を小さく（色を R32 に詰める・位置から求める・uv は image/text の mode だけ）。kernel でまとめて写す改善は WS141 の範囲と明記。
H6 flat の varying は Pi の fragment で安くない。1 つにつき varying の signal・空き 1 命令・mov（shader-lower.c:117-137）、頂点側と fragment の入力の照合（shader-analyze.c:184）で variant でも省けない可能性（推測）。14 個で fragment あたり 30〜40 命令増。全画面の形まで束ねると fill が大きく増（全画面 5 枚で推測 10ms 以上）。直し: 面積の大きい形は今の push constant の pipeline のまま。qfrag の counter で比べてから。

Medium
M1 頂点の buffer の bind と push constant: 今は corner の buffer が binding 0（compose.c:2351、heads.c:1265）。束の flush で束の buffer を bind（firstVertex を kernel は扱う vulkan-native-draw.c:925-930）、1 形 1 draw に戻る前に corner の buffer を bind し直す、別の pipeline layout の後は screen.xy を push し直す。
M2 見た目の変化（ユーザー判断済みの物に加わる）: Home の content が 1 枚の絵として薄くなる（今は形ごとの opacity で光・床・映り込みが tile の下から透ける）、画面の外にはみ出した窓と影が縮む間に端で切れる、窓の glass の blur の位置が layer と一緒に拡大縮小（軽微）。
M3 起動した app の窓が閉じる 240ms の間に出ない（kwl_glass_mapped の ANIM_LAUNCH で icon から育つ shell.c:2750-2790）。直し: map・unmap・アニメの窓では desktop の絵を描き直すか、その窓だけ絵の上に直に描く。
M4 主の pass の外で描く cache の pass: glass_shape_draw の cull は compose->scissor_now（glass.c:1706-1714）。scissor_now・view_*・layer_on=0・keep_colours=1・backdrop_set・backdrop_recording を設定して戻す（keep_colours を 1 にしないと暗い外観で色が変わる glass.c:1743-1753、Home は自分の色 home.c:585）。cell・時計の帯を描き直したら kwl_damage_rect で damage に足す。
M5 「CPU で画素を触らない」は事実と違う: renderArea を cell にすると kernel は 256KiB 以下の箱を CPU で UIF に変換（native-storage.c:690-748、NATIVE_SNAPSHOT_UPDATE_BYTES）、部分の CLEAR も CPU（vulkan-native-pass.c:494）。全面にすると hover ごとに全面の TFU と tile の読み書き。記述を直し Guardrail のもとでどちらを採るかユーザーに確かめる。
M6 鍵: 時計は整形した時刻と日付の文字列、外観は不要（accent はページの点だけ）、atlas の generation は外す。
M7 試験: layer の鍵の host 試験では H1・H2・M4 を検出できない。--no-layer-cache と KWL_BATCH=0 の切り替えを作り、hover の出入り・分の繰り上がり・動作中の印・閉じている間の言語と壁紙の変更の後の絵を参照と比べる、Pi でも shot.c で。アニメのコマも。plan/ws141/tests の道具（shader の host 試験、kwl-latency.py）は master の Tools に登録か ws211 に移す。
M8 p002 では検索とページの drag は今の経路のまま、WS の目標「開いたままの操作も軽い」は p003 まで。
M9 desktop の絵を最初に開く時に作ると止まる（Pi で 8MiB の 1GiB 未満の連続 uncached）、断片化で失敗も。起動時に 2 枚とも作る。閉じ始めの撮り直しも変化の印（H3）が無ければ使い回す。
M10 新しい file（layer-cache.c）は Makefile.linux・Makefile.freebsd（Makefile.freebsd:15-67）にも。build の確認は rpi4・amd64 だけでなく。

Low
L1 ページの点は home_pages > 1 の時だけ（home.c:681-682）。L2 時計の帯を閉じている間に毎分描かない、開く時に描く。L3 WAR の external 依存も（backdrop.c:262-275）。L4 起動時の tile の拡大は quad に半径を付けて角を切る。L5 layer の影は縁の帯だけに。L6 KWL HOME layer=cover/content・opened の log の意味を保つ。L7 uncached の mapping へは cached で組んでからまとめて写す。L8 2 LSB は静止の絵では妥当（推測）、アニメのコマは根拠なし。

設計どおりと確かめた点: draw の数（床 192、icon ごと 11、映り込み 8 片）、時間差 510ms、push constant 96 byte、instance の rate が無い、flat と 32 word、snapshot の generation の再利用と 128MiB、UNORM、blend ONE/ONE_MINUS_SRC_ALPHA、VPM 18 入力・22 出力、i915 の varying 16・attribute 16。
