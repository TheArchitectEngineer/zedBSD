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

## q1221-i02（2026-10-16 P2）: 5330 の記録の解析と、kernel の起動・compositor の起動の続き

commit: d9dbec60d（base 3f9387f72）。

### 5330 の記録（P1 の `plan/ws245/temp/5330/q1collect-base.txt`・`q1collect-p017.txt`、main ed9621e9b の image、読むだけ）

| 起動 | init 開始 | greeter の start | homes | networkd running | networking 終わり | session の開始 | compose の開始→終わり | HANDOFF go |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| base の kernel | 9.95 | 10.05 | 10.05→12.05 | 13.89 | 19.57 | 約 11.95 | 17.82→28.29 | 28.35（表示は MODE switch の 30.06） |
| p017 の kernel | 1.88 | 1.98 | 1.98→2.18 | 3.99 | 4.19 | 約 2.17 | 6.59→16.91 | 16.96 |

- kernel→init の差（9.95 と 1.88）は kernel の i915 の側（P1 の担当）。
- compositor: session の開始から Vulkan の前までに 4.4〜5.9 s（QEMU では 0.07 s）。compose の中は起動ごとに遅い段が違う（base: vulkan-device 1.7 s・wallpaper 2.0 s・glyphs 3.4 s、p017: glyphs-icons 1.6 s・tiles 1.7 s・glyphs 4.9 s）。どの段も file の読み込みか GPU の image の作成を含み、残りは compose の中の計られていない部分（約 3 s）。5330 の root は USB の stick（sda）。
- QEMU で USB の stick を QEMU の throttling で遅くした時（10 MB/s・100 IOPS）、session の開始から Vulkan の前までが 0.07 → 2.5〜3.1 s に伸びる（binary と lib の page-in）。5330 の 4.4〜5.9 s は同じ形。compose の 10 s は QEMU（llvmpipe）では出ず、i915 の driver の側（kernel の中の SPIR-V の compile・GPU の image の作成）か USB の I/O かは、下の細かい STARTUP の行で 5330 で分かる。

### 変えた事

1. **kernel の起動の disk の待ち**（`src/kern/platform/pcat.c`、Q1 の許可）: 今までは USB の disk が来るまで最大 5 秒待ってから NVMe・AHCI を probe していたので、NVMe・SATA から起動する機械は毎回 5 秒待っていた。loader が渡す `boot0=UUID=`（起動した FAT の volume serial）があれば、NVMe・AHCI もすぐ probe し、その serial の FAT がどの disk の partition にも無い間だけ最大 5 秒待つ（disk が増えた時だけ見直す）。`boot0=UUID=` が無い時は今までどおり。HAL は不変。
2. **late の段は desktop の表示を待つ**（`userland/base/init/main.c`）: desktop を先に立てる起動では、1 段目の後に、`desktop=yes` の service（greeter.service に足した）が動いている間、sessiond の印 `/run/desktop-shown`（`userland/base/service/service-config.h` の KERN_INIT_DESKTOP_SHOWN）を最大 30 秒待ってから late の段を始める。待つ間も init は子を回収し control socket に答える（console に戻った時の getty はすぐ立つ）。sessiond は session に GO を送った時と login 画面が READY の時に印を作る（`sessiond_desktop_shown`）。init は起動ごとに印を消す。
3. **一つの起動で各 service を自動で立てるのは 1 回**（`started_once`）: 2 の待ちの間に終わった sessiond（console に戻った物）を 2 段目がもう一度立てていた不具合を、boot-test で見つけて直した。
4. **homes を late に**（5330 で 2 s。自動 login の home は sessiond が作る）。
5. **compositor の STARTUP の行を細かく**（`userland/desktop/wayland/main.c`・`compose.c`・`glass.c`）: `start`（main に入った時刻）・`settings`・`backend`・`power`・`seat`・`prepare`（Vulkan の前の各段）、`vulkan-display`、`wallpaper-images`、`glyphs-fonts`・`glyphs-image`、`image-layouts`（GPU の待ち）・`render-thread`。動作は変えない。
6. 触った file の comment の WS・BUG の番号と plan/ への参照を消した（guardrail）。
7. 文書: `docs/reference/init-services.md`（desktop の key、待ち）、`docs/reference/kernel-boot-parameters.md`（boot0 の待ち）。

### 測定（QEMU、Venus、kernel の起動からの秒）

| 起動 | init 開始 | greeter の start | desktop shown | networkd の start | HANDOFF go |
| --- | --- | --- | --- | --- | --- |
| i01 の後（NVMe、4 回） | 5.41〜5.76 | 5.43〜5.79 | （無し） | 5.53〜6.0 | 8.97・9.14・9.17・10.30 |
| i02 の後（NVMe、3 回） | 0.43・0.40・0.39 | 0.46・0.42・0.40 | 4.06・4.02・4.20 | 4.16・4.12・4.31 | 4.02・3.99・4.16 |
| i02 の debug（kmsg=console、NVMe） | 0.45 | 0.73（networking の後） | — | 0.58 | 4.45 |
| i01 の後（USB、100 IOPS に絞る） | 3.71 | 4.04 | — | 4.74 | 11.93 |
| i02 の後（USB、100 IOPS に絞る） | 3.67 | 4.00 | 10.31 | 10.41 | 10.29 |

