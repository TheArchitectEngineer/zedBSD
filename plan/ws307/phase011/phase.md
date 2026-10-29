<!-- awesome-plan project=zedbsd record=ws307-p011 -->

# ws307-p011 通しの試験

- 親: [WS307](../ws.md)。仕様: [spec.md](../spec.md) §12。Queue: q1665 / q1665-i08（担当 P4、2026-10-29、worktree p4-ws307l）、q1665-i10（P4、2026-10-29、worktree p4-ws307n、基点 main 8ba9bdbbb: BUG-356・357 の直しを含む）。
- Status: cleared（2026-10-30 Q1 の判定。q1665-i10: p011-install.py PASS（12 回目）・日本語の画面 PASS（p010-screens.py --language ja の 5 回目）。Boot#### は p007 の承認待ち・GTK4 の app は p004 の後で範囲外）
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
  - size-refused（2026-10-30 Q1 の追加、走らせる前に書いた）: 「他の OS」の S4 で読めない大きさ「abc」で「作成」→ log に `INSTALLER EDIT refused reason=size`、画面に誤りの文、helper の session と `EDIT request` が無く partition の数が同じ。
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
  - q1688-i01（P4、2026-10-29〜30）で UFS の遅さを直した（6387b82a0: mkfs の native が tail journal を書かない、4096 の batched journal の sector の単位。[BUG-355](../../bugs/BUG-355.md)・[BUG-358](../../bugs/BUG-358.md)）。image は `build/q1688-img/installer-image.img`（この branch の tree、`make … BUILD=build/q1688-img … installer-image` rc 0）。ae125c7d4 の回避は 9d4baeaa3 で戻した（4096 も既定の項目、failure は 4096 の disk）。
  - 6 回目（`p011.yygfuq4e`）: live が SSH に上がらず FAIL。gdbstub で `klog_buffer` を読んだ: live の USB stick で xHCI の command 15 が時間切れ、BOT の転送が error 17/42、root が書けず `/sbin/init` の exec が EIO。host の fdatasync は中央値 1 s（load 12〜15）。Q1 が [BUG-359](../../bugs/BUG-359.md) に登録。試験の対処（Q1 の了承）: stick と 7 台の disk を cache=unsafe に（`installer_walk.Run.start` の `-set drive.boot.cache=unsafe`、p011 の drive）。
  - 7 回目（`p011.3xwutjtb`）: map・refused-*・cancel・damage・failure（4096 の全体、壊した unit → S10、`failed step=unpack errno=5`、約 2 分）・restore・nvme・nvme-harness が ok。nvme の後の S11 に「やり直す」が無く（S11 は「閉じる」と「再起動」だけ、spec §4.11）FAIL。試験の誤り: S11 の後は installer を閉じて live の session で開き直す（`Walk.again`）に直した。
  - 8 回目（`p011.u2sndh3h`）: 開き直しが働き、k4（4096 の全体、既定の項目、S11）まで ok。「他の OS」の S4 で大きさ「1 GiB」を入れて「作成」を押した後、`INSTALLER EDIT` の行が 180 s 出ず FAIL（installer の log に helper の session も無い。画面を撮っていなかったので原因は未確認）。S4 の大きさを入れた後と FAIL の時の画面を撮るよう直した。
  - 9 回目（`p011.fl2cf9xe`）: 8 回目と同じ所で FAIL。S4 の画面（`19-other-s4-size.png`・`20-failed.png`）で欄が「7890 MiB1 GiB」になり、installer は「Type a size such as 20 GiB or 512 MiB.」を出して何も作らなかった（log の行は無い）。切り分け（`build/ws307-test/probe.*`）: live の session の欄で Backspace が空白を消さない（Q1 が [BUG-360](../../bugs/BUG-360.md) に登録）。`set_field` を Ctrl+A と打ち込みに直した（bb56483e0）。Q1 の指示で、読めない大きさを log にも出すよう installer を直し（`flow.c` の `EDIT refused reason=size|room`、67a3dd649）、p011 に size-refused を足した。
  - 10 回目（`p011.4yxae838`、67a3dd649 の前の image）: other まで ok。USB の S4 で「すぐに書く」の dialog を閉じていなかった（試験の誤り）ので FAIL。1d177cb34 で直した。
  - 11 回目（`p011.r99kxhe0`、67a3dd649 の image）: size-refused（`EDIT refused reason=size parts=3->3`）・other まで ok。空の GPT の USB の S4 で ESP と partition を作った後、「次へ」が押せないまま FAIL（「The disk needs an EFI system partition…」）。WS307 の不具合: `disk.c` の `disk_esp_enough` が kernel の identity の fs の型 `vfat` を求めていたが、identity は partition が現れた時に決まり、S4 が後で FAT32 にした ESP は型が空のまま（blkid に TYPE 無し、ESP の先頭には FAT がある、`build/ws307-test/probe.9shbdri3`）。helper は FAT を mount して見ているので、型を求めるのをやめた（01b12baa6）。host の `installer-model-test.sh` にこの場合を足した（直す前 FAIL、後 PASS）。
  - 12 回目（`p011.9yb97ua0`、01b12baa6 の tree の `build/q1688-img`）: **PASS**。`p011-install: PASS`、STEP 25 個が全て ok: map・refused-tiny・refused-live（`disk-sdc`）・refused-mbr・refused-zcfg・cancel・damage・failure（4096 の NVMe の全体、既定の項目、`failed step=unpack errno=5`）・restore・nvme・nvme-harness・k4（4096 の全体、既定の項目、S11）・size-refused・other（`STATE loader` に ZEDBSD）・usb（`STATE swap file`）・other-untouched・other-loader-kept・other-file-kept・other-loader-ours・other-cfg（`kernel=rootpart:/boot/vmunix`・`swap0=PARTUUID=`）・boot-nvme・boot-k4・boot-usb（`boot-*/login.png`、boot-k4 の PNG は目で見て login prompt）・ssh-nvme（`alice`・`p011host`・導入した unit の記録）。画面の PNG は `shots/` に 33 枚。導入の時間は 4096 の全体でも約 2 分（q1688 の前は 30 分で終わらなかった）。
