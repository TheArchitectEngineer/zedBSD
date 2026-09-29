<!-- awesome-plan project=zedbsd record=ws094-p005 -->

# ws094-p005: context menu・名前の変更・Trash・Copy・Paste・New Folder・Show in Files

Status: cleared（2026-09-30、QEMU の Venus。実機は未実施）
Disposition: normal
Parent: [WS094](../ws.md)
Queue: main の依頼（2026-09-30、worktree `.claude/worktrees/ws094-desktop`、branch `wt/ws094`、`git merge main -m WIP` の後）

## 範囲と受け入れ

design §4 の「操作」のうち、次のものを扱う。

- 右 click の menu。項目の上と空いた所で別の menu を出す。
- 名前の変更（icon の名前の所の text field、F2）。
- Trash（menu と Delete）、Cut・Copy・Paste（menu と Ctrl+X・C・V）、Duplicate、Undo。
- New Folder。
- Show in Files（項目）と Show Desktop in Files（空いた所）。
- Clean Up（空いた所の menu）。
- Change Wallpaper（Settings があるときだけ出す、J7）。

touch の long press と drag は p006 で扱う。

受け入れ: QEMU の Venus で、pointer で menu を開いて選ぶと、どの操作も働くこと（log と画面）。
新しい項目は空いた cell に入り、他の項目は動かないこと。名前を変えた項目は cell を保つこと。
Paste で名前がぶつかったときは、問いが desktop の上に出て答えられること。

## 設計（p004 からの追加）

| file | 内容 |
| --- | --- |
| `ui-desktop-actions.c`（新規） | `fm_desktop_action`: desktop の menu の action を扱う。Open は desktop の開き方（folder は新しい窓）、Show in Files は `/bin/files ~/Desktop`、Clean Up、Change Wallpaper は `/bin/settings wallpaper`。他は `fm_ui_action` に渡す。`fm_desktop_operation_key`: Delete・F2・Ctrl+C・X・V・D・Z・Ctrl+Shift+Z・Ctrl+Shift+N。`fm_desktop_rename_end`: 名前の変更を終え、項目の場所（shown と saved）を新しい名前へ移す。`fm_desktop_can_change_wallpaper` |
| `ui-desktop.c` | 右 press で `desktop_context` を呼ぶ（項目なら選択して items の menu、空いた所なら選択を外して empty の menu）。名前の field を描く。file manager の message と操作の進みを下端の pill に描く。問い（`fm_overlay_draw`）を desktop の上に描く。問いがある間は、pointer と key を `fm_ui_event` に渡す。`FM_EVENT_ACTION` を扱う。listing の名前の順の hash も配置し直しの条件に加えた（rename は数も mtime の秒も変えないことがある） |
| `desktop-layout.c` | shown: 今の配置を名前つきで memory に持つ。次の配置では saved の後に使う。これで、新しい項目は空いた cell に入り、他の項目は動かない。`fm_desktop_remember`・`fm_desktop_layout_rename` を足した。Clean Up は shown も消す |
| `ui-context.c` | `app->desktop` の menu。項目の menu は Files と同じだが、Open in New Tab を出さず、Get Info の代わりに Show in Files を出す。空いた所の menu は New Folder・Paste・Clean Up・Show Desktop in Files・Change Wallpaper…（Settings があれば） |
| compositor の `wayland/menu-shell.c`（main の了解 2026-09-30、p002 の漏れ） | desktop の context menu は開いた直後に閉じていた。`zwl_menu_tick` は「menu の窓が一番上の窓でなければ閉じる」が、desktop surface は一番上の窓にならない。直し: desktop で開いた menu は、開いた時の一番上の窓（`desktop_top`）を覚える。その窓が一番上のままなら開いたままにし、他の窓が上に来る・map される・その窓が消えると閉じる（done を送る）。窓の menu の動きは変えていない |
| `menu.c`・`main.c` | desktop（toplevel が無い）では、menu の service だけを開く（context menu 用、window の menu は無い）。閉じた context menu は desktop でも refresh で片付く |
| `files.h` | action を 3 つ足した（`SHOW_IN_FILES` 55、`CLEAN_UP` 56、`CHANGE_WALLPAPER` 57）。`struct fm_desktop` に `shown`・`laid_names` を足した。宣言も足した。`struct fm_tab` の注釈が p004 で `fm_desktop_place` の上にずれていたので戻した |

design との差:

