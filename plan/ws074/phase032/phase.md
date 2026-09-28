<!-- awesome-plan project=zedbsd record=ws074p032 -->

# ws074-p032: form の部品と文字の入力

Phase ID: `ws074-p032`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: p017、p056（focus と key の入力）

## 範囲（2026-09-28 に絞った。p059・p067 の後）

デモの目標（amazon.co.jp、[amazon-goal.md](../amazon-goal.md)）の検索の操作に要る form: `<input>`（text・password・submit・button・
reset・hidden・checkbox・radio。search・email 等は text の欄として）、`<textarea>`、`<select>` の最小（選ばれた option を描いて送る）
の描画、focus と caret、文字の入力と編集、Enter と submit の button による送信（GET、page の encoding）、script が有効なときの
`noscript` を隠す、`box-sizing`。fetch・XHR・Location・History・localStorage は p064 へ移した。

## 設計と実装

- DOM（`dom/control.c` 新、`dom/dom.h`）: `struct dom_control`（element が持ち、finalizer で解放）に値と caret、dirty、checkedness、
  描画が記録する content box・caret の位置と色・横の scroll。`dom_control_kind`（type 属性から）、`dom_control_value`
  （dirty なら自分の値、でなければ value 属性か textarea の text）、`dom_control_label`（button の既定の label は Submit・Reset）、
  `dom_option_text`、`dom_select_chosen`、`dom_attribute_ascii`。
- CSS: `box-sizing`（`-webkit-` の別名も）、border-style の `inset`・`outset` を区別（描画は solid のまま）。UA の stylesheet に
  Chromium の form の部品の規則（13.33px、inline-block、field の `2px inset #767676`、button の `2px outset`・`#efefef`・
  border-box・中央寄せ、checkbox・radio の margin、textarea の monospace、select）と `noscript { display: none }`（この browser では
  script は常に有効）。
- layout（`layout/control.c` 新）: `<input>`・`<textarea>`・`<select>` は replaced box。自然な大きさは Chromium の式: field は
  `size`（既定 20）× 平均の文字幅 + (最大の文字幅 − 平均)（OS/2 の xAvgCharWidth と head の bbox の幅を丸めた値、`text/font.c` の
  `text_font_char_widths`、表が無ければ「0」の advance）、高さは line-height、button は label の幅、checkbox・radio は 13px、
  textarea は `cols` × 平均 + scroll bar 15px × `rows` 行、select は最も広い option + 矢印の 20px。text を持つ部品は text の baseline
  で行に立つ（`inline.c`）。`box-sizing: border-box` を replaced と block の幅・高さに適用。
- 描画（`paint/list.c`）: UA の見た目のまま（四辺が inset・outset）の部品は Chromium と同じく 1px の `#767676` の枠と背景、author が
  変えたものは style どおり（背景画像も）。field の値（password は •、空なら placeholder を `#757575` で）を content box に clip し、
  caret が見えるように横に scroll。button の label は text-align（既定は中央）、textarea は行ごと、select は option と矢印。描いた
  位置を control の状態に記録し、page が caret（1px、文字の色、program の focus があるとき）を display list の最後に足す
  （`paint_add_rect`）。
- 入力と送信（`page/form.c` 新、`page/input.c`、`view/view.c`）: focus のある text の部品が文字（keypress が取り消されなければ）と
  Backspace・Delete・左右・Home・End（textarea の Enter は改行）を受け、`input` event。maxlength。Tab で入ると caret は末尾、
  click で入ると押した位置の最寄りの文字の境界、text の部品は focus の ring を常に出す（Chromium の `:focus-visible`）。Space は
  button・checkbox・radio を押す（scroll しない）。checkbox・radio の変更は `input`・`change`、radio は同じ group を外す。field の
  Enter は既定の button の click を経た暗黙の送信、submit の click・Enter は その button を submitter に。`submit` event（取り消し
  可）、entry list を tree 順に（名前の無いもの・disabled を除き、button は submitter だけ、checkbox・radio は checked のもの、
  select は選ばれた option、textarea の改行は CRLF、`_charset_`）、`application/x-www-form-urlencoded`、encoding は
  `accept-charset` → `<meta charset>`・http-equiv → UTF-8（windows-1252 では表せない文字を `&#N;`）。action から query と fragment
  を除いて `?` と entries を付け、view が link と同じく link の callback を経て開く。POST は送らず console に出す。
