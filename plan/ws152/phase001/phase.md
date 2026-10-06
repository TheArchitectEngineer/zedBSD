<!-- awesome-plan project=zedbsd record=ws152-p001 -->

# ws152-p001: system の更新の方式の検討

Status: planning（2026-10-06 P2。検討の第 2 版（design-reviewer の指摘を入れた版）。§7 の判断 U1〜U12 待ち。code は書かない）
Disposition: normal
Parent: [WS152](../ws.md)
Queue: Q1 の P2 の列（2026-10-06。Q1 によれば「WS152 は第 1 段に含む」）

## 目的

ベータ3 で実装する system の更新の方式を決める。code は書かない。結論と判断の項目をユーザーに出す。

目標の段の記録は食い違っている。phase.md と master.md の 454 行は「ベータ3」、ws.md は「ベータ4 以降」、master.md の 255 行は「ベータ2 に移動」、Q1 の割り当ては「第 1 段」。Q1 に揃えてもらう。

## 検討の観点（元の版のまま）

1. **更新の単位**: image の全体（A/B の 2 つの partition か、root の file system の差し替え）か、package（base・Keiland・外部 package ごと）か、その組み合わせか。kernel・boot loader・firmware の更新の扱い。
2. **配布**: 更新の置き場所（GitHub の release（WS129 の CI の Prerelease・Latest）か専用の server か）、版の付け方（`1.0.0-betaN`）、差分の配布の要否、帯域。
3. **検証**: 署名（鍵の管理・失効）、hash、改ざんの検出。HTTPS の上でも署名を確かめる。
4. **適用**: 動いている system への適用の手順、再起動の要否、利用者の data（home・設定）を保つこと、設定の file の移行（desktop.conf などの版の差）。
5. **失敗の回復**: 適用の途中の電源断、起動しない新しい版からの自動の戻り（A/B と boot の成功の印）、手動の戻し。
6. **UI**: Settings の Updates の頁（更新の確認・download の進み・適用・再起動の予約・自動の確認の on・off）、通知、libkeiland・compositor の拡張・libkeiland-backend の口（他の設定と同じ経路）。権限（管理者だけ）。
7. **Linux・FreeBSD の Keiland**: OS の package manager（apt・pkg）に任せ、Updates の頁は案内だけにするか、を判定する。
8. 既存の仕組みとの関係: インストーラ（WS019・WS119）、release（WS129）、外部 package（WS032）。

## 1. 今の事実（2026-10-06 の tree で確かめた）

