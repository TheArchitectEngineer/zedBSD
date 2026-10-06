<!-- awesome-plan project=zedbsd record=ws173-p004 -->

# ws173-p004: AAT のシナリオ（今日の機能）、suite、runner と実行の記録の形

Status: in-progress・test-wait（T1-202c、2026-10-06 q784: smoke の fail 3 件は helper の不具合、P2 7a442548 → main f4b86474 で直した。T1-202c の流し直し待ち）
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
