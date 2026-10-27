<!-- awesome-plan project=zedbsd record=ws307-spec -->

# WS307 インストーラの仕様

- 親: [WS307](ws.md)。作成: [ws307-p001](phase001/phase.md)（P4、2026-10-28）。
- 状態: **案**（U1〜U8 は回答済み、U9〜U15 は回答待ち。回答は §15 に原文で引用し、本文の「→ U<n>」の所を直す）。
- 正はユーザーの原文（[ws.md](ws.md) の「ユーザーの指示と希望の仕様」）。この文書は原文のブラッシュアップで、原文と食い違えば原文が正。
- 調査の出典は file:行（基点 main 5300ea917）。

## 1. 範囲

- 対象: UEFI＋ACPI の amd64 の PC だけ（原文「インストーラはUEFI+ACPIなPC用で、他のプラットフォームは考慮しない」）。BIOS・i386・PC-98・aarch64・riscv64 などは扱わない。
- 形: Keiland のアプリ（Wayland の client、libkeiland の UI）。ウィザード形式で「次へ」で進む。
- 導入先: kernel が block device として登録する、書き込みのできる物理 disk。今の driver で NVMe（`nvme<C>n<N>`、`src/drivers/pci/pci-nvme.c:5630`）・SATA（AHCI、`sdX`、`pci-ahci.c:2281`）・USB Mass Storage（BOT・UAS、`sdX`、`usb-storage.c:1818`・`usb-uas-disk.c:412`）。Intel VMD の配下の NVMe も NVMe として出る（`pci-vmd.c`）。
- 入れる物: Base System（header 込み）・Keiland Desktop・Firmware・Packages（Clang・OpenSSH・OpenSSL＋CA 証明書・GTK4・Fonts・libavcodec・curl と、その依存）。
- 範囲外は §13。

## 2. 用語

| 語 | 意味 |
| --- | --- |
| live の system | installer が動いている system（導入元。USB の image など → U1） |
| 導入先 | 選ばれた disk と、その上の partition |
| unit | `/opt/installer` に置く tarball の単位。Base・Desktop・Firmware の 3 つと、`userland/packages/` の package ごと |
| helper | root の権限で disk の操作と展開を行う program（`install-admin`、§9） |
| 計画（plan） | 画面で決めた内容（disk、partition の表の変更、root・swap の割り当て、unit の一覧）。最終確認の後に helper に渡す |
| 公開（publish） | 導入先が起動できる状態にする最後の書き込み（loader と `zedbsd.cfg`）。§7 |

## 3. 全体の構成

```text
installer（Keiland のアプリ、利用者 kei で動く）
  │ 画面・検査・計画の組み立て（disk には触らない）
  │ stdin/stdout の pipe（行の protocol、§9.2）
  ▼
install-admin（set-user-ID root の helper、1 つの仕事だけ）
  │ probe（読むだけ）／ install（計画を実行）
  ├─ /sbin/diskpart（GPT の書き込み、reload）
  ├─ /sbin/mkfs -t fat32 / -t ufs --profile=native（device の format）
  ├─ /sbin/mkswap（swap の header。partition の mode を足す、§11.3）
  └─ unit の検査（SHA-256）と展開（ustar＋deflate）、設定の書き込み
/opt/installer/（live の system の読み取り専用の payload、§6）
```

- installer は disk の device を開かない。disk の一覧・partition の表・ESP の空きは helper の `probe` から得る。
- disk に書くのは helper だけ。書く時は 2 つ（U2 の回答「ボタンでその場で書く」）:
  - S4（partition の編集）の「削除」「作成」: 押した時にその場で partition の表を書く（§4.4・§7.1）。キャンセルしても書いた分は残る。
  - それ以外（disk 全体を使う時の表、root・swap の型の印と format、展開、設定、公開）: 最終確認の後（§7.2）。root・swap の型の印と format の時期は → U14（推し: 最終確認の後）。

## 4. 画面

共通:

- 1 つの window。下の帯に「戻る」「次へ」（primary）、右上に「キャンセル」。Enter は「次へ」、Alt+←/→ は戻る・次へ（Settings の Welcome と同じ、`userland/desktop/settings/welcome.c:186-253`・`:308`）。
- 「次へ」は、その画面の検査を満たさない時は無効にし、理由を 1 行で出す。
- 最終確認より前の「キャンセル」は、確認の dialog（`kl_dialog`）の後、S4 で disk に書いた変更が無ければ installer を閉じる（disk は無傷）。書いた変更があれば S10 に移り、書いた変更の一覧を出す（§4.10）。
- 文字は `kl_tr` で翻訳する（domain `installer`、§10）。
- 試験のため、Settings と同じ形で `INSTALLER READY`・画面の名前・control の矩形を session log に出す（`plan/tools/aat/scenarios/aatlib.py:71-77`、`settings/ui.c:1755` の形に倣う）。

### 4.1 S1 Welcome

- 表示: Kei のロゴ、「Kei Operating System をこのコンピュータに入れます」の説明、注意（導入先の disk の内容が消えうる、data の backup を勧める）、言語（desktop の `ui.language` に従う、§10）。
- 検査: helper の `probe` が成功している（失敗なら理由と「再試行」を出し、次へは無効）。UEFI で起動していない（`/boot/efi` が無い）時は「UEFI で起動した時だけ使える」と出して次へを無効にする。
- 遷移: 次へ → S2。

### 4.2 S2 導入先の disk の選択

- 表示: disk の一覧（1 行 1 台）: 名前（`nvme0n1`・`sda`）、種類（NVMe・SATA・USB、`hw.diskstats` の kind、`include/uapi/sysctl.h:104-170`）、大きさ（`BLKGETINFO` の sector の数×大きさ）、取り外し可の印、partition の要約（数・ESP の有無・partition table の種類）、型番（→ U9）。
- 選べない disk は灰色で理由を出す:
  - live の system の disk（`/`・`/boot/boot0`・`/boot/efi` を含む disk。`volumed` と同じ求め方、`userland/base/volumed/main.c:480-540`）。
  - 読み取り専用（`KERN_BLOCK_READ_ONLY`）、file を裏に持つ物（`loopN`・ramdisk）。
  - logical sector が 512 byte でない（4Kn）: 今の `mkfs` の device の mode が 512 byte だけ（`userland/base/mkfs/block-command.c:144-148`）。§16 T7。
  - 小さすぎる（§5.6 の最小）。
- disk の抜き差し: helper の `probe` を「再読み込み」ボタンで取り直す（自動の追従は Future）。
- 検査: 1 台を選んでいる。
- 遷移: 戻る → S1、次へ → S3。

### 4.3 S3 partition の扱い

