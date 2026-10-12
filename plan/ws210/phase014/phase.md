<!-- awesome-plan project=zedbsd record=ws210-p014 -->

# ws210-p014: libkeiland の frame の枠組み（lit の変化の damage、motion の合流と描き直しの取り消し、ペインの部品）

Parent: [WS210](../ws.md)
Status: in-progress（P1。(a)(b) と app の移行は merge 済み。(c) F2.1 と F2.2〜F2.6（Settings、KL_VERSION 92）と F2.7 の申請（Calendar・Files・Music・Photos・Phone・Mailer）を実装、Pi の確認待ち。(d) F4 は保留）
Phase disposition: normal
Queue: [P1 の lane](../../agents/P1/queue.md)

## 由来

- 2026-10-12 ユーザー（Pi 4、place9 の前の版）:「Settingsの左ペインは、単項目のポイントは200msくらいの遅延で済んでいますが、マウスの通った軌跡のすべてで描画を予約してしまって、遅延と描画が積み重なっています。マウス移動イベントを受け取ったとしても、一定のFPS以上には処理しない工夫や、イベントをpeekして次もマウスイベントなら、一定のFPS以上ならマージするような統一的な枠組みが必要かも。」
- 同日の補足:
  - 「一律にFPSでマウス移動イベントをマージするのではなく、ウィンドウごとに申請されたらそうする感じがいいですね。」
  - 「書き直しが完了する前の書き直しイベントが来たら前の分をキャンセルするのでもいいです。ペインの実現方法がlibkeilandにあるなら、それがいい。」
  - 訂正:「前の描き直しが始まる前に次の描き直しが来たら、ですね。訂正します。レンダリングの途中キャンセルは意図してません。」→ 取り消すのは、まだ描き始めていない予約だけ（積んだ damage・未着手の frame）。描いている途中の frame（CPU の描画・present）は止めずに最後まで描き、その後に最新の状態で次を描く。
- Q1 の依頼:
  - (1) motion の合流（間の lit の変化の damage を積まない）。
  - (2) frame の速さの上限（compositor の frame callback まで描かない）。
  - (3) app ごとの hover_pending・lit_changed・damage の仕組みを libkeiland の共通の口に寄せ、6 つの app を順に載せ替える。
  - (4) 前の描き直しが始まる前に新しい描き直しが来たら、前の予約を取り消す（描いている途中の frame は止めない）。
  - (5) list・sidebar のペインは libkeiland の部品で hover と合流を持つ。
- [p013](../phase013/phase.md)（present の部分の copy）とは別の Phase。p013 は 1 frame の present を軽くする。この Phase は、描く frame の数と、1 frame に描く面積を減らす。

## 今の作り（agent/p1 1a3281d40 の source で確かめた）

| 所 | 作り | 問題 |
| --- | --- | --- |
| main loop | 6 つの app とも `kl_app_dispatch(app, wait)` → 溜まった入力を全部 take → 1 frame（例: settings/main.c:465-555、music/main.c:460-524）。present は FIFO で、libvulkan の WSI が前の frame の callback を待ってから commit する（wsi-wayland.c:774-783）。描くのが遅いと、その間に motion が数十溜まる | 次の 1 frame で全部の motion を順に処理する |
| Settings の hover | `ui_motion`（settings/ui.c:984-1049）が motion ごとに「前の lit」と「今の lit」の両方の矩形を `hover_damage`（外接の 1 つ）に足す | 1 frame の間に通った全部の行の外接の箱を描く。**ユーザーの言う「軌跡のすべて」**。frame が長くなり、次の frame にもっと溜まる |
| Files の hover | 同じ型（`fm_ui_damage`、2 つの部分、03a97b3c5） | 同じ。2 つまで分けるが、通った行は全部入る |
| kl_ui の hover | `kl_ui_pointer_motion`（libkeiland/ui/ui.c:430-475）が hot の変わるたびに、古い hot と新しい hot を `ui_damage_key` で外接の 1 つに足す | Music・Calendar・Photos・Phone も「軌跡」を描く |
| ペインの部品 | libkeiland に `kl_sidebar_section`・`kl_sidebar_item`・`kl_sidebar_place`（kl_ui の hot で lit になる）と `kl_list_begin`・`kl_list_row`・`kl_list_end`・`kl_list_item` がある（keiland.h:4534-4598）。Calendar・Mailer が `kl_sidebar_item` を使う | Settings の左ペインは自前の行（`ui_draw_sidebar`、settings/ui.c:735-812、SE_HIT_PAGE_ROW、自前の hover_kind）。Files は `kl_sidebar_place` で描くが、lit は自前の FM_HIT_PLACE でも持つ。Music は自前の行を `kl_ui_hit` で（music/view.c:568-594） |
| frame callback | Vulkan の window は libvulkan の WSI が内部で使う。libkeiland は見ない。chooser と picker（SHM）だけが自分で `wl_surface_frame` を使う（chooser.c:346、picker.c:442） | app は「compositor が次の frame を受け取れるか」を知らずに描き始める |

## 設計

### F1. lit の変化の damage は「描いた時の lit」と「今の lit」だけ（Q1 (1)・(4)、既定で常に効く）

- kl_ui は `drawn_hot`（最後に描いた frame の `kl_ui_begin` の時の hot）を持つ。
- `kl_ui_pointer_motion`・`kl_ui_pointer_leave` は hot を更新するだけにする。motion ごとに damage を積まない。戻り値は「hot が drawn_hot と違う、または前から描き直しが要る」。
- damage は frame を描く時（下の F3 の `kl_ui_take_frame`）に作る。`drawn_hot` の矩形と `hot` の矩形の 2 つだけで、どちらも前の frame の記録（shown）から引く。`kl_ui_hit_still` の region は入れない。
  - 間で通った行は描かない。次の frame を描き始める前に lit が何度変わっても、まだ描いていない予約は取り消され、最新の状態だけが描かれる（ユーザーの (4)）。描いている途中の frame は止めない。その frame が終わった後の次の frame で、最新の状態を描く。
  - hot が drawn_hot に戻った（行き来した）時は、何も描かない。
