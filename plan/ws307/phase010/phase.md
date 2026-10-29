<!-- awesome-plan project=zedbsd record=ws307-p010 -->

# ws307-p010 installer の app

- 親: [WS307](../ws.md)。仕様: [spec.md](../spec.md) §4・§9.1・§10・§11.2（J5・T25）。Queue: q1665 / q1665-i08（担当 P4、2026-10-29、worktree p4-ws307l）。
- Status: cleared（2026-10-29 Q1 の判定: QEMU の p010-screens.py PASS、全画面の PNG 21 枚、host の installer-model-test PASS。日本語の画面と実機は未実施。残りは下の「残り」）
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

- build（2026-10-29）: `make -j32 ZEDBSD_CONFIG=plan/ws307/tests/config-amd64-p010.mk BUILD=build/ws307l-img ZEDBSD_INSTALLER_TEST_FILES="<guest.py extra-files>" installer-image` rc 0、`check-amd64-native-image … OK`。installer の 6 つの .o は target の clang で `-Werror` の warning 0（全 build の warning は外部 package の source だけ）。index は 32 unit（clang・libcxx・emacs 抜き）。
- host: `sh plan/ws307/tests/host/installer-model-test.sh`（新規、ASan・UBSan、build の index で catalog.c と disk.c）→ PASS（30 ok: index の読み・後ろの unit への依存・既定の閉包・GTK4 の依存・依存で入る項目、probe の読み、live の disk の拒み、GPT 無し・MBR の「選ぶ」の拒み、他の OS の GPT の「選ぶ」可と十分な ESP、FAT の不明で拒む、空きと置き場所、大きさの文字）。`python3 tools/i18n/tr.py check --strict userland/desktop/locale/ja/installer.tr userland/desktop/locale/installer.keys userland/desktop/installer/` → 239 entries、239 translated、0 problems。
- QEMU: `python3 plan/ws307/tests/p010-screens.py build/ws307l-img/installer-image.img` → **PASS**（`build/ws307-test/p010s.liao5etg`、Venus の GPU、USB で起動、空の NVMe 12 GiB と「他の OS」の SATA 14 GiB（ESP に `\EFI\BOOT\BOOTX64.EFI`、Basic data））。installer が自動で開き（`KWL INSTALLER start`）、QMP の keyboard と tablet で log の control の位置を押して、S1・S2・S3（選ぶ）・S4 の注意の dialog・S4 で 9.7 GiB を作成（`EDIT DONE`、probe の取り直し）・S5・S6 と swap 無しの dialog・S7 と firmware の license・キャンセルの dialog・S10（S4 で書いた変更の一覧）→ やり直して NVMe 全体・S7a・S8・導入の dialog・S9（unpack）・S11（約 7 分）を撮った（21 枚、`shots/`）。撮影は QEMU の VNC の Venus の head（QMP の screendump は GL の scanout の surface が無く失敗する、zdesktop-shot.py の道）。
- 前の 2 回: `p010s._a_2i2yl`（index の依存の解決の順の不具合で CATALOG EINVAL、S1 で止まった。catalog.c を直した）、`p010s.o5x6af_s`（click の後に再描画されない不具合で license の画面の control が log に出ず止まった。押した時に dirty を立てるよう直した）。

## 残り（p011 へ、または未実施）

- 日本語の画面の撮影（catalog は入っている。言語を切り替えた QEMU の絵は未実施）。
- 導入先の起動、S9 のキャンセル・失敗、拒む disk の各画面、4096 の disk、swap file の道は p011 の通しの試験で。
- Boot####（p007 の承認待ち）。S11 は firmware の boot menu で選ぶ案内だけ。
