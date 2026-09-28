<!-- awesome-plan project=zedbsd record=ws079-p010 -->

# ws079-p010: 上の右端からのスワイプ（Notes）と画面の端のジェスチャーの整理

<!-- awesome-plan-current:start -->
Status: cleared（pointer の範囲。2026-09-28、Kei desktop subagent。QEMU の Venus guest の証拠だけ。pen の入力の接続は p003 の後、本物の Notes での確認は p005 の後）
Disposition: normal
Parent: [WS079](../ws.md)
Queue: main の指示（Kei desktop subagent、2026-09-28）。Awesome Plan の Queue の item ではない
Resume point: p003（tablet.c）が入ったら pen の接触を `zwl_corner_contact_*` と Wiseview・Home の端の判定へつなぐ。p005 の後に本物の `/bin/notes` で再確認
<!-- awesome-plan-current:end -->

## 範囲

- [design-input-notes.md](../design-input-notes.md) §4: 右上の 28×28 から左下へのスワイプの認識器（pointer。pen・touch を後で同じ入口に入れられる形）、
  腕を上げる前の見た目と成立・取り消しの見た目、Notes の起動・最前面・全画面。
- 同じ文書の末尾の「追記: 画面の端のジェスチャーの整理（2026-09-28 ユーザー）」（main が途中で p010 に加えた）:
  1. 右上のスワイプは App Home の上でも効く（Home を閉じて Notes を最前面・全画面。無ければ起動。既に最前面・全画面なら Notes には何もしない）。
  2. Home の上の下端からのスワイプは Home を閉じる（Wiseview を開かない）。それ以外（デスクトップ・アプリ・全画面の Notes）では今どおり Wiseview。
  3. Home の今の閉じ方は残す。
  4. 端のジェスチャーは端の範囲から始めたものだけ。窓の中から始めた線は端のジェスチャーにならない。
- 範囲外: pen（p003 の `tablet.c` は触らない）、touch、本物の Notes（別の agent が書いている）。HAL の変更なし。

## 実装（2026-09-28）

| file | 変更 |
| --- | --- |
| `userland/desktop/wayland/corner.c`（新） | 認識器。入口は `zwl_corner_contact_begin/_move/_end(server, source, x, y, time)`（`enum zwl_contact_source`: pointer・pen・touch）。pointer の adapter は `zwl_corner_button`・`zwl_corner_motion`。`zwl_corner_tick`（1500 ms の期限・見た目の settle・起動待ちの期限・release が来ない contact の片付け）、`zwl_corner_showing`、`zwl_corner_draw` |
| `shell.c` | button の鎖で corner を **App Home の前**に置いた（右上の zone は Home の zone と重ならないので Home が閉じているときは「Home の直後」と同じ。Home が開いているときも右上が効く）。motion の鎖も同じ。`zwl_glass_still` は hint の間は still でない。hint は system bar と network の menu の上に描く。下端の Wiseview の開始を `wiseview_edge_press` に切り出した。全画面の窓の上の端のジェスチャー `zwl_glass_edge_button`・`zwl_glass_edge_motion`（左上 28×28 → Home、右上 → Notes、下端 20 px → Wiseview、それらが開いたものの入力）と `zwl_glass_overlay` |
| `seat.c` | 全画面の mode（`!server->windowed`）のとき `zwl_glass_edge_*` を通す（今までは全画面の窓の上で端のジェスチャーが一つも効かなかった）。pointer の event の時刻を `server->input_time` に置く（速さは compositor の停止に左右されない evdev の時刻で測る） |
| `display.c` | 端のジェスチャーが何かを見せている間（hint・Home・動いた後の Wiseview）は全画面の窓でも direct scanout にせず合成する |
| `home.c` | 起動の fork/exec を `zwl_spawn()` に切り出した（App Home の起動は同じ動作）。`zwl_home_dismiss()`。Home の上の下端 20 px からの上へのスワイプ: 12 px で追従を始め、240 px で閉じ切る。離したとき 84 px（0.35）以上上なら `HOME close via=bottom`、足りなければ開き直す（search は保つ）。Wiseview はこの経路に来ない（Home が開いている間は Home が全ての button を取る） |
| `protocol.c` | client の `set_fullscreen` の処理を `zwl_window_enter_fullscreen()` に切り出し、corner からも使う（compositor 側からの fullscreen の configure） |
| `zwl.h`・`glass.h`・`Makefile` | 宣言、`home_bottom_*` と `input_time` の field、`corner.c` |
| `userland/desktop/wltest/`・`userland/base/tests/acquire-fence/main.c` | wltest に `--app-id=NAME`（試験の代役）。`wltest_window_open` の引数が増えたので acquire-fence の呼び出しも直した（app_id は従来どおり `wltest`） |