- 判定の材料: 上の STEP（guest の session の log を SSH で読む、QMP、VNC の PNG、host の disk の file の検査、boot-test.sh、SSH）。QEMU の console・serial の log は使っていない。6 回目の解析は gdbstub で kernel の `klog_buffer` を読んだ。
- 試験の条件の補い: 試験の disk と stick は cache=unsafe（BUG-359 の回避、Q1 の了承）。S11 の後は installer を閉じて開き直す（spec §4.11 の S11 は「閉じる」と「再起動」だけ）。欄は Ctrl+A と打ち込み（BUG-360 の回避）。

## 日本語の画面（q1665-i10、2026-10-30）

- `python3 plan/ws307/tests/p010-screens.py --language ja build/q1688-img/installer-image.img`。live の利用者として Settings の言語の設定 `ui.language` を 1 にする。設定の client（`userland/tests/keiland-settings`）は image に入っていないので、build の `bin/keiland-settings` を guest に写して使う（635ee8306）。その後、installer が再起動なしで `INSTALLER LANGUAGE language=ja` を出す。
- 1 回目（`p010s-ja.3w3y17pt`）: keiland-settings が image に無く 127 で止まった（上の 635ee8306 で直した）。2 回目（`p010s-ja.5qnw_lx0`）: 21 枚を日本語で撮れたが、最後の導入が unpack 中に errno 42 で S10 になった（host の load 25〜27、boot.img の写しに 9 分）。p010 の disk も cache=unsafe にした（c5a8f4f79）。
- 3 回目（`p010s-ja.0h0x1im2`）: **PASS**（`p010-screens: PASS`、21 枚: S1・S2・S2 で選んだ所・S3・S4 の注意・S4・S4 で作った後・S5・S6・swap 無しの dialog・S7・S7 の下・license・キャンセルの dialog・S10（キャンセル）・S3（全体）・S7a・S8・導入の dialog・S9・S11）。PNG を目で見て確かめた。窓の題名「Installer」が英語のままだった（題名は窓を作る時だけ付けていた）ので、言語が変わった時に付け直すよう直した（f703b3f28）。3 回目の画面で「インストーラ」になったことを確かめた。英語で残った所が 2 つあった: S10 の副題「Nothing was installed」（catalog に無かった）と S2 の「パーティションテーブル: none」。訳を足した（4a587ed1f、`tools/i18n/tr.py check --strict` は 241/241）。disk の label（"EFI system partition" など、他の OS が付けた物）は data なので、そのまま出す。
- 4 回目（`p010s-ja.5_a96zmo`、4a587ed1f の image）: S2 で「パーティションテーブル: none」のままだった（probe が表の無い disk に `none` を返し、それを訳さず出していた）。S2 を見た所で止め、`screens.c` で `none` も訳す形に直した（c9662274b）。
- 5 回目（`p010s-ja.7p9co8ca`、その image）: **PASS**（`p010-screens: PASS`、21 枚）。題名「インストーラ」、S2「パーティションテーブル: なし」、S10 の副題「何もインストールしていません」を PNG で確かめた。
- p011 の通し（12 回目 PASS）は 01b12baa6 の image で流した。その後の installer の変更（題名・訳・`none`）は表示だけで、p011 は流し直していない。

## 未実施・残り

- GTK4 の app の build の確かめ（p004 の後、Q1 の判断）。
- Boot#### の確かめ（p007 の承認待ち）。
- 同じ image を dd した disk との同居（spec §11.2 の既知の制限: 同じ image の 2 台は PARTUUID が重なり曖昧）。
