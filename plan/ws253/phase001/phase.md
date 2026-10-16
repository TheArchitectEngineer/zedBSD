# ws253-p001 起動の時間の測定と service の順序の変更

- 親: [WS253](../ws.md)、Queue: q1221-i01、状態: in-progress（実装と QEMU の確認は済み。5330 の素の起動の時間の確認が残る）
- 範囲: WS253 の完了の条件 1〜4（5 の QEMU の起動の確認を含む）。
- 手順:
  1. QEMU で今の main の image を起動し、段ごとの時刻を測る（guest の記録・画面。console/serial の log は使わない）。何が待っているか（networkd の notify-timeout・DHCP の待ち・after= の連鎖・sessiond の GPU の待ち等）を特定する。
  2. init の service の定義（userland/base/init/services/、rc.conf の既定）と init・sessiond を直し、通常の起動では sessiond を先に、networkd・audiod・sshd・ntpdate・cron などを後に立てる。debug の起動（kmsg=console・login=console・Ctrl/Shift）では今の順序。
  3. 直した後の時刻を測り、表にする。boot-test.sh で graphical の login を確かめる。
  4. desktop がどうしても依存する service の一覧と理由を書く。

## 結果（2026-10-16 P2、branch agent/p2-ws253）

commit: b2f0919a7（実装・試験の道具）、52df69d05（sysctl の comment の崩れの直し）。base 4fceafa73。

### 設計

- **service の定義の印 `late=yes`**: desktop がすぐには要らない service に付ける。付けた物: networkd・networking・ntpdate・netclock・sshd・cron・audiod・bluetoothd・volumed。付けない物: syslogd・homes・greeter（sessiond）・getty_console・bootlog。
- **init の 2 段の起動**（`userland/base/init/main.c` の start_enabled_services → start_round）: 起動の parameter が `login=graphical` かつ `kmsg=quiet` の時だけ「desktop を先に」。1 段目で late でない service を依存の順に立てる。この段では after= に名がある late の service を待たない（greeter の after=syslogd,networking は syslogd だけを待つ）。requires= で late の service を要る物は 2 段目に回る。2 段目で残り全部（late を含む）を依存の順に立てる。
- **debug の起動は今の順序**: `kmsg=console`（loader の Ctrl）・`login=console`（Shift）・parameter の無い起動は 1 段で、今までと同じ依存だけの順序（network が greeter の前）。
- **判定のため kernel に sysctl `kern.boot.kmsg` を足した**（`include/uapi/sysctl.h` の KERN_BOOT_KMSG 10、`src/kern/sysctl.c` の sysctl_boot_parameter を login と共通に）。user から kmsg= を読む手段が無かったため。HAL は不変。`/sbin/sysctl` の一覧にも足した。
- **起動の時刻の記録 `/var/log/init.log`**: init が起動ごとに作り直す。行は kernel の起動からの秒（ms まで）・event・service（`init started`・`order desktop-first|dependencies`・`start X`・`running X`・`done X`・`failed X`・`replaced X`・`round desktop-started`・`system running`）。sessiond の `at_ms=`・session の `KWL STARTUP … at_ms=` と同じ時計（CLOCK_MONOTONIC）。
- 単純さの理由: 依存の表し方（after=・requires=）は変えず、印 1 つと段 1 つを足しただけ。debug の判定は boot の parameter 2 つだけで、試験用の環境変数は無い。
- 文書: `docs/reference/init-services.md`（Boot order 節、`late` の行、init.log）、`docs/reference/kernel-boot-parameters.md`（kern.boot.kmsg）。greeter.service に順序の注記。
- guardrail に従い、触った file（init/main.c・kern/sysctl.c・uapi/sysctl.h・sysctl/main.c）の comment の WS・BUG の番号を消した。

### 測定（QEMU、KVM、4 CPU・8 GiB、Venus の desktop、NVMe、kei の自動 login）

道具: `plan/ws253/tests/boot-timing.py IMAGE OUTDIR`（Venus の画面を VNC で毎秒、SSH の応答、guest の init.log・sessiond.log・session.log・messages を SSH で読む）。image は `plan/ws253/tests/config-amd64-boottime.mk`（current-uat から clang・libcxx・emacs を外し root の password を残す）を `plan/tools/guest/test-image.sh` で。時刻は kernel の起動からの秒（guest の CLOCK_MONOTONIC）。

| 起動 | init 開始 | greeter の start | networkd running | 全 service の start 済み | session の compose 済み | session に表示を渡す（HANDOFF go） |
| --- | --- | --- | --- | --- | --- | --- |
| 前（main 4fceafa73、3 回） | （記録なし） | （networking の後） | （記録なし） | （記録なし） | 8.89・8.64・8.71 | 9.45・9.20・9.27 |
| 後・通常（kmsg=quiet、4 回） | 5.41・5.45・5.42・5.76 | 5.43・5.48・5.45・5.79 | 5.54・5.63・5.59・6.05 | 5.65・5.73・5.70・6.86 | 8.42・8.59・8.63・9.73 | 8.97・9.14・9.17・10.30 |
| 後・debug（kmsg=console、2 回） | 5.40・5.42 | 5.67・5.70 | 5.55・5.59 | 5.68・5.74 | 8.71・8.76 | 9.27・9.30 |

