<!-- awesome-plan project=zedbsd record=ws074p035 -->

# ws074-p035: flexbox（最小）

Phase ID: `ws074-p035`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29。2026-09-28 の枠は ASan と guest が未実施で uncleared、2026-09-29 の枠で残りの確認と入れ子の flex の修正を行った）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: p013、p069

## 範囲（amazon-goal.md §4 の 5）

row・column（と reverse）、wrap、`flex` の shorthand、grow・shrink・basis、`justify-content`・`align-items`・`align-self`、
`gap`、`order`、auto margin、`inline-flex`（block として）、`-webkit-` の別名。

## 設計と実装

- CSS（`css/values.c`・`cascade.c`・`css.h`・`internal.h`）: `flex-direction`・`flex-wrap`・`justify-content`・`align-items`・
  `align-content`・`align-self`・`row-gap`・`column-gap`（`grid-*-gap`）・`flex-grow`・`flex-shrink`・`flex-basis`・`order`、
  shorthand `flex`（none・auto・数と basis の組）・`flex-flow`・`gap`、`-webkit-` の別名。display の `-webkit-flex` は flex、
  `-webkit-box` は block（line clamp 用の使われ方のため）。
- box（`layout/box.c`）: flex container の子を item に（要素の box は inline・replaced も block-level に、float を止める、
  連続する text は anonymous の item、空白だけの text は捨てる）。
- layout（`layout/flex.c` 新、`block.c`・`position.c`）: item を order で安定に並べ、仮の主軸の大きさ（basis・width/height、
  無ければ内容: row は広い幅で測った最長の行、column は高さ）を min/max で挟み、wrap なら行に分け、grow・shrink（basis で
  重み付け）で余りを分け、残りを auto margin に。各 item を block として主軸の大きさで layout し、justify-content で主軸、
  align で交差軸に置く（stretch は行の太さへ）。flex container は formatting context、item も。行の flex の内容の幅は item の
  flex 前の margin box の和（shrink-to-fit と入れ子の測定のため）。
- 試験: `tests/pages/flex.html`（新、golden 4 つ）。

## 確認（2026-09-28 の枠。host の証拠）

- host の build（plain、-Werror）warning 0。style-check: 新しい指摘 0（`cascade.c` の 1 件は既存）。
- 回帰（plain のみ）: golden 48/48（flex の 4 つを追加、他は不変）、host-view 59/59、host-form 28/28、run-js-tests 7/7、
  run-dom-tests 6/6、run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17、host-base・heap・interp・link・number・
  object・position・text 0 failed。**ASan の build と回帰は未実施**（wrap-up のため）。
- `chrome-boxes.py flex.html`（800x700）: 67 の box のうち 65 が 1px 以内（違う 2 つは太字の glyph の幅の差）。
  `render-compare.py flex.html`（修正前の値 87.77%、修正後は未計測）。
- Amazon（p068 と同じ capture の `*-local-noscript.html`、1280x900）:

  | page | p069 の後 | p035 の後 |
  | --- | --- | --- |
  | トップ | 画素 27.89%、ink 21.30% | 画素 33.49%、ink 24.38% |
  | 検索 | 画素 75.16%、ink 19.89% | 画素 78.51%、ink 28.10% |

  検索の `--render` は host で 1.57 s。

## 写真（2026-09-28 の枠）

- `p035-20260928-flex.png`（修正前の比較）と Amazon の比較は前の枠の worktree（`agent-ae19962dd0a453495`、merge の後に削除）の
  `build/` にあり、もう無い。2026-09-29 の写真は下。

## 2026-09-29 の枠（残りの確認と修正）

- 環境: worktree の branch を main（077e0609）へ fast-forward。guest の image は main の sysroot を worktree の
  `build/amd64/sysroot` に複写し、`build/llvm`・`build/NoctLang`・`build/distfiles`・`build/ws035-fonts`・`build/ws035-wallpaper`・
  `build/ws035-sq-venus`（Venus の renderer）を main の `build/` への symlink にして `build-browser-image.sh` で作った
  （`make toolchain` は走らせていない。build は worktree の `build/llvm-source` に LLVM の source を展開した。共有の tree への書き込みは無い）。
