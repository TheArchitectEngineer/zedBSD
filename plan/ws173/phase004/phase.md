<!-- awesome-plan project=zedbsd record=ws173-p004 -->

# ws173-p004: AAT のシナリオ（今日の機能）、suite、runner と実行の記録の形

Status: cleared（2026-10-06 Q1 判定: T1-202c の smoke 8 本 pass、full 79 本の判定の一覧（pass 36・fail 13・needs-person 18・not-run 8・未判定 4）。full の fail は q788 で切り分け）
Disposition: normal
Parent: [WS173](../ws.md)
Queue: Q1 の指示（2026-10-05 夜、P2 g17 の Task 1、最優先）
依存: [p003](../phase003/phase.md)（`plan/tools/aat/aat`）、p001・p002（`aat-input`・`keiland-shot`、P1）

## 範囲

- UAT に残すのは機器（電源 button・蓋・USB の抜き差し・touchpad の指・YubiKey と NFC・音・Wi-Fi の電波・S0 idle）と全体の使用感。それ以外は AAT（エージェントが素の 5330 を SSH で操作）。
- 今日の時点で実装済みの機能の AAT の項目と合否の基準。出典: [UAT の一覧（ws159-p005）](../../ws159/phase005/phase.md)、[plan/uat.md](../../uat.md)（2026-10-05 午後の所見と直した BUG-181・193・194・195〜197、ws099-p033、ws132-p009）、今日 merge した機能（Phone・Calendar・Mail の mock、dark mode、SKK と Languages、画面 keyboard の予測と絵文字、Settings の Users と Manage users、video player、account-admin）。
- 形（2026-10-05 夜のユーザーの決定、Q1 の伝達）: シナリオは `tests/scenarios/{os,desktop,apps}/…` の文書（[plan/tests.md](../../tests.md) §3: header と 目的・準備・操作と確認（各操作に 操作・確認事項・正解・確認方法）・合格・注記）。**主役は人が読める文書で、エージェントが文書を読んで aat で操作する**。自動の script は補助（同じ id で `plan/tools/aat` の側）。suite は `tests/suites/`。実行の記録は `plan/ws173/runs/<日付>-<suite>.md`。最初に `plan/tools/aat/scenarios/` に項目の script を書き始めていたが、この形に移した。

## 作った物

- **シナリオ 75 本**（`tests/scenarios/`）:
  - os（11）: boot（session-up・kernel-log）、acpi（tables-and-touchpad、BUG-195）、power（battery-state・power-button BUG-196・shutdown BUG-197・lid・ac-plug）、accounts（sudo・su-root-locked・passwd・account-admin）。
  - desktop（25）: home（super-key・launcher-button）、wiseview（super-tab）、keyboard（super-not-to-client）、windows（move-by-title・maximize-double-click BUG-179・unmaximize-drag BUG-180・open-maximized ws099-p033・close-minimize）、bar（status-icons・network-details・volume-slider BUG-170）、startup（wallpaper-time）、appearance（dark-mode・window-opacity BUG-171・wallpaper）、input-method（choose-method・japanese-textedit・skk-textedit）、osk（open-close・qwerty-type・prediction・emoji）、lock（lock-unlock）、session（logout-login）、touchpad（gestures、hands）。
  - apps（39）: 13 個の app の open-from-home、terminal（type-command・fullscreen-f11 BUG-194・many-windows BUG-175・japanese-history BUG-173）、emacs（edit-save）、settings（pages・single-instance・users-page・change-password・manage-users・about・sound-page）、phone（browse）、calendar（navigate）、mailer（read-compose）、videoplayer（play）、imageview（open-png-jpeg）、pdfviewer（open-turn）、textedit（type-save）、files（devices ws132-p009・mount-usb hands）、monitor（frame-rate）、browser（long-url-selection BUG-181）、notes（draw-stroke）。
  - 人の要否: `hands` 6 本（power-button・shutdown・lid・ac-plug・touchpad・mount-usb）、`look` は撮影の見えを人が決める物。
  - 位置は名前と log の行（`ZWL HOME icon`・`ZWL MAP`・`ZWL GLASS launch`・`ZWL GLASS dock`・`ZWL NETWORK icon`・`ZWL VOLUME popup open`・`ZWL OSK open`・`ZWL OSK crect`・`ZWL OSK qrect`・`ZSETTINGS CONTROL`・`ZFILES DEVICE row` など）で示し、固定の画素は使わない。log に無い位置（bar の launcher、浮いた窓の title bar と button、画面 keyboard の flick の鍵と tool の tab）は compositor の作りの値を注記に書いた。
