<!-- awesome-plan project=zedbsd record=ws181-p001-design -->
# WS181 設計: 窓の状態、App Home の独立のモード、画面の端の gesture、整列のメニューと整列モード

Parent: [ws181-p001](phase.md)
版: 第 1 版（2026-10-07 P2）、design-reviewer の前

この文書は compositor（`userland/desktop/wayland/`）の挙動の設計で、実装は p002〜p004。ユーザーの原文は [ws.md](../ws.md) の「由来」。
前提にした今の実装: `layout.c`（session の `layout_mode`、ws142-p008）、`shell.c` の `window_dock`・`window_undock`・`layout_match`・
`layout_keep_front`・`layout_press_switches`・`bar_press`・pull、`home.c` の `kwl_home_layer`（desktop の層を右下へずらして角を残す）、
`shell.c` の `wiseview_edge_press`（下端から上で Wiseview）。

## 0. 今の不具合の原因（目標 1・2）

- docked の窓 A から別の app B へ切り替えると（docked mode）、B が dock され、A は **dock のまま**後ろに残る（ws142-p007 §1「切り替え元は dock のまま後ろに残る」）。
- B を floating に戻すと `layout_mode = WINDOWED` になるが、A は `maximized = 1` のまま。WINDOWED では他の app の窓を隠さないので、docked の A が B の後ろに見える。
- A を click すると `layout_press_switches()` が「窓の mode で docked の他の app の窓への press は切り替え」として A を floating に戻す。ユーザーが見た「click すると floating になる」はこれ。
- docked の窓を閉じると `layout_keep_front()` が次に前に来た窓を dock する（2026-10-06 の決定）。2026-10-07 の UAT はこれを floating に変える（§1.3）。

## 1. 窓の状態（p002）

### 1.1 状態の名前

窓（親を持たない toplevel）の状態は次の 5 つのどれか。`kwl_layout_state()`（layout.c、純関数）が今の値から決める。

| 状態 | 条件 | 見え方 |
| --- | --- | --- |
| `minimized` | `surface->minimized`（**利用者が明示に**最小化した） | 描かない。docked mode が終わっても最小化のまま |
| `fullscreen` | `surface->fullscreen` | 全画面 |
| `docked` | `surface->maximized`、かつその desktop の docked mode の「前の app」の窓 | dock の領域、title は system bar |
| `dock-hidden` | docked mode で、前の app でない窓（最小化でない） | 描かない、press も受けない。**docking の隠れ**。docked mode が終わると floating で見える |
| `floating` | 上のどれでもない | 自分の場所と大きさ |

- `dock-hidden` は flag として持たず、mode と前の app から毎回決める（今の `layout_hides()` の規則）。保存する状態は `minimized` だけなので、2 つの隠れが混ざることは無い。
- 同じ app の他の窓は docked mode でも隠さない（2026-10-06 の決定「アプリ内のウィンドウは下に見えてもいい」）。状態は `floating`（dock の窓の下）。
- dialog・sheet（親を持つ窓）は状態を持たず親に従う（今どおり）。

### 1.2 不変条件（新しい規則の核）

- **I1**: `layout_mode = WINDOWED` の間、`maximized = 1` の窓は**どの desktop にも 1 つも無い**（最小化の窓も含む）。
- **I2**: `layout_mode = DOCKED` の間、見える docked の窓は表示中の desktop で高々 1 つ（前の app の窓）。後ろの app の窓は `maximized` の値によらず描かない。

I1 は「docked mode を出る」1 つの関数 `layout_leave(server, front, via)`（shell.c）で守る:

1. 前の窓 `front`（docked で前にある窓、無ければ NULL）は今どおり animation つきで `window_undock()`。
2. それ以外の `maximized = 1` の窓（他の desktop の物、最小化の物も）は **animation 無しで** floating に戻す（`window_float_quiet()`: `maximized = 0`、場所と大きさを `restore_*` に、configure を送る、`window_resized()`）。隠れていた窓なので動きは見せない。
3. `layout_set(WINDOWED, via)`。
4. log: `KWL LAYOUT leave via=<via> front=<id|0> quiet=<数>` と、状態の要約 `KWL LAYOUT windows desktop=<n> floating=<n> docked=<n> dock_hidden=<n> minimized=<n> fullscreen=<n>`（AAT が読む）。

