<!-- awesome-plan project=zedbsd record=ws035p082 -->

# ws035-p082: グラフィカルログインマネージャの検討（設計のみ）

Phase ID: `ws035-p082`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-27、サブエージェント。設計の文書だけ、実装なし）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行）

## 範囲

2026-09-27 ユーザー:「WS035に、グラフィカルログインマネージャの検討を追加してください。Waylandではなくて、Vulkanを直接叩くのかなあ。」
案の比較、認証、session の開始、表示と入力の引き継ぎ、security、ユーザーの仮説の意味。

## 結果

[login-manager-design.md](../login-manager-design.md):

- 仮説への答え: login の画面を Wayland の client にしない（Wayland を開かない）・`VK_KHR_display` で描く、は賛成。ただし描く process を
  root にしない。
- 推奨: zdesktop の greeter mode（`zdesktop --greeter`、uid `_greeter`、socket を開かない）と、小さな root の `sessiond`（shadow と crypt の
  照合、seat の device の持ち主、session の起動・終わり、fallback）への権限の分割。
- 引き継ぎ: 最初は console が一瞬出る形、後で lease の fd の受け渡し（libvulkan の入口と kernel の revoke が要る）。
- 判断が要る点（§8）: `/dev/gpu0` の 0666、kernel の revoke（複数 user の前提）、既定で無効、自動 login 無し、置き場。
- 実装の Phase の案 g1〜g5 と kernel の k（別の承認）。

調べた事実（コード）: `userland/base/login/main.c`、`userland/base/etc/rc.conf`、`src/kern/devfs.c`（input 0640、他 0666、chown の記録）、
`src/drivers/gpu/gpu.c`、`userland/desktop/wayland/main.c`（`/tmp/wayland-0`）、[graphics-design.md](../graphics-design.md) §5。

検証: 設計のみのため試験なし。設計の敵対的なレビュー（design-reviewer）は、この repository の規則（サブエージェントを使わない）により
行っていない（未実施）。