| 項目 | 事実 | 出典 |
| --- | --- | --- |
| release の image の形 | GPT。①ESP の FAT（64 MiB。`EFI/BOOT/BOOTX64.EFI` 27 KB・`vmunix` 約 2.4 MB・`zedbsd.cfg`・`logo.ppm` 6.2 MB）、②UFS の root（PARTLABEL `zedBSD-root`、1 GiB、読み書き。system と利用者の data（`/home`・`/etc` の変更・`/var`）が同じ partition に混ざる）、③swap（PARTLABEL `zedBSD-swap`、1 GiB）。全体は約 2.1 GiB（2,216,689,664 bytes）。GPT の GUID は build の時に 1 回だけ決まるので、同じ release から作った USB は皆同じ GUID を持つ | `platform/amd64/vmunix.mk` の `$(BUILD)/hdd-image.img`（native の layout）、`tools/build/zedimage-host.c`（ESP 64 MiB、GUID）、`build/uat-0505/hdd-image.img` の partition 表 |
| 起動の設定 | `zedbsd.cfg` の元は `kernel=vmunix`、`rootpart=PARTLABEL=zedBSD-root`、`swap0=PARTLABEL=zedBSD-swap`。release ではこれに `logo=`・`login=graphical`・`kmsg=quiet` が加わる。loader は同じ disk の FAT から `zedbsd.cfg` を探し、無ければ止まる（隠れた代わりは無い）。`kernel=` は 1 つだけ。**前の版へ戻る仕組み（試しの起動・失敗の時の戻り・選択の menu）は無い**。loader は file を読むだけで書かない（`EFI_FILE_MODE_READ`） | `docs/howto/boot-and-storage.md`、`bootloader/uefi/bootx64.c` |
| boot の FAT を動いている間に触る | kernel が private に mount している。fstab の `/boot/esp` の行で同じ状態を見せる（2 重の mount は BUG-065 で拒まれる） | `docs/reference/kernel-boot-parameters.md`、`src/kern/mount.c` の `mount_private_allow_adoption` |
| system の大きさ | root の tree は約 310〜320 MB（2026-10-05 の UAT・AAT の build、clang 入り）。release の構成は emacs・libavcodec・videoplayer も足すので、もっと大きい（**未測定**） | `du -sm build/uat-0505*/rootfs`、`config/release/config-amd64-beta2.mk` |
| USB | 利用者の guide は「4 GB 以上」。image は 2.1 GiB なので、4 GB の USB でも残りは使われない | `docs/release/zedbsd-1.0.0-beta2-guide.md` |
| partition・file system を広げる道具 | 無い（`diskpart` に resize は無く、UFS の growfs も、GPT の予備の header を動かす物も無い） | `userland/base/diskpart`、`userland/base/mkfs` |
| 別の layout | インストーラの「共存」と古い image は、FAT の上の `rootfs.img`（読み取り専用の下の層）＋`data.img`（書く上の層）の overlay の root。kernel に overlay の root がある | `docs/howto/boot-and-storage.md` の File-backed overlay root |
| release の配布 | GitHub の release（`awemorris/zedBSD`）。asset は `zedbsd-VERSION-amd64.img.gz`・Windows の zip・`SHA256SUMS`・`LICENSES.md`。**署名は無い**。rc も最終版も `--prerelease --latest=false` で出し、**最終版はその後ユーザーが手で latest にする**（だから最終版は `releases/latest` で見つかる。rc は見つからない）。最終の tag の job は rc の asset を写すだけで、何も build しない | `.github/workflows/release.yml` 1〜11 行・161〜211 行 |
| 版 | `VERSION`（今 `1.0.0-beta2`）。image の `/etc/os-release` に `VERSION_ID`・`BUILD_ID`・`ZEDBSD_RELEASE`。nightly も同じ `VERSION_ID` を持ち、`ZEDBSD_RELEASE` に `+g<rev>` が付くだけ | `Makefile` の `ZEDBSD_OS_RELEASE` |
| HTTPS と暗号 | release の image（`config/release/config-amd64-beta2.mk` が `config/ci/config-amd64.mk` を含む）に curl・openssl・ca-certificates がある。base の `fetch` は HTTP だけ。base の `libpasskey` は既に libcrypto（OpenSSL）を使う | 同左、`userland/base/libpasskey/crypto-openssl.c` |
| package の管理 | 機械の上に package の database も道具も無い。外部 package（WS032）は build の時に image に入る | `userland/packages/`、`userland/base/package.mk` |
| 権限の型 | Settings の利用者の管理は、compositor の backend が起動する set-user-ID root の `/usr/libexec/account-admin`（呼び手が wheel、本人の password、標準入力の要求、結果は 1 行） | `userland/base/account-admin/main.c` |
| 起動の順 | init は `mount -a` の**前**に `/etc/rc.conf` を読み（hostname と service の policy）、`/var`・`/var/log` を作る。`rc.conf` の parser は厳密で、知らない key があると全体を拒み、その時は service を起こさない | `docs/reference/init-services.md`、`userland/base/init/main.c` |
| `/etc` の書き手 | account の file（`passwd`・`group`・`shadow`）と `rc.conf` は、`/etc` の中に一時 file を作り、置き換えの rename で公開する。`/etc/passwd` には system の account（`sshd`・`_greeter`）と人の account が混ざる | `userland/base/common/account.c`、`docs/reference/init-services.md` |
| 原子的な置き換え | `renameat2(RENAME_NOREPLACE)` による「置き換えない公開」、`sync FILE`（FAT の directory の fsync も）。既存の file を置き換える rename が FAT で原子的だという保証は書かれていない | `docs/reference/atomic-publication.md` |
| Settings | Updates の頁は `se_soon_draw`（中身なし） | `userland/desktop/settings/pages.c` 48 行 |
| Linux の Keiland | `.deb`（`tools/release/keiland-linux-deb`） | `tools/release/` |

