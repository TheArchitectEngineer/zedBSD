<!-- awesome-plan project=zedbsd record=ws129-p005 -->
# ws129-p005: release notes・既知の問題の一覧・利用の手引き

Status: planning（p001 と各 WS の成果を待つ、10/13 頃に集める）
Disposition: normal
Parent: [WS129](../ws.md)
Focused goal: fg019（ベータ1）
Queue: none
目安: 2〜3h

## 範囲

1. release notes: 新機能（fg019 の各 WS の到達を ws.md から集める。到達していないものは書かない）、対象 platform（Latitude 5330・5320、各々の制限）、動作の確認の範囲（QEMU と実機を分ける）。
2. 既知の問題の一覧: [Bug Board](../../known-bugs.md) の open の行から、利用者に見えるものを選び、回避の方法を添える。WS の実機の Phase（ws005-p023・ws118-p004・ws119-p006）の不合格を加える。
3. 利用の手引き: USB への書き方（img.gz の展開、Windows・Linux・macOS）、BIOS の設定（UEFI の boot、Secure Boot を切る要否）、初回の login、WiFi の接続（Settings）、使える USB の LAN・WiFi の adapter、インストーラ（WS119 が載れば）。
4. 置き場所は p001 で決めた所（例 `docs/release/beta1.md`）。CI の本文はその file から作る。

## 受け入れ

文書の草稿を main とユーザーに review してもらう（ユーザーの確認が要る）。

## 所有 path

p001 で決めた文書の path、`plan/ws129/`。

## 依存

p001、各 WS の成果。

## 未決の判断

なし（内容の review はユーザー）。

## 追加（2026-10-05 Q1、ws122-p001 の P2 の依頼）

release notes に FFmpeg（libavcodec ほか、LGPL 2.1 以降）の告知と source の在り処（FFmpeg 9.0.2 の tarball の URL と sha256、zedBSD の build の Makefile）を書く。image には `/usr/share/licenses/ffmpeg/` に COPYING.LGPLv2.1 と LICENSE.md が入る。

## 下書き（2026-10-09、q920、P1）

Q1 の割り当て（q920 (2)）。言語は英語（release.md §9 U13）、置き場所は docs/release/（U8）。
- release notes の下書き: `docs/release/zedbsd-1.0.0-beta2.md`（release.yml が `docs/release/zedbsd-<VERSION>.md` を本文にする）。Downloads・Signing in・機能（desktop・app・hardware・開発）・license と FFmpeg の source（tarball の URL と sha256、Makefile）。
  確かめていない・条件付きの行は HTML の comment の `review:` に理由と差し替えの文を書いた（GitHub では見えない、RC で消す）。
- 2026-10-09 ユーザーの決定（Q1 の中継）: WS143 Bluetooth と WS083 Vulkan Video は入れ、間に合わなければ 10/16 に OFF。両方「入る」で書き、OFF の時の差し替えの文を comment に用意した。Vulkan Video が `i915.debug=video` の門のままなら、その旨を行に書く（p008 の既定化しだい）。
- 既知の問題の更新: `docs/release/zedbsd-1.0.0-beta2-known-issues.md`。resolved の BUG-095・156・157・166・167・168・171・172・174・175・176・177・190 を外し、BUG-253/255（蓋）・189/212（有線と Wi-Fi）・222・205・206/207・203・271 を足した。RC までに直す予定の物（BUG-184・188・235・232・179/180・234・191・238/242）と Bluetooth の行は comment に置き、RC で残っていれば表へ移す。
- 判断が要る点（Q1・ユーザー）: FFmpeg の LGPL 2.1 §6 の source の提供。ffmpeg.org への link だけより、release の asset に `ffmpeg-9.0.2.tar.xz` を載せる方が確実（release.yml の変更、p004 の範囲）。
- 未実施: 機能の行の一つ一つの実機の確認（p007）。link は最終の tag ができるまで切れる。

2026-10-09 ユーザーの決定（Q1 の中継、クリック）: FFmpeg の source は「ffmpeg.org への link だけ」。release notes の節を書き直した（版 9.0.2、ffmpeg.org と tarball の link と sha256、zedBSD は patch を当てず configure の引数は GitHub の tree の package の Makefile）。release.yml は変えない。review の comment を外した。
既知の問題の下書きの comment から plan の file の名前を外した（docs から plan へ参照しない）。release notes の file（docs/release/zedbsd-1.0.0-beta2.md）は前の commit で git に入っていなかったので、8a050dff0 で加えた。

## 2026-10-09 夜 P1: review の comment の整理

- 既知の問題: ユーザーが確かめて close した BUG-271（IPP の PDF、表の行）、BUG-235・BUG-232・BUG-179/180（RC までに直す予定の comment の行）を外した（BUG-219・237 は元から無い）。comment に残るのは BUG-184・188・234・191・238/242 と、BUG-253（UAT 待ち）・BUG-189/212（UAT で再現しなければ close）・Bluetooth の条件。
- release notes: Hardware の 2 つの comment（Bluetooth・Vulkan Video の OFF の差し替えと古い注記）を 1 つにまとめ、IPv6 の待ちを T1-478（音量の試験で誤り）から networkd の再試験 T1-500 に直した。操作パネルの comment を T1-496 の結果（QEMU は目視を除き pass、UAT の 1・6・7 待ち）に。
- RC（10/13）で残す判断: link（tag の後に有効）、Windows の zip（`ZEDBSD_RELEASE_ZIP := n` のまま、行は無し）、Phone・Mail を preview と書くか（ユーザー）。

