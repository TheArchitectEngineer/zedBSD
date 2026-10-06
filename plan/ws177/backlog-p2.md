# WS177 積み残しの一覧（P2）

[WS177](ws.md) の「集め方」に従い、P2 が 1 行ずつ足す。

| 元の WS・Phase | 分岐（準正常系・異常系） | 期待の動き | source の所在 | 書いた日 |
| --- | --- | --- | --- | --- |
| WS175 ws175-p008（Notes の文字の UI） | box を開いたまま指で zoom・scroll、窓の大きさを変える | box が頁の物に付いて動く（今は開いた時の窓の位置に留まる） | `main.c` の `app_box_place`・`app_box_new`（rect は開く時だけ計算） | 2026-10-06 |
| WS175 ws175-p008（Notes の文字の UI） | 指の tap で box の中の caret を動かす、指で box の外を tap して確定 | 指も pointer と同じに box を扱う（今は指の入力を box に渡さず、外の tap で確定もしない） | `window.c` の `window_event`（TOUCH_* を box の queue に入れない）、`main.c` の `app_touch` | 2026-10-06 |
| WS175 ws175-p008（Notes の文字の UI） | box の中の Ctrl+C・X・V、Ctrl+←→（語）、box の中の undo | clipboard と語の移動、打鍵ごとの undo（今は libkeiland の field・text area に無い。Ctrl+Z は開いた時の文字に戻す） | libkeiland の `field.c`・`text-area.c`、`main.c` の `app_action`（UNDO の box の分岐） | 2026-10-06 |
| WS175 ws175-p008（Notes の文字の UI） | System Menu の無い compositor で box を開いている時の Ctrl+S・Ctrl+W | 保存・閉じる（今は box が全部の key を取り、Notes の shortcut に届かない） | `window.c` の `window_event`（box_open の時 KEY を box だけに） | 2026-10-06 |
| WS175 ws175-p008（Notes の文字の UI） | 回転した頁・回転した文字の行を box で編集 | caret と box を文字の向きに合わせる [L8]（今は常に横の field を行の下に出す） | `main.c` の `app_box_edit`・`app_box_place` | 2026-10-06 |
| WS175 ws175-p008（Notes の文字の UI） | 長い行（512 byte 超）の編集 | 全部の文字を編集できる（kl_field の上限 512 byte で切れる） | `box.c` の `notes_box_open`（`kl_field_set`） | 2026-10-06 |
| WS175 ws175-p008（Notes の文字の UI） | box の font が無い（`keiland.ttf` の欠落） | 代わりの font で box を出す（今は status「its font is missing」で開かない） | `box.c` の `notes_box_open` | 2026-10-06 |
| WS175 ws175-p008（Notes の文字の UI） | 置き換えの font の無い system で既存の行を元の font に無い字で編集 | 理由を出して box を残す（今は ENOTSUP で status を出す。画面の確認は未実施） | `main.c` の `app_box_try` | 2026-10-06 |
| WS175 ws175-p008（Notes の文字の UI） | [M8] 指の long-press の drag | 選んだ物の移動・大きさ（画像の段からの残り、今は未実装） | `touch.c`・`main.c` | 2026-10-06 |
| WS175 ws175-p008（Notes の文字の UI） | [N13] 表示中の頁の前後の外の editor を捨てる、[M13] autosave の cache と時間 | 大きな文書で memory と時間を抑える（今は各頁の editor を残す） | `edit.c` の `notes_page_editor`・`main.c` | 2026-10-06 |
| WS128 ws128-p004（PDF Viewer の検索と選択） | 文書の form XObject の中の文字、注釈（/Annots）の文字 | 検索・選択の対象にする（今は頁の top level の content だけ） | libpdf `editor.c` の `pdf_page_text_open`（editor の scan の範囲） | 2026-10-06 |
| WS128 ws128-p004（PDF Viewer の検索と選択） | 行をまたぐ語・ハイフンで切れた語、空白の数の違い | 行の終わりを空白として一致させる、連続の空白を 1 つとして一致させる（今は文字の並びがそのまま一致する所だけ） | `pdfviewer/find.c` の `find_match` | 2026-10-06 |
| WS128 ws128-p004（PDF Viewer の検索と選択） | ASCII 以外の大文字・小文字、全角・半角、濁点の合成など | Unicode の case folding と正規化で一致させる（今は ASCII の A〜Z だけ） | `find.c` の `find_fold` | 2026-10-06 |
| WS128 ws128-p004（PDF Viewer の検索と選択） | 頁をまたぐ選択、ダブルクリックで語・トリプルで行の選択、Ctrl+A | 複数頁と語・行の単位の選択（今は 1 頁の中の drag だけ） | `find.c` の `pv_select_button`・`pv_select_motion` | 2026-10-06 |
| WS128 ws128-p004（PDF Viewer の検索と選択） | 指（touch）での選択と handle | 長押しで選択、handle で範囲の変更（今は pointer だけ。指は scroll のまま） | `pdfviewer/touch.c`、`find.c` | 2026-10-06 |
| WS128 ws128-p004（PDF Viewer の検索と選択） | 文字の多い頁・大きな文書の検索の速さ | 頁の文字の読みを背景で・一致の数の表示（今は表示の頁から順に同期で読み、一致の数は出さない） | `find.c` の `pv_find_next`・`find_page_text` | 2026-10-06 |
| WS128 ws128-p004（PDF Viewer の検索と選択） | titlebar の無い compositor（System Menu・titlebar の無い環境） | window の中の検索の欄で探せる（今は titlebar の field だけ。Ctrl+F は何もしない） | `pdfviewer/titlebar.c` の `pv_titlebar_focus_find` | 2026-10-06 |
| WS128 ws128-p004（PDF Viewer の検索と選択） | 縦書き・回転した文字の選択の塗り | 文字の四隅の向きのまま塗る（今は四隅を囲む軸に沿った箱） | `find.c` の `find_mark` | 2026-10-06 |
| WS128 ws128-p004（PDF Viewer の検索と選択） | ToUnicode の無い・壊れた font の文字（U+FFFD） | 読めない字を検索・copy で知らせる（今は U+FFFD がそのまま copy される） | libpdf `pdf_font_unicode`、`find.c` の `pv_select_copy` | 2026-10-06 |
| WS164 ws164-p002（Welcome） | 設定の store が書けない（Keiland の拡張の無い desktop、書き込みの失敗） | Welcome の終わりで印を付けられないことを画面に出し、次の login でまた出ることを知らせる（今は log だけで窓を閉じる） | `settings/welcome.c` の `welcome_finish` | 2026-10-06 |
| WS164 ws164-p002（Welcome） | Files の起動の失敗 | Today を開けなかったことを知らせる（今は log だけ） | `settings/main.c` の `main_open_files` | 2026-10-06 |
| WS164 ws164-p002（Welcome） | Welcome の途中で Wi-Fi の鍵の入力・接続の失敗、radio の無い machine で Ethernet も無い | 段の中で失敗と次の手を出す（今は Wi-Fi・Ethernet の頁の表示のまま） | `settings/welcome.c` の Network の段 | 2026-10-06 |
| WS164 ws164-p002（Welcome） | 言語と入力の段（WS154 の Languages） | p001 の H3 のとおり Look と Keys の間に足す（今は 5 段のまま） | `settings/welcome.c` | 2026-10-06 |
| WS164 ws164-p002（Welcome） | Welcome の窓の大きさが小さい・日本語の UI の長い文 | 帯と Skip が重ならないよう詰める・文を折り返す（今は固定の配置） | `settings/welcome.c` の `se_welcome_bar`・`se_welcome_draw` | 2026-10-06 |
| WS164 ws164-p002（Welcome） | Welcome の key の操作（Enter で Next、Esc で閉じる） | keyboard だけで段を進める（今は pointer と指の click だけ） | `settings/ui.c` の `ui_key`、`welcome.c` | 2026-10-06 |
| WS156 ws156-p002（通知の口） | 通知を出した client が切れた・notify の object を destroy した | その client の待ち・表示中の通知を ACTION 無しに変える（今は model に残り、closed は誰にも送られない） | `wayland/notify-shell.c` の `notify_tell_closed`、client の破棄の側 | 2026-10-06 |
| WS156 ws156-p002（通知の口） | app の名前が空の通知 | client の window の app_id を名前にする（今は空のまま保つ。p003 の描画で決める） | `wayland/notify-shell.c` の `notify_post` | 2026-10-06 |
| WS156 ws156-p002（通知の口） | 不正な UTF-8 の題・本文、制御文字 | 置き換えるか拒む（今は bytes のまま保つ） | `wayland/notify.c` の `kwl_notify_post` | 2026-10-06 |
| WS156 ws156-p002（通知の口） | 一つの client が短い間に大量に post する | 速さの制限（今は client ごと 32 個の上限だけ） | `wayland/notify-shell.c` の `notify_post` | 2026-10-06 |
| WS156 ws156-p002（通知の口） | libkeiland の事象の ring（32）が溢れる | 古い事象を捨てたことを app に知らせる（今は黙って捨てる） | `libkeiland/system/system-view.c` の `system_view_notify_event` | 2026-10-06 |
| q824 ws148-p002（Privacy の頁を無くし、最近の履歴の口） | 「Keep recent items」を off にしている間の Files の Recents | Recents に「最近の項目を残さない設定です」と出し、Settings への道を示す（今は空の一覧だけ） | `files/ui-grid.c` の題、`files/ui-search.c` の Recents の読み | 2026-10-06 |
| q824 ws148-p002 | Clear Recents の確かめ | 押し間違いに備えて確かめるか、元に戻す（今は押すとすぐ空になる） | `files/actions.c` の `fm_action_clear_recents` | 2026-10-06 |
| q824 ws148-p002 | 他の app が開いていた「最近の file」の menu | 一覧が空・止められた時に、開いている app の menu も読み直す（今は各 app が次に読む時まで古い） | libkeiland `recent.c`、各 app の open recent | 2026-10-06 |
| q824 ws148-p002 | Storage の頁の文と Files の Clear Recents の日本語 | 翻訳の catalog に入れる（今は Storage の頁の本文と Files の題の button は翻訳されない、既存の Trash などと同じ） | `settings/page-storage.c`、`files/ui-grid.c`、`locale/ja/*.tr` | 2026-10-06 |
| q826 ws128-p004（PDF の検索） | Enter の後に field へ戻した時の選択 | caret を末尾に置き、続けて打つと query に足される（今は compositor が query 全体を選んで戻すので、打つと置き換わる） | `wayland/titlebar-shell.c` の `shell_focus`、`pdfviewer/titlebar.c` の `pv_titlebar_input` | 2026-10-06 |
| WS161 ws161-p004（libpasskey の os 層と fidoctl） | report に番号の付いた FIDO の鍵、64 byte でない report の鍵 | ID の byte を外して読む・report の大きさに合わせる（今は開かない・EIO） | `libpasskey/os-zedbsd.c` の `pk_os_open`、`os-posix.c` の `os_read` | 2026-10-06 |
| WS161 ws161-p004 | 鍵が 2 本以上ある時の fidoctl | どれかを選ばせる・Selection（触った鍵）で決める（今は `-d` が無ければ一覧の最初） | `fidoctl/main.c` の `fidoctl_open`、`pk_ctap2_selection` | 2026-10-06 |
| WS161 ws161-p004 | 使っている途中で鍵が抜かれた | 抜かれたことを言って終わる（今は read の ENODEV・EIO をそのまま出す） | `libpasskey/os-posix.c` の `os_read` | 2026-10-06 |
| WS161 ws161-p004 | PIN の入力 | 端末では echo を切って読む・PIN の長さの規則（4〜63 byte）を先に確かめる（今は標準入力の 1 行をそのまま） | `fidoctl/main.c` の `fidoctl_read_pin` | 2026-10-06 |
| WS161 ws161-p004 | 鍵の reset、resident の credential の一覧と削除 | `fidoctl reset`・`credentials`（今は無い） | `fidoctl/main.c`、`ctap2.c` | 2026-10-06 |
| WS172 ws172-p003（passkey-fido2） | 複数の鍵が同じ account の credential を持つ | 触れた鍵を選ぶ（selection 0x0B、2.0 では UP だけの GetAssertion）（今は最初に見つかった鍵） | `passkey-fido2/helper.c` の `helper_assert` | 2026-10-06 |
| WS172 ws172-p003 | 試行の途中に挿した・かざした鍵 | 途中で現れた鍵にも問う（今は開始の時の鍵だけ） | `passkey-fido2/device.c` の `fido2_devices_open`、`helper.c` | 2026-10-06 |
| WS172 ws172-p003 | CANCEL・親の終わり | helper が親の pipe の EOF で鍵に CANCEL を送って終わる（今は sessiond の TERM・KILL と helper の alarm だけ） | `passkey-fido2/helper.c` | 2026-10-06 |
| WS172 ws172-p003 | 内蔵の UV（指紋）だけの鍵、PIN の protocol の細部（maxCredentialCountInList、PIN の長さの最小） | UV の鍵を PIN 無しで使う・allowList を鍵の上限で分ける（今は PIN が設定された鍵だけ、allowList は 5 本まで一度に） | `helper.c` の `helper_token`・`helper_assert` | 2026-10-06 |
| WS172 ws172-p003 | release の image の loopback の鍵 | release の passkey-fido2 は `HIDRAW_BUS_VIRTUAL` の鍵を拒む（設計 P5、今は区別しない） | `passkey-fido2/device.c` | 2026-10-06 |
| WS172 ws172-p003 | helper が process を作れないこと | zedBSD に RLIMIT_NPROC か同等の制限を入れて helper に掛ける（今は空の root で exec の program が無いことだけ） | `passkey-fido2/helper.c` の `helper_sandbox`、kernel | 2026-10-06 |
| WS172 ws172-p003 | label の長さ | 32 文字（UTF-8 の文字）で数える（今は 32 byte） | `passkey-fido2/wire.c` の `fido2_label_valid` | 2026-10-06 |
| WS172 ws172-p003 | 壊れた鍵の行 | 一度だけ log に出す（今は黙って飛ばす） | `passkey-fido2/main.c` の `main_keys` | 2026-10-06 |
| WS165 ws165-p002（手書きの照合） | 書いた線が 1 点だけ・極端に少ない | 認識しない・候補を出さないと伝える（今は 1 点の雲で何かを候補にする） | `wayland/hand-cloud.c` の `hand_cloud_make` | 2026-10-06 |
| WS165 ws165-p002 | templates の file が無い・壊れている | 手書きの面に「認識の data が無い」と出す（今は読みの失敗で template 0 個） | p003 の compositor の読み、`hand_templates_parse` | 2026-10-06 |
| WS165 ws165-p002 | Hershey の太さの重ね線が濁点の位置に来る字（ほ・ぼ） | 手本から重ね線を除く（変換の時に近い平行の線をまとめる） | `packages/fonts/hand-hershey/convert.py` | 2026-10-06 |
| WS165 ws165-p002 | 濁点・半濁点の位置が本体の中・左上に書かれた | 位置に依らず小さな印を探す（今は右上の 45% の範囲） | `wayland/hand-cloud.c` の `strokes_mark` | 2026-10-06 |
| WS165 ws165-p003（compositor の手書き） | templates の読みが重い・大きい file | 起動の時ではなく別の thread で読む・最初の認識が遅れないようにする（今は最初の認識の時に event loop で読む、228 字で数 ms） | `wayland/keyboard-hand.c` の `kwl_hand_load` | 2026-10-06 |
| WS165 ws165-p003 | ink が 8,192 点を越える | 点を間引いて全部の線を使う（今は越えた点を捨てる） | `wayland/keyboard-hand.c` の `kwl_hand_recognize` | 2026-10-06 |
| WS165 ws165-p003 | 「No handwriting data」の note | 日本語の UI で訳す（今は英語のまま） | `wayland/keyboard-hand.c`、`locale/ja/wayland.tr` | 2026-10-06 |
| WS175 ws175-p009（Save Clean Copy） | 刈り込みは page の resource だけ | form XObject・Type 3 font・tiling pattern の中の /Resources も使う名で刈り込む | `libpdf/clean.c` の `clean_write_resources` | 2026-10-06 |
| WS175 ws175-p009 | attachment を落とした name tree の /Limits が古いまま、直接の file specification は落ちない | 葉の /Limits を書き直す・直接の filespec の対も落とす | `libpdf/clean.c` の `clean_drop_names`・`clean_write_value` | 2026-10-06 |
| WS175 ws175-p009 | Save Clean Copy の後の案内が無い | copy を開くかの提案、増分の更新で消した物が残る旨の一度だけの注意（D1 (a)） | `notes/main.c` の `app_save_clean` | 2026-10-06 |
| WS169 ws169-p002（compositor のメールの口） | 17 個目の読み手の listen | 古い・死んだ行を先に掃除して受け入れる（今は 16 行が埋まると BUSY、死んだ行は次の arrived で空く） | `wayland/mail-shell.c` の `mail_listen` | 2026-10-06 |
| WS169 ws169-p002 | 読み手の許可の変化 | 許可が on・off に変わったことを読み手に知らせる（今は何も送らず、arrived の時に設定を読むだけ） | `wayland/mail-shell.c`、`kl-system-protocol.h` | 2026-10-06 |
| WS169 ws169-p002 | 許可の UI の置き場 | Settings の Notifications の頁（今は「later」）ができたら mail.codes.* の switch をそちらにも出す（今は Mail の app の中だけ） | `settings/pages.c`、`mailer/` | 2026-10-06 |
| WS169 ws169-p002 | mail を出せるのは同じ uid の誰でも | arrived を送れる client を Mail に限る（今は system manager の見える client なら誰でも arrived を送れる） | `wayland/mail-shell.c` の `mail_arrived` | 2026-10-06 |
| WS169 ws169-p003（IMAP・SMTP の backend） | ISO-2022-JP・Shift_JIS・EUC-JP の本文と件名 | UTF-8 に変換して出す（今は bytes のまま、文字化けする） | `mailer/mime.c` の `mime_to_utf8` | 2026-10-07 |
| WS169 ws169-p003 | 1 MiB を超えるメール | 本文の部分だけを取る（BODYSTRUCTURE と BODY.PEEK[1]）、添付の大きさを正しく（今は先頭 1 MiB を取り、添付の大きさは encode の大きさからの見積もり） | `mailer/imap.c` の `ml_imap_fetch`、`mime.c` | 2026-10-07 |
| WS169 ws169-p003 | 証明書の検証の失敗・TLS の無い server・接続の timeout・server の BYE | 理由を画面に出し、自己署名を許すかを聞く（今は error の文だけ） | `mailer/tls.c`・`conn.c` | 2026-10-07 |
| WS169 ws169-p003 | modified UTF-7 の folder 名、`\Noselect` の親、literal の folder 名 | 正しく読み、表示する（今は quoted・atom だけ、名前は bytes のまま） | `mailer/imap.c` の `imap_list_name` | 2026-10-07 |
| WS169 ws169-p003 | MOVE・UIDPLUS の server | `UID MOVE`・`UID EXPUNGE` を使う（今は COPY＋\Deleted＋EXPUNGE で、他の \Deleted の message も消える） | `mailer/imap.c` の `ml_imap_move` | 2026-10-07 |
| WS169 ws169-p003 | 宛先の名前が ASCII でない、To・Cc の長い行 | encoded word と header の折り返し（今は打ったまま 1 行） | `mailer/compose.c` の `compose_field` | 2026-10-07 |
| WS169 ws169-p003 | SMTP の AUTH LOGIN だけの server、8BITMIME の無い server | AUTH LOGIN、EHLO の答えに従う（今は AUTH PLAIN だけ、本文は QP なので 7bit） | `mailer/smtp.c` | 2026-10-07 |
| WS169 ws169-p003 | code の語が他の語の一部（shipping の pin など） | 語の境で数える（今は部分一致） | `mailer/code.c` の `code_contains` | 2026-10-07 |
| WS169 ws169-p004（メーラの app） | password の保存 | desktop の秘密の store に置く（今は 0600 の平文の file、2026-10-06 ユーザーの仮置き。`secret.c` の 2 関数を置き換える） | `mailer/secret.c` | 2026-10-07 |
| WS169 ws169-p004 | 起動ごとの取り直し、offline | local の cache に message を保ち、起動を速く・offline でも読む（今は memory だけ、起動のたびに各 folder の最新 50 通） | `mailer/store.c`・`sync.c` | 2026-10-07 |
| WS169 ws169-p004 | 一覧の先（51 通目より古い）、server の側の既読・削除の変化 | scroll で古い物を取る、FLAGS・EXPUNGE の変化を一覧に反映する（今は取った時のまま） | `mailer/sync.c`・`imap.c` | 2026-10-07 |
| WS169 ws169-p004 | Trash の中の Delete、Sent を自分で保つ server（Gmail）で Sent が 2 通 | 完全な削除、APPEND しない（今は Trash で Delete は何もしない、Gmail では 2 通） | `mailer/main.c` の `ml_request_move`、`sync.c` の ML_JOB_SEND | 2026-10-07 |
| WS169 ws169-p004 | 送信の失敗の後の書きかけ、下書き、添付の保存・送信 | 書きかけを Drafts に保つ、添付を保存・付ける（今は失敗の通知だけ、書いた物は画面に残る） | `mailer/view.c`・`main.c` | 2026-10-07 |
| WS169 ws169-p004 | account の削除・編集、5 個目の account | Settings か Mail の中で消す・直す（今は追加だけ、4 個まで） | `mailer/account.c`・`view.c` | 2026-10-07 |
| WS169 ws169-p004 | 日付の語が古くなる（Yesterday のまま日をまたぐ） | 描く時に今から作る（今は取った時の語） | `mailer/store.c` の `store_dates` | 2026-10-07 |
| WS169 ws169-p004 | 一覧が 512 通を超える | 全部を出す（今は 512 通まで） | `mailer/view.c` の `ML_MESSAGES_MAX` | 2026-10-07 |
| WS169 ws169-p005（browser の code の入力） | page に focus の欄が無い、別の tab・別の窓 | code を clipboard に置いて知らせる、どの tab に入れるかを選ぶ（今は focus の要素に打つだけ、無ければ何も起きない） | `browser/shell/mail.c` の `shell_mail_fill` | 2026-10-07 |
| WS169 ws169-p005 | 英字を含む code、`autocomplete="one-time-code"` の欄 | 英字の DOM の code、one-time-code の欄を探して入れる（今は数字の code だけ、focus の欄へ） | `browser/shell/mail.c`、`mailer/code.c` | 2026-10-07 |
| WS169 ws169-p005 | 通知の popup（WS156 p003）が無い間 | 通知の click で入る経路の QEMU の確認（今は titlebar の control だけが見える） | `browser/shell/mail.c`、WS156 p003 | 2026-10-07 |
| WS169 ws169-p005 | offer の 2 分の時間切れ | 時間で起きて取り下げる（今は loop が起きた時に見る） | `browser/shell/shell.c` の待ちの timeout | 2026-10-07 |
| WS169 ws169-p005 | 窓の中の帯の UI | page の上に code の帯（「Sign-in code … — Fill in / ×」）を出す（今は titlebar の control だけ。shell に描く層が要り、libbrowser の描画に関わる。2026-10-07 Q1 了承で backlog） | `browser/shell/`、libbrowser | 2026-10-07 |
| WS170 ws170-p002（Phone の保存） | 2 台が同じ連絡先を同時に作る・同じ item の state を書く、壊れた file | 同じ番号の連絡先をまとめる、state の衝突の file（cloud の conflicted copy）を読む、壊れた file を飛ばして知らせる（今は file ごとに別の連絡先、壊れた header は既定の値） | `phone/store.c` | 2026-10-07 |
| WS170 ws170-p002 | 大きな store（数千の item）、64 KB を超える本文、添付の `.files/` | 遅延して読む、大きな本文、添付の file の保存と表示（今は全部を起動で読む、64 KB で切れる、添付は名前だけ） | `phone/store.c` | 2026-10-07 |
| WS170 ws170-p002 | vCard の他の欄（複数の TEL・EMAIL・PHOTO・N）、QUOTED-PRINTABLE・折り返しの行 | 読む・保つ（今は FN と最初の TEL だけ、折り返しは読まない） | `phone/store.c` の `store_load_contact` | 2026-10-07 |
| WS170 ws170-p003（Phone の app） | 連絡先の編集・削除、会話の削除、添付の送信 | 編集・削除の UI（今は追加だけ、添付は notice） | `phone/view.c`・`main.c` | 2026-10-07 |
| WS170 ws170-p003 | 送信の失敗の再送、送信中のまま app を閉じた item | 再送の button、起動で sending の item を failed にする（今は state のまま） | `phone/main.c` | 2026-10-07 |
| WS170 ws170-p003 | 64 人を超える連絡先、32 を超える送信待ち | 全部を出す・待つ（今は一覧 64 人まで、待ちは 32 で古い物を上書き） | `phone/view.c` の `PH_VIEW_CONTACTS_MAX`、`main.c` の `PH_PENDING_MAX` | 2026-10-07 |
| WS170 ws170-p003 | 日の語が古くなる（Today のまま日をまたぐ） | 描く時に今から作る（今は読んだ・足した時の語） | `phone/store.c` の `store_words` | 2026-10-07 |
| WS170 ws170-p004（phone の API） | 本物の backend（モデム、スマホの bridge、VoIP、RCS）と、その状態・着信・通話の UI | backend の口を libkeiland-backend に置き、着信・通話中の画面（今は loopback だけ、着信の事象は無い） | `wayland/phone-shell.c`、libkeiland-backend | 2026-10-07 |
| WS170 ws170-p004 | 受信の時刻の時計の違い、長い本文の UTF-8 の途中の切れ | 送り手の時刻と受けた時刻、文字の境で切る（今は compositor の時刻、Echo は 1024 byte で切る） | `wayland/phone-shell.c` の `phone_loopback_send` | 2026-10-07 |
| WS155 ws155-p002（Calendar の保存） | 繰り返しの予定（RRULE）、TZID の zone、複数日の予定、VALARM、他の program の 1 file に複数の VEVENT | 展開・zone の変換・複数日の帯・知らせ・全部を読む（今は最初の VEVENT だけ、TZID は local と見なす、1 日の予定だけ） | `calendar/store.c` の `store_parse` | 2026-10-07 |
| WS155 ws155-p002 | 2 台が同じ予定を書き換える、壊れた .ics | cloud の conflicted copy を見つけて知らせる、壊れた file を飛ばして知らせる（今は黙って飛ばす） | `calendar/store.c` | 2026-10-07 |
| WS155 ws155-p003（Calendar の app） | calendar の追加・色の変更、Settings、Custom の種類、… の menu | 作る（今は notice） | `calendar/view.c` | 2026-10-07 |
| WS155 ws155-p003 | 予定の drag での移動・長さの変更、日をまたぐ予定、検索の結果の一覧 | 作る（今は編集の form だけ、検索は月の中を薄くするだけ） | `calendar/view.c` | 2026-10-07 |
| WS155 ws155-p003 | app が止まっている間の開始の通知、日をまたいだ時の今日の更新 | 通知の daemon か compositor の予定の知らせ、0 時の今日の更新（今は起動の時の今日、app が動いている間だけ通知） | `calendar/main.c` | 2026-10-07 |
| WS155 ws155-p003 | 時刻の欄の誤り（25:00 など）、終わりが始まりより前 | 欄に誤りを示す（今は notice と、終わりを始まりにする） | `calendar/view.c` の `view_edit_save` | 2026-10-07 |
| WS155 ws155-p004（時計から開く） | 時計の長押し・右 click、App Home に Calendar が無い image | 予定の簡単な一覧の popover、無い時の知らせ（今は click で起動だけ、無ければ log だけ） | `wayland/shell.c` の `bar_press`、`home.c` | 2026-10-07 |