**分かること**:

- 今の native の layout では、system と利用者の data が 1 つの partition に混ざっている。loader に戻りの仕組みが無い。1 GiB の root に「今の system＋新しい版＋戻すための前の版」を同時に置く余地は無い。
- `/etc` には、mount の前に読まれる物（`rc.conf`）と、rename で置き換えられる物（account の file）がある。system と data に分けるのは、設計の判断である（§2 の B-3）。

## 2. 案

### 案 A: 同じ root の中で file ごとに置き換える（freebsd-update の型）

- 単位: system の file の一覧（manifest。path・hash・種別（system／設定））で、変わった file だけを入れ替える。
- 適用: download と検証は動いている間に済ませ、`/var/db/update/` に置く。次の起動の早い oneshot が、他の service の前に置き換える。置き換えは journal（どこまで済んだか）に沿って行い、電源が切れても次の起動で続きから行う。
- 設定の file: 前の版の hash と同じ（利用者が変えていない）なら置き換え、変えていれば残して `.new` を置く。account の file は、system の account を足すだけの併合。
- 利点: 今の layout のまま。beta2 の利用者もそのまま更新できる。download は変わった file の分だけ。2026-09-26 のユーザーの決定（native の layout）を変えない。
- 欠点:
  - 1 GiB の root に、新しい版と戻すための前の版を置く余地がほぼ無い（§1）。
  - 起動しない新しい版から自動で戻れない。
  - 適用の間は system が半分新しい。

### 案 B: system の A/B の 2 つの partition と、別の data の partition（推奨）

**B-1. layout**

- ESP | `sys-a`（UFS）| `sys-b`（UFS）| swap | `data`（UFS）。
- system の partition は system の物だけを持ち、利用者の data は data の partition に置く。
- 2026-09-26 のユーザーの決定（native の layout、root と swap 各 1 GiB）を変える。
- system の partition の大きさは後から変えられない（変えるには再 install）。大きさは判断の項目にする（U8）。

**B-2. 起動の選び方**

loader が FAT に書かない形にする。FAT の directory と FAT の表を変えない。

- **ESP に固定の file を置く。** install の時に一度だけ作り、以後は大きさを変えずに中身だけをその場で上書きする。
  - `vmunix-a`・`vmunix-b`: 例えば各 16 MiB を確保する。
  - `zedbsd.cfg`: loader が volume を見つけるための物。中身は共通の行だけ。
  - `slot-a.cfg`・`slot-b.cfg`: 各側の `kernel=`・`rootpart=`。
  - `bootstate`: 1 sector の状態。2 つの写しを持ち、各写しに通し番号と checksum を付ける。
  - その場の上書きは FAT の表を変えない（cluster の鎖は同じ）。ただし firmware や zedBSD の FAT が directory の entry の時刻を書き換えるかは未確認で、p003 の前の確認の Phase で確かめる。
- **`bootstate` の中身**: 今の側（active）、試す側（try、無しも可）、試しの通し番号。書くのは userland の更新の helper と起動の成功の oneshot だけ。
- **loader の動き**:
  1. try があり、UEFI の NV 変数 `ZedbsdTried` が試しの通し番号と違う時は、NV 変数にその番号を書いてから try の側で起こす。
  2. それ以外は active の側で起こす。
  3. NV 変数に書けない時は試さず、active で起こす。
  - こうして試しは 1 回だけになり、loader は FAT に何も書かない。
  - kernel には UEFI の runtime services が無いので、NV 変数を扱うのは loader だけになる。
- **起動の成功**（oneshot）:
  - 今の root の identity（`kern.boot.config_partition` と root の partition）が `bootstate` の try と一致する時だけ、`bootstate` を active=try・try=無し に書き換える。
  - 一致しない時（try の側が起きずに active で起きた時）は失敗と記録する。`bootstate` の try を消し、その版を data の側の「失敗した版」の一覧に入れ、利用者に知らせる（§5）。
  - 「成功」の条件（greeter の画面が出た時、など）は判断の項目（U10）。
