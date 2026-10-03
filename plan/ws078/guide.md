<!-- awesome-plan project=zedbsd record=ws078-guide -->

# WS078 作業の手引き（2026-10-01）

この file だけで WS078（Kei Operating System への名前の移行）を続けられるように、ゴール・棚卸し・安全な順番・command をまとめた。
記録の正は [ws.md](ws.md) と [phase003](phase003/phase.md)（食い違えば そちらが正）。数は 2026-10-01 の main の checkout（fb1f3317）で下の §5.1 の command を流して数えた。
image の build・QEMU・実機は流していない（未確認）。

## 1. ゴール

### 1.1 ユーザーの決定（2026-09-28、ws.md「目標」「ユーザーの判断」）

| # | 決定 |
| --- | --- |
| D1 | OS の名前は **Kei Operating System**（Kei = 軽い）。kernel の内部名は **zedbsd**。デスクトップの内部名は **Keiland**（画面には出さない。機能の名前で呼ぶ: System Menu、File Manager） |
| D2 | OS の見える所から zedBSD・zed・z を**徐々に**外す |
| D3 | 実行ファイル: zdesktop → `/bin/wayland`、zdesktop-x11server → `/bin/xserver`、zdesktop-browser → `/bin/browser`（済み、p003） |
| D4 | シンボルに zedbsd を入れない。接頭辞は `ZEDBSD_` でなく **`KERN_`**（済み、p002） |
| D5 | ロゴは **Kei** の 3 文字（K だけにしない、KDE の商標） |
| D6 | make の `ZEDBSD_` 変数は**今は残す**。`__ZEDBSD__`（toolchain の target）は**残す** |
| D7 | Keiland の protocol と library の接頭辞は `keiland_`（済み、p006）。`zwp_`・`zxdg_` は upstream の名前で改めない |
| D8 | Keiland の部品は `userland/desktop/<name>`（済み、p003） |

### 1.2 デモ（fg010）との関係

改名はデモの場面を直に持たないが、**画面に出る旧名**はデモに出る。2026-10-01 の棚卸しで、画面に出る旧名が 1 つ残っている:
**browser の start page（`userland/desktop/browser/data/start.html`）の title・見出し・本文の「zedBSD Browser」「zedBSD desktop」「zdesktop window」**（窓の title にも出る）。
デモの S1（起動の Kei の画面）・S2（greeter・system bar）は p004 と ws035-p107〜p109 で Kei になった（graphical な表示の確かめは未実施）。

### 1.3 完了の定義

| # | 条件 | 状態 |
| --- | --- | --- |
| C1 | 識別子（kernel・driver・UAPI・libc・bootloader）に `ZEDBSD_`・`__zedbsd_` が無い（D6 の例外を除く） | 済み（p002） |
| C2 | 実行ファイル・directory・data の path・API・protocol の名前が新しい（D3・D7・D8） | 済み（p003・p006）。protocol の header の **file 名** `zed-*-v1-client-protocol.h` が 6 つ残る（§2.3 の B3） |
| C3 | 画面・文書に出る旧名が無い（D1・D2・D5。残すと決めたものを除く） | 一部（p004 incomplete。§2.3 の A） |
| C4 | 注釈の旧名（`zdesktop`）が無い | 未着手（§2.3 の B1。main の判断: 他の作業が落ち着いた後に 1 つの agent で一度に） |
| C5 | 全文の規約と回帰（p005、WS の最後） | 未着手 |

**残すと決まったもの**（変えない）: `uname` の sysname・version（`userland/base/libc/posix.c:5319`・`5325`）、disk の format の印（`userland/base/mkfs/fat32-format.c:140`、
`ufs-format.c:31`・`1036`）、GPT の partition 名、kernel と loader の文言、source の copyright の header の「zedBSD」、Settings の About の「powered by zedBSD」
（`userland/desktop/settings/page-about.c:119`、Kei の identity の設計の副題）、make の `ZEDBSD_` 変数、`__ZEDBSD__`、file 名 `zedbsd.cfg`・`bootloader/uefi/zedbsd-config.c`・
toolchain と package の patch の `zedbsd` の target 名、試験の log の印（main の判断「当面は変えない」）、X の core font の名前 `zed-unicode`（retro の内部の名前）。

