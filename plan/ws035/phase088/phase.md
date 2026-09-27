<!-- awesome-plan project=zedbsd record=ws035p088 -->

# ws035-p088: drag and drop の「ask」（落とし先が尋ねる）と app の間の text の drop

Phase ID: `ws035-p088`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-27 main の割り当て「if time remains: the DnD "ask" popup and text drops between apps」）

## 範囲

p084 の drag and drop は copy と move だけで、`wl_data_device_manager` の ask（落とした後に落とし先が利用者に尋ねる）は
zdesktop も zdesktop-files も扱わず、file の drag は `text/uri-list` だけを出し、zdesktop-terminal は drop を受けなかった。

1. zdesktop: Alt を押しながらの drag は ask を選ぶ。落とし先が drop の後に出す context menu を drop の serial で受け付ける。
   落とした offer を finish せずに壊したら source を cancel する。ask の後の set_actions で選ばれた action を source へ知らせる。
2. zdesktop-files: 落とし先として ask に「Move Here・Copy Here・Link Here・Cancel」の menu を落とした所に出し、選ばれた
   action で copy・move・link して finish、Cancel か menu を閉じたら offer を壊す（drop を断る）。source は copy・move・ask を出し、
   `text/uri-list` に加えて `text/plain;charset=utf-8`（path を 1 行に 1 つ）も出す。
3. zdesktop-terminal: `text/uri-list`（優先）か UTF-8 の text の drop を copy として受け、uri-list は単一引用符で囲んだ path を
   空白で区切って、text はそのまま shell へ貼る。

## 実装（2026-09-28）

- zdesktop `data.c`: `DATA_SEAT_ALT`、`drag_choose` の「Alt は ask」、enter の serial を記録し、drop で落とし先の client と
  serial（`dnd_drop_client`・`dnd_drop_serial`）を残す。落とした offer が finish 前に消えたら `SOURCE_CANCELLED`
  （`ZWL DATA drag unfinished`）。落とした後の ask の offer への set_actions は選ばれた action を offer に記録し、source へ
  `action` を送る（`ZWL DATA drag chosen`）。offer 自身へは送らない（下）。`zwl.h` にその 3 つの field。
- zdesktop `menu.c`: `get_context_menu` は press の serial のほか、drop の serial（その client のもの）も受ける。drop の選択なら
  落とし先の窓を最前面に上げてから menu を開く（上げないと menu がすぐ閉じた）。
- zdesktop-files: `files.h`（`FM_DND_ASK`、`FM_ACTION_DROP_MOVE/COPY/LINK/CANCEL`、`FM_REQUEST_DROP_ASK/CANCEL`、
  `FM_CONTEXT_DROP`、`drop_asking`）、`ui-drag.c`（ask の drop で menu を求める）、`ui-context.c`（drop の menu と action）、
  `menu.c`（`fm_menu_context` が serial を受ける）、`main.c`（menu・断り・menu が閉じたら断る・finish の action）、`dnd.c`
  （2 つの型、copy・move・ask、`fm_dnd_finish(window, action)`、`fm_dnd_abort`）、`window.h`。
- zdesktop-terminal: `clipboard.c`（drop の enter・leave・drop、`clipboard_read`、`clipboard_paths`、`terminal_clipboard_drop`）、
  `main.c`（`main_drop_paste`）、`terminal.h`。

## 設計の判断・制限

- ask の選び方は Alt（GNOME・KDE と同じ）。落とし先が ask だけを好むことはない（zdesktop-files の好みは move）。
- 選ばれた action を offer へ `action` として送らない: zedBSD の libwayland（client）は、client が既に壊した server 側の
  object への event を protocol error にする（libwayland の zombie の扱いが無い）。落とし先は set_actions・finish・destroy を
  続けて送るので、送ると落とし先が切断される（最初の実行で zdesktop-files が `vkAcquireNextImageKHR` の SURFACE_LOST で
  落ちた）。落とし先は自分で選んだので知らせる必要も無い。libwayland の zombie の扱いは残り（下）。
- drop の menu が閉じられた（Esc・外の click）ときも drop を断る（source は cancelled を受け、何も動かない。この経路は試験で未確認）。
- terminal の drop は常に copy（source の file は動かない）。貼るだけで Enter は押さない。

## 検証（amd64、Venus の guest、2026-09-28）

- `plan/ws035/tests/zdesktop-p088.sh` PASS: (1) Alt で Logo.png を A から B へ: `drag action client=2 action=4`、B の
  `DROP drop ... action=4`・`CONTEXT-MENU open rows=5`、Copy Here で `DROP operation=copy`・`drag chosen action=1`・
  `drag finish client=2 action=1`、両方の folder に Logo.png。(2) Alt で Screenshot.png、Cancel: `DROP ask cancel`・
  `drag unfinished client=2`・A の `DRAG out done dropped=0`、何も動かない。(3) terminal へ Screenshot.png: `ZTERM DROP enter
  uris=1 text=1`・`drag accept client=3 mime=text/uri-list`・`ZTERM DROP bytes=36 uris=1`、shell に
  `'/tmp/fhome/Desktop/Screenshot.png'`、file は残る。
- 回帰 PASS: zdesktop-p084（窓の間の DnD。source が 2 つの型・copy|move|ask になったので `types=2 actions=7` に直した）、
  zdesktop-p086（terminal のタブ）、zdesktop-p087（clipboard の橋、terminal の clipboard.c を変えたため）、files-p009（context menu）。
- 規約: 変えた file の style-check は前と同数（すべて 0）。build warning 0。
- 画面（`/home/awe/zedBSD-rpi4/build/ws035-shots/`）: `p088-20260928-venus-ask.png`（B の drop の menu: Move Here・Copy Here・
  Link Here・Cancel）、`p088-20260928-venus-term-drop.png`（terminal に引用符付きの path）。
- 実機（i915）: 未実施。boot test: 2026-09-27 のユーザーの指示で無し。

## 残り

- libwayland（client）の zombie の扱い: client が壊した server 側の object（offer 等）への event を捨てる（今は切断）。
- 未確認の経路の試験: drop の menu の keyboard 操作・Esc で閉じる、Link Here（guest では Copy Here と Cancel だけを確認）。
- zdesktop-files が text の drop（file ではない）を受けて file を作ること、terminal からの drag（source）。
- X11 の app への drop（XDND の橋）は無い。
