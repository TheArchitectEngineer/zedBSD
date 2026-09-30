<!-- awesome-plan project=zedbsd record=ws102-design -->

# WS102 の設計: スクリーンキーボード（compositor に直接）

2026-09-30 ws102-p001。目標と達成基準の案（K1〜K8）は [ws.md](ws.md)。前提は次の 3 つ。

- ユーザー: compositor に直接作る。手書きの認識は stub でよい。
- master「デモの touch」: touch の場面は Windows の上の QEMU（WS085）で見せる。5330 の実機は mouse と keyboard。
- master「広く浅く」: 段（L1…）に分け、各段に数値目標を置く。

加えて、IME（WS095）は人間が作業中なので、`ime.h`・`text-input.c`・`input-method.c` と seat.c の IME の hook は変えない。

## 1. 今の状態（調査の結果、2026-09-30 の main）

| 部分 | 所在 | 事実 |
| --- | --- | --- |
| touch の入力 | `input.c`（`/dev/input/eventN`、MT の slot と position を持つ node は touch）、`touch.c`（`zwl_touch_frame`、2 画面 × 16 slot） | QEMU の注入（`touchinject`、kernel の `CONFIG_INPUT_TEST_INJECT`）と、WS085 の Windows の QEMU の `usb-multitouch`（10 指）は、どちらも kernel の USB HID touch → evdev → `touch.c` の同じ経路を通る。画面の全体へ写す |
| touch の振り分け | `touch.c` `contact_begin`（:785） | 1 本目の指は title bar か shell か client に行く。shell は pointer の左 button として受け取る（`shell_press` → `zwl_seat_button_shell`、`server->shell_source = TOUCH`）。shell が取った指があると、2 本目以降の指は wl_touch の client にしか行かない（`shell_busy`）。**2 本指の連打には専用の経路が要る** |
| 角と端の gesture | `corner.c`（右上の Notes、`zwl_corner_contact_begin/move/end` は source に依らない）、`home.c`（左上の角の drag、Home の表示中の下端）、`shell.c`（`wiseview_edge_press`: 下端 20 px の全幅、`zwl_glass_button` の desktop の swipe: `x<16` か `x>=width-16`） | 下の左右の角の gesture は無い。下の 20 px は全幅が Wiseview（Home の表示中は Home を閉じる swipe）。下の角の最も外側の 16 px は desktop の swipe |
| key を送る | `seat.c` `zwl_seat_key`（:818、IME・shortcut を通る）、`zwl_seat_key_deliver`（:908、focus の client に直に `wl_keyboard.key`）、`zwl_seat_modifiers` | client は US の XKB keymap（`keymap.c`）で evdev の code を読む。key の repeat は client が行う |
| 文字列を送る | `ime.h` の公開の API: `zwl_text_input_current(server)`・`zwl_text_input_deliver(input, preedit, begin, end, commit, before, after)` | text-input-v3 を有効にした focus の field に、UTF-8 を commit できる。IME の file を変えずに呼べる。ただし IME が組み立て中（`server->ime->composing`・`preedit_shown`）のときは、IME の preedit を壊さないよう避ける |
| text-input-v3 の app | `ime-probe` だけ | **Text Editor・Terminal は text-input-v3 を持たない**（Text Editor の対応は WS095-p007、人間の作業の範囲）。持たない app に送れるのは US の evdev の key だけ（英数・記号は送れるが、かなは送れない） |
| 描画 | `glass.c`（`glass_shape_draw` の GLASS・SHADOW・SOLID・RING・TEXT、`glass_draw_text` は UTF-8 で DroidSansFallback の fallback を持つので、かなの key の文字が出せる）、`volume.c` の popup が白いすりガラスの型 | frame の順（`zwl_glass_draw`）の最後、corner の後に足す。全画面の窓の上では `zwl_glass_overlay` が 1 を返さないと direct scanout で隠れる |
| 上に重ねる物の入力 | `volume.c` の型（`*_is_open`・`*_button`・`*_motion`、`zwl_glass_button` と `glass_motion_take` からの hook） | 全画面の窓の上でも働かせるには、`zwl_glass_edge_button`・`zwl_glass_edge_motion` にも hook が要る |
| 作業の領域 | 1 か所にまとまっていない。`zwl_glass_space`（窓の置き場と fit、configure_bounds）、`docked_rect`（最大化）、`zwl_window_send_configure`（全画面）、`desktop_place`（desktop surface） | 下の keyboard の高さを差し引く口を 1 つ作り、各所がそれを使う |

## 2. 形（決めたこと）

### 2.1 置き場所と process