## 2. 今の状態

### 2.1 済んだもの

| Phase | 内容 | 証拠 |
| --- | --- | --- |
| p002 | `__ZEDBSD_*`→`__KERN_*`、`ZEDBSD_*`→`KERN_*`（header の guard・UAPI）、`zbl_uefi_zedbsd_config*`→`zbl_uefi_kern_config*` 等 | ws.md の表（2026-09-28、amd64 の image と boot test PASS、pcat・rpi4 の build は未実施） |
| p003 | `userland/desktop/` への移動と実行ファイルの改名 | [phase003](phase003/phase.md)（6d8ca152） |
| p006 | data の path（`/etc/keiland`・`/usr/share/keiland`・`keiland*.ttf`・`/usr/libexec/keiland-x11`）、`keiland_`・`KEILAND_`・`keiland.h`、`zed_*_v1`→`keiland_*_v1` | ws.md の表（35177e46） |
| p004（一部） | boot の logo（Kei）、画面の文字列の多く、User-Agent、vendor、hostname の既定 `kei`（`userland/base/getty/main.c:96`） | ws.md の表（b3f5c5f9）。全画面の起動画面は ws035-p107（`plan/ws035/kei-identity-design.md` の段階 2） |
| ws035-p121 | wl_output の名前、Vulkan の application 名、usage、xkb の節の名前 | `plan/ws035/phase121/phase.md` |

### 2.2 開いているもの

| 項目 | 状態 |
| --- | --- |
| p001（棚卸し） | planning のまま。棚卸しは ws.md の表とこの手引きの §2.3 で済んでいる。**main に提案: p001 を canceled（理由: ws.md と guide.md §2.3 で代わった）にする**（Awesome Plan §3、履歴は残す） |
| p004 | incomplete。残り: §2.3 の A（見える文字列）と graphical な表示の確かめ。2026-10-01 に [phase004](phase004/phase.md) を作り手順を書いた |
| p005 | planning。全 Phase の後 |
| retro の program の名前（zterm・zwm・zgears・zshell・Xzed・zedinst、`userland/retro/`） | **ユーザーの判断待ち**（ws.md の p004 の行）。変えない |
| `TERM=zed` | main の判断: 低い優先度。名前を決めるのは main かユーザー（§3 の p009 の案） |
| 古い p069 の demo（`plan/ws035/demo/zdesktop`・`run-zdesktop.sh`・`build-demo-image.sh`） | 廃止の検討（main）。使う物: `plan/ws031/tests/vkloop-hw.sh:174`・`179`、`plan/ws031/tests/zdesktop/` |

### 2.3 棚卸し（2026-10-01、main の checkout。command は §5.1）

**A. 画面・文書に出る旧名（p004 の残り）**

| 所 | 行 | 中身 | 依存する試験 |
| --- | --- | --- | --- |
| `userland/desktop/browser/data/start.html` | 5・26・27・33・34・59（6 行） | `<title>zedBSD Browser</title>`、`<h1>zedBSD Browser</h1>`、「The Web browser of the zedBSD desktop」、「written for zedBSD」、「in a zdesktop window」、footer「zedBSD · browser」 | **`plan/ws074/tests/browser-start.sh:62`** が `title=zedBSD Browser` を待つ（一緒に直す。WS074 の file なので main が当てる） |
| `docs/reference/kernel-boot-parameters.md` | 325 | `login=graphical  the greeter (zdesktop --greeter) on the display` | なし |
| `include/libc/wayland/API-PROVENANCE.md` | 69〜71 | 「zed-gpu-buffer-v1-client-protocol.h」「Other zdesktop clients」「zdesktop extension」 | なし。**WS104 p001 がこの directory を `userland/desktop/keiland/wayland/` へ `git mv` する**ので、WS104 p001 の後に新しい path で直す |

