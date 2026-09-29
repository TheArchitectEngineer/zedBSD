<!-- awesome-plan project=zedbsd record=ws089-p007 -->

# ws089-p007: desktop の設定の仕組み（libkeiland の preferences と zdesktop の反映）

Status: in-progress（2026-09-29）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: なし（2026-09-29 main の依頼。サブエージェントが worktree の branch `wt/ws089` で実行）

## 目的と受け入れ（[案](../proposed/desktop-preferences.md)、許可済み D4、[design.md](../design.md) §6.3）

- libkeiland に `keiland_preferences_*`（`~/.config/keiland/desktop.conf`、key 単位の書き込み: flock → 読み直し → その key だけ変える →
  `mkstemp` → `fsync` → `rename`。未知の key と注釈を保つ）。`KEILAND_VERSION` を上げる。
- zdesktop（greeter でないとき）が glass を作る前に読み、以後 1 秒ごとに file の inode・size・`st_mtim` を比べ、変わった key を当てる:
  `wallpaper`・`window.opacity`・`pointer.speed`・`pointer.natural`・`keyboard.repeat.rate`・`.delay`。key が無ければ command line の値に戻る。
- 反映は 1 秒以内（見込み）。壁紙の描き直しの時間を計る。