- **自動の戻り**: 新しい版が起きない時は、利用者が電源を入れ直すと前の側で起きる。panic の後に自動で再起動するかは未確認。watchdog は無い。
- **書く先**: 今動いている root と同じ disk の上で、partition の番号と type で決める。PARTLABEL では決めない。同じ release の USB が 2 本挿さっていると、label も GUID も同じになるため。
  - `slot-*.cfg` は PARTUUID で書く。最初の起動でその機械だけの PARTUUID に振り直す案は、p002 で検討する。
- **ESP は A/B ではない。** `vmunix-b` と `bootstate` の上書きの間の電源断で、上書きしていた file は壊れる。ただし `vmunix-a`・`slot-a.cfg`・`BOOTX64.EFI` の sector は書かれない。`bootstate` は 2 つの写しのどちらかが残る。USB の flash のページの破れが隣の sector を巻き込むかは媒体の性質であり、保証できない。
- **loader（`BOOTX64.EFI`）の更新**: 判断の項目にする（U6）。置き換えない場合、古い loader と新しい kernel の間の受け渡し（`KERN_BOOT_PROVENANCE_VERSION` など）を互換に保ち続けることが制約になる。また loader の不具合は再 install でしか直らない。

**B-3. `/etc` の分け方**（判断の項目 U9）

- **事実**:
  - init は mount の前に `rc.conf` を読む。
  - account の file は rename で置き換えられる。
  - `passwd` と `rc.conf` には、system の物と機械の物が混ざる。
- **案**:
  - (i) data の partition を kernel が root と一緒に早く mount する（boot の引数 `datapart=`）。機械ごとの file は data の側に置き、`/etc` からは symlink で指す。書き手（`account.c`・`rc.conf` の書き手など）は、symlink の先の実体の directory に一時 file を作って rename するように直す。
  - (ii) `/etc` を、system が持つ file と data が持つ include の file に分ける。init・account の読み手が 2 つの file を合わせて読む。
  - (iii) `/etc` だけの overlay（lower は system の `/etc`、upper は data）。kernel に overlay は既にある。ただし案 C と同じ「影」が出る。
- **推奨**: (i)。読み手は変えずに済み、書き手の数は限られる（p002 で全数を調べる）。
- **合わせて決めること**:
  - `passwd`・`group`・`shadow` は data の側に置く。新しい版の最初の起動で、system の account の足りない物だけを足す（消さない）。
  - `rc.conf` は data の側に置く。新しい版で足された service は、既定の policy の file（system の側）から足りない物だけを足す。
  - `/tmp`・`/var/run -> ../run`・`/var/empty`・`/usr/local`（利用者が入れた物は更新で消える。data の側へ移すか、`/usr/local` を使わせないか）。

**B-4. data の移行と戻し**

- 確定（起動の成功）の前に data へ行う変更は、前の版も読める形に限る（行を足すだけなど）。
- 前の版が読めなくなる変換（`rc.conf` の `version:` の変更など）は、確定の後に行う。その前に、変える file の写しを data の側に残す。手動で前の版に戻す時は、その写しを書き戻す。
- libkeiland の設定の store は、知らない key を無視し、無い key は既定の値にする（今の作り）。

**B-5. 単位と配布の物**

- system の partition の image の全体と、kernel。
- 今の CI の `AMD64_NATIVE_ROOT_IMAGE` は、`/etc/passwd`・`rc.conf`・`/home`・`/var` を含む root の tree の全体から作られ、asset としても公開されていない。system だけの image と最初の data の image を作る target を、p002 で新しく作る。

**利点**:

- 書いている間も、今の system の partition は無傷（ESP は B-2 の範囲）。
- 前の版が丸ごと残るので、戻しは partition の単位で確実。ただし data の移行は B-4 の規則を守った場合に限る。
- 起動しない版からは、電源の入れ直しで戻る。

**欠点**:

- layout が変わる。beta2 の USB は repartition が要る（U5）。
- 1 回の download は system の全体（gzip で数百 MB の見込み、未測定）。
- 4 GB の USB では、ESP 64 MiB＋sys 1 GiB×2＋swap 1 GiB を引くと data は約 0.66 GiB しか残らない。data を USB の残りまで広げる道具は無い（U8）。
- `/etc` の分離（B-3）と loader の変更（B-2）が要る。

### 案 C: 読み取り専用の system の image の file と overlay（既存の overlay の root を使う）

- 単位: `rootfs-<版>.img`（読み取り専用の下の層）と `vmunix-<版>`。これらを 1 つの大きな partition に置く。
- 適用: 新しい image の file を書き、確かめ、起動の選び方を B-2 と同じ仕組みで切り替える。
- 利用者の data は overlay の上の層（`data.img`）。
- 利点:
  - kernel に overlay の root が既にある。
  - 固定の分割が無く、版の image の file と data が 1 つの partition の空きを分け合える。
  - WS019 の共存の install（FAT の上）も同じ形で更新できる。
- 欠点:
  - 上の層に写された system の file（利用者が触った `/etc` の file）が、新しい下の層の file を隠す（B-3 と同じ性質の問題で、overlay では全部の path に起こりうる）。上の層は動いている間 `/` として使われているので、整理しにくい。
  - release は 2026-09-26 に overlay から native の layout に移った。案 C は overlay に戻すことになる。
  - 速さは未測定。

（他に、1 つの partition に版ごとの tree を hard link で置く ostree の型もある。content の store と checkout の道具を新しく作る量が大きいので、候補から外した。）

### 比べると

| 観点 | 案 A | 案 B | 案 C |
| --- | --- | --- | --- |
| 書き込みの途中の電源断 | journal で続きから | system は無傷。ESP の上書きの中の file は壊れうる | image の file は無傷。起動の選び方は B と同じ |
| 起動しない新版から戻る | 難しい | 電源の入れ直しで戻る（試しは 1 回） | 同左 |
| 空き | 1 GiB に入らない | 分割が固定（4 GB の USB だと data は 0.66 GiB） | 空きを分け合える |
| 利用者の data の分離 | 無し | 有り（`/etc` の分け方が要る） | 有り（影が出る） |
| beta2 からの続き | そのまま | repartition | layout の変更 |
| 2026-09-26 の layout の決定 | 保つ | 変える | 変える（overlay に戻す） |
| download の量 | 変わった file だけ | system の全体 | system の全体 |
| 新しく作る物 | manifest・journal・設定の併合 | layout・system だけの image・`/etc` の分離・loader の try | 起動の選び方・影の整理 |

**推奨: 案 B。**

- 更新が壊れても機械が起きることと、利用者の data を system から分けることは、desktop の OS の更新で一番大事な性質である。案 B は、system の partition の単位でそれを満たす。
- 案 C は影の問題が `/etc` に限らず全部の path に及ぶ。
- 案 A は 1 GiB の root に戻すための余地が無い。
- 案 B の難所は B-2（loader）と B-3（`/etc`）で、どちらも実装の前の Phase で確かめる（§8）。

## 3. 配布と版（案 B の場合）

- **置き場所**: GitHub の release。専用の server は持たない。workflow の変更は WS152 の Phase の中で行う。WS129 はベータ1 の release の作業の WS なので、そこへは足さない。
- **asset を足す**:
  - `zedbsd-VERSION-amd64-system.ufs.gz`（system の partition の image）。
  - `zedbsd-VERSION-amd64-vmunix`。
  - `zedbsd-VERSION-update.json`（manifest）。版、build の通し番号、更新元として受ける最も古い版、各 asset の大きさと SHA-256（gzip を解いた後の image の hash も）、release notes の URL を持つ。
  - `zedbsd-VERSION-update.json.sig`（署名）。
  - manifest の形は p004 の最初に決める。
- **署名の順序**:
  1. rc の job が asset を作る。
  2. ユーザーが手元で manifest に署名し、`.sig` を rc の release に足す。
  3. 最終の tag の job が、rc の asset と `.sig` を写す前に、tree の公開鍵で署名を確かめる。
