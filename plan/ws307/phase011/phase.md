<!-- awesome-plan project=zedbsd record=ws307-p011 -->

# ws307-p011 通しの試験

- 親: [WS307](../ws.md)。仕様: [spec.md](../spec.md) §12。Queue: q1665 / q1665-i08（担当 P4、2026-10-29、worktree p4-ws307l）。
- Status: in-progress（BUG-356 で止まっている: 試験の disk を付けた live の system が QEMU で起動しない。GUI の通しは未達）
- 承認: q1665-i01 と同じ（Q1 の中継）。2026-10-29 Q1 の判断: p004 抜きで進める（GTK4 の app の build の確かめだけを p004 の後に残す）。
- 前提: p010（installer の app）、p009（install-admin）、p006・p008 の 4096。

## 方法（技術の判断）

- 1 本の script `plan/ws307/tests/p011-install.py IMAGE`（配置 → 起動 → GUI の導入 → 回収 → 導入先だけの起動 → 要約）。共有の部品は `plan/ws307/tests/installer_walk.py`（p010-screens.py と共有）。
- GUI の操作は AAT の `aat-input`・`keiland-shot` でなく QMP の keyboard と USB の tablet、撮影は QEMU の VNC（Venus の head）。押す場所は installer の log の `INSTALLER CONTROL`（AAT と同じく log の位置から、固定の pixel を使わない）。`keiland-shot` は compositor を `ZEDBSD_TEST_SCREEN_CAPTURE` で build し直す要があり desktop の unit（と index の hash）が変わるので、試験の image の index を製品と同じに保つためにこの形にした（J4 の「unit・index は同じ」）。
- 試験の image は p010 と同じ `plan/ws307/tests/config-amd64-p010.mk`（clang・libcxx・emacs 抜き、試験の hook は OpenSSH と harness の鍵・net.conf だけ）。
- disk（縮めた組、BUG-356 のため）: 空の NVMe 12 GiB と「他の OS」の SATA 14 GiB（ESP 300 MiB に `\EFI\BOOT\BOOTX64.EFI`、Basic data 4 GiB、空き）。
- 場合: 拒む disk（live の USB は S2、空の disk の「選ぶ」は S3）、NVMe 全体の unpack 中のキャンセル（S10、`failed step=unpack`）、unit を壊した NVMe の全体（S10、SHA-256 の不一致）、NVMe 全体（自動 login 切）、「他の OS」の disk に S4 で 1 GiB の swap を作り Basic data を root に（消去の確認、loader は `\EFI\ZEDBSD`）。後で host で他の OS の loader と `\EFI\ZEDBSD\BOOTX64.EFI`・`zedbsd.cfg`、NVMe の導入先だけの起動（boot-test.sh）と導入した利用者の password での SSH。
- 「他の OS」の disk の導入先の起動は未実施: loader が `\EFI\ZEDBSD` にあり、firmware は Boot#### でしか起動しない（p007、ユーザーの承認待ち）。

## 結果

- 2026-10-29 の 1 回目（`build/ws307-test/p011.vq3t9c4w`、7 台の disk: NVMe 512・4096 の NVMe・「他の OS」の SATA（3 partition、1 つを乱数で）・zedbsd.cfg の FAT の SATA・MBR の SATA・空の GPT の USB・1 GiB の USB）: live の system が SSH に出ない（400 s。VGA は起動の splash のまま、gdbstub で全 CPU が sched_idle）。1 回ずつの切り分け: NVMe 512 だけ → 起動（88 s・56 s）、4096 の NVMe だけ・NVMe 512＋4096・空の GPT の USB だけ・3 partition の SATA だけ → 起動しない。Q1 が BUG-356 に登録（再現の script は `build/tmp/bug356-repro.py`、worktree の中、git に入れない）。
- 縮めた組（NVMe 512＋「他の OS」の SATA、p010 と同じ組）の 2 回（`p011.0yxo6eq4`、`p011.8egqibob`）も live の system が SSH に出なかった。p010-screens.py の対照の 2 回（`p010s.61y2ae0f`、`p010s.ortbr5wz`）は SSH は出たが、installer が最初の `kl_window_present_resize` の EIO で終わった（`INSTALLER FAILED operation=present error=5`）。同じ組で 13:00 前の p010 の 3 回目は PASS しているので、起動しない・present の EIO は disk の組だけで決まらず、host の Venus（virgl_render_server・llvmpipe）の状態か時刻で揺れると見られる（未確認）。installer は present とその作り直しの失敗を何回か待って再試行するよう直した（be0f02fa1・daf0c60fe）。
- その後の 1 回（`p011.tcubp4gi`、daf0c60fe の image、縮めた組）も live の system が SSH に出なかった。p011 の受け入れ（GUI の通しの導入と導入先の起動）は未達。BUG-356 の解析の後に `python3 plan/ws307/tests/p011-install.py IMAGE` を流し直す（縮めた組、BUG-356 が直れば外した disk を戻す）。

## 未実施・残り

- 4096 の disk・swap file（空の GPT の USB）・MBR と zedbsd.cfg の拒む disk の GUI の試験（BUG-356: これらの disk を付けると live の system が起動しない）。
- GTK4 の app の build の確かめ（p004 の後、Q1 の判断）。
- Boot#### の確かめ（p007 の承認待ち）。
- 同じ image を dd した disk との同居（spec §11.2 の既知の制限: 同じ image の 2 台は PARTUUID が重なり曖昧）。