- 比較の道具: `chrome-fonts.sh` の fontconfig で、font に無い family（Arial、form の部品の system の font）を Inter → Droid Sans
  Fallback に（browser と同じ振る舞い。以前は Droid に落ちて Chromium の参照が崩れていた）。
- 試験: `tests/host-form.c`（新、28 の確認）、`tests/pages/form.html`・`results.html`（新）、`tests/browser-p032.sh`（新、guest）。
  layout の dump に `control KIND` を足した。

## 確認（host は Debian の cc、guest は QEMU の Venus。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。amd64 の image の build: browser・libbrowser の warning 0。
  style-check（変更した source）0（`css/cascade.c` の 1 件は main の既存）。`git diff --check` 0。
- `host-form` 28/28（plain と ASan）: 描画（値・placeholder・label・option・textarea・password を出さない）、caret（focus で出て、
  program の focus が無いと消える）、入力と編集、maxlength、Space で scroll しない、input・change・submit の event、暗黙の送信の
  URL（UTF-8、tree 順、CRLF）、submitter、windows-1252（`t=%E9%26%2326908%3B`、action の query と fragment を除く）、POST を送らない。
- 回帰: `host-view` 59/59、golden 32/32（keys の 3 つを更新: `<button>` が UA の見た目になり、p060 までは全幅の block）、
  `run-js-tests` 7/7、`run-dom-tests` 6/6、`run-loader-tests` 11/11、`run-http-tests` 14/14・`--async` 16/16。ASan で Amazon の
  検索の capture の `--render` に報告なし。
- Chromium との比較（`render-compare.py form.html`、800x600）: 97.31%。field の幅・高さ（Name は 223x22 と 221x22）と位置は一致。
  違い: `<button>` が全幅（p060）、checkbox の印・radio の丸（p062）、textarea の下の行の高さ。
- Amazon（script を除いた capture、`live-compare.py`）: 検索 2.42% → 39.08%、トップ 11.56% → 9.19%（noscript の style が消え
  部品が出たが、外の CSS がまだ無いので部品と `<button>` の全幅が他をずらす）。
- guest（`browser-p032.sh --amazon`、status 0）: form.html で Tab・「kei」・Enter → `results.html?name=kei&q=kei&secret=abc&hint=
  &agree=on&color=red&kind=books&…` を開く。**live の amazon.co.jp のトップで検索の欄を click・「kei」・Enter → Chrome と同じ
  `/s/ref=nb_sb_noss?__mk_ja_JP=%E3%82%AB…&url=search-alias%3Daps&field-keywords=kei` を開き、題名「Amazon.co.jp : kei」の
  結果の page が出る**。両方の log に ERROR なし。`browser-p056.sh`・`browser-p057.sh` status 0。
- boot test PASS（`build/ws074-shots/p032-20260928-boot-login.png`）。

## 写真（`/home/awe/zedBSD-rpi4/build/ws074-shots/`）

- `p032-20260928-form-vs-chromium.png`（私たち | Chromium | 違い）、`p032-20260928-guest-form-typed.png`（Tab と入力の後、ring と
  caret）、`…-guest-form-submitted.png`。
- `p032-20260928-guest-amazon-typed.png`（Amazon のトップの検索の欄に「kei」）、`…-guest-amazon-results.png`（検索の結果の page）。
- `p032-20260928-amazon-search-noscript.png`・`…-top-noscript.png`（capture の比較）、`p032-20260928-boot-login.png`。

## 残り・移管

- `<button>` の inline-block（p060）。checkbox の印と radio の丸（p062 の角丸）。textarea の行の折り返し・縦の scroll・Enter 以外の
  上下の移動。select の選択の変更（dropdown、keyboard）。選択（shift・mouse の drag）、copy・paste、IME。`change` event の blur の
  ときの発火。`form` 属性、fieldset の disabled、reset button の動作、image button の座標、`formaction`・`formenctype`。POST
  （request の body が要る、p064）。HTTP の Content-Type の charset を form の encoding に使うこと（今は accept-charset と meta）。
  JS の `value`・`checked`・`form.submit()` の binding（p031）。
