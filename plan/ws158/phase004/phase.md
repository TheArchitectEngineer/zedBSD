<!-- awesome-plan project=zedbsd record=ws158-p004 -->

# ws158-p004: 各 app の文を翻訳の口に通す（Settings・Files から）と、言語の選択の UI

Status: test-wait（T1 依頼中。2026-10-06 q805 P2: 再開の地点の 1〜4 を実装、末尾。以前: in-progress、途中でラップアップ）
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

## 2026-10-06 q805 P2: 再開の地点の 1〜4

- **Languages の頁**（`page-languages.c`）: Display language の card（English・日本語。どちらもその言語の名前で、訳さない。control 4・5、`ui.language` を `se_look_set_number` で、log `LANGUAGES ui language=en|ja`）。管理者だけに Login screen の card を出す（`se_users_load` → `se_users_admin_available`）。今の system の言語（`KEILAND_SYSCONFDIR/keiland/language` を読む、log `LANGUAGES system language=en|ja|none`）、English・日本語（control 6・7）、password の欄（control 8、点で表示）、Apply（control 9、`kl_system_account_administer(…, "system-language\nja\n", …)`、送った後に欄を wipe）、答えの行。`se_languages_key`（Enter で Apply、Esc で欄を空に、空なら keyboard を返す）と `se_languages_result`（ok なら file を読み直す、拒否は account-admin の語 not-administrator・bad-password・busy を文に）。`pages.c` に key の callback、`system.c` の result の連鎖に足した。`settings.h` の `struct se_languages` に `system_read`。
- **Settings の文を kl_tr に**: Languages の頁の全部、sidebar の頁の名前（`ui.c`）、titlebar の breadcrumb（Settings・Search・頁の名前）、頁の見出しと要約（`widgets.c`）、Home の tile と group の名前（`page-home.c`）、検索の結果の行（`search.c`）。表の中の文（`se_pages` の名前と要約、入力方式の選択肢、Search Results）は `userland/desktop/locale/settings.keys` に並べ、tr.py が読む。
- **catalog**: `userland/desktop/locale/ja/settings.tr`（80 件、全部訳した）、`ja/files.tr`（24 件）。`plan/ws158/tests/tr-host-test.sh` の `check --strict` に settings・files を足した。
- **Files**（`main.c`）: window の時（desktop の時は除く）に `kl_settings_open(display, NULL)` と `kl_tr_follow(…, "files", …)`、loop の各回に `kl_settings_dispatch`、変わったら描き直す（log `ZFILES LANGUAGE language=…`）。文: sidebar の section と場所の名前（`ui.c`、表の文は `locale/files.keys`）、breadcrumb の Home・Computer、list の列の見出し（`ui-list.c` の `list_title`）、空の folder・開けない folder の文（`ui-grid.c`）。`plan/tools/files/host-build.sh` に `translate.c` を足した（host の files-render・files-model が kl_tr を link する）。
- **AAT**: `apps.settings.display-language`（日本語を選び、Settings と Files を撮って English に戻す。needs-person）、`apps.settings.login-language`（管理者が ja、次に en に変え、file を確かめる）。helper は `helpers_apps.py`。
- 確認: zedBSD の settings・files の build warning 0、keiland-linux の build warning 0、style-check（変えた file の新しい指摘 0。settings の ui.c などに前からある指摘 10 は他の Phase の物で触っていない）、tr-host-test PASS（catalog 3 つ ok）、tr.py check --strict（settings 80/80、files 24/24）、host の settings-render の build、files-render・files-model の build と files-model PASS、aat run-host PASS、check-scenarios PASS。QEMU は T1（未実施）。
- 残り（この Phase の外に出すかは Q1）: menu（`menu.c`、compositor に送る）の言語の追従。検索の要約の文（「N results for …」は複数形の kl_trn が要る）。Files の message（`fm_ui_message` の 19 件）と context menu。Settings のほかの頁の本文（Network・Users などの card の中の文）。
- 2026-10-06 Q1 の決定: 上の残り（menu の言語の追従・検索の要約の複数形・Files の message 19 件と context menu・Settings の他の頁の本文）は p004 の外に **p004b** として分け、ベータ2 の後の順で行う。p004 の受け入れはこの節までの範囲（T1-239）。

## 2026-10-06 q809 P2: T1-207・T1-239 の FAIL

- T1-207（`tr-p003-guest.sh` の「`ZWL LOCK locked` MISSING」）: 回帰ではない。zdesktop は sessiond が始めた session だけを lock する（解く者がいないため、greeter.c の `zwl_lock` が `kl_backend_session_managed` を見る）。この試験は zdesktop を sessiond 無しで単独に起動するので、Super+L は lock しない。→ 段 6 を試験から除き、AAT のシナリオ `desktop.language.lock-japanese`（`keiland-settings set ui.language 1` → Super+L → 撮影 → 解除 → 英語に戻す）に移した。AAT の image（`plan/tools/aat/config-amd64-aat.mk`）に `keiland-settings` を足した。
- T1-239 login-language の「not an administrator」: helper の誤り。Login screen の card は出ていた（control 6・7・8 が log にある）が、Apply（9）は変更と password がそろうまで click を受けない（hit を出さない）ので、helper が 9 を探して落ちた。→ password の欄（8）で card を確かめ、password と Enter で Apply する（helpers_apps.py）。
- T1-239 display-language の英語の残り: Settings の検索の欄の placeholder（「Search settings」）、Files の検索の欄（「Search」）と path の欄、breadcrumb・tab・preview・Today の頁の場所の名前、Today の頁の本体（挨拶・今日開いたファイルの数と空き・Folders・Recent Files・Recent Folders・Show all・空の案内・folder の card の名前と項目の数）。p004 の範囲（Settings と Files の文）なので直した: `kl_tr`・`kl_trn`（複数形）・`kl_tr_format`（`{1}` の place）。catalog は files 40 件、settings 81 件、全部訳した。window の題（「Settings」「Files」、app の名前）は app を作る時に決まり、言語の変更で送り直す仕組みが要るので p004b に残す。
- 確認: zedBSD の files・settings と keiland-linux の build warning 0、style-check 0、tr-host-test PASS（catalog 3 つ）、files の host（files-render・files-model）PASS、check-scenarios PASS。QEMU は T1（未実施）。
