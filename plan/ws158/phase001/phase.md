<!-- awesome-plan project=zedbsd record=ws158-p001 -->

# ws158-p001: 翻訳の仕組みの設計

Status: in-progress（2026-10-05、P2。設計を書いた。判断の 5 点待ち）
Disposition: normal
Parent: [WS158](../ws.md)
Queue: Q1（ベータ2 の割り当て、P2）

## 今の構成（2026-10-05 に source を読んだ）

- UI の文は C の source の中の英語の文字列（`"Open"`・`"Move To"` など）で、翻訳の口は無い。
- zedBSD の libc の printf は位置の指定（`%1$s`）を持たない（`src/libc/format.c`）。語順の違う言語のために、位置の指定は翻訳の口が自分で持つ。
- font は UI の Inter と、仮名・漢字を持つ fallback（DroidSansFallbackFull、Apache-2.0）。日本語の文は今も描ける（Files の日本語の file 名などで確かめ済み）。
- 設定は `kl_settings_*`（libkeiland）で compositor が持ち、`kl_settings_watch` で app に変更が届く（Settings の頁が compositor の設定を変え、compositor・app がすぐ追う経路がすでにある）。
- WS154 の Languages の頁に「Display language」の card を用意した（今は English だけ）。

## 設計（案）

- **D1 口**（libkeiland、Linux・FreeBSD も同じ）:
  - `const char *kl_tr(const char *english)`: 今の言語の訳を返す。訳が無ければ英語のまま（英語が基準）。
  - `const char *kl_trc(const char *context, const char *english)`: 同じ英語が場所で訳し分けられる時（「Open」の動詞と形容詞など）。
  - `const char *kl_trn(const char *singular, const char *plural, unsigned long n)`: 数で形が変わる文（英語の 1 件・n 件。日本語は 1 つの形）。
  - `int kl_tr_format(char *out, size_t size, const char *template, ...)`: `{1}`・`{2}` の位置で文字列を差し込む（語順の違いのため。数は呼ぶ側が文字列にする）。
  - 関数の形の macro は使わない（全文の規約）。key は英語の文そのもの（gettext と同じ考え。source の文がそのまま key で、抽出が容易）。
- **D2 catalog**（独自、Zlib）: 言語・domain ごとの UTF-8 の text file `share/keiland/locale/<言語>/<domain>.tr`。1 行に `英語<TAB>訳`（context は `context<0x04>英語`、複数形は `単数<0x00>複数` を key に）、`\t`・`\n`・`\\` の escape、`#` の行は注釈。読み込みの時に hash の表にする。domain は app ごと（`files`・`settings`・`wayland` …）と共通の `keiland`（menu の Cut・Copy など）。
- **D3 抽出の道具**（`plan/tools/i18n/` か `tools/i18n/`）: source から `kl_tr("…")`・`kl_trc`・`kl_trn` の文を集め、domain の catalog の雛形（英語の key と空の訳、場所の注釈）を作り、今の catalog と突き合わせて、足りない・使われない key を出す。
- **D4 言語の選択と通知**（2026-10-05 ユーザーの方針）:
  - settings の key `ui.language`（compositor、`en`・`ja`、既定 `en`）。Languages の頁の「Display language」で選ぶ。
  - Keiland の app は libkeiland が `kl_settings_watch("ui.language")` で変更を受け、catalog を読み直し、app に「描き直せ」の callback を出す（`kl_app` の再描画）。logout も app の再起動も要らない。compositor・system bar・menu も同じく key の変更で読み直す。
  - 他の toolkit の app: sessiond の `session.sh` が session の起動の時に `ui.language` から `LANG`・`LANGUAGE`・`LC_MESSAGES` を作る（logout・login で反映、最低線）。compositor が app を起動する時（App Home・Files・launcher）にその時点の言語の環境変数を渡す（app の再起動で反映）。Linux・FreeBSD では OS の locale の名前（`ja_JP.UTF-8`）にする。
- **D5 書式**: 日付・時刻・数の書式を libkeiland に（`kl_tr_date`・`kl_tr_time`）。system bar の時計（英語 `Mon Oct 5 00:36` → 日本語 `10月5日(月) 0:36`）、Files の日時の列など。
- **D6 layout**: 日本語で文が短く・長くなる所は、今の `fm_text_draw_fit`（幅に合わせて切る）で崩れない。固定幅の button などは `se_button_width` のように文から幅を出す所が多い。p004 で app ごとに PNG で確かめる。
- **D7 greeter・lock**: login の前の言語は system の既定（`/etc/keiland/locale` の 1 行、install の時と Settings の管理者の操作で書く案）。lock の画面は session の言語。

## Phase（ws.md の案を保つ）

p002 口と catalog の読み込み・抽出の道具（host の試験）／p003 compositor・greeter・lock／p004 各 app／p005 日本語の訳と用語集／p006 規約・回帰・UAT。

## ユーザーの判断が要る点（Q1 経由）

1. key を英語の文にする（gettext と同じ）で良いか。代わりは ID（`files.menu.open` など）で、訳し忘れが見つけやすいが source が読みにくくなる。
2. catalog は独自の text の形式（Zlib）で良いか。gettext の `.po` と互換にすると既存の翻訳の道具が使えるが、parser が大きくなる。
3. ベータ2 の言語は英語と日本語の 2 つで良いか。
4. login の前（greeter）の言語を system の既定として持ち、管理者（wheel）だけが Settings で変える、で良いか。
5. ws089-p015（Settings の日本語の UI）と ws127-p005（Files の日本語の UI）は、この WS の p004・p005 に吸収して、元の Phase は canceled（吸収）にする、で良いか。
