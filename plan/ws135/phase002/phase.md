<!-- awesome-plan project=zedbsd record=ws135-p002 -->
# ws135-p002: compositor の設定の store・merge の書き・拡張の protocol

Status: in-progress（q656-i01、P2 generation7。実装と host 試験済み・QEMU は p003 の probe と一緒に T2 へ）
Disposition: normal
Parent: [WS135](../ws.md)
Queue: q656 / q656-i01（2026-10-04 user「WS135の設計D1-D6を承認します。」、Q1 の依頼で p002〜p006 を design.md 第 2 版のとおり）

## 範囲（design.md §7 p002）

compositor: store と merge の書き、key の表、session の開始の読み・終わりの書き（worker、SIGHUP）、`kl_system_manager_v1` v1 と `kl_system_settings_v1`、
`kl_backend_peer_uid`（3 OS）、壁紙の非同期、音量の順序、repeat の送り直し。移行の間は今の watcher を残し、file の変化を store に取り込む。

## 実装

| file | 内容 |
| --- | --- |
| `userland/desktop/settings-keys/settings-keys.{h,c}`（新） | key の表（10 行: compositor 9・app 1）と検査（`kl_settings_key_check` は範囲の外を丸めず EINVAL、`kl_settings_key_number` は読みで丸める）、名前・値の書式 |
| `userland/desktop/keiland/kl-system-protocol.h`（新） | `kl_system_manager_v1`（destroy・get_settings、event capabilities）と `kl_system_settings_v1`（destroy・set・reset、event value・done・result）の opcode、flags（DEFAULT・UNKNOWN）、result の列挙（WS131 §4.1） |
| `wayland/settings-store.{h,c}`（新、host で build できる） | session の store。開始の読み（範囲の外を丸める、型の違う値・未知の key は飛ばす、音は start だけ）、choose・reset・report（audiod）・follow（Settings が書いた file、start も更新）、終わりの merge（lock・読み直し・差の key だけ置換か除去・他の行を残す・fsync・rename）、Log Out の writer thread（不変の copy）と finish（join と残りの同期の merge） |
| `wayland/settings.c`（新） | open（command line の既定、store、全ての適用）、tick（壁紙の完了・音の報告・通知）、logout・close、follow（watcher から）、protocol の request（set・reset、検査、音は volume.c へ、壁紙は glass.c の thread）、全 settings object への value＋done、result（saved は home が無い時 not_saved）、global の可視（session だけ・同じ uid） |
| `wayland/glass.c` | `zwl_glass_wallpaper_begin`（`O_NONBLOCK`・`fstat` で通常の file だけ、thread で読み・decode）・`zwl_glass_wallpaper_poll`（join して描く、失敗は描かない）、`wallpaper_draw`・`wallpaper_decode` に分割、`file_read` を `O_NONBLOCK`＋`S_ISREG` に、close で loader を join |
| `wayland/volume.c` | 開始の音量を store の start から（`zwl_settings_kept`）、keep は store へ report、`zwl_volume_report`・`zwl_volume_request`（audiod が kept の音量を受ける前は EBUSY） |
| `wayland/seat.c`・`input-method.c`・`ime.h` | `zwl_seat_repeat_changed`・`zwl_ime_repeat_changed`（repeat の変更を bind 済みの keyboard と IME の grab に送り直す） |
| `wayland/preferences.c` | 適用の code を除き、watcher だけ（変化で `zwl_settings_follow`）。p004 で除く |
| `wayland/main.c`・`handoff.c`・`protocol.c`・`zwl.h` | 開始の順（settings → preferences）、tick、SIGHUP を stop_service に、Log Out で `zwl_settings_logout`、終了で `zwl_settings_close`、global 25 番と dispatch、bind で capabilities |
| `libkeiland-backend/keiland-backend.h`・`peer/peer-getpeereid.c`（zedBSD・FreeBSD）・`libkeiland-backend-linux/peer-linux.c`（SO_PEERCRED） | `kl_backend_peer_uid` |
| Makefile（zedBSD・Linux・FreeBSD の wayland と backend） | 新しい source |

log の文字列 `ZWL PREFERENCES key=… applied` は変えない。新しい log は `ZWL SETTINGS …`（open・snapshot・set/reset・result・logout-save・saved・peer）。

## 検証

- host: `sh plan/ws135/tests/host-store.sh build/p2-q656/host-store` → **45 passed, 0 failed**（ASan・UBSan。表と検査、読み（未知の key・comment・範囲の外・壊れた行・重複・相対 path・音の start）、
  set の検査、merge（変化なしは書かない・set/reset/戻し・comment と未知の key が残る）、session の間の手での編集が残る、2 つの store の merge、64 KiB 超の file を壊さない、
  home 無し、writer と後の変更、音（audiod の前は書かない・同じ値は書かない・sound.available は書かない）、follow）。
- build: zedBSD の compositor（`make -j16 ZEDBSD_CONFIG=plan/ws095/tests/config-amd64-ime.mk BUILD=build/p2-q652 build/p2-q652/bin/wayland`）warning 0、
  `make keiland-linux` warning 0。`git diff --check` OK。
- 未実施: FreeBSD の native build（FreeBSD の guest が要る）、`plan/tools/keiland-os-boundary/check.sh`（この worktree では `make -pn disk-image` が openssl の
  package の build の状態を求めて止まる）、QEMU（p003 の probe と一緒に T2 へ）。