- 表示: 2 つの選択肢（radio 相当。libkeiland に radio が無いので、選択の印の付いた大きな button 2 つ、`kl_button`）。
  - 「disk 全体を使う」: 既存の partition が全て消えることを、既存の partition の一覧（番号・大きさ・型・label）とともに出す。
  - 「partition を選ぶ」: 既存の partition を保ち、空きに作る。
- 「partition を選ぶ」が選べない disk（→ U10）: MBR の disk（推し A）。理由を出す。
- 検査: どちらかを選んでいる。
- 遷移: 戻る → S2。全体 → S7。選ぶ → S4。

### 4.4 S4 簡易の partition の編集（「partition を選ぶ」の時）

- 表示: partition の一覧（slot・開始・大きさ・型の名前・label・「新規」「未使用」の印）と空き領域の行。下に「削除」「作成」。
- 操作は押した時にその場で disk の partition の表に書く（U2 の回答「ボタンでその場で書く」）。手順は §7.1:
  - 画面に入った時、1 回だけ「この画面の削除・作成はその場で disk に書きます。キャンセルしても元に戻りません」を出す。最初に書く操作の前に管理者の password を聞き、helper の session を始める（§9）。
  - **削除**: 選んだ partition を消す。確認の dialog（partition の番号・大きさ・型・label と「中身は消えます」、`KL_BUTTON_DANGER`）の後に書く。mount 中の partition（live の system が使っている物、helper が外せない物）は削除の button を無効にし理由を出す。
  - **作成**: 大きさだけを聞く（`kl_field`、MiB・GiB の単位、既定は最大の空き）。置き場所は大きさが入る最初の空き（LBA の小さい方）、開始と大きさは 1 MiB に揃える。型は zedBSD 独自の「未割り当て」（U3 の回答、§5.3）、label は無し。確認の dialog は出さない（既存の data を消さないため）。
  - **ESP の自動作成**: 作成の時、disk に ESP（型 `C12A7328-F81F-11D2-BA4B-00A0C93EC93B`）が無ければ、新しい partition の前に ESP（§5.4 の 512 MiB）を同じ空きに作り、`mkfs -t fat32` で format する。ESP と新しい partition の両方が入らない、または GPT の slot（diskpart は 16 まで、`userland/base/diskpart/main.c:401`）が 2 つ空いていない時は、何も書かずに作成の失敗を出す。表を書いた後に ESP の format が失敗した時は、作った 2 つの entry を消す書き込みを試み、作成の失敗として出す（消せなければ、その状態を出す）。
  - 書いた後は helper が表を読み直し、一覧を disk の実際の表で描き直す（画面の想定でなく）。
  - 表を書いたが kernel の reload が失敗した時（diskpart の exit 3。導入先の partition が mount 中など）: 「表は書いたが、この disk の新しい partition は再起動まで使えない」を出し、次へを無効にする（S10 へ進む button を出す）。
- 検査（次へ）: disk に「十分な ESP」（§5.4）が 1 つ以上ある。無ければ次へを無効にし理由を出す。
- 遷移: 戻る → S3（書いた変更は残る。S3 で「disk 全体を使う」に変えると、全体の道の表で置き換わる）。次へ → S5。

### 4.5 S5 rootfs の partition の選択

- 表示: disk の partition のうち、root に選べる物（→ U12。推し A は型が「未割り当て」の partition だけ。S4 で作った物と、前の回にこの installer で作って残った物）。各々の大きさと、必要な最小（§5.6）。
- 選んだ partition は計画の上で型を UFS（`516E7CB6-6ECF-11D6-8FF8-00022D09712B`、`tools/build/zedimage-host.c:428-431` と同じ）、label を `Kei root` にし、最終確認の後に `mkfs -t ufs --profile=native` で初期化する（→ U14）。
- 検査: 1 つを選んでいる（→ U11 推し A: 選ばないと次へ進めない）。大きさが最小以上。
- 遷移: 戻る → S4、次へ → S6。

### 4.6 S6 swap の partition の選択

- 表示: root 以外の選べる partition（U12 と同じ規則）と「swap の partition を使わない」の行。
- 選んだ partition は型を swap（`516E7CB5-6ECF-11D6-8FF8-00022D09712B`）、label を `Kei swap` にし、最終確認の後に swap の header を書く（`mkswap` の partition の mode、§11.3。→ U14）。
- 選ばない時: 次へで確認の dialog（「swap の partition がありません。root の UFS の上に swap file を作ります」）を出す（原文「swapパーティションは選択しなくてもいいが、確認はされる」）。swap file は §5.5。
- 検査: 最小の大きさ（64 MiB）以上。
- 遷移: 戻る → S5、次へ → S7。

### 4.7 S7 入れる項目の選択（S3 の両方の道の合流）

- 表示: check box 相当の行（`kl_switch` か選択の印の付いた行）。

  | 項目 | 中身（unit） | 既定（U8 の回答） |
  | --- | --- | --- |
  | Base System（header 込み） | `base`（kernel・loader・base の program・library・`/usr/include`） | 必須（外せない） |
  | Keiland Desktop | `desktop`（compositor・sessiond・標準の app・desktop の library・font） | 選択。外す時に警告 |
  | Firmware | `firmware`（`/lib/firmware` と license） | 選択。外す時に警告 |
  | Packages | 下位の項目 | 下位の既定に従う |
  | 　Clang | `lang/clang`（依存 `devel/libcxx`） | 無選択 |
  | 　OpenSSH | `network/openssh`（依存 `security/openssl`） | 選択 |
  | 　OpenSSL（＋CA 証明書） | `security/openssl`・`security/ca-certificates` | 選択（U8 の「OpenSSL」「ca-certificates」） |
  | 　GTK4 | `desktop/gtk4`（依存 glib・cairo・pango・harfbuzz など 15 個、`userland/packages/desktop/gtk4/Makefile:123-126`） | 無選択 |
  | 　Fonts | `fonts/*`（noto-color-emoji・hand-hershey） | 選択 |
  | 　libavcodec | `multimedia/libavcodec` | 無選択 |
  | 　curl | `network/curl`（依存 openssl・zlib・ca-certificates） | 無選択 |
  | 　Python | `lang/python3`（依存 zlib・openssl・ca-certificates、`userland/packages/lang/python3/Makefile:209-210`）。U8 の回答で追加 | 無選択 |

- Desktop・Firmware を外す時の警告（U8 の回答「外そうとすると警告」）: `kl_dialog` で「外すと、入れた system は console の login だけになります（graphical の desktop がありません）」「外すと、Wi-Fi・GPU などの firmware が要る device が動かないことがあります」を出し、「外す」「やめる」を選ばせる。
- ca-certificates は原文で「OpenSSL (+cacertificate)」の 1 項目、U8 の回答では既定の名前に別に挙がる。1 つの項目（OpenSSL＋CA 証明書）のまま、既定で選択とした（§16 T13）。

