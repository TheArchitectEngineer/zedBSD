<!-- awesome-plan project=zedbsd record=ws142-p007 -->
# ws142-p007: 設計 — 最大化を desktop の session の状態にする、touchpad の gesture の体系

Parent: [WS142](../ws.md)
Status: planned（2026-10-06 Q1 の設計の第 1 版。実装に取りかかれる。ユーザーの判断は「未決」の 2 点）
Disposition: normal
Related: [BUG-217](../../bugs/BUG-217.md)・[BUG-215](../../bugs/BUG-215.md)・[BUG-216](../../bugs/BUG-216.md)・[BUG-224](../../bugs/BUG-224.md)・[BUG-228](../../bugs/BUG-228.md)・[BUG-209](../../bugs/BUG-209.md)・[BUG-232](../../bugs/BUG-232.md)

## 由来（ユーザー、2026-10-06 UAT）

- BUG-217「最大化は『タブレットとして最大化で使う』というユーザの意思だと考えて、デスクトップセッションの状態として捉え、アプリ切り替え時には切り替え先のアプリも最大化したいです。逆もしかりで、ウィンドウモードアプリから最大化アプリに切り替えるときは、ウィンドウ化します。」
- BUG-215 3 本指の preview は swipe 1 回で 1 つ移動。BUG-216 WiseView の題の文字は出さない、2 本指の左右で選び 2 本指の下 swipe で確定して戻る。BUG-224 上端から 2 本指の下 swipe で最大化 → 窓。BUG-228 全画面から下端の 2 本指の上 swipe で最大化（窓でなく）。
- 2026-10-06「計画としては、Cの設計の見直しをまずやりましょう。」

## 1. 最大化の session の状態（BUG-217）

**状態**: compositor（shell）に desktop ごとでなく **session に 1 つ**の `layout_mode`（`WINDOWED` / `DOCKED`）を持つ。今の窓ごとの `maximized`（dock）はこの mode を写す。

| 出来事 | 規則 |
| --- | --- |
| 利用者が窓を dock（title の double click・dock の button・上端への drag・Super+↑） | `layout_mode = DOCKED` |
| 利用者が dock を外す（restore の button・drag で外す・Super+↓・BUG-224 の gesture） | `layout_mode = WINDOWED` |
| app の切り替え（Alt+Tab の確定・WiseView の確定・bar の icon の click・App Home で起動中の app を選ぶ（BUG-232）・3 本指の preview の確定） | 切り替え先の窓を `layout_mode` に合わせる: DOCKED なら dock（animation つき）、WINDOWED なら前の浮いた位置と大きさに戻す |
| 新しい窓の map | 今の規則（ws099-p033: dock の中で開く窓は dock で）を `layout_mode` で言い直す |
| 全画面（fullscreen）の出入り | `layout_mode` は変えない。全画面から出る時は `layout_mode` に戻る（BUG-208 の `fullscreen_docked` は `layout_mode` で置き換える） |
| 窓を閉じる・最小化 | `layout_mode` は変えない |
| dock を拒む窓（大きさの固定・dialog・popup） | 状態を変えずに、その窓だけ浮いたまま |

- 切り替え元の窓は**そのままにしない**: DOCKED の時、切り替え元は dock のまま後ろに残る（1 つの面に 1 つの窓が見える tablet の形）。WINDOWED の時は浮いたまま。
- 浮いた位置を覚える: 窓ごとの `restore_rect`（今の dock の前の位置）を、mode の切り替えでも保つ。
- 実装の場所: `userland/desktop/wayland/shell.c` の `window_dock()`・`window_undock()`（4411・4472 行）と、切り替えの入口（`switcher-shell.c`、WiseView の確定、bar の click、`home.c` の `home_launch()`）を 1 つの関数 `shell_switch_to(window)` に集め、そこで mode に合わせる。`protocol.c` の `zwl_window_enter/leave_fullscreen` は `fullscreen_docked` の代わりに `layout_mode` を見る。
- log: `ZWL LAYOUT mode=docked|windowed reason=...`（AAT の確かめ用）。