compositor の新しい file `keyboard.c`（と `keyboard.h`）にまとめる。gesture の認識・panel の配置と描画・flick の判定・文字の送出を、すべて compositor の中で行う（K6）。

配列と flick の表（どの key のどの向きがどの文字か）は描画と Wayland に依らない `keyboard-layout.c` に分ける。host の試験は、表と方向の判定を直接読む。
手書きの認識は `keyboard-hand.c` の interface（`zwl_hand_recognize(strokes, candidates)`）に閉じ、今は stub にする（§2.7）。

既存の file に足すのは hook の数行だけにする。

| file | 足すもの |
| --- | --- |
| `shell.c` | `zwl_glass_button`・`glass_motion_take`・`zwl_glass_edge_*`・`zwl_glass_draw`・`zwl_glass_overlay`・`zwl_glass_space`・`docked_rect` |
| `touch.c` | §2.6 の経路 |
| `desktop.c`・`protocol.c` | 作業の領域 |
| `zwl.h`・`Makefile` | 宣言と source の一覧 |

IME の 3 つの file と seat.c の IME の hook には触れない。seat.c は `zwl_seat_key_deliver` と `zwl_seat_modifiers` を呼ぶだけ（変えない）。

### 2.2 開く gesture（K1・K3・K7）

`corner.c` と同じ型の認識器を、下の 2 つの角に置く（`zwl_keyboard_contact_begin/move/end` と、pointer の adapter）。

| 値 | 右下（flick） | 左下（QWERTY） |
| --- | --- | --- |
| 角の区域 | `x ≥ width-28 && y ≥ height-28` | `x < 28 && y ≥ height-28` |
| 向き | 左上へ（対角 ±25°） | 右上へ（対角 ±25°） |
| arm | x と y の両方が 14 px 以上、1500 ms 以内（corner.c と同じ） | 同左 |
| 開く | 対角に 108 px、または flick（最後の 100 ms で 40 px 以上、0.8 px/ms 以上） | 同左 |

- **既存の gesture との区別（K7）**: 下の角の 28×28 は keyboard の区域として、Wiseview の下端の press（`wiseview_edge_press`）と desktop の swipe（外側 16 px）より先に取る。Wiseview の下端は角を除いた幅（`28 ≤ x < width-28`）になる。
  - Home の表示中の右下 40×40（Home を閉じる）は Home が先に取る。Home の表示中は keyboard を開かない。
  - 角の press が arm しないまま離されたとき（ただの tap）は何もしない。
  - 角から上へまっすぐの swipe（対角の外）は arm しない。角では Wiseview の swipe は始まらない（角を除いた下端から始める）。これを K7 の回帰で確かめる。
- **閉じる**:
  - panel の閉じる key。
  - panel の上から角へ向けた外への swipe。flick の panel は右へ 80 px、QWERTY は下へ 80 px。
  - 開いた gesture と同じ角の gesture をもう一度行う（toggle）。
  - lock・greeter・App Home・Wiseview が開いたときは閉じる。
- mouse でも同じ gesture が働く（左 button の drag）。QMP の pointer でも試験できる。

### 2.3 panel の配置と見た目（K6）

画面の大きさは `server->width/height` から比で決める。1280x800（QEMU）と 1920x1080（5330 の LCD・Windows の QEMU）で確かめる。

| panel | 位置 | 大きさ |
| --- | --- | --- |
| flick | 右端、下寄せ（右と下の余白 12 px） | 上の帯（36 px: 今の face の名前と閉じる ×）と、4 列 × 4 行の key。1 key は `clamp(height / 11, 64, 96)` px の正方形、間は 6 px（1280x800 で key 72 px、panel は 318 × 342 px 前後） |
| QWERTY | 下端の全幅（左右の余白 12 px） | 5 行（数字・3 行の英字・space の行）。高さは `clamp(height × 0.38, 260, 420)` px（800 で 304 px） |
| 手書き | QWERTY と同じ場所（面の切り替え） | 左に書く面、右に候補の列と key（消す・space・enter・QWERTY に戻る） |

- 見た目は `volume.c` の popup と同じ白いすりガラスにする（`MODE_SHADOW` の上に `MODE_GLASS` の {1,1,1,0.86}、edge 0.85）。
- key は角丸の `SOLID`（淡い白）。押している key は accent の青（#2f7cf6）に白い文字。
- 開く・閉じるときは 180 ms で角から滑り出す（L3 で数値を詰める）。
- system bar の上には重ねない。全幅の QWERTY は Wiseview の下端の区域を覆う。keyboard が出ている間、下端の Wiseview の swipe は keyboard の上端から始める。

