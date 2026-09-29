<!-- awesome-plan project=zedbsd record=ws081-p013 -->

# ws081-p013: Notes の指の scroll・pinch（ペンは線）

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-29、WS081 の作業用サブエージェント。Notes の wl_touch、拡大した頁の慣性の scroll・rubber band、二本指の拡大、double tap、toolbar の tap、掌の判定、頁の picture の拡大への対応。host 試験と QEMU の guest 試験（main の pen の image）。掌の guest 試験は injector の制約で不可（下）、実機は未実施）
Disposition: normal
Parent: [WS081](../ws.md)
Queue: main の指示（2026-09-29「p013（Notes の指の線と、指の scroll・pinch）: デモの中心なので最優先。ペンは線、指は scroll・pinch（palm と指の区別の方針は design に従う）。userland/desktop/notes/ の変更を許可」）。Awesome Plan の Queue の item ではない
Resume point: なし（次は main の指示どおり p010）
<!-- awesome-plan-current:end -->

## 範囲と決定

design §3.8・§5.6 は「Notes は指で線」でしたが、main の指示「ペンは線、指は scroll・pinch」に従って改めました（design §5.6 に追記）。

- 指は線を描きません。Notes が wl_touch を bind したので、compositor は指を pointer の代わりとして Notes に渡さなくなりました。
- 指の担当
  - 一本指: 拡大した頁の scroll（libkeiland の scroller。慣性、端の rubber band、catch）。
  - 二本指: 距離が 5% 変わったら拡大を始め、指の間の頁の点を保ちます。倍率は頁全体の 1〜4 倍で、頁の picture が 4096 px を越えない範囲です。
  - double tap: 頁全体と 2 倍を切り替えます。
  - toolbar の tap: その button を押します。toolbar から始めた drag は何も動かしません。
- 線はペン（tablet）と pointer（mouse）だけが描きます。
- 掌の方針（design には app 側の規則が無かったので、この Phase で決めて design §5.6 に書きました）
  - touch screen が信頼しない接触（HID Confidence 0）は、kernel の touch の状態機械が離します（WS079 p012）。
  - Notes は、ペンが窓の近く（tablet の proximity）にある間と、離れて 500 ms の間は、新しい指を掌として追いません。
  - ペンが近づいた時に追っている指は cancel し、その指は離れるまで追いません。
  - glide している頁はペンが近づいたらその場で止めます（ペンが指す所に書けるように）。
- 頁全体（倍率 1）の位置は、`notes_view_layout` と同じ float の計算で 1 px も変わりません。
- 別の頁を表示したら、倍率はそのままで頁の上端から始めます。

## 実装

| file | 内容 |
| --- | --- |
| `userland/desktop/notes/touch.h`・`touch.c`（新） | 上の決定。Wayland と Vulkan に依存しない（host で試験できる）。`notes_touch_layout`（窓・toolbar・余白・頁・fit の scale）、`_event`、`_pen`（ペンの近さ）、`_tick`、`_view`、`_take_tap`、`_top`、`_clock`。`NOTES TOUCH …` の log（drag・release・rest・caught・tap・double-tap・pinch start/end・palm・palm cancel・stop・cancel） |
| `window.c`・`app.h` | seat の TOUCH の capability で `wl_touch` を bind（version 5 まで）。down・up・motion・cancel を touch の別の queue（256）に入れ、受け取った時刻（µs）を付ける。閉じる時に `wl_touch_destroy` |
| `main.c` | 指の event を `notes_touch_event` に渡し、toolbar の tap を `notes_ui_hit` で押す。tick の待ちを loop の timeout に入れる。ペン（tablet）の入力の source から、ペンの近さを `notes_touch_pen` に知らせる。描く時は fit の layout から touch の layout を作り、その位置で描く。最後に描いた位置・zooming と違えば描き直す（double tap のように event の中で変わった場合も含む）。二本指の拡大の間は、今ある頁の picture を新しい倍率に伸ばして描き、指が離れたら描き直す。`NOTES LAYOUT` の行は、指も glide も止まった時に、前に出した値と違えば出す（WS079 の試験の読み方を保つ） |
| `render.c`・`app.h` | 頁の picture に専用の stencil buffer を持たせ、上限を窓の大きさから `NOTES_PICTURE_MAX`（4096）に変えた。拡大した頁は窓より大きく、窓の大きさの共有の stencil では頁の途中で picture が切れていた（guest の画面で見つけた）。stencil の作り方は `render_stencil_image` に出して、窓と頁で共有 |
| `Makefile` | `touch.c` |
| `userland/base/tests/touchinject/main.c`（main の依頼、BUG-099） | 各 exit の経路で理由を stderr に出す。replay の成功で `touchinject: done lines=N`、途中の中断で行と理由、script の読みの失敗・既定の screen の失敗・`close` の失敗、`-c`・`-s`・`-d`・`-t` の失敗 |
| `plan/ws081/tests/p012-guest.sh`・`p013-guest.sh`（BUG-099） | 共通の `inject` を通す。`replay=0` の行が無ければ、SSH の状態と出力の全部を `OUTDIR/SCRIPT.failed.log` に残す。build の touchinject を guest に置いてから走る |
| `plan/bugs/BUG-099.md`・`plan/known-bugs.md` の BUG-099 の行 | 診断の追加を記録（unreproduced / tracking のまま） |
| `plan/ws081/tests/host-notestouch.c`・`run-notestouch.sh`・`p013-guest.sh`（新） | 試験（下） |