- 入力の配達は変えない。全部の motion が今と同じに app に届く。描き込み・drag の app にも安全。
- 押した widget（active）・click・`kl_ui_pointer_cancel` も、同じく「描いた時」と「今」の比べで damage にする。

### F2. window ごとの申請: motion の合流と frame の速さ（第 3 版、review-1 と [review-2](review-2.md) を反映）

既定は今のまま（全部の motion を届け、frame の速さに上限なし）。app が window ごとに申請した時だけ効く（ユーザーの補足）。描き込み・drag の精度が要る app（Notes の pen、Paint、Image Viewer の drag、Text Editor の選択の drag）は申請しない。

**着手の条件（review-2 M5、Q1 2026-10-12）**: F2.2〜F2.6 は、Pi の hover 中の `kl_window_present_times` の acquire_ms と wait_ms（Settings の SLOW-FRAME の行、settings/main.c:582-583）を見てから着手を決める。描画が compositor より遅ければ pacing は何もしないので、どちらも 0 に近ければ着手しない。F2.1 だけを先に入れた（既定にはしない）。

**口**:
- `kl_window_set_pacing(window, flags, max_fps)`（KL_VERSION 91、F2.1 で入れた）。今は `KL_PACING_MOTION` だけで、max_fps は 0。
- F2.2 以降で `KL_PACING_FRAME`、max_fps、`kl_window_frame_wait` を足す時は次の版（92）にする。WS210 の GPU の canvas はその後の版（Q1 が master に記録）。
- `KL_PRESENT_NONE` の window（app が自分で present する）に `KL_PACING_FRAME` を申請すると ENOTSUP（L5）。

#### F2.1 `KL_PACING_MOTION`（実装済み 37e00bcd5、既定ではない）

- `window_pointer_motion`（ui/window.c）で、申請した window の新しい motion が、queue の一番新しい event が同じ window の MOTION なら、その slot の x・y・modifiers・serial・arrival_us（今）・time_us を置き換える。新しい slot は取らない。
  - app の queue（`kl_app_event` の window と input.kind で判定）と window の queue の両方。
  - 置き換えた後も `keiui_window_stamp`（compositor の時刻）と `keiui_window_wake` を呼ぶ（L1）。
- 判定は `keiui_motion_merges`（ui/internal.h の static inline、純関数）。host の試験 6 項目（plan/ws210/tests/run-host-kl-ui-lit.sh の merge-*）。
- 合流しない: 間に BUTTON・LEAVE・KEY などがある MOTION、enter の motion（push の経路が別）、touch・tablet。
- 効果は F1 の後では event の処理の CPU だけの見込み（L4、推測）。app に申請させるかは Pi で測ってから決める。

#### F2.2 frame の要否は「描画の関数の最初」で決め、消費は due の時だけ（H1）

- 申請すると (a) の効果が消える穴（H1）: settle（settings/main.c:549、files/main.c:764）が周ごとに damage を足す一方で、drawn_hover は描いた時だけ更新される（settings/ui.c:426-429）。due でない周が続くと、settle のたびに「描いた時の hover と、その周の hover」が積まれて軌跡が戻る。
  - 同じ型: Calendar の `animating = 0`（calendar/main.c:587-588）、Music の `lit_changed = 0`（music/main.c:1252・1258）、`kl_ui_take_frame` の消費。
- 規則: 描画の関数の最初で due を問い、due でなければ何も消費せずに戻る。settle・`kl_ui_take_frame`・animating・lit_changed の消費は due の時だけ行う。
  - Settings・Files の settle は、main loop の「frame の判断の前」から描画の関数（main_frame）の最初に移す。
- host の試験: due でない周を 2 回挟んで sidebar を通った後の frame の部分が 2 つだけ（最初の行と今の行）であること。

#### F2.3 urgent（待たせない入力）は「待たせてよい物」の列挙で決める（M1）

- 待たせてよい物だけを列挙し、他の kind は全部 urgent にする。
  - 待たせてよい物: `KL_WINDOW_MOTION`・`LEAVE`・`AXIS`・`TOUCH_MOTION`・`DROP_MOTION`・`TABLET_MOTION`・`TABLET_HOVER`・`DROP_ACTION`。
  - urgent になる物の例（review-2 M1 の漏れ）: POST・CONTROL_TEXT・CONTROL_DONE・TAB・POPUP_DONE・DRAG_DONE・DROP_ENTER/LEAVE・CONTROL_DROP・TABLET_DOWN/UP・AXIS_STOP・RESIZE（立て所 window.c の configure と resize）。
- kl_ui は click を 1 つの slot に持ち、key を frame ごとに 1 つずつ渡し、scroll・gesture・drag を `kl_ui_begin` で進める。urgent は「入力が溜まり、kl_ui の動作は次の frame」を短くする。

#### F2.4 問い合わせは副作用なし、解除は present の時（M2）

- `int kl_window_frame_wait(const struct kl_window *window, uint64_t now_us)`: 副作用の無い問い合わせ。0 は「今描いてよい」、正の値は「その ms 後に描いてよくなり得る」、-1 は「callback を待つ（poll が起こす）」。値は ms に切り上げる。
- 1 周に何度呼んでもよい（待ちの関数と描画の関数の両方、Music の key（music/main.c:1296-1304）が遅れない）。
- urgent の解除・watchdog の時の callback の destroy と要求し直しは、`kl_window_present_parts` の中（present の時）だけで行う。
- host の試験: 同じ状態で 2 回呼んで同じ答え。present の後に urgent が解ける。

#### F2.5 callback の要求と失敗（H3・M3・M4・M7）

