<!-- awesome-plan project=zedbsd record=ws105-p009 -->

# ws105-p009: gdm と logind（seat-logind・最小の D-Bus・pause と resume・`keiland.desktop`）

Status: planned
Disposition: normal
Parent: [WS105](../ws.md)
Queue: なし
依存: p008
実行者: phase-runner（high）。**始める前に [design.md](../design.md) の §5.1・§5.5・§5.6 を読む。**

## 目的

決定 D11・D12。gdm の session の一覧から Keiland を選ぶと、gdm が `/opt/keiland/bin/wayland` を利用者（root でない）の session として起動する。compositor は
logind から DRM と入力の device の fd を受ける（利用者には device を直接開く権限が無い）。Log Out で gdm に戻り、VT の切り替え（pause・resume）で画面が戻る。

## 作る・変える file

| file | 中身 |
| --- | --- |
| `wayland/linux/dbus-linux.c`（と `linux/dbus-linux.h`） | design §5.5 の最小の D-Bus の client。外に出す関数の例: `dbus_open_system(struct keiland_dbus *)`、`dbus_call(bus, destination, path, interface, member, signature, args..., reply)`（同期。reply を待つ間に来た signal は queue に溜める）、`dbus_add_match(bus, rule)`、`dbus_fd(bus)`、`dbus_dispatch(bus, callback)`（poll で readable のときに signal を読む）。型は `s`・`o`・`u`・`b`・`h` と、それらの struct（reply の `(hb)`）だけ。message の最大の大きさ、壊れた message は error で切る |
| `wayland/linux/seat-logind-linux.c` | design §5.5 の手順（`GetSession`・`TakeControl`・`TakeDevice`・`ReleaseDevice`・`PauseDevice`・`PauseDeviceComplete`・`ResumeDevice`） |
| `wayland/linux/os-linux.c` | seat の選択に `logind` を足す（design §5.1）。`zwl_os_poll_count`・`fill`・`done` で D-Bus の fd を main loop に入れ、signal を seat-logind に渡す |
| 共通: `zwl.h`・`display.c`（`zwl_schedule`）など | design §5.6 の p009 の行（`os_paused`、paused の間は合成しない、resume で swapchain を作り直す）。**zedBSD では `os_paused` は常に 0 で、道は通らない** |
| `userland/desktop/wayland/linux/keiland.desktop` | design §5.5 の内容 |
| `userland/desktop/keiland-linux.mk` | `install-session`: `$(DESTDIR)/usr/share/wayland-sessions/keiland.desktop` に入れる（`/opt/keiland` の外の唯一の file、D11） |
| `plan/tools/keiland-linux/build-guest.sh`・`guest.sh` | `gdm` の variant（p001 で用意した物）を使う。gdm の自動 login（`/etc/gdm3/daemon.conf` の `[daemon]` に `AutomaticLoginEnable=true`・`AutomaticLogin=kei`、`WaylandEnable=true`）と、利用者 kei の session を Keiland にする（`/var/lib/AccountsService/users/kei` に `[User]`・`Session=keiland`・`SessionType=wayland`）を `guest.sh` の command（`gdm-setup`）で行う |

D-Bus の message の形の要点（実装の前に D-Bus の仕様の "Message Format" の節を読む）:

- header: endian の byte `l`、type（1 method_call、2 method_return、3 error、4 signal）、flags、version 1、body の長さ（u32）、serial（u32）、header の field の配列 `a(yv)`
  （1 PATH `o`、2 INTERFACE `s`、3 MEMBER `s`、4 ERROR_NAME `s`、5 REPLY_SERIAL `u`、6 DESTINATION `s`、7 SENDER `s`、8 SIGNATURE `g`、9 UNIX_FDS `u`）。header の後を 8 byte に揃える。
- fd（`h`）は body の中では fd の配列の index（u32）で、fd 自体は `sendmsg`・`recvmsg` の `SCM_RIGHTS`。header の UNIX_FDS に数を入れる。
- 認証の後に `NEGOTIATE_UNIX_FD` を送って `AGREE_UNIX_FD` を受ける（fd を受けるのに要る）。

## guest での手順

```
plan/tools/keiland-linux/build-guest.sh build/keiland-linux/guest-gdm gdm
GUEST_DIR=build/keiland-linux/guest-gdm GUEST_RUN=build/keiland-linux/run-gdm SSH_PORT=2227 plan/tools/keiland-linux/guest.sh start
make keiland-linux && make keiland-linux-install keiland-linux-install-session DESTDIR=$PWD/build/keiland-linux/stage
GUEST_DIR=... plan/tools/keiland-linux/install-guest.sh
GUEST_DIR=... plan/tools/keiland-linux/guest.sh gdm-setup
GUEST_DIR=... plan/tools/keiland-linux/guest.sh ssh 'systemctl restart gdm'
# 30 秒ほど待って
GUEST_DIR=... plan/tools/keiland-linux/guest.sh screenshot build/keiland-linux/p009-session.png
GUEST_DIR=... plan/tools/keiland-linux/guest.sh ssh 'ps -o user,pid,args -C wayland; loginctl list-sessions'
```

## 確かめ（完了の条件）

1. build（gcc・clang）warning 0、`elf-check.sh`・`makefile-sync.sh` PASS。
2. gdm の自動 login で Keiland の session が起動する: `ps` の `wayland` の user が `kei`、screenshot に wallpaper と system bar（PNG をユーザーに見せる）、
   compositor の log に seat logind の行（`ZWL SEAT logind session=...` のような 1 行を Linux の module から出す）。
3. 利用者の session の中で app が起動する（App Home から Terminal）。入力が届く（key と pointer）。
4. Log Out: gdm の greeter に戻る（自動 login を切った状態で確かめる: `daemon.conf` の `AutomaticLoginEnable=false` にして、greeter で session を選んで kei で login →
   Keiland → Log Out → greeter の screenshot）。greeter の操作は QMP の key と click で行う。
5. VT の切り替え: Keiland の session の中で `guest.sh key ctrl alt f3`（text の console）→ 5 秒 → 元の VT（`loginctl` で分かる VT。`ctrl alt f2` など）に戻る →
   screenshot で Keiland の画面が戻る。compositor の log に pause と resume の行。戻った後に pointer と key が届く。
6. text console から root での起動（p006 の `seat-direct`）が今も動く（`KEILAND_SEAT=direct`）。
7. 共通の file を変えたので zedBSD の回帰（design §9.2）。

## 結果

（実行の後に書く。design §10 の V6 の結果も）
