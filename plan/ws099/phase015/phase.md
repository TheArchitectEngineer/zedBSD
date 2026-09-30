<!-- awesome-plan project=zedbsd record=ws099-p015 -->

# ws099-p015: 全画面を常に合成し、下の端からの swipe で窓に戻す

Status: uncleared（2026-09-30、サブエージェント P6、worktree `ws035-keiland`（branch `wt/ws035`）。QEMU の Venus だけ。実装と C3 の試験は PASS、
他の WS の回帰の試験 3 本（ws079 p010、ws035 p052・p053）が直の scanout の前提のまま FAIL。直しの差分を main に依頼（下の「残り」）。その適用と C9 の再実行で clear）
Disposition: normal
Parent: [WS099](../ws.md)
Queue: なし（2026-09-30 Q1 の割り当て）
依存: p010（cleared）

## 範囲と受け入れ

- ユーザーの判断（2026-09-30 昼、ws.md の「Notes の全画面の出口」）: 「画面を下からSwipeで戻しましょう。また、全画面でコンポジット無効のモードに
  なっているなら、それは使わないように修正して、コンポジットを有効にした上で、スワイプ操作を可能にします。」
- 全画面の直の scanout（`display.c` の fullscreen mode）を使わず、全画面の窓も window mode で合成する。使われなくなる code は消す。
- 全画面の間、下の端から上への swipe（pointer と touch）で窓に戻す。Wiseview の swipe より先に全画面の解除として扱う。窓の位置は BUG-114 の規則のまま。
  下の左右の角は WS102 の keyboard のまま（K7）。
- C3 の試験に「Notes の swipe で全画面 → 下からの swipe で窓に戻る」を足す。
- 全画面の Notes の pen の線の遅れと frame の間隔を前後で測る。WS079 の `demo-s8-s9.sh` の頁送り（L2 は最長 142 ms）を悪くしない。
- 回帰: C9、WS079-p010、boot test。IME・browser・keyboard.c・toolchain は触らない（触っていない）。

## 変更

- `display.c`: fullscreen mode（`enter_fullscreen`・`present_current`・`hidden_callbacks`、import の `scanout` の判定）を消した。
  `zwl_schedule` は最初の pass で window mode に入り、そのまま全画面の窓も合成する。`zwl.h` の `scanout`、`compose.[ch]`・`import.c` の説明を合わせた。
- `seat.c`: 「全画面の窓の上では端の gesture だけが zdesktop の物」の判定を `server->windowed`（直の scanout の有無）から
  `zwl_glass_fullscreen_input`（shell.c）に替えた。全画面の上では resize の矢印を出さない。
- `shell.c`:
  - `zwl_glass_fullscreen_input`・`fullscreen_top`: 上の窓が全画面で、上に何も出ていない（`zwl_glass_overlay`）時、または下の swipe が接触を持つ間。
  - 下の端の swipe（`unfullscreen_press`・`unfullscreen_motion`）: 全画面の上で下の 20 px（`WISEVIEW_EDGE`）で始まった左の press を取り、
    上へ 80 px（`UNFULLSCREEN_DISTANCE`）動いたら `zwl_window_leave_fullscreen`（BUG-114 の置き場所）。接触は離すまで swipe の物。
    `zwl_glass_edge_button` では keyboard の角（`zwl_keyboard_button`）・Notes の角・App Home の角の後、Wiseview の代わりに置いた。
    log は `ZWL GLASS unfullscreen-swipe start y= source=`・`ZWL GLASS unfullscreen surface= via=swipe errno=`。
  - `zwl_glass_title_at`: 全画面の上では title bar の指を待たない（touch.c の 2 本指の判定に渡さない）。
  - 合成の見た目: 全画面の窓は本体だけ（title bar・影・すりガラス無し）、角は四角、不透明（`draw_window`・`draw_body`）。直の scanout の時と同じ絵。
    前は角が丸く、画面の四隅に壁紙が見えた。
  - `fullscreen_whole`: 全画面の窓が上で、何も上に無く、静止し、不透明で画面を覆う時、壁紙・desktop・下の窓を描かない（`zwl_glass_draw`）。
- `damage.c`: client が隠した cursor が、その client の窓の本体の上だけを動く時は、何も描き直さない（`cursor_unseen`）。全画面の Notes の上の pen の
  hover と線で、位置ごとに出力を描き直していた（下の計測の「最初の版」）。
- 計測の行（`--log-frames` の時だけ）: `ZWL LAT pen`（tablet.c、pen の位置を送った時）・`ZWL LAT adopt`（display.c、image を取った時）・
  `ZWL LAT submit`・`ZWL LAT shown`（compose.c、frame の submit と fence）、`zwl_microseconds`（wire.c）。変更の前の image には
  `ZWL LAT shown ... direct=1`（直の present の後）も入れて比べた（`build/ws099/p015-before-lat.img`）。

