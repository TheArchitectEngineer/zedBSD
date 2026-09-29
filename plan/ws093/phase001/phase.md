<!-- awesome-plan project=zedbsd record=ws093-p001 -->

# ws093-p001: 設計

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS093](../ws.md)
Queue: main の依頼（2026-09-29、worktree `.claude/worktrees/ws093-open`、branch `wt/ws093`、main 353e9790 から）

## 範囲と受け入れ

Files から file の種類に応じた app を開く仕組みの設計を [design.md](../design.md) に書き、実装の Phase を ws.md に計画する。

## 結果

- 調査: WS071 の Files はすでに開く仕組みを持つ（`files/apps.c` の対応の表: 利用者の一覧・system の一覧・組み込みの表、`fm_apps_launch`、
  double click・Enter の既定での起動、touch の tap が click になるので double tap は double click、File menu と context menu の Open With）。
  画像と text の double click が Quick Look と terminal の less になるのは、組み込みの表の既定のため。
- 設計: 組み込みの表に Image Viewer（png・jpeg・gif）・Text Editor（text）・Browser（html）を既定として足す（`needs` で program の無い image は
  従来どおり）。p003 で「Always Open With」の submenu と利用者の一覧への書き込み（Files が書いた行だけを扱う）。p004 で規約と回帰。
- 判断の要る点: 無し（既定の順と名前は design §2 の表で決めた。変えたい場合は main・ユーザー）。

## 見つけたこと

- `plan/tools/files/host-model.c` の section 17 は `$XDG_CONFIG_HOME/zdesktop/open-with` に書き、`apps.c` は `keiland/open-with` を読む。
  p002 で host の試験を流して確かめる（`plan/tools/` は main の担当）。

## 確認

設計の Phase のため build・試験は無し。コードは変えていない。

## Resume point

p002（既定の対応）から。