- 依存は自動で選び、「依存で入る」と灰色の印で出す（外せない）。依存の元は §6.3 の index。例: Keiland Desktop は `fonts/noto-color-emoji`・`fonts/hand-hershey` を要る（`userland/desktop/wayland/Makefile:47`）ので、Desktop を選ぶと Fonts が依存で入る。GTK4 は `desktop/libwayland`（Desktop の unit）を要るので Desktop が依存で入る。
- 下に合計（展開後の大きさ）と導入先の root の空きを出す。
- 検査: root の大きさ ≥ 選んだ unit の展開後の合計＋swap file（作る時）＋余裕（§5.6）。足りなければ次へを無効にし、不足の量を出す。
- 遷移: 戻る → S6（選ぶ道）か S3（全体の道）。次へ → S7a。

### 4.7a S7a 利用者（U7 の回答で追加）

- 表示と入力（`kl_field`。password は `secret`）:
  - 利用者の名前（login の名前。小文字の英字で始まり、英小文字・数字・`-`・`_`、32 文字まで。`account-admin` の名前の規則に合わせる）と表示名。
  - 利用者の password と確認。
  - root の password と確認。
  - 自動 login（`kl_switch`、既定は入。Desktop を入れない時は無効）。
- 作る利用者は管理者（wheel）と network の group に入れる（今の image の kei と同じ、`userland/base/etc/group:1`）。
- 検査: 名前が規則に合い、`root` や base の system の利用者の名前でない。password が空でなく、確認と一致する。
- 導入先の設定（§7.2 の段 7）: base の unit の既定の利用者 kei（uid 1000）を、入力した名前・password に置き換える（名前が kei なら password だけを変える）。root の password を設定する。自動 login が入なら `/etc/keiland/autologin` に名前、切なら空にする（`userland/desktop/sessiond/main.c:201-217`）。password の hash は `account-admin` と同じ共有の account の core で作る。
- 遷移: 戻る → S7、次へ → S8。

### 4.8 S8 最終確認

- 表示: 導入先の disk（名前・種類・大きさ・型番）、partition の表（全体の道は前と後の表、消える partition を赤で。選ぶ道は S4 で書いた後の今の表）、root・swap・ESP の割り当て、format される partition、入れる項目、loader の置き場所（§5.4）。
- 「disk 全体を使う」の時は「disk の全ての data が消えます」を太字で出し、確認の check（「消してよい」）を入れないと開始できない。
- 開始の button（`KL_BUTTON_DANGER`）を押すと、helper の session がまだ無ければ（全体の道）管理者（wheel）の password を聞く dialog（`kl_field` の secret）。helper が password を確かめる（§9）。
- 遷移: 戻る → S7。開始 → S9。password の誤りは同じ dialog で再入力。

### 4.9 S9 progress

- 表示: 段の名前（§7 の手順の 1 行）、全体の進み（`kl_progress`、展開は byte で）、今の unit の名前、経過の時間。
- 「キャンセル」: 確認の dialog の後に helper に中止を送る。partition の表の書き込みと format の最中は、その段の終わりで止まる（途中の段を壊さない）。止まったら S10。
- 遷移: 成功 → S11。失敗・キャンセル → S10（原文「progressだけでなく、キャンセルや失敗時にここに飛ぶ」）。

### 4.10 S10 失敗

- 表示: 失敗かキャンセルか、失敗した段と理由（helper の message）、**導入先の今の状態**（S4 で書いた変更の一覧、変えた partition の表、format した partition、公開していないので導入先からは起動しない事）、「最初からやり直す」（S2 へ、probe を取り直す）と「閉じる」。
- 来る時: S9 の間のキャンセル・失敗、S4 で書いた後のキャンセル、S4 の書き込みの失敗で先へ進めない時、probe 以降の helper の失敗。S4 で何も書いていない時のキャンセルは S10 に来ない（installer を閉じる、disk は無傷）。§16 T1。

### 4.11 S11 終了

