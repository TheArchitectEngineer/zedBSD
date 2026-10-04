<!-- awesome-plan project=zedbsd record=ws142-p001 -->

# ws142-p001: デスクトップのアプリの切り替えの設計

Status: in-progress（2026-10-05 P1 generation17 / q701-i01。設計を書いた。人間の判断が要る点 D1〜D11 が残る）
Disposition: normal
Parent: [WS142](../ws.md)
Queue: q701 / q701-i01（Q1 の投入、ベータ2）。design-reviewer は省く（2026-10-05 ユーザー）。code は書かない

## 前提（ws.md のユーザーの要望 1〜5）

1. Super（Windows キー）でアプリの一覧（App Home）。
2. タッチパッドの下の端から 2 本指で上 → Wiseview（できなければ 3 本指で上）。
3. 左の端から右へ・右の端から左への 2 本指 → 仮想デスクトップの左・右へ。
4. 窓を最大化していない時、上部のバーに開いているアプリの icon。hover で約 25% のプレビュー、複数の窓は並べる、click で最前面（複数なら待たずにプレビュー、プレビューの click でその窓）。最大化の時は icon を出さず Wiseview に任せる。
5. 3 本指のタップ・Alt+Tab で「次のアプリ」をプレビューした状態、2 本指の左右のスワイプ（Tab・Shift+Tab）で移る、タップ（Alt を離す）で切り替え。最大化の時は画面の中央に同じ popup。

## 今の実装（2026-10-05 に source を読んだ）

| 所 | 今 |
| --- | --- |
| Wiseview（`shell.c`、ws035-p063） | 下の端からの pointer の drag で開く（`WISEVIEW_EDGE` 20 px、`WISEVIEW_DISTANCE` 240 px で全開、`WISEVIEW_THRESHOLD` 0.35）。今の仮想デスクトップの窓だけ（`wiseview_windows`、`map_order` の新しい順 = 最後に前に出た順）。Super+Tab でも開く。tile の描画は `draw_tile`（窓の image を縮小、題、close button） |
| App Home（`home.c`） | 左上の角からの gesture と launcher の click（`home_open(via)`、log `ZWL HOME open via=…`）。開いている間は key を全部取る |
| 仮想デスクトップ（ws035-p065） | 4 つ（`DESKTOPS`）。左右の端からの pointer の drag で滑る（`desktop_offset`、`DESKTOP_MS` 220）。Ctrl+Alt+←→、Shift で窓ごと。`desktop_turn(server, target, via)` |
| 上部のバー（`shell.c` `bar_layout`、44 px） | 左: launcher、線、docked（最大化）の窓の題と button。中: 4 つの desktop。右: IME・音量・network・電池・時計。fullscreen の窓が上を覆う時はバーを出さない（`bar_hidden`） |
| 最大化 | 窓を dock する（`surface->maximized`）= 題がバーに入る。Keiland の「最大化」はこれ |
| アプリの mark | 題の左の mark: `zwl_icon_for_app_id`（App Home の絵と色）か名前の頭文字（`draw_picture_mark`・`mark_name`） |
| 鍵盤の経路（`seat.c` `zwl_seat_key`） | lock → IME の早い key → Home → network → menu → titlebar → `zwl_glass_key`（音量、Wiseview、Super+Tab、Super+Alt+文字（edit.c、Super+Alt+P が前のアプリ）、Ctrl+Alt+←→）→ client |
| 修飾 key | `server->modifier_keys`（左右の Shift・Ctrl・Alt・Meta の bit）。Super 単独の判定は無い。Alt+Tab は client に行く |
| タッチパッド（`touchpad.c`、ws159-p004） | 1 本指の移動、tap（1 本左・2 本右・**3 本で中 button**、BUG-166 の規則）、tap-drag、click pad、2 本指の scroll（natural）。pad の外形（座標の最大値）は知らない。gesture（端・本数で別の動作）は無い |
| 5330 のタッチパッドの端 | `latitude5330-linux/touch-pad-evdev.txt.gz`（ユーザーの 60 秒の操作、2783 個の座標）: X は 0〜1336、Y は 0〜760 の全域が出る。X < 40 が 25 個、X > 1296 が 27 個、Y > 720 が 103 個。**firmware は端の接触を消さないので、端の検出はできる**（要望 2 の「利用できないなら」には当たらない） |
| 試験の注入 | `/dev/input-inject`（CONFIG_INPUT_TEST_INJECT）の touch pad（ws159-p003、`touchinject` の `pad`）で多点の座標を QEMU に入れられる。QMP の key・pointer。`wltest --app-id=NAME` で app ID の違う窓 |