- **要求の場所（M7）**: commit を要求する内部の hook を 1 つにする（`keiui_present_commit_hook`）。今の `keiui_present_frame`（present.c）・p013 の E1・E2 の copy の経路・p005 の `kl_window_frame_end` が同じ hook を呼ぶ。p013 と p014 の順は ws.md に書く（p013 を先に入れ、F2.5 はその hook に載せる）。
- **OS ごとの期待（M3）**:
  - zedBSD の libvulkan: `vkQueuePresentKHR` は積んで戻り（wsi-swapchain.c:576-591）、worker の `wayland_commit` が 1 つ前の callback を待ってから commit する（wsi-wayland.c:774-783・797-845）。main で present の前に要求した callback は、普通は 1 つ前の commit に乗る（推測）。定常で 1 frame 先行する。
  - Linux・FreeBSD の compat: 同期で commit する（libvulkan-compat/wsi-swapchain.c:1502-1544）。100ms で打ち切るので、watchdog と重なると約 5 fps（L3）。
  - 受け入れ（F2.7）に「worker に並ぶ frame の数が 1 以下」と、acquire_ms・wait_ms を入れる。足りなければ WSI の側（libvulkan、別 Phase、Q1 の判断）。
- **失敗の経路（M4）**:
  - (a) SHM の EAGAIN（present-shm.c:58-61、compositor が 2 枚とも読んでいる）: due に戻すと忙しい待ちになる。`shm_release`（present-shm.c:213-226）が来るまで due にしない。
  - (b) worker の非同期の失敗: `vkQueuePresentKHR` には返らず、次の acquire で返る（wsi-swapchain.c:567-570）。救いは watchdog。SUBOPTIMAL は commit 済みとして扱う（present.c:337-338）。
  - (c) compositor が callback を保留するのは compose が起きない時だけ（sleep_hold display.c:162、出力の喪失、image の無い window）。重なって隠れた window と別の head の window は compose されて解放される（compose.c:1825-1868）。
    - 保留中に present すると、WSI の worker が前の callback を最大 10 秒待つ（WAYLAND_WAIT_NS、wsi-wayland.c:25・776）。3 枚が尽きると `vkAcquireNextImageKHR` が最大 10 秒待つ（PRESENT_TIMEOUT present.c:31、wsi-swapchain.c:3054-3110）。超えると SURFACE_LOST が残り、Settings は EIO→DEVICE_LOST で終わる（settings/present.c:101-104）。
    - F2 では、watchdog の present は callback が戻るまで 1 回だけにする。zedBSD の watchdog は 1 秒程度にする。urgent も保留中は 1 回にする。
    - 「保留が 10 秒続くと surface を失う」は F2 より前からの問題で、Q1 が bug として記録する（Q1 2026-10-12）。
- callback は高々 1 つ。最初の frame・resize・configure では無条件に due。申請を外した時と window の破棄で destroy。max_fps は前の present の時刻から測る（L5）。

#### F2.6 待ちの関数（H4・M6）

- app ごとに、描画の理由と描画以外の理由を表にする。描画以外の理由は due に関わらず待ちを 0 にする: Music・Phone・Mailer の `request_count`（music/main.c:1320、phone/main.c:896、mailer/main.c:777）、worker の結果、時計。
- 描画の理由（dirty・lit_changed・hover_pending・moving・animating）だけがある時は、`kl_window_frame_wait` の値と他の待ちの短い方にする。Calendar の animating は main loop（calendar/main.c:389-391）で見る。
- callback の done は default queue に届くので、取りこぼさない（app.c:200-262、wsi-wayland.c:940-1000）。
- host の試験: 待ちの関数の表（描画の理由だけ・描画以外の理由あり・due）の答え。spin が無いこと。

#### F2.7 申請と受け入れ

- 申請（F2.2〜F2.6 の後）: Settings・Files・Music・Calendar・Photos・Phone・Mailer の UI の window に `KL_PACING_FRAME`（max_fps 0）。
- 受け入れの数値は先に決める（review-2 F2.6）: Pi の hover の連続で、(1) app の描く frame の数が申請の前より少ない、(2) click・key の入力から commit までの時間が前より長くない、(3) worker に並ぶ frame の数が 1 以下、(4) app の CPU の時間が前より短い。P3 の perf の計測で取る。
- L2 の訂正: display.c:435-454 は unmap の時の解放で、保留の根拠ではない（保留は display.c:162 と compose.c:1825-1868）。

### F3. 共通の口: frame で何を描くか（Q1 (3)）

- 新規の口:
  - `void kl_ui_damage(struct kl_ui *ui, const struct kl_rect *rect)`: app の自前の変化（時計・進み具合・list の中身）を足す。
  - `void kl_ui_damage_whole(struct kl_ui *ui)`: 全体を描くことを足す。
  - `int kl_ui_take_frame(struct kl_ui *ui, struct kl_rect *parts, unsigned capacity, unsigned *count)`: 次の frame で描く物を返し、溜めた damage を空にする。
    - 戻り値: `KL_FRAME_NONE`（描く物が無い）、`KL_FRAME_PARTS`（parts に count 個）、`KL_FRAME_WHOLE`。
    - parts は F1 の lit の 2 つの矩形と `kl_ui_damage` の和。矩形の list への足し方は p013 E2 の module（`present-damage.c`、重なり・1.5 倍・最大 4 つ・面積のしきい値で全体）を使い回す。
- app の 1 frame の形（6 つの app で同じになる）:

```c
/* After the input: is a frame due, and what does it draw? */
due = kl_window_frame_due(window, now, &wait);
if (due) {
	need = kl_ui_take_frame(ui, parts, 4U, &count);
	/* NONE: nothing; PARTS: draw each part clipped, present the parts; WHOLE: draw and present all. */
}
```

- `kl_window_present_parts(window, pixels, stride, parts, count)`（新規）: p013 の矩形の list を present に渡す。今の `kl_window_present_part` は 1 つの矩形の版として残す。
- 今の `kl_ui_take_damage` は互換のために残す。中身は `kl_ui_take_frame` の外接の 1 つにする（NONE も WHOLE も 0、今の意味のまま）。
- app が自前の lit（Files の FM_HIT_*、Settings の SE_HIT_*）を持つ間は、app が F1 と同じ「描いた時と今」の比べを自分で行い、`kl_ui_damage` に渡す。F4 で部品に移れば要らなくなる。

