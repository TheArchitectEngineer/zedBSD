<!-- awesome-plan project=zedbsd record=ws135-p005 -->
# ws135-p005: app だけの設定（Terminal の terminal.ambiguous-wide）を `kl_settings_*` へ

Status: cleared（q656-i01、2026-10-04。Q1 判定）
Disposition: normal
Parent: [WS135](../ws.md)
Queue: q656 / q656-i01（D3 (a) 承認: app だけの設定は libkeiland が app の file を直接、別の process に通知しない）

## 実装

- `userland/desktop/terminal/settings.c`: 自前の file の読み書きを除き、`kl_settings_open(NULL, "terminal")`（app の key は display が要らない）の
  `kl_settings_get_int`・`kl_settings_set_int`（`terminal.ambiguous-wide`）に。file（`~/.config/keiland/terminal.conf` の `ambiguous-wide=`）の書式は同じなので
  既存の file はそのまま読める。`terminal.h` の注釈。
- `libkeiland/settings-app.c`: folder を mkdir -p で作る（`~/.config` が無い home）。
- `plan/ws135/tests/host-kl-settings.c`（`plan/ws089/tests/host-settings-stub.c` を移して改名）: app の key を本物の `settings-app.c` で扱う。Settings の
  `host-build.sh` と Terminal の `terminal-p009.sh` が使う。
- `plan/ws128/tests/terminal-p009.{sh,c}`: libkeiland の settings と stand-in を link。期待値を「key は元の行の場所で置き換え」に直した（旧 terminal は末尾へ移していた。
  他の行が残ることは同じ）。

## 検証

- host: `sh plan/ws128/tests/terminal-p009.sh` → **PASS**（table・check の settings 7 項目・speed）、Settings の `host-build.sh`・`host-wallpaper.sh` PASS、
  `host-settings.sh` 26/26。
- build: zedBSD の terminal・settings warning 0、`make keiland-linux` warning 0。
- QEMU（T2 に依頼）: `plan/ws128/tests/terminal-p009-guest.sh`（menu で切り替え、terminal.conf に保存、再起動で読み戻し）。未実施。

## Files の open-with（2026-10-04、Q1「open-with は WS135 で行って」、Files の source は Q1 が WS135 の担当として許可）

- 設計: design.md §2.4（prefix の行 `files.open-with.<type>`、値は `<name><TAB><command>`、libkeiland の `~/.config/keiland/files.conf`）。
- `settings-keys`: `KL_SETTINGS_KEY_PREFIX`・`KL_SETTINGS_TYPE_OPENER`、prefix の行、`kl_settings_key_find` が MIME の型の suffix を検査（小文字・数字・`.+-_`、`/` ちょうど一つ）、
  opener の値の検査（TAB ちょうど一つ、名前と command が空でない、他に制御文字なし、256 未満）。
- libkeiland: cache が prefix の key の entry を値の来た時に作る（上限 64）、prefix の key の未選択は `EAGAIN`、reset は値なし。app の file は opener の値をそのまま読む。
- `files/apps.c`: 選んだ既定は `kl_settings_open(NULL, "files")` の get・set・reset（`fm_apps_set_default`・`clear_default`・`has_default`、`fm_apps_for` は最初に選択）。
  利用者の list（`~/.config/keiland/open-with`）は読むだけで書かない、昔の「# set by Files」の後の行は読まない（この変更の前の選択は失われる、design §2.4）。
  旧 `apps_rewrite`・`apps_copy_list`・`apps_is_type_line`・`apps_make_folders` を除いた。
- 試験: `plan/tools/files/host-default.c` を新しい形に書き換え（HOME と XDG_CONFIG_HOME を一時に、files.conf の確認、利用者の list は書かれない、昔の行は読まない、
  MIME でない型は拒否）。`plan/tools/files/host-build.sh` に libkeiland の settings と stand-in。`files-open.sh` の always は `files.conf` の選択と解除を確かめる。
  `plan/ws135/tests/host-settings.c` に prefix の 12 項目。
- 検証: `plan/tools/files/host-default.sh` → PASS（20 項目）、`host-model.sh` PASS、`host-settings.sh` 38/38、zedBSD の files・keiland-linux warning 0。
  `host-p009.sh`・`host-p010.sh`・`host-p013.sh` は FAIL だが、変更の前（main a7940cf、git stash で確認）でも同じく FAIL（open-with と無関係、Files の既存の問題として Q1 に報告）。
- QEMU（T2 に依頼）: `plan/tools/files/files-open.sh always`（と open の回帰）。未実施。

## 結果（Q1、2026-10-04）

cleared。T2-014（QEMU Venus、agent/p2 47b3418 の image）PASS 10/10: settings-p003・p007・p004・p005・settings-pages（1280x800、24 頁）・terminal-p009-guest（広い幅が restart の後も読み戻される）・files-open always（files.conf の選択と cleared）・files-open mouse・volume-p005（desktop.conf は書かれない、feedback の音 4）・boot-test。証拠 worktrees/t2/build/t2-014/out/。FreeBSD の native build と host 試験は T2-016（47b3418）で別に確かめる。