`window_undock()` の終わりの `layout_set(WINDOWED)` は `layout_leave()` に置き換え、前の窓を除いた残りを静かに戻す。

### 1.3 出来事ごとの規則

| 出来事 | 規則 | 今との違い |
| --- | --- | --- |
| 窓を dock（title の double click・maximize の button・title を bar へ drag） | `layout_mode = DOCKED`。他の app の窓は `dock-hidden` | 同じ |
| docked の窓を floating に（restore の button・bar の title の double click・pull・touchpad の TOP2） | `layout_leave()`: **全部の窓が floating**、最小化は最小化のまま | 後ろの docked の窓も floating に（目標 1） |
| docked mode で app の切り替え（Alt+Tab・Wiseview・bar の icon・App Home の起動中の app・activation） | 切り替え先を dock（今どおり）。切り替え元は dock のまま後ろに残る（`dock-hidden`、I2）。行き来のたびに大きさを変えない | 同じ |
| 窓の mode で app の切り替え | I1 により docked の窓は無いので、切り替え先は floating のまま前へ | `layout_press_switches()` を削除（目標 1 の「click で floating になる」が無くなる） |
| **docked の窓を閉じる**（app が自分で閉じるのを含む） | `layout_leave(front = NULL, via = "closed")`: 隠れていた窓は floating で見える、最小化の窓は最小化のまま | **変更**: 2026-10-06 の「次の窓も最大化」を 2026-10-07 の UAT で置き換える（目標 2） |
| **docked の窓を最小化** | 閉じるのと同じ（`via = "minimized"`）。その窓は最小化で、戻すと floating | 変更（既定の案 D2） |
| docked の窓を別の desktop へ移す | 閉じるのと同じ（`via = "moved"`） | 変更 |
| docked mode で新しい窓が開く | docked で開く（ws099-p033）。前の docked の窓は `dock-hidden` | 同じ |
| docked mode で desktop を変える | 先の desktop の前の窓を dock する（今の `layout_keep_front`、session の tablet mode）。空の desktop では何もしない | 同じ |
| 全画面の出入り | `layout_mode` を変えない。出る時は mode に戻る | 同じ |
| docked の窓が全画面になって、その窓を閉じる | 閉じるのと同じ（全画面の前に docked だった窓 = `fullscreen_docked`） | 変更 |

### 1.4 「docked の窓が無くなった」の検出

compositor には unmap を shell に知らせる hook が無いので、desktop ごとに「その desktop の docked mode の持ち主」を覚える:

- `server->dock_owner[DESKTOPS]`（`struct kwl_object *`）。`window_dock()`・`kwl_glass_open_docked()`・`kwl_glass_unfullscreen_docks()`・
  `layout_match()` が dock し直さなかった（もう docked の窓への切り替え）時に、その desktop の値をその窓にする。
- `objects.c` の surface の消滅（`server->drag` などを消している所）で、この窓を指す値を NULL にし、`server->dock_owner_gone` を立てる。
- `kwl_glass_tick()` の `layout_keep_front()` を `layout_follow()` に置き換える。docked mode で、表示中の desktop の持ち主が
  (a) 消えた（`dock_owner_gone`）、(b) `mapped` でない、(c) 最小化した、(d) 別の desktop に移った、(e) docked でも全画面でもなくなった
  のどれかなら `layout_leave(NULL, 理由)`。持ち主がまだ無い desktop（docked mode のまま desktop を変えた先）では、今の `layout_keep_front` と同じく前の窓を dock して持ち主にする。
- 最小化（`window_minimize()`）と別の desktop への移動（`window_to_desktop()`）は、その場で `layout_leave()` を呼ぶ（tick を待たない）。

### 1.5 試験（p002）