### F4. ペインは libkeiland の部品で（ユーザーの (5)）

- 既存の部品（`kl_sidebar_item`・`kl_sidebar_place`・`kl_list_row`）は kl_ui の hot で lit になる。F1 により、hover の描き直しと取り消しを自動で持つ。app は自前の hover の状態を持たない。
- 載せ替え:
  - **Settings の左ペイン**: 自前の行（SE_HIT_PAGE_ROW、ui.c:735-812）を `kl_sidebar_item`（と `kl_sidebar_section`）に替える。見た目（icon・accent の選ばれた行・focus の無い時の灰色、settings/ui.c の `ui_focus`）が合わない所は、`kl_sidebar_item` に flag を足すか `kl_sidebar_place` の flag を使う。どちらを使うかは実装の時に、画素の比べで決める。
  - **Files の sidebar**: `kl_sidebar_place` の lit だけを使い、自前の FM_HIT_PLACE の hover による damage をやめる。FM_HIT_PLACE は click の場所として残す。
  - **Music の左ペイン**: 自前の行の `kl_ui_hit` を `kl_list_row`（scroll 付きの list）か `kl_sidebar_item` に。
  - Settings の右の page の項目（tile・control）は、この Phase では F3 の app の自前の比べで扱う。後で部品に移す。
- 部品に足す物があれば、公開の struct を変えず、flag か新しい関数で足す。

### F5. 版

- KL_VERSION 90: `kl_ui_take_frame`・`kl_ui_damage`・`kl_ui_damage_whole`・`kl_window_present_parts`（F3、main 6d9df4d23）。
- KL_VERSION 91: `kl_window_set_pacing`・`KL_PACING_MOTION`（F2.1）。
- KL_VERSION 92: F2.2 以降の `KL_PACING_FRAME`・`kl_window_frame_wait`（着手した時）。
- WS210 の GPU の canvas の版は、その後に送る。Q1 が master の「次の KL_VERSION」を直す。

### F6. 試験

- host（新規、`plan/ws210/tests/`）:
  - kl_ui の F1: motion の列（A→B→C→A、A→B→C）の後の `kl_ui_take_frame` が描く矩形。still の region。leave。press と cancel。
  - F2 の合流: 合成した event の列（MOTION の連続、BUTTON を挟む、2 つの window が交互）の queue の結果。
  - F3: 合流の list の算法（p013 の module と同じ試験を共有）。
  - 各 app の既存の host の試験。Files は files-render の peek と draw の画素の比べ、Phone は hover-part、Calendar・Music は既存の試験。部分の frame の画素が全体の frame と一致すること。
- T1（QEMU）: Settings の左ペインを速く上下に動かした後の PNG と、全体で描いた PNG の比べ。
- Pi（ユーザー）: Settings の左ペインを速く動かした時の遅れが「単項目のポイント」の遅れを超えて積み重ならないこと。P3 の perf の行で、hover 中の frame の数と面積を比べる。

### F7. 完了の条件（review-1 の後、H5・M7）

1. host の試験が PASS。対象の app の rpi4・amd64 の build が warning 0。`exports.py --check` と OS 境界の check が通る。
2. (a) と (b): Settings・Files の hover は「描いた時と今」の 2 つだけを描く。kl_ui の F1・F3 が入り、frame ごとに take する app が NONE で描かない。
3. 受け入れは app の描く面積と frame の数で測る（H5）。compositor の側の効果は p013 と別 WS。
4. Pi でユーザーが、遅れの積み重なりが無いことを確かめる。
5. (c) F2 と (d) F4 は、この条件に含めない（別に Q1 が決める）。

## 実施の記録

- 2026-10-12 F1 を実装した（Q1「review と並行して実装してよい、merge は review の結果を見てから」）。
  - 中身: libkeiland/ui/ui.c の `drawn_hot`・`lit_pending`・`ui_key_lights`。`kl_ui_take_damage` は「描いた時の lit」と「今の lit」の 2 つの外接の矩形を返す。lit が行き来して元に戻った時は 1x1 を返す（0 は全体を意味するので）。公開の API と KL_VERSION は変えない。
  - host の試験: `plan/ws210/tests/run-host-kl-ui-lit.sh` が 13 PASS。軌跡の端・行き来・still を越える・回り道・leave・still だけ。各部分の描画の後の画素は全体の描画と一致した。
    - 前の ui.c（main）で同じ試験を流すと back-to-drawn と detour が FAIL になる（陰性対照）。
  - 既存の host の試験: Phone 19 PASS・Calendar 13 PASS・Music 16 PASS。Files の files-render の部分と全体の画素は 3 場面で一致した。
  - build: rpi4・amd64 の libkeiland と 6 つの app が warning 0。
  - 限り: 公開の口が 1 つの矩形なので、縦の list を上から下へ通った時は、上下の端の外接の箱が通った行を全部含む（軌跡と同じ大きさ）。効くのは、行き来・回り道・still を越える場合。端の 2 つを別々に描くには F3 の `kl_ui_take_frame`（矩形の list）が要る。

- 2026-10-12 F3 の口を agent/p1 に実装した（Q1「review の指摘で変わり得る前提で、merge はしない」）。
  - libkeiland: `kl_ui_take_frame`・`kl_ui_damage`・`kl_ui_damage_whole`・`kl_window_present_parts`・`KL_FRAME_*`、KL_VERSION 90、exports.map。docs/architecture/keiland.md の Widgets の行。
  - kl_ui の damage は矩形の list（最大 4）。重なるか外接の箱が和の 1.5 倍以下ならまとめる。5 つ目はいちばん広がりの少ない所にまとめる。
  - `kl_ui_take_damage` は同じ list の外接の箱（互換）。present.c は部分の list を写し、incremental present に渡す。
  - host の試験 `run-host-kl-ui-lit.sh` は 21 PASS。F3 の分: 縦に全部の行を通ると、最初と最後の行が 2 つの部分になる。app の部分も足される。元に戻ると NONE。容量 1 で 1 つにまとまる。WHOLE。各部分を clip して描いた後の画素は、全体の描画と一致した。
  - 既存の host の試験: Phone 19・Calendar 13・Music 16 PASS、Files は host build が通った。
  - build: rpi4・amd64 の libkeiland と 6 つの app が warning 0。keiland-os-boundary PASS。exports.py --check。
  - app はまだ載せ替えていない（F3 の app の段と F4 は review の後）。

