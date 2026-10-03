<!-- awesome-plan project=zedbsd record=ws135-p005 -->
# ws135-p005: app だけの設定（Terminal の terminal.ambiguous-wide）を `kl_settings_*` へ

Status: in-progress（q656-i01、P2 generation7。Terminal は実装・host 試験済み・T2 の QEMU 待ち。Files の open-with は判断待ちで未着手）
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

## 未着手（判断待ち）

- Files の open-with（`~/.config/keiland/open-with`、`files/apps.c`）: 種類ごとの key（`files.open-with.<type>`）は今の固定の key の表に入らず、表に prefix の行
  （動的な key）を足す設計の追加が要る。Files の source は WS127 の担当。Q1 の判断（WS135 に入れるか、Files の WS で行うか）を待つ。desktop-layout・tags・places・
  IME の辞書は data として対象外（design.md §1.4）。