- host（`plan/ws181/tests/host-layout-state.c`）: `kwl_layout_state()` の表（mode × docked × 最小化 × 全画面 × 前の app か）、`layout_leave()` の後に I1 が成り立つこと（shell の外へ出した小さな関数で、窓の配列を相手に）。
- 今の試験の追従: `tests/scenarios/desktop/windows/layout-mode-switch.md` の 3〜5 段（後ろの docked の窓が見える・click で floating・閉じたら次も dock）は新しい規則に書き換える。ws142 の `plan/ws142/tests/host-layout.c` と `p010-guest.sh` の該当の段も直す（ws142 は incomplete で、p010 が使う回帰の試験なので直して残す）。
- QEMU（T1）: Files・Terminal・Calculator を開き、(1) Terminal を dock → Alt+Tab で Files → Files を restore → 3 つとも floating（`KWL LAYOUT windows … docked=0`）、Terminal を click しても大きさが変わらない。(2) Calculator を最小化 → Files を dock → Files を閉じる → Terminal は floating で見え、Calculator は最小化のまま（`minimized=1`）。

## 2. App Home の独立のモード（p003）

### 2.1 見た目

- 開く時、desktop の層（壁紙・窓）は**画面の上へ全部出ていく**（`kwl_home_layer()`: `x = 0`、`y = -progress × (高さ + 影の幅)`、`scale = 1 - 0.04 × progress`）。
  開ききった後は desktop の層を描かない（今の右下の角 `HOME_KEEP` の覗きと、それを click で閉じる `HOME_KEEP_NEAR` を削除）。
- 下から上への swipe（§3）で開く時、層は指に付いて上がる（`progress = 上への距離 / HOME_RISE_DISTANCE`、360 px）。離した時 `HOME_THRESHOLD`（0.30）以上で開き、未満で戻る。
- launcher・Super・左上の角からの drag で開く時も、同じ上への動き（左上の角の drag は今の対角の進みを progress に使う）。
- 閉じる時は層が上から戻ってくる（逆の動き）。

### 2.2 開く・閉じる

| 操作 | 結果 |
| --- | --- |
| 下端から上への swipe（desktop の上） | Home を開く（§3） |
| launcher の click・tap、Super の単独 | 開く・閉じる（今どおり） |
| 左上の角から右下への drag | 開く（今どおり） |
| Home の上で下向きの drag（縦が横より大きい、どこから始めても） | **desktop の層を上から引き下ろして閉じる**（指に付く、離した時の閾値は 0.30）。今の「左上への drag で閉じる」をこれに置き換える |
| Home の上で下端から上への swipe | 何もしない（既定の案 D4。今の「同じ swipe で閉じる」は覗きの角と組だったので外す） |
| Home の上で上端から下への swipe | Home を閉じ（animation 無し）、Wiseview を開く gesture を始める |
| Esc・app の起動・起動中の app の icon（BUG-232） | 閉じる（今どおり） |

### 2.3 試験（p003）

- host: `kwl_home_layer()` の値（progress 0・0.5・1 で y・scale）、Home の上の drag の縦と横の判定。
- 今の試験の追従: `tests/scenarios/desktop/home/` の中で角の覗き・下端の swipe で閉じるを使う段があれば直す。

## 3. 画面の端の gesture（p003）

pointer（mouse）と touch screen（first finger は shell の pointer の左 button、touch.c）は同じ経路を通るので、両方に同じ規則が効く。

| 始まり | 動き | 結果 | 今 |
| --- | --- | --- | --- |
| 下端（`y ≥ 高さ - 20`）、desktop の上 | 上へ | **App Home** | Wiseview |
| 上端の帯（`y < TOP_EDGE_BAND` = 10 px）、左上の角（28 px）と右上の角（`CORNER_ZONE` 28 px）を除く | 下へ `TOP_EDGE_START`（12 px）以上 | **Wiseview**（指に付いて開く: `progress = 下への距離 / WISEVIEW_DISTANCE`、240 px） | 無し（bar の press） |
| 上端の帯 | 動かずに離す | その点での bar の press（今の `bar_press()` を離した時に呼ぶ）。帯の中の press は離すまで遅らせる | 押した時に bar の press |
| bar の docked の title（帯の下） | どの向きでも drag | **pull**: 窓を floating にし、そのまま移動を続ける（§3.1） | 下向きに 140 px で外れる |
| 左上の角・右上の角 | 今どおり | App Home・Notes | 同じ |
| 全画面の上の下端 | 上へ | 全画面を出る（ws099-p015、今どおり） | 同じ |
| 全画面の上の上端の帯 | 下へ | Wiseview（既定の案 D6） | 無し |
| Wiseview の上の下端 | 上へ | Wiseview を閉じ（animation 無し）、Home を開く gesture | 無し |

