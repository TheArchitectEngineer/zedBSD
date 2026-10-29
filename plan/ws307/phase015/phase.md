<!-- awesome-plan project=zedbsd record=ws307-p015 -->

# ws307-p015 画面の lock の既定を never にし、never でも lock される不具合を直す

- 親: [WS307](../ws.md)。Queue: q1674 / q1674-i01（担当 P5、2026-10-29、worktree p5-ws307lock、基点 main 55ee43590）。
- Status: in-progress
- ユーザーの指示（原文、2026-10-29）:「WS307でついでに行ってほしいのですが、デスクトップがロックされるデフォルトの時間の設定を、neverをデフォルトにしてほしいです。また、neverにしても、現在は実際にはロックされてしまうみたいなので、直しておいてほしいです。」
- 範囲（Q1）: idle の後に画面を lock する時間の既定を never に（Settings の頁、compositor の既定、設定の file の既定。file に値がある利用者はその値を保つ）。never でも lock される原因を探して直す。sleep（suspend）の既定は範囲の外（変えない）。

## 調べた事（基点 55ee43590）

- Settings に「lock までの時間」の設定は無かった。Never を選べるのは Power の頁の「Sleep after」の 2 つの slider（`power.sleep.ac` 既定 30 分・`power.sleep.battery` 既定 15 分、`settings-keys.c`）だけ。
- compositor は sleep とは別に、login session で 10 分の無操作の lock を固定で持っていた: `userland/desktop/wayland/main.c` の `MAIN_LOCK_IDLE_MS`（10 分）を `--lock-idle` が無い時に `lock_idle_ms` にし、`shell.c` の tick が `lock_idle_ms` を過ぎたら `kwl_lock(server, "idle")`。この値は設定の file を見ず、`--lock-idle` の他に変える手段が無い（sessiond も `--lock-idle` を渡さない）。
- **原因**: Sleep を Never にしても、この固定の 10 分の lock は残るので、10 分の無操作で lock される。
- 他に lock する道（`kwl_lock` の呼び手）: `seat.c` の Super+L（"key"）、`home.c` の App Home の Lock Screen（"home"）、`backend-host.c` の蓋（"lid"、開けると password 無しで外れる蓋の覆い）、`sleep.c` の sleep の前の lock（"sleep-idle"・"sleep-lid"・"sleep-rest"。sleep は lock の後でだけ始まる設計）。画面の消灯（sleep の時間の半分、`kwl_screen_off`）は lock しない。時間で lock するのは shell.c の idle の lock と、sleep の時間（既定 30・15 分）で始まる sleep の前の lock の 2 つ。後者は sleep の設定（Never なら起きない）に従い、この Phase では変えない（sleep の既定を Never にするかは Q1 がユーザーに確認中）。

## 直し（commit 9db7a023b）

- 新しい設定の key `power.lock`（`settings-keys.c`、resolver compositor、INT 0〜240 分、既定 0 = never、KEPT）。desktop.conf に値が無い利用者は never、値がある利用者はその値のまま（新しい key なので既存の値は無い）。
- compositor（`wayland/settings.c`）: `power.lock` を `settings_apply_lock` で `lock_idle_ms` に（分 × 60000）。login session だけ、`--lock-idle` を渡された時はそちらが勝つ（既存の試験の `--lock-idle=20` のため）。変えた時は次の tick から（`KWL PREFERENCES key=power.lock applied value=N`）。`main.c` の固定の 10 分（`MAIN_LOCK_IDLE_MS`）を消した。`kwl.h` の注記。
- Settings（`settings/page-power.c`）: Sleep の card の下に「Screen Lock」の card、slider「Lock the screen after」（Never・1・2・5・10・15・30・60 分、端の語「Never」「1 hour」）。離すと `power.lock` を保存（`ZSETTINGS LOOK set key=power.lock`）。説明の行「The screen locks after this long without input. A sleep locks it too.」。`look.c`・`settings.h`（`lock_minutes`、既定 0）、`pages.c`（検索語に lock）。日本語（`locale/ja/settings.tr`、tr.py update）: 画面のロック・操作が無いままこの時間が過ぎると画面をロックします。スリープの時もロックします。・画面をロックするまで・1 時間。

## 試験

- host: `sh plan/tools/settings/host-store.sh build/p015-host/host-store` → 49 passed 0 failed（足した 3 つ: file に値が無い時 power.lock は 0 で未選択、241 は EINVAL、file の `power.lock=10` は 10 のまま）。`sh plan/tools/settings/host-settings.sh` → 38 passed 0 failed。`plan/tools/style-check.py` の新しい違反 0（settings.c の 2 件は元から）。`tools/i18n/tr.py check` 0 problems。
- build: `AAT_CONFIG=plan/ws307/tests/config-amd64-p015.mk plan/tools/aat/build-image.sh build/ws307-p015`（AAT の config から clang・libcxx・emacs を外した物。sysroot は main の build/amd64/sysroot の複写）→ OK、変えた file の warning 0。
- QEMU: `plan/ws307/tests/p015-lock.sh build/ws307-p015/hdd-image.img OUTDIR`（Venus の guest、`plan/ws307/tests/p015-lock.py` の 3 項目。判定は session の行と keiland-shot の PNG、SSH で）— 結果は下。
- 実機: 未実施（範囲の外）。
