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