- **新しい版の見つけ方**:
  - 最終版は `releases/latest` で見つかる（ユーザーが手で latest にする）。ただし latest にする前の時間は見つからない。
  - もう一つの道は、固定の tag の release に置いた、署名つきの `channel.json`。置き換えの間に取れない時間ができ、CDN の cache もある。
  - どちらでも、最後の判断は署名つきの manifest で行う。どちらを使うかは p004 で決める技術の選択。
- **版の比べ方**:
  - manifest の build の通し番号（単調に増える）で比べる。semver の文字の比べ方では `beta10` が `beta9` より前になるので使わない。
  - nightly・開発の build は更新を確かめない（U11）。
- **差分の配布**: 第 1 段は無し（全体を配る）。後の段で、前の版との block の差分を足す（Future Work）。
- **download**: 利用者の権限で、data の側の置き場に gzip のまま落とす。HTTP の Range で途中から再開する。置き場の大きさ（gzip の image、未測定）は data に入る必要がある（U8 と関係）。

## 4. 検証

- **署名**: Ed25519。機械の側は libcrypto の `EVP_DigestVerify` で manifest の署名を確かめる。image と kernel は manifest の SHA-256 で確かめる。書いた後の partition は、cache を通さずに読み直して比べる（cache を通さない読み方は p005 で確かめる）。HTTPS の上でも署名を必ず確かめる。
- **鍵は 2 段**:
  - offline の root の鍵（控えを 2 つ以上、ユーザーが持つ）が、期限つきの署名の鍵に署名する。
  - manifest は署名の鍵で署名する。
  - 機械は system の側の `/etc/keiland/update-keys/` に root の公開鍵を持つ。
  - 署名の鍵の失効は、root の鍵で署名した失効の一覧で配る。失効の一覧は channel と一緒に取る。
- **巻き戻しと凍結**:
  - これまでに見た最も新しい build の通し番号を data の側に記録する。それより古い manifest は受けない（戻した後でも）。
  - channel に署名つきの時刻と期限を持たせる。期限切れは警告だけにする（時計の狂った機械があるので、拒みはしない）。
- **鍵の管理**（U2）: 推奨は offline の 2 段。代わりの案は CI の secret（手間は無いが、repository の乗っ取りがそのまま更新の乗っ取りになる）。

## 5. 適用・回復・UI

- **権限**:
  - 確認と download は利用者の権限で行う。
  - 書き込みと `bootstate` の変更は管理者（wheel）だけで、本人の password を求める（`account-admin` と同じ型）。
- **実行する物**: set-user-ID root の `/usr/libexec/system-update`。download した file の検証、使っていない側への書き込み、読み直し、`bootstate` の更新だけを行う。curl・TLS は root で動かさない。
  - 書く partition は lock し、同時に 2 つ動かない。
  - logout や compositor の終わりで止まったら、次に最初から確かめ直す。download は Range で続きから。
- **経路**: 他の設定と同じ（libkeiland の system の拡張 → compositor → libkeiland-backend-zedbsd → helper）。
- **Updates の頁**:
  - 表示: 今の版、最後に確かめた時刻、「更新を確かめる」。
  - 新しい版がある時: 版、大きさ、release notes（browser で開く）、「download して入れる」。
  - 進み: download の %、検証、書き込みの %。
  - 済んだら「再起動して仕上げる」（「後で」も選べる）。
  - 設定: 自動の確認の on・off。
  - 前の側に版が残っている時: 「前の版に戻す」（B-4 の写しの書き戻しを含む）。
  - 管理者でない人には、表示だけで操作できないことを出す。
- **通知**（WS156。任意の機能として後から足せる形にする）: 新しい版がある時、更新の後の最初の login で「版 X になった」、戻った時に「新しい版が起動しなかったので版 Y に戻った」（B-2 の失敗の記録から）。失敗した版は、それより新しい版が出るまで勧めない。
- **再起動**: 要る。今すぐ・後で。電源の頁の再起動と同じ経路を使う。

## 6. Linux・FreeBSD と、他の WS との関係

