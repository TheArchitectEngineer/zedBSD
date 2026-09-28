<!-- awesome-plan project=zedbsd record=ws081-p012 -->

# ws081-p012: PDF Viewer への適用（scroll と慣性、二本指の拡大）

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-29、WS081 の作業用サブエージェント。PDF Viewer の wl_touch、慣性の scroll・rubber band・catch、二本指の拡大、page mode の swipe。host 試験と QEMU の guest 試験。boot test は未実施（下）、実機は未実施）
Disposition: normal
Parent: [WS081](../ws.md)
Queue: main の指示（2026-09-29「p005 の後 p012（PDF Viewer への適用、`userland/desktop/pdfviewer/` を許可）。QEMU の guest で touchinject の flick で慣性の scroll を確認」）。Awesome Plan の Queue の item ではない
Resume point: なし（p010・p011・p013 は main の指示の後）
<!-- awesome-plan-current:end -->

## 範囲

design §5 の scroller と gesture（p005 の libkeiland）を PDF Viewer に入れる。PDF Viewer は wl_touch を bind する（design §4.1 の (a)。compositor は wl_touch を持つ client に
指を pointer として渡さない）。Files・Terminal・Notes と compositor は変えない。HAL・toolchain・kernel は変えない。

## 設計（この Phase で決めたこと）

| 指の場所・数 | 動き |
| --- | --- |
| 頁の上の一本指（scroll mode、page mode の縦・拡大した頁） | `keiland_gesture` の drag の差（frame の時刻に resampling）で `keiland_scroller` を動かし、`scroll_x`・`scroll_y` をその位置にする。端の外は rubber band、離せば fling（τ 0.45 s・μ 300 px/s²）、端を越えた fling とばね、動いている頁の touch は catch（tap にしない） |
| page mode の横の一本指（頁が横に収まるとき） | swipe: `app->swipe` を指の差（両端の外は 1/3）にし、離した時の速度（px/ms）と距離で、pointer と同じ判定（`pv_app_swipe_end`）で頁をめくる |
| 二本指 | 距離が 5% 変わったら拡大を始め、その時の比からの比で倍率を変える（跳ばない）。始めた時に指の間にあった文書の点（`pv_place`: 頁と頁の中の point）を、指の間の今の点に置く（拡大と二本指の pan が同時に効く）。拡大の間は frame が頁の raster を最近傍で伸縮して描き（`pv_canvas_stretch`、`app->zooming`）、指が離れたら新しい倍率で描き直す |
| double tap | fit の時は tap の点を保って 2 倍、拡大している時は mode の fit へ |
| sidebar・file chooser・password card の上の最初の指 | pointer の左 button の代わり（down で press、motion、up で release、cancel は click しない）。その間は他の指を取らない |
| long press | 何もしない（PDF Viewer に context menu が無い。log だけ） |

- scroller は touch から頁が止まるまで view を持つ。各 tick で最後に書いた位置と比べ、key・wheel・action・resize が view を動かしていたらそれを引き取る（glide を止め、指が
  あればそこから drag を続ける）。
- 範囲（`0..content − view`）は変わった時だけ scroller に渡す（同じ範囲を渡し直すと drag の最中の軸が止まるため）。
- 指の間と glide の間は `app->touching` で先読みの rasterize（`pv_app_prefetch`）を止める（指に対する応答を遅らせない）。
- wl_touch の時刻（compositor の ms、低い 32 bit）は、受け取った時刻（µs）を基準に 64 bit の µs に戻す（2 s より離れていれば受け取った時刻）。
- `view.c`・`draw.c`・`canvas.c`・`viewer.h` は WS079 の host 試験が C89・keiland 無しで build するので、C89 のまま、libkeiland に依存しない関数だけを足した。
  libkeiland を使うのは target だけで build する `touch.c` と `window.c`・`main.c`。

## 実装

