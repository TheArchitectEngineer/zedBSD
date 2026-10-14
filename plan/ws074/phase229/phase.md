<!-- awesome-plan project=zedbsd record=ws074-p229 -->

# ws074-p229: UAT のアドレスバー（compositor 側: 既定の URL、箱の幅、key repeat）

P2 (2026-10-14)。親: [WS074](../ws.md)。前段: [p228](../phase228/phase.md)（B1 の調べ、browser の `MAIN_HOME_PAGE`）。
Status: **cleared**（2026-10-14 Q1 判定: T1-618 で 1〜3・5 が PASS（QEMU、main eea58c75c）、4 の titlebar-p010 の FAIL は試験の古さ（ws181-p009 の pill の配置）で e5c6f83b6 で試験を直し T1-620 で p010・p011・p013 PASS（main 7b5626717）。実機は未実施、ユーザーの UAT 待ち）。

## ユーザーの指摘（2026-10-14）

「アプリ起動時のデフォルトのURLをgoogle.comにしたいです。」「Webブラウザアプリで、添付のようにアドレス入力のボックスが小さくなって編集がしづらいです。また、アドレスバーではキーボードリピートが効かないようです（QEMUなのでそのせいかも？）」
添付: titlebar の左に題「https://www.amazon.c…」、中央のアドレスの箱「https://www.a…」は約 130 px、右に広い空き。

## 原因

1. **既定の URL**: browser は引数なしで https://www.google.com/ を開く（p228）が、launcher が start.html を引数で渡していた（`userland/desktop/wayland/apps.conf` の Browser の行、`home.c` の組み込みの一覧の `HOME_BROWSER_START`）。
2. **箱が小さい**: browser の Location は BREADCRUMB の control で、段（parts）は scheme の後の host と path（amazon なら「www.amazon.co.jp」1 段）。compositor の `shell_layout` は breadcrumb の幅を**段の文字幅（`shell_crumb_full`）までに制限**しており、編集中（field が URL 全体を表示）も同じ幅のままだった。130 px ≒「www.amazon.co.jp」の幅。加えて field の text は横に scroll せず、右端で「…」に切られ、URL の末尾の cursor が見えなかった。
3. **repeat が効かない**: QEMU のせいではない。titlebar の field は compositor が持つ text field で、app の repeat（client が `wl_keyboard.repeat_info` で自分で繰り返す）の外にある。kernel の autorepeat（value 2）は input-thread が転送しない（`input-thread.c:948`）ので、field には押下 1 回しか届かず、repeat の timer も無かった。

## 直したこと