## 設計

### A. 共通の模型: アプリの一覧（新 `apps.c`・`apps.h`、純粋）

- 入力: 今の仮想デスクトップの、map 済み・image のある toplevel（sheet・desktop の surface・panel・cursor を除く。`wiseview_windows` と同じ条件）。各窓の `app_id`、client、`map_order`（前に出た順）、新しい `open_order`（最初に map した順、`zwl_object` に足す）、最小化か。
- まとめ方: `app_id` が同じ窓を一つのアプリにする（同じ app の 2 つの process も一つ）。`app_id` が空なら client ごとに一つ（D7）。
- 二つの並び:
  - **バーの並び**: アプリの最初の窓の `open_order` の古い順（新しいアプリは右に足され、並びが動かない）。
  - **MRU の並び**: アプリの窓の `map_order` の最大の新しい順（今のアプリが先頭）。
  - 各アプリの窓は `map_order` の新しい順。
- 純粋な関数 `zwl_apps_build(windows, count, apps, capacity)` で作り、描画と入力は結果を読むだけ。host で試せる。

### B. 「最大化」の判定

- 今のデスクトップに docked の窓がある（バーにその題が出ている）か、fullscreen の窓がバーを隠している（`bar_hidden`）時を「最大化」とする（D6）。この時バーにアプリの icon を出さない。切り替えの popup は中央。

### C. Super 単独 → App Home（要望 1）

- 状態: `super_tap`（0/1）、`super_down_ms`。
- Super（左右の Meta）を、他の修飾も key も押していない時に押す → `super_tap = 1`。
- Super を押している間に他の key の押下、pointer の button・scroll、タッチの接触 → `super_tap = 0`（Super+Tab・Super+L・Super+Alt+文字はそのまま動く）。
- Super を離す時、`super_tap` かつ 1 秒以内 → App Home を開く（開いていれば閉じる）。`home_open(via=super)`、log `ZWL SUPER home open|close`。
- Super の押下・解放は今まで通り client にも渡す（D9）。lock・greeter の間は何もしない。

### D. タッチパッドの gesture（要望 2・3、`touchpad.c` を拡張）

- `zwl_touchpad_init` に pad の座標の最大値（`x_max`、`y_max`、evdev の absinfo）を足す。
- 新しい action `ZWL_TOUCHPAD_GESTURE`: `kind`（BOTTOM2・UP3・LEFT2・RIGHT2・TAP3）、`phase`（BEGIN・UPDATE・END・CANCEL）、主の軸の移動（µm）、END の速さ。
- 端の帯: `EDGE_UM` = 6 mm（5330 で 72 単位。実機の UAT で調整）。
- 2 本指: 2 本目が置かれた時に両方の始点を記録。
  - 両方の始点の Y が `y_max − EDGE` 以上 → BOTTOM の候補。X が `EDGE` 以下 → LEFT の候補、`x_max − EDGE` 以上 → RIGHT の候補。
  - 重心が 4 mm 動いた時、候補があり、動きが内向きで主の軸が副の軸の 2 倍以上 → gesture の BEGIN（その触れている間は scroll を出さない）。それ以外は今の 2 本指の scroll。
- 3 本指: 重心が上へ 8 mm、縦が主 → UP3 の BEGIN（要望 2 の代わりの動作。D3）。
- 指が離れて END（速さ付き）、本数が増えたら CANCEL。
- 3 本指の tap: 今は中 button（BUG-166 の規則）。要望 5 では切り替え。**D1**。案: TAP3 は gesture（切り替え）にし、中 button は 3 本指で pad を押す click に移す。
- 指に付いて動く（D10）:
  - BOTTOM2・UP3 → Wiseview の開き具合 = 移動 / 40 mm（pointer の `WISEVIEW_DISTANCE` の代わり）。END で 0.35 以上か上への速さ（≥ 100 mm/s）なら開く、それ以外は閉じる（pointer の drag と同じ `wiseview_settle`）。Wiseview が開いている時、BOTTOM2 の逆（上の端から下）は作らない。下への 2 本指は scroll のまま。閉じるのは今の操作（click・Esc・下への drag）。
  - LEFT2（右へ動く）→ 左の仮想デスクトップへ。RIGHT2（左へ動く）→ 右へ。`desktop_offset` を移動 × (出力の幅 / 60 mm) で追わせ、END で半分以上か速さで `desktop_turn`、端のデスクトップでは戻る（ws035-p065 と同じ）。