| file | 内容 |
| --- | --- |
| `userland/desktop/pdfviewer/touch.h`・`touch.c`（新） | 上の設計。`pv_touch_open`・`close`・`event`・`tick`・`clock`。`PDFVIEWER TOUCH …` の log（drag・release・rest・caught・tap・double-tap・long-press・pinch start/end・cancel） |
| `window.h`・`window.c` | seat の TOUCH の capability で `wl_touch` を bind（version 5 まで、shape・orientation は NULL）。down・up・motion・cancel を touch の別の環（256）に入れ、受け取った時刻（µs）を付ける。`pv_window_take_touch`。閉じる時に `wl_touch_destroy` |
| `main.c` | `pv_touch` を viewer と一緒に作る。loop で touch の入力を渡し、tick の待ち時間を loop の timeout に入れ、frame の前に tick する |
| `view.c`・`viewer.h` | `pv_app_clamp`・`pv_app_zoom_to`・`pv_app_page_left`・`pv_app_place_at`・`pv_app_show_place`・`pv_app_swipe_end`（pointer の release の判定を関数に出した。log の `SWIPE …` は同じ）、`struct pv_place`、`pv_app` の `touching`・`zooming`。prefetch は touching・zooming の間は休む |
| `draw.c`・`canvas.c` | zooming の間、raster のある頁は `pv_canvas_stretch`（最近傍）で描く（`shown_flags` も集める） |
| `Makefile` | `touch.c` |
| `plan/ws081/tests/host-pdftouch.c`・`run-pdftouch.sh`・`make-touch-pdf.py`（新） | host 試験（下） |
| `plan/ws081/tests/p012-guest.sh`（新） | guest 試験（下） |
| `plan/ws081/tests/host-scroll.c`（p005 の試験） | 全文の規約 §3（関数の定義の引数を 1 行に 1 つ、static 関数の前方宣言、main を先に、main の複数行の説明）に合わせた。中身は不変 |

## 確認（実行したもの）

| 確認 | 結果 |
| --- | --- |
| `plan/ws081/tests/run-pdftouch.sh`（viewer の core は C89・`-Werror`、touch.c と libkeiland は gnu11・`-Werror`、plain と ASan+UBSan） | 両方 `host-pdftouch: ok (33 checks)` |
| 同上の中身 | flick（3000 px/s）: 離した時は描いていた位置、離した後も進み、fling の式の距離（±15%）で止まる。上端の rubber band（240 px 引いて 80〜132 px）と 1 s 以内の戻り、端への fling（越えて端にちょうど戻る）、catch（止まり、tap も拡大もしない）、cancel（glide しない）、glide 中の End（view を引き取る）、二本指（5% で始まり、比 2/開始の比で拡大、指の間の文書の点が 1 pt 以内で保たれる、拡大の間は raster を描き直さない、終わりに新しい倍率で描き直す）、double tap（fit へ、2 倍へ、tap の点を保つ）、page mode の flick で次の頁、短く遅い drag は戻る、sidebar の thumbnail の tap で頁（pointer の代わり） |
| WS079 の `run-pdf-render.sh 50`（notes.pdf を作るため）と `run-pdfviewer-host.sh`（view.c・draw.c の回帰、plain と ASan） | どちらも ok（host-pdfviewer 51 項目すべて ok） |
| `plan/ws081/tests/run-scroll.sh`（p005、整形の後） | `host-scroll: ok (87 checks)` |
| `make -j16 ZEDBSD_CONFIG=plan/ws079/tests/config-amd64-pen.mk BUILD=build/amd64 build/amd64/bin/pdfviewer build/amd64/dynamic/libpdf.so build/amd64/dynamic/libkeiland.so`（`-Werror`） | rc=0、`warning:`・`error:` 0、toolchain の展開・build 0 件 |
| [p012-guest.sh](../tests/p012-guest.sh)（`VENUS_RENDERER=…/build/ws035-sq-venus/install` の pen の guest に compositor・pdfviewer・libkeiland・libpdf・libz-compat・libjpeg-compat と文書を置く） | **PASS**。flick: 一本指を 100 ms で 300 px 上へ（60 Hz）、PDF Viewer の log で drag が scroll、離した時の速度 2942 px/s、離した後に 1134 px 進み、2942 px/s の fling の止まる距離（1134.0 px）に 1133.8 px で止まった（位置は時刻の関数なので QEMU の遅い frame でも同じ）。二本指: 200→400 px で倍率 1.582→3.000（1.90 倍）。page mode の左への flick: `SWIPE offset=-167 velocity=-2.50 direction=1`、`PAGE shown=1`。compositor の ERROR/FAILED 0 |
| `plan/tools/style-check.py`・`style-extra.py`（WS079 の写し）: touch.c・touch.h・view.c・draw.c・canvas.c・window.h・main.c・viewer.h・両方の host 試験 | 指摘 0。`window.c:176` の joined-check（`window->compositor == NULL \|\| window->shell == NULL`）は既存の行で、この Phase では変えていない |

