<!-- awesome-plan project=zedbsd record=ws307-p010 -->

# ws307-p010 installer の app

- 親: [WS307](../ws.md)。仕様: [spec.md](../spec.md) §4・§9.1・§10・§11.2（J5・T25）。Queue: q1665 / q1665-i08（担当 P4、2026-10-29、worktree p4-ws307l）。
- Status: in-progress
- 承認: q1665-i01 と同じ（2026-10-29 ユーザー「P4にWS307」、Q1 の中継）。HAL の API・toolchain には触れない。Boot#### は p007 の承認の後（S11 は案内だけ）。
- ユーザーの要望（原文 2026-10-29）:「インストーラはスクリーンショットをぜひ見せてほしいです。楽しみです。」→ S1〜S11・S7a を QEMU の screendump で撮り `build/` の下の path を Q1 へ。

## 設計（2026-10-29）

- 置き場所: `userland/desktop/installer/`（`main.c` 窓・入力・log、`helper.c` install-admin の子の process と行、`catalog.c` index の項目と依存の閉包・license、`disk.c` probe の記録と選べる物の規則・空き・大きさ、`flow.c` 画面の順・検査・dialog・要求、`screens.c` 描画）。共有の `installer.h`。
- 登録: `userland/desktop/installer/Makefile` に `installer`（platform amd64、default n、class basic、SELECTABLE n、menu installer、`/bin/installer`）。link は `platform/amd64/installer-image.mk`（kuidemo と同じ形: libvulkan・libwayland-client・libkeiland・libtruetype・libc）。live の tree だけに入る（install-admin と同じ）。
- helper との話: probe は session が無い時は `install-admin probe` の子、session がある時は session に `probe`。session は S4 の最初の書き込みか S8 の開始で始め、disk に結び付く（S2 で別の disk を選ぶと入力を閉じて終える）。行は pipe から `kl_app_watch_fd` で受け、`end`・`DONE`・`FAIL` で要求の終わり。中止は stdin の `cancel`。admin の session の probe が失敗した時に `FAIL probe` を出すよう直した（以前は何も出さず installer が待ち続ける形だった）。
- 画面: 1 つの panel、上に題と「キャンセル」、中は scroll の pane、下の帯に「戻る」・step の点・次へが無効の理由・「次へ」（S8 は「インストール」、danger）。Enter で次へ、Alt+←/→、Esc でキャンセルの確認。dialog は `kl_dialog`（Enter が主、Esc が最後）。
- 検査（次へを無効にして理由を 1 行）: S1 probe・UEFI・index、S2 選べない disk（live・読み取り専用・file・sector・小さい）、S3「partition を選ぶ」が選べない disk（GPT でない・編集できない GPT・16 entry・FAT の `zedbsd.cfg` が有るか不明・十分な ESP が無い）、S4 十分な ESP、S5 root の最小、S6 swap 64 MiB、S7 root の大きさと firmware の license の同意、S7a 名前・password・hostname の規則（live の `/etc/passwd` の system の名前は拒む）、S8 全体の道の「消してよい」。
- AAT の log（session log、stderr）: `INSTALLER READY`、`INSTALLER SCREEN name=s1-welcome…`、`INSTALLER LAYOUT screen= dialog= controls=N` と `INSTALLER CONTROL name= x= y= width= height= enabled=`（窓の座標、変わった時だけ）、`INSTALLER PROBE`・`EDIT`・`INSTALL`・`DIALOG`。
- 翻訳: domain `installer`、`userland/desktop/locale/installer.keys`（表の文字）と `userland/desktop/locale/ja/installer.tr`（239 件、`tr.py check --strict` 0 problems）。OS 名は `/etc/os-release` の NAME（無ければ `ZEDBSD_OS_NAME`）。
- 自動起動: compositor の `desktop_welcome`（`userland/desktop/wayland/desktop.c`）が `/opt/installer/index` のある時に `/bin/installer` を 1 回起動し、Welcome は出さない。App Home: `apps.conf` に `Installer|/bin/installer|…|installer`（path が無い image では隠れる）、`icons.c`・`icons.h` に installer の絵（disk に下向きの矢印）。
- 試験の image: `plan/ws307/tests/config-amd64-p010.mk`（clang・libcxx・emacs を外す。`Makefile` の installer の profile に試験だけの `ZEDBSD_INSTALLER_TEST_OMIT`、`installer-image.mk` に `ZEDBSD_INSTALLER_TEST_ITEMS`（Clang の項目の無い `plan/ws307/tests/installer-items-p010`））。

## swap file の書き込みの遅さ（Q1 の依頼、2026-10-29）

- installer の側（`admin/session.c` の `session_install_swapfile`）は既に 1 MiB ずつの `write` と最後の 1 回の `fsync` で、小さい write・sync の繰り返しではない。installer の側で直せる所は無い。
- 遅さは kernel の native UFS の書き込みの道（`src/drivers/fs/ufs.c`、journal の付いた block の割り当て）と見られる（p006 の `p006-ufs-speed.sh` の 16 MiB の dd が 512 で 23 秒と同じ桁、約 0.7〜1 MB/s）。原因の特定は範囲の外（Q1 に返す）。

## 結果

（実施中）
