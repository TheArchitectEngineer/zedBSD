<!-- awesome-plan project=zedbsd record=ws110-p001 -->

# ws110-p001: 通常と試験の role、引数の契約、変える範囲

Status: cleared（2026-10-05 Q1: host の試験 16 case と T1-168 の roles-guest PASS（引数なし・--session が role=normal で期限なし、--testing が 150 秒、拒む組み合わせが exit 2））。以前: in-progress（実装済み。zedBSD の compositor の build と guest の確かめは p002 の T1 とまとめる）
Disposition: normal
Parent: [WS110](../ws.md)
Queue: Q1（2026-10-05、P2。設計から）
設計の元: [design.md](../design.md)（2026-10-02 の検討）

## 今の code（2026-10-05 に読んだ、main e8d3db79 の後）

- `userland/desktop/wayland/main.c`:
  - 既定は `timeout_ms = 150000`（150 秒で終わる）。
  - `--timeout=N`（1〜86400 秒）は `timeout_ms` を書く。`--max-frames=N` は frame の数の上限。
  - `--session` と `--greeter` は `timeout_ms = UINT64_MAX` を書く。引数の順で結果が変わる: `--session --timeout=900` は 900 秒で終わる session、`--timeout=900 --session` は期限なし。
- `server->session` で変わる物は 3 つだけ:
  1. lock の idle の既定 10 分（main.c:121）。
  2. Files の desktop の client を既定で起動する（desktop.c:708）。
  3. App Home の Log Out（と、sessiond が管理する時の Lock Screen）（home.c:948）。
- 製品の起動の側は全て `--session`（または `--greeter`）を付けている:
  - zedBSD: `userland/desktop/sessiond/session.sh`（`--session`）と `sessiond/greeter.c`（`--greeter`）。
  - Linux・FreeBSD: `userland/desktop/wayland/data/keiland.desktop`（`--session`）、`keiland-desktop.in`（`plan/tools/keiland-launcher/check.py` が `--session` を期待）、`tools/release/keiland-linux-deb/run.py`（`--session`）。

## 引数の契約（案）

| 起動 | 意味 |
| --- | --- |
| `wayland`（role の指定なし） | 通常の session。期限なし、lock の idle 10 分、Files の desktop、Log Out |
| `wayland --session` | 上と同じ（互換の別名。今の製品の起動を壊さない） |
| `wayland --testing` | 試験。既定 150 秒で終わる。session の 3 つの機能は無い（今の「何も付けない」の動き） |
| `wayland --testing --timeout=N` / `--max-frames=M` | 試験の上限。値の範囲は今と同じ |
| `wayland --greeter --auth-fd=N` | login 画面。今のまま（期限なし） |

- **`--timeout`・`--max-frames` は `--testing` を要る**: 付けずに使うと usage で止まる（exit 2）。暗黙に試験へ移すと、直し忘れた試験が気づかれず session（Files の desktop・10 分の lock）で動くので、大きな音で止める。
- **矛盾は拒む**: `--testing` と `--session`・`--greeter`・`--control-fd`・`--lock-idle` の組み合わせは exit 2。
- **順に依らない**: 引数を全部読んでから、role と期限を 1 度決める（今の「読む途中で `timeout_ms` を書き換える」をやめる）。
- `--log-frames`・`--width`・`--height`・`--socket`・`--glass`・`--wallpaper`・`--desktop-client` などは、どの role でも使える。
- `ZWL READY … timeout_ms=…` の行に `role=normal|testing|greeter` を足す（試験が role を確かめられるように）。

## 変える範囲（Q1 に決めてもらう）

| 区分 | 数（2026-10-05、`git ls-files` を grep、`plan/history`・`plan/uat`・evidence を除く） | 直し |
| --- | --- | --- |
| compositor の source | `main.c`（引数の読み、role の決め、usage、READY の行）、`zwl.h`（`testing` の項目と説明） | 手で |
| 製品の起動 | session.sh・greeter.c・keiland.desktop・keiland-desktop.in・run.py（5） | 変えない（`--session` は別名として残る）。p002 で点検だけ |
| 試験の起動で、1 行に `/bin/wayland --timeout=` の形 | 232 行（約 220 file） | 機械で `/bin/wayland --testing --timeout=` に。`plan/ws035/tests` 46、`plan/tools` 38、`ws099` 18、`ws079`・`ws068` 16 ずつ、`ws089`・`ws074` 15 ずつ、他 |
| 試験の起動で、上の形でない物 | 14 行: Python の argv の list（`plan/tools/keiland-freebsd/*-freebsd.py` 2、`ws014/tests/wayland-qemu.py`、`ws099/tests/p017-*.py` 2）、変数（`ws099/tests/p020-late-request.sh` の `$wayland`）、`--log-frames --timeout=` の順（`ws099/phase017/p076-framed.sh`）、boot の script（`ws031`・`ws035/demo`・`ws075`・`ws101` の `run-zdesktop*.sh` と `zdesktop` の `arguments=` 4） | 手で 1 つずつ |
| `--session` を付けた試験 | 9 行（ws035 の p095・p102、ws114 の start-session 3、keiland-launcher の check.py ほか） | 変えない（別名） |
| 履歴・証拠（`plan/history`・`plan/uat`・`evidence`・`checkpoint*.json`） | — | 変えない |

