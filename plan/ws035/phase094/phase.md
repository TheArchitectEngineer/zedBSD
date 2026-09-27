<!-- awesome-plan project=zedbsd record=ws035p094 -->

# ws035-p094: sessiond（グラフィカルログインの g1・g3）

Phase ID: `ws035-p094`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て。ユーザー承認 2026-09-28「ログインマネージャーの提案は承認します。1点だけ、コンソール
ログインでなくグラフィカルログインをデフォルトにします。…」。設計 [login-manager-design.md](../login-manager-design.md) §8 の案を
承認、既定をグラフィカルに変更）

## 範囲

設計 §9 の g1（sessiond と描画の無い試験用の greeter）と g3（`XDG_RUNTIME_DIR`、session の終わりから greeter へ戻る）。
greeter の画面（g2）は p095、既定の有効化と init の `replaces=`・boot parameter `login=` は p098。

## 実装（2026-09-28）

- `userland/desktop/sessiond/`（新、root の service `greeter`、`/sbin/sessiond`）:
  - `main.c`: `--graphical`・`--console`・`--greeter=PATH`・`--session=PATH`。どちらの option も無ければ boot parameter
    （sysctl `kern.boot.login`、p098 で kernel に入る）が `graphical` のときだけ graphical。console のとき・greeter の program か
    `/dev/gpu0` が無いとき・greeter が続けて 3 回すぐ（20 秒以内に error で）終わったときは exit 0（init が置き換えた
    `getty_console` を起こす、p098）。SIGTERM・SIGINT で greeter か session を終わらせ device を root に戻して終わる。
    log は `/var/log/sessiond.log`（`SESSIOND ...` の行）、認証は syslog（LOG_AUTH）。
  - `greeter.c`: `_greeter`（uid 78、新しい account）で greeter を起こす（socketpair の片方を fd 3、`--greeter --auth-fd=3`、
    wallpaper があれば `--wallpaper=`、出力は `/var/log/greeter.log`、setsid・initgroups・setgid・setuid、小さな環境）。
    要求は 1 行: `AUTH name password`（OK / FAIL）、`POWER poweroff|reboot`（OK）、他は ERROR。誤りは 2 秒の後に FAIL、
    3 回続くごとに 2 倍（最大 16 秒）。password は照合の直後に消し、読んだ buffer も消す。log に password を書かない。
  - `session.c`: user に seat を渡し、`/run/user/UID`（0700、user の、lstat で directory であること）を作り、
    `/bin/sh /etc/zdesktop/session` を user として（setsid・initgroups・setgid・setuid、HOME・USER・LOGNAME・PATH・SHELL・
    `XDG_RUNTIME_DIR`、出力は `$XDG_RUNTIME_DIR/session.log`）。utmpx に USER_PROCESS・DEAD_PROCESS（line `seat0`）。
    終わったら process group と、root 以外なら user の全 process（user になった子の `kill(-1)`）を TERM・KILL、
    socket を消し、greeter へ戻る。
  - `seat.c`: `/dev/gpu0`〜`gpu3` と `/dev/input/event*` を seat の user の 0600 に（owner を先に）、止めるとき root の
    0666・0640（wheel）に戻す。greeter と session の間、毎秒もう一度（hotplug の新しい node）。
  - `session.sh`（`/etc/zdesktop/session`）: 通常の folder を作り `zdesktop --session --glass --socket=$XDG_RUNTIME_DIR/wayland-0`
    （`--session` は p095）。`greeter.service`（`/etc/service.d/greeter`、`replaces=getty_console` は p098 で init が読む）。
- `userland/base/login/verify.c`・`verify.h`（新）: login の照合（passwd と shadow、`!`・`*` の無効、空の password、crypt）を
  切り出し、login と sessiond が共有。`login/main.c` はそれを使う（見た目の振る舞いは同じ）。
- `userland/base/etc/`: `_greeter`（uid・gid 78、`/var/empty`、`/sbin/nologin`、shadow は `*`）。
- `zdesktop` の package が `desktop/sessiond` を要る（zdesktop のある image に sessiond が入る）。
- 試験用: `userland/base/tests/greeter-probe`（`/tmp/greeter-probe` の手順を送る絵の無い greeter）、
  `plan/ws035/tests/config-amd64-login.mk`・`build-login-image.sh`（lean image ＋ probe）、`zdesktop-p094.sh`。

## 検証（amd64、Venus の guest、login image、2026-09-28）

- `plan/ws035/tests/zdesktop-p094.sh` PASS（20 項目）: greeter が uid 78 で動き `/dev/gpu0`・input が 78 の 0600、誤りの
  password と未知の user は 2 秒後に FAIL、不正な要求は ERROR、正しい password で OK、session が uid 1001・
  `XDG_RUNTIME_DIR=/run/user/1001`（drwx------ alice）・HOME・USER で動き `/dev/gpu0` は alice の 0600、`who` に
  `alice seat0`、session の終わりで greeter が再び起き、3 回続けて失敗すると `CONSOLE reason=greeter-failed` で終わり
  device は root の 0666 に戻る。SIGTERM で greeter を終わらせ STOP。option も boot parameter も無ければ
  `CONSOLE reason=boot-parameters`。log は `build/ws035-p094-logs.txt`。
- login の回帰: serial の console で root の login（空の password、verify.c 経由）PASS。
- 規約: 新しい 8 file の style-check 0。build warning 0。
- 画面: この Phase は絵を持たない（greeter の画面は p095）。
- 実機（i915）: 未実施。

## 残り

- kernel の revoke が無いため、前の持ち主の開いた fd は chown の後も使える（設計 §7-1、1 人の user の機械に限る）。
- 画面の lock（g5）、継ぎ目の無い引き継ぎ（g4）。
