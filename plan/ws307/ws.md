<!-- awesome-plan project=zedbsd record=ws307 -->

# WS307 Keiland のアプリとしての zedBSD のグラフィカルなインストーラ

- Status: planning（2026-10-28 新設。p001 で仕様を確定中、q1643 P4）
- 仕様: [spec.md](spec.md)（確定。ユーザーの確認事項 U1〜U29 は全て回答済み。design-reviewer の 1 回目・2 回目の指摘を反映済み）。
- Resume point: p001 は受け入れを満たした（2026-10-28、P4。Q1 の cleared の判断待ち）。次は実装の p002（OS 名の統一）か p003（build と installer-image）。p003・p004 は toolchain の許可、p007 は HAL の承認が要る。
- Master: [plan/master.md](../master.md)。Primary Milestone: MG003。Related: MG006・MG007。
- 前身: [WS119](../ws119/ws.md)（インストーラの作り直し、2026-10-28 にこの WS の新設で canceled）。参考: [WS028](../ws028/ws.md)（canceled）、[WS019](../ws019/ws.md)・[WS062](../ws062/ws.md)（completed、disk image の構成）。

## ユーザーの指示と希望の仕様（原文、2026-10-28）

指示:「WSを新しく作成します。Keilandアプリとして、zedBSDのインストーラを新たに作成します。既存のインストーラ作成WSがあれば、それらはキャンセルを理由にcloseしておいてください。」「下記の仕様が私の希望なのですが、ブラッシュアップしたいです。まずは記録しておいてください。」

```
・グラフィカルなインストーラをKeilandアプリで作成する。インストーラはUEFI+ACPIなPC用で、他のプラットフォームは考慮しない。
・インストール先はNVMe, USB Mass Storage, SATA HDDなどのサポートされたディスクです。
・ウィザード形式で、次へで進める形式。画面は下記にします。
　・Welcome画面。
　・インストール先のディスク選択の画面。
　・ディスクのパーティションの扱いを選択する画面。ディスク全体を使う、パーティションを選ぶ、のどちらか。
　・パーティションを選ぶ場合に進む、簡易的なパーティション編集画面。パーティション一覧が表示される。選択されたパーティションを削除するボタンがある。パーティションを作成するボタンがある。パーティションの作成は、サイズのみ聞かれる。パーティションは未使用でマークされ、OSのIDはない状態で作成される。パーティションを作成するとき、EFIパーティションがなければ、自動で作成する。EFIを作成できなければパーティション作成も失敗する。次の画面に進むとき、EFIパーティションが存在することをバリデーションする。十分なサイズのEFIパーティションがないときは次の画面に進めない。
　・パーティションを選ぶ場合に、簡易的なパーティション編集画面のあとに表示される、rootfsパーティション選択画面。選択されたパーティションをUFSとマークして、mkfsして初期化する。
ィションがないときは次の画面に進めない。
　・パーティションを選ぶ場合に、rootfsパーティション選択画面のあとに表示される、swapパーティションの選択画面。選択されたパーティションをswapとマークして、mkswapして初期化する。swapパーティションは選択しなくてもいいが、確認はされる。swapパーティションがない場合、UFS上にloopback swapfileが作成される。
　・ここで、ディスク全体を使う、パーティションを選ぶ、の両方が合流する画面。インストールする項目の選択画面。選択項目は、Base System (ヘッダファイル込み）、Keiland Desktop、Firmware、Packages。Packagesには下位項目があり、Clang, OpenSSH, OpenSSL (+cacertificate), GTK4, Fonts, libavcodec, curlがある。依存ライブラリは自動選択で、libc++やcairoなども依存で入る。
　・最終確認の画面。
　・インストールのprogressの画面。
　・インストール失敗の画面。progressだけでなく、キャンセルや失敗時にここに飛ぶ。
　・インストール終了の画面。再起動ボタンがある。そのまま終了してデスクトップを使い続けられる。

base, desktop, firmwareはtarballにしておいて、/opt/installer以下に置く。packagesはuserland/packages/のパッケージ単位でtarballにしておき、選択項目＋依存関係のあるものをインストールする。
```