- **決めが要る点**:
  1. **(A) `--testing` を要る（上の案、推奨）** か、**(B) `--timeout`・`--max-frames` を付けたら暗黙に試験**か。(B) なら試験の script はほとんど変えずに済む（約 240 行の機械の置き換えが要らない）。しかし 2026-10-02 のユーザーの指示は「--testingをつけると開発モードにするように変更。関連するテストスクリプトも修正する」で、(A) に当たる。
  2. ws.md の記録では、実装は「ユーザーへ `--session` の元の意味と推奨の `--testing` の仕様を説明し、実装の指示待ち」になっている。Q1 の「設計から進めて」がユーザーの実装の指示に当たるかを確かめたい。当たらなければ、ここまでを設計として止める。
  3. 機械の置き換えは約 220 file・他の WS の試験に及ぶ（AGENTS.md の subagent の修正可能範囲の外）。WS110 の範囲として Q1 が許すか。許すなら p002 を 1 回の commit（置き換えの script と、その前後の grep の数の記録つき）にする。
- **T1 の試験**: 置き換えの後、代表の試験だけを流す（全部は流さない、2026-10-03 の試験の方針）。案: files-p002（`--testing --timeout`）、zdesktop-p095（greeter → session、`--session` の別名）、引数の契約の host の試験（引数なし・`--session` が 150 秒で終わらない、`--testing` の既定と順、矛盾の拒否）。

## Phase の分け方（案）

- **p001**（この Phase）: 契約の確定と `main.c`・`zwl.h` の変更、引数の契約の host の試験（compositor を `--testing --max-frames=1` などで起動せずに済む形を探す。無理なら parse の関数の単体の試験）。
- **p002**: 試験の script の置き換え（機械の 232 行＋手の 14 行）と製品の起動の点検、T1 の代表の試験。
- **p003**: 全文規約と回帰（ws.md の T4）。

## ユーザーの決定（2026-10-05 朝）

「今実装する」: (A) --testing を必須（--timeout・--max-frames は --testing が要る）、他の WS の試験の script 約 220 file の機械の置き換えと手の 14 行を WS110 の範囲として 1 回の commit で（置き換えの script と前後の grep の数を付ける）。

## ユーザーの決定（2026-10-05、Q1 経由）

- (1) A: `--testing` を必須にする。`--timeout`・`--max-frames` は `--testing` が要る。組み合わせは拒む。role は引数の順に依らない。READY の行に `role=` を足す。
- (2) 実装の指示あり。
- (3) 他の WS の試験の約 220 file の機械の置き換えと、手の 14 行を WS110 の範囲として許す。置き換えの script と前後の grep の数を付けて 1 回で commit する。main の最新に合わせてから一気に行う。

## 実装（2026-10-05、P2）

- 新しい `userland/desktop/wayland/role.c`・`role.h`: `zwl_role_resolve`（options の要求から role と期限を 1 度決める。拒む時は理由の文）と `zwl_role_name`。server を知らないので、host の試験で単独で走る（`pointer-accel.c` と同じ形）。3 つの Makefile（zedBSD・Linux・FreeBSD）に足した。
- `main.c`: `--testing` を足した。`--timeout`・`--max-frames`・`--greeter`・`--session`・`--control-fd`・`--lock-idle` は要求に印を付けるだけにして、読み終えた後に `zwl_role_resolve` で決める（`server->role`・`timeout_ms`・`session`）。拒む時は `wayland: <理由>` と usage を出して exit 2。既定の `timeout_ms = 150000` は消した（試験の既定は role.h の `ZWL_ROLE_TESTING_TIMEOUT_MS`）。READY の行の末尾に ` role=normal|testing|greeter`（`pid=(\d+)` を読む既存の試験はそのまま読める）。usage を直した。
- `zwl.h`: `role` の項目と、session の説明の直し。
- 確かめ（host）:
  - `sh plan/ws110/tests/run-host-role.sh`: 16 case、`host-role: PASS`（引数なし・`--session`・sessiond の組み合わせが normal で期限なし、`--testing` 150 秒、`--testing --timeout` 900 秒、`--greeter`、拒む物 9 つ）。
  - Linux の Keiland（`make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/p2-keiland-linux all`）: warning 0（-Werror）。その compositor に拒む引数 6 通りを渡し、全て理由の 1 行と exit 2（backend を開く前に止まる）。
  - style: role.c・role.h・host-role.c は違反 0。
- 未実施: zedBSD の compositor の build。main を取り込んだ後、`userland/desktop/keiland/wallpapers/` の file が sysroot の stamp より新しく、`make -n` の確かめが 1 になる（sysroot が作り直される）。subagent は sysroot を作らないので、Q1 に相談した。FreeBSD の build も未実施（native の FreeBSD が要る）。

## Q1 の判定（2026-10-05）

host の試験 16 case と T1-168 の roles-guest PASS（引数なし・--session が role=normal で期限なし、--testing が 150 秒、拒む組み合わせが exit 2）。**cleared**。