## 試験（`plan/ws099/tests/`）

- `c3-swipe-back.sh`（C3、`criteria.sh` の C3 に追加）: 1. 右上の swipe で Notes を全画面、2. 下の端から上の swipe で窓に戻り（placed=0、中央、
  title bar は system bar の下 y>=98）、Wiseview は開かない、3. title bar で drag、4. もう一度全画面にして swipe で drag した場所に戻る（placed=1）、
  5. 下の端より上から始めた線は Notes の物、6. 右下の角の swipe は全画面の上でも keyboard（`ZWL OSK open kind=flick`）で、下の swipe にならない、
  7. image に `/bin/touchinject` がある時（WS079 の demo の image）、指の下の swipe（`source=2`）でも窓に戻る。
- `p015-pen-latency.sh IMAGE OUTDIR [STROKES]`・`p015-lat.py`: demo の image で `/bin/notes --fullscreen`、peninject で 6 本の線（60 点、15 ms 間隔）。
  遅れ = pen の位置を送った時から、その後の Notes の次の image を映す frame の終わり（直の present の return、または合成の frame の fence）まで。
  間隔 = 線の間に Notes の新しい image を映した frame の間。Notes 自身の `NOTES FRAMES` も出す。QEMU の Venus（lavapipe）の数で、実機の数ではない。

## 計測（QEMU の Venus、1280x800、同じ host で交互に）

| image | 遅れ 中央値 / p90 / 最大（ms） | frame の間隔 中央値 / p90 / 最大（ms） | Notes の frame（1 本あたり、frame_us） |
| --- | --- | --- | --- |
| 前（直の scanout）1 回目 `build/ws099/p015-lat-before1` | 73.0 / 112 / 140 | 107 / 124 / 307 | 9 frame、107〜112 ms |
| 前 2 回目 `p015-lat-before2` | 74.0 / 116 / 139 | 107 / 289 / 322 | 9 frame、105〜112 ms |
| 後、最初の版（合成だけ）`p015-lat-after1` | 276.5 / 503 / 524 | 379 / 516 / 522 | 5 frame、226〜260 ms |
| 後、最初の版 2 回目 `p015-lat-after2` | 268.0 / 494 / 960 | 355.5 / 574 / 574 | 2〜5 frame、145〜330 ms |
| **後、最終**（全画面だけを描く、隠れた cursor は描き直さない）`p015-lat-after3` | 116.5 / 187 / 592 | 113 / 208 / 311 | 7〜10 frame、93〜131 ms |
| **後、最終** 2 回目 `p015-lat-after4` | 117.0 / 199 / 249 | 113 / 244 / 421 | 10〜11 frame、92〜105 ms |

- frame の間隔（中央値）は 107 → 113 ms でほぼ同じ、Notes が 1 本の線で描く frame は 9 → 10〜11 に増えた。遅れ（中央値）は 74 → 117 ms（+43 ms）。
  増えた分は合成の frame 1 枚の費用（QEMU の Venus で `ZWL PERF compose draw_ms` 約 100 ms、ほとんどが acquire と submit+present の固定の費用）。
  実機の i915 の合成の費用はこれより桁で小さい見込みだが、実機は未実施。
- 最初の版が遅かったのは、pen の位置ごとの cursor の描き直し（Notes は pen の cursor を隠しているのに、出力全体の frame を作っていた）と、
  Notes の image を待つ間にその frame が入ったため。

WS079 の `demo-s8-s9.sh`（QEMU、同じ host）:

| image | scroll の頁送り 最長 | page mode の frame 最長 | page mode の頁送り（slide 込み）最長 | 結果 |
| --- | --- | --- | --- | --- |
| 前 `build/ws099/p015-before.img` | 136 ms | 139 ms | 434 ms | PASS |
| 後 `build/ws099/p015-after-lat2.img` | 134 ms | 135 ms | 452 ms | PASS |

L2 の最長 142 ms（page mode の frame）は後も 135 ms で超えない。S8（指の swipe で全画面、pen、Esc）も PASS。

## 検証