**2026-09-30 ユーザーの方針で改めた（縁に組み込む）**: ユーザー「スクリーンキーボードは、ウィンドウのエッジにぴったりと組み込み、フローティング
ウィンドウにしない見た目がいいです。」→ Q1 は「画面の縁にぴったり付け、浮いた窓（余白・外の角丸・影）にしない」と読む。

- flick の panel: 右の列の全体（system bar の下端から画面の下端まで、右の余白 0）。§2.10 の道具の面を上に、flick の key を下に置く。
- QWERTY・手書きの panel: 下端の全幅（左右と下の余白 0）。
- panel の外の角丸と影をやめ、画面の縁に接する辺は直線にする。内側（作業の領域に面する辺）は 1 px の区切りの線だけにする。system bar と
  同じ「縁に組み込んだ帯」の見た目にする（system bar が上の縁の帯、keyboard が右か下の縁の帯）。
- key の角丸と押した key の accent は今のまま。
- 開く・閉じる動きは、角から滑り出すのではなく、縁から帯が伸び出す（右の列は右から、QWERTY は下から）。§2.8 の窓の縮む動きと同じ時間と easing にする。
- 作業の領域（§2.8）は panel の内側の辺までを差し引く（余白の分が無くなる）。

### 2.4 flick の配列（K2）

key は 4 列 × 4 行。左の 3 列が 12 key、右の列が ⌫・空白・⏎・face の切り替え（`あ → A → 1 → あ` と巡る）。どの face も 12 key の並びは同じで、
各 key に「中心・左・上・右・下」の 5 つの文字（無い所は空）を持つ。

かなの face（iOS・Android の日本語の 12 key と同じ並び）:

| key | 中心 | 左 | 上 | 右 | 下 |
| --- | --- | --- | --- | --- | --- |
| 1 行 1 | あ | い | う | え | お |
| 1 行 2 | か | き | く | け | こ |
| 1 行 3 | さ | し | す | せ | そ |
| 2 行 1 | た | ち | つ | て | と |
| 2 行 2 | な | に | ぬ | ね | の |
| 2 行 3 | は | ひ | ふ | へ | ほ |
| 3 行 1 | ま | み | む | め | も |
| 3 行 2 | や | （ | ゆ | ） | よ |
| 3 行 3 | ら | り | る | れ | ろ |
| 4 行 1 | ゛゜小（巡らせる） | — | — | — | — |
| 4 行 2 | わ | を | ん | ー | 〜 |
| 4 行 3 | 、 | 。 | ？ | ！ | … |

英字の face（携帯の 12 key の英字）:

| key | 中心 | 左 | 上 | 右 | 下 |
| --- | --- | --- | --- | --- | --- |
| 1 行 1 | @ | # | / | & | _ |
| 1 行 2 | a | b | c | — | — |
| 1 行 3 | d | e | f | — | — |
| 2 行 1 | g | h | i | — | — |
| 2 行 2 | j | k | l | — | — |
| 2 行 3 | m | n | o | — | — |
| 3 行 1 | p | q | r | s | — |
| 3 行 2 | t | u | v | — | — |
| 3 行 3 | w | x | y | z | — |
| 4 行 1 | a/A（直前の 1 文字の大小を入れ替える） | — | — | — | — |
| 4 行 2 | ' | " | ( | ) | : |
| 4 行 3 | . | , | ? | ! | - |

数字と記号の face: 1 行 1〜3 行 3 が 1〜9、4 行 2 が 0。各数字の key の左・上・右・下に、英字の face に無い ASCII の記号（`+ - * = % < > [ ] { } $ ^ ~ \ | ; ` と back quote）を 4 つずつ置く。4 行 1 は全角の記号の key（「」・。 を左上右下に）、4 行 3 は space。

表の細部（どの記号をどの key に置くか）は p003 の Phase で `keyboard-layout.c` の定数として決める。host の試験では、次の 3 つを確かめる。

- 英小文字 26・数字 10・ASCII の記号 32・空白が、英字か数字の face のどこかにある（英大文字は a/A の key で出す）。
- かなの 46 字（清音）と「ー」が、かなの face にある。
- 1 つの face の中で同じ文字が 2 回出ない。

- 「゛゜小」の key は、直前に送った 1 文字を濁点・半濁点・小書きへ巡らせる（`か → が → か`、`は → ば → ぱ → は`、`つ → っ → づ → つ`）。
  - text-input で送った文字は、`delete_surrounding_text`（前の 1 文字の byte 数。かなは UTF-8 で 3 byte）と新しい文字の commit で置き換える。
  - evdev で送った文字（英字）は、⌫ と key で置き換える。
