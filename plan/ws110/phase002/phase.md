<!-- awesome-plan project=zedbsd record=ws110-p002 -->

# ws110-p002: 試験の起動に --testing を付ける、代表の試験

Status: in-progress（置き換えは済み、T1 の代表の試験待ち）
Disposition: normal
Parent: [WS110](../ws.md)
Queue: Q1（2026-10-05）
依存: [p001](../phase001/phase.md)（role.c と main.c）

## 範囲（2026-10-05 ユーザーの許可、Q1 経由）

他の WS の試験を含めて、tree が追う試験の起動のうち、`--timeout`・`--max-frames` を付けて `--testing` の無い物に `--testing` を足す。置き換えの script と、前後の数を付けて 1 回で commit する。履歴（`plan/history`・`plan/uat`・evidence・JSON の checkpoint）と、cleared の Phase の記録の中の command は変えない。

## 置き換え（2026-10-05、P2。1 回目の 811f55ef は main の WS138 p002 の wallpaper の置き換え（08478c20、196 file）と衝突して Q1 が merge を中止。main b3d5e68d の上で、試験の script は main の版のまま script を流し直した）

- `plan/ws110/tests/add-testing.py`: `git ls-files` のうち履歴と文書（`.md`・`.txt`・`.log`・`.json`）を除いた file で、compositor の起動の行を 3 つの形で探して `--testing` を足す。
  1. command の行: `/bin/wayland`・`/opt/keiland/bin/wayland`・`$wayland` の後の同じ行に `--timeout=` か `--max-frames=` がある物。`--testing` は program の直後に入れる。
  2. Python の argv の list: `str(prefix/'bin/wayland'),` の後に `'--testing',` を入れる。
  3. zdesktop の service の file の `arguments=` の行: 先頭に `--testing` を入れる。
- 結果: **233 file・246 行**（`add-testing: files=233 starts before=246 after=0`、一覧は commit の時の出力）。
  - 手の 14 行に当たる形（Python の argv 2、`wayland-qemu.py`、`p017-*.py` 2、`$wayland`、`--log-frames --timeout=` の順、boot の script 4、`arguments=` 2）も、script の 3 つの形で入った。diff で 1 行ずつ読んで確かめた。
  - 置き換えの後、`.sh` は全て `sh -n`、`.py` は全て `py_compile` を通った。`git diff --check` も問題無し。
- 文書で、今の手順として使う物を 2 つ手で直した: `userland/tests/wltest/README.md`（起動の例）、`plan/ws138/phase002/phase.md`（planned の Phase の command）。cleared の `ws131/phase006` と、`ws014`・`ws110` の設計の記録は変えていない。
- 製品の起動（session.sh・greeter.c・keiland.desktop・keiland-desktop.in・run.py）と、`--session` を付けた試験 9 行は変えていない（`--session` は別名）。
- `plan/ws035/demo/run-zdesktop.sh`（`--timeout=86400`、展示用）も `--testing` になった。今までと同じ動き（session の機能無し、1 日で終わる）を保つ。通常の session にするなら別の判断。
- 注意: 置き換えた試験は、WS110 の後の compositor が要る（前の compositor は `--testing` を知らず、引数の誤りで止まる）。T1 は WS110 の後の main で image を作る。

## build（host）

- zedBSD の compositor（`make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws140-p002 build/ws140-p002/bin/wayland`）: warning 0（role.c を含む）。Q1 の許可で worktree の sysroot を作り直してよいことになったが、この build では作り直しは走らなかった。
- Linux の Keiland: warning 0（p001）。FreeBSD は未実施（native の FreeBSD が要る）。

## T1 の代表の試験（依頼）

image は WS110 の後の main で `plan/tools/files/build-files-image.sh BUILD`。

1. `plan/ws110/tests/roles-guest.sh`: 引数なしと `--session` が role=normal・期限なし、`--testing` が 150 秒、`--testing --timeout=20` が 20 秒で自分で終わる、拒む 4 通りが exit 2 と理由。
2. `plan/ws127/tests/files-p002.sh`（置き換えた試験の代表、`--testing --timeout`。T1-166 の Move To の期待の直しも確かめる）。
3. `plan/ws035/tests/zdesktop-p095.sh`（greeter → `--session` の session → Log Out、login の image が要る。image が別なら後回しでよい）。

## 結果

（T1 の後に書く）
