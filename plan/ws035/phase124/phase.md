<!-- awesome-plan project=zedbsd record=ws035p124 -->

# ws035-p124: 窓の title の左の app の印を App Home の絵に揃える

Phase ID: `ws035-p124`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-29、サブエージェント。QEMU の Venus と host。実機は未実施）
Phase disposition: normal
Queue: なし（2026-09-29 main の割り当て、p123 の残り。shell.c の変更は main の許可、入力・touch の処理には触れない）

## 範囲

title bar・docked の bar・Wiseview の名札の左の app の印（`shell.c` の `draw_title()`、青い四角に頭文字）を、[p123](../phase123/phase.md) の
App Home の絵に揃える。Browser の絵を App Home で確かめる（時間があれば）。

## 設計

- 窓の application ID（X11 の窓は class）から絵と色を引く表を `icons.c` に置く（`zwl_icon_for_app_id()`）: `files`・`notes`・`terminal`・
  `pdfviewer`・`browser`・`mview`・`Gears`（zgears の class）・`XTerminal`（zterm の class）。色は App Home の組み込みの一覧・demo の
  apps.conf と同じ。表に無い ID の窓（test の client、他の X11 の program）は今までどおり青い四角に頭文字。
- 印は 20 px の角の丸い四角（前と同じ大きさ・位置、title の始まりは変えない）に、絵を 14 px で。40 px の atlas の絵を縮めると線が
  細く灰色に滲むので、App Home の絵を 14 px でも atlas に描く（40 px の行の右、10 枚で 150 px）。14 px では線が 1 px 強になり薄いので、
  App Home の絵に限って線の太さの下限を 1.6 px にした（`ICON_APP_MIN_STROKE`、40 px の絵と titlebar の control の icon は変わらない）。
- 印の四角と絵は title の ink の不透明度で薄くなる（Wiseview の名札が現れる間、前の青い印は薄くならなかった）。
- `draw_title()` を、絵の印（`draw_picture_mark()`）と頭文字の印（`draw_letter_mark()`、前の処理そのまま）に分けた。

## 実装（2026-09-29）

| file | 変更 |
| --- | --- |
| `userland/desktop/wayland/icons.h`・`icons.c` | `struct icon_app_id` と表、`zwl_icon_for_app_id()`、App Home の絵の線の太さの下限 |
| `userland/desktop/wayland/glass.c` | App Home の絵を 14 px でも atlas に（`app_icons_small`）、`glass_draw_icon()` が 21 px 以下なら小さい方を使う |
| `userland/desktop/wayland/shell.c` | `draw_title()`・`draw_picture_mark()`・`draw_letter_mark()`（入力・touch の処理には触れていない） |

## 検証（2026-09-29、QEMU の Venus。実機は未実施）

- build warning 0（wayland、demo の Venus の image。image の build の warning は openssl・openssh・Noct の外部の既知のものだけ）、
  `style-check.py` shell.c・icons.c・icons.h・glass.c 違反 0、`plan/tools/titlebar/icons-host.c` PASS。
- scratch の preview で 12・14・16 px の絵を 20・22 px の四角に並べて見て、14 px と線の下限 1.6 px を選んだ。
- `demo-walk.sh`（1920x1280、kei）全段の log の待ち ok。Files・Terminal・zterm（X terminal）・PDF Viewer の title bar と Wiseview の名札
  （PDF Viewer・Notes・zterm・Terminal・Files）の印が絵になった。拡大: `p124-20260929-title-marks-zoom.png`。
  画面: `/home/awe/zedBSD-rpi4/build/ws035-shots/p124-20260929-demo-{04-files,05-terminal,05b-xterminal,07-pdfviewer,08-wiseview}.png`。
- Browser の絵: demo の Venus の image には browser が無い（WS075 の HDMI の demo の image は `config-zdesktop-hw.mk` に browser があり、
  package が `/usr/share/browser/start.html` を入れるので App Home に出る。apps.conf の追加は要らない）。guest で一時の apps.conf
  （`Browser|/bin/terminal|…|3a8fd8|browser`）で App Home に地球の絵が出ることを確かめた: `p124-20260929-browser-tile.png`。
- `plan/tools/boot-test.sh build/amd64/hdd-image.img` PASS（boot-test の QEMU には Venus が無いので greeter が終わり getty の login。
  `p124-20260929-boot-test-login.png`）。
- 未実施: 実機（i915 の HDMI）、browser の窓の title bar の印（Venus の image に browser が無い）。