認識の数値（§4.1 のとおり）: zone `x ≥ W−28 かつ y < 28`、腕上げ `dx ≥ 14 かつ dy ≥ 14`（押してから 1500 ms 以内、過ぎたら `cancel reason=timeout`）、
方向は短い方 × 25 ≥ 長い方 × 9（0.36、対角 ±25°）、距離の成立 `(dx+dy)/2 ≥ 108`、速さの成立 `(dx+dy)/2 ≥ 40` かつ直前 100 ms の対角の速さ ≥ 0.8 px/ms
（最新の点から 100 ms 以上前の最も新しい点、無ければ最古の点までの平均）。network の menu か window の menu が開いているときは始めない（その押しは menu を閉じる）。

見た目: 腕が上がると、右上の角を中心とする glass の四分円が contact まで広がる（半径 24 + 1.3 × 進み、角が捲れるような形）。影・白い glass・縁。
対角から外れている間は灰色がかり、離せば成立する距離では縁が Kei の青になる。半径 96 を超えると「Notes」の文字。離すと 200 ms で角へ縮む（取り消し）
か、広がりながら消える（成立）。画面に Keiland の名前は出さない。

成立の動作: Home が出ていれば `zwl_home_dismiss(server, "notes")`。app_id `notes` の mapped な toplevel（最も後に raise されたもの）があれば、
既に最前面かつ全画面なら何もしない（`CORNER notes surface=N already`）。そうでなければ minimize を解き、今の desktop へ移し、`zwl_glass_raise`
（click と同じ raise と focus）、`zwl_window_enter_fullscreen`。無ければ `/bin/notes` の存在を確かめ、`zwl_spawn(server, "/bin/notes --fullscreen")`。
起動から 5000 ms は二つ目を起こさない（`CORNER notes waiting`）。

system bar との関係（測定）: zone は x=1252..1279（1280 幅）。network の icon の hit の箱は x=1044..1076（run ごとに時計の文字幅で数 px 動く）で重ならない。
電池の絵（押しの動作なし）も zone の外。時計の文字は右端 16 px の手前で終わり、zone は時計の最後の約 12 px に掛かる（時計に押しの動作は無いので、
腕が上がらない押しは今どおり何もしない）。docked の窓の button は bar の左〜中央で重ならない。

## 検証

build（`BUILD=build/ws079-p010`、`ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk`、`-Wall -Wextra -Werror`）:
`make -j16 … build/ws079-p010/bin/wayland build/ws079-p010/bin/wltest build/ws079-p010/bin/acquire-fence-test` → rc=0、warning 0。
`git diff --check` → 問題なし。clang-format は host に無く未実施（automation.md の 19.1.7 は今の host に入っていない）。規約は全文（§2〜§11）を目で確かめた。

QEMU（Venus guest、`plan/ws035/tests/zdesktop-guest.sh start`、1280x800、KVM）: 画像は main の `build/ws035-sq/hdd-image.img`（2026-09-28 12:59）の複製
`build/ws079-p010-img/`。worktree で full image を build すると guest 用の clang・lldb（`build/packages/clang`、`build/llvm-build`）の build が始まるため
image は作らず、build した `wayland`・`wltest` と代役の `/bin/notes`（[notes-standin.sh](../tests/notes-standin.sh)）を起動中の guest の disk の複製へ scp した。
試験 [zdesktop-p010.sh](../tests/zdesktop-p010.sh)（pointer は `qmp-pointer.py`、判定は guest の `/tmp/zdesktop.log` と VNC の画面の画素）:

| # | 確かめたこと | 結果 |
| --- | --- | --- |
| 0 | zone（x=1252）と network の icon（x≤1076）が重ならない | PASS |
| 1 | 否定: 短い（30 px）→ `cancel reason=short`、方向違い（左へ 200・下へ 20）→ `direction`、遅い（進み 90 を 1.8 s）→ `short`（speed 0.00）、押して 1.8 s 待ってから → `timeout`、zone の外（x=1240）から → press 無し。commit 0、代役 0 | PASS |
| 2 | 距離のスワイプ（進み 160）→ `commit via=distance`、`notes launch`、`MODE fullscreen`、代役 1、画面の 3 点が fdf6e3（全画面）。hint を押したまま撮影 | PASS |
| 3 | 全画面の代役の上で flick（進み 96、speed 1.35 px/ms）→ `commit via=flick`、`notes surface=6 already`、代役は 1 のまま | PASS |
| 4 | 窓の代役（640x400）を別の窓の下に置いてスワイプ → `notes surface=6 raise fullscreen`、`CONFIGURE … width=1280 height=800 fullscreen=1`、全画面の画素 | PASS |
| 5 | 全画面の代役の上で左上から Home を開く（`HOME open via=drag`）→ 右上のスワイプ → `HOME close via=notes`、`already`、画面は代役 | PASS |
| 6 | 全画面の代役の上で下端から上 → `WISEVIEW opening`（全画面の窓の上の Wiseview） | PASS |
| 7 | 代役なしで Home を開き右上のスワイプ → `HOME close via=notes`、`notes launch`、全画面の画素 | PASS |
| 8 | Home の上で下端から上 → `HOME bottom swipe`、`HOME close via=bottom`、`WISEVIEW opening` は増えない | PASS |
| 9 | デスクトップで下端から上 → Wiseview。下端の 30 px 上から始めた線 → Wiseview にならない | PASS |
| 10 | Home の左上のドラッグで開く、右下に残った desktop の角の click で閉じる（`HOME close via=corner`）、title bar の double click で dock・bar の title の double click で undock | PASS |

最終の run: `p010: PASS`、`/tmp/zdesktop.log`・`a.log`・`b.log`・`notes.log` に ERROR/FAILED 無し。それまでの run: r1（試験の script の誤り: 画面撮りの
引数、guest の `ps` が引数を出さないので代役を数えられない → pid の file にした）、r2（flick の速さを compositor の処理時刻で測っていたため、全画面から
窓の mode への切り替え（約 450 ms）で 0.08・0.75 px/ms に落ちた → evdev の event の時刻で測るように直した。試験の flick も 80→96 に）。

画面（`build/ws035-shots/`、main の checkout の共有の dir に置いた）: `ws079-p010-20260928-desktop.png`・`-hint.png`（腕が上がった hint）・
`-notes-fullscreen.png`・`-notes-after-flick.png`・`-notes-behind.png`・`-notes-raised.png`・`-home-over-notes.png`・`-home-to-notes.png`・
`-wiseview-over-notes.png`・`-home-launch-notes.png`・`-home-bottom-closed.png`・`-wiseview.png`・`-docked.png`、log の抜粋 `-log.txt`。

未実施: 実機（pen・touch・i915 の HDMI）、本物の Notes（p005 の後）、pen の入力（p003 の後）、`boot-test.sh`（full image を worktree で作ると clang の
build になるため。変えたのは compositor と wltest だけで kernel・boot は不変）、clang-format。

## 所見と残り

- 全画面の窓の上では、今まで App Home・Wiseview の端のジェスチャーが効かない作りだった（コードを読んだ所見: `seat.c` は `server->windowed` のときだけ shell を通していた。変更前の動作は guest で確かめていない）。
  ユーザーの表（全画面の Notes の上でも下端は Wiseview、左上は Home）のため、全画面でも端の範囲だけは compositor が取るようにした。
  代わりに全画面の app は左上 28×28・右上 28×28・下端 20 px から始まる押しを失う（D3 を左上と下端にも広げた形。main の確認を求める）。
  下端の click（動かない押し）は mode を切り替えない（Wiseview の進みが 0 の間は合成しない）。
- 全画面の窓の上でジェスチャーが何かを見せると、表示は direct scanout から合成に切り替わる（Venus の guest で 400〜1100 ms）。見た目は切り替えの後に出る。
  実機の i915 での切り替えの時間は未測定。
- 全画面の上の左上 28×28 の click は Home を開く（launcher の click と同じ扱い。launcher は全画面の窓の下で見えない）。
- pen: `zwl_corner_contact_*` は source を持つ。p003 の後、pen の接触（`down`/`motion`/`up`、event の時刻付き）をこの入口と、
  Home・Wiseview の端の判定（今は pointer の座標で見ている）へつなぐ。窓の中から始めた pen の線が端のジェスチャーにならないことは実機の pen で確かめる。
- 代役の `/bin/notes` は試験の guest の disk の複製にだけ置く。本物の Notes は `--fullscreen` を受けて最初の map の前に `set_fullscreen` を頼むこと（§4.3）。
