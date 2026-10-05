<!-- awesome-plan project=zedbsd record=ws154-p001 -->

# ws154-p001: 設計（IME の選択、Languages の頁、SKK の IME）

Status: in-progress（2026-10-05、P2。設計を書いた。Q1 の確認待ち）
Disposition: normal
Parent: [WS154](../ws.md)
Queue: Q1（ベータ2 の割り当て、P2 の 3 番目）

## 今の構成（2026-10-05 に source を読んだ）

- IME は 1 つの process（`/usr/libexec/keiland-ime`、`userland/desktop/ime/`）。compositor（`wayland/input-method.c`）が socket pair で起動し、落ちたら 1 秒後に起動し直す（1 分に 3 回まで）。
- process は「言語」（engine）の列を持つ。今は `direct`（A、key をそのまま返す）と `ja`（あ、ローマ字かな漢字の文節変換、`SKK-JISYO.ja`）の 2 つ（`main.c` の `main_engines`）。Alt+Space で次へ、日本語の keyboard の key で ID を指定して選ぶ（`keiland_ime_status_v1` の `select`・`next`）。選ぶと `language(id, label)` で compositor に知らせ、右上の indicator が label を描く。
- engine の口は `engine.h`（`key`・`reset`・`surrounding`・`content_type`・`save`・`destroy`）で、Wayland を知らない。host の試験は engine.h だけで動かす。
- app ごとの言語の記憶（ws095-p016）は compositor が言語の ID で持つ（`struct zwl_ime_app`）。
- Settings の設定は `settings-keys.c` の表（compositor が持つ key は `KL_SETTINGS_RESOLVER_COMPOSITOR`）。頁は `pages.c` の表。
- Emacs（remacs）の辞書 `userland/base/emacs/dict/SKK-JISYO.X`（387 KB）・`SKK-JISYO.remacs`（86 KB）は UTF-8、remacs の作者の独自の著作で Zlib（`userland/base/emacs/LICENSE`。SKK-JISYO.L（GPL）からの写しは無いと header に書かれている）。

## 設計

### D1 設定の key

- `ime.method`（`KL_SETTINGS_RESOLVER_COMPOSITOR`、int、0〜2、既定 1、KEPT）。0 = なし（英語）、1 = 日本語、2 = SKK。今の動き（日本語）が既定。

### D2 選択の反映（再 login 不要）

- compositor は keiland-ime を起動する時に `--method=none|ja|skk` を渡す。設定が変わると、compositor は IME に SIGTERM を送り（辞書の保存が走る）、終わったら新しい引数で起動し直す。意図した起動し直しは「1 分に 3 回」の数に入れない。
- IME の engine の列: なし → `direct` だけ（Alt+Space は何もしない、indicator は A のまま）。日本語 → `direct`・`ja`（今と同じ）。SKK → `direct`・`skk`。
- app ごとの記憶は言語の ID なので、選択を変えた後に残る `ja` の記憶は、`skk` の列では「知らない ID」として無視され、その app は desktop の言語で始まる。

### D3 SKK の mode と indicator・app ごとの記憶

- SKK の mode（かな・カナ・英数（`l`）・全英（`L`））は、それぞれを言語の ID にする: `skk`（かな、label「あ」）、`skk-katakana`（「ア」）、`skk-latin`（「A」、SKK の ASCII mode。C-j で かな に戻る）、`skk-wide`（「Ａ」）。engine の中の mode が変わったら、process は `language(id, label)` を送り直す（engine の口に、今の mode の ID と label を返す関数と、ID で mode を選ぶ関数を足す。持たない engine は今のまま 1 つの ID）。
- こうすると、ws095-p016 の app ごとの記憶がそのまま SKK の mode も記憶する（compositor は変えない）。`select(id)` は ID の engine を選び、その mode にする。

### D4 SKK の engine（`userland/desktop/ime/skk-*.c`、engine.h の口）