- **Linux・FreeBSD の Keiland**: 案内だけにする（推奨）。Keiland の版を出し、「この system の package manager（apt・pkg）で更新される」と表示する。
- **WS119（インストーラ、ベータ4 以降）**: dedicated の install を案 B の layout で書くよう変える必要がある。WS119 への依存として §8 に置く。WS019 の共存（FAT の上の overlay）は、案 B では更新の対象の外。
- **WS129**: 変えない（§3）。
- **WS032（外部 package）**: system の image に入るので、system と一緒に更新される。
- **WS148（Privacy）**: 頁が残るなら、自動の確認が GitHub へ要求を出すことを示す（WS148 は「要らなければ頁を削除」の段階）。
- **WS156（通知）**: §5。

## 7. ユーザーの判断が要る項目

| ID | 問い | 案 | 推奨 |
| --- | --- | --- | --- |
| U1 | 方式 | A（同じ root で file ごと）・B（A/B の system ＋ data）・C（image の file と overlay） | **B**（2026-09-26 の native の layout の決定を変える） |
| U2 | 署名の鍵 | offline の 2 段（root の鍵の控えを 2 つ以上）・CI の secret | **offline の 2 段** |
| U3 | 通り道 | beta の最終版も配る（rc は配らない）・正式の最終版だけ | **beta の最終版も配る** |
| U4 | 自動の確認 | 既定で on（1 日 1 回、download は利用者の操作で）・既定で off | **on**（download はしない） |
| U5 | beta2 の利用者 | beta3 は image を書き直す（data は利用者が外へ写す）・移行の道具を作る | **書き直す** |
| U6 | loader（`BOOTX64.EFI`）の更新 | 更新で置き換えない（再 install の時だけ。受け渡しの互換を保ち続ける）・更新で置き換える | **置き換えない** |
| U7 | Linux・FreeBSD | 案内だけ・apt・pkg を Settings から起動 | **案内だけ** |
| U8 | 大きさ | (a) USB 4 GB のまま（sys 1 GiB×2、data 約 0.66 GiB）・(b) USB 8 GB 以上（data 約 4 GiB）・(c) 最初の起動で data を USB の残りまで広げる（GPT・UFS を広げる道具を新しく作る） | **(b)**。(c) は後の段 |
| U9 | `/etc` の分け方 | (i) data を早く mount し symlink、書き手を直す・(ii) system と data の file に分けて読み手が合わせる・(iii) `/etc` の overlay | **(i)** |
| U10 | 起動の成功の条件 | greeter の画面が出た時・利用者の login・init の `system running` | **greeter の画面**（login の前に電源を切っても戻らない。desktop が壊れた版は確定しない） |
| U11 | nightly・開発の build | 更新を確かめない・nightly の通り道を作る | **確かめない** |
| U12 | system の partition の大きさ | 1 GiB・2 GiB（後から変えられない） | **2 GiB**（release の構成は 1 GiB に近づいている見込み。未測定） |

## 8. 実装の Phase の分け方（案 B の場合の案）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p002 | 確かめ（実機の firmware を含む）: UEFI の NV 変数を loader が書けるか、ESP の file のその場の上書きで FAT の directory・表が変わらないか（zedBSD の FAT と firmware）、cache を通さない読み直し、release の構成の system の大きさと gzip の大きさ。code は確かめの小さな物だけ | U1 |
| p003 | layout と `/etc`: system だけの image と最初の data の image の target、data の早い mount、`/etc` の機械ごとの file の全数の調査と分離（U9）、書き手の修正、system の account と service の併合、`/tmp`・`/var`・`/usr/local` の扱い | p002、U5・U8・U9・U12 |
| p004 | loader と起動の成功: ESP の固定の file、`bootstate`、NV 変数による 1 回の試し、起動の成功と失敗の oneshot、手動の戻し、data の移行の規則（B-4）の仕組み | p002・p003、U6・U10 |
| p005 | release: manifest の形、CI の asset、署名の道具（2 段の鍵、ユーザーの手元）、最終の tag の job の署名の確かめ、channel、失効の一覧。試験用の鍵は試験の config にだけ入れる | p003、U2・U3・U11 |
| p006 | 更新の helper: 確認と download（利用者の権限、Range）、root の helper（検証、書き込み、lock、読み直し、`bootstate`）。host の試験 | p004・p005 |
| p007 | Settings の Updates の頁、libkeiland・compositor の拡張の口、Linux・FreeBSD の案内。WS156 の通知は任意（WS156 p002 の口が main にあれば使う） | p006 |
| p008 | QEMU（T1）と実機: 版 N から N+1、決まった点での電源断（gdbstub で止めて切る道具を T1 と作る）、起動しない版からの戻り、data の移行の後の戻し、2 本の USB。全文の規約 | p002〜p007 |