画面に出る文字列（C の文字列の literal）に `zdesktop` は 0 行、「Keiland」も 0 行（2026-10-01）。`zedBSD` の literal は上の「残すと決まったもの」と試験だけ。
Settings の検索の語 `"version kernel release zedbsd operating system"`（`userland/desktop/settings/search.c:82`）は利用者が「zedbsd」と打っても About が出る
ための語で画面には出ない。残してよい（変えるなら main の判断）。

**B. 内部の旧名（画面に出ない）**

| # | 種類 | 数（行 / file） | 所 | 扱い |
| --- | --- | --- | --- | --- |
| B1 | 注釈の `zdesktop`（plan の外） | **678 / 175**（C の注釈 608 行、他は Makefile・mk・sh・shader・md） | `userland/desktop` 582 / 153（内訳: wayland 153/34、files 132/22、settings 62/13、libkeiland 30/8、ime 29/7、imageview 29/6、textedit 26/6、browser 26/11、libwayland 23/16、terminal 22/8、pdfviewer 18/4、libkeiui 16/9、sessiond 8/3、他）、`include` 36/4（`keiland.h` 27）、`platform/amd64/vmunix.mk` 36、`userland/base` 23/16（試験の probe の Makefile・`etc/rc.conf:12`）、`docs` 1 | p007（提案） |
| B2 | make の変数の `ZDESKTOP` | 28 行: `platform/amd64/vmunix.mk` の `DYNAMIC_ZDESKTOP_*_OBJS` 25 行、`userland/desktop/libkeiland/Makefile` の `LIBZDESKTOP_SOURCES`・`LIBZDESKTOP_IMAGE_DATA` 3 行 | build の内部。D6 は `ZEDBSD_` だけを残すと決めた（`ZDESKTOP` は別） | p008（提案） |
| B3 | protocol の header の file 名 `zed-*-v1-client-protocol.h` | 6 file（`userland/desktop/libwayland/zed-{edit,glass,gpu-buffer,ime-status,keyboard-inset,titlebar}-v1-client-protocol.h`）、include 16 行 / 14 file、文書 1 行。中身は既に `keiland_`（p006） | `userland/desktop/libwayland`・`libkeiland`・`libvulkan/wsi-wayland.c`・`ime/program.h`・`userland/base/tests/{acquire-fence,gpu-forge,titlebar-probe}` | p008（提案）。`ime/program.h` は IME（人間が作業中） |
| B4 | 「the zedBSD desktop」等の注釈 | 10 / 10 | `browser/main.c:9`、`files/files.h:9`、`files/main.c:9` ほか | p007（提案、B1 と一緒） |
| B5 | 試験の log の印（source の中） | `"ZWL ` 444、`"ZTERM ` 46、`"ZBROWSER ` 31、`"ZFILES ` 24、`"ZSETTINGS ` 11（literal の数） | `userland/desktop/*` | **当面は変えない**（main）。変えるなら試験と同時（下） |
| B6 | 試験の script の印と `/tmp/zdesktop.log` | plan の script（.sh・.py・.c）で `ZWL` 1126 行 / 182 file、`ZFILES` 344 / 28、`ZTERM` 153 / 20、`ZBROWSER` 110 / 16、`ZSETTINGS` 77 / 8。`zdesktop.log` 1065 行 / 175 file。`zdesktop`（大小無視）2216 行 / 272 file、file 名に `zdesktop` 91 個 | `plan/` | 当面は変えない。plan の記録（`plan/history`・phase.md）は書き換えない |
| B7 | `TERM=zed` と terminfo の entry | `userland/base/sh/main.c:481`、`userland/base/terminfo/zed.zti`（longname「zedBSD virtual console」）、`terminfo/Makefile:13` | console の端末の種類。Terminal の app は子に `TERM=xterm`（`userland/desktop/terminal/main.c:766`） | p009（提案、名前の判断が要る） |
| B8 | retro の名前 | `userland/retro/{xzed,zedinst,zgears,zshell,zterm,zwm}`、`"zedBSD Xzed"`（`userland/retro/xzed/main.c:1641`）、App Home の X の app の command（`userland/desktop/wayland/home.c:895`・`896` の `/bin/zterm`・`/bin/zgears`。画面の label は「X terminal」「Gears」） | ユーザーの判断待ち | 変えない |
| B9 | `keiland-x11` の注釈の「zdesktop-x11」 | `userland/desktop/wayland/keiland-x11:2`、`wayland/Makefile:22` | B1 と一緒 | p007（提案） |