- 表示: 完了の message、入れた物、導入先から起動する方法（§5.8。loader を fallback の path に置いた時は「USB を外して再起動」。`\EFI\ZEDBSD\` に置いた時は「firmware の boot menu で選ぶ」（U5 の回答）。Boot#### を作った時はその名前と BootOrder の位置（→ U15））、作った利用者の名前。
- 「再起動」: `kl_system_power_action(…, KL_POWER_REBOOT, …)`（`userland/desktop/include/keiland/keiland.h:2293`。sessiond が root か wheel の session だけに許す、`sessiond/power-rules.c:8-16`。kei は wheel、`userland/base/etc/group:1`）。
- 「閉じる」: installer を閉じ、live の desktop をそのまま使える（原文）。

## 5. disk と partition の規則

### 5.1 disk の列挙（helper の probe）

- `/dev` の block device を `BLKGETINFO`（`include/uapi/block.h:29-42`）で調べ、partition でない物（`KERN_BLOCK_PARTITION` の無い物）を disk とする。`diskpart --machine list` と同じ（`userland/base/diskpart/main.c:92-125`）。
- partition の表は diskpart の table の code（`table.h` の `dp_load`）で読む。GPT・MBR・空を区別する。
- 各 partition の fs の型・label・PARTUUID は `BLKGETIDENTITY`（`include/uapi/blkid.h:26-36`）、mount の有無は `KERN_SYSTEM_GET_MOUNTS`。
- 既存の ESP の空き: helper が ESP を読み取り専用で private の mount point に mount して `statvfs` で量り、すぐ unmount する（mount 中の ESP はその mount から量る）。

### 5.2 partition table

- GPT だけを書く。1 MiB 境界。protective MBR と primary・backup の GPT（diskpart の `init`・`add`・`delete` の既存の動作、`docs/reference/block-administration.md`）。
- 「disk 全体を使う」: `diskpart init` で表を置き換える（disk GUID・PARTUUID は helper が乱数で作る）。
- 「partition を選ぶ」: S4 の 1 つの操作ごとに書く（U2 の回答）。削除は `diskpart delete`、作成は `diskpart add`。ESP の自動作成は ESP と新しい partition の 2 つの entry を 1 回で書く（diskpart に複数の add を 1 回の確認・書き込みで行う batch の mode を足す、§11.3。2 回に分けると間で止まった時に ESP だけが残るため）。触らない entry は byte 単位で保つ（GPT の attribute を含む。diskpart の add・delete は他の entry を書き換えない）。型の印（root・swap）は最終確認の後に、型を変える batch で書く（→ U14）。
- 書いた後 `BLKREREADPART` で reload する。導入先の disk の partition が mount 中だと EBUSY になる（`src/kern/partition.c:316-377`）ので、helper は書く前に導入先の disk の自動 mount（`volumed` の mount）を外し、外せなければ書かずに失敗する。

### 5.3 型と label

| 用途 | 型 GUID | label |
| --- | --- | --- |
| ESP | `C12A7328-F81F-11D2-BA4B-00A0C93EC93B` | `EFI System` |
| root | `516E7CB6-6ECF-11D6-8FF8-00022D09712B`（今の image と同じ） | `Kei root` |
| swap | `516E7CB5-6ECF-11D6-8FF8-00022D09712B`（同） | `Kei swap` |
| 未割り当て（S4 で作った物。root・swap に選ばれるまで） | zedBSD 独自の GUID（U3 の回答）。値は p002 で乱数の v4 GUID を 1 つ決め、`include/uapi/` の header と diskpart の型の名前の表に置く | 無し |

- 起動の設定は PARTLABEL でなく PARTUUID で選ぶ（§7.4）。今の image は `PARTLABEL=zedBSD-root` で、同じ label の disk が 2 台あると曖昧で起動が失敗する（`src/kern/block-identity.c:184-190`、[WS062](../ws062/ws.md) の「制限・移管」）。live の USB を挿したまま導入先から起動しても曖昧にならないようにする。

### 5.4 ESP

- 新しく作る ESP の大きさ: 512 MiB（U4 の回答。FAT32 の最小は約 33 MiB、`userland/base/mkfs/fat32-format.c:66`）。
- ESP に置く物: loader（`BOOTX64.EFI`、今 29 KiB）、`vmunix`（今 約 4 MiB）、`zedbsd.cfg`、`logo.ppm`（U4 の回答: ESP に全部。今の image のような別の ZEDBSD の FAT は作らない）。loader は同じ disk の全ての FAT から root の `/zedbsd.cfg` を探す（`bootloader/uefi/bootx64.c:1815` 付近）ので ESP の root に置けば足りる。
- 「十分な ESP」（S4 の検査）: 型が ESP、FAT16 か FAT32、かつ
  - 計画で新しく作る ESP: 上の大きさ。
  - 既存の ESP: 空き ≥ 置く物の合計＋16 MiB（`/opt/installer` の index に置く物の大きさを書く。旧 zedinst の `max(64 MiB, loader＋kernel＋FAT の overhead＋16 MiB)` に倣う）。既に `zedbsd.cfg` がある ESP（別の zedBSD）は、上書きせず「既に別の Kei がある」と出して使わない（§13）。
- loader の path（U5 の回答）: `\EFI\BOOT\BOOTX64.EFI` が無ければそこに置く。有れば上書きせず `\EFI\ZEDBSD\BOOTX64.EFI` に置き、S11 で firmware の boot menu から選ぶよう案内する。UEFI の `Boot####` も作る（§5.8）。
- 新しい ESP は `mkfs -t fat32` で作る。その FAT の serial は disk の登録番号で永続の識別でない（`fat32-format.c:156-158`）ので、`zedbsd.cfg` に `boot0=PARTUUID=<ESP の PARTUUID>` を書く（loader の自動の `boot0=UUID=` に頼らない、`bootloader/uefi/zedbsd-config.c:157-173`）。

### 5.5 swap

- swap の partition を選んだ時: `mkswap` の partition の mode（§11.3）で ZEDSWAP2 の header を書き、`zedbsd.cfg` に `swap0=PARTUUID=<swap>`。kernel は header の無い partition を受け付けず、起動が止まる（`docs/reference/kernel-boot-parameters.md:219-252`、`src/kern/vm/swap-source.c:351-440`）ので、helper は書いた後に header を読み直して確かめる。
- 選ばない時: root の UFS に `/swapfile` を作る。大きさ = min(RAM, 2 GiB − 4 KiB)（`mkswap` の file の上限は INT32_MAX、`userland/base/mkswap/swap-format.c:31-44`）。穴の無い file として 0 を書いてから `mkswap`、`/etc/fstab` に `/swapfile none swap sw 0 0`（init が `swapon -a` で有効にする、`userland/base/init/main.c:310-311`）。旧 zedinst と同じ手順（`git show 2cf4b30e6^:userland/retro/zedinst/nativeinstall.noct`）。extent は 1024 まで（`swap-source.c:44`）。新しい UFS の上なので通常は満たす。満たさなければ swap file を作らず、S11 で「swap 無し」と出す（失敗にしない）。
- 原文の「loopback swapfile」は、今の kernel が UFS の上の file を extent で直に swap に使う（loop device を通さない、`swap-source.c:145-200`）ので、その方式で満たす（§16 T2）。

### 5.6 大きさの検査

- root の最小 = 選んだ unit の展開後の合計 × 1.1 ＋ swap file（作る時）＋ 1 GiB。S5 では Base だけの値、S7 で選んだ物の値で確かめる。
- `mkfs -t ufs --profile=native --check-size BYTES` で、その大きさが UFS にできるかを helper の probe で確かめる（`docs/reference/image-formatters.md`）。
- 「disk 全体を使う」の disk の最小 = ESP ＋ swap（作る時）＋ root の最小。

### 5.7 「disk 全体を使う」の既定の配置（U4 の回答）

| # | partition | 大きさ |
| --- | --- | --- |
| 1 | ESP（FAT32） | 512 MiB |
| 2 | swap | RAM と同じ、1 GiB 以上 8 GiB 以下（RAM は sysctl の `HW_MEMORY_STATS`、`include/uapi/sysctl.h:43`）。disk が 32 GiB 未満なら 1 GiB |
| 3 | root（UFS） | 残り全部 |

### 5.8 UEFI の boot entry（U5 の回答「Boot####の作成もWSに入れる」）

- 今: kernel に UEFI の runtime services の口が無い。amd64 の handoff に EFI の system table が無い（`bootloader/include/amd64-handoff.h:177-195`）。efibootmgr に当たる道具も無い。
- 作る物:
  1. loader → HAL: UEFI の loader が ExitBootServices の前に、EFI の system table（runtime services の table）の物理 address と、runtime の memory の範囲（`EfiRuntimeServicesCode`・`Data` の記述子と attribute）を handoff に足す（handoff の新しい版。loader → HAL の固有の構造体は `bootloader/` の header に置く、Guardrail「HAL と kernel・boot loader と HAL の受け渡し」）。
  2. HAL → kernel: 名前の値（`hal_get_arch_handoff(name)`、例 `uefi.system_table`・`uefi.runtime_map`）で渡す。既存の API なので HAL の API は変えない。
  3. kernel: amd64 の platform の module（`src/drivers/platform/pcat/` の下）が runtime の範囲を専用の page table に 1:1 で map し、`GetVariable`・`SetVariable`・`GetNextVariableName` を ms_abi で呼ぶ（`SetVirtualAddressMap` は呼ばない。呼び出しの間は割り込みと他の CPU からの同時の呼び出しを排他）。
  4. UAPI: `/dev/system` に変数の読み書きの要求（root だけ）。base の道具 `efiboot`（`list`・`create`・`delete`・`order`）。
