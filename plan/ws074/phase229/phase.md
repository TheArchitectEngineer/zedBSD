<!-- awesome-plan project=zedbsd record=ws074-p229 -->

# ws074-p229: UAT のアドレスバー（compositor 側: 既定の URL、箱の幅、key repeat）

P2 (2026-10-14)。親: [WS074](../ws.md)。前段: [p228](../phase228/phase.md)（B1 の調べ、browser の `MAIN_HOME_PAGE`）。
Status: in-progress / 実装済み・**T1 の試験待ち**（依頼は Q1 経由、番号は Q1 が付ける）。QEMU・実機は未実施。

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

## 残り・制限

- QEMU・実機: 未実施（上の T1 の依頼）。
- 編集していない breadcrumb の見た目（箱が無く段の文字だけ）は変えていない。browser の Location を常に箱として見せる（Chrome 風）なら別の判断が要る。
- FreeBSD の build は未実施（変更は OS に依らない C）。