- `userland/desktop/wayland/apps.conf`・`home.c`: Browser の command を `/bin/browser`（引数なし）に。`HOME_BROWSER_START` を削除。`plan/ws035/demo/apps.conf`（完了した WS035 の demo の一覧、同じ行を持つ。Q1 の「start.html の他の参照も追従」の指示で直した）も同じ。
- `userland/desktop/browser/Makefile`: data の comment を実態に（start.html は引数で開く network 不要の page、引数なしは home page）。
- **start.html は残す**: `about.html`・`text.html` が「Back to the start page」で link し、`plan/ws074/tests/browser-start.sh` が引数で開く（network の無い試験の起点）。launcher の既定から外しただけ。
- `userland/desktop/wayland/titlebar-shell.c`:
  - **幅**（`shell_layout`）: breadcrumb の path を編集している間（field が keyboard を持つ間）は、段の幅の上限を外し、**残りの room 全部**を取る。編集中の breadcrumb は give way しない（keyboard を持つ search と同じ）。編集中の最小幅は `CRUMB_LEAST`（120 px）。編集していない breadcrumb（Files などの段の表示）は従来どおり段の幅まで（見た目・hit は変わらない）。
  - **狭い窓の縮み方**（`kwl_titlebar_title_limit`）: controls の窓でも tabs と同じく、controls が望む幅（`shell_controls_need`: 各 control の幅、search は field、breadcrumb はその最小、"..."）が題の 1/5 の後に入らなければ、**題が先に縮む**（最小 `TITLE_LEAST` 56 px、1/5 より大きくしない）。その後は従来の順（search が button に → 右の normal の control が "..." へ → breadcrumb。編集中の breadcrumb は残る）。定数 `TAB_TITLE_LEAST`・`TAB_TITLE_GAP` は共用のため `TITLE_LEAST`・`TITLE_GAP` に改名。
  - **field の横 scroll**（`shell_field_scroll`・`shell_draw_field_text`・`shell_field_at`）: field の text が幅より長い時は、cursor（と IME の preedit）が見える位置から描く。表示の先頭（byte offset、文字境界）を `field.scroll` に持ち、cursor が右端を越えたら進め、先頭より左へ行ったら戻し、text が短くなったら末尾まで入る所まで戻す。収まる文字だけを描く（右端の「…」は付けない。cursor が端に居ても文字が隠れない）。click の位置→cursor も scroll を足して求める。search field も同じ関数なので同じく scroll する。
  - **key repeat**（`kwl_titlebar_key`・`kwl_titlebar_tick`・`kwl_titlebar_key_released`、本体は `shell_field_press` に分けた）: field が取った key（文字・BackSpace・Delete・矢印・Home/End、候補の ↑↓）を、押している間 **compositor の timer** で繰り返す。値は app に `repeat_info` で渡す `server->repeat_delay_ms`・`server->repeat_rate`（既定 400 ms・25/s、Keiland の設定 `keyboard.repeat.delay/rate` で変わる、rate 0 は repeat なし）。間隔の計算は IME（`userland/desktop/ime/method.c` `program_repeat_due`、BUG-172）と同じ（次は「予定 + 間隔」、1 間隔以上遅れたら「今 + 間隔」で、まとめ打ちしない）。Enter・Esc・Tab は繰り返さない。modifier（Shift 等）の押下は repeat を止めない（Shift+矢印で選択を伸ばせる）、他の key の押下は止めて新しい key の repeat を始める。各回の押下は今の modifier で行う。repeat の 1 回の hold の終わりに `KWL TITLEBAR repeat key=K count=N` を出す（試験が読む）。
    - **kernel の value 2 を使わない理由**: (a) kernel の速さ（evdev の既定 250 ms/33 ms）は Keiland の設定の delay・rate と合わない。app（client 側の repeat）・IME（`repeat_info` で自分で繰り返す）と同じ値・同じ方式にそろえるには compositor の timer が要る。(b) input-thread は value 2 を全ての経路で捨てる（super-tap 等が repeat を区別しない作り）ので、転送を足すと他の入力に波及する。
    - main loop（`main.c`）の pass ごとに `kwl_titlebar_tick` を呼ぶ（poll は最長 10 ms なので、誤差は最大約 10 ms）。field が keyboard を失った時・lock/login/App Home が key を取る時・設定で rate 0 にした時に止まる。押している key の release は `kwl_seat_key` の先頭で必ず見る（`kwl_titlebar_key_released`）ので、menu や App Home が release を取っても repeat は残らない。IME が field に仕える時は、IME が使わずに返した key（virtual keyboard 経由の押下）をこの timer が繰り返す（IME は返した key を自分で繰り返さない設計、`method.c`「goes back to the application, which repeats it itself」）。
- `titlebar.h`: `kwl_titlebar_key_released`・`kwl_titlebar_tick` を宣言。`seat.c`: release の hook。`main.c`: tick の呼び出しと `titlebar.h` の include。

## 追加: 編集していない時も箱（2026-10-14 ユーザーの判断）

ユーザー:「編集していない時のアドレス欄は、Chrome のように常に箱で見せます。」（Q1 経由）

- **選び方**: control の宣言で「常に箱」を選べるようにした。browser の Location だけが選び、Files など他のアプリの breadcrumb は今の見た目（段と矢印）のまま。理由: compositor が app_id で browser を見分ける作りは他の client（将来の別の browser・file の path の欄）に使えず、意味が control の model に無い。新しい request を足さず、既存の `set_control_value`（今は PROGRESS だけ）を BREADCRUMB にも開き、値 `KWL_BREADCRUMB_BOXED`（1）で「常に箱」を表す。
  - protocol: `kl_titlebar_v1`（と manager）を **version 5** に上げた（`wayland/protocol.c`、`libwayland/titlebar-protocol.c`）。compositor（`wayland/titlebar.c`）は version 5 以上の client の BREADCRUMB の値 0・1 を受け、他は従来どおり protocol error。古い version で bind した client（titlebar-probe の error の試験は version 1）は変わらない。
  - libkeiland: `KL_BREADCRUMB_BOXED`（`keiland.h`、**KL_VERSION 94**）。`kl_titlebar_set_control_value`（と `kl_window_set_control_value`）が BREADCRUMB に 0・1 を受け、古い compositor（version 5 未満）には `ENOTSUP` を返す（送らないので client が切られない）。
  - browser（`shell/titlebar.c` `titlebar_state`）: Location に `KL_BREADCRUMB_BOXED` を毎回の状態の表示で設定（controls を宣言し直した後も付く）。`ENOTSUP` は無視して段の表示のまま。
