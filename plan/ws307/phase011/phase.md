<!-- awesome-plan project=zedbsd record=ws307-p011 -->

# ws307-p011 通しの試験

- 親: [WS307](../ws.md)。仕様: [spec.md](../spec.md) §12。Queue: q1665 / q1665-i08（担当 P4、2026-10-29、worktree p4-ws307l）、q1665-i10（P4、2026-10-29、worktree p4-ws307n、基点 main 8ba9bdbbb: BUG-356・357 の直しを含む）。
- Status: in-progress（q1665-i10: 5 回目を途中で止めた。q1688（UFS の遅さ BUG-355・358）の後に流し直す）
- 承認: q1665-i01 と同じ（Q1 の中継）。2026-10-29 Q1 の判断: p004 抜きで進める（GTK4 の app の build の確かめだけを p004 の後に残す）。
- 前提: p010（installer の app）、p009（install-admin）、p006・p008 の 4096。

## 方法（技術の判断）

- 1 本の script `plan/ws307/tests/p011-install.py IMAGE`（配置 → 起動 → GUI の導入 → 回収 → 導入先だけの起動 → 要約）。共有の部品は `plan/ws307/tests/installer_walk.py`（p010-screens.py と共有）。
- GUI の操作は AAT の `aat-input`・`keiland-shot` でなく QMP の keyboard と USB の tablet、撮影は QEMU の VNC（Venus の head）。押す場所は installer の log の `INSTALLER CONTROL`（AAT と同じく log の位置から、固定の pixel を使わない）。`keiland-shot` は compositor を `ZEDBSD_TEST_SCREEN_CAPTURE` で build し直す要があり desktop の unit（と index の hash）が変わるので、試験の image の index を製品と同じに保つためにこの形にした（J4 の「unit・index は同じ」）。
- 試験の image は p010 と同じ `plan/ws307/tests/config-amd64-p010.mk`（clang・libcxx・emacs 抜き、試験の hook は OpenSSH と harness の鍵・net.conf だけ）。
- disk（縮めた組、BUG-356 のため）: 空の NVMe 12 GiB と「他の OS」の SATA 14 GiB（ESP 300 MiB に `\EFI\BOOT\BOOTX64.EFI`、Basic data 4 GiB、空き）。
- 場合: 拒む disk（live の USB は S2、空の disk の「選ぶ」は S3）、NVMe 全体の unpack 中のキャンセル（S10、`failed step=unpack`）、unit を壊した NVMe の全体（S10、SHA-256 の不一致）、NVMe 全体（自動 login 切）、「他の OS」の disk に S4 で 1 GiB の swap を作り Basic data を root に（消去の確認、loader は `\EFI\ZEDBSD`）。後で host で他の OS の loader と `\EFI\ZEDBSD\BOOTX64.EFI`・`zedbsd.cfg`、NVMe の導入先だけの起動（boot-test.sh）と導入した利用者の password での SSH。
- 「他の OS」の disk の導入先の起動は未実施: loader が `\EFI\ZEDBSD` にあり、firmware は Boot#### でしか起動しない（p007、ユーザーの承認待ち）。

## 判定の条件（q1665-i10、2026-10-29、走らせる前に書いた）

