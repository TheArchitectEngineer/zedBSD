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
