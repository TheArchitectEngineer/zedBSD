<!-- awesome-plan project=zedbsd record=ws213-p005 -->

# ws213-p005: S3: 作業 thread（glyph の raster・壁紙・大きな字）

Parent: [WS213](../ws.md)
Status: in-progress（2026-10-12 P4 実装、ユーザーの承認「進め方はおすすめでいいです」と Q1 の依頼。host 試験 PASS・build warning 0、T1・Pi の確かめ待ち）
Phase disposition: normal

## 範囲

- p001 の 5 節。M は cell と metrics だけ、raster の job を W に、walk の始めに終わった job を cell の割り当ての順に書く、未着の字は描かずに印、その写しの layer は無効。壁紙の画素とぼかし・大きな字を W に。

設計は [p001](../phase001/phase.md)。

## 受け入れの条件

1. host: p001 の 9 節 (j) の試験 PASS。
2. build warning 0（下のコマンド）。
3. Pi（1 boot、ユーザーか Q1 の手で）: 日本語の UI で初出の字の多い画面（Settings・App Home の検索、各 10 秒）で `walk_max_us` が p004 より下がる。壁紙の変更（3 回、pointer を動かしながら）の間も `KWL PERF cursor` の `box_lag_p99_us` ≤ 16700 と `box_lag_max_us` ≤ 16700（p004 と同じ条件: 最長の超過の原因が kernel の長い transaction なら WS208・kernel への依存、p001 6.4）、`KWL PERF main` の `pass_max_us` ≤ 33300（review-4 L-d）。
4. T1（i915・Venus）で回帰なし、30 分まで。

## コマンドと上限

- build（warning 0、出力は自分の `build/ws213-p005/` に、review-4 L-b）: `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws213-p005/arm64 build/ws213-p005/arm64/bin/wayland`、`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws213-p005/amd64 build/ws213-p005/amd64/bin/wayland`、`make -j16 KEILAND_ASK=n KEILAND_LINUX_BUILD=build/ws213-p005/linux keiland-linux`（`KEILAND_ASK=n` で package の導入と install を聞かない、`Makefile:476-482`）（各 900 秒まで）。FreeBSD は T1 の guest（`plan/tools/keiland-freebsd/backend-test.sh`）。
- build の注記（p001 review-5 の答え (4)、Q1）: `BUILD=` の override は動き（`Makefile:79`）、libc.so なども自分の dir に作るので最初の build は長い（`vmunix.mk:411`）。LLVM と sysroot は共有のまま使う（wayland は `$(DYNAMIC_SYSROOT)/usr/lib/crt1.o` に依存、`userland-dynamic.mk:239`）。**make が共有の sysroot を作り直そうとしたら（sysroot の規則が動き出したら）止めて main に報告する**。toolchain と共有の `build/` は変えない。
- host 試験は各 120 秒まで。同じ条件で変更なしの再試行は 3 回まで。QEMU・実機は T1 と Q1 経由（担当は QEMU を起動しない）。

## 実行（2026-10-12 P4、agent/p4 09e429a9a）

設計は p001 の 5 節のとおり。W は 2 つ: glyph の worker（新しい thread）と、今ある壁紙の loader の thread に画素とぼかしを足した物。

### glyph の worker（`userland/desktop/wayland/glyph-worker.{c,h}`、新しい file）

- M は今どおり cell（LRU）と metrics（自分の face で `truetype_set_pixel_size`・`truetype_glyph_metrics`）を同期で決め、raster の job（font の slot・glyph・pixel・測った箱・cell・cell の割り当ての番号）を W に出す。W は自分の face を同じ font の bytes の上に開く（face の size は状態なので thread で共有しない。1 番目の font は M と同じ companions（Mahora Bold・mono）も開く）。W は job を出された順に描き、同じ順に返し、M の wake pipe を 1 度だけ書く（取るまで）。W は Vulkan・host image・compositor の物に触れない。
- M（glass.c）: `glass_cache_glyph` は箱のある glyph を W に頼み、cell を `pending` にする（advance は measure に使える）。`glass_draw_glyph_at` は pending の glyph を描かない（印）。layer の絵の guard（`guard_clock`）の間に描かなかった glyph は guard の evictions に数え、その絵は完了の時に落ちて描き直される（`KWL LAYER dropped ... reason=eviction` の数に入る）。
- 取り込み: `kwl_compose_list_begin`（walk の始め、frame と layer の絵の両方）で `kwl_glass_glyphs_take` が終わった job を出した順に atlas の cell へ書く。cell の割り当ての番号（`cell_generation`）が job の物と違えば書かない（同じ cell を 2 度割り当てたら後の方が残る、L-4）。色の emoji の cell も新しい番号を取る（古い job が上書きしない）。描けなかった job の cell は何も描かない（`KWL GLASS glyph ... errno=`）。
- 描き直し: `kwl_glass_glyphs_poll`（main loop の毎 pass、`kwl_compose_poll` の後）が W の結果があれば `server->dirty = 1`（全体を描き直す。dirty の frame は scene の reuse もしない）。
- `KWL GLASS glyph ... pending=1` が W に頼んだ字。W が動かなければ（`KWL GLASS worker unavailable`）今どおり M が描く。`KWL GLASS worker started faces=` は起動の印。
- walk の外の `glass_text_width`（IME の tick など）が atlas を書かなくなった（p001 5 節 H-b の残り）。

