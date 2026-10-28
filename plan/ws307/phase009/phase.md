<!-- awesome-plan project=zedbsd record=ws307-p009 -->

# ws307-p009 helper install-admin

- 親: [WS307](../ws.md)。仕様: [spec.md](../spec.md) §5・§6.1・§6.4・§7・§8・§9（J2・J5・U26）。Queue: q1665 / q1665-i06（担当 P4、2026-10-29、worktree p4-ws307j。i05 は p4-ws307i）。
- Status: in-progress（段 1 の設計、段 2 の index と unpack を host で確かめた。残り: 段 2 の plan・target、段 3〜7）
- 承認: 2026-10-29 ユーザー「P4にWS307」（Q1 の中継）。HAL の API・toolchain には触れない。Boot####（段 11 の efiboot）は p007 の承認の後で、この Phase では書かない（S11 の案内だけ）。
- 前提: p003（index と index-sha256.h、live の tree の installer の menu）、p005（diskpart batch・mkswap の partition・mkfs の乱数の serial と 4096）、p008（loader の `kernel=rootpart:`）、p006（4096 の kernel）。

## 設計（段 1、2026-10-29）

### 置き場所と build

- `userland/desktop/installer/admin/`: `main.c`（入口・呼び出し元の確かめ・権限・lock・要求の振り分け）、`index.[ch]`（index の読みと hash、unit の閉包）、`unpack.[ch]`（gzip＋ustar の展開）、`plan.[ch]`（計画の読みと検査、cfg の文）、`target.[ch]`（導入先の設定: 利用者・hostname・sshd・fstab・記録）、`probe.[ch]`（disk の列挙）、`step.[ch]`（子の program と確認の phrase）。
- 登録は `userland/desktop/installer/Makefile`（`userland/*/*/*/Makefile` の深さ）: `install-admin`、menu `installer`、SELECTABLE n、mode 4550、`usr/libexec`、class basic、libz-compat を link。source に `userland/base/common/account.c`・`sha256.c`、`userland/base/diskpart/table.c`、`userland/base/login/verify.c` は要らない（password を聞かない）。
- index の hash: `platform/amd64/installer-image.mk` が `$(BUILD)/installer/index-sha256.h`（`INSTALL_ADMIN_INDEX_SHA256`）を作る。install-admin の object に target 固有の `-I$(BUILD)/installer` と、その header への依存を足す。source は `#include "index-sha256.h"`。host の試験は自分の header を渡す。

### 要求（spec §9.2）

- `install-admin probe`: 読むだけ。lock を取ってから読む（session が生きている時は EBUSY、installer は session に `probe` を頼む）。出力は 1 行 1 record の TAB 区切り: `firmware uefi|other`、`memory BYTES`、`disk NAME REG KIND SECTOR COUNT FLAGS USED TABLE ENTRIES REASON`、`part DISK SLOT START COUNT TYPE PARTUUID LABEL FSTYPE FSLABEL MOUNTED`、`esp …`（空き・`zedbsd.cfg` の有無・loader の名前の有無）、`rootsize BYTES INODES`（`mkfs --check-size` の要求で）、`end`。
- `install-admin session`: stdin の要求を順に。`probe`、`edit delete …`・`edit create …`・`edit create-with-esp …`、`install …`、`cancel`、EOF は中止。stdout は `STEP`・`BYTES`・`UNIT`・`STATE`・`DONE`・`FAIL`。
- field の形: 利用者の入れた文字（表示名・password・hostname・名前）は `LEN:BYTES`（長さを前に）。他は空白を含まない token。
- 4 つ組（spec §7.4）: `REG:START:COUNT:PARTUUID`（disk の登録番号、開始の LBA、sector の数、PARTUUID）。

### 段