## 3. 次の作業の順番

master の優先: WS099・WS079・WS090・WS089・WS094・WS100・**WS078**・WS102。1 番は WS104・WS105（Linux への移植）。2026-10-10 ごろからは bug と実機の調整だけ。
ws.md の方針: 「改名は作業中の agent と衝突しやすい。他の agent が merge を終えた静かな時点で main か 1 つの agent が一度に行い、その後に各 agent へ新しい名前を知らせる」。

**安全な順番**:

| 順 | Phase | 目的 | いつ | 受け入れ |
| --- | --- | --- | --- | --- |
| 1 | [ws078-p004](phase004/phase.md) の残り | §2.3 A の start.html と docs の 1 行、graphical な表示の確かめ | **今できる**（小さい。browser の start.html は WS074 の作業と同じ directory なので、WS074 の worktree の有無を main が確かめる） | browser-start PASS（新しい title）、画面の写真、boot test |
| 2 | ws078-p001 | canceled の提案（§2.2） | main の判断 | — |
| 3 | ws078-p007（提案） | 注釈の `zdesktop`（B1・B4・B9）を「the compositor」等へ一度に置き換える | **WS104 の p001〜p007 が main に入った後**（WS104 の patch は `zdesktop` を含む文脈の行を持つ: `plan/ws104/patches/p001-paths.patch`・`p003.patch`・`p003-after-gitmv.patch`・`p007.patch`。先に置き換えると patch が当たらない）。かつ userland/desktop を変える worktree が無い時 | object が byte で同じ（注釈だけ）、build の warning 0、boot test |
| 4 | ws078-p008（提案） | make の変数（B2）と protocol の header の file 名（B3） | p007 の直後（同じ静かな時点）。IME の `ime/program.h` の 1 行は IME の作業が人間から戻った後か、main がユーザーに確かめてから | build の warning 0、boot test、WS099 の C9 |
| 5 | ws078-p009（提案、判断待ち） | `TERM=zed` と terminfo の entry の名前（B7） | main かユーザーが新しい名前を決めた後 | sh の試験、console の less・vi の表示 |
| 6 | ws078-p005 | 全文の規約と回帰（WS の最後） | 全ての後 | §5.4 |

保留（Phase にしない）: B5・B6（試験の log の印。変えるなら「全試験と同時の別の Phase」と main が決める）、B8（retro、ユーザーの判断）、古い p069 の demo（main）。

提案の Phase の詳細:

| ID（提案） | 目的 | 触る file | 受け入れ |
| --- | --- | --- | --- |
| ws078-p007 | B1・B4・B9 の注釈の置き換え。語の既定: 「zdesktop」→「the compositor」（文脈が所有格なら「the compositor's」）、「zedBSD desktop」→「the desktop」、「zdesktop-x11」→「keiland-x11」。shader（`userland/desktop/wayland/shaders/*.frag`・`*.vert`）の注釈も変えてよいが `regenerate.py` は流さない（`shaders.h` の SPIR-V は注釈を含まない）。IME（`userland/desktop/ime/`、29 行）は除く | §2.3 B1 の file（plan の外） | `git diff --stat` の行数が置き換えの数と同じ（行を足し引きしない）、前後の object の比較（§5.3）、build の warning 0、boot test |
| ws078-p008 | B2: `DYNAMIC_ZDESKTOP_*_OBJS` → `DYNAMIC_<PROGRAM>_OBJS`（例 `DYNAMIC_WAYLAND_PROGRAM_OBJS`・`DYNAMIC_TERMINAL_OBJS`）、`LIBZDESKTOP_*` → `LIBKEILAND_*`。B3: `git mv` で `zed-*-v1-client-protocol.h` → `keiland-*-v1-client-protocol.h`、include の 16 行と文書 1 行 | `platform/amd64/vmunix.mk`、`userland/desktop/libkeiland/Makefile`、§2.3 B3 の file | build の warning 0、boot test、WS099 の C9、gpu-boundary の試験（`gpu-zedbsd` と WSI が `zed-gpu-buffer` を読む） |
| ws078-p009 | B7: 新しい名前（案「kei」）の terminfo の entry（`zed.zti` を `git mv`、longname「Kei virtual console」）、`sh/main.c:481` の既定 | `userland/base/sh/main.c`、`userland/base/terminfo/` | sh の試験（`plan/tools/sh/`）、SSH の guest で `echo $TERM`・`tput clear`・`less`（S10 の確かめと同じ） |

## 4. 未知と調べ方

| 未知 | なぜ重要 | 調べ方 |
| --- | --- | --- |
| graphical な起動での Kei の logo・greeter・system bar・About の表示 | p004 の受け入れ（「graphical な起動での表示の確認は未実施」） | §5.2 の Venus の guest で画面を撮る（`plan/ws035/tests/zdesktop-check.py` は guest の画面の PNG を QMP・VNC で撮る）。起動の途中の loader の logo は QEMU では撮りにくいので、5330 の実機の写真（ユーザー）で確かめる |
| 注釈の置き換えで object が変わらないこと | p007 の安全の確かめ | §5.3 の前後の object の比較。`-g` が有れば行番号が変わらないこと（1 行を 1 行で置き換える）で DWARF も同じ |
| browser の start page を見る試験の数 | start.html の title を変えると壊れる試験 | `git grep -n "zedBSD Browser" -- plan ':!plan/history'`（2026-10-01: `plan/ws074/tests/browser-start.sh:62` の 1 つ） |
| WS105（Linux）が `zed-*-v1` の file 名や `DYNAMIC_ZDESKTOP_*` を参照するか | p008 の衝突 | p008 の前に `git grep -n -E 'zed-[a-z-]+-v1|ZDESKTOP' -- plan/ws105 plan/ws104 userland toolchain` を流し、WS104・WS105 の patch・設計に出れば main と順を決める |
| pcat・rpi4 の build（p002 で未実施） | p005 の回帰の範囲 | p005 で `make -n ZEDBSD_CONFIG=<pcat の config>` を確かめ、build を 1 回（image の build は同時に 1 つ）。config の名前は `config/ci/` を見る |

判定に QEMU の console・serial の log を使わない（AGENTS.md）。画面は PNG、guest の中は SSH。

## 5. コマンド（repo の root から。`<W>` は作業の名前、例 `ws078-p004`）

共通の決まりは [plan/ws104/commands.md](../ws104/commands.md) の §0・§1（build と warning）・§4（boot test）・§5（WS099 の基準）・§6（GPU の境界の試験）。

### 5.1 棚卸しの command（数え直し）