## 確認（実行したもの）

| 確認 | 結果 |
| --- | --- |
| `plan/ws081/tests/run-notestouch.sh`（touch.c と libkeiland、`-Wall -Wextra -Werror -Wconversion`） | `host-notestouch: ok (30 checks)` |
| 同上、`EXTRA_CFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -O1"` | ok（30 checks） |
| 同上の中身 | 倍率 1 の位置が `notes_view_layout` と一致し、drag で動かない。二本指の拡大: 比 2 で約 1.85 倍、指の間の点が 1 pt 以内。4 倍で止まり、この時は横も scroll するので両軸の点が保たれる。頁全体より小さくならない。3% の変化では拡大しない。拡大した頁は flick に付いて動き、glide して止まる。上端の rubber band と戻り。double tap で頁全体と 2 倍（縦の点を保ち、横は収まるので中央）。toolbar の tap は 1 回だけ取れ、頁の tap は取れない。toolbar からの drag は動かさない。ペンが近い時と離れて 200 ms の指は掌（動かない）で、離れて十分後は動く。ペンが drag を cancel し、glide を止める。別の頁は上端から |
| mutation（scratchpad の script、10 個: 掌の時間、4 倍の上限、5% の閾値、double tap の倍率、toolbar の判定、ペンの近さ、拡大の下限、fling の速度、anchor の x・y） | 9 個が FAIL。残った 1 個（`touch_zoom_about` の下限 1 を 0.5 に）は、`notes_touch_layout` が毎 frame 同じ下限で抑えるので同値 |
| `make -j16 ZEDBSD_CONFIG=plan/ws079/tests/config-amd64-pen.mk BUILD=build/amd64 build/amd64/bin/notes build/amd64/bin/touchinject build/amd64/dynamic/libkeiland.so`（`-Werror`） | rc=0、`warning:`・`error:` 0 |
| [p013-guest.sh](../tests/p013-guest.sh)（main の `build/main-pen/hdd-image.img` を worktree へ複写して起こし、notes・libkeiland・libpdf・libz-compat・libjpeg-compat・touchinject を SSH で置く。image の compositor。Notes fullscreen 1280x800） | **PASS**（最後の実行）。二本指 200→400 px で 1.912 倍。拡大した頁の flick は 1003 px/s で、離した後に 322 px glide して止まった。ペンの線は `NOTES STROKE` 1 本、指の drag は線を描かない。ペンの接近が Notes に届く（`NOTES HOVER source=1`）。double tap で `zoom=1.000`。toolbar の「+ Page」の tap で `NOTES PAGE current=1 count=2 new`。compositor の ERROR/FAILED 0 |
| guest の touchinject（新しい build） | 良い台本は `touchinject: done lines=3` で rc 0。悪い台本は `line 2: bad command: move 9 1 1`・`stopped at line 2` で rc 1。`-c` 14/14、`-s` 15/15 |
| `plan/tools/style-check.py`・`style-extra.py`（touch.c・touch.h・main.c・window.c・render.c・app.h・touchinject・host-notestouch.c） | 指摘 0。`window.c:160`・`:933` の joined-check は既存の行で、この Phase では変えていない |

画面（QEMU）は `/home/awe/zedBSD-rpi4/build/ws081-shots/` の `ws081-p013-20260929-*.png` です。

- `-whole.png`: 頁全体
- `-zoomed.png`: 二本指で拡大
- `-flicked.png`: glide の後
- `-pen.png`: 拡大した頁にペンの線
- `-double-tap.png`: 頁全体に戻り、線は縮尺どおり
- `-new-page.png`: toolbar の tap で 2 頁目

Notes の log は worktree の `build/ws081-p013-guest/notes-log.txt` です。

## 経過と判断

- 1 回目の guest 試験の画面で、拡大した頁の白い picture が頁の途中で切れ、ペンの線も一部しか見えなかった。原因は、頁の picture が窓の大きさの共有の stencil のため窓の大きさまでに切られていたこと。頁の picture に専用の stencil を持たせて直した（上の表）。
- double tap の後の画面が拡大のまま残っていた。double tap は event の処理の中で倍率を変えるので、tick の前後の比較では描き直しが起きていなかった。最後に描いた位置と比べる形に直した。
- 掌の guest 試験（ペンの hover 中の指）は、試験の injector が一度に一つの device しか開けない（二つ目は `EBUSY`）ためできない。掌の判定は host 試験で確かめ、guest では掌の判定が頼る「ペンの接近が Notes に届くこと」だけを確かめた。
- guest の Notes は journal から前の回の notebook を戻していたので、試験の始めに journal の folder（`${XDG_DATA_HOME:-$HOME/.local/share}/keiland/notes`）を消すようにした。

## 未実施・制限

- 実機（touch LCD とペン）の掌の判定・手触りは未確認（p007）。500 ms・5% の係数は既定。
- 拡大の間の伸縮は Vulkan の sampler の線形の補間で、指が離れると描き直す。4096 px の picture と同じ大きさの PDF の背景の画像（host の linear の image）が device の上限の内にあるかは、Venus の QEMU でしか確かめていない。
- 横の swipe で頁をめくる動きは入れていない（main の指示は scroll・pinch）。
- boot test は main に依頼する（subagent は image を build しない）。

## 残り

- p010（Files）→ p011（Terminal）は main の指示どおり次に行う。