- **flick の判定**:
  - 押した点からの動きが `max(16, key × 0.3)` px 未満なら中心の文字。
  - それ以上なら、角度で上下左右を決める（45° の境）。
  - 押している間は、key の上下左右に 4 つの文字の案内（花びら）を出し、今の向きの文字を青くする。

### 2.5 文字の届け方（K2・K8、IME の file を変えない）

| 送る物 | 経路 | 理由 |
| --- | --- | --- |
| US の keyboard にある文字（英字・数字・ASCII の記号・space・⏎・⌫・矢印） | `zwl_seat_key_deliver` で evdev の press と release。Shift が要る文字は、その間だけ `server->modifiers` に Shift を立てて `zwl_seat_modifiers` を送り、終わったら戻す | どの app（text-input の無い Text Editor・Terminal・X11 の app）にも届く。IME を通らないので、IME の状態を乱さない |
| それ以外の文字（かな・全角の記号・「ー」など） | focus の field が text-input-v3 を有効にしていれば、`zwl_text_input_current` と `zwl_text_input_deliver(…, NULL, 0, 0, 文字, 0, 0)` で commit。IME が組み立て中（`server->ime != NULL && server->ime->composing`）なら、keyboard は送らずに「変換中」と出す（L1）。L3 で IME と組む（§5 の D2） | IME の file を変えずに、公開の API だけで送れる |
| text-input の無い app へのかな | 送らない。panel の上に「この app はかなを受け取れません」と 2 秒出す（log `ZWL OSK refused reason=no-text-input`） | US の evdev ではかなを表せない。app に text-input-v3 が入れば（WS095-p007）そのまま届く |

- log: `ZWL OSK key face=… key=… dir=… text=… via=key|commit|refused`。試験は log と、受け取った側の log や file で判定する。
- 受け取りの確かめ: 英数は Text Editor の保存した file（Ctrl+S）で見る。かなは `ime-probe`（text-input-v3 の commit を log に出す）で見る。Text Editor のかなは WS095-p007 の後。

### 2.6 touch の経路（K2・K3 の 2 本指の連打）

- **L1**: keyboard の区域と panel の上の 1 本目の指は、今の shell の経路（pointer の左 button として受け取る）で扱う。1 本指で 1 key ずつ打つ。
- **L2**: `touch.c` に `ROUTE_OSK` を足す。panel の上に落ちた指は、shell が他の指を持っていても、指ごとに独立した key の press として keyboard に渡す（`zwl_keyboard_touch_down/motion/up(server, id, x, y, time)`）。QWERTY の 2 本の親指の連打（rollover）のため。IME の hook とは関係しない（touch.c は IME の file ではない）。

### 2.7 手書き（K4）

- 書く面の中の線を点の列で持つ（1 画は最大 512 点、最大 64 画）。線は太さ 3 px の丸い端の線で描く。
- 書き終えて 600 ms 経つか、「認識」の key を押すと、`zwl_hand_recognize` を呼ぶ。
- stub は、画の数と外接の矩形を log に出し、候補に「（認識はまだ）」と、試験用の固定の候補 3 つ（`あ`・`い`・`う`）を返す。候補を押すと §2.5 の経路で送る。
- 本物の認識の engine は後で差す（Future）。

### 2.8 作業の領域（K5）

**2026-09-30 ユーザーの方針で改めた**（前の案は flick の panel では縮めず、浮いた窓は高さも縮めた）。ユーザー:「スクリーンキーボードを表示したとき、
デスクトップの表示領域をキーボードのない範囲に狭めて、ウィンドウをアニメーションで移動、リサイズしましょう。タッチを活用するケースでは、ウィンドウは
最大化されていることが多いと思うので、最大化のままアニメーションで幅が狭まるのがいいですね。また、ウィンドウが最大化されていないなら、描画エリアから
はみでないように移動して、どうしてもはみ出た分はそのままにするのがいいと思います。」

- `zwl_keyboard_reserved(server, &right, &bottom)` を作る。flick の panel が出ている間は panel の幅を right に、QWERTY・手書きの面が出ている間は
  その高さを bottom に返す。**どちらの keyboard でも作業の領域を縮める。**
- `zwl_glass_space`・`docked_rect`・`desktop_place` から right と bottom を差し引く。desktop の層（icon）も縮んだ領域に置く。
- **最大化した窓**: 最大化のまま、縮んだ領域いっぱいの大きさへ animation で縮める（flick では幅、QWERTY では高さ）。閉じると animation で元に戻す。
- **浮いた窓**: 大きさは変えず、縮んだ領域に収まるよう animation で移動する。収まらない（窓が領域より大きい）ときは、上端（title bar）を領域の上端に、
  左端を領域の左端に合わせ、はみ出た分はそのまま keyboard の下に隠す。keyboard を閉じると、compositor が動かした窓だけ元の位置へ animation で戻す
  （keyboard が出ている間に利用者が動かした・大きさを変えた窓は戻さない）。
