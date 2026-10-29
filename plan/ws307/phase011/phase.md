<!-- awesome-plan project=zedbsd record=ws307-p011 -->

# ws307-p011 通しの試験

- 親: [WS307](../ws.md)。仕様: [spec.md](../spec.md) §12。Queue: q1665 / q1665-i08（担当 P4、2026-10-29、worktree p4-ws307l）。
- Status: in-progress
- 承認: q1665-i01 と同じ（Q1 の中継）。2026-10-29 Q1 の判断: p004 抜きで進める（GTK4 の app の build の確かめだけを p004 の後に残す）。
- 前提: p010（installer の app）、p009（install-admin）、p006・p008 の 4096。

## 方法（技術の判断）

- 1 本の script `plan/ws307/tests/p011-install.py IMAGE`（配置 → 起動 → GUI の導入 → 回収 → 導入先だけの起動 → 要約）。共有の部品は `plan/ws307/tests/installer_walk.py`（p010-screens.py と共有）。
- GUI の操作は AAT の `aat-input`・`keiland-shot` でなく QMP の keyboard と USB の tablet、撮影は QEMU の VNC（Venus の head）。押す場所は installer の log の `INSTALLER CONTROL`（AAT と同じく log の位置から、固定の pixel を使わない）。`keiland-shot` は compositor を `ZEDBSD_TEST_SCREEN_CAPTURE` で build し直す要があり desktop の unit（と index の hash）が変わるので、試験の image の index を製品と同じに保つためにこの形にした（J4 の「unit・index は同じ」）。
- 試験の image は p010 と同じ `plan/ws307/tests/config-amd64-p010.mk`（clang・libcxx・emacs 抜き、試験の hook は OpenSSH と harness の鍵・net.conf だけ）。
- disk: 空の NVMe 12 GiB、4096 の NVMe 8 GiB、「他の OS」の SATA 14 GiB（ESP 300 MiB に `\EFI\BOOT\BOOTX64.EFI` と別の file、Basic data A 2 GiB（乱数で埋め、hash を前後で比べる）、Basic data B 4 GiB）、`zedbsd.cfg` を持つ FAT の SATA 3 GiB、MBR の SATA 2 GiB、空の GPT の USB 9 GiB、1 GiB の USB。
- 場合: 拒む disk（1 GiB は S2、MBR と zedbsd.cfg の disk は S3 の「選ぶ」）、NVMe 全体の unpack 中のキャンセル（S10、`failed step=unpack`）、unit を壊した 4096 の全体（S10、SHA-256 の不一致）、NVMe 全体（自動 login 切）、4096 全体、「他の OS」の disk に S4 で 1 GiB の swap を作り Basic data B を root に（消去の確認、loader は `\EFI\ZEDBSD`）、空の GPT の USB に S4 で ESP と partition を作り swap 無し（swap file）。後で host で Basic data A の hash・他の OS の loader と file・`\EFI\ZEDBSD\BOOTX64.EFI`・`zedbsd.cfg`、導入先だけの起動（NVMe・4096・USB を boot-test.sh）、NVMe の導入先に導入した利用者と password で SSH。
- 「他の OS」の disk の導入先の起動は未実施: loader が `\EFI\ZEDBSD` にあり、firmware は Boot#### でしか起動しない（p007、ユーザーの承認待ち）。

## 結果

（実施中）

## 未実施・残り

- GTK4 の app の build の確かめ（p004 の後、Q1 の判断）。
- Boot#### の確かめ（p007 の承認待ち）。
- 同じ image を dd した disk との同居（spec §11.2 の既知の制限: 同じ image の 2 台は PARTUUID が重なり曖昧）。