- **suite**（`tests/suites/`）: `smoke`（8）、`full`（75、起動が先、log out と shutdown が最後）、`hardware`（9）。
- **`plan/tools/aat/check-scenarios.py`**: header の項目と値、id と path、paths の実在、節、各操作の 4 項目、suite の各行が実在のシナリオに当たるか。`--list`。
- **自動の補助**（`plan/tools/aat/scenarios/`）: `helpers_os.py`（8）・`helpers_desktop.py`（25）・`helpers_apps.py`（36）＝ hands 以外の 69 本。`aatlib.py`（aat の呼び出し、log の待ち、App Home からの起動、窓、Settings の頁と control、step の記録、判定）、`common.py`（前後の片付け: app を閉じる・`/tmp/aat-work`・Esc、`/etc/shadow` の控えと戻し、入力方式と Alt+Space）、`samples.py`（PNG・JPEG・2 頁の PDF・4 秒の MP4 を host で作り `/tmp/aat-samples` へ。image に入れない）。
- **runner**: `plan/tools/aat/run-aat.sh TARGET OUTDIR [SUITE|ID|PATTERN|area:NAME ...] [--record PATH] [--no-samples]`（中身は `scenarios/runner.py`）。前に `aat check`・`ZWL READY`・`aat start`・samples。補助のあるシナリオは補助が流し、無い物は `by-agent`、hands は `needs-person`、QEMU の hardware は `not-run`。
- **実行の記録の形**: `OUTDIR/summary.md` が 1 回の実行の記録（日時・target（QEMU か実機）・image（os-release と uname）・件数・シナリオごとの判定・理由・撮影・step の記録への link）。`OUTDIR/records/<id>.md` は step ごとの「したこと・見た物（log の行・値）・撮影」と判定。`--record plan/ws173/runs/<日付>-<suite>.md` で plan に写す（撮影は OUTDIR の絶対の path）。エージェントが文書を読んで行う回も、同じ表と step の形で書く。
- **aat の土台の直し**（T1-200・200b の結果、同じ Phase の中で）: ControlPath を `/tmp/aat-<uid>/ssh-%C` に、`aat-input start` を SSH の fd から切り離す、撮影を 2 段にして失敗の時に状態・user・`ls -l` を出す、`check` の status を道具 3 つだけに、get・put・shot の位置引数 "local" が top の `--local` を上書きして host で走っていた取り違えを直した（dc1c1a82・7d7f9d3d）。窓を (client, surface) で数え（surface の番号は client ごとの ID）、`where --client`、`windows --json`、`click --with alt`、窓の行の読み直し（`GLASS press move` は押した点で窓の位置ではない、launch・moved・dock・undock・resized・open-docked を読む）（2aa3903d）。
- QEMU の自己試験で root の鍵の login が lock された root に通らない時の image: `plan/ws173/tests/config-amd64-aat-root.mk`（root を lock しない。その時 os.accounts.su-root-locked は FAIL が正しい）。

## 判断（P2）

- 判定の語は plan/tests.md の `pass`・`fail`・`needs-person` に、runner だけが付ける `by-agent`（補助が無い）と `not-run`（この target では流さない）を足した。
- 補助は文書の操作を同じ順に行い、確認方法の log の行・file の中身で判定する。見えの判断の要る物は、機械で確かめられる所を確かめてから `needs-person`。
- 起動は App Home から（Super を押して離す → 名前 → `ZWL HOME icon` の click）。file を開く・Settings の頁は session の利用者（kei）として SSH から命令で（Settings の単一の instance への頁の受け渡し、ws089-p016）。
- 設定を変える補助は元に戻す（appearance・opacity・wallpaper・入力方式・音量・password は `/etc/shadow` の控えから）。`--local`（runner の host の試験）では host の program を止めず shadow に触れない。
- compositor の窓の行（GLASS moved・dock・undock・resized・WINDOW centred）は client を持たないので、同じ surface の番号の窓が 2 つあると区別できない。補助は同じ種類の窓を 1 つずつにする。P1 に client= を足すよう Q1 が頼んだ。

## 確かめ（host）

