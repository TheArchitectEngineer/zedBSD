<!-- awesome-plan project=zedbsd record=ws179-p002 -->

# ws179-p002: 残りの app の accent を theme に従わせる

Status: cleared（2026-10-07 Q1 の判定: T1-337 の purple・yellow で Calendar・PDF Viewer・Phone・Mailer・Notes・Image Viewer・Music が accent に従う（Q1 が PNG を目視）。PDF Viewer の検索の highlight は橙のまま（accent でない、意図どおり））（旧: in-progress（q833、P1。2026-10-07 実装と host 試験済み、T1 の撮影待ち））
Disposition: normal
Parent: [WS179](../ws.md)

## 範囲

2026-10-07 Q1: Calendar・PDF Viewer・Phone・Mailer・Notes・Image Viewer の直に書いた accent を、p001 の theme（`accent`・`accent_ink`・`accent_text`）に従わせる。p001 の直後、ベータ2 の中。設計は [design.md](../design.md) の §5（p002）。

## 実装（2026-10-07、P1）

- Calendar（`calendar/view.c`・`calendar.h`）: 吹き出し・選んだ日・今日・card などの accent の tint を `style->theme->accent` に、今日の数字と選んだ表示の文字を `accent_ink` に。机上の日めくりの帯は accent と ink（日曜は赤と白のまま）、page の絵の cache を accent で引き直す（`page_accents`）。土曜の青（`CAL_COLOR_SATURDAY`・`_TEXT`）は曜日の色なので変えない。
- PDF Viewer（`pdfviewer/draw.c`・`find.c`・`viewer.h`・`main.c`）: 今の page の帯と縁、既定の button と文字、検索の印を `pv_draw_set_accent`（main.c が theme から渡す、外観の変化と同じ時に）。draw.c・find.c は libkeiland 無しの host 試験に入るので theme を直に読まない。
- Phone（`phone/view.c`）: 通話の button の丸を theme の accent に。RCS の吹き出しの青（`PH_COLOR_RCS`、SMS の緑と対の区別の色）は変えない。
- Mailer（`mailer/view.c`）: sign-in の code の帯の tint を theme の accent に。label の色の表（`store.c`）は label の色なので変えない。
- Notes（`notes/ui.c`・`main.c`）: 道具の印・選んだ道具の pill を theme の accent、その文字を `accent_text`、図形の選択の枠を theme の accent（RGBA の並び）。
- Image Viewer（`imageview/draw.c`）: 「Open…」の button と文字を `accent`・`accent_ink`。
- Music（`music/view.c`、範囲の外の小さな直し）: 演奏中の曲と artist の文字を `accent_text`、再生の button の印を `accent_ink`（p001 で theme の accent に従っていた所の文字）。
- 従わせない物（記録）: IME の候補の popup（`ime/popup.c`、IME は外観を watch しない別の program、`0x2563eb`）。

各 app は外観の変化で描き直す口を既に持ち（KL_APP_THEME か `kl_appearance` の callback）、libkeiland は accent の変化でも同じ口を呼ぶ（p001）。

## 確認（host）

| コマンド | 結果 |
| --- | --- |
| target の clang（`-Werror -fsyntax-only`）: 変えた 12 file | warning 0 |
| `plan/ws170/tests/run-host-phone.sh`・`plan/ws169/tests/run-host-mailer.sh` | 全 PASS（FAIL 0） |
| `plan/ws079/tests/run-pdfviewer-host.sh`・`plan/ws128/tests/run-host-pdfviewer-find.sh` | ok、14 passed 0 failed |

未実施: QEMU（T1: 各 app を purple・yellow で撮る）、実機。