- **animation の方式**: keyboard の出る動き（同じ時間、既定 200 ms、同じ easing）と同時に、compositor が窓の矩形を補間して描く。大きさの変わる窓には
  **最初に 1 回だけ**最終の大きさの configure を送り、animation の間は今の buffer を補間の矩形に合わせて描く（伸縮か切り取り）。client の新しい buffer が
  届いたらそれに替える。frame ごとに configure を送らない（遅い client・Vulkan の client で animation が止まらないように）。既存の dock の animation
  （`anim_from`・`anim_to`、1 つの窓）を複数の窓へ広げる。
- **全画面の窓**は大きさを変えない。keyboard はその上に重ね、caret の位置は保証しない（制限として書く）。
- flick の panel の cursor の矩形による寄せ（前の案）は、作業の領域を縮めるので要らない。
- **keyboard による大きさの変更の知らせ（2026-09-30 ユーザー）**:「スクリーンキーボードの表示でサイズが変更されるとき、念のためウィンドウに特殊な
  XDGメッセージを送りましょう。対応しているウィンドウであれば、現在のキャレットを画面の中心など見やすい位置にセンタリングできる、という寸法です。
  libkeiuiの機能にしましょう。」
  - 標準の xdg-shell には独自の event を足せないので、title bar（`keiland_titlebar_v1`）と同じ形の Keiland の独自の protocol を窓ごとに作る
    （例 `keiland_keyboard_inset_v1`: manager の global と、xdg_toplevel ごとの object。`libwayland/*-protocol.c` に手書き、zdesktop の `protocol.c`）。
  - event `inset(right, bottom, reason)`: keyboard が出る・閉じるために compositor がその窓の大きさか位置を変えるとき、最終の大きさの
    `xdg_toplevel.configure` の**前に**送る。client は同じ configure の列で新しい大きさと一緒に受け取り、新しい大きさで描くときに caret を寄せる。
    keyboard を閉じて戻すときも送る（right・bottom は 0）。大きさも位置も変えない窓（全画面）にも、keyboard が上に重なることを知らせるため送る。
  - libkeiland: 薄い wrapper（`keiland_keyboard_inset_*`、KEILAND_VERSION 18。17 は ws075-p029 の `keiland_glass_set_blur` が先に使った）。bind できない compositor では何もしない。
  - libkeiui（KUI_VERSION 7）: `kui_window` が object を作って event を受け、app の callback（任意）に渡す。既定の動き: focus を持つ編集の text の
    view（`kui_text`）が、次の描画で caret の行を view の見えている範囲の縦の中央に寄せる（文書の先頭・末尾で寄せられない分は寄せない、
    view が scroll できないときは何もしない）。全画面で keyboard が重なるときは、重ならない範囲の中央に寄せる。
  - 対応していない窓（libkeiui を使わない app、他の toolkit）はこの object を作らず、今までどおり configure だけを受ける。

### 2.10 右の列の道具の面（2026-09-30 ユーザーとの議論、案）

ユーザー:「フリック入力のパネルは、画面右側を1列全体、占有します。」flick の panel は右の列の全体（system bar の下から画面の下まで）を占め、
下に今の 4×4 の flick、上に**道具の面**を置く。作業の領域（§2.8）は列の幅を差し引く。

ユーザーの要望（同日）:
- 変換候補・予測の列（Wayland の input-method に標準の口が無いので独自の拡張）
- カーソルの移動（矢印、行頭へ・行末へ、頁の上下）
- 範囲の選択の開始の button。選択の間はカーソルの key で範囲を広げ縮め、コピーか切り取りで終わる
- 編集の操作（独自の Wayland の拡張）
- クリップボードの履歴（常には出さず、button から何段かの操作で）
- バックスペース
- Unicode の絵文字
- 直前の app に切り替える窓の操作（app をまたいだコピーと貼り付けのため）
- Termux のような補助の key（Esc・Tab・Ctrl・`|`・`~`・矢印）は QWERTY の面に置く（「Termuxの補助キーは、AWERTYの方がいいかも。」、AWERTY は QWERTY の意）

