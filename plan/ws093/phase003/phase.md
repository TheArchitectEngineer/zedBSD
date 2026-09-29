<!-- awesome-plan project=zedbsd record=ws093-p003 -->

# ws093-p003: Always Open With と Use System Default

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS093](../ws.md)
Queue: main の依頼（2026-09-29、worktree `.claude/worktrees/ws093-open`、branch `wt/ws093`、main を merge してから）。
`plan/tools/files/host-build.sh` の修正は main の許可（この Phase の中で）。

## 範囲と受け入れ

[design.md](../design.md) §3: 利用者の上書きを画面から選ぶ。File menu と context menu の「Always Open With」（その file の way と、区切りと
「Use System Default」）。選ぶと利用者の一覧（`$XDG_CONFIG_HOME/keiland/open-with`、無ければ `~/.config/keiland/open-with`）に Files の行を書き、
その file をその way で開く。受け入れ: host の試験、guest で txt を Terminal (less) にすると double click が less で開き、Use System Default で
Text Editor に戻る。

## 変えたこと

| file | 内容 |
| --- | --- |
| `userland/desktop/files/apps.c` | `fm_apps_set_default`・`fm_apps_clear_default`・`fm_apps_has_default`。利用者の一覧の場所を `apps_user_list` に分けた（`fm_apps_for` も使う）。書き方: 新しい一覧（`open-with.new`）に `# set by Files` と `TYPE<TAB>NAME<TAB>COMMAND` を先頭に書き、古い一覧の行を写す（同じ type の Files の行とその注釈だけを除く。利用者の行・注釈はそのまま）。`rename` で置き換える。folder（`~/.config`・`keiland`）が無ければ作る。log `DEFAULT set/clear type=… error=…` |
| `userland/desktop/files/files.h` | `FM_ACTION_ALWAYS_WITH_FIRST`（400〜）・`FM_ACTION_USE_SYSTEM_DEFAULT`（450）、menu の状態の `user_default`、app の `menu_user_default`、3 つの関数の宣言 |
| `userland/desktop/files/menu.c` | File menu の submenu「Always Open With」（Open With の次）、way の slot 8 つ・区切り・「Use System Default」。`menu_state_always`（slot の名前と表示、Use System Default は利用者の選択がある時だけ有効） |
| `userland/desktop/files/ui-context.c` | context menu の「Always Open With」（way・区切り・Use System Default） |
| `userland/desktop/files/ui-menu.c` | action の処理 `menu_always`（既定にして、その file を新しい既定で開く。pill「Plain Text files now open with Terminal (less)」）・`menu_system_default`（pill「… open with the system's default again」）。menu の way の cache に `fm_apps_has_default` の結果を持ち、変えた後は cache を捨てる |
| `plan/tools/files/host-build.sh` | libkeiland の `gesture.c`・`scroll.c`・`motion.c` を build に足した（`files/touch.c` が ws081-p010 から使う。これまで link できなかった）。main の許可 |
| `plan/tools/files/host-model.sh`（新規） | host-build の後に `files-model` を一時 folder で走らせる。p002 の `plan/ws093/tests/host-model.sh`（host-build の複写に libkeiland を足した補い）はこれに置き換えて削除した |
| `plan/ws093/tests/host-default.c`・`host-default.sh`（新規） | Always Open With の host の試験 |
| `plan/ws093/tests/open-guest.sh` | `always` の手順（context menu と File menu の操作、zdesktop の `MENU row item=` の位置で click） |

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| style | `python3 plan/tools/style-check.py`（apps.c・menu.c・ui-context.c・ui-menu.c・host-default.c）、`git diff --check` | 0 件 |
| host（新規） | `sh plan/ws093/tests/host-default.sh` | PASS（21 件）: 一覧が無い時に folder と一覧を作る、選んだ way が既定、2 度目の選択は置き換え（type ごとに 1 行）、利用者の行・注釈・他の type の行は残る、別の type の選択は互いに残る、Use System Default は Files の行だけを消し利用者の行が既定に戻る、選択の無い type の clear は何も変えない |
| host（回帰） | `sh plan/tools/files/host-model.sh`（直した host-build.sh で） | link できるようになった。open の試験はすべて ok。FAIL 1 件「picture: PNG not read yet (ENOTSUP)」は既存の失敗（p002 で main の apps.c でも出ることを確認。thumbnail、範囲外） |
| build | `sh build/ws093-build.sh`（files・imageview・textedit・wayland、`config/ci/config-amd64.mk`、`BUILD=build/ws093-amd64`） | 4 つとも rc 0、warning 0 |
| guest（QEMU、Venus、always） | main の `build/ws035-sq/hdd-image.img` の複写で `plan/ws093/tests/open-guest.sh build/ws093-shots always` | PASS: 4-notes.txt の context menu → Always Open With（submenu に Text Editor・Terminal (less)・Terminal (ed)・区切り・淡い Use System Default）→ Terminal (less): `DEFAULT set type=text/plain app=Terminal (less) error=0`、less の terminal で開く（`always-less.png`）、一覧は `# set by Files` と `text/plain<TAB>Terminal (less)<TAB>@terminal less %f`、double click が less で開く。File menu → Always Open With（Terminal (less) が先頭、Use System Default が有効、`always-file-menu.png`）→ Use System Default: `DEFAULT clear … error=0`、一覧は空、double click が Text Editor で開く（`always-back.png`）。zdesktop の ERROR 0 |
| guest（回帰、mouse） | 同じ guest で `open-guest.sh build/ws093-shots/p003 mouse` | PASS（p002 の全ての種類の double click と Enter） |
| boot | `OUTPUT=build/ws093-boot-test bash plan/tools/boot-test.sh build/ws093-run/disk.img` | PASS（`build/ws093-shots/boot-login-p003.png`） |

- 判定は Files・zdesktop の log（SSH）・ps・書かれた一覧・画面。console・serial は読んでいない。
- 画面は worktree の `build/ws093-shots/`（`always-context/submenu/less/file-menu/system/back.png`）。
- 使い終えた disk の複写 `build/ws093-run`・`build/ws093-pen-run` は削除した（main の許可）。

## 既存の失敗（範囲外）

- `files-model` の「picture: PNG not read yet (ENOTSUP)」（PNG の thumbnail）。main の apps.c でも出る。

## 未実施・制限

- 実機は未実施。注入の touch での Always Open With（長押しの context menu）は未実施（p002 で double tap は確認済み）。
- 情報の card の opener の pill は今のまま（選ぶとその way で開く。既定にはしない）。

## Resume point

p004（全文の規約と回帰）から。