| 確認 | 結果 |
| --- | --- |
| build（WS079 の demo の image `build/ws099-p015-demo`、criteria の image `build/amd64`） | exit 0、`-Werror` で warning 0、`git diff --check` 0 |
| `c3-swipe-back.sh`（demo の image、touch 込み） | **PASS**（`build/ws099/p015-c3.log`）。1 回目は touch の script の put が失敗して FAIL、put の確認と再試行を足して PASS |
| `criteria.sh` C3（criteria の image、touch は SKIPPED） | p138 **PASS**、c3-swipe-back **PASS**（`build/ws099/p015-criteria/results.txt`） |
| `criteria.sh` C9 | 下の表 |
| WS079 `zdesktop-p010.sh`（demo の image） | **FAIL** 2 項目、他 43 項目 ok: `MODE fullscreen` の log が無い（直の scanout を消した）、「全画面の Notes の上の下の swipe で Wiseview」が開かない（ユーザーの判断で窓に戻す、に替わった）。直した版（`plan/ws099/phase015/proposed-p010.diff`）は **PASS**（`build/ws099/p015-regress/p010-proposed.log`、Wiseview 0・窓に戻る 1）。`notes-fullscreen` の画面の四隅が Notes の色（角が四角）も ok |
| WS079 `demo-s8-s9.sh` 前・後 | どちらも **PASS**（上の表） |
| boot test（criteria の image `build/ws099/p015-criteria.img`） | **PASS**（`build/ws099/p015-boot/login.png`） |
| 最後の build（`fullscreen_whole` の条件の関数呼び出しを変数へ出した style の直しの後）: demo `build/ws099/p015-final-demo.img`、criteria `build/ws099/p015-final-criteria.img` | どちらも exit 0、desktop の warning 0 |
| 最後の image で直した p052・p053（`proposed-p052.diff`・`proposed-p053.diff` を当てた複写） | どちらも **PASS**（`build/ws099/p015-final/p052.log`・`p053.log`） |
| 最後の image で `c3-swipe-back.sh`（demo の image、touch 込み） | 1 回目 FAIL（touchinject の device が開かれる前に指を送った: `ZWL TOUCH added` の後に contact が無く `removed`）。script の待ちを 1500 → 3000 ms にして **PASS**（`build/ws099/p015-final/c3b.log`） |
| 最後の image で boot test | **PASS**（`build/ws099/p015-final/boot/login.png`。boot test の QEMU は Venus が無く greeter は console に戻る、前の Phase と同じ） |

C9（`build/ws099/p015-criteria/results.txt`）:

| 試験 | 結果 |
| --- | --- |
| p052 | **FAIL**: 直の scanout の `MODE fullscreen`・2 回目の `MODE window` を待つ試験（2 分の待ちが切れ、その後の画面も時間がずれて違う）。直した版 `plan/ws099/phase015/proposed-p052.diff`（configure の fullscreen=1・0 を待つ） |
| p053 | **FAIL**: 手順 4「fullscreen mode は cursor を描かない」。合成になったので全画面の窓の上にも zdesktop の矢印が出る（前は直の scanout で cursor が無かった）。直した版 `proposed-p053.diff`（矢印を期待） |
| p072・p076・p126・p128・p134・p137・p138・cursor-owner | 全て **PASS** |

画面: `build/ws099/p015-final/c3b/`（最後の image。touch.png は窓に戻った直後で Notes の新しい大きさの image の前、`NOTES LAYOUT window=1024x690` はその後に出ている）、`build/ws099/p015-c3/`（fullscreen.png・window.png・moved.png・back.png・stroke.png・osk.png・touch.png）、
`build/ws099/p015-lat-before1/strokes.png`・`p015-lat-after3/strokes.png`（全画面の Notes の線、前後で同じ絵、四隅も同じ）、
`build/ws099/p015-regress/s8s9-after/`（S8・S9）。

## 判断の要る点・振る舞いの変化

- 全画面の窓の上に zdesktop の矢印（mouse の cursor）が出る。前は直の scanout で全画面の上に cursor が無かった（mouse では全画面の Notes の
  toolbar を狙えなかった）。client が隠した cursor（Notes の pen）は今までどおり出ない。p053 の手順 4 はこの点を試していた。
- 全画面の上の下の端の swipe は、Wiseview ではなく窓に戻す（ユーザーの判断どおり）。全画面の上で Wiseview を開くのは Super+Tab だけになる。

## 残り

- main への依頼: 他の WS の試験の直し（修正範囲の外なので適用していない）。`plan/ws099/phase015/proposed-p010.diff`（ws079）、
  `plan/ws099/phase015/proposed-p052.diff`・`proposed-p053.diff`（ws035、C9 の一覧）。適用後に C9 を流し直して clear。
- 実機（5330）の pen の遅れと frame の間隔は未実施（QEMU の数だけ）。
- 遅れの中央値は QEMU で +43 ms。実機で気になるなら、全画面の窓の image だけを合成の pass の先頭で取る、Notes の image を待つ間の frame を止めるなどを
  別の Phase で。