- **HAL の API に触る場合**: 3 の「専用の page table への切り替え」「FPU・SIMD の状態の保存」などに、今の HAL の宣言（`include/hal/hal.h`）で足りない物があれば、その差分の案を該当の Phase の plan に置き、ユーザーの承認を得てから適用する（AGENTS.md §7）。承認が得られるまで Boot#### の作成は無効のまま、installer は loader の配置と S11 の案内だけを行う。
- installer の使い方: §7.2 の段 10 で、導入先の ESP の PARTUUID・partition の番号・開始・大きさと loader の path から device path（`HD(番号, GPT, PARTUUID, 開始, 大きさ)/File(\EFI\…\BOOTX64.EFI)`）を作り、名前「Kei」の `Boot####` を書く。BootOrder の位置は → U15。同じ device path の既存の entry があれば作り直さずに使う。書けなければ失敗にせず、S11 で「boot menu で選ぶ」を出す（loader は置いてあるので起動はできる）。

## 6. 入れる物（payload）

### 6.1 置き場所と形

- live の system の `/opt/installer/`（原文）:

  ```text
  /opt/installer/index              unit の一覧（§6.3）
  /opt/installer/boot/BOOTX64.EFI   loader
  /opt/installer/boot/vmunix        kernel
  /opt/installer/boot/logo.ppm      起動の logo
  /opt/installer/units/base.tar.gz
  /opt/installer/units/desktop.tar.gz
  /opt/installer/units/firmware.tar.gz
  /opt/installer/units/<category>-<name>.tar.gz   package ごと（例 lang-clang.tar.gz）
  ```

- tarball は ustar（`pax` が読み書きする形、`userland/base/pax/main.c:239`）を deflate（gzip の形）で圧縮した物。guest の展開は helper が `libz-compat`（base の library、inflate がある、`userland/base/libz-compat/Makefile:3-8`）で行う。zstd・xz は guest に無いので使わない（§16 T3）。
- 所有者・mode（set-user-ID を含む）・mtime・symlink を tarball に持つ。値は今の root の image を作る時と同じ manifest から取る（build の staged の tree は build の利用者の所有なので、tree の stat を使わない）。
- 展開の安全: 絶対 path・`..`・導入先の外を指す hard link・device の node は拒む。symlink は作るだけで辿らない。

### 6.2 unit の分け方（build）

- 今の root の tree（`$(BUILD)/rootfs`、`Makefile:1415-1459` の `ZEDBSD_ROOTFS_TREE_RULE`）の各 file を、登録した package（`ZEDBSD_USERLAND_PACKAGE`、`Makefile:447-471`）の menu（第 8 引数: base・desktop・firmware・packages の類）で unit に振り分ける:
  - menu が base の package、`/etc`・`/usr/include`（`ZEDBSD_ROOTFS_DEVELOPMENT=y` の header、`Makefile:1485`・`:1520-1528`）、`/lib` の runtime → `base`
  - menu が desktop → `desktop`
  - `userland/firmware/*` → `firmware`
  - `userland/packages/<category>/<name>` → `<category>-<name>`
- installer 自身（app と helper）と `/opt/installer` は、どの unit にも入れない（導入先に installer を残さない）。
- build の検査: 全ての file がちょうど 1 つの unit に属し、unit の和が root の tree と一致する（漏れ・重複の検出）。
- unit を作る config: 配布用の config（→ U1）が Packages の 7 項目とその依存の package を全て build する。live の system に package を入れておく必要は無い（unit だけを `/opt/installer` に置く）。

### 6.3 index と依存

- `/opt/installer/index` は build が作る text（1 行 1 unit、TAB 区切り）: unit 名、表示名、group（base・desktop・firmware・package）、版、tarball の大きさ、展開後の大きさ、SHA-256、依存の unit（空白区切り）。S7 の項目と下位の項目の対応（項目 → unit の一覧）も index に書く（項目を増やす時に installer を変えない）。
- 依存の元は `ZEDBSD_USERLAND_PACKAGE` の第 10 引数（REQUIRE、`Makefile:521-532` が展開する物）。group を跨ぐ依存（desktop の compositor → `fonts/*`、`gtk4` → `desktop/libwayland`）は unit の依存に写す（package の依存が desktop の unit の中の package を指せば `desktop` に、base の中を指せば `base` に）。
- installer は選んだ項目の unit の閉包を index から求める（循環は build の検査で拒む）。

### 6.4 導入の記録

- 入れた unit の一覧と版を `/var/db/installer/units`、unit ごとの file の一覧を `/var/db/installer/<unit>.files` に書く。今の guest に package の database は無い（WS153 の設計は `.kapp` で別物、`plan/ws153/ws.md:20-22`）。後の package の管理（削除・更新）はこの記録を読む前提で Future に置く。

## 7. disk への書き込み（helper）

### 7.1 S4 の編集（その場で書く、U2）

1. **再確認**: disk の登録番号・大きさ・partition の表が、画面が表示している表と同じか（`BLKGETINFO` と表の読み直し）。違えば書かずに失敗（「disk が変わった」）し、画面の表を描き直す。
2. **自動 mount を外す**: 導入先の disk の `volumed` の mount を外す。消す partition が mount 中で外せなければ書かずに失敗。
3. **書く**: 削除 → `diskpart delete`、作成 → `diskpart add`、ESP の自動作成 → batch で 2 つの add、続けて ESP を `mkfs -t fat32`（失敗の扱いは §4.4）。確認の phrase（`WRITE name:registration`、`FORMAT name:registration`）は helper が、installer の画面の確認の後に送る。
4. **reload の結果**を返す（exit 3 は「書いたが reload の失敗」、§4.4）。
5. 表を読み直して返す。

### 7.2 インストール（最終確認の後）

順序は「起動できない途中の状態を、起動できるように見せない」ことを守る。loader と `zedbsd.cfg` は最後に置く（旧 zedinst の「設定を最後に公開する」と同じ）。