- image: この branch の tree から `make -j32 ZEDBSD_CONFIG=plan/ws307/tests/config-amd64-p010.mk BUILD=build/ws307n-img ZEDBSD_INSTALLER_TEST_FILES="<guest.py extra-files>" installer-image`（rc 0）。過去の build の成果は入れない。
- `p011-install.py` は BUG-356・357 の直しで 7 台の組に戻した（f7f422552 の形。live の USB の拒みの確かめを足した）: 空の NVMe 512（12 GiB）・4096 の NVMe（8 GiB）・「他の OS」の SATA（ESP に `\EFI\BOOT\BOOTX64.EFI` と `\OTHER\NOTE.TXT`、Basic data A 2 GiB を乱数で、Basic data B 4 GiB、空き）・zedbsd.cfg の FAT の SATA・MBR の SATA・空の GPT の USB（9 GiB）・1 GiB の USB、と live の USB。
- PASS: 最後の行が `p011-install: PASS` で、次の STEP が全て ok（1 つでも fail か途中の FAIL なら FAIL）:
  - map（7 台を大きさで見つける）・refused-tiny（S2 で 1 GiB の disk が押せない）・refused-live（S2 で名の無い live の USB がちょうど 1 台押せない）・refused-mbr・refused-zcfg（S3 で「選ぶ」が押せず「全体」は押せる）。
  - cancel（NVMe 512 の全体を unpack 中にキャンセル → S10、log に `failed step=unpack`）。damage・failure（4096 の NVMe の全体、壊した unit → S10、`INSTALLER INSTALL failed`）・restore。
  - nvme（NVMe 512 の全体、自動 login 切 → S11）・nvme-harness（導入先の root に試験の net.conf、passwd に alice、rc.conf に hostname）・k4（4096 の全体 → S11）。
  - other（S4 で 1 GiB を作り swap、Basic data B を root、消去の確認 → S11、log の `STATE loader` に ZEDBSD）・usb（空の GPT の USB に S4 で 6 GiB を作り root、swap 無し → swap file → S11、log に `STATE swap file`）。
  - host の検査（live を止めた後、image の file を直に読む）: other-untouched（Basic data A の SHA-256 が前と同じ）・other-loader-kept・other-file-kept・other-loader-ours（`\EFI\ZEDBSD\BOOTX64.EFI` が MZ で他の OS の物と違う）・other-cfg（zedbsd.cfg に `kernel=rootpart:/boot/vmunix` と `swap0=PARTUUID=`）。
  - boot-nvme・boot-k4（`BOOT_NVME_BLOCK_SIZE=4096`）・boot-usb: 導入した disk だけで `plan/tools/boot-test.sh`（画面を撮って login prompt を読む）が rc 0。
  - ssh-nvme: 導入した NVMe だけで起動し、導入した利用者 alice の password で SSH（`id -un` が alice、hostname が p011host）。
- 判定に使う物: guest の中の session の log（`/run/user/1000/session.log`、SSH で読む。QEMU の console・serial の log ではない）、QMP の操作の結果、VNC の画面の PNG、host での disk の file の検査、boot-test.sh、SSH。QEMU の console・serial の log では判定しない。
- 日本語の画面: `p010-screens.py --language ja IMAGE`（ui.language を live の利用者で 1 に。Settings の Languages の page が書く同じ設定。installer が再起動なしで `INSTALLER LANGUAGE language=ja` を出す）が `p010-screens: PASS` で、撮った PNG（S1〜S11・S7a と dialog）の文字が日本語であることを PNG を見て確かめる（目で見た結果、ユーザーにも PNG を渡す）。
- QEMU: P4 の物は `build/ws307-test/` の下の各回の directory（guest.py の runtime、port は guest.py が空きを選ぶ）。同時に 1 台（p011 の後に ja、ja と p011 の導入先の起動は重ねない）。各回に timeout（live の wait 600 s、導入の終わり 1800 s・USB 4800 s、boot-test 500 s）。

## 結果