（原文の「ィションがないときは次の画面に進めない。」の行は、貼り付けの時の重複の断片と見られる。意味はユーザーのブラッシュアップの時に確かめる。）

## 目標（希望の仕様からの要約、Q1。正は上の原文）

UEFI＋ACPI の PC 向けに、Keiland のアプリとして動くウィザード形式のグラフィカルなインストーラを作る。対応する disk（NVMe・USB Mass Storage・SATA など）に、disk 全体か選んだ partition へ zedBSD を入れる。入れる物は base（header 込み）・Keiland Desktop・Firmware・Packages（clang・OpenSSH・OpenSSL＋CA 証明書・GTK4・Fonts・libavcodec・curl と依存）。base・desktop・firmware は tarball で `/opt/installer` の下、packages は `userland/packages/` の package ごとの tarball。

## 画面の流れ（原文の順）

1. Welcome
2. インストール先の disk の選択
3. partition の扱い: disk 全体を使う／partition を選ぶ
4. （選ぶ時）簡易の partition の編集: 一覧・削除・作成（大きさだけ、未使用の印・OS の ID 無し）。ESP が無ければ作成の時に自動で作る（作れなければ作成も失敗）。次へ進む時に十分な大きさの ESP があるかを検査
5. （選ぶ時）rootfs の partition の選択: UFS の印を付け mkfs
6. （選ぶ時）swap の partition の選択: swap の印を付け mkswap。選ばなくてもよいが確認する。無ければ UFS の上に loopback の swapfile
7. 入れる項目の選択（3 の両方の道が合流）
8. 最終確認
9. progress
10. 失敗（キャンセル・失敗の時もここ）
11. 終了: 再起動のボタン、または終えて desktop を使い続ける

## ブラッシュアップで決める点（Q1 の気付き、未決。ユーザーと詰める）

- 「disk 全体を使う」の時の partition の配置（ESP・root・swap の大きさ）と、既存の data の消去の確認。
- ESP の「十分な大きさ」の値、既存の ESP を共有する時（他の OS と共存）の loader の置き方と UEFI の boot entry の作り方。
- インストーラ自身の起動の形（live の USB の image か、インストール済みの desktop から起動するのか）と、`/opt/installer` の tarball の作り方（build の target、署名・検査）。
- package の依存の解決の元（userland/packages の Makefile の依存か、別の manifest か）と、入れた後の package の管理（WS153 の Apps の頁との関係）。
- 権限（インストーラは root の操作が要る: sessiond・passkey との関係）、ロケール（WS158 の翻訳）。
- 試験の方法（QEMU の OVMF＋NVMe・USB・SATA、実機）。

## ユーザーの決定（p001 の確認事項の回答、Q1 の中継。問いと選択肢は [spec.md](spec.md) §15）