1. 設計（この節）。
2. 純粋な部分と host の試験: index（形式 1 の record、SHA-256 の照合、unit の閉包と順、setid の許可、boot の行）、unpack（gzip の header・raw deflate・CRC32 と ISIZE、ustar の header の checksum、path の安全（絶対・`..`・空の成分・長さ）、型（file・directory・symlink だけ）、所有者 0:0、mode と setid の許可の照合、`openat(…, O_NOFOLLOW)` の成分ごとの作成）、plan（要求の token と長さ付きの field、名前・hostname・password の規則、4 つ組、cfg の文）、target の文の編集（passwd・shadow・group の既定の利用者の置き換え、rc.conf の hostname、sshd_config の `PermitRootLogin no`、fstab の swap、autologin）。
3. probe（`BLKGETINFO`・`BLKGETIDENTITY`・`KERN_SYSTEM_GET_MOUNTS`・swap の source・boot の slot・diskpart の `dp_load`、live の disk の判定、ESP の空きと `zedbsd.cfg` の有無は読み取り専用の private の mount）。
4. main: wheel・自動 login の利用者の uid・index の hash の確かめ、`setresuid`・`setresgid`・`setsid`・`clearenv`・fd・umask・rlimit・signal、`/run/install-admin`（0700）と lock、syslog の auth。
5. session の edit（spec §7.1）と install（§7.2 の段 1〜10）: 子の diskpart・mkfs・mkswap（確認の phrase を直前の `BLKGETINFO` から）、mount を外す、0 埋め、nosuid・nodev の mount、展開と進み、設定、unmount・sync、ESP の公開（loader の path の規則、cfg は一時の名前→rename）。中止は段の終わり・file の間。
6. 登録と build（installer の menu、index の hash の依存、`make installer-image` の試験の config で live に入る）。
7. QEMU: 試験の hook の installer の image を USB で起動、空の NVMe・SATA・USB を足し、SSH で `install-admin session` に計画を流す → 導入先だけで boot-test.sh（自動 login を切る）、中を SSH で。4096 の NVMe も。512 の disk の j3 の sync（p008 の残り）もここで。

### 決めた事（技術の判断）

- T29: deflate は libz-compat の raw（windowBits −15）で、gzip の header と trailer（CRC32・ISIZE）は helper が読む（libz-compat は gzip の wrapper を読まない）。libz-compat の inflate は入力の全体を持ってから 1 回で出力の全体を作る（少しずつ渡すと毎回始めから decode する）ので、tarball は全体を memory に読み（同じ fd で読みながら SHA-256、照らしてから展開）、出力も全体を memory に持つ。最大の unit（lang-clang、65 MB → 208 MB）で約 280 MB。installer の image の起動に要る memory の下限は p011 で量る。

## 結果

- 段 2 の一部（2026-10-29、P4）: `userland/desktop/installer/admin/index.[ch]`（形式 1 の読み、2 回の走査で依存と setid の unit を slot に、同じ名前・未知の record・未知の依存・長すぎる field の拒み、SHA-256 の照合、依存の閉包（依存を先に、循環は ELOOP）、setid の許可の照合）と `unpack.[ch]`（同じ fd で読みながら SHA-256・大きさの照合、gzip の header・raw deflate・CRC-32 と ISIZE、ustar の checksum と magic、file・directory・symlink だけ、path の安全、成分ごとの `openat(O_NOFOLLOW)`、新しい file だけ（`O_EXCL`）、mode と setid の許可、entry ごとの callback で中止）。
- host: `sh plan/ws307/tests/host/install-admin-unpack-test.sh`（新規、ASan・UBSan）→ PASS（21 の場合: good の展開と mode・symlink・1777、2 回目の EEXIST、許されない setid、絶対 path、`..`、先の entry の symlink を通る書き（ELOOP で外に書かない）、hard link、pax の header、壊れた header、違う SHA-256・大きさ・CRC-32、依存の順と循環、未知の形式・依存・record、index の digest の一致と不一致）。`INSTALLER_PAYLOAD=<p4-ws307g の build/ws307-main/installer/payload>`（1f487021a の build）で 34 の unit を全て展開し entry の数が index と同じ（lang-clang を含む、52 秒）。
- 残り: 段 2 の plan・target、段 3〜7。