1. **再確認**: 計画の disk の登録番号・大きさ・partition の表が、最終確認の画面が表示した表と同じか（`BLKGETINFO` と表の読み直し）。違えば何も書かずに失敗（「disk が変わった」）。
2. **disk の確保**: 導入先の disk の自動 mount を外す。`BLKRESERVE` の前提（mount・open が無い）を満たさなければ失敗。
3. **partition の表**: §5.2。全体: `diskpart init`。選ぶ: 表は S4 で書き済み。root・swap の型と label を batch の mode で変える（→ U14）。reload まで。
4. **format**: 新しい ESP を `mkfs -t fat32`、root を `mkfs -t ufs --profile=native`、swap の partition に `mkswap`（§11.3）。各々の確認の phrase（`FORMAT name:registration` など、`docs/reference/image-formatters.md`）は helper が自分の最終確認の後に送る（diskpart・mkfs の文書が installer に許す使い方）。
5. **root の mount**: root を `/run/install-admin/target` に mount。
6. **展開**: 計画の unit を依存の順（base が先）に、SHA-256 を確かめてから展開。進みを byte で報告。中止の要求は file の間で受ける。
7. **設定**: `/etc/fstab`（swap file の時）、`/var/db/installer/`、swap file（§5.5）、利用者と root の password と自動 login（§4.7a）。live の system の `/etc` は写さない（base の unit の既定）。Desktop を入れない時は graphical の login を設定しない（`zedbsd.cfg` に `login=graphical` を書かない）。
8. **root の unmount と sync**。
9. **ESP の公開**: ESP を mount し、`vmunix`・`logo.ppm` を書いてから loader（§5.4 の path）、最後に `zedbsd.cfg` を一時の名前で書いて rename。`zedbsd.cfg` の中身:

   ```text
   kernel=vmunix
   boot0=PARTUUID=<ESP>
   rootpart=PARTUUID=<root>
   swap0=PARTUUID=<swap>        （swap の partition の時だけ）
   logo=logo.ppm                （live の system の設定に倣う）
   login=graphical              （Desktop を入れた時だけ）
   kmsg=quiet
   ```

10. **ESP の unmount と sync**。
11. **boot entry**: `Boot####` を書く（§5.8）。失敗は警告にとどめる。成功を報告。

### 7.3 起動の識別

- root・swap・boot0 は PARTUUID で選ぶ（§5.3）。PARTUUID は helper が乱数で作り、disk の中と既存の entry と重ならないようにする。

## 8. 失敗とキャンセル

| 時 | 導入先の状態 | 画面 |
| --- | --- | --- |
| S4 で何も書く前のキャンセル | 無傷 | installer を閉じる |
| S4 で書いた後のキャンセル（S5〜S8） | S4 の変更（削除・作成・ESP）が残る | S10 に書いた変更の一覧 |
| S4 の 1 つの書き込みの失敗 | §4.4（作成の失敗は ESP の entry を消す試み、reload の失敗は再起動まで使えない） | S4 に理由。先へ進めなければ S10 |
| §7.2 の段 1〜2 の失敗 | S4 の変更だけ | S10 |
| 段 3 の途中・後 | partition の表が変わったかもしれない（diskpart の exit 3 は「書いたが reload の失敗」） | S10 に表の状態と「再起動が要るかもしれない」 |
| 段 4〜8 の失敗・キャンセル | 表は新しく、format・展開が途中。`zedbsd.cfg` は無いので導入先からは起動しない | S10 に状態 |
| 段 9 の失敗 | ESP に一部の file。`zedbsd.cfg` は最後なので起動の設定は無い | S10 に状態 |

- 巻き戻しはしない（diskpart と mkfs は crash に原子的でない、`docs/reference/block-administration.md`。§4.4 の ESP の自動作成の失敗だけは作った entry を消す）。「最初からやり直す」で同じ disk に入れ直せる（全体なら表を作り直す。選ぶなら S4 で作った partition は型が「未割り当て」のまま残り、S5 で選べる、→ U12）。
- 段 3・4 の最中のキャンセルはその段の終わりまで待つ。段 6 は file の間で止まる。

## 9. 権限（U6 の回答「setuid の helper、自分で起動 (推し)」）

### 9.1 形

- installer は利用者（live の session の kei）で動き、権限を持たない（`docs/architecture/security.md:11-30` の原則）。
- root の仕事は新しい set-user-ID root の helper `/usr/libexec/install-admin`（mode 4555、`account-admin` と同じ形の 1 つの仕事だけの program、`userland/base/account-admin/main.c:8-45`。呼び出し元が wheel かは helper が自分で確かめる）。
- installer が helper を子として起動する（U6 の回答。zedBSD の UEFI PC だけの app なので compositor を通さない。Guardrail の「app 自身の機能のための OS の依存」、D14）。
- 要求の種類:
  - `probe`: 読むだけ（disk・partition・ESP の空き・RAM・UEFI の起動か）。呼び出し元が wheel なら password 無し（変更が無いため）。
  - `session`: 書き込みの session。最初の行の呼び出し元の password を確かめ（security.md の「管理者は変更の時に自分の password を入れる」）、installer が終わるまで stdin の要求を順に受ける: `edit`（§7.1 の 1 つの操作）と `install`（§7.2）。S4 の最初の書き込みの前（選ぶ道）か最終確認の開始（全体の道）で始める。U2 の「その場で書く」で操作ごとに password を聞くと S4 が使いにくいので、1 つの installer の実行に 1 回とする（sudo の一定時間の記憶に近い。§16 T12）。
- 全ての要求を syslog の `auth` に記録する（password は記録しない）。

### 9.2 protocol

- `session` の stdin: 最初に password。続けて要求を 1 行ずつ: `edit delete <slot>`・`edit create <start> <count>`・`edit create-with-esp <start> <count>`（各々に disk の識別と、画面が表示した表の digest を付ける）、`install <計画>`。`install` の計画は（disk の登録番号と識別、partition の表の変更、root・swap・ESP、unit の一覧、利用者の設定）。stdout: 1 行ずつ `STEP <n> <総数> <名前>`、`BYTES <済み> <合計>`、`UNIT <名前>`、`DONE`、`FAIL <段> <errno> <message>`、`STATE <導入先の状態の要約>`。
- 中止: installer が stdin に `cancel` の行を送る（signal でなく。set-user-ID の process への signal の可否に頼らない）。installer が終わった（stdin の EOF）時も中止と扱う。
- helper は計画を全て自分で検査し直す（installer を信用しない）: 選んだ disk が live の system の disk でない、partition の重なり・境界、触らない entry を変えていない、unit が index にあり SHA-256 が合う。

## 10. 翻訳と表示