画面（QEMU）: `/home/awe/zedBSD-rpi4/build/ws081-shots/ws081-p012-20260929-flick-before.png`・`-flick-after.png`（2 頁目まで glide）・`-pinch-after.png`（拡大）・
`-swipe-after.png`（page mode の 2 頁目）。host の frame: `-host-rubber-band.png`（上端の rubber band）・`-host-pinch-stretched.png`（拡大の間の伸縮）。
guest の log は worktree の `build/ws081-p012-guest/`（`flick-log.txt`・`pinch-log.txt`・`swipe-log.txt`）。

## 経過と判断

- guest 試験の 1 回目は flick だけ PASS し、二本指と swipe の指が PDF Viewer に届かなかった。compositor の log で、2 回目以降の touchinject の screen は
  `ZWL TOUCH added` の後に報告が 1 つも読まれていなかった。同じ台本を続けて流すと 3 回目（2 s 待った後）だけ届いた。compositor は新しい evdev の node を
  時々探すので、宣言から 300 ms で触れた指は node が開かれる前に過ぎていた（1 回目は偶然間に合った）。台本の最初の待ちを p004 と同じ 2600 ms にして PASS。
  製品の不具合ではなく試験の台本の問題。
- host 試験の二本指で、止まった後に報告の無い指は、motion の予測が最後の速度で最大 E（12 ms）分先に残る（design §3.3 の打ち切りは `τ_n + E + T̂/2`）。
  USB の touch screen は触れている間は止まっていても毎 scan 報告する（p004 の guest でも止まった指の報告が compositor に届いた）ので、試験の指も止まった後に
  同じ位置を報告し続ける形にした。報告の無い止まった指の扱いは下の「残り」。

## 未実施・制限

- **boot test は未実施**。pen の image を作り直す `make disk-image` の dry-run（`build/ws081-p012-image-dry.log`）が `userland/base/noct/noct` の cmake の build を
  含んでいた（main の merge で noct が変わったため）。AGENTS.md の toolchain（`userland/base/noct/` の build の規則）に当たるので、subagent では走らせず、main に
  image の build と boot test を依頼する。guest 試験は起動した guest に新しい compositor・libkeiland・pdfviewer を置いて動かしたもの。
  あわせて、worktree の `userland/base/noct/noct/build-zedbsd-amd64` が 02:08 付けで、p004 の image の build（[phase004](../phase004/phase.md)）の時に noct が
  worktree の中で build されていた可能性がある（共有の `build/NoctLang` ではなく worktree の source の中）。main に報告する。
- 実機（touch LCD）は未確認（p007）。滑らかさの数値は QEMU では測れない（p004 と同じ）。
- 拡大の間の伸縮は最近傍（指が離れると描き直す）。sidebar・chooser・card の上は pointer の代わりで、sidebar の列に慣性は無い。long press は何もしない。
- guest の窓のタイトルバーに文字が出ていない（画面）。pen の image にフォントが入っていないためと見られ、この Phase の変更とは無関係（調べていない）。

## 残り

- 止まったまま報告の来ない指（evdev が同じ値を落とす panel）の予測の尾を戻す規則（`now − τ_n` が `2.5 T̂` を越えたら最後の点へ）の要否を p007 の実機で確かめる
  （motion library、p003 の範囲）。
- main: pen の image の作り直し（noct の build を含む）と `plan/tools/boot-test.sh`。

## main の検証（2026-09-29、merge の後）

main の checkout（4e850cdd）で zdesktop の image と pen の image を作り直した（batch120）。どちらも boot test PASS（`build/main-batch120-boot1/login.png`、
`build/main-batch120-penboot/login.png`）。pen の構成は pdfviewer を image に入れないので、試験の BUILD に
`make ZEDBSD_CONFIG=plan/ws079/tests/config-amd64-pen.mk BUILD=build/main-pen build/main-pen/bin/pdfviewer …/libpdf.so …/libkeiland.so …/libz-compat.so …/libjpeg-compat.so`
で足した（warning 0）。`p012-guest.sh build/main-pen`: 1 回目は段 3 の replay の成否の行が無く FAIL（効果の検査は全部 ok、[BUG-099](../../bugs/BUG-099.md)）、
再実行で PASS（flick 2942 px/s、1133.8 px で停止、pinch 1.582→3.002、page 2）。WS079 の p012-guest も PASS。QEMU の証拠だけ、実機は未実施。