- 2026-10-28 U1「menuconfig で選び自動で開く (推し)」: installer と `/opt/installer` は menuconfig の選択（配布用の config で y）。その image で起動すると desktop で installer が自動で開き、閉じても desktop を使える。
- 2026-10-28 U2「ボタンでその場で書く」: partition の編集の画面の削除・作成は、押した時にその場で disk に書く（キャンセルしても書いた分は残る）。
- 2026-10-28 U3「zedBSD 独自の未割り当ての型 (推し)」: 作成した partition は zedBSD 独自の「未割り当て」の型 GUID で書く。
- 2026-10-28 U4「ESP 512M＋swap 区画＋root (推し)」: disk 全体を使う時は ESP 512 MiB（loader・kernel・設定を ESP に）・swap の partition・残りを root。
- 2026-10-28 U5「\EFI\BOOT\BOOTX64.EFIがなければそこに置く。あれば、上書きせず、\EFI\ZEDBSD\BOOTX64.EFIに置いて、完了の画面で、Frimware のboot menuで選ぶ、と出す。Boot####の作成もWSに入れる。」: UEFI の runtime services（SetVariable）と Boot#### の作成も WS307 の範囲。HAL の API に触る場合は、その差分の案を Phase に置き、別にユーザーの承認を得る（Q1 の補い）。
- 2026-10-28 U6「setuid の helper、自分で起動 (推し)」: 新しい setuid root の helper を installer が自分で起動する（compositor を通さない）。
- 2026-10-28 U7「「利用者」の画面を足す (推し)」: 最終確認の前に利用者の画面（利用者の名前・password、root の password、自動 login）。
- 2026-10-28 U8「Base必須、DesktopとFirmwareは規定で、外そうとすると警告、PackagesはOpenSSL, OpenSSH, ca-certificates, Fontsのみ規定。Pythonのパッケージも追記しておいてください。」: Packages の下位に Python を足す。
- 2026-10-28 U9「今の口で出せる物だけ」: disk の一覧に型番を出さない（kernel の作業を入れない）。
- 2026-10-28 U10「GPT の disk だけ」: 空・MBR の disk は「disk 全体を使う」だけ。
- 2026-10-28 U11「rootfs 未選択なら進めない (推し)」。
- 2026-10-28 U12「ESP 以外の既存も選べる」: rootfs・swap に ESP 以外の既存の partition も選べる（選んだ時に「中身を消す」確認）。
- 2026-10-28 U13「WS307 で header も入れる」: GTK4・cairo・pango・glib・libavcodec・libc++ などの package に header と .pc を入れ、入れた system で GTK4 の app を build できるようにする。
- 2026-10-28 U14「最終確認の後 (推し)」: root・swap の型の印と mkfs・mkswap は最終確認の後。
- 2026-10-28 U15「末尾に足します。また、KeiではなくzedBSDです。Keiの名前はプロジェクトから削除予定です。」: Boot#### は BootOrder の末尾、名前は「zedBSD」。spec と UI の文言で「Kei」の名を使わない。
- 2026-10-28 U4b「U4 のまま ESP に全部」: WS238 の決定（今の image は ESP に loader だけ、kernel・設定・logo は ZEDBSD の FAT32）との違いを示した上で、導入先は ESP 512 MiB に全部。導入先は今の image の形と違う。
- 2026-10-28 U16「その disk では無効にする (推し)」: 既存の ESP が小さい・使えない disk では「partition を選ぶ」を無効にする。
- 2026-10-28 U17「WS307 で 4096 byte に対応」: mkfs（FAT32・UFS）・mkswap・diskpart（と要れば kernel の FAT・UFS・swap）の 4096 byte の sector の対応を WS307 に含める。
- 2026-10-28 U18「一覧を出し同意で入れる (推し)」: Firmware の license の一覧を S7 で出し、同意しないと入れない。
- 2026-10-28 追加の指示「P4に、ESPにカーネルをインストールすると回答してしまいましたが、ブートローダがUFSからカーネルをロードできるようにしたいです。そのphaseを作ってほしいです。」: loader が root の UFS から kernel を読む Phase（p007）を足す。導入先の kernel は UFS の上（`/boot/vmunix`）、ESP は loader（と cfg）。U4・U4b の「ESP に全部」は kernel を除いた形に読み替える（ESP 512 MiB は U4 のまま、Q1 の解釈）。
- 2026-10-28 U19「ESP（FAT）のまま (推し)」: `zedbsd.cfg` は ESP。loader の探し方を変えない。
- 2026-10-28 U20「root の UFS（/boot/logo.ppm） (推し)」: 起動の logo は root の UFS。
- 2026-10-28 U21「image は今のまま (推し)」: 今の disk image（hdd-image・installer の image）は WS238 の形のまま。新しい形は導入先だけ。
- 2026-10-28 指示「P4に伝えて下さい。make menuconfigは、インストーラ作成の場合、カーネルのコンフィグには使いますが、make installer-imageでは、すべてのパッケージをビルドしますし、イメージの形式（kernelのパーティション）が異なります。」: installer の image は専用の target `make installer-image`。menuconfig は kernel の config にだけ使い、userland・package は全部を build して unit にする。image の形は `hdd-image.img` と違う。U1 の「menuconfig で選ぶ」はこの形に読み替える（自動で開くのは残る）。U21 の「image は今のまま」は `hdd-image.img` の事（Q1 の解釈）。
- 2026-10-28 U22「9 項目＋「その他」で個別 (推し)」: 全 package を unit にし、S7 は 9 項目と「その他」の下位に他の package を個別に。
- 2026-10-28 U23「インストーライメージでは、ESPにブートローダ、BOOT FAT32にカーネルとzedbsd.cfg、rootfs, swapのパーティションを置きます。」: installer の image は ESP（loader）＋BOOT の FAT32（kernel・cfg）＋root＋swap。
- 2026-10-28 U24「FW は driver の選択、tests・X11 無し (推し)」: Firmware は kernel の driver が選ぶ firmware。tests・X11 の package は入れない。
- 2026-10-28 U25「今の hdd-image と同じ (推し)」: installer の image の BOOT の FAT32 は Basic Data の型・label「ZEDBSD」、logo.ppm もその FAT32。
- 2026-10-28 U26「すべてのイメージで、ユーザ名をuser, パスワードをuserに変更します。インストーラはsetuidのヘルパーを使うので、パスワード認証を不要にします。これは特別なインストーライメージのみです。」: 全 image の既定の利用者を user/user に（p002）。installer の image では helper は password を聞かない（呼べる者の制限は spec §9.1）。
- 2026-10-28 U27「今後keiの名前は削除しますので、zedbsdにします。」: 導入先の hostname の既定は zedbsd（S7a の欄は技術の判断で足す）。base の既定の hostname も zedbsd に（p002）。
- 2026-10-28 指示「P4に伝えてください。起動ロゴ画像を含めて、KeiというOS名を削除して、zedBSDに統一します。インストーラだけでなくすべてに影響します。そういうphaseをこのWSに追加してください。」: p002 を足した（spec §13a）。git mv・大量の rename は Phase の中で Q1 に一覧を出してから（Q1 の補い）。
- 2026-10-28 U28「Keilandの名前はそのままです。libkeilandもそのままです。KeiというOS名は、予定ではzedBSDに変更ですが、OSディストリビューション名が変更になる可能性があります。zedBSDはカーネル名です。OS名にするかは未定ですが、現状ではOS名もzedBSDに仮決めしておきます。libkeiland以外にlibkei*があるなら、変更を検討します。」: Keiland・libkeiland と内部名は残す。OS 名は zedBSD に仮決めし、1 か所（`ZEDBSD_OS_NAME` → os-release）で定義する。libkeiland の系の外の libkei* は無い（旧名 libkeiui は統合済み、spec §13a）。
- 2026-10-28 U29「推し」: 担当が今の logo と同じ様式で「zedBSD」の文字の画像を作り、PNG を Q1 経由でユーザーに見せてから入れる。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 仕様の確定（調査、spec.md、ユーザーの確認事項、design-reviewer、実装の Phase の案） | — | in-progress（q1643、P4。受け入れを満たした、Q1 の確認待ち） |