- 文字は `kl_tr`（domain `installer`）。英語が原文、日本語の catalog `userland/desktop/locale/ja/installer.tr` を作る（`tools/i18n/tr.py`、WS158 の仕組み）。desktop の `ui.language` に `kl_tr_follow` で従う（`keiland.h:2849`）。
- 寸法は論理 px（Guardrail「寸法の単位」）。
- App Home の entry を `userland/desktop/wayland/apps.conf` に足す（command の path が無ければ隠れる、`wayland/home.c:41-50`。installer の無い image では出ない）。icon は `wayland/icons.c:596-619` の一覧に足す。

## 11. build と image

### 11.1 置き場所

| 物 | source |
| --- | --- |
| installer（app） | `userland/desktop/installer/`（`ZEDBSD_USERLAND_PACKAGE`、platform は amd64、menu desktop） |
| install-admin（helper） | `userland/base/install-admin/`（base の program。Guardrail「userland/base は arch で有無を分けない」に従い全 platform で build し、UEFI でない system では `probe` が「対象外」を返す） |
| unit と index を作る道具 | `tools/build/`（host） |
| 配布の config | menuconfig の `ZEDBSD_INSTALLER`、配布用の config で y（U1 の回答） |

### 11.2 image

- `ZEDBSD_INSTALLER=y` の時: 全 unit と index を作り、live の root に `/opt/installer` と installer・install-admin を入れる。root の大きさ（`AMD64_NATIVE_ROOT_MIB`、今 1024、`platform/amd64/vmunix.mk:1344`）を unit の合計に合わせて大きくする。
- 起動時に自動で開く（U1 の回答）: compositor の Welcome の自動起動（`userland/desktop/wayland/desktop.c:857-880`）と同じ仕組みで、`/opt/installer/index` がある時に 1 回 `installer` を起動する。Settings の Welcome と重ならないよう、installer がある時は Welcome を出さない。

### 11.3 base の道具に足す物

- `mkswap` の partition の mode: `mkswap DEVICE`（block device、`BLKRESERVE`、`FORMAT name:registration` の phrase、書いた後の検証）。今は regular file だけ（`userland/base/common/format-file.c:48`）。
- `diskpart` の batch の mode: 複数の操作（add、型と名前の変更）を 1 回の確認（`WRITE name:registration`）で 1 回書き、触らない entry を保つ。型と名前の変更は今の diskpart に無い（add・delete・init だけ、`userland/base/diskpart/main.c:73-90`）。
- （U9 が B の時）kernel: NVMe の Identify Controller の model・serial、AHCI の IDENTIFY の model・serial、USB の INQUIRY の vendor・product を `struct disk` に持ち、新しい UAPI（`BLKGETINFO` の版を上げるか新しい ioctl）で出す。HAL の API は変えない。

## 12. 試験

| 層 | 方法 |
| --- | --- |
| host | 計画の組み立て（配置・ESP の自動作成・検査）、index と依存の閉包、ustar＋deflate の展開（path の安全）、`zedbsd.cfg` の生成、diskpart の batch と mkswap の partition の mode の table・codec。host の C の試験 |
| QEMU の helper | live の image（`ZEDBSD_INSTALLER=y`）を USB で起動、導入先の空の disk を `guest.py start --qemu-extra` で足す（NVMe `-device nvme`、SATA は q35 の ich9-ahci に `-device ide-hd`、USB `-device usb-storage`）。SSH で `install-admin` を直に流す（GUI 無し） |
| QEMU の GUI | AAT（`plan/tools/aat/`、`aat-input`・`keiland-shot`）で S1〜S11 を操作。installer の READY・画面・control の log で操作する |
| 導入先の起動 | 導入先の disk の file だけで `plan/tools/boot-test.sh`（`BOOT_MODE=uefi-nvme`）の login prompt。USB の image を付けたままの起動でも root が導入先になる（PARTUUID） |
| 共存 | 前もって「他の OS」の GPT（ESP に `\EFI\BOOT\BOOTX64.EFI` と別の file、Basic Data の partition に file）を作った disk に「partition を選ぶ」で入れ、触らない partition の byte の hash が変わらないこと、既存の fallback の loader が残ること |
| 失敗 | 容量の不足、展開の途中のキャンセル、SHA-256 の不一致（壊した unit）、導入先の自動 mount が外れない。S10 の表示と、導入先から起動しないこと |
| 実機 | Q1 が割り当てる。導入先は空の USB の disk か空の NVMe（ユーザーの判断）。ユーザーの試験の一覧に置く |

- boot-test.sh の UEFI の mode は disk を 1 台しか取らない（`plan/tools/boot-test.sh:316-335`）ので、導入の試験は guest.py の `--qemu-extra` を使う 1 本の script を `plan/ws307/tests/` に作る（配置 → 起動 → 導入 → 回収 → 導入先だけの起動 → 要約）。

## 13. 範囲外（Future の候補）

- partition の大きさの変更・移動、既存の partition の format の型の変更、MBR から GPT への変換（→ U10）。
- network からの導入、package の後からの追加・削除・更新（§6.4 の記録だけを残す）。
- 4Kn の disk（mkfs の device の mode が 512 byte だけ）。
- 同じ ESP に 2 つ目の Kei を入れる（既に `zedbsd.cfg` がある ESP は使わない）。
- 暗号化、RAID、LVM 相当。
- BIOS・他の arch。

## 14. 原文との対応

| 原文の項目 | この文書 |
| --- | --- |
| グラフィカル、Keiland のアプリ、UEFI＋ACPI の PC だけ | §1・§3 |
| 導入先は NVMe・USB Mass Storage・SATA など | §1・§4.2 |
| ウィザード形式、次へで進む | §4 共通 |
| Welcome | §4.1 |
| disk の選択 | §4.2・§5.1 |
| partition の扱い（全体／選ぶ） | §4.3・§5.7 |
| 簡易の partition の編集（一覧・削除・作成は大きさだけ・未使用・OS の ID 無し・ESP の自動作成と失敗・次へで ESP の検査） | §4.4・§5.3・§5.4 |
| rootfs の選択（UFS の印・mkfs） | §4.5 |
| 「ィションがないときは次の画面に進めない。」 | §4.5（→ U11） |
| swap の選択（swap の印・mkswap・選ばなくてよいが確認・無ければ UFS の上の swapfile） | §4.6・§5.5 |
| 入れる項目（Base（header 込み）・Desktop・Firmware・Packages と 7 つの下位・依存の自動選択） | §4.7・§6.3 |
| 最終確認 | §4.8 |
| progress | §4.9・§7 |
| 失敗（キャンセル・失敗の時も） | §4.10・§8 |
| 終了（再起動、終えて desktop を使い続ける） | §4.11 |
| base・desktop・firmware は tarball で `/opt/installer`、packages は package ごとの tarball、選択＋依存を入れる | §6 |

