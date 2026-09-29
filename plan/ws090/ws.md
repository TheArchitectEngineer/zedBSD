<!-- awesome-plan project=zedbsd record=ws090 -->

# WS090: widget・control の共有 library（慣性の smooth scroll を含む）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p002 cleared（2026-09-29、`libkeiui` KUI_VERSION 1: canvas・text・icons・theme）。次は p003（scroll view・入力の層・`kui_text_touch`）
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「スクロールやボタンなど、ウィジェットやコントロールを共有ライブラリにする。独自のものでよい。慣性スムーズスクロールは少なくともライブラリにして再利用したい。」

- 今は Files・Terminal・Notes・PDF Viewer・ブラウザ・（作成中の）Settings が、scroll・button・list・sidebar・card 等をそれぞれ持つ。慣性の scroll は
  WS081 で `libkeiland/motion.c`・scroller・gesture にまとめ始めた。
- 独自の共有 library（Kei の見た目、Vulkan と Wayland の上）に、少なくとも慣性の smooth scroll（touch・wheel・key・overscroll の rubber band）を
  部品として出し、既存の app を順に移す。button・toggle・slider・list・sidebar・card・text field 等の範囲と、library の名前・置き場（libkeiland を
  広げるか、新しい library か）・API の形は p001 で決める。新しい app（WS089 Settings、WS091・WS092）はこれを使う。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [ws090-p001](phase001/phase.md) | 設計（[design.md](design.md)） | cleared（2026-09-29） | — |
| [ws090-p002](phase002/phase.md) | libkeiui の骨組みと描画の層（canvas・text・icons・theme）（Settings の書き換えは p007 へ） | cleared（2026-09-29。`libkeiui.so` warning 0、host 13/13 で Files・Settings と byte で一致） | p001 |
| ws090-p003 | scroll view（`kui_scroll`）と入力の層（`kui_ui`）と文字の view の touch（`kui_text_touch`: 1 本指で選択・2 本指で scroll、2026-09-29 ユーザー） | planning | p002 |
| ws090-p004 | 窓の土台（`kui_window`、Vulkan・shm・無し）、Text Editor の窓・present・touch・clipboard | planning | p003 |
| ws090-p005 | 部品（button・switch・slider・field・list・sidebar・card・row・header・dialog・chip・progress）と見本の program | planning | p003 |
| ws090-p006 | file chooser を libkeiui へ、libkeiland から取り除く（KEILAND_VERSION）、Text Editor の chooser・dialog・chip | planning | p004・p005 |
| ws090-p007 | Settings を libkeiui へ（描画の層の共有の置き換えを含む） | planning | p005、WS089 の完了 |
| ws090-p008 | PDF Viewer・Image Viewer を移す | planning | p006 |
| ws090-p009 | Files（その 1）: 描画の層と scroll | planning | p003 |
| ws090-p010 | Files（その 2）: 部品と窓 | planning | p009・p005・p004 |
| ws090-p011 | Terminal・Notes: 窓（見せ方は無し）と scroll の model | planning | p004 |
| ws090-p012 | 規約の全文との照合と回帰 | planning | 全て |

p007〜p011 は app ごとに独立で、デモ（10/17）の前は 10/10 までに移し終えたものだけ残す（design.md J5）。

## 2026-09-29 の申し送り（main）

- WS089（Settings）は files の `canvas.c`・`text.c`・`icons.c` と `artwork/mark.c` を source のまま共有して compile している（ws089-p002）。files に target 別の CPPFLAGS が付くと中身が変わりうる。共有の library にするときの最初の対象の候補（F-038 を昇格したもの）。
- 2026-09-29 ユーザー:「テキストエディタのファイルピッカーは、KeiのUIライブラリに入れるのがいいと思いました。」→ 最初の共有の部品はファイルピッカー（Open・Save As の chooser）。今は libkeiland に `keiland_file_chooser_*` として置き（WS092 のエージェントが作る）、WS090 の library の設計の時にそこへ移す。