- 2026-10-12 [review-1](review-1.md) と Q1 の判断（M7 の分け方）:
  - **(a)** Settings と Files の自前の lit に「描いた時の hover と今の hover」の比べを入れた（H2）。agent/p1-a 05a6b4d8d → main d75a27337。
    - Files: `fm_input_hover_settle`、Settings: `se_ui_hover_settle`。どちらも main loop が frame を決める前に 1 回呼ぶ。damage は 2 部分で、部分ごとに clip して描く。
    - motion ごとの kl_ui の damage は取らない（app の hit が同じ矩形）。Settings の左ペインは自前の描画のまま（M4）。
  - **(b)** F1・F3 の直し（agent/p1、merge は Q1）:
    - H1: 互換の `kl_ui_take_damage` は、lit が変わって元に戻った時に 1 と 1x1 を返す（全体にしない）。試験 back-to-drawn。
    - M1: drawn_hot は kl_ui_end で記録する（`ui_settle_lighting`）。その frame が今の hot の widget を記録した時、hot が無ければ前の drawn_hot の widget を記録した時だけ。描かない frame（1x1 の key の frame）は何も変えない。試験 frame-after-key-frame は NONE にならない。
      - ただし、その frame の後は kl_ui_end の pointer の再評価で、記録の無い widget が WHOLE になる（前からの shown の入れ替えの作り）。前の ui.c でも同じ WHOLE になるので、この試験は前後を見分けない。
    - M5: canvas に複数矩形の clip は足さない（公開の `struct kl_canvas` を変えない、p001 の A2）。部分は近ければまとめる（外接の箱が和の 1.5 倍以下、最大 4）。
      - 根拠（host、Files）: 1 回の部分の走査の固定の費用は約 0.2ms（sidebar の 1 行の部分、224x58 で 0.24ms）。行あたりの画素の費用は 1 行（216x30）で約 0.1〜0.2ms。
      - 離れた 2 行は 2 回の走査の方が、間の行を全部描くより安い。Pi での比は p013 の baseline と合わせて測る。
    - M6: Mailer の host の試験（plan/ws169/tests/run-host-mailer.sh）18 PASS（hover-part を含む）。
    - L2: keiland.h の F3 の注記に「KL_VERSION 90 から」の意味の変化を書いた。新しい source の file は無い（Makefile 3 つは不変）。exports は 4 名。
    - L3: drawn_hot が無い（invalid）時は何も足さない（ui_damage_key は invalid の key で 0）。
    - 試験: run-host-kl-ui-lit.sh 22 PASS。Phone 19・Calendar 13・Music 16・Mailer 18 PASS。Files の保留の hover の画素は 3 場面で一致。
    - build: rpi4・amd64 の libkeiland と 7 app（files・settings・music・calendar・photos・phone・mailer）が warning 0。keiland-os-boundary PASS、exports.py --check。
  - **(c)** F2（KL_PACING_FRAME）は H3・H4・M2・M3・L7 を直した設計を書き直してから review（後回し）。MOTION の合流は L4 のとおり window_push で、効果は小さい見込み。→ 2026-10-12 F2 の第 2 版を書いた。[review-2](review-2.md) の後、第 3 版に直し、F2.1 を実装した（37e00bcd5、KL_VERSION 91、既定ではない）。F2.2 以降は Pi の計測の後。
  - 2026-10-12 7 つの app を `kl_ui_take_frame`・`kl_window_present_parts` に移した（music 3e2726cc2〜files 597dd899a → main c6348fba6）。
  - **(d)** F4 は保留。Music は不要（L1）。
  - **H5（受け入れの範囲）**: p014 の受け入れは「app の描く面積と frame の数」に限る。compositor の側の効果（wl_surface.damage と VkPresentRegionsKHR が外接の 1 つにまとめられる、wsi-swapchain.c:3304-3349・wsi-wayland.c:833-842）は、p013 と WSI の複数矩形の damage（別 WS、Q1 が判断）に回す。