- `python3 plan/tools/aat/check-scenarios.py` → `check-scenarios: PASS`（75 本、suite full 75・hardware 9・smoke 8）。
- `sh plan/tools/aat/tests/run-host.sh` → `aat-host: PASS`（aat の CLI・ssh を通ること・窓の client ごとの数え・`--with`・シナリオの形・runner の helper の pass と hands の needs-person と記録・撮影）。
- 補助の 3 file は `--list` で 69 の id を返し、構文と未定義の名前の確かめ（ast）を通った。target での動きは未確認。

## 未実施

- QEMU（T1）: runner の自己試験（下の依頼）。
- 5330 の実機: p005（ユーザーが USB で起動）。
- 補助の target での確かめ全部（座標の決め方・待ちの時間・Settings の control の座標が窓の中の座標であること・画面の倍率 1 の仮定）。QEMU で直すべき所が出る見込み。

## T1 への依頼（Q1 経由）

1. image: main（この Phase の merge の後）で `plan/tools/guest/test-image.sh plan/tools/aat/config-amd64-aat.mk BUILD`（harness つき: QEMU の network と root の鍵）。
2. 起動: `plan/ws035/tests/zdesktop-guest.sh start BUILD/hdd-image.img`（Venus、1280x800、GUEST_RUNTIME は自分の物）。kei の自動の login で desktop が出る。
3. `plan/tools/aat/aat --qemu check`。root で入れなければ（lock された root）、`plan/ws173/tests/config-amd64-aat-root.mk` で作り直して 1 から。
4. `plan/tools/aat/run-aat.sh qemu OUTDIR smoke`、続けて `plan/tools/aat/run-aat.sh qemu OUTDIR2 full`（長いので分けてよい、log out と shutdown は最後）。
5. 合格（runner の自己試験として）: smoke の 8 本が pass か needs-person（fail が無い）。full は結果の一覧を返す（fail は P2 が補助かシナリオか機能かを調べる）。`OUTDIR/summary.md`・`records/`・`png/`・`errors.txt` の path を返す。

## q788（2026-10-06 P2）: T1-202c の full の fail 13 件の切り分け

証拠は `/home/awe/zedBSD-worktrees/t1/build/t1-202c-full/`（records・png・errors.txt。session の log は無かった）。

| scenario | 判定 | 原因と直し |
| --- | --- | --- |
| `apps.settings.pages`（wifi・display）・`apps.settings.about`・`apps.settings.sound-page` | 機能（試験のための log） | Settings は page の controls が前の list と違う時だけ `ZSETTINGS LAYOUT` を出す。control の無い page から control の無い page（sound → display、home → about）や、今の page を頼み直す時（wifi）は出ず、helper が待つ行が来ない。→ `se_ui_go`（settings/ui.c）で page を頼まれる度に次の frame で list を出し直す（`logged_count = -1`）、今の page を頼まれた時も `ZSETTINGS PAGE` を出す |
| `apps.settings.change-password` | helper | 1 回目の後の Esc は、欄が空なら「戻る」で Users の頁を離れ、2・3 回目は Settings の概要の頁に打っていた（right.png）。→ Esc の後に Users の頁を頼み直して controls を読み直す（`users_page_again`） |
| `apps.settings.manage-users` | helper | users の一覧の最後の行が窓の下（window 座標 y 1294）にあり click が画面の外。→ wheel で頁を下へ送ってから click（`reveal_control`、Remove の button も） |
| `apps.terminal.fullscreen-f11` | helper | F11 は compositor の menu の Fullscreen の項が取り（BUG-194）、Terminal は `ZTERM MENU state … fullscreen=1` を出す（`ZTERM FULLSCREEN key` は menu の無い desktop の時だけ）。→ どちらも受ける |
| `apps.emacs.edit-save` | helper（と小さな機能の差） | REmacs は `-nw` を持たず、`-nw` という buffer を開いた（emacs.png）。→ helper とシナリオを `emacs FILE` に。REmacs が GNU と同じく `-nw` を黙って受けるかは別（Q1 に報告） |
| `apps.videoplayer.play` | helper | `VIDEOPLAYER FRAMES` は最初と 100 枚ごとだけ。1.6 s では 1 行。→ Space の `PAUSE shown=N` の N > 1 で確かめる |
| `apps.pdfviewer.open-turn` | helper | viewer の最初の mode は scroll で、Page Down は 1 画面送るだけ（`PAGE shown` を出さない）。→ PAGE が無い時は 2 枚の撮影が違うこと（動いた）を確かめて needs-person |
| `desktop.bar.volume-slider` | 試験の場（設計どおり） | QEMU に音の device が無く popup は `sound=0`、その時 slider は何もしない（volume.c）。→ `sound=0` なら撮影して needs-person（音のある machine で） |
| `desktop.input-method.skk-textedit` | helper | SKK の engine の言語は `skk`（`ZWL IME language=skk`）、helper は `ja` を待った。→ method 2 は `skk` |
| `apps.terminal.japanese-history` | 未解決 | Languages の頁の switch の click が何も起こさなかった（`None; None`）。同じ run の `desktop.input-method.choose-method` は同じ操作で pass。log が無く原因は分からない |
| `apps.terminal.type-command` | 未解決 | Windows キーで `ZWL HOME open` が来なかった（20 個の Terminal の後）。log が無く原因は分からない |