```
git grep -I -c -i 'zdesktop' -- ':!plan' ':!.internal' | awk -F: '{s+=$2;f++}END{print s" lines in "f" files"}'
for d in userland/desktop/wayland userland/desktop/files userland/desktop/settings userland/desktop/libkeiland userland/desktop/ime userland/desktop/imageview userland/desktop/textedit userland/desktop/browser userland/desktop/libwayland userland/desktop/terminal userland/desktop/pdfviewer userland/desktop/libkeiui userland/desktop/sessiond userland/desktop include platform userland/base docs; do printf "%-32s %s\n" $d "$(git grep -I -c -i zdesktop -- $d | awk -F: '{s+=$2;f++}END{print s+0" / "f+0}')"; done
git grep -I -h -i 'zdesktop' -- ':!plan' ':!.internal' '*.c' '*.h' | awk '{ if ($0 ~ /^[ \t]*(\/\*|\*|\/\/)/) c++; else o++ } END{print "comment-lines",c+0,"other-lines",o+0}'
git grep -I -n 'ZDESKTOP' -- ':!plan' ':!.internal'
git ls-files | grep -v '^plan/' | grep -iE 'zed' | grep -v '^userland/retro'
git grep -I -n -E 'zed-[a-z-]+-v1' -- userland include platform tools ':!userland/retro'
git grep -I -n -iE 'zedbsd|zdesktop' -- userland/desktop/browser/data docs include/libc/wayland/API-PROVENANCE.md
git grep -I -n 'zedBSD' -- 'userland/*.c' 'userland/*.h' | grep -vE ':[0-9]+:[[:space:]]*(/\*|\*|//)' | grep '"'
git grep -I -n -E 'TERM=zed|"zed"' -- userland src
for m in ZWL ZFILES ZTERM ZBROWSER ZSETTINGS; do printf "%s src=%s plan-scripts=%s\n" $m "$(git grep -I -c -w "$m" -- userland | awk -F: '{s+=$2}END{print s+0}')" "$(git grep -I -c -w "$m" -- 'plan/*.sh' 'plan/*.py' 'plan/*.c' ':!plan/history' | awk -F: '{s+=$2}END{print s+0}')"; done
```

- 2026-10-01 の値: 1 行目 `678 lines in 175 files`、3 行目 `comment-lines 608 other-lines 0`（C の file の `zdesktop` は全て注釈）。他は §2.3 の表。
- `.internal/` は読まない（AGENTS.md）。git grep の対象から外してある。

### 5.2 p004 の確かめ（browser の start page と画面）

```
mkdir -p build/<W>
sh plan/ws074/tests/build-browser-image.sh build/<W>-browser > build/<W>/browser-build.log 2>&1; echo "exit=$?"
export GUEST_RUNTIME=$PWD/build/<W>-browser-run
sh plan/ws074/tests/browser-guest.sh start build/<W>-browser/hdd-image.img
sh plan/ws035/tests/zdesktop-guest.sh wait --timeout 240
sh plan/ws074/tests/browser-start.sh build/<W>/browser-start
sh plan/ws035/tests/zdesktop-guest.sh stop
unset GUEST_RUNTIME
```

- PASS: 最後の行 `browser-start: PASS (build/<W>/browser-start)`（`browser-start.sh:80`）。`start.png` で title と見出しを目で確かめる。
- `build-browser-image.sh`・`browser-guest.sh` は引数を省くと build/amd64（`build-browser-image.sh:13`・`browser-guest.sh` の 8 行の注釈）。必ず渡す。
- `browser-start.sh` の runtime の既定は `build/ws074-run`（14 行）。WS074 の agent と衝突しないよう GUEST_RUNTIME を渡す。

system bar・Settings の About の画面（WS099 の基準の image。Notes・Settings 入り、graphical な login で kei が起動時に login する
（`plan/ws100/tests/config-amd64-volume.mk` の注釈）。About の頁は Settings の引数 PAGE（`userland/desktop/settings/main.c:121`、頁の名前 `about` は `pages.c:48`）で、
kei の session の環境での起こし方は `plan/ws100/tests/volume-p005.sh:115` と同じ形）:

```
sh plan/ws099/tests/build-criteria-image.sh build/<W>-criteria > build/<W>/criteria-build.log 2>&1; echo "exit=$?"
export GUEST_RUNTIME=$PWD/build/<W>-run
sh plan/ws035/tests/zdesktop-guest.sh start build/<W>-criteria/hdd-image.img
sh plan/ws035/tests/zdesktop-guest.sh wait --timeout 240
sleep 30
python3 plan/ws035/tests/zdesktop-check.py build/<W>/session.png --runtime "$GUEST_RUNTIME"
python3 plan/tools/guest/guest.py run 'export XDG_RUNTIME_DIR=/run/user/1000 HOME=/home/kei; /bin/settings --timeout-s=600 about > /tmp/s.log 2>&1 </dev/null & sleep 6; echo started'
python3 plan/ws035/tests/zdesktop-check.py build/<W>/about.png --runtime "$GUEST_RUNTIME"
sh plan/ws035/tests/zdesktop-guest.sh stop
unset GUEST_RUNTIME
```

- PNG（`session.png`: 壁紙・system bar、`about.png`: About の頁）をユーザーに見せる。`sleep 30` は session が上がるまでの待ち（`feedback-latency.sh` は
  起動の後 35 秒待つ）。greeter の画面は WS099 の C1（`criteria.sh … C1`、commands.md §5。greeter → login → session → Log Out → Shut Down）の画面
  （`build/<W>/criteria/` の下）を使う。
- 起動の途中の loader の Kei の画面は QEMU では撮っていない（§4）。

### 5.3 注釈だけの置き換えの確かめ（p007）

同じ BUILD の directory で前と後を作り、object を比べる（BUILD を分けると、生成した header の path などで違いが出うるため）。

```
mkdir -p build/<W>
make -j64 ZEDBSD_CONFIG=plan/ws099/tests/config-amd64-criteria.mk BUILD=build/<W>-cmp disk-image > build/<W>/before.log 2>&1; echo "exit=$?"
rm -rf build/<W>/before-objects && mkdir -p build/<W>/before-objects
(cd build/<W>-cmp && find . -name '*.o' -print0 | cpio -0pdm ../<W>/before-objects 2>/dev/null)
# ここで置き換え（sed など）。1 行を 1 行で置き換え、行を足し引きしない
git diff --numstat | awk '{a+=$1; d+=$2} END {print "added="a, "deleted="d}'
make -j64 ZEDBSD_CONFIG=plan/ws099/tests/config-amd64-criteria.mk BUILD=build/<W>-cmp disk-image > build/<W>/after.log 2>&1; echo "exit=$?"
(cd build/<W>/before-objects && find . -name '*.o' | LC_ALL=C sort) > build/<W>/objects.txt
while read -r o; do cmp -s "build/<W>/before-objects/$o" "build/<W>-cmp/$o" || echo "DIFF $o"; done < build/<W>/objects.txt | tee build/<W>/object-diff.txt | wc -l
```

- PASS: 2 回の build が `exit=0`、`added` と `deleted` が同じ数、最後の数が `0`（object が byte で同じ）。差が出たら `object-diff.txt` の file を調べる
  （注釈の外を変えていないか `git diff` を見る）。
- `<W>` の中の `/` は使わない（`../<W>/before-objects` の相対 path のため）。`cpio` が無ければ `rsync -a --include='*/' --include='*.o' --exclude='*' build/<W>-cmp/ build/<W>/before-objects/`。
- image の build を 2 回する（2 回目は変えた file の分だけ compile）。image の build は他と同時にしない。

### 5.4 回帰の組

| Phase | 流すもの |
| --- | --- |
| p004 | §5.2、boot test |
| p007 | §5.3、boot test |
| p008 | build（warning 0）、boot test、WS099 の C9（commands.md §5）、GPU の境界の試験（commands.md §6。`zed-gpu-buffer` の header を読む WSI と gpu-forge・acquire-fence の probe） |
| p009 | sh の試験（`plan/tools/sh/`、使い方は各 script の先頭）、SSH の guest で `echo $TERM` |
| p005 | 上の全て、`make menuconfig-host-test`（BUG-080 の分類の確かめ）、pcat・rpi4 の build（1 つずつ） |