- 2026-10-12 Pi 4（place15）の log と F2.2〜F2.6 の着手（Q1、ユーザー「Settingsのサイドバー、改善なし」「右ペインのボタンも遅れます」「スクロールはうまく動いているので、マウスイベントと描画の蓄積の問題」）。
  - log（Pi の `/tmp/kwl.log` と `/run/user/1000/session.log` を SSH で読んだだけ）:
    - Settings の SLOW-FRAME（250ms 超だけが出る、14 行）: draw 112〜293ms、present 21〜184ms。present の内訳 acquire 0〜61ms（中央 約 6）、wait（GPU の copy の fence）0〜141ms（中央 約 22）、copy 0〜51ms。
    - compositor（P3 の kwl-latency.py、387 frame）: fence 中央 65ms・p90 125ms、draw→shown 中央 85ms・p90 149ms、input→shown 中央 101ms・p90 268ms。compositor の 1 frame は 85〜150ms。
    - 250ms 以下の frame（hover の部分の frame の多く）は log に出ないので、hover の 1 コマの時間・面積は直接は見えない（限り）。
  - 判断（M5）: acquire・wait は 0 に近くない（acquire 最大 61ms は swapchain の image が全部 compositor 側にある印）。zedBSD の WSI は FIFO で、worker が前の commit の callback を待ってから commit する（wsi-wayland.c:774-845）。app は compositor（85〜150ms/frame）より速く hover の frame を出せるので、2〜3 枚が WSI に並び、並んだ frame の 1 枚ずつが古い lit を見せる。これが「描画の蓄積」と「軌跡が後から順に描かれる」。scroll は連続の変化なので古い frame が見えても目立たない。→ F2.2〜F2.6 に着手した（Settings だけ）。
  - 実装（KL_VERSION 92）:
    - `KL_PACING_FRAME`・`kl_window_frame_wait(window, now_us)`。`kl_window_present_parts`（SHM と Vulkan）が、present の前に `wl_surface_frame` を要求する（callback が無い時だけ）。状態の変化は ui/internal.h の純関数（`keiui_pacing_waits`・`keiui_pace_wait`・`keiui_pace_input`・`keiui_pace_ask`・`keiui_pace_shown`・`keiui_pace_clear`）、window.c はそれを呼ぶだけ。
    - urgent（F2.3）: window_push で、待たせてよい物（MOTION・LEAVE・AXIS・TOUCH_MOTION・DROP_MOTION・DROP_ACTION・TABLET_MOTION・TABLET_HOVER）以外の kind は urgent。urgent は次の present で解ける。
    - 待ち（F2.4）: callback が無い→0。callback を待つ間に 1 枚出した（overdue）→ -1（callback だけを待つ、何が来ても）。urgent → 0（1 回）。watchdog 1 秒 → 0（1 回）。他は watchdog までの ms（切り上げ）。副作用なし。
    - 失敗: present が失敗（EAGAIN・EIO）したら、その present で要求した callback を destroy する（urgent は残す）。
    - Settings（F2.2・F2.6）: `KL_PACING_MOTION | KL_PACING_FRAME` を申請（log `PACING motion=1 frame=1 errno=`）。main loop は「何か変わった」→ `kl_window_frame_wait` が 0 の時だけ settle と frame。0 でなければ何も消費しない（settle も dirty も残す）。main_timeout は frame の理由がある時、wait が 0 なら 0、正なら他の待ちと短い方、-1 なら他の待ちだけ（callback の done が poll を起こす）。
    - 右の page の button・tile・control の hover も同じ settle と部分の frame に乗っているので、同じ pacing が効く（ユーザーの追加の報告）。
    - SLOW-FRAME の閾値を 100ms に下げ、`parts=`（0 は全体）と `area=`（描いた画素数）を足した。次の Pi で hover の 1 コマを見るため。
  - 設計からの違い（自分で review）:
    - 第 3 版は「-1 は callback を待つ」だったが、overdue でない時は watchdog までの ms を返す（callback が来ない compositor でも 1 秒で 1 枚描ける。poll は done で起きる）。-1 は overdue の時だけ。
    - callback の要求は `kl_window_present_parts` の中（p013 の commit の hook は未 merge）。p013 が入ったら hook に移す（M7）。
    - SHM の EAGAIN（M4(a)）は callback を destroy するだけ。FRAME を申請する SHM の window は今は無い。
    - max_fps は 0 のまま（F5 の後の版）。
    - 保留が 10 秒続くと WSI が surface を失う問題（F2.5 (c)）は前からのまま。pacing は保留中の present を 1 枚に減らすだけ。
    - Files・Music など他の app の申請（F2.7）はまだ。
  - 試験: `run-host-kl-ui-lit.sh` 46 PASS（新規 18: pace-waits-*・pace-urgent-*・pace-idle・pace-moves-wait・pace-wait-pure・pace-watchdog・pace-overdue・pace-answered・pace-urgent-once・pace-not-shown）。build: rpi4・amd64 の libkeiland と settings が warning 0（-Werror）。`exports.py --check`、keiland-os-boundary PASS。Linux・FreeBSD の libkeiland の build は未実施。QEMU・Pi は未実施（Q1 が P3 の place16 に入れる）。
  - Pi で見る所: Settings の左ペインと右の page の button を速く動かした時、止まった後に「通った行が順に光る」が無くなること。log の `ZSETTINGS PACING motion=1 frame=1 errno=0`、SLOW-FRAME の `parts`・`area`・acquire・wait。