- QEMU では約 9 秒で desktop になり、**60 秒は再現しない**。内訳: kernel の起動から init まで約 5.4 秒、service の全部で約 0.25 秒（networkd の READY 約 10 ms、networking の oneshot 約 100 ms、homes 約 100 ms）、session の compositor の起動に約 3.5 秒（cursor-shapes 約 1.5 秒、compose まで約 3 秒）。
- 順序の変更で greeter の start は約 0.25 秒早まり（debug の 5.67 → 通常の 5.43）、network 等は greeter の後に立つ（init.log）。QEMU では network の待ちが短いので HANDOFF の差は 0.1〜0.3 秒。実機で network・Wi-Fi・DHCP が秒単位で待つ時は、その分がそのまま desktop の前から外れる。
- 後・通常の 4 回目（after2c）は kernel の起動から遅く（init 5.76）全体が約 1 秒遅い。host の揺らぎと見ている（未解析）。
- 前の 3 回は init.log の無い main の image。前の順序の内訳は、同じ順序を取る後の image の debug の起動の init.log で見た: bootlog → syslogd → volumed → audiod → bluetoothd → homes → networkd（notify）→ networking（oneshot）→ sshd → cron → **greeter** → netclock。
- 記録: worktree の `build/ws253/out-{before1..3,after1,after2a..c,debug1,debug2}/`（git の外）。

### boot-test.sh（QEMU の std VGA、GPU 無し）

`OUTPUT=build/ws253/boot-test plan/tools/boot-test.sh build/ws253/after2.img` → PASS。画面 `/home/awe/zedBSD-worktrees/p2/build/ws253/boot-test/login.png`: syslogd の直後に greeter（sessiond）が立ち、display が無いので console に戻し、2 段目の後に `greeter ended; starting getty_console` → `login:`。Venus の graphical の desktop の画面は `/home/awe/zedBSD-worktrees/p2/build/ws253/out-after1/frames/0014.2.png`（desktop）・`0017.2.png`（Welcome の窓）。

### desktop がどうしても依存する物

- **kernel の GPU の device（/dev/gpu0）**: sessiond は GPU の driver が attach 中（hw.gpu.attaching）の間、最大 120 秒待つ。i915 の attach が遅いと、その分は順序を変えても残る。
- **input の device（/dev/input/event*）**: sessiond が seat の user に渡す。kernel の driver の物。
- **mount -a・swapon**: init が service の前に同期で行う（/home・/var）。
- **syslogd**: sessiond が login を syslog に書く。greeter は after=syslogd。daemon で notify が無いので待ちはほぼ 0。
- **homes**（oneshot、late にしない）: /etc/passwd の home を作る。自動 login の home は sessiond も作るので必須ではないが、約 0.1 秒なので 1 段目に残した。
- 後回しでも desktop が動く物（compositor が再接続する）: networkd（network の panel、1 秒ごと）、audiod（音量・音）、bluetoothd（**Bluetooth の keyboard・mouse は bluetoothd が立つまで使えない**。利用者に見えるのはこの点だけ）、volumed（USB の storage、2 秒ごと）、sshd、cron、netclock・ntpdate（その間の時刻は RTC）。

### 残件

- **5330 の素の起動の時間の確認**（WS253 の完了の条件 5）: 未実施。QEMU で 60 秒が出ないので原因は実機の側（候補: sessiond の GPU の待ち、i915 の attach、networkd の notify（Wi-Fi の radio を止める retire_managed_policy）、DHCP（BUG-326）、kernel の device の probe）。この image の init.log・sessiond.log を 5330 で読めば段ごとに分かる。2026-10-16 15:2x に 5330 を SSH で読もうとしたが P1 の起動の途中で接続できなかった（読むだけ、何も変えていない）。
- desktop を先にする起動でも、2 段目（networkd の notify は最大 10 秒、networking の oneshot）の間 init は子の回収と control socket の応答をしない。その間に sessiond が console に戻った時の getty、`service` の要求、電源の要求は 2 段目の終わりまで遅れる（boot-test で getty が「system running」の後に出たのはこれ）。今は記録だけ。
- oneshot の終わりの検出が 100 ms の poll（ONESHOT_POLL_MS）なので oneshot ごとに最大 0.1 秒。小さいので変えない。
- host の短い試験は無い（init は pid 1 でしか動かない）。確認は QEMU の init.log の順序（通常・debug）と boot-test。