- Super+Tab（Wiseview）・Super（Home）・Alt+Tab は変えない。
- **touchpad の gesture（ws142-p009 の BOTTOM2 → Wiseview、TOP2 → 最大化を窓に など）は変えない**（既定の案 D3。UAT の文は「画面の端」）。

### 3.1 dock bar を触ってからの drag

- 今の pull（`bar_press()` で始まり `glass_motion_take()` で追う）を使う。変える所は 2 つ:
  1. 外れる判定を下向きの距離だけでなく**押した点からの距離**（`hypot(dx, dy)`）にする（横や斜めの drag でも外れる）。外れるまでの距離は `PULL_DISTANCE` を 140 から 48 px に縮める（触って引き出す感じ）。
  2. 外れた後は今どおり `server->drag` に移って、離すまで窓が指（pointer）に付いて動く。窓の中の掴んだ点は title の横の位置の比で決める（今どおり）。
- docked の窓が外れたので `layout_leave()`（§1.2）が走り、他の窓も floating で見える。

### 3.2 試験（p003）

- host: 端の判定の純関数（`kwl_edge_classify(x, y, width, height, fullscreen, home, wiseview)` → none / home-up / wiseview-down / bar-deferred / corner）を `swipe.c` か新しい `edge.c` に置き、表で試す。pull の外れの距離の判定。
- QEMU（T1、touch の注入 `/dev/input-inject` の MT か pointer）: 下端から上 → `KWL HOME open via=edge`、上端から下 → `KWL WISEVIEW opened via=top-edge`、docked の title を横に drag → `KWL GLASS undock via=pull` の後に `KWL GLASS move` が続く。

## 4. 整列のメニュー（p004）

### 4.1 入口

- bar の仮想 desktop の切り替えの pill で、**今の desktop の絵を tap・click** → 整列のメニューを開く（もう一度で閉じる）。他の desktop の絵は今どおりその desktop へ（既定の案 D5）。
- メニューは desktop の pill の下に出る glass の popup（network の menu と同じ描き方）。行は 5 つ、各行に小さな図（枠の配置の線画）と名前:

| ID | 名前（英、翻訳の catalog へ） | 日本語 | 配置 |
| --- | --- | --- | --- |
| `columns` | Side by Side | 左右に並べる（水平に等分） | 横に n 等分（n ≤ 4） |
| `rows` | Stacked | 上下に並べる（垂直に等分） | 縦に n 等分（n ≤ 4） |
| `right-main` | One on the Right | 右に 1 つ、左に縦の分割 | 右半分に 1 つ、左半分を縦に n-1 等分（n ≤ 4） |
| `left-main` | One on the Left | 左に 1 つ、右に縦の分割 | 左右の逆 |
| `grid` | Grid | 格子 | `cols = ceil(sqrt(n))`、`rows = ceil(n / cols)`、最後の行の余りは横に広げる（n ≤ 9） |

- 対象の窓: 表示中の desktop の、map 済み・最小化でない・全画面でない・親を持たない toplevel（desktop の icon の surface を除く）。重なりの上から順に、その形の上限まで。上限を超えた窓は今の場所のまま下に残る（既定の案 D7）。
- n = 1 は全部の形で 1 つの枠（作業の領域の全体、ただし floating）。n = 2 の `right-main`・`left-main` は左右の 2 等分になる。対象が 0 の時、行は薄く描き、選んでも何もしない。
- 操作: pointer の click・touch の tap・鍵盤（↑↓ と Enter、Esc で閉じる）。メニューの外の press で閉じる。

### 4.2 枠の計算（`arrange.c`、純関数）

- 作業の領域: dock の領域（`docked_rect()`: bar の下、画面の keyboard の panel を除く）から四方に `ARRANGE_MARGIN`（8 px）を引いた矩形。枠の間は `ARRANGE_GAP`（8 px）。
- 枠は title bar を含む: 窓の body は枠の上から `KWL_GLASS_TITLE + KWL_GLASS_GAP` 下から始まり、枠の下まで。body の幅・高さは整数に丸め、余りは最後の枠に足す。
- `kwl_arrange_slots(layout, n, area, slots[])` が枠の矩形を返す。

