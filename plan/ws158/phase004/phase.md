<!-- awesome-plan project=zedbsd record=ws158-p004 -->

# ws158-p004: 各 app の文を翻訳の口に通す（Settings・Files から）と、言語の選択の UI

Status: in-progress（2026-10-06 P2、途中でラップアップ。下の「再開の地点」）
Disposition: normal
Parent: [WS158](../ws.md)
Queue: Q1 の指示（2026-10-06「WS158 p004: Settings の Languages の頁に Display language の選択（ui.language）、管理者の system の言語（/etc/keiland/language、既存の admin-helper の形）、標準の app を kl_tr_follow で（Settings 自身と Files から）」）
依存: [p002](../phase002/phase.md)（kl_tr_*）、[p003](../phase003/phase.md)（compositor の language.c が /etc/keiland/language を読む）

## 範囲

1. Settings の Languages の頁: 「Display language」で English・日本語 を選ぶ（`ui.language`、すぐ切り替わる）。
2. 管理者が system の言語（login の画面の言語、`/etc/keiland/language`）を変える口: account-admin（set-user-ID root の既存の道具、
   `kl_system_account_administer` を compositor が中継）に操作 `system-language` を足し、Settings の Languages の頁に管理者だけの card（English・日本語、
   自分の password、Apply）。
3. Settings と Files の文を `kl_tr` に通し、`kl_tr_follow` で言語の変更に追う。catalog `userland/desktop/locale/ja/settings.tr`・`files.tr`。

## 済んだ物（2026-10-06 P2）

- **account-admin**（`userland/base/account-admin/`）: 操作 `system-language`（引数 1 つ `en` か `ja`、`admin_language_valid()`、request の name に持つ）。
  呼び手の確かめ（root でない・wheel・password、間違いは 2 秒）は他の操作と同じ。適用は account の file の lock を取らず、`/etc/keiland` が無ければ作り
  （0755）、`/etc/keiland/language` を `account_file_write()` で atomic に 0644 で書く（`admin_system_language()`）。syslog は「system-language by NAME for ja」。
  compositor・backend（`account-zedbsd.c`）は操作の行をそのまま渡すので変更なし。
- 試験: `plan/ws158/tests/run-host-admin-language.sh` → `host-admin-language: ok (13 checks)`（ASan・UBSan。ja・en、拒否: 知らない言語・空・無し・2 つ・
  password 無し・path、他の操作は前のまま）。前からの `plan/ws089/tests/run-host-account-admin.sh` → 34 checks, 0 failed。
- **Settings**（途中）: `settings.h` に `struct se_languages`（system の言語・選んだ言語・password の field・keyboard・依頼・message）と `se_app.languages`、
  `se_look.ui_language`、宣言 `se_languages_key`・`se_languages_result`・`se_users_load`。`look.c` は `ui.language` を読み、`kl_tr_follow(settings, "settings", …)`
  で言語の変更の度に描き直す（log `LOOK language=ja`）。`page-users.c` に `se_users_load()`（管理者かどうかを Users の頁の前に知るため）。
- build: zedBSD の `bin/settings`（`config-amd64-zdesktop.mk`、BUILD=build/p2-b194）rc 0・warning 0、account-admin の main.c・edit.c は target の clang で
  -Werror の compile 0 warning。style-check 0。

## 再開の地点（ラップアップ 2026-10-06）

1. `page-languages.c`: Display language の card（English・日本語 の 2 つの toggle、index 4・5、`se_look_set_number(app, "ui.language", n, 0)`、
   言語の名前はその言語で（「English」「日本語」）訳さない）。管理者の card（`se_users_load` の後に `se_users_admin_available` で出す）: 今の system の言語
   （`KEILAND_SYSCONFDIR "/keiland/language"` を読む）、English・日本語の toggle（index 6・7）、password の field（index 8、page-users.c の
   `users_field_draw` に倣う）、Apply（index 9、`kl_system_account_administer(system, password, "system-language\nja\n", &request)`、
   送った後に field を wipe）、message の行。`se_languages_key`（field の文字・Enter で Apply・Esc で wipe）と `se_languages_result`（ok で
   system の言語を読み直す、拒否は `kl_system_account_refusal` の語を文に）を書き、`pages.c` の Languages の行に key の callback、`system.c` の
   result の連鎖に足す。
2. 文を `kl_tr` に: Languages の頁の全部、`ui.c` の sidebar の page の名前、`widgets.c` の頁の見出しと要約、`page-home.c` の tile と group の名前。
   menu（`menu.c`、compositor に送る）は言語の変更で menu を送り直す仕組みが要るので後（残り）。
3. catalog `userland/desktop/locale/ja/settings.tr` を `tools/i18n/tr.py extract/update` で作り訳す。`tr-host-test.sh` の `check --strict` に足す。
4. Files: `kl_tr_follow(…, "files", …)`、sidebar・menu・空の folder の文から。`ja/files.tr`。
5. T1: Settings の Languages で日本語を選び、Settings と system bar が日本語になる PNG。管理者の card で system の言語を ja にし、
   logout → greeter が日本語（session の管理者が要るので UAT か AAT の image）。