## 15. ユーザーの確認事項

2026-10-28 に Q1 へ送った（第 1 回 U1〜U8、第 2 回 U9〜U13、U14 は U2 の回答から）。回答の原文（ユーザーのクリックで選んだ選択肢の文、Q1 の中継）をここに引用し、本文を直す。

| # | 問い | 推し | 回答（原文） |
| --- | --- | --- | --- |
| U1 | installer の載る媒体と起動 | A: menuconfig の選択、配布用の config で y、起動で自動で開く | 2026-10-28「menuconfig で選び自動で開く (推し)」→ A |
| U2 | partition の編集を disk に書く時期 | A: 最終確認の後にまとめて | 2026-10-28「ボタンでその場で書く」→ B（原文どおり、S4 の button でその場で書く。キャンセルしても書いた分は残る）。§3・§4.4・§7.1・§8・§9 に反映 |
| U3 | 作成した「未使用」の partition の型 | A: zedBSD 独自の GUID | 2026-10-28「zedBSD 独自の未割り当ての型 (推し)」→ A |
| U4 | disk 全体の配置 | A: ESP 512 MiB（kernel も ESP）・swap partition・root | 2026-10-28「ESP 512M＋swap 区画＋root (推し)」→ A |
| U5 | 既存の ESP を共有する時の loader と boot entry | A: fallback が無ければそこ、有れば `\EFI\kei\`、Boot#### は Future | 2026-10-28「\EFI\BOOT\BOOTX64.EFIがなければそこに置く。あれば、上書きせず、\EFI\ZEDBSD\BOOTX64.EFIに置いて、完了の画面で、Frimware のboot menuで選ぶ、と出す。Boot####の作成もWSに入れる。」→ §5.4・§5.8・§4.11 |
| U6 | root の権限 | A: setuid の helper を installer が直に起動、最終確認で password | 2026-10-28「setuid の helper、自分で起動 (推し)」→ A（password の時期は U2 に合わせ §9） |
| U7 | 利用者のアカウント | B: 最終確認の前に利用者の画面 | 2026-10-28「「利用者」の画面を足す (推し)」→ B、§4.7a |
| U8 | 入れる項目の必須と既定 | A: Base 必須、Desktop・Firmware 既定で選択、Packages 無選択 | 2026-10-28「Base必須、DesktopとFirmwareは規定で、外そうとすると警告、PackagesはOpenSSL, OpenSSH, ca-certificates, Fontsのみ規定。Pythonのパッケージも追記しておいてください。」→ §4.7 |
| U9 | disk の一覧の型番 | B: kernel に model を足す | 未回答 |
| U10 | GPT でない disk で「partition を選ぶ」 | A: 空は新しい GPT、MBR は全体だけ | 未回答 |
| U11 | 「ィションがないときは…」の行 | A: rootfs を選ばないと進めない | 未回答 |
| U12 | rootfs・swap に選べる partition | A: この installer で作った物だけ | 未回答 |
| U13 | package の header | A: 今の package のまま | 未回答 |
| U15 | （U5 の回答から）作った Boot#### の BootOrder の位置 | A: 先頭に入れる（再起動で導入先から起動）。B: 末尾に足す（firmware の menu に「Kei」で出るだけ） | 未回答 |
| U14 | （U2 の回答から）S5・S6 の root・swap の型の印と mkfs・mkswap の時期 | A: 最終確認の後（progress の中）。B: S5・S6 の次へでその場で | 未回答 |

## 16. 担当が決めた技術の判断（理由）

| # | 判断 | 理由 |
| --- | --- | --- |
| T1 | S10 に来るのは S9 の間のキャンセル・失敗、S4 で書いた後のキャンセル、S4 で先へ進めない失敗、probe 以降の失敗。S4 で何も書いていない時のキャンセルは閉じるだけ | 何も書いていなければ「失敗の状態」を示す物が無い。U2（その場で書く）で書いた後は、何が残ったかを S10 で示す。原文「progressだけでなく、キャンセルや失敗時にここに飛ぶ」 |
| T2 | 「loopback swapfile」を UFS の上の file を extent で使う今の方式で満たす | kernel は file の swap を loop device を通さず extent で直に扱う（`src/kern/vm/swap-source.c:145-200`）。loop を足す利点が無い |
| T3 | tarball は ustar＋deflate（gzip の形） | guest に zstd・xz が無く、deflate は base の `libz-compat` にある。外部の圧縮の library を base に入れない（設計方針 §2.1） |
| T4 | root・swap・boot0 は PARTUUID で選ぶ | PARTLABEL・FAT の serial は live の USB と重なりうる（`block-identity.c:184-190`、`fat32-format.c:156-158`） |
| T5 | loader と `zedbsd.cfg` を最後に書く | 途中で止まった導入先が起動できるように見えない（§8） |
| T6 | helper は diskpart・mkfs・mkswap を子として起動し、確認の phrase を送る | 文書の決めた使い方（`docs/reference/block-administration.md` の diskpart init、image-formatters の block-device modes）。format の code を二重に持たない |
| T7 | 4Kn の disk は選べない（灰色と理由） | mkfs の device の mode が 512 byte だけ。拡張は Future |
| T8 | 既存の ESP の空きは helper が読み取り専用で mount して量る | installer は device を開かない（§3）。FAT の空きは fs を読まないと分からない |
| T9 | 中止は stdin の `cancel` の行 | set-user-ID の process への signal の可否に頼らない。EOF も中止 |
| T10 | 展開の元は build の unit で、live の root を写さない | live の system の状態（log・設定・利用者の file）を導入先に持ち込まない。旧 zedinst も live の upper を写さなかった |
| T12 | 書き込みの password は 1 回の installer の実行に 1 回（helper の session） | U2 で S4 の操作ごとに書くので、操作ごとの password は使いにくい。session は installer の終わり（stdin の EOF）で終わる |
| T13 | 「OpenSSL（＋CA 証明書）」は 1 つの項目のまま、既定で選択 | 原文が 1 項目。U8 の回答の既定の名前（OpenSSL・ca-certificates）はどちらも選択になり食い違わない |
| T11 | 日本語の catalog も作る | Keiland の app は `kl_tr` で翻訳する（WS158）。install の画面だけ英語にする理由が無い |

## 17. 未決・残り

- U1〜U13 の回答の反映。
- design-reviewer の指摘の反映（[phase001](phase001/phase.md) の手順 4）。
- 実装の Phase の表（[ws.md](ws.md)）。
