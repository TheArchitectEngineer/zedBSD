<!-- awesome-plan project=zedbsd record=ws158-p002 -->

# ws158-p002: libkeiland の翻訳の口と catalog の読み込み、catalog の道具

Status: in-progress（2026-10-05 夜、P2 g17。実装と host の試験まで。target での確かめは口を使う p003・p004 で）
Disposition: normal
Parent: [WS158](../ws.md)
Queue: Q1 の指示（2026-10-05 夜「WS158 p002（決定 ①〜⑤ を phase.md に記録してから、独自の UTF-8 catalog と英語・日本語）」）
依存: [p001](../phase001/phase.md)（cleared）

## 範囲

p001 の D1（口）・D2（catalog）・D3（道具）・D4 の一部（言語の設定と、走っている app の切り替え）。英語（source の文）と日本語（`ja`）の 2 つ（判断 ③）。compositor・greeter・lock の文を口に通すのは p003、各 app は p004、日本語の訳は p005。

## 作った物

- **口**（`userland/desktop/keiland/keiland.h`、`KL_VERSION` 37）:
  - `kl_tr(english)`・`kl_trc(context, english)`・`kl_trn(singular, plural, count)`: 今の言語の文。catalog に無ければ英語のまま（英語が基準、判断 ①）。
  - `kl_tr_format(out, size, pattern, ...)`: `{1}`〜`{9}` を NULL で終わる文字列の引数で埋める（語順の違い）。`{{` は `{`。切れたら `ERANGE`、引数の無い place は `EINVAL`。
  - `kl_tr_open(domain, language)`・`kl_tr_open_directory(directory, domain, language)`・`kl_tr_close()`・`kl_tr_language()`・`kl_tr_language_code(setting)`（0 `en`、1 `ja`）。
  - `kl_tr_follow(settings, domain, changed, data)`: desktop の設定 `ui.language` を読み、変わる度に catalog を読み直して `changed` を呼ぶ（logout も app の再起動も要らない、2026-10-05 ユーザーの方針）。
  - 実装 `userland/desktop/libkeiland/translate.c`（catalog の読み込みと引き、libc だけ）・`translate-follow.c`（設定の watch）。zedBSD・Linux・FreeBSD の Makefile と `exports.map`（`exports.py`）に足した。
- **catalog**（独自の UTF-8 の text、Zlib、判断 ②）: `LANGUAGE/DOMAIN.tr`。行は `msg ENGLISH TEXT`・`ctx CONTEXT ENGLISH TEXT`・`plural SINGULAR PLURAL FORM...`（TAB 区切り、`\t`・`\n`・`\\`、`#` は注釈、CRLF も可）。訳の空の行は未訳（英語が出る）。置き場所は install で `KEILAND_DATADIR/keiland/locale/`（zedBSD は `/usr/share/keiland/locale/`）、tree では `userland/desktop/locale/<言語>/<domain>.tr`（最初の catalog と install の規則は p003）。app の domain の catalog を先に、共通の `keiland` を後に引く。同じ key が 2 度あれば後の行が勝つ。複数形の規則は日本語 1 形、英語と知らない言語は単数・複数。
- **設定**: `ui.language`（compositor の key、INT 0〜1、既定 0）を `settings-keys.c` の表に足した。compositor の側の切り替え（system bar・menu）は p003。
- **道具** `tools/i18n/tr.py`: `extract SOURCE...`（`kl_tr`・`kl_trc`・`kl_trn` の string literal を集めて空の catalog、隣り合う literal の連結・escape・注釈の中を除く・literal でない呼び出しを報告）、`update CATALOG SOURCE...`（訳を保ち、新しい文を空で足し、使われなくなった文を注釈で残す。冒頭の注釈を保つ）、`check CATALOG [SOURCE...] [--strict]`（行の形、`{N}` の place が英語と同じか、重複、source との過不足。`--strict` で未訳も失敗）。
- **試験** `plan/ws158/tests/tr-host-test.sh`（`tr-host-test.c` を `-std=c89 -pedantic -Werror` で translate.c と build、`tr-tool-test.sh` も呼ぶ）。

## 判断（P2）

- `ui.language` は INT（0・1）にした。settings の型に列挙の文字列が無く、`ime.method` と同じ形で済む。言語を足す時は上限と `kl_tr_language_code` の表を広げる。
- 返す文の寿命は、言語か domain が変わるまで（`kl_tr_follow` の `changed` の後は前の文を使わない）。1 つの thread（libkeiland の窓の thread）だけが使う。
- 失敗（名前の拒否・`ENOMEM`・大きすぎる catalog）の後はすべて英語。catalog が無い言語は英語（エラーにしない）。
- 道具は `tools/i18n/`（build の道具と同じ所。product の source の一部として使い続ける）。

## 確かめ（host）

- `sh plan/ws158/tests/tr-host-test.sh` → `tr-host-test: PASS`（口 33 項目: 英語、catalog の無い言語、msg・ctx・plural、共通の domain と自分の domain の順、escape、CRLF、壊れた行、後の行、format の place の順・`{{`・切れ・place の欠け、名前の拒否の後の英語、close。道具 15 項目）。
- build: zedBSD の `dynamic/libkeiland.so`・`bin/settings`・`bin/wayland`（`config-amd64-zdesktop.mk`、BUILD=build/p2-b194）と `make keiland-linux` は rc 0・warning 0。`exports.py --check` OK。style-check 0（translate.c・translate-follow.c・試験）。`git diff --check` OK。
- settings の host の試験: `plan/tools/settings/host-settings.sh` 38 passed、`host-store.sh` 43 passed（key を足した影響なし）。
- `tools/i18n/tr.py extract userland/desktop/libkeiland userland/desktop/keiland`: literal でない呼び出しは translate.c の中の 1 つだけ（`kl_trc` が context 無しで `kl_tr` に回す所）。

## 未実施

- target（QEMU・実機）: まだ口を使う app が無い。p003・p004 で文を口に通した時に、英語・日本語の PNG で確かめる（T1）。
- FreeBSD の build（Makefile.freebsd に足しただけ、この host では build しない）。
- 日付・時刻の書式（D5）、session の環境変数（D4 の他の toolkit）、greeter の言語（D7）は p003 以降。