### 4.3 どの窓をどの枠に（目標 5「今の位置から大まかに」）

- 各窓の body の中心と各枠の中心の距離の 2 乗の和が最小になる割り当て（n ≤ 9 なので部分集合の DP: `dp[mask]`、2^9 × 9 の手間）。等しい時は重なりの上の窓を先の枠に（決まった結果になる）。
- `kwl_arrange_assign(centres[], n, slots[], order[])`（arrange.c、純関数、host で試す）。

### 4.4 適用

1. docked mode なら先に `layout_leave(front, "arrange")`（全部 floating に）。
2. 各窓を割り当てた枠へ: `x, y, window_width, window_height` を枠の body に、`placed = 1`、configure を送る、`window_resized()`。
3. 動きは `ARRANGE_MS`（180 ms）で今の場所から枠へ滑らせる（窓ごとに from・to と始まりの時刻を持つ。今の `server->anim` は 1 つの窓だけなので、整列は `arrange` の構造の中に自分の glide を持ち、`body_rect()` がそれを見る）。
4. 整列モードに入る（§5）。log: `KWL ARRANGE apply layout=<id> desktop=<n> windows=<n> slots=<id>@x,y,w,h;...`。

## 5. 整列モード（p004）

### 5.1 状態

- desktop ごとに `struct kwl_arrange { unsigned on; unsigned layout; unsigned count; struct { struct shell_rect slot; struct kwl_object *window; } slots[9]; ... }`（`server->arrange[DESKTOPS]`）。
- 整列モードの間、枠の窓の title bar の drag は**移動でなく入れ替え**になる（§5.2）。他の操作（click で前へ、閉じる、最小化）は今どおり。

### 5.2 title bar の drag で入れ替え（目標 6）

- 押して動かし始めると（今の移動の開始と同じ判定）、窓は pointer に付いて動く（移動と同じ見た目）。pointer の下の枠（自分の枠を除く）を glass の縁で示す。
- 離した点が:
  - **別の枠の中** → 2 つの窓の枠を入れ替える。掴んだ窓はその枠へ、相手の窓は掴んだ窓の元の枠へ、どちらも `ARRANGE_MS` で滑る、configure を送る。log `KWL ARRANGE swap a=<id> b=<id> slots=<i>,<j>`。
  - **system bar の中**（今の「title を bar へ drag で dock」）→ その窓を dock し、**整列モードを終える**（§5.3）。
  - それ以外（自分の枠、枠の外） → 元の枠へ滑って戻る。

### 5.3 整列モードが終わる時

| 出来事 | 結果 |
| --- | --- |
| 整列モードで title bar の **double click**（今の dock） | その窓を dock、整列モードを終える。log `KWL ARRANGE end reason=dock` |
| title bar を **dock bar（system bar）へ drag** | 同じ |
| maximize の button | 同じ |
| docked の窓を floating に戻す（restore・pull・double click） | `layout_leave()` で**全部が普通の floating** になる。窓は整列の時の場所と大きさにいる（dock の前の `restore_*` が枠）が、**整列モードではない**（title の drag は普通の移動）。整列の解除の手順は要らない（目標 6） |
| 枠の窓が閉じる・最小化・別の desktop へ・全画面 | 整列モードを終える（残りの窓はその場の floating）。log `reason=closed` など |
| 表示中の desktop に新しい窓が開く | 終える（新しい窓は今の置き方で） |
| 枠の窓の大きさを縁で変える | 終える |
| メニューでまた形を選ぶ | その形で整列し直す（モードは続く） |

- 終わった後の窓は全部ただの floating で、整列の記録は残さない（`arrange.on = 0`、枠の pointer を消す）。
- surface の消滅は `objects.c` で `arrange.slots[].window` を NULL にして終える（§1.4 と同じ所）。

### 5.4 試験（p004）

- host（`plan/ws181/tests/host-arrange.c`）: 5 つの形 × n = 1〜上限の枠（重ならない、領域の中、隙間 8 px）、割り当ての DP（左右に並んだ窓は `columns` で左右の順を保つ、4 隅の窓は `grid` で同じ隅へ、総当たりと同じ最小値）。
- QEMU（T1）: 窓 3 つで今の desktop の絵を click → メニュー（撮影）→ `right-main` → `KWL ARRANGE apply`（撮影）→ 左上の窓の title を右の枠へ drag → `KWL ARRANGE swap` → 1 つを double click → `reason=dock` と `KWL LAYOUT mode=docked` → restore → `KWL LAYOUT windows … docked=0` で、title の drag が普通の移動（`KWL GLASS move`、swap でない）。