### 実装の Phase の案（p001 の成果、[spec.md](spec.md) による。ユーザーの回答で変わりうる。Phase の directory はまだ作らない）

| Phase | 内容 | 依存 | 見積もり | 状態 |
| --- | --- | --- | --- | --- |
| p002 | OS 名「Kei」を削除して zedBSD に統一する（tree 全体、起動の logo の画像を含む。既定の利用者 user/user（U26）と hostname zedbsd（U27）も）。棚卸し → 一覧を Q1 に出して git mv・rename の所有と衝突を調整 → 変更 → 試験（spec §13a） | p001 | 12 LW | planning |
| p003 | build: `make installer-image`（menuconfig は kernel の config だけ、userland・package は決まった選択で全部）、file の manifest（持ち主の package・mode）、重なりの規則、unit の tarball（ustar＋deflate、所有者 0:0、mode の正規化、set-user-ID の許可の一覧）、index（SHA-256・file の数・依存・項目・firmware の license）、manifest から mode を正規化した live の tree、installer の image（ESP＋ZEDBSD の FAT32＋root＋swap、build ごとの PARTUUID）、漏れと重複の検査（spec §6・§11.2）。sysroot の持ち主の付け方は toolchain（`toolchain/llvm/sysroot.mk`）に触れるので Q1 の許可が要る | p001 | 14 LW | planning |
| p004 | package の header と `.pc`（gtk4・cairo・pango・glib・libavcodec・libcxx など）と pkgconf の package。導入先で GTK4 の app を build する確認（spec §6.2、U13）。`lang/clang`・`devel/libcxx` は toolchain なので Q1 の許可が要る | p003 | 8 LW | planning |
| p005 | base の道具: `mkswap` の partition の mode（ZEDSWAP2 64 bit）、`diskpart` の batch（add・delete、型・名前・PARTUUID・attribute の変更）と「未割り当て」の型 GUID、`mkfs -t fat32` の乱数の serial、UFS の inode の密度、道具の側の 4096 byte の sector（mkfs の FAT32・UFS の fragment 4096 の profile、mkswap、diskpart）（spec §5.9・§11.3）。host の試験と QEMU の短い確認 | p001 | 12 LW | planning |
| p006 | kernel の 4096 byte の sector: FAT・UFS の driver、swap の partition、swap file の extent、fs の識別（spec §5.9、U17。`fat.c:911`・`ufs.c:60`・`swap-source.c:246`・`file.c:2920`）。QEMU の 4096 の NVMe・USB で mount・swap・起動 | p005 | 10 LW | planning |
| p007 | UEFI の runtime services と Boot####（spec §5.8、U5・U15）。**最初の段で HAL の差分の案（handoff の名前の追加、runtime の呼び出しの関数）を plan に置き、ユーザーの承認を待つ**。承認の後: loader の handoff、HAL、`/dev/system` の UAPI（形・版・上限を先に決める）、`efiboot`。QEMU の OVMF で Boot#### を書いて読み戻す | p001 | 12 LW（承認待ちを除く） | planning |
| p008 | boot loader が UFS から kernel を読む（spec §5.10）: file の源の抽象、`bootloader/common/` の UFS の reader、cfg の `rootpart=PARTUUID=` の照合、`kernel=rootpart:/boot/vmunix`・`logo=rootpart:…`、失敗は画面に止める。host の試験と QEMU の OVMF の起動（512、p006 の後に 4096）。HAL の API・handoff は変えない | p001（4096 は p006） | 12 LW | planning |
| p009 | helper `install-admin`: probe・session（edit・install、lock、disk への結び付け、S8 での password の聞き直し）、4 つ組の照合、signal と `setresuid`、index の hash（同じ fd）、展開（安全な path、nosuid の mount）、利用者（kei の置き換え）・hostname・sshd の設定、kernel と logo を root の /boot に、ESP の公開（loader・cfg）、Boot####（p007 の承認の後）。host の敵対的な計画の試験と、QEMU の SSH での導入（NVMe・SATA・USB、導入先だけの起動）（spec §5・§7〜§9） | p003・p005・p008（4096 は p006、Boot#### は p007） | 18 LW | planning |
| p010 | installer の app: S1〜S11 と S7a、検査、確認の dialog、選択の無効化、翻訳（ja）、App Home、自動起動、AAT の log（spec §4・§10・§11.2） | p009 の protocol | 14 LW | planning |
| p011 | 通しの試験: installer の image、AAT で GUI の導入（全体・選ぶ・共存・既存の partition を root に・swap 無し・キャンセル・失敗・拒む disk・dd した disk との同居・4096 の disk）、導入先の起動、`plan/ws307/tests/` の 1 本の script（spec §12）。p007 の承認が無い間は Boot#### の確認を「未実施（承認待ち）」と記録 | p002・p004・p010（4096 は p006） | 12 LW | planning |
| p012 | 実機の導入（Q1 の割り当て、ユーザーの試験の一覧） | p011 | 2 LW | planning |
| p013 | 文書: `docs/howto/boot-and-storage.md` の古い zedinst の節を新しい installer に置き換え、`docs/reference` に install-admin・efiboot・mkswap・diskpart・mkfs・loader の UFS の追加 | p012 | 3 LW | planning |
| p014 | 規約の全文の見直し（WS307 が変えた C）。WS の最後 | p013 | 5 LW | planning |

- 2026-10-28 ユーザー「P4を立てて、WS307の仕様検討を行ってもらってください。ユーザ確認事項があれば回答させていただきます。仕様を確定させたいです。そのためのphaseをQ1が作り、P4に実行させてください。」