- 2026-10-12 F2.7 の申請（Q1、ユーザー place19「Calenderの最適化が甘いです。左ペインも遅い、右ペインは重くならないけどポイントの描画が300ms遅れて、すでにカーソルがないのに光ってる。」）。
  - Pi の log（SSH で読んだだけ）: Calendar の行は READY・GLASS・CELL・DONE だけで、frame の時間の行が無い（Calendar に SLOW-FRAME の log が無かった）。compositor の PERF は compose frame_ms 107〜153ms、Calendar（client=6、1904x1020）は約 17 秒に 59 commit。数で直接の確かめはできない（限り）。
  - host の計測（scratch の bench、1904x1020、glass）: 全体の frame 16.4ms に対し、左ペインの行の hover の部分の frame は 0.23〜0.31ms（10600〜18232 画素）、months の cell 0.5〜0.8ms、panel 0.5〜0.9ms。部分の描画には乗っている。よって遅れの主因は Settings と同じ WSI の FIFO に並ぶ frame（compositor 1 コマ 100〜150ms で 2 枚並ぶと約 300ms、並んだ frame が古い lit を見せる）と判断（推測、Pi の数は次の log で）。
  - 見つけた Calendar の欠陥（直した）:
    1. notice が出ている間、`cal_view_wait` が 0 以上を返すので `animating` が立ち、入力で起きるたびに 3D の desk を描き直して全体を present していた。`cal_view_animating`（page の turn・cell の sink・drag だけ）に替えた。
    2. desk だけの frame が `kl_window_present`（全体）で出していた。desk の area だけの `kl_window_present_part` に。
    3. hover の部分の frame が desk に触れなくても `desk_valid` を 0 にしていたので、hover の後の page の turn の最初の frame が全体の描画になった。desk に触れない部分の frame では保つ（`desk_seen`）。desk を一部だけ切る部分の frame では under を取らず無効にする（前は古い desk の画素を under に取り得た）。
    4. page の turn の間の hover は全体の描画だった。desk の area を `kl_ui_damage` で部分に足して、lit の部分と desk の部分だけを描く。
    5. 日付の click で選んだ日・editor・表示が変わると、それより前に描いた panel（と前の cell の選択）が古いまま、その後の desk だけの frame が続いて直らなかった（host の bench で click の frame の後の全体の描画と画素が違うことを確かめた）。`view->again` で次の frame を全体に。
  - 申請（KL_PACING_MOTION | KL_PACING_FRAME、log `PACING motion=1 frame=1 errno=`）: Calendar・Files（desktop の mode は申請しない）・Music・Photos・Phone・Mailer。形は Settings と同じ: 描画の関数の最初（Files は main loop の settle の前）で `kl_window_frame_wait` が 0 の時だけ描き、0 でなければ何も消費しない。待ちの関数は、frame の理由がある時に pace が 0 なら今まで通り、正なら他の待ちと短い方、-1 なら他の待ちだけ（callback の done が poll を起こす）。request_count（Music・Phone・Mailer）は pace に関わらず 0。Calendar は page の turn の 16ms の待ちを、描いてよい時だけにした。
  - Calendar に SLOW-FRAME（100ms 超、draw・present・copy・acquire・queue・wait・parts・area）を足した。
  - 試験: Calendar の host 試験 13 PASS、`run-host-kl-ui-lit.sh` 46 PASS。scratch の bench（commit しない、開発用）: hover の部分の frame の後も desk を保つ、部分の画素が全体と一致、page の turn の間の desk だけの frame と「lit の行＋desk」の部分の frame が全体と一致、click の後の again。Music・Photos・Phone・Mailer の host 試験は view.c だけで main.c を含まないので、main loop の変更は build とレビューだけ。
  - build: rpi4・amd64 の calendar・files・music・photos・phone・mailer が warning 0（-Werror）。libkeiland は不変。QEMU・Pi は未実施。
  - Pi で見る所: Calendar の左ペイン・months の cell・panel を速く動かした後に古い lit が残らない・遅れが積み重ならないこと、日付の click の後の page の turn。他の 5 app の hover。log の `PACING ... errno=0` と Calendar の `SLOW-FRAME`。

- 2026-10-12 Calendar の日付の click の遅れ（Q1、ユーザー place22「Calendarですが、150msくらい遅れてポイントされますね。日付クリックはパネルが更新されるものの、800ms遅れます。」。hover の 150ms は compositor の 1 コマで P3 の担当、P1 は click）。
  - Pi の log（Q1 が写した kwl.log、SSH は読むだけ）: 日付の click の前後に `SLOW-FRAME draw=181〜258 present=15〜25 parts=0 area=1024000`（窓全体）が 2〜3 コマ続く。1 回の click は press のコマ（BUTTON の入力は全部 dirty で全体）、click のコマ（全体、SELECT と FLIP）、`view->again` の次のコマ（全体）の 3 コマ ≒ 660ms と present。同じ日の SELECT が続くのは利用者の重ねての click（kl_ui の click は 1 コマで消える）。
  - host の計測（scratch の bench、1280x800、glass、Xeon。Pi は約 18 倍）: 全体のコマ 11.0ms の内訳は、背景と 3 枚の card の veil 4.5ms（1,024,000 画素の blend）、卓上カレンダーの 3D 2.8ms（720 三角形、2 倍の supersample）、残りが文字・cell・panel。
  - 直し（userland/desktop/calendar）:
    1. 日付の cell の上の左 button は、press では何も描かず、release（click）ではその cell だけを部分のコマで描く（`cal_view_cell_at`、`kl_ui_damage`）。focus を持つ field・key で光った widget・drag・他で押した press の release は今まで通り全体。
    2. click で選んだ日が変わったら、`again`（次のコマで全体）の代わりに、前に選んだ日の cell と「選んだ日」の card（日付・予定・卓上カレンダー）を同じコマの中で部分として描き直す（`view->redraw`、main.c がそのコマの parts に足して present）。editor・表示の変化は今まで通り `again`。
    3. 背景と card の veil を窓の大きさ・glass・card・色ごとに一度だけ絵にし、各コマは clip の中を写す（`view_ground`）。
    4. 卓上カレンダーの 3D は、止まっている間と、めくり始めのコマ（まだ動かず、下の page は隠れている。host で画素一致を確かめた）は前の絵を使う（`picture_still`）。
    5. SLOW-FRAME に `why=`（parts・desk・day・input・button・key・again・moving・animating・lit…）、日付を選んだコマに `DAY-FRAME draw= present= parts=` を足した。
  - host の結果: click のコマ 1.7ms（前は press・click・again の全体 3 コマで約 33ms）。全体のコマ 11.0→3.9ms（glass）、3.65ms（不透明）。部分で描いた click の後の画素は、同じ時刻の全体の描画と glass・不透明とも一致（0 画素の差、reduce motion と motion の両方）。直しの前と後の全体のコマも画素一致。
  - 試験: `run-host-calendar.sh` 13 PASS、`run-host-kl-ui-lit.sh` 46 PASS。build: rpi4・amd64 の calendar が warning 0。QEMU・Pi は未実施。commit b9cec0fe9。
  - Pi で見る所: 日付の click からパネルの更新まで（log の `DAY-FRAME`、`SLOW-FRAME ... why=`）、ページめくり、months の scroll。