| 部品 | 中身 | 仕組み |
| --- | --- | --- |
| 常に出る列 | 直前の app、BS、道具の面の tab（候補・編集・履歴・絵文字） | — |
| 編集の面 | ← → ↑ ↓、行頭・行末、頁の上・下、選択（toggle）、コピー・切り取り・貼り付け、取り消し・やり直し、全選択 | 移動は evdev の key（Home・End・PgUp・PgDn）。選択の間は移動に Shift を付ける。編集の操作は下の拡張、無い窓は key に落とす |
| 編集の操作の拡張 | `keiland_edit_v1`（仮）: 窓ごとの object。app が出来る操作と状態（選択がある・貼り付けられる・取り消せる）を知らせ、compositor が `action(copy/cut/paste/undo/redo/select_all/select_begin/select_end)` を送る。button は状態で灰色にする | libkeiland の wrapper と libkeiui の `kui_text`。拡張の無い窓は Ctrl+C・X・V・Z・Y・A の key に落とす（Terminal のように意味が違う app は app の id の表で Ctrl+Shift+C・V に） |
| 直前の app | compositor の focus の履歴（新しい順）の 2 番目の窓を前へ出し focus を移す。もう一度押すと戻る。長押しで最近の窓の一覧（後） | compositor の中だけ。keyboard は開いたまま |
| クリップボードの履歴 | 履歴の tab → 一覧（text だけ、最近 10 件）→ tap で貼る（2 段） | compositor の `data.c` が selection の text を記憶の中だけに持つ。password の欄（text-input の purpose）からの複写は残さない。lock・Log Out で消す |
| 絵文字 | 種類の tab と格子、tap で送る | text-input の commit（text-input の無い app へは送らない）。font が要る（下の判断） |
| 変換候補 | 縦の列に 8〜10 候補、tap で確定 | 独自の拡張（例 `keiland_input_method_candidates_v1`: IME が候補の一覧と選択を compositor へ送り、compositor が選択を返す。keyboard が出ている間は IME の popup を出さない）。**IME（WS095）は人間の作業中**なので、設計だけ先に置き、実装は IME を戻してから D2 と一緒に（p012） |

- 特許: 画面の keyboard の矢印・Home・End の key は古くから広くある（例: Windows XP Tablet PC Edition の入力 panel、X の xvkbd、GNOME の onboard）。特定の gesture（空白の長押しで trackpad など）は避け、普通の button にする。法的な判断ではない。
- 絵文字の font（2026-09-30 ユーザーの判断「色付きにする」）: 色付きの絵文字の font（候補 Noto Color Emoji、OFL-1.1。CBDT の bitmap 版と COLRv1 の版がある）を入れ、libtruetype に色の glyph の対応を足し、keyboard の panel と libkeiui の文字の描画（app の側）で色の絵文字を描く。どちらの形式を取るかと、license の監査は p019 の最初に決める。

### 2.9 試験（QEMU、Windows の QEMU）

| 何 | どこで | どう |
| --- | --- | --- |
| host | `plan/ws102/tests/host-keyboard.c` | `keyboard-layout.c` の表（全ての face と向きに文字があり、重複が無い）、flick の方向の判定（閾値と 45° の境）、濁点の巡り、配置の比（1280x800・1920x1080） |
| guest（Linux の上の QEMU の Venus、pen image） | `plan/ws102/tests/osk-guest.sh` | `touchinject` の script で角の swipe・tap・flick を注入し、zdesktop の log（`ZWL OSK …`）、受け取った側（Text Editor の保存の file、`ime-probe` の log）、画面で判定する。mouse の gesture は QMP の pointer で |
| 回帰 | WS099 の C9（`criteria.sh … C9`）、Notes の角（WS079-p010）、Wiseview の下端の swipe | K7 |
| Windows の上の QEMU（WS085） | L4 | 物理の touch と QMP の多指の注入で、L1・L2 の手順を目視と log で確かめる |

## 3. 段と数値目標