- shell の入口: `zwl_glass_gesture(server, &action)`。pointer の端の drag の処理（`wiseview_edge_press`・desktop の swipe）と同じ状態（`wiseview_gesture`・`desktop_dragging` など）を、pointer の代わりに gesture の移動で進める。

### E. バーのアプリの icon とプレビュー（要望 4、新 `apps-bar.c`）

- 場所: 最大化でない時、`bar->title_x`（launcher の線の右）から `bar->desktops_line − 16` まで。1 つ 36 px（28 px の mark、`draw_picture_mark` を大きさの引数付きにし、頭文字の mark も同じ）。
- 今のアプリ（focus の窓のアプリ）の icon の下に小さな点。全部の窓が最小化のアプリは薄く。
- 入りきらない時は最後の枠を「+N」にし、click で Wiseview。
- hover の状態機械:
  - IDLE: pointer が icon に入る → ARMED（icon、時刻）。
  - ARMED: `HOVER_MS` = 400 ms 留まる → SHOWN(hover)。出る → IDLE。
  - SHOWN: 別の icon に移る → 待たずにその icon の SHOWN。icon と popup の両方から出て `LEAVE_MS` = 300 ms → IDLE。
  - icon の click: 窓が 1 つ → その窓を前に（最小化なら戻す）、IDLE。複数 → 待たずに SHOWN(click)。SHOWN(click) の icon の click で閉じる。
  - プレビューの click → その窓を前に、IDLE。Esc → IDLE。
- popup: icon の下（バーの下端 + 8 px）、画面の中に収める。アプリの窓を MRU の左から並べる。
  - 1 枚の大きさの上限: 出力の幅・高さの 25%（線の長さで 25%。D4）。
  - 並びが画面の幅を超えたら、全部を同じ割合で縮める（最小 12%）。それでも超えたら 2 段。
  - 各プレビューは Wiseview の `draw_tile`（窓の image の縮小、題、hover で close の ×）。最小化の窓は薄く。
- 描画の順: 窓の上、menu・network の menu の下（glass の層）。image は合成済みの窓の import を縮小して描くだけで、写しは作らない（Wiseview と同じ）。

### F. 切り替えの UI（要望 5、新 `switcher.c`、純粋な状態機械 + 描画）

- 状態: OFF / ON（入口: keys・pad、アプリの MRU の snapshot、選択の index、置き場: bar・center）。
- 開く（Alt+Tab、TAP3）:
  - 今のデスクトップのアプリの MRU の snapshot を取る。
  - index = 1（「1 つ次」= 直前に使ったアプリ。1 つしかなければ 0、0 個なら開かない）。
  - 置き場: 最大化なら center、それ以外は bar。
  - log `ZWL SWITCH open via=… index=… app=… placement=…`。
- 進める: Tab・→・2 本指で右 → +1、Shift+Tab・←・2 本指で左 → −1（端で一周）。2 本指は、ON の間は pad の横の scroll を client に渡さず、指の移動 12 mm で 1 歩（natural scroll の向きに依らず、指の向き）。縦は無視。
- 決める: Alt を離す、1 本指の tap、pad の click、Enter → 選んだアプリの最新の窓を前に（最小化なら戻す）。pointer でプレビューを click → その窓。OFF。
- やめる: Esc → 何も変えずに OFF。ON の中で TAP3 をもう一度 → +1。
- 置き場 bar: 選んだアプリのバーの icon を hover の見た目で光らせ、E の popup をその icon の下に出す（E の SHOWN と同じ描画、via=switch）。
- 置き場 center: 画面の中央に、アプリの icon（48 px）の列を MRU の順に並べ、選んだものを光らせる。その上に選んだアプリの窓のプレビューを出す（E と同じ大きさの規則）。
- 鍵盤の経路: `zwl_glass_key` の先頭で Alt を押した状態の Tab を取る（client には渡さない）。Alt の押下・解放は今まで通り client に渡る。
- 並びの考え方は **D2**。案: バーは開いた順（並びが動かない）、切り替えは MRU（Alt+Tab の慣例: 2 回で元に戻る）、置き場 bar では光る icon が並びの中を跳ぶ。
- 遅れの目標: 操作（key・tap・click）からプレビューの最初の frame まで 100 ms 以内（hover の待ちを除く。BUG-179 の 0.1 秒に揃える）。log の `at_ms` で量る。