- 2026-10-13 サイドパネルの hover の遅れの区間ごとの計測と、clip の外の描画（Q1、ユーザー「サイドパネルのポイントの遅れは気になりますね。なんでだろう。」「サイドパネルはlibkeilandなんじゃないですか？ウィジェットの実装が問題？」。Pi 4、main 1392d52、1024x680、compositor の 1 コマは 15〜36ms）。
  - trace（1f98f8151）: `KL_TRACE_FRAMES=1` で、libkeiland の window ごとに 1 フレーム 1 行 `KL TRACE ...` を stderr に出す（ui/trace.c・trace.h、window.c の hook、present.c の段の時刻）。公開の API と KL_VERSION は変えない。
    - 時刻 `_at` は CLOCK_MONOTONIC の µs で、compositor の `KWL LAT ... at_us=` と同じ時計。区間 `_us`。
    - 入力: `in`（queue に積んだ数）・`merged`（KL_PACING_MOTION で合流した数）・`in_at`（そのフレームが映す最初の入力の到着）・`in_last_at`（最後の到着）・`in_ev_at`（最後の motion の compositor の時刻）・`x`・`y`。
    - pacing: `waited_at`（前のフレームの後、kl_window_frame_wait が最初に 0 以外を返した時刻）・`ready_at`（最後に 0 を返した時刻＝描き始め）・`paced_us`。
    - 描画: `draw_us`（ready から kl_window_present_parts まで）・`parts`・`area`。
    - present: `copy_us`・`acquire_us`・`submit_us`（記録と submit）・`queue_us`（vkQueuePresentKHR）・`fence_us`・`present_us`（全体）・`shown_at`。
    - compositor: trace 専用の wl_surface.frame の done（`done_at`＝受けた時刻、`done_ev_at`＝compositor が送った時刻）。`lat_us`＝`in_last_at` から done まで、`ev_lat_us`＝`in_ev_at` から。
    - callback は present の前に要求するので、WSI が前のフレームの commit を後から出すと、その commit の done になる（早く見える）。compositor の `KWL LAT adopt surface=` と時刻で突き合わせる。8 フレームより多く done を待つと、古い物から done なしで出す。
    - host 試験 host-trace（run-host-present-damage.sh の中）25 checks・0 failures。
  - host の計測（scratch の bench、commit しない。1024x680、glass、Xeon、他の担当の負荷あり。Pi は約 18 倍の見込み）:

    | 場面 | 前 | 後（c12ae9572） |
    | --- | --- | --- |
    | Calendar 左ペインの隣の 2 行の hover（1 部分 18232 px） | 0.35 ms | 0.12 ms |
    | Calendar 左ペインの離れた 2 行（2 部分 20776 px） | 0.62 ms | 0.13 ms |
    | Calendar の 1x1 の clip（clip の外の走査だけ） | 0.27 ms | 0.035 ms |
    | Calendar の全体のフレーム | 3.5 ms | 3.4 ms |
    | Settings 左ペインの hover（Appearance・Wi-Fi・About・Display、1〜2 部分） | 0.15〜0.37 ms | 0.13〜0.35 ms |
    | Settings の 1x1 の clip | 0.045〜0.07 ms | 0.04〜0.05 ms |

    - Calendar の前の内訳（perf）: 文字の glyph の cache の探索（text_glyph の self）33%、months の cell ごとの予定の探し（view_day・cal_same_day、cell × 予定の数）15%。どちらも clip の外。
    - Settings の内訳: clip の中の合成（左ペインの glass の veil と hover の角丸の blend）が約 55%。clip の外の走査は 2 割未満。
  - 直し（c12ae9572、見た目は同じ）:
    1. libkeiland text.c: glyph の cache の hash。乗算だけの hash の下位 12 bit は key の下位 12 bit（glyph の番号）だけで決まり、同じ glyph の大きさ・太さ・face が 1 つの slot に集まり、続き番号の文字が長い連続の slot を作っていた。key の全部の bit を混ぜる。slot は `& (size - 1)`。全部の app の文字の描画に効く。
    2. libkeiland text.c: kl_text_draw は clip の行の外の glyph を描かず、pen だけ進める（戻り値は同じ）。
    3. Calendar view.c: clip の外の cell は、hit・drop の記録・today の log の後で描画をやめる（予定の探しと文字を省く）。
  - 画素の一致（host、前の text.c・view.c の build と比べた）: Calendar の左ペインの 4 組・months の cell の 2 組・Settings の 4 page × 2 組。hover の後の絵と全体の絵がどれも 0 画素の差。hover の後の絵は、同じ build の全体の描画とも一致した。
  - 判断（数からの推測）: Pi の hover の 1 フレームの CPU の描画は、Calendar で約 2〜11ms→約 2ms、Settings で約 3〜6ms（×18 の見込み）。compositor の 1 コマ 15〜36ms より小さい。よって遅れの主因は描画ではなく、pacing（前のフレームの callback を待つ）・present（acquire・fence）・compositor の次のコマの段の和と見込む。Pi の trace の `paced_us`・`draw_us`・`present_us`・`lat_us` で確かめる。
  - 見つけた事（直していない）: Text Editor の自前の glyph の cache（userland/desktop/textedit/text.c:621）が同じ hash を使う。WS210 p008 か別の依頼で直す。
  - 試験: run-host-kl-ui-lit.sh 46 PASS、run-host-present-damage.sh（host-trace を含む）0 failures、run-host-calendar.sh 13 PASS。build: rpi4・amd64 の libkeiland.so・settings・calendar が warning 0。exports.py --check。keiland-os-boundary は B5 が FAIL（plan/ws141/tests/shader-host-test.c の keiland_inputs、main から merge した物で、この変更とは関係ない）。Linux・FreeBSD の libkeiland の build、QEMU、Pi は未実施。

## Phase の中の順

1. F1（kl_ui の内側だけ、公開の口は変えない）。これだけで Music・Calendar・Photos・Phone の「軌跡」が消える。KL_VERSION は変えない。
2. F3 の口と F5 の版。Settings を載せ替える（F4 の Settings の左ペインを含む。ユーザーの報告の本体）。
3. F2.1（実装済み、既定ではない）。F2.2〜F2.7 は Pi の hover の acquire_ms・wait_ms を見てから（review-2 M5）。p013 を先に入れ、F2.5 はその commit の hook に載せる（M7）。
4. Files・Music の左ペイン（F4）と、残りの app の載せ替え。

## 見積

3 LW（F1 0.5、F3 と Settings 1、F2 0.75、残りの app と部品 0.75）＋ T1・Pi の待ち。