| 段 | 内容 | 数値目標 | 測り方 |
| --- | --- | --- | --- |
| **L1 まず動く** | 右下の swipe で flick の panel、かな・英字・数字の face、英数は Text Editor へ、かなは text-input の app（ime-probe）へ、閉じる、既存の gesture と区別 | (a) 注入した右下の swipe 10 回で 10 回開く。右下から真上への swipe 10 回で 0 回開く。(b) かな 46 字・英字 26・数字 10 の全てを、表の上で flick で出せる（host）。(c) 注入の flick で打った「aiueo123」が Text Editor の file に、「あいうえお」が ime-probe の log に、そのまま届く（誤り 0）。(d) C9 の 10 本が PASS | host の試験、`osk-guest.sh`（pen image）、`criteria.sh C9` |
| **L2 面を揃える** | 左下の swipe で QWERTY（shift・記号・数字・⌫・⏎・矢印）、手書きの面（線と stub の候補）、作業の領域（K5）、2 本指の連打（`ROUTE_OSK`） | (a) QWERTY で注入の 30 文字（英大小・記号を含む）を 5 文字/秒で打って誤り 0。(b) 2 本指を 50 ms ずつ重ねた 100 打鍵で取りこぼし 0。(c) 手書きの線が指に 1 frame（60 Hz で 17 ms）以内で追いつく（frame の時刻の log）。(d) QWERTY・flick を開くと、最大化した Text Editor が keyboard に重ならない大きさに縮み（log の configure と画面）、浮いた窓は収まる位置へ動く。animation の frame の間隔 ≤ 20 ms、client の新しい buffer が animation の終わりから 100 ms 以内に出る（L3 で測る） | 同上と frame の log |
| **L3 速さと日本語** | 打鍵から app までの遅れ、開く動きの滑らかさ、IME と組んだ変換（§5 の D2 の決定の後） | (a) key の離しから compositor の送出まで p95 ≤ 5 ms、app の frame まで p95 ≤ 50 ms（QEMU の Venus）。(b) 開く動きの frame の間隔が全て ≤ 20 ms。(c) D2 が決まれば、flick で打った「きょうはいいてんき」を変換して、第 1 候補が「今日はいい天気」 | 計測の道具（L3 の最初の Phase で作る）、log の時刻の中央値と p95（3 回） |
| **L4 デモの場** | Windows の上の QEMU（WS085）で、物理の touch で L1・L2 の操作 | 台本 S14 の操作が、物理の touch で最後まで通る（3 回中 3 回） | ユーザーと目視、WS085 の QMP の多指の注入 |
| （後） | 本物の手書きの認識、予測の候補、key の音、配列の設定 | — | Future |

## 4. Phase

| Phase | 段 | 内容 | 依存 | 触る所 |
| --- | --- | --- | --- | --- |
| p002 | L1 | `keyboard.c` の骨組み: 右下と左下の角の認識器（pointer と touch）。下端の Wiseview と desktop の swipe から角を除く。開閉、空の glass の panel、overlay と scanout、log | p001 | `wayland/`（keyboard.c・shell.c の hook・zwl.h・Makefile） |
| p003 | L1 | `keyboard-layout.c`: flick の 3 つの face の表、方向の判定、濁点の巡り。panel の key の描画と、押した時の花びら。host の試験 | p002 | keyboard.c・keyboard-layout.c |
| p004 | L1 | 文字の送出（§2.5）: evdev と Shift、text-input の commit、組み立て中と text-input の無い app の扱い。guest の試験（Text Editor・ime-probe） | p003 | keyboard.c |
| p005 | L1 | L1 の仕上げ: 閉じる gesture と toggle、lock・Home・Wiseview で閉じる。K7 の回帰（C9・Notes の角・Wiseview）。1920x1080 の配置 | p004 | keyboard.c・shell.c |
| p006 | L2 | QWERTY の面（shift の latch・記号・数字・矢印）と左下の gesture | p005 | keyboard.c・keyboard-layout.c |
| p007 | L2 | 作業の領域（`zwl_keyboard_reserved`（right と bottom）、最大化の窓の animation の縮みと戻し、浮いた窓の animation の移動と戻し、desktop の層、§2.8） |
| p015 | L2 | keyboard の inset の知らせ（`keiland_keyboard_inset_v1`、libkeiland の wrapper、libkeiui の `kui_window` の受け口と `kui_text` の caret の中央寄せ、Text Editor で確かめる、§2.8） | p007 | libwayland・protocol.c・libkeiland・libkeiui | p006 | shell.c・desktop.c・protocol.c |
| p008 | L2 | 手書きの面（線・stub の `zwl_hand_recognize`・候補） | p006 | keyboard-hand.c |
| p009 | L2 | `touch.c` の `ROUTE_OSK`（多指の連打） | p006 | touch.c |
| p010 | L3 | 計測の道具と基準値（遅れ・開く動きの frame） | p009 | plan/ws102/tests |
| p011 | L3 | 数値目標への直し（遅れ・動き） | p010 | keyboard.c ほか |
| p012 | L3 | IME と組んだ日本語の変換（D2 の決定の後） | p005、D2、WS095 | 決定による |
| p013 | L4 | Windows の上の QEMU（WS085）での確認（物理の touch） | p009、WS085 | — |
| p014 | — | 全文の規約と回帰（WS の最後。選んだ段の後） | 最後の段 | — |

L1 は p002〜p005 の 4 つ。広く浅くの方針では、WS102 はまず L1 までで止め、他の WS の L1 に揃えてから L2 へ進む。

## 5. 判断が要る点

