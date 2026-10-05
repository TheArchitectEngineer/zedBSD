<!-- awesome-plan project=zedbsd record=ws089-p016 -->

# ws089-p016: 単一の instance

Status: planning（設計を書いた。compositor の xdg-activation の所有を Q1 に）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: q741（Q1、2026-10-05、P2）
依存: p010、compositor の Phase（WS099 と直列）
目安: 2h（1 Queue）。実行者の目安: phase-runner
所有 path: `userland/desktop/settings/main.c`、compositor・libkeiland の activation（所有は Q1 が決める）

## 範囲

二つ目の `settings` の起動（App Home・Files の desktop の menu の Change Wallpaper・`--page=`）で、既存の窓を前に出し指定の頁を開く。今の zdesktop に xdg-activation は無いので、仕組み（xdg-activation-v1 か libkeiland の小さな拡張か、Settings の UNIX socket か）を先に決める。

## 受け入れ

二つ目の起動で窓が 1 つのまま前に出て頁が替わる（guest の log と画面）。

## 検証の方法と範囲

QEMU の Venus。 やっていない確認は「未実施」と書く。

## 未決の判断

仕組みの選択（compositor に足すなら main の許可）。

## Event

2026-10-02 / ws089-beta1-plan: fg019 の計画で新設。

## 設計（2026-10-05、P2、q741）

- Wayland の app は自分で窓を前に出せない。前に出すには compositor が「起動の印」（activation の token）を確かめて窓を activate する仕組みが要る。zdesktop には xdg-activation-v1 が無い（`wayland/` を grep して確かめた）。
- 案:
  1. **compositor**: xdg-activation-v1 の global（`get_activation_token`・`set_app_id`・`set_surface`・`commit` → `done(token)`、`activate(token, surface)`）。zdesktop が app を起動する時（App Home・Files の「Open With」・desktop の menu の Change Wallpaper）に token を作り、環境変数 `XDG_ACTIVATION_TOKEN` で渡す。token の有効は 1 回・数秒。activate は token が有効なら窓を前に出して focus を移す。
  2. **Settings**: 起動の時に `$XDG_RUNTIME_DIR/keiland-settings.socket` に接続を試みる。繋がれば、二つ目の起動として「頁の語」と自分の `XDG_ACTIVATION_TOKEN` を送って終わる。繋がらなければ一つ目として socket を作って聞く。一つ目は受けた頁を開き、token で `activate` を呼ぶ。socket の file は利用者だけ（0600）。
  3. 他の app（Files・Text Editor など）も同じ形を後で使える（libkeiland に「単一の instance」の小さな口を置く案）。
- **所有の判断が要る**: 1 は compositor（`userland/desktop/wayland/`、WS099 と直列）。Q1 が P1 か P2 のどちらに割り当てるかを決める。2 は Settings（P2）。
- 試験: 一つ目の Settings を開き、二つ目を `settings sharing` で起動して、窓が 1 つのまま前に出て Sharing の頁になること（zdesktop の log の activate と Settings の log、PNG）。
