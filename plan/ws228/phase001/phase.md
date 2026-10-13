<!-- awesome-plan project=zedbsd record=ws228-p001 -->
# ws228-p001: 簡単な vi の範囲の設計と実装

Status: planned（2026-10-14 Q1、q971、P3）
Disposition: normal
Parent: [WS228](../ws.md)
Queue: q971
依存: 無し

## 手順

1. 範囲の設計を phase.md に書く（命令の一覧、入れない物、端末の扱い、UTF-8、file の保存の安全（一時 file と rename、権限の保持）、大きな file の上限、signal と端末の復元）。Q1 が design-reviewer を通す。ユーザーの判断が要る点は印を付けて Q1 に返す。
2. review の後に実装（`userland/base/vi/`、Makefile の `ZEDBSD_USERLAND_PACKAGE`、menu は base、表示名は `vi`）、`plan/coding-style.md` の全文を適用。
3. 確かめ: amd64 の build warning 0、host で pty を使う短い試験（挿入・削除・保存・undo・日本語）。QEMU は p002 で T1。