## 2. touchpad の gesture の体系

今の種類（`touchpad.h`）: BOTTOM2（下端から 2 本指の上）・UP3・LEFT2・RIGHT2（端から）・TAP3。足す物と決まり:

| gesture | 今の状態 | 動作 |
| --- | --- | --- |
| 下端から 2 本指で上（BOTTOM2） | 窓の mode・最大化 | **WiseView を開く**（今どおり） |
| 下端から 2 本指で上（BOTTOM2） | **全画面** | **全画面 → 最大化**（BUG-228。WiseView は開かない） |
| 上端から 2 本指で下（**TOP2**、新規） | 最大化 | **最大化 → 窓**（BUG-224、`layout_mode = WINDOWED`） |
| 上端から 2 本指で下（TOP2） | 全画面 | 全画面 → 最大化（全画面からは 1 段ずつ） |
| WiseView の中で 2 本指の左・右 swipe | WiseView | **選択を 1 つずつ**移す（1 回の swipe（指を離すまで）で 1 つ、BUG-216） |
| WiseView の中で 2 本指の下 swipe | WiseView | **選んだ窓で確定して戻る**（BUG-216、`shell_switch_to`） |
| 3 本指の tap（TAP3） | — | 窓の preview（Alt+Tab の画面）を開く。今の app から（BUG-209） |
| preview の中で 2 本指の左・右 swipe | preview | **1 回の swipe で 1 つ**（BUG-215。今の「scroll の量で連続」をやめる） |
| preview の中で 2 本指の下 swipe・tap | preview | 確定 |

- 「1 回の swipe で 1 つ」の判定: 指が置かれてから離れるまでに横の移動が閾値（端の gesture と同じ `travel`、目安 8 mm）を超えたら、方向に 1 つ動かし、その swipe の間は追加で動かさない。慣性の scroll（BUG-211）の event とは区別する（gesture の mode の間は client に scroll を送らない）。
- WiseView の画面の題の文字「Wiseview」は描かない（名前は code と docs で保つ）。案内の文は「Swipe left or right to choose · Swipe down to open」（翻訳の catalog に）。
- TOP2 の検出: `touchpad.c` の `edges_of_fingers()` に上端の band を足す（BOTTOM2 と対称）。
- log: `ZWL GESTURE name=top2 ...`、`ZWL WISEVIEW select step=+1|-1 via=swipe`。

## 3. 試験

- host: `shell_switch_to` の mode の規則の表（窓の種類 × mode × 出来事）、gesture の判定（1 swipe で 1 つ、TOP2 の検出、全画面の時の BOTTOM2）。
- QEMU（T1、AAT のシナリオ）: tests/scenarios/desktop/ に draft で `layout-mode-switch`（dock の app から窓の app へ Alt+Tab → 窓が dock になる、逆も）、`wiseview-swipe`、`preview-swipe-step`。touchpad の gesture は QEMU の usb-multitouch か `/dev/input-inject` の MT で。
- 実機（ユーザー）: 5330 の touchpad で gesture の感触。

## 4. Phase の分け方（実装、ベータ2）

| Phase | 内容 | 見積もり |
| --- | --- | --- |
| p008 | `layout_mode` と `shell_switch_to`、全ての切り替えの入口の集約、全画面との整合、host 試験 | 1 LW |
| p009 | gesture: TOP2、全画面の BOTTOM2、WiseView・preview の 1 swipe 1 つ、確定の下 swipe、題の文字の削除 | 1 LW |
| p010 | AAT のシナリオ・T1・規約の見直し | 0.3 LW |

## 5. 未決（ユーザー）

1. DOCKED の時に切り替え先が dock を拒む窓（固定の大きさ）なら、浮いたまま中央に出す（案）でよいか。
2. BUG-209 の 4 つの仮定（端で回る、Shift で左、3 本指の tap も今の app から、短い Alt+Tab は今の app のまま）。この設計は仮定どおりで書いた。