- **見た目**（`titlebar-shell.c` `shell_draw_crumb_box`）: 角丸の箱（白 0.55、pointer の下で 0.72、編集中の field は 0.95）。中に client の text（URL 全体）を field の text と同じ位置（`CRUMB_TEXT_INSET`）に、幅を越える分は「...」で切る。text が無ければ placeholder を薄く。段は使わない（URL を出す）。
- **幅**: 編集中と同じ規則。`shell_crumb_field`（編集中か「常に箱」）なら段の幅の上限を外して空きを全部取り、give way せず、最小は `CRUMB_LEAST` 120 px、狭い窓では題が先に縮む（`shell_crumb_least`・`shell_controls_need` も同じ判定）。箱と編集中で幅が変わらないので、click で箱が跳ねない。
- **click で編集**: 箱全体を field の hit として記録し、press→release（動かさない）で `shell_act` が path の field として keyboard を渡す（`shell_focus(..., edit=1)`、log `KWL TITLEBAR focus ... id=4 edit=1`）。最初の click は **URL 全体を選択**（cursor は先頭、Chrome の address bar と同じ。打てば置き換わる）。編集中の 2 回目の click は従来どおり cursor を置く・drag で選択。press が動けば従来どおり窓の移動。
- build（`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/bin/wayland build/amd64/bin/browser`、log `build/ws074-p229/build3.log`）: exit 0、warning 0（libkeiland・libwayland・compositor・browser を含む）。Linux の flag の `-fsyntax-only`（`titlebar-shell.c`・`wayland/titlebar.c`・`protocol.c`・`libkeiland/titlebar.c`・`libwayland/titlebar-protocol.c`）全て ok。
- 計画の文書: [WS070 の titlebar の設計](../../ws070/titlebar-design.md) の §2.2 の表（request 8 `set_control_value` の意味、version 5）は WS070 の file なので未更新。Q1 に更新を依頼する。

## 確かめ（host）

- build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/bin/wayland`（worktree の build/、log は `build/ws074-p229/wayland-build.log`・`wayland-build2.log`）→ exit 0、warning 0（全 374 の compile が `-Wall -Wextra -Werror`）。
- Linux の flag（`make -n keiland-linux` と同じ `cc -Wall -Wextra -Werror`）で `main.c`・`seat.c`・`home.c`・`titlebar-shell.c` を `-fsyntax-only` → 全て ok。FreeBSD は未実施。
- `git diff --check` 0。
- host の試験: titlebar の layout・field・repeat を host で試す既存の試験は無い（`titlebar-shell.c` は compositor の glass・model に依存し、host の harness が無い）。新設はしなかった。確かめは下の T1 の AAT。

## T1 への依頼（Q1 経由、QEMU の Venus guest）

image: この Phase の commit を含む tree で `plan/ws074/tests/build-browser-image.sh build/<T>`（`config-amd64-browser.mk`、compositor の package の `/etc/keiland/apps.conf` が入る）。guest は network（google.com）が要る。zdesktop を `--glass` で起動。

1. **launcher から既定の URL**: App Home を開き Browser を起動（click か検索で「browser」→ Enter）。PASS: zdesktop の log に `KWL HOME launch name=Browser pid=N` があり、`ps` の browser の command line に引数が無い（`/bin/browser` だけ）、browser の log に `ZBROWSER TITLEBAR ... path=https://www.google.com/` が出る。PNG（google.com のページ）を返す。
2. **箱の幅**: 1280x800 の画面で browser の窓（既定の大きさ）の Location を click（または Ctrl+L）で編集にする。PASS: `KWL TITLEBAR focus ... edit=` の後の `KWL TITLEBAR control client=C surface=S where=floating id=4 ... width=W` の W（browser の Location は id 4）が、編集前の幅より大きく、Back/Forward の右から Reload（と "..."）の左までの残り（目安 400 px 以上）。PNG（編集中の titlebar）を返す。長い URL（例 https://www.google.com/search?q=0123456789012345678901234567890123456789012345678901234567890123456789）を開いて編集にし、End を押した時に cursor と末尾が見える PNG も。
3. **repeat**: 編集中の field で ← を 1.5 秒押し続ける（QMP の key down、1.5 s 待ち、key up）。PASS: zdesktop の log に `KWL TITLEBAR repeat key=105 count=N`（N は 20 前後、少なくとも 10）。BackSpace（key=14）も同様に押し続け、field の text が 10 文字以上減る（browser の log の `text` の event か PNG）。押すのをやめた後に繰り返しが続かない（count の行が 1 回だけ、以後 text が変わらない）。
4. 回帰: `plan/tools/titlebar/titlebar-p010.sh`（CONTROLS: breadcrumb の段の click、search の入力、狭い窓の "..."）が PASS。
5. **編集していない時の箱**（T1-618 の再試験で足す）: browser で google.com を開いた後、編集していない状態の titlebar の PNG。PASS: Location が Back/Forward の右から Reload（と "..."）の左まで広がる角丸の箱で、中に `https://www.google.com/` が出ている（段と矢印ではない）、`KWL TITLEBAR control ... id=4 ... width=W` の W が編集中と同じ。箱を 1 回 click → `KWL TITLEBAR focus ... id=4 edit=1`、URL 全体が選択された PNG、文字を打つと URL が置き換わる（browser の `text` の event）。Files（`/bin/files`）の breadcrumb は従来どおり段と矢印（PNG）。