### G. 既存の操作との関係

- 残す: pointer の下の端の drag（Wiseview）、Super+Tab（Wiseview）、Ctrl+Alt+←→、Super+Alt+P（前のアプリ）、pointer の左右の端の swipe。
- 3 本指の tap は D1 で決まる。
- 2 本指の scroll は、端からの内向きの始まり以外は今のまま。ON の間だけ横は切り替えに使う。

### H. log（試験が読む）

- `ZWL APPS bar count=N apps=a,b,…`
- `ZWL APPS icon app=… x= y= width= height=`
- `ZWL APPS preview app=… windows=K via=hover|click|switch`
- `ZWL APPS preview window surface=… x= y= width= height=`
- `ZWL APPS raise surface=… via=bar|preview|switch`
- `ZWL SWITCH open|step|commit|cancel …`
- `ZWL SUPER home open|close`
- `ZWL GESTURE kind=… phase=… travel_um=…`

## 試験の方法

| 段 | 何を |
| --- | --- |
| host | `apps.c`（まとめ方、二つの並び、最小化、app_id が空）、`switcher.c`（開く・進める・一周・決める・やめる・1 つ・0 個、key と pad の入口）、Super 単独の判定（他の key・button で取り消し、1 秒）、`touchpad.c` の gesture（実際の 5330 の範囲の座標の台本: 下の端の 2 本指の上、左右の端の 2 本指、端でない 2 本指は scroll のまま、3 本指の上、TAP3、本数の増加で CANCEL、端の帯の境目）。ws142 の `tests/` に置く |
| QEMU（T1、pen の image: input-inject あり） | `wltest --app-id` で app a・b（窓 2 つ）・c を開く。<br>QMP の key: Super 単独 → `ZWL SUPER home open`、Super+Tab は Wiseview のまま、Alt+Tab（1 回 → b、Tab でさらに、Shift+Tab、Alt を離す → `APPS raise`）、Esc でやめる、dock した窓がある時は center。<br>qmp-pointer: hover 400 ms でプレビュー、click（1 つ → raise、複数 → すぐプレビュー → プレビューの click で raise）。<br>touchinject の pad: 下の端の 2 本指 → Wiseview、左右の端の 2 本指 → `desktop` の移動、TAP3 → SWITCH open、2 本指の横 → step、tap → commit。<br>PNG: バーの icon、プレビュー、中央の popup。合否は log と PNG |
| 実機（5330、UAT） | 端の帯の幅・閾値・速さの感じ、プレビューの見え方、Alt+Tab の慣れ |

## Phase の分け方（案）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p002 | C: Super 単独 → App Home（host の試験、QEMU の key） | p001 |
| p003 | D: タッチパッドの gesture（touchpad.c の拡張と shell の入口、Wiseview・デスクトップが指に付いて動く）。host の試験、QEMU の input-inject、実機 | p001、D1・D3・D10 |
| p004 | A・B・E: アプリの模型、バーの icon、hover・click のプレビュー、最大化の時に出さない | p001、D2・D4〜D7・D8・D11 |
| p005 | F: 切り替え（Alt+Tab・TAP3・2 本指・中央の popup） | p003（TAP3・2 本指）、p004、D1・D2 |
| p006 | 全文の規約、QEMU の回帰（T1）、実機の UAT | p002〜p005 |

p002 は D に依らないので先に進められる。p004 は D の答えが既定の案のままなら進められる。

## 人間の判断が要る点