- **見つけた不具合と修正**（guest の live の Amazon で検索の欄が出ない）: `#nav-logo` の中の `a.nav-logo-link` の `width: 100%` が、
  flex item の内容を広い幅（2^24 単位）で測る間にその幅の 100% になり、logo が約 262135 px になって header の検索の欄を 0 幅に押し出していた。
  - `layout.h`: `layout_tree.measuring`（広い幅での測定の入れ子の数）。`position.c` の `layout_shrink_to_fit` と `flex.c` の
    `flex_base_size` の測定の間に数える。
  - `flex.c`: 測定中の container の % の flex-basis・幅は不定（内容の大きさ）。`flex_lay_item` は item の style の大きさと margin を
    layout の後に戻す（`flex_lay_sized` に分けた）。前は flexed の大きさを style に書いたままで、次の layout（測定の後の本番）が
    前の大きさ（測定の幅で伸びた大きさ）を basis にしていた。
  - `position.c`: `layout_content_width` は % の幅の子も内容で測る（Chromium と同じく、測る幅の % は不定）。
  - `flex.c` の真偽値の直接代入（前の枠の残り）を `if` に直した。
  - 試験: `flex.html` に入れ子の測定の行（`.nested`、100% 幅の item を持つ flex item と伸びる item）を足し、golden 4 つを更新。
  - `browser-p032.sh`: 検索の欄の位置を flex item の `block <input>` からも取る、local の capture（`top-local-noscript.html`）を使う、
    欄が header にあるので PageDown の後に PageUp で戻す、結果の page の待ちを 90 s に。

## 確認（2026-09-29。host は Debian の cc。guest は QEMU（Venus）。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check（`flex.c`・`position.c`・`layout.h`）: 指摘 0。
- 回帰（plain と ASan の両方、修正の後）: golden 48/48、host-view 59/59、host-form 28/28、host-link 22/22、host-position 19/19、
  host-text 20/20、host-base 2038、heap 31、interp 45、number 71、object 98（全て 0 failed）、run-js-tests 7/7、run-dom-tests 6/6、
  run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17。ASan の `--render` で Amazon の 3 つの capture
  （top・search の local-noscript、script 付きの top-local）に報告なし。
- `render-compare.py flex.html`: 800x700 で 98.37%（修正前の枠の値 87.77%）、入れ子の行を足した後 800x820 で 98.19%。
  `chrome-boxes.py`（800x820）: 77 の box のうち 75 が 1px 以内（違う 2 つは既知の太字の glyph の幅、入れ子の行の 10 box は全て一致）。
- Amazon（2026-09-28 23:52 に取り直した capture の `*-local-noscript.html`、`live-compare.py`、1280x900）:

  | page | p035（入れ子の修正前） | p035（修正後） |
  | --- | --- | --- |
  | トップ | 画素 33.49%、ink 23.59% | 画素 34.45%、ink 23.79% |
  | 検索 | 画素 76.96%、ink 26.91% | 画素 77.89%、ink 27.53% |

  header（logo・届け先・検索の欄・言語・account・cart）が Chromium と同じ並びになった。検索の `--render` は host で 1.46 s、トップ 0.48 s。
- guest（QEMU の Venus、`build-browser-image.sh` の image、zdesktop 1280x800）:
  - `browser-p032.sh`（form.html: Tab・入力・Enter で送信）: status 0。`browser-page.sh flex.html`: status 0（READY、ERROR なし）。
  - `browser-p032.sh --amazon`（live）: トップの header の検索の欄を click して「kei」を入力、Enter で
    `/s/ref=nb_sb_noss?…field-keywords=kei` へ NAVIGATE（status 0）。結果の page は外の CSS 約 2 MB が届くにつれて描き直され、
    Enter から 40 s ではまだ style の無い描画、直接開いた場合 30 s で途中、90 s で header・結果・filter が style 付きで出る
    （速さは p071）。live の取得はこの枠でトップ 3 回・検索 2 回（locator の失敗の再試行を含む）。

## 写真（`/home/awe/zedBSD-rpi4/build/ws074-shots/`）

- host: `p035-20260928-final-flex.png`（flex.html、私たち | Chromium | 違い）、`p035-20260929-nested-flex.png`（入れ子の行を含む）、
  `p035-20260929-amazon-top-local-noscript.png`・`…-search-local-noscript.png`（修正後）、`p035-20260928-amazon-*-local-noscript.png`（修正前）。
- guest（QEMU）: `p035-20260929-guest-form-typed.png`、`…-guest-flex-window.png`、`…-guest-amazon-top-typed.png`（検索の欄に kei）、
  `…-guest-amazon-results-40s.png`（Enter から 40 s、style 前）、`…-guest-amazon-search-30s.png`・`…-search-90s.png`。

## 未実施・残り

- 実機での確認は未実施。
- 範囲の外に残したもの: `align-content`（start 以外）、`wrap-reverse` の行の順、baseline（start として）、自動の最小の大きさ
  （min-content）、不定の高さの %、`inline-flex` の inline-level、`-webkit-box` の flex としての扱い、
  stretch された item の中身の再 layout（高さだけ変える）、入れ子の flex の測定の回数（各段で測定と本番の 2 回、深い入れ子で増える。p071）。