### 大きな字（lock 画面の時計の数字）

- `glass_large_prepare` は W があれば、M で 11 字の箱と位置を測って（`large_fill` と同じ並べ方）W に頼み、`EAGAIN` を返す（greeter は atlas の時計の大きさで描く）。全部の字が walk の始めに `large` の image に書かれた時に `large_pixels` が立つ。描けなかった大きさは `large_failed` に覚えて頼み直さない。W の時は quiesce と `vkDeviceWaitIdle` を呼ばない（書くのは飛んでいるコマの無い walk の始め）。

### 壁紙（loader の thread、`glass_loader`）

- `wallpaper_draw` を、何にでも書ける `wallpaper_render`（出力の大きさの行と、ぼかしの行を pitch 付きで）と M の写しに分けた。起動の時は今どおり M が map した image に直に描く（出力の大きさの写しを持たない ws035-p133 のまま）。
- 起動の後の壁紙の変更（`kwl_glass_wallpaper_begin`）は、loader の thread が読み・decode・出力の大きさの画素・ぼかしまで作る（2.4MB の一時の行、1024×600）。M は `kwl_glass_wallpaper_poll` で quiesce の後に行を写すだけ（memcpy）。出力の大きさがその間に変わっていたら、M が今どおり描く。
- landscape（`kwl_glass_landscape`）も loader が空いていれば loader で作る（ログは `path=-`）。loader が別の絵を読んでいる間は今どおり M が描く。

### 確かめ（host、2026-10-12）

- `sh plan/ws213/tests/glyph-worker-host-test.sh` → PASS（17 checks、plain と ThreadSanitizer。本物の libtruetype と tree の Mahora の font）。順: 'A'〜'z' を 4 つの大きさで頼み、出した順に返り、M の自分の face の描画と箱・coverage が同じ。測った箱と違う job は EINVAL、渡していない font は ENOENT で、どちらも coverage なし。取るまで wake pipe は 1 度だけ。2000 個を頼んだままの stop で全部が解放される。cache の再現: 8 cell に 4000 回ランダムに割り当て、ランダムに取り込み（walk の始め）、番号の合う cell にだけ書く → 最後に全部の cell がそれぞれ最後に割り当てた字の coverage を持つ（後の方が残る）。TSan の報告 0。
- 他の host 試験（input-thread・cursor-due・list-replay・render-thread）PASS。
- build warning 0: rpi4（`build/arm64/bin/wayland`）・amd64（`build/amd64/bin/wayland`）・keiland-linux（`build/keiland-linux`）。style-check: 新しい・触った所の指摘 0（glass.c の残る 2 件は元から）。
- host で試していない物: glass.c の組み込み（pending の字を描かない、guard への数え、walk の始めの取り込み、大きな字、壁紙の写し）。読んで確かめた。T1・Pi で見る。

### 残り・制限

- 窓の絵の cache（`kwl_glass_draw_desktop_picture`・anim の絵）は guard の外なので、pending の字の無い絵が animation の間だけ使われ得る（1 回の animation の間の字の欠け）。layer の絵（App Home）は guard で描き直す。
- 初めて出る字はその frame では描かれず、W が描き終えた次の frame から出る（設計どおり、1〜2 frame）。
- W の face は M と別に font を開くので、companions（Mahora Bold・mono）の分だけ memory が増える。fallback（日本語）の font の bytes は M と共有。

### T1 と Pi で見る事

- T1（i915・Venus、30 分まで）: shot.c の画が前と同じ（起動の直後の初出の字は 1 frame 遅れて出るので、shot は落ち着いた後）、`KWL GLASS worker started` が出る、`KWL GLASS glyph ... errno=` が無い、App Home の検索で日本語を打って字が出る、lock 画面の大きな時計の数字が出る（`KWL GLASS large pixels=`）、壁紙の変更（絵と landscape）で `KWL GLASS wallpaper path= ms=` が出て絵が変わる。`KWL VULKAN_ERROR`・`KWL FAILED` なし。Linux の TSan の guest（p004 の受け入れ 3 と一緒に）で W と loader の thread に触れる報告 0。
- Pi（受け入れ 3）: 日本語の UI で Settings・App Home の検索（各 10 秒）の `KWL PERF pass` の `walk_max_us` が p004 より下がるか、壁紙の変更 3 回（pointer を動かしながら）の `KWL GLASS wallpaper ... ms=`（M の写しの時間、前は 900ms 前後）と `KWL PERF cursor`・`KWL PERF pass`（`KWL SLOW_PASS` の部分）。