| D | 問い | 選択肢 | 案 |
| --- | --- | --- | --- |
| D1 | 3 本指の tap は中 button（BUG-166 で決めた規則）か、切り替え（要望 5）か | (a) 切り替えにし、中 button は 3 本指で pad を押す click へ (b) 切り替えにし、中 button は無し (c) 中 button のままにし、切り替えは別の動作 | (a) |
| D2 | アプリの並び。「1 つ次のアプリ」は何の次か | (a) バーは開いた順、切り替えは MRU（直前のアプリが「次」。置き場 bar では光る icon が跳ぶ） (b) どちらも MRU（バーの並びが使うたびに動く） (c) どちらも開いた順（「次」は右隣） | (a) |
| D3 | 3 本指の上へのスワイプ（Wiseview） | 5330 は端を出すので 2 本指の下の端は使える。(a) 3 本指の上も常に有効（どこからでも） (b) 2 本指の下の端だけ | (a) |
| D4 | プレビューの「25%」 | (a) 幅・高さが画面の 25% まで（面積は約 6%） (b) 面積で 25%（幅・高さが 50%） | (a) |
| D5 | バー・切り替えに出す窓の範囲 | (a) 今の仮想デスクトップの窓だけ（Wiseview と同じ） (b) 全部のデスクトップ（選ぶとそのデスクトップへ移る） | (a) |
| D6 | 「最大化している時」 | (a) docked の窓がある（バーに題が出ている）か、fullscreen がバーを隠している時 (b) 前面の窓が docked の時だけ | (a) |
| D7 | アプリのまとめ方 | (a) app_id ごと（同じアプリの 2 つの process も一つ） (b) process ごと | (a) |
| D8 | 時間 | hover の待ち 400 ms、離れて閉じるまで 300 ms、Super 単独は 1 秒以内に離した時だけ、Alt+Tab の popup はすぐ出す | 左の値 |
| D9 | Super の押下・解放を client にも渡すか | (a) 渡す（今のまま） (b) 単独の tap になった時は渡さない（押下は先に渡っている） | (a) |
| D10 | gesture は指に付いて動くか、閾値で発動するか | (a) 付いて動く（Wiseview が開いていく、デスクトップが滑る） (b) 閾値で発動 | (a) |
| D11 | 最小化の窓 | (a) バー・切り替え・プレビューに出し、選ぶと戻す（薄く描く） (b) 出さない | (a) |

（細かい既定: プレビューの hover で close の × を出す、「+N」の click で Wiseview、端の帯 6 mm。どれも実機の UAT で調整する）

## 残り

- D1〜D11 をユーザーが決めた後に p002〜p006 を Queue に入れる（p002 は先に始められる）。
- 端の帯・閾値は実機（5330）の UAT で確かめる。

## ユーザーの決定（2026-10-05 未明、Q1 が 1 問ずつ聞いた）

- D1: 3 本指のタップは **switcher**（中ボタンは 3 本指の押し込みへ）。
- D2: **bar は開いた順、切り替えは最近使った順（MRU）**。
- D3: **両方で開く**（2 本指の下端のスワイプと、3 本指の上スワイプ（どこでも））。
- D4: プレビューの上限は**画面の幅と高さの 25%**。
- D5: **今のデスクトップの窓だけ**。
- D6: 「最大化の状態」は **docked だけ**。ユーザー「dockedだけです。fullscreenはコンポジタが動作しません。」→ fullscreen の間は compositor の bar・switcher・gesture は動かない（設計の fullscreen の扱いを外す）。
- D7: **app_id でまとめる**。
- D8: 時間は**案のまま**（hover 400 ms・離れて 300 ms・Super 単独は 1 s 以内・Alt+Tab は即時、実機の UAT で調整）。
- D9: Super を client に**渡さない**（案と逆。p002 の super-tap はこれに合わせる）。
- D10: gesture は**指に追従**。
- D11: 最小化した窓を**含める**（薄く描き、選べば戻す）。
- 追加の要望（2026-10-05 未明、ユーザー原文「barのapp iconはドラッグで順番を入れ替えられるようにしましょう。app内のウィンドウが複数の仮想デスクトップにあるとき、各デスクトップでアイコンを表示して、各デスクトップでアイコン順をドラッグ可能にします。プレビューはそのデスクトップのウィンドウだけにします。」）→ bar の app の icon はドラッグで並べ替えられる。順はデスクトップごとに持つ。app の窓が複数のデスクトップに在れば、各デスクトップの bar に icon を出し、プレビューはそのデスクトップの窓だけ（D2 の「開いた順」は並べ替えの前の初期の順）。p004 の範囲に入れる。