範囲（ddskk の基本の操作）:
- **入力**: ローマ字かな（SKK の規則: `nn`・`n'`・`xa` などの小書き、`-` は ー、句読点）。かな mode・カナ mode（`q` で切替）、英数 mode（`l`、C-j で戻る）、全英 mode（`L`）。
- **変換の開始**: 大文字の key で ▽ mode（読みの入力）。読みの途中の大文字は送り仮名の始まり（`KaKu` → 読み「か」＋送り「く」で辞書の okuri-ari の「かk」を引き、▼書く）。`Q` で今の位置から ▽。
- **変換**: Space で ▼ mode（最初の候補）。Space で次、`x` で前。5 個目からは候補の窓に 7 個ずつ（`asdfjkl` で選ぶ、Space で次の 7 個、`x` で前）。Enter・C-j で確定。C-g で ▽ に戻す（▽ の C-g で取り消し）。確定せずに次の文字を打つと確定して続ける（ddskk と同じ）。
- **辞書にない時の登録**: 候補が尽きると登録の mode（preedit に `[登録]かく ` と入力中の語）。登録の中でさらに変換できる（再帰の登録、深さの上限 3）。Enter で利用者の辞書に足して確定、C-g で取り消し。
- **学習**: 確定した候補を利用者の辞書の先頭へ。利用者の辞書（`~/.config/kei/ime/skk-jisyo`、SKK の形式、UTF-8）は、今の日本語の IME と同じく、key が 3 分来ない時と終わる時に書く（BUG-143 の考え）。
- **入らない**（後で）: 接頭・接尾辞（`>`）、abbrev mode（`/`）、数値の変換（`#`）、注釈の表示、server の辞書、補完（Tab）。
- 秘密の欄（hint・purpose が password など）では学習しない（日本語の IME と同じ）。

### D5 辞書（ユーザー: Emacs の辞書を重複して持ち、別々に管理する）

- `userland/base/emacs/dict/` の 2 つを `userland/desktop/ime/skk-dict/` に複写して置き、以後は別に更新する。
- package `ime-dict-skk`（`KEILAND_DATADIR/keiland/ime/skk/SKK-JISYO.X` と `SKK-JISYO.remacs`）。引く順は 利用者 → X → remacs。license は Zlib（remacs と同じ、release の license の一覧に足す）。
- install の場所は今の日本語の辞書（`KEILAND_DATADIR/keiland/ime/ja/`）に揃える（WS154 の ws.md の「`/usr/share/kei/ime` → `/usr/share/keiland/ime` の案」は既に `keiland/ime` になっている）。

### D6 Languages の頁（Settings）

- 新しい頁 `SE_PAGE_LANGUAGES`（group は Personalization、glyph は地球か文字、検索の語 `language ime input japanese skk english`）。
- card「Input method」: 3 つの選択（日本語「Japanese」・「SKK」・「None (English)」）と短い説明。変えるとすぐ切り替わる（D2）。
- card「Display language」: WS158（翻訳）の UI の言語の選択の場所。WS158 ができるまで「English」だけを表示する（選べない）。

### D7 Linux・FreeBSD

- IME の process と compositor は 3 OS で同じ source（`Makefile.linux`・`Makefile.freebsd`）。SKK の engine と辞書の package も同じく足す。

## Phase

| Phase | 内容 |
| --- | --- |
| p002 | `ime.method` の key、compositor の `--method` と起動し直し、IME の engine の列（なし・日本語）、Languages の頁。host の試験（settings の key、IME の引数の解析）。 |
| p003 | SKK の engine（D4）と辞書の package（D5）。host の試験（engine.h を通した状態機械・変換・送り仮名・候補・登録・学習の保存）。 |
| p004 | SKK を選択に加え（D2・D3 の mode の ID）、T1（QEMU でキーの注入と indicator・preedit）と実機の UAT。全文の規約。 |

## Q1 に確かめる点

1. D2 の「設定を変えたら IME を起動し直す」で良いか（辞書の保存の後、数百 ms の間 IME が無い）。代わりに status の protocol に「method を変えよ」の request を足す案もある（protocol の変更が要る）。
2. D3 の「SKK の mode を言語の ID にする」で良いか（compositor の app ごとの記憶を変えずに済む）。
3. D4 の範囲（接頭・接尾辞、abbrev、数値の変換、補完は後）で良いか。