- Get Info は desktop の menu に出さない。情報の card は Files の窓の overlay で、desktop には card を描く場所が無いため。代わりに Show in Files を出す。
- Settings の頁は `--page=wallpaper` ではなく、位置引数の `wallpaper` で開く（`se_page_find` の語）。
- 名前を変えた項目は、saved の行が無くても cell を保つ（shown の名前を変える）。saved の行がある項目は、file の行の名前も変える（design §4 のとおり）。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make … BUILD=build/ws094-amd64 build/ws094-amd64/bin/files …/bin/wayland …/dynamic/libkeiland.so` | rc 0、warning 0 |
| style | `plan/tools/style-check.py`（ui-desktop.c・ui-desktop-actions.c・desktop-layout.c・ui-context.c・menu.c・menu-shell.c・host-desktop.c）、`git diff --check` | 0 件（main.c の 3 件は以前からのもの） |
| host | `plan/ws094/tests/host-desktop.sh`（p005 の分を足した）、`plan/tools/files/host-model.sh` | PASS、PASS。p005 で足した確認: shown が配置で保たれ、新しい項目は空いた cell に入る。rename で shown と saved が移る。Clean Up で shown が消える。desktop の 2 つの menu の行。F2 の field と Enter で名前が変わる |
| guest: menu（新しい手順） | `files-desktop-guest.sh build/ws094-shots/p005-final install show menu` | PASS（下） |
| 回帰: p003・p004 の手順 | `files-desktop-guest.sh … install show watch input saved` | PASS |
| 回帰: probe | `desktop-guest.sh … install role refuse input home-drag dnd restart` | PASS |
| 回帰: Files | `BIN=build/ws094-amd64 files-open.sh … mouse` | PASS |
| 回帰: WS099 の C9（compositor を変えたため） | `plan/ws099/tests/criteria.sh build/ws094-c9.img build/ws094-c9 C9`。image は ws099 の criteria image の複写に、この worktree の compositor・Files・library を入れたもの | 10 本すべて PASS（p052・p053・p072・p076・p126・p128・p134・p137・p138・cursor-owner）。1 回目は criteria image ではない base.img で流し、probe の program が無いため 6 本が FAIL した。image の取り違えで、code の問題ではない |
| boot test | `plan/tools/boot-test.sh build/ws094-run/disk.img` | PASS（`build/ws094-boot-test/login.png`） |

guest の手順 menu で確かめたこと（判定は zdesktop と Files の log、SSH と画面。console・serial は読んでいない）:

- 空いた所の右 click。
  - `DESKTOP context empty x=700 y=366` が出て、menu に New Folder・Paste・Clean Up・Show Desktop in Files が並ぶ（`menu-empty.png`）。
  - New Folder を選ぶと、`untitled folder` が空いた cell（0,5）に入り、名前の field が開く（`rename-field.png`）。
  - `Plans` と打って Enter で名前が変わり、`DESKTOP rename … error=0` が出て、Plans は 0,5 のまま。
- notes.txt の右 click。
  - menu は Open・Open With・Always Open With・Cut・Copy・Paste・Rename・Duplicate・Tags・Show in Files・Move to Trash（`menu-item.png`）。
  - Rename で拡張子の前が選ばれるので、`todo` と打つと todo.txt になり、cell 0,1 を保つ。
- photo.png を Copy して、空いた所で Paste。同じ folder なので `photo 2.png` が作られ、空いた cell 0,6 に入る。
- 他の folder の report.pdf を clipboard に置いて Paste。名前がぶつかるので、問いが desktop の上に出る（`collision.png`、暗くした desktop の中央の card）。Enter（Keep Both）で 8 項目になる。
- script.sh を click して Delete。Trash（`~/.local/share/Trash/files/script.sh`）へ移り、7 項目になる（`trash.png`）。
- report.pdf の Show in Files。
  - `/bin/files /tmp/dhome/Desktop` の窓が map する（`show-in-files.png`）。
  - その窓の横で desktop を右 click すると、menu は開いたままになる（`menu-over-window.png`）。窓を click すると閉じる。menu が開いている間に新しい窓が map しても閉じる。
- Clean Up。`DESKTOP clean-up error=0` が出て、7 項目が 1 列目の 0〜6 に詰め直される（`cleanup.png`）。
- Change Wallpaper。image に `/bin/settings` が無いので menu に出ないことを確かめた。Settings がある場合の起動は未実施。

画面は `build/ws094-shots/p005-final/` と `build/ws094-shots/p005/`。

## 見つけたこと・制限

- compositor の desktop の popup の対応（p002）は、menu を開くまでは働いていたが、次の frame で閉じていた。p002 の試験は menu を開くところまでしか見ていなかった。
- 1 回目の Paste の試験は「同じ folder への Copy で問いが出る」と想定していた。実際の Files は、同じ folder への Copy を `photo 2.png` にして問いを出さない。試験は、他の folder の file を Paste して問いを出す形に直した。
- 未実施:
  - Change Wallpaper の起動（image に Settings が無い）。
  - touch の long press（p006）。
  - Undo・Duplicate・Cut の guest での確認（host と Files の既存の試験の範囲。desktop では `fm_ui_action` に渡すだけ）。
  - 実機。
- 名前の field は名前の幅に合わせて広げる（cell の幅から 2 cell まで）。最初の実行では cell の幅に固定していて、`untitled folder` の頭が切れていた。

## Resume point

2026-09-30: cleared。次は p006（drag: desktop の中の移動、folder への移動、Files の窓との DnD、touch）。