外への依存:

- WS119: dedicated の install を案 B の layout にする。
- greeter・sessiond: U10 の「画面が出た」の印。

## 9. design-reviewer の review（2026-10-06）と扱い

第 1 版への指摘（高 7、中 10、低 10）を第 2 版に入れた。

| 指摘 | 扱い |
| --- | --- |
| H1 確定の rename が FAT で原子的でない・H2 ESP は A/B でない・M1 loader が FAT に書く可否 | B-2 を、loader が FAT に書かない形（固定の file のその場の上書き、`bootstate` の 2 つの写し、NV 変数）に変えた。表の「無傷」を直した。firmware の確かめを p002 に置いた |
| H3 `/etc` の分離は技術の選択ではない | B-3 と U9 にした。init の mount の前の読みと、書き手の rename を事実に書いた |
| H4 data の移行と戻しの両立 | B-4 の規則（確定の前は前の版も読める形だけ、変換は確定の後で写しを残す） |
| H5 起動の成功の印の穴 | B-2 の成功と失敗の oneshot（root の identity の一致）、失敗した版の記録、U10 |
| H6 鍵の失効 | §4 の 2 段の鍵と失効の一覧、U2 |
| H7 PARTLABEL で別の disk を壊す | B-2 の「書く先」（同じ disk の上の partition の番号と type、cfg は PARTUUID） |
| M2 巻き戻しと凍結 | §4（最も新しく見た番号の記録、channel の時刻と期限） |
| M3 版の比べ方 | §3（build の通し番号）、U11 |
| M4 比べ方の偏り | 表に 2026-09-26 の決定の行、案 C の利点、「改ざんに強い」の削除、「電源の入れ直し」、ostree の型を足した |
| M5 大きさ | §1（release の構成は未測定）、0.66 GiB に直した、U8 の (c)、U12 |
| M6 download と書き込み | §3・§5（利用者の権限で gzip のまま落とす、root は検証と書き込みだけ、lock） |
| M7 latest | §1・§3 を直した。署名の順序を書いた |
| M8 CI の成果物 | B-5（system だけの image の target を作る） |
| M9 Phase の依存 | §8（確かめの Phase、manifest の形、移行の仕組み、WS119、WS129 を変えない、WS156 は任意）。目標の段の食い違いは「目的」に書き、Q1 に揃えてもらう |
| M10 試験 | p002 の実機の確かめ、p008 の決まった点の電源断と実機、試験の鍵は試験の config だけ |
| L1〜L10 | L1（cfg の行を保つ）は B-2 で slot の cfg と共通の cfg に分けたので、release の行（`logo=` など）は共通の `zedbsd.cfg` に残り、重ならない。L2 は §1。L3 は B-2 の固定の名前。L4 は B-2 と U6。L5 は B-3。L6 は §1。L7 は §4。L8（swap file）は p003 で検討。L9 は §6。L10 は §3 |

## 成果物

- 方式の案（2〜3 の案と利点・欠点、推奨）、判断の項目、実装の Phase の分け方の案。design-reviewer の review。

## 受け入れ

- 上の観点の全部に案と根拠があり、ユーザーが方式を選べる形になっている。

## 記録

- 2026-10-06 P2: 検討の第 1 版（§1〜§8）。§1 の事実は tree と `build/uat-0505` の image で確かめた。
- 2026-10-06 P2: design-reviewer の review（§9）を受けて第 2 版。release の構成の system の大きさと gzip の大きさは未測定（p002）。
