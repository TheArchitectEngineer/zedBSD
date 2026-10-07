<!-- awesome-plan project=zedbsd record=ws052p013 -->

# ws052-p013: Settings の Power の頁（sleep・画面の時間）

Phase ID: `ws052-p013`
Parent: [WS052](../ws.md)
Status: cleared（2026-10-07 Q1 の判定: T1-358 QEMU PASS: 電源 30・電池 15 minutes、sleep できない行、slider の drag で `ZSETTINGS LOOK set key=power.sleep.ac value=0 error=0` と `KWL PREFERENCES key=power.sleep.ac applied value=0`。実機の sleep の時間の効き目は p012 の 5330 の UAT）
Phase disposition: normal
Queue: q850（2026-10-07 Q1「先に ws052-p013 をしてよい」）

## 範囲

設計 [ws052-p007](../phase007/phase.md) §8、N4（2026-10-05 ユーザー）: Settings の「Battery」（未実装の印）を「Power」にし、無操作で sleep するまでの時間を電源・電池で選ぶ（Never・5・10・15・30・60・120 分、既定 30・15）。画面を消すのはその半分（選ばない、文で言う）。sleep できない時はそう言い、時間は画面を消す半分に効く。電池の残りの表示は後。

## 実装（2026-10-07）

- 新しい `userland/desktop/settings/page-power.c`: card「Sleep」（説明の行は「The screen turns off after half that time.」、sleep できない時は「This computer cannot sleep. …」。`kl_system_power_get_state` の actions に `KL_POWER_SUSPEND` の bit が無ければ sleep できない）と 2 つの段の slider（「Sleep after, on the power adapter」「Sleep after, on battery」、7 段、値は「Never」か「N minutes」（`kl_trn`）、端の語「Never」「2 hours」）。離した時に `power.sleep.ac`・`power.sleep.battery` を保存（`se_look_set_number`、log `LOOK set key=power.sleep.ac value=N`）、compositor が直ぐ従う（`KWL PREFERENCES key=power.sleep.ac applied value=N`、ws052-p012 の sleep.c）。他で設定された段に無い値は最も近い段に描く。
- `pages.c`: `SE_PAGE_POWER`（旧 `SE_PAGE_BATTERY`）、名前「Power」・説明「Sleep and the screen.」・語 `power`（`/bin/settings power`）・検索の語に battery・sleep・screen ほか、ready。
- `settings.h`（`se_look` の `sleep_ac`・`sleep_battery`、prototype）、`look.c`（既定 30・15、`look_read` で読む）、`Makefile`・`Makefile.linux`・`Makefile.freebsd`。
- 翻訳: `locale/settings.keys`（Battery → Power、説明）、`ja/settings.tr` に 10 件（plural 1 件、`tr.py check` 115/115）。
- 鍵（`power.sleep.ac`・`.battery`、0〜240、既定 30・15）は ws052-p012 で `settings-keys.c` に足した。

## 確認（2026-10-07）

- build（warning 0）: `make ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws052-p012 build/ws052-p012/bin/settings`、`make keiland-linux`（rc 0、OpenSSL 以外 warning 0）。
- host: `sh plan/tools/settings/host-settings.sh` 38/0。style-check: page-power.c 違反 0、pages.c・look.c 新しい違反 0。
- 未実施: QEMU（T1: `/bin/settings power` の PNG（card・2 つの slider・30 minutes・15 minutes、QEMU は sleep できないので「This computer cannot sleep. …」の行）、電源の slider を Never の端へ drag して離すと `LOOK set key=power.sleep.ac value=0` と compositor の `KWL PREFERENCES key=power.sleep.ac applied value=0`、日本語の表示）。