## 2026-10-14 P3: 10/13〜14 にベータ2 に入った物と決定の書き足し

Q1 の割り当て（材料は plan/beta2.md・master.md・各 ws.md と bug の ticket、読むだけ）。英語、既存の文体と review の comment の形に合わせた。docs/ から plan/ への link は無い（grep で確かめた）。browser（WS074）の節は触っていない。

- release notes（`docs/release/zedbsd-1.0.0-beta2.md`）
  - The desktop: 画面の回転（WS217、90°・180°・270°、touch・pen・pointer が追従、Beta 2 は `~/.config/keiland/displays.conf` の `rotate=` の行だけで Settings の操作は無い）。review の comment（file を手で書く機能を載せるかの判断）。
  - Hardware: BUG-303（TSC が分かる時は TSC で LAPIC の timer を較正、PIT の止まった desktop が起動する）。review の comment（13400F・5330 の実機の確かめ待ち、失敗なら行を消す）。
  - Hardware〔判定待ち〕: WS229 Gen9・Gen9.5（6〜10 世代、既定 off、`i915.gen9=on`、GT と Vulkan だけ、firmware の framebuffer のまま、Vulkan Video 無し、suspend を断る）、WS231 AHCI（Intel の RAID mode も、suspend を越えて動く、NCQ・TRIM・hotplug・ATAPI 無し）。〔判定待ち〕の扱い（入れば印を消す、入らなければ行と既知の問題の行と手引きの節を消す）を comment に。
  - For developers: emacs の SKK（BUG-302、単独の n、compact と SKK-JISYO.X の辞書）、`/bin/vi`（WS228、UTF-8・多段 undo・`.`・回復の file・入れない物）、kernel の VT console の cup・SGR、useradd・usermod・userdel（WS196、usermod は名前・ID・home を変えない＝-d・-m 無し、account-admin の直し: 管理者の数え方・uid を使い回さない・lock した人は autologin しない）、menuconfig（WS227 の表示名と画像・PDF の library の Desktop への移動、WS232 の i386 PC-9800・PC/AT と Retro）。
  - 新しい節「Keiland on Linux and FreeBSD」: WS143 p007 の Linux の BlueZ の Settings → Bluetooth（Connect の失敗の文、Cancel で pairing をすぐ終える）、FreeBSD の未対応の表示、WS227 の `/opt/keiland/sbin/passkey`（host にある命令は入れない）。review の comment（Debian の QEMU+KVM と btvirt だけ、FreeBSD の頁と passkey の FreeBSD は未実施）。
- 既知の問題（`docs/release/zedbsd-1.0.0-beta2-known-issues.md`）
  - Computers〔判定待ち〕: Gen9 の制限（display の driver 無し、Vulkan Video 無し、suspend 不可、RC6 off で電池が早く減る）、AHCI の制限（NCQ・TRIM・hotplug・ATAPI 無し、ALPM・DevSleep を入れない理由、RST の後ろの NVMe は見えない）。
  - Touch: 開く前に範囲にあるペン（F-104、回避は離して戻す）。F-104 は compositor の tablet.c の問題で Gen9 とは関係しないので、一般の行にした。
  - Network: BUG-281 の疑い（QEMU の SSH の止まり・起動直後の命令が走らない、原因不明）。
  - Desktop and applications: vi の幅の差（F-103、結合文字・Ambiguous の文字で cursor が 1 桁ずれる）。
  - review の comment: 〔判定待ち〕の扱いと、SSH の行が project の QEMU の試験でだけ間欠に見えた疑いであること。
- 利用の手引き（`docs/release/zedbsd-1.0.0-beta2-guide.md`）
  - 新しい節「Older Intel graphics」〔判定待ち〕: USB の FAT の partition の `zedbsd.cfg` の末尾に `i915.gen9=on` を足す手順と制限（release notes から `#older-intel-graphics` で link）。review の comment（Windows・macOS での手の確かめは無い、CR LF は loader が受ける＝`bootloader/uefi/zedbsd-config.c`、行の足し方は `ZEDBSD_BOOT_EXTRA_LINES` と同じ）。
  - Building it yourself: menuconfig の表示名・Retro・i386 で Desktop を外す、Linux・FreeBSD の build が `/opt/keiland/sbin/passkey` など host に無い zedBSD の命令を入れる。

確かめた source: `userland/desktop/wayland/displays.h`（rotate= の書式と置き場所）、`userland/base/useradd|usermod|userdel/main.c` と `docs/reference/user-administration.md`（option と入れない物）、`userland/base/passkey/Makefile.linux|freebsd`（sbin、password か PIN）、`plan/ws228/phase001`・`phase004`（vi の範囲、SGR の番号）、`plan/ws229/phase002`・`phase005`（`i915.gen9=on`、zedbsd.cfg の行）、`plan/ws231/phase001`（RAID の bind、suspend、入れない物）、BUG-302・303・281 の ticket、F-103・F-104。

未実施: 文書の手順の実機の確かめ（Gen9 の zedbsd.cfg の編集を含む）。

残り（Q1）:
- 18 時のコードフリーズの判定で〔判定待ち〕の印を消すか、行（release notes の 2 行、既知の問題の 2 行、手引きの節）を消す。
- beta2.md の WS129 の行は「HFP・DSDT の行を反映済み」とあるが、既知の問題に DSDT の行は無い（HFP の行は在る）。DSDT の決定の中身が分からないので今回は足していない。Notes の描く道具の節も未（今回の範囲外）。
- WS074（browser）の最終の結果は Q1 が書く。