- 2026-10-29 の 1 回目（`build/ws307-test/p011.vq3t9c4w`、7 台の disk: NVMe 512・4096 の NVMe・「他の OS」の SATA（3 partition、1 つを乱数で）・zedbsd.cfg の FAT の SATA・MBR の SATA・空の GPT の USB・1 GiB の USB）: live の system が SSH に出ない（400 s。VGA は起動の splash のまま、gdbstub で全 CPU が sched_idle）。1 回ずつの切り分け: NVMe 512 だけ → 起動（88 s・56 s）、4096 の NVMe だけ・NVMe 512＋4096・空の GPT の USB だけ・3 partition の SATA だけ → 起動しない。Q1 が BUG-356 に登録（再現の script は `build/tmp/bug356-repro.py`、worktree の中、git に入れない）。
- 縮めた組（NVMe 512＋「他の OS」の SATA、p010 と同じ組）の 2 回（`p011.0yxo6eq4`、`p011.8egqibob`）も live の system が SSH に出なかった。p010-screens.py の対照の 2 回（`p010s.61y2ae0f`、`p010s.ortbr5wz`）は SSH は出たが、installer が最初の `kl_window_present_resize` の EIO で終わった（`INSTALLER FAILED operation=present error=5`）。同じ組で 13:00 前の p010 の 3 回目は PASS しているので、起動しない・present の EIO は disk の組だけで決まらず、host の Venus（virgl_render_server・llvmpipe）の状態か時刻で揺れると見られる（未確認）。installer は present とその作り直しの失敗を何回か待って再試行するよう直した（be0f02fa1・daf0c60fe）。
- その後の 1 回（`p011.tcubp4gi`、daf0c60fe の image、縮めた組）も live の system が SSH に出なかった。p011 の受け入れ（GUI の通しの導入と導入先の起動）は未達。BUG-356 の解析の後に `python3 plan/ws307/tests/p011-install.py IMAGE` を流し直す（縮めた組、BUG-356 が直れば外した disk を戻す）。

- q1665-i10（P4、2026-10-29、image `build/ws307n-img/installer-image.img`、この branch の tree から `make … installer-image` rc 0）:
  - 1 回目（`p011.64jmegwb`、16:48、前の起動）: live の system が 600 s で SSH に答えず FAIL。後の 4 回では再現せず、解析していない（未確認）。
  - 2 回目（`p011.rx1mdyms`）: live は SSH に上がった（7 台＋live の USB、Venus）。STEP map が fail: 試験の script の誤り（`installer_walk.Run.root` が command を quote せず guest.py の `run` に渡し、`sh -c` が最初の語だけを実行）。1ab8b8169 で `shlex.quote`。
  - 3 回目（`p011.g7w4mwzu`）: map・refused-tiny・refused-live ok、MBR の disk（2 GiB）が S2 で「小さすぎる」で押せず FAIL。試験の disk の大きさの誤り（spec §5.6: 全体の最小＝ESP＋swap 1 GiB＋root の最小）。def85c332 で MBR の disk を 4 GiB に。
  - 4 回目（`p011.hfdv7fqg`）: map・refused-tiny・refused-live・refused-mbr・refused-zcfg・cancel（NVMe 512、約 9 分で S10、`failed step=unpack`）・damage ok。failure（4096 の NVMe の全体、既定の項目 165 MiB）が 1800 s で S10 に届かず FAIL: 4096 の native UFS の書きが 22 分で約 50 MB（BUG-358）。ae125c7d4 で回避（failure を NVMe 512 へ、4096 は最後に S7 で desktop・firmware・fonts を外す、timeout 3600 s）。
  - 5 回目（`p011.tjf9naoi`）: refused-zcfg まで ok、cancel の途中（23:23）でユーザーの指示により止めた（q1688: 先に UFS の遅さ BUG-355・358 を直してから流し直す。4096 の導入は既定の項目に戻す）。
  - 条件の変更（走らせた後）: ae125c7d4 の回避（4096 の項目を減らす）は q1688 の後に戻す（ユーザーの指示）。
- p011 の受け入れは未達（MBR・zedbsd.cfg・1 GiB・live の拒みの GUI の確かめは 4・5 回目で ok）。

## 未実施・残り

- q1688（BUG-355・358 の直し）の後に p011 を流し直す（4096 の導入を既定の項目に戻して）。その後に日本語の画面（`p010-screens.py --language ja`）。
- 導入（cancel の後の failure・nvme・other・usb・k4）・host の検査・導入先の起動・SSH は未実施。
- GTK4 の app の build の確かめ（p004 の後、Q1 の判断）。
- Boot#### の確かめ（p007 の承認待ち）。
- 同じ image を dd した disk との同居（spec §11.2 の既知の制限: 同じ image の 2 台は PARTUUID が重なり曖昧）。