| # | 点 | 既定（この設計で選んだもの） | 誰が |
| --- | --- | --- | --- |
| D1 | Text Editor・Terminal へかなを送る方法。今の 2 つは text-input-v3 を持たない | L1 では、かなは text-input-v3 の app（ime-probe）で確かめ、Text Editor には英数だけを送る。Text Editor の text-input-v3 は WS095-p007（人間の作業の範囲）を待つ。WS102 は Text Editor を変えない | ユーザー（WS095 の担当）。「WS102 で Text Editor に text-input-v3 を入れてよい」なら、L1 の中に Phase を 1 つ足す |
| D2 | flick の日本語の変換 | L1・L2 はかなをそのまま commit する。L3 で、(a) かなをローマ字の key の列にして `zwl_seat_key`（IME を通る経路）で送り、IME に変換させる案と、(b) IME に「かなを受け取る」口を足す案のどちらかを、WS095 の担当と決める | ユーザー（WS095 の担当） |
| D3 | 下の角の区域（28×28）を Wiseview と desktop の swipe から外すこと | 外す（§2.2）。Wiseview の下端は、角を除いた幅から始める | 既定のまま進め、L1 の回帰で確かめる |
| D4 | flick の英字の配列 | 日本の携帯の 12 key の英字（@abc / def / …）。QWERTY は L2 の別の面 | 既定のまま進める |

## 6. 範囲外

- 本物の手書きの認識（interface だけ）、予測・学習の候補、key の音と振動、配列の設定の画面、複数の画面、縦の画面。
- X11 の app（keiland-x11）への文字は、US の evdev で届く範囲だけ（かなは届かない）。
- 実機の外付けの touch LCD（master: 間に合えば別の計画）。

## 7. 見直し（2026-09-30 p001、自分で）

設計を書いた後に、次の点を確かめて直した。

| 点 | 見つけたこと | 直し |
| --- | --- | --- |
| かなの届け先 | K2 の案は「Text Editor・Terminal に届く」だったが、2 つとも text-input-v3 を持たない（text-input-v3 を持つのは `ime-probe` だけ）。US の evdev ではかなを表せない | §2.5 と D1: L1 では、かなを text-input の app で確かめる。Text Editor への対応は WS095-p007 か、ユーザーの判断で WS102 に 1 Phase 足す |
| IME の file を変えないこと | text-input の commit は、`ime.h` の公開の API（`zwl_text_input_current`・`zwl_text_input_deliver`）だけで送れる。key は `zwl_seat_key_deliver`（IME を通らない）で送れる。IME の組み立て中に commit すると、IME の preedit と食い違う | 組み立て中は送らずに「変換中」と出す（L1）。IME と組む変換は D2 で決める（L3） |
| 2 本指の連打 | 今の `touch.c` は、shell が指を持っている間の 2 本目の指を、shell に渡さない | L1 は 1 本指、L2 に `ROUTE_OSK` の Phase（p009）を置いた |
| 全画面の窓の上 | direct scanout の間は、compositor の描く物が出ない | `zwl_glass_overlay` を keyboard の表示中に 1 にする（§2.1 の hook）。全画面の窓の caret は保証しない（§2.8） |
| 下端の gesture の衝突 | 下の 20 px は全幅が Wiseview、外側の 16 px は desktop の swipe | 下の角 28×28 を keyboard が先に取る。Wiseview は角を除いた幅から始める（D3、K7 の回帰で確かめる） |
| 配列の表 | 最初に書いた英字と数字の face の表に空き（「・」）があった | かな・英字は完全な表にした。数字と記号の置き方は p003 で定数として決め、host の試験で網羅と重複を確かめる |
| flick の panel の大きさ | 「4 列 × 5 行」と 12 key の配置が合っていなかった | 上の帯と 4 × 4 の key に直した |
| 作業の領域 | 1 か所にまとまっていない（4 か所） | `zwl_keyboard_reserved` を 1 つ作り、4 か所がそれを引く（p007） |
| 試験の経路 | Windows の上の QEMU の touch（WS085 の `usb-multitouch`）と、Linux の上の QEMU の注入（`touchinject`）は、同じ kernel の USB HID touch → evdev → `touch.c` を通る | 日々の試験は Linux の上の注入で行い、Windows の上の確認は L4（p013）にした |

残る危険:

- 下の角の区域が、今の Wiseview や desktop の swipe の使い方（角から始める人）とぶつかる。L1 の回帰と、ユーザーの目視で確かめる。
- IME（WS095）の人間の作業で、公開の API（`zwl_text_input_current`・`zwl_text_input_deliver`・`struct zwl_ime` の field）の形が変わるおそれがある。変わったら p004 の送出を合わせる。
