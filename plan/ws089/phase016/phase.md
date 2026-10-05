<!-- awesome-plan project=zedbsd record=ws089-p016 -->

# ws089-p016: 単一の instance

Status: in-progress（実装済み・T1 の試験待ち）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: q741（Q1、2026-10-05、P2、設計）、q749（実装、P2 g15）
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

## Q1 の割り当て（2026-10-05）

compositor の xdg-activation-v1 も P2 が持つ（P1 は WS052 p006 に専念するため）。WS170・WS169 の mock の後に、compositor の変更を WS099 の他の変更と直列に Q1 が順を決めて入れる。

## 実装（2026-10-05、P2 g15、q749）

- **compositor**（`userland/desktop/wayland/`）: `activation.c`・`activation.h` を新設し、xdg_activation_v1 version 1 を global 26 で出す（`protocol.c`）。token は 16 byte の乱数（getentropy）の 32 桁の 16 進、1 回限り・30 秒、表は 16 個（古いものから置き換え）。token の許可: 求めた client が keyboard の focus を持つ（reason=focus）、または接続から 5 秒以内で窓を出していない client（reason=new-program。その program は自分の窓を上に出せるので、その権利を別の program の窓に渡しても新しい権限にならない）。許可の無い token も渡す（activate は何もしない）。`activate` は token が既知・未使用・30 秒以内・許可済み、surface が map 済みの toplevel、lock・greeter でない時だけ `zwl_glass_activate`（`shell.c` に新設: App Home を閉じ、別の desktop の窓ならその desktop に移り、`zwl_glass_bring` で前に出して focus）。log は `ZWL ACTIVATION token …`・`ZWL ACTIVATION activate … result=activated|refused reason=…`・`ZWL APPS raise … via=activation`。`zwl_spawn`（`home.c`）は起動する program ごとに許可済みの token を作り `XDG_ACTIVATION_TOKEN` に入れる（作れなければ消す）。`zwl_client.connected_ms` を `main.c`・`input-method.c` で記録。
- **libwayland**: `activation-protocol.c` と private の `xdg-activation-v1-client-protocol.h`（wayland-protocols 1.44 の XML と照合、`keiland/wayland/API-PROVENANCE.md` に行を追加）。
- **libkeiland**（KL_VERSION 32）: `instance.c` に `kl_instance_open/fd/take/close`（`$XDG_RUNTIME_DIR/keiland-NAME.instance` の UNIX socket。runtime dir が利用者の物で 077 が 0 の時だけ。二つ目の起動は request の 1 行と token の 1 行を書いて終わる。残った socket は利用者の socket なら消して引き継ぐ。`XDG_ACTIVATION_TOKEN` は消す）と `kl_activation_token`（自前の queue で token を求める）・`kl_activate`。二つ目の起動の token は compositor に求めた物を優先し（新しい program として許可される）、求められない時だけ環境変数の物（長く動く Files が自分の未使用の古い token を子に継がせるため）。
- **Settings**: `main.c` で起動時に `kl_instance_open("settings", 頁の語または空)`。渡したら `ZSETTINGS DONE reason=handed-over page=…` で終わる。一つ目は loop で `kl_instance_take` → `se_ui_go` で頁を開き `kl_activate`（log `ZSETTINGS INSTANCE request page=… known=… activate=…`）。`window.c` の wait は socket の fd でも起きる（`se_window.extra_fd`）。runtime dir が私的でなければ従来どおり一人で動く（`INSTANCE alone errno=…`）。
- Calendar（WS155）の単一の instance も同じ `kl_instance_*` と、system bar の時計からの `zwl_spawn` の token で賄える。

### 検証（2026-10-05）

- build: zedBSD の `bin/wayland`・`bin/settings`（libkeiland.so・libwayland-client.so を含む、`ZEDBSD_CONFIG=plan/ws089/tests/config-amd64-settings.mk BUILD=build/p2-p016`）warning 0。Linux の Keiland（`make keiland-linux KEILAND_LINUX_BUILD=build/p2-keiland-linux`）warning 0、header-check 通過。`makefile-sync.sh` PASS。`exports.py` で exports.map を再生成。
- host: `sh plan/ws089/tests/run-host-instance.sh`（Linux の libkeiland.so、私的な runtime dir）PASS 21 項目（一つ目が listen、二つ目が頁と token を渡して終わる、頁なし・token なし、不正な token は捨てる、close で socket が消える、残った socket の引き継ぎ、同じ process の二つ目、077 の開いた dir は ENOTSUP、不正な名前・2 行の request は EINVAL）。
- style-check: 新しい file 0 件、変えた file は増えていない（home.c の 1 件は以前から）。keiland-os-boundary は C4（account-zedbsd.c、変更前から）のみ FAIL。
- 未実施: QEMU（T1 に `settings-p016.sh` を依頼）、FreeBSD の build、App Home からの起動の経路（spawn の token）の guest での確認。
