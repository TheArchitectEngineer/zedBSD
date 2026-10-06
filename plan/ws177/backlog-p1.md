# WS177 積み残しの一覧（P1）

[WS177](ws.md) の「集め方」に従い、P1 が 1 行ずつ足す。

| 元の WS・Phase | 分岐（準正常系・異常系） | 期待の動き | source の所在 | 書いた日 |
| --- | --- | --- | --- | --- |
| ws090-p023 | Files の list（列・複数選択・並べ替えの見出し）と grid（icon の格子）を libkeiland の部品にする（libkeiland に該当の部品が無い） | 複数選択・列・並べ替えを持つ list と icon の格子の部品を libkeiland に足し、Files がそれで描く | `userland/desktop/files/ui-list.c`・`ui-grid.c`、`userland/desktop/libkeiland/ui/list.c` | 2026-10-06 |
| ws090-p023 | icon だけの小さなボタン（Files の sidebar の削除・取り出し、tabs・Quick Look・Help・情報の閉じる、Settings の `se_icon_button_draw`）が自前の描画 | libkeiland に icon の button を足し、両 app がそれで描く | `userland/desktop/files/ui.c`（`ui_draw_sidebar`）・`ui-tabs.c`・`ui-preview.c`・`ui-help.c`・`ui-info.c`、`userland/desktop/settings/widgets.c` | 2026-10-06 |
| ws090-p023 | 異常系: Files の部品の `kl_ui` が作れない（`fm_widgets_begin` が ENOMEM） | 部品を kl_ui 無しで（光らせずに）描く（今は sidebar の `kl_sidebar_place` が NULL の kl_ui で呼ばれうる） | `userland/desktop/files/ui-widgets.c`・`ui.c`（`ui_draw_sidebar`） | 2026-10-06 |
| ws090-p007 | 準正常系: Settings の管理の操作の行が長すぎる（libkeiland の欄は 511 byte） | 今は送らずに「too long」と言うだけ。欄ごとの長さの上限（account-admin が取る長さ）で打てなくする | `userland/desktop/settings/page-users-admin.c`（`admin_apply`）・`widgets.c` | 2026-10-06 |
| ws090-p022 | Browser の web の form（input・textarea）で IME を受ける（未着手） | WS131 p025（browser の shell を libkeiland の窓に）の後、libbrowser に編集の口（commit・preedit・caret の矩形・編集中か）を足して結線 | `userland/desktop/browser/shell/`、`userland/desktop/libbrowser/`（WS074） | 2026-10-06 |