- NVMe: 起動から desktop まで約 9 s → 約 4 s（kernel の 5 秒の待ちが消えた）。host の画面で見ても QEMU の起動から desktop まで 14.0 s → 10.6 s。
- 遅い USB: late の段を desktop の後に回して 11.9 → 10.3 s。
- USB の起動（絞らない）: init 0.46、HANDOFF 4.18。待ちの log は出ない（stick は NVMe の probe の間に見つかっている）。
- boot の disk が無い時: virtio-blk（kernel に driver が無い）から kmsg=console の image を起動 → `boot: waiting up to 5 seconds for the boot disk (FAT DC15-A90B)` → `boot: boot-storage wait expired` → `vfs: boot0 selector resolution failed (error 6)` → `VFS initialization failed (6); entering idle.`（今までと同じ失敗の経路）。画面 `/home/awe/zedBSD-worktrees/p2/build/ws253/vblk/shot16.png`。
- boot-test.sh PASS: `/home/awe/zedBSD-worktrees/p2/build/ws253/boot-test-i02c/login.png`（GPU 無し: greeter → console に戻る → すぐ getty、late の段は後）。

### 残件

- 5330 の素の起動での確認（P1 への依頼文は返却に書いた）。compose の約 10 s の内訳は、細かい STARTUP の行で分かる。i915 の側（kernel の中の SPIR-V の compile など）なら P1 の担当。
- USB の stick からの読み込みの遅さ（page-in・小さな file）が 5330 の compositor の前半の 4〜6 s の主な候補。USB storage の driver と VM の page fault の先読みは今回の範囲の外（必要なら別の WS）。

## q1221-i03（2026-10-16 P2）: kernel の起動の段の時刻

commit: 71715e844（base 52c4b1e66）。

### 5330 の記録（P1 の `ws253-boot-8666f25a6-20261016.txt`、main 8666f25a6 の kernel、読むだけ）

- init.log: init started 14.741 → greeter 14.842 → desktop shown 19.542（HANDOFF go 19480、session ready waited 4444）→ late の段 19.542〜29.932（homes 1.6 s、networkd の READY 5.0 s、networking 2.1 s）。
- STARTUP: start at_ms=17332（sessiond の start 14.84 から compositor の main まで約 2.5 s）、compose 2065 ms（うち glyphs-fonts 1541 ms）、output-swapchain 3210 ms・MODE switch 3245 ms（表示は 22.7 s）。
- `sysctl hw.diskstats`（起動の後）: sda read_ops=1693 read_bytes=33 MB **read_ns=92.2 s**（1 回の読み込みの平均 54 ms）、busy_ns=42.3 s。QEMU の USB の stick は同じ数の読み込みで平均 0.68 ms（NVMe 0.40 ms）。dd の 10 MB の連続の読み込みは 0.56 s で速いので、**小さな読み込みが 1 回ごとに長く待つ**。glyphs-fonts（font の file 3 つ、計約 340 KB を read() で読むだけ）の 1.5 s、compositor の exec・lib の page-in の 2.5 s、homes の 1.6 s、kernel→init の一部はこの USB の読み込みの待ちと見ている（usb-storage・xHCI の driver の側、この Phase の範囲の外）。
- output-swapchain 3.2 s は i915 の modeset（P1 の担当）。記録だけ。

### 変えた事

- `kern_log_boot_step()`（`src/kern/klog.c`・`include/kern/klog.h`）: `boot: at N ms: STEP` の行を kernel の log に残す。N は kernel の clock（user の CLOCK_MONOTONIC と同じ、HAL と CPU の準備ができた時が 0）からの ms。kmsg=quiet でも dmesg で読める。既存の行は変えない。
- 段: HAL と CPU（entry.c）、platform の device の発見の始まりと終わり・USB の root の probe・fixed disk の probe・boot disk の発見・boot storage の発見・VFS・init の開始（main.c・pcat.c）、PCI・ACPI・deferred PCI・UCSI の attach（pcat.c）、GPU の attach の始まりと終わり（sysctl.c の kern_gpu_attach_begin/end、i915・nvrtx が呼ぶ）。
- 触った file の comment の WS・BUG の番号を消した。

### QEMU の確認（Venus）

| disk | HAL | PCI | USB の root | fixed disk | boot storage | VFS | init | HANDOFF go |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| NVMe | 0 | 39 | 48 | 155 | 158 | 416 | 0.425 s | 4.028 s |
| USB | 0 | 43 | 57 | 163 | 168 | 529 | 0.544 s | 4.213 s |

（ms。QEMU の Venus は kern_gpu_attach を使わないので GPU の行は出ない。）

## q1221-i04（2026-10-16 P2）: BUG-327 USB の stick の小さな読み込みの待ち

commit: d3b388354（main c91056f4a を merge した上、fast-forward）、90cddaf43（BUG-327 の記録）。

- 計測 `sysctl hw.usb.trace=1`（既定 0、root だけが書ける）: usb-storage の command ごとに `usb-storage-trace:` の 1 行（queue・total と、CBW・data・CSW の各 transfer の「controller に渡す→xHCI の event」「event→USB core の完了」「完了→待つ thread」の µs）。詳しくは [BUG-327](../../bugs/BUG-327.md)。
- QEMU（USB の stick、qemu-xhci）: MSI で 1 command 約 0.4〜0.5 ms、INTx でも同じ。5330 の約 30 ms は QEMU では再現しない（QEMU の device は即座に答える）。原因の特定と修正は 5330 で上の行を取ってから（2026-10-16 ユーザー「P2はQEMUでテストをお願いします。実機は他のチケットに回します。」）。
- 触った file（usb.c・usb.h・pci-xhci.c・kern/sysctl.h）の comment の WS・BUG の番号と design の節の符号を消した。log の文字列 `usb: URB completion contract q009-release-acquire-v1`（usb.c）は試験が読む可能性があるので残した。
