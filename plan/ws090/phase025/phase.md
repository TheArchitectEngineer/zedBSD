<!-- awesome-plan project=zedbsd record=ws090-p025 -->

# ws090-p025: Browser の web の form の欄で IME を受け付ける

Status: in-progress（q833、P1。2026-10-07 browser の部分を実装・host 試験 28/28、QEMU は T1 待ち。Settings の User name は診断の log を足して T1 の再試験待ち）
Disposition: normal
Parent: [WS090](../ws.md)
Queue: q833（P1、WS031 の後）

## 範囲

- ws090-p022 の項目 6: Browser の page の `<input>`・`<textarea>` に focus がある時、IME の preedit・commit が欄に届く（正常系）。
- libbrowser の公開の browser.h の ABI は変えない（要るなら Q1 へ）。

## 確認

- host の試験（headless の page に preedit と commit を渡す）、QEMU は T1（AAT の image で Browser の form に「nihon」→ 日本）。

## Settings の User name の欄（2026-10-07、T1-317・T1-321、P1）

症状: User name の欄（plain、IME を受けない欄）で Alt+Space の後「nihon」を打つと `n` だけが出て、Space で空、Enter で Full name へ。T1-321 で `KEI-IME ACTIVATE` は出るが preedit・commit の log は無い。
調べたこと: `page-users-admin.c` は User name を `SE_FIELD_PLAIN` で描き、libkeiland の `kl_field` は plain の欄に caret を出さない（`kl_ui_text_wanted` は 0）ので、Settings は text input を off にするはず。compositor は text input が有効な時だけ IME に鍵を渡す。それなのに IME が activate している（`ime_current`: App Home・title bar の欄・app の有効な text input のどれか）。どれが activate させたか、今の log では分からない。
今回: compositor に `KWL TEXT enable client= surface=`・`KWL TEXT disable …` の log を足した（`wayland/text-input.c`）。T1 の再試験で、`KWL IME activate …`（`field home`・`field client= surface=`・`client=` のどれか）と `KWL TEXT` の並びから原因を決めて直す。

### T1-326 の結果（2026-10-07）と次の診断

- `KWL TEXT enable client=4 surface=19` は Alt+Space（`KWL IME language=ja`）の直後に 1 回だけ、disable は無い。preedit・commit の log は compositor に無い（出していない）。PNG: User name は「nihon」で `n` だけ、Space で空、Enter で Full name。Full name は「にほn」→「日本」で正しい。
- 調べたこと: libkeiland の `kl_field` を Settings と同じ順（plain の欄に focus、`se_field_key` の 1 pixel の frame、全体の frame）で host で動かすと `kl_ui_text_wanted` は 0（scratch の実験、commit しない）。compositor は text input の enter を keyboard の focus の変化でだけ送るので、enable が Alt+Space の後に出たのは enter の時か wanted の変化の時。どちらかは今の log で決められない。
- 今回: Settings に診断の log を足した（stderr、zdesktop の log に入る）: `ZSETTINGS TEXT wanted=… page=… keyboard=… admin_focus=… admin_mode=… name_plain=…`（`main_text_input`、wanted が変わる時だけ）、`ZSETTINGS TEXT event=<14 commit|15 preedit|16 delete> bytes=… before=…`（`se_fields_input`、文字は出さない）。T1 の再試験で、User name に focus がある時に wanted が 1 か、IME の commit・delete が Settings に届くかで原因を決めて直し、診断の log は直した後に外す。

## 実装（browser の web の form、2026-10-07、P1）

ユーザーの決定（Q1 経由、ws090-p025 (a)）: browser.h に関数を足す（追加だけ、既存の ABI は変えない）。engine と shell の両方を P1 が行う（B1 は停止中、WS074 の ws.md への覚えは Q1 経由）。

- `include/browser/browser.h`: `browser_view_text_target(view, float caret[4])`（focus が text の欄か textarea なら 1、password は 0、caret の矩形は view の pixel）、`browser_view_compose(view, preedit, begin, end)`（caret に下線つきで出す、値は変えず input も出さない、空で終わる）、`browser_view_commit_text(view, text, delete_before, delete_after)`（合成を消し、caret の前後の UTF-8 の byte を文字単位で消し、text を入れて input を 1 回）。`exports.map` は `browser_view_*` の wildcard なので変更無し。
- libbrowser: `dom/dom.h`・`control.c`（`dom_control` に `preedit`・`preedit_cursor`、解放）、`page/form.c`（`page_compose_element`・`page_compose`・`page_commit_text`・`page_compose_end`、`form_delete_around`・`form_utf8_length`）、`page/input.c`（focus が移る時・要素が document から外れた時に合成を終える）、`paint/list.c`（欄と textarea で値の caret の所に preedit を挟んで描き、caret は preedit の cursor、1 pixel の下線、placeholder は出さない）、`view/view.c`（3 関数）。
- shell: `internal.h`（`SHELL_EVENT_TEXT_COMMIT/PREEDIT/DELETE` と text・begin・end・before・after）、`window.c`（`KL_WINDOW_TEXT_*` を queue に）、`shell.c`（`shell_text` で view へ、`shell_frame` の後の `shell_text_input` で `kl_window_text_input`・`kl_window_text_cursor`）。title bar の location の欄は compositor の欄なので compositor が先に IME を取る（`ime_current`）。

### 確認（host）

| コマンド | 結果 |
| --- | --- |
| `BROWSER_HOST_BUILD=build/p1-browser sh plan/ws074/tests/host-build.sh plain`（`-Werror`） | build 成功、warning 0 |
| `BROWSER_HOST_BUILD=build/p1-browser sh plan/ws090/tests/host-browser-ime.sh`（新規、`pages/ime.html`） | 28 checks, 0 failed（focus 無しで target 0・commit を捨てる、text の欄の target と caret の矩形、合成の表示・下線・input 無し・caret の位置、commit の値と input、合成中の commit、前 3 byte・後 2 byte の削除、password は target でない、textarea の合成・focus の移動で終わる・commit） |
| `host-view`・`host-form`（`plan/ws074/tests/pages`） | host-view 59/59。host-form 29 中 2 failed（`draw: text-indent moves the placeholder`・`caret: drawn in the focused field`）は変更前の tree（git stash で戻した build/p1-browser-base）でも同じ 2 つで、今回の回帰ではない（字の metrics の期待値の古さ、WS074） |
| target の clang（amd64、`-Werror -fsyntax-only`）: libbrowser の 5 file、shell の 2 file、Settings の main.c・widgets.c | warning 0 |

未実施: QEMU（T1 に依頼: AAT の image で Browser の form に「nihon」→ 日本）、実機。準正常系の残り（surrounding text・文節の範囲・click での合成の終わり・textarea の value）は `plan/ws177/backlog-p1.md`。