boot test:

```
OUTPUT=build/<W>/boot plan/tools/boot-test.sh build/<W>-criteria/hdd-image.img; echo "exit=$?"
```

- PASS: `exit=0`、`boot-test: PASS build/<W>/boot/login.png`。PNG をユーザーに見せる。

## 6. 実機（Dell Latitude 5330）

一般の手順は [plan/tools/hw5330/README.md](../tools/hw5330/README.md)（2026-10-01）。素の 5330 の起動とユーザーへの頼み方は README §2、demo の image は §3。
WS078 に固有の確かめ（p004 の graphical な表示。ユーザーの目）:

| # | 確かめ | 判定 |
| --- | --- | --- |
| H1 | 電源から loader の Kei の起動画面（全画面、spinner） | 「Kei」の 3 文字の印。「zedBSD」が大きく出ない（副題の「powered by zedBSD」は残す決定） |
| H2 | greeter・lock・system bar | 「Kei」。「zedBSD」「zdesktop」「Keiland」が出ない |
| H3 | Settings の About | 「Kei」と「powered by zedBSD」 |
| H4 | Browser の start page（p004 の後の image） | title・見出しに「zedBSD」が無い |
| H5 | Terminal の `uname -a` | sysname は「zedBSD」のまま（残す決定）。hostname は `kei` |

## 7. 注意

- **改名は 1 つの agent が静かな時点で一度に**（ws.md）。始める前に `git worktree list` と master の割り当てで、`userland/desktop`・`platform/amd64/vmunix.mk` を変える
  作業が無いことを main が確かめる。終わったら main が各 agent に新しい名前を知らせる。
- **WS104 の patch との衝突**: `plan/ws104/patches/p001-paths.patch`・`p003.patch`・`p003-after-gitmv.patch`・`p007.patch` は `zdesktop` を含む行を文脈に持つ
  （`git grep -c -i zdesktop -- plan/ws104/patches` で 3・4・4・1 行）。p007・p008 は WS104 の p001〜p007 が入った後にする。WS104 p001 は `include/libc/keiland.h` と
  `include/libc/wayland/`（API-PROVENANCE.md を含む）を `userland/desktop/keiland/` へ移すので、§2.3 の path はその後に変わる。
- **IME（`userland/desktop/ime/`）は人間が作業中**（master 2026-09-30）。注釈の置き換えと header の改名で `ime/` を除き、残りを main に記録する。
- **WS074 は動いている**（2026-09-30「Run ws074」）。`userland/desktop/browser/` と `plan/ws074/tests/browser-start.sh` を変える時は main が WS074 の worktree の有無を確かめる。
  `browser-start.sh` は WS074 の file なので、subagent は変えず main に頼む（AGENTS.md「subagent の修正可能範囲」）。
- plan の記録（`plan/history/`、各 phase.md・ws.md の過去の文、試験の file 名）は書き換えない（ws.md「plan/history と plan/ws078・master.md の決定の文は書き換えていない」）。
- toolchain（`toolchain/`、`userland/base/noct/` の patch、`build/llvm` ほか）の `zedbsd` の名前は変えない（D6、AGENTS.md「toolchain は main の許可なしに変えない」）。
- HAL の API（`include/hal/hal.h`）の名前を変える時は差分ごとの事前承認が要る（AGENTS.md）。`include/hal/hal.h` の旧名は先頭の copyright の header の「zedBSD」だけ（残すもの）。
- image の build は同時に 1 つ、BUILD を必ず渡す、build/amd64 を使わない、boot test の OUTPUT は専用（commands.md §0）。
- 判定に QEMU の console・serial の log を使わない。push しない。commit は `git commit -m WIP -- <自分の path>`。