## 6. Phase の分け方と見積もり

| Phase | 内容 | 見積もり |
| --- | --- | --- |
| p002 | §1: `kwl_layout_state`、`layout_leave`・`window_float_quiet`、`dock_owner` と `layout_follow`、`layout_press_switches` の削除、最小化・移動・閉じるの規則、log、host 試験、ws142 の試験と scenario の追従 | 0.6 LW |
| p003 | §2・§3: `kwl_home_layer` の上への動き、覗きの削除、Home の上の下向きの drag、端の判定（下 → Home、上 → Wiseview、帯の遅れた bar の press）、Home ↔ Wiseview の移り、pull の距離、host 試験、scenario の追従 | 1 LW |
| p004 | §4・§5: `arrange.c`（枠・割り当て）、メニュー（描画・入力）、適用と glide、整列モードの入れ替え・終わり、log、host 試験、AAT の scenario を draft で | 1.2 LW |

## 7. 人の判断（既定の案で進める、Q1 に送る）

- **D1（知らせ）**: docked の窓を閉じた時、2026-10-06 の決定（次の窓も最大化）を 2026-10-07 の UAT（他の窓は floating で表示）で置き換える。
- **D2**: docked の窓の**最小化**と**別の desktop への移動**も閉じるのと同じ（docked mode を終えて他の窓を floating で見せる）。既定: そうする。
- **D3**: touchpad の gesture（BOTTOM2 → Wiseview、TOP2 → 最大化を窓に、ws142-p009）は変えない（UAT の文は「画面の端」）。案: 画面に揃えて BOTTOM2 → Home、TOP2 → Wiseview にもできる。既定: 変えない。
- **D4**: Home の上で下端から上への swipe は何もしない（今は Home を閉じる）。Home を閉じる gesture は「Home の上の下向きの drag」。既定: そうする。
- **D5**: 整列のメニューは**今の desktop の絵**の tap で開き、他の desktop の絵は今どおり切り替え。案: pill のどこでもメニューを開き、メニューの中に desktop の切り替えも置く。既定: 今の desktop の絵。
- **D6**: 全画面の上で上端から下 → Wiseview（下端から上は今どおり全画面を出る）。既定: そうする。
- **D7**: 形の上限（`columns`・`rows`・`right-main`・`left-main` は 4、`grid` は 9）を超えた窓はその場に下に残す（最小化はしない）。既定: そうする。

## 8. 作らない物（正常系の外、backlog-p2.md へ）

- 整列の時、client の最小の大きさが枠より大きい窓（client の描いた大きさのまま枠の左上に置く。重なりを避ける詰め直しはしない）。
- 大きさの固定の窓の整列（枠の中央に置く letterbox はしない）。
- 整列モードの間の画面の回転・解像度の変更（モードを終える、枠を計算し直さない）。
- 整列モードを再起動の後に覚えること。
- 鍵盤だけでの入れ替え。
- touchpad の gesture の変更（D3 の既定）。

## 9. design-reviewer の指摘と反映

2026-10-07 の design-reviewer（第 1 版に対して）の指摘。**まだ本文に反映していない**（利用の上限でラップアップ）。次の世代が反映して第 2 版にする。

- Blocking:
  - B1: `kwl_glass_open_docked()` は map の前（protocol.c:945）。そこで `dock_owner` を付けると、§1.4 の (b)「mapped でない」で直ちに leave になる。map の前の `desktop` も違う。→ 持ち主は map の後か tick の観測で付ける。
  - B2: `layout_match` を通らない前面化がある（edit.c:349 の FOCUS previous、corner.c:770、`window_to_desktop`、`window_lower`・2 本指の flick）。→ 持ち主は毎 tick「表示中の desktop の `sheet_owner(kwl_top_window())` が maximized ならそれ」と観測し、その前に前回の持ち主の消滅・最小化・移動・undock を判定する。lower と同じ app の窓の raise の規則を表に足す。
  - B3: `layout_leave` で `dock_owner[]` を全部消すこと、gone を desktop ごとに持つこと、裏の desktop の持ち主が消えた時の規則（session なので即 leave が既定の案）が無い。
  - B4: 上端の帯の遅らせを `bar_press()` に置くのは誤り。launcher（x<40）・media・IME・volume・network・menu-shell・apps bar・titlebar の controls が先に press を取る。→ 帯の判定を `kwl_glass_button` の先頭近く（power dialog・switcher・Wiseview・corner・keyboard の後）に置き、離した時は press と release を流し直す。下以外へ 8 px を超えたら press を流し直して motion を続ける。左上の除外を launcher の幅 40 に合わせる。