- runner の改善: 各シナリオの間の session の log を `OUTDIR/logs/ID.log` に残す（`Run.log_start`・`save_log`、aatlib.py）。次の run から fail の原因を log で追える。host の試験に 1 項目（run-host.sh）。
- 確認: Settings の build warning 0、style-check 0、aat run-host PASS、check-scenarios PASS。QEMU は T1 の full の流し直しで。

## q788-i02（2026-10-06 P2）: T1-232 の full の残りの fail 8 件

証拠は `/home/awe/zedBSD-worktrees/t1/build/t1-232/`（今回から `logs/ID.log` がある）。

| scenario | 判定 | 原因と直し |
| --- | --- | --- |
| `apps.settings.about`・`apps.settings.sound-page` | helper | `ZSETTINGS ABOUT` と `ZSETTINGS SOUND open` は Settings の起動の時に 1 回だけ出る（page を開く時ではない）。helper は page を頼んだ後の mark から探した。→ launch の前の mark から探す |
| `apps.settings.change-password` | helper | 現在の password の欄（window の y 662〜698）が窓の下端（680）に半分隠れ、3 回目の click が外れて `aat-pass-1` が欄に入り、確認の欄が空で request が出なかった（right.png の 10 文字）。→ 各回の前に `reveal_control` で欄が全部見えるまで頁を送る |
| `apps.settings.manage-users` | helper | 一覧の行を `index >= 100` で探し、PIN の欄（200〜202、`PIN_FIELD_FIRST`）を最後の行として click した。行が選ばれず Remove（23）が出なかった。→ 行は 100〜199 |
| `apps.videoplayer.play` | helper（sample） | 4 秒の sample は window・2 回の撮影の間に終わり（`END reached`、`ENDED shown=19`）、Space は再生し直した（PAUSE が無い）。→ sample を 12 秒に（samples.py、シナリオの文書も） |
| `apps.terminal.japanese-history` | helper | desktop の入力方式の既定は 1（Japanese、compositor の main.c `server.ime_method = 1`、Settings の look.c も 1）。起動の時は `ZWL IME method=` を出さないので helper は 0 と読み、1 を選ぼうとした。Settings は選ばれ済みの switch の click を何もしない（page-languages.c）。→ `current_method` の既定を 1 に。choose-method の「最後に元へ戻す」も 0 ではなく 1 へ戻るようになる |
| `apps.terminal.type-command` | helper（compositor の遅さを記録） | 直前の many-windows の 20 個の Terminal を kill した後、compositor は 20 個の client を手放すのに 23 秒かかり（`ZWL PERF 22690ms: passes=154 … work 94.1%`、その間の compose は 9 frame・2.3 秒だけ）、その間の Windows キーが 10 秒の待ちに間に合わなかった（次の scenario の頭で `HOME open via=super` と `close via=escape` が同じ ms に出た）。→ many-windows は全部の client の `ZWL CLIENT gone` を待って（90 秒まで）かかった秒数を記録する。client 1 個に約 1 秒の後始末は compositor の性能の問題として Q1 に報告（原因は未調査） |
| `desktop.session.logout-login` | シナリオ | App Home の Log Out の項目は p037 で Power Off の dialog の中に移った。→ dialog を開き（`power_dialog`）、Up（Cancel → Log Out）と Enter。`ZWL POWER choice=logout via=key error=0` を記録 |

- 確認: aat run-host PASS、check-scenarios PASS（88）、py_compile。QEMU は T1 の流し直しで（未実施）。