## T1-618 の結果と p010 の FAIL（2026-10-14）

T1-618（main eea58c75c、7e089184b でも同じ）: 1・2・3・5 PASS（Location の幅 117→642、repeat count=28、箱と 1 回の click で全選択、Files は段のまま）。4 の `plan/tools/titlebar/titlebar-p010.sh` が FAIL: docked の double click の後の `GLASS undock surface=8` が MISSING、続く narrow の 5 行も MISSING。

- **原因（試験の側、p229 ではない）**: undock の double click の x は `docked_free_x`（docked の control の右端と bar の minimize の中点）。ws181-p009（2026-10-08 の UAT）から、窓が docked の間は desktops の pill と status の pill が docked の control のすぐ右に来る（`shell.c` `bar_layout`）。T1-618 の画面（`/home/awe/zedBSD-worktrees/t1/build/review/t1-618/p010/docked.png`）では Preview の右端 756、desktops の pill 約 781、status の pill 909〜993、minimize 1184 で、中点の約 962 は status の pill（press は control panel）に当たり、undock にならない。narrow の 5 行はその続き: 1 つ目の窓が docked のまま（docked の窓が desktop を持つ、WS181）なので 2 つ目の probe も docked で開き（narrow.png、`client=2 ... where=docked`）、floating の行が出ない。
- **p229 の変更が効かない根拠（source）**: docked の probe（Files の control、breadcrumb は箱でも編集中でもない）の配置は p229 の前と同じ（段の幅の上限・give way は `shell_crumb_field` が 0 の時は従来どおり）。題「Files」「Narrow」は短く、題の縮み方は効かない（380 px の窓は 1/5 が 46 < 56 なので題の幅も変わらない）。p010 の最後の PASS は T1-281（2026-10-07、ws181-p009 の前）で、それ以後 T1-618 まで p010 は流れていない。75a6bdc86（p229 の前）で流した記録は無い（未実施）。
- **直し**: `titlebar-p010.sh`・`titlebar-p011.sh`・`titlebar-p013.sh` の `docked_free_x`（3 つとも同じ関数）の右の限りを、minimize と、最新の `KWL GLASS desktops x=`（最初の slot、pill の左は x−10）と `KWL GLASS status left=` のうち docked の control より右で最も左の物にした。T1-618 の値で計算すると 756 と 781 の中点の 768（bar の題の範囲、どの pill にも当たらない）。awk を同じ行で模擬して 768 を確かめた。`sh -n` 3 本 ok。
- **T1 の再試験（p010 だけ）**: この commit を含む tree で menu の image（`plan/tools/titlebar/build-menu-image.sh build/<T>`）を作り、guest を起こして `plan/tools/titlebar/titlebar-p010.sh build/<T>/p010`。PASS: 最後の行 `titlebar-p010: PASS`（`GLASS undock surface=8 ok` と narrow の 5 行 ok）。p011・p013 は同じ関数の直しなので、時間があれば同じ image で流す（任意）。

## 残り・制限

- QEMU・実機: 未実施（上の T1 の依頼）。
- 編集していない時の箱は上の「追加」で対応（2026-10-14 ユーザーの判断）。
- FreeBSD の build は未実施（変更は OS に依らない C）。