- Should-fix:
  - S1: I3「整列中なら WINDOWED」を足し、dock の全経路（keep_front、open_docked、gesture_fullscreen の layout_set、client の MAXIMIZE、switch_to）で全 desktop の整列を終える。
  - S2: 横に pull して bar の中で離すとまた dock する（`kwl_glass_toplevel_move_end` の y<BAR）。→ 一度 bar の外へ出てからだけ dock する。`pulled_rect` の補間を新しい距離に、`pull_start_x` を足す。
  - S3: 整列の適用で前の窓に anim が 2 つ重なる。→ 前の窓も静かに戻し、中心は leave の後の `restore_*` から計算する。
  - S4: dock で開いた窓は全部同じ `dock_restore_default` の場所 → `window_float_quiet` で `kwl_glass_place` の段ずらし。
  - S5: 移動の開始の経路が他にもある（`kwl_glass_press_move`、xdg move の CSD、touch の ROUTE_TITLE）。経路ごとに入れ替えか終了かを表に。入れ替えの drag は `server->drag` と別の状態に。CSD の窓の枠は body を枠の全体に。
  - S6: 整列モードの印が無い。「新しい窓」は親を持たない toplevel に限る。印と、閉じた時に詰め直すかを §7 に。
  - S7: D2 の移動は、連れて行く操作（Ctrl+Alt+Shift+矢印）なら docked のまま連れて行く。leave は見えなくなる移動（Wiseview の drag）だけ。corner.c の Notes も同じ規則で。
  - S8: 上端の帯を mouse にも効かせるか、全画面で 10 px を奪うことを §7 に。
  - S9: Home の上の bar を残すか、左上の角の drag（右下へ）と層の上への動きの向き、Home の上の下向きの drag と上端の帯の優先を表に。
  - S10: ws142 の試験（host-layout.c・p010-guest.sh）と ws142-p007・p008 の決定を変えるので、Q1 の許可と ws142 への comment が要る。
  - S11: §6 に依存を書く（p002 → p003、p002 → p004、p003 → p004）。
  - S12: 試験を足す（docked mode で app を開いて 1 s 後も docked、kill -9、裏の desktop の持ち主が閉じる、FOCUS previous・flick の後に閉じる、帯の中の各 widget の click、横の pull を bar で離す、整列中に別の desktop で dock して戻る、CSD の窓を含む整列）。
- Minor:
  - M1: `DESKTOPS` と `struct shell_rect` は shell.c の private（kwl.h は `KWL_APPS_DESKTOPS`）。
  - M2: `wiseview_progress` は上向きで計算する。上端から開くには向きの flag と `wiseview_current` が要る。
  - M3: 毎 tick 調べるより `display.c:360` に `kwl_glass_unmapped()` を足す方が確か。
  - M4: `window_undock` → `layout_leave` が再帰しない形にする。
  - M5: `window_float_quiet` は `anim`・`pull`・`click_docked` を消す。
  - M6: I2 の文を同じ app の docked の窓に合わせる。
  - M7: 整列の領域は落ち着いた値（`kwl_keyboard_reserved`）を使う。
  - M8: 上の段の枠へ入れ替えようとして bar に入ると dock される。
  - M9: 整列中の desktop の切り替え、Wiseview で整列中の desktop へ窓を移す場合。
- D の判定: D3（touchpad を変えない）と D5（今の desktop の絵だけ）は利用者の文からずれる疑い → 既定にせず聞く。D2 は移動の部分が誤り（S7）。D6 は帯を mouse に効かせるかが抜けている。D1・D4・D7 は妥当。
