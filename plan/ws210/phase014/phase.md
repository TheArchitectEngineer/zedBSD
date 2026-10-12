<!-- awesome-plan project=zedbsd record=ws210-p014 -->

# ws210-p014: libkeiland の frame の枠組み（lit の変化の damage、motion の合流と描き直しの取り消し、ペインの部品）

Parent: [WS210](../ws.md)
Status: planning（P1 の設計 第 1 版、2026-10-12。design-reviewer の review 待ち）
Phase disposition: normal
Queue: [P1 の lane](../../agents/P1/queue.md)

## 由来

- 2026-10-12 ユーザー（Pi 4、place9 の前の版）:「Settingsの左ペインは、単項目のポイントは200msくらいの遅延で済んでいますが、マウスの通った軌跡のすべてで描画を予約してしまって、遅延と描画が積み重なっています。マウス移動イベントを受け取ったとしても、一定のFPS以上には処理しない工夫や、イベントをpeekして次もマウスイベントなら、一定のFPS以上ならマージするような統一的な枠組みが必要かも。」
- 同日の補足:
  - 「一律にFPSでマウス移動イベントをマージするのではなく、ウィンドウごとに申請されたらそうする感じがいいですね。」
  - 「書き直しが完了する前の書き直しイベントが来たら前の分をキャンセルするのでもいいです。ペインの実現方法がlibkeilandにあるなら、それがいい。」
- Q1 の依頼:
  - (1) motion の合流（間の lit の変化の damage を積まない）。
  - (2) frame の速さの上限（compositor の frame callback まで描かない）。
  - (3) app ごとの hover_pending・lit_changed・damage の仕組みを libkeiland の共通の口に寄せ、6 つの app を順に載せ替える。
  - (4) 描き直しの途中に来た新しい描き直しは、前の予約を取り消す。
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
  - 間で通った行は描かない。描き直しが終わる前に lit が何度変わっても、前の予約は取り消され、最新の状態だけが描かれる（ユーザーの (4)）。
  - hot が drawn_hot に戻った（行き来した）時は、何も描かない。
- 入力の配達は変えない。全部の motion が今と同じに app に届く。描き込み・drag の app にも安全。
- 押した widget（active）・click・`kl_ui_pointer_cancel` も、同じく「描いた時」と「今」の比べで damage にする。

### F2. window ごとの申請: motion の合流と frame の速さ（ユーザーの補足、Q1 (2)）

既定は今のまま（全部の motion を届け、frame の速さに上限なし）。app が window ごとに申請した時だけ効く。
- 口（新規）: `int kl_window_set_pacing(struct kl_window *window, unsigned flags, unsigned max_fps)`。
- `KL_PACING_MOTION`: pointer の motion を合流する。
  - 新しい MOTION を queue に積む時、その window の一番新しい queue の event が同じ window の MOTION なら、それを新しい位置・時刻で置き換える（window の queue と kl_app の queue の両方、ui/window.c `window_push`・ui/app.c `keiui_app_push`）。
  - BUTTON・LEAVE・KEY などを挟んだ MOTION は合流しない（press と release の位置は保つ）。
  - touch の motion は合流しない（gesture の速さが要る）。
- `KL_PACING_FRAME`: compositor が前の frame を受け取るまで、次の frame を描かない。
  - libkeiland は `kl_window_present*` の中で、`vkQueuePresentKHR` の直前に window の surface に `wl_surface.frame` を要求する（WSI の commit が運ぶ）。SHM の present は自分の commit の前に要求する。
  - `int kl_window_frame_due(struct kl_window *window, uint64_t now_us, int *wait_ms)`: 描いてよい時に 1 を返す。それは「前の frame の callback が戻っている」かつ「max_fps が 0 でなければ前の frame の開始から 1000/max_fps ms たった」時。描いてはいけない時は 0 を返し、`wait_ms` に loop の待ちの上限を入れる（callback 待ちなら -1）。
  - 申請しない window は常に 1（今と同じ）。
  - 入力は frame を待つ間も受けて、状態（hot・scroll・選択）だけ進める。描くのは due の時に最新の状態で 1 回。これで「描き直しの途中に来た描き直しは前の分を取り消す」が frame の単位で成り立つ。
  - `max_fps` の既定（0）は display の refresh（callback の速さ）。System Monitor の 2 fps の graph のような app の都合の上限は、app が渡す。
- 申請する app（この Phase）: Settings・Files・Music・Calendar・Photos・Phone の UI の window。申請しない app: Notes の pen、Paint、Image Viewer の drag など、描き込み・drag の精度が要る物。

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

- 新しい口（`kl_window_set_pacing`・`kl_window_frame_due`・`kl_ui_damage`・`kl_ui_damage_whole`・`kl_ui_take_frame`・`kl_window_present_parts` と定数）で KL_VERSION を 90 にする。
- WS210 の GPU の canvas の版（p001 の A2、今は「90 以降」）は 91 以降に送る。Q1 が master の「次の KL_VERSION」を直す。

### F6. 試験

- host（新規、`plan/ws210/tests/`）:
  - kl_ui の F1: motion の列（A→B→C→A、A→B→C）の後の `kl_ui_take_frame` が描く矩形。still の region。leave。press と cancel。
  - F2 の合流: 合成した event の列（MOTION の連続、BUTTON を挟む、2 つの window が交互）の queue の結果。
  - F3: 合流の list の算法（p013 の module と同じ試験を共有）。
  - 各 app の既存の host の試験。Files は files-render の peek と draw の画素の比べ、Phone は hover-part、Calendar・Music は既存の試験。部分の frame の画素が全体の frame と一致すること。
- T1（QEMU）: Settings の左ペインを速く上下に動かした後の PNG と、全体で描いた PNG の比べ。
- Pi（ユーザー）: Settings の左ペインを速く動かした時の遅れが「単項目のポイント」の遅れを超えて積み重ならないこと。P3 の perf の行で、hover 中の frame の数と面積を比べる。

### F7. 完了の条件

1. host の試験が PASS。6 つの app の rpi4・amd64 の build が warning 0。`exports.py --check` と OS 境界の check が通る。
2. 6 つの app が `kl_ui_take_frame`（と申請した pacing）で動く。Settings・Files・Music の左ペインが部品で描かれる。
3. Pi でユーザーが、遅れの積み重なりが無いことを確かめる。

## Phase の中の順

1. F1（kl_ui の内側だけ、公開の口は変えない）。これだけで Music・Calendar・Photos・Phone の「軌跡」が消える。KL_VERSION は変えない。
2. F3 の口と F5 の版。Settings を載せ替える（F4 の Settings の左ペインを含む。ユーザーの報告の本体）。
3. F2 の申請と frame の callback。Settings で効きを Pi で見る。
4. Files・Music の左ペイン（F4）と、残りの app の載せ替え。

## 見積

3 LW（F1 0.5、F3 と Settings 1、F2 0.75、残りの app と部品 0.75）＋ T1・Pi の待ち。
