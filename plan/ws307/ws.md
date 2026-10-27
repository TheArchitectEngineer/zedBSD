<!-- awesome-plan project=zedbsd record=ws307 -->

# WS307 Keiland のアプリとしての zedBSD のグラフィカルなインストーラ

- Status: planning（2026-10-28 新設。p001 で仕様を確定中、q1643 P4）
- 仕様: [spec.md](spec.md)（案。ユーザーの確認事項 U1〜U13 の回答待ち）。
- Resume point: p001 の手順 3〜5（回答の反映、design-reviewer の指摘の反映、実装の Phase の表の確定）。
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

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 仕様の確定（調査、spec.md、ユーザーの確認事項、design-reviewer、実装の Phase の案） | — | in-progress（q1643、P4） |

### 実装の Phase の案（p001 の成果、[spec.md](spec.md) による。ユーザーの回答で変わりうる。Phase の directory はまだ作らない）

| Phase | 内容 | 依存 | 見積もり | 状態 |
| --- | --- | --- | --- | --- |
| p002 | base の道具: `mkswap` の partition の mode、`diskpart` の batch の mode（spec §11.3）。host の試験と QEMU の短い確認 | p001 | 6 LW | planning |
| p003 | （U9 が B の時）kernel: NVMe・AHCI・USB の disk の model・serial を UAPI に出す（HAL の API は変えない） | p001 | 6 LW | planning |
| p004 | build: unit の振り分け・tarball（ustar＋deflate、所有者・mode は image の manifest から）・index と依存・`/opt/installer`・`ZEDBSD_INSTALLER` の config・漏れと重複の検査（spec §6・§11） | p001 | 10 LW | planning |
| p005 | helper `install-admin`: probe・install（計画の再検査、partition・format・展開・設定・公開、進みと中止の protocol）。host の試験と QEMU の SSH での導入（NVMe・SATA・USB の導入先、導入先だけの起動）（spec §5・§7〜§9） | p002・p004（p003 は任意） | 14 LW | planning |
| p006 | installer の app: S1〜S11（と U7 の利用者の画面）、計画の組み立てと検査、翻訳（ja）、App Home、自動起動、AAT の log（spec §4・§10） | p005 の protocol | 14 LW | planning |
| p007 | 通しの試験: AAT で GUI の導入（全体・選ぶ・共存・swap 無し・キャンセル・失敗）、導入先の起動、`plan/ws307/tests/` の 1 本の script（spec §12） | p006 | 8 LW | planning |
| p008 | 文書: `docs/howto/boot-and-storage.md` の古い zedinst の節を新しい installer に置き換え、`docs/reference` に install-admin・mkswap・diskpart の追加 | p007 | 3 LW | planning |
| p009 | 規約の全文の見直し（WS307 が変えた C） | p008 | 4 LW | planning |
| p010 | 実機の導入（Q1 の割り当て、ユーザーの試験の一覧） | p007 | 2 LW | planning |

- 2026-10-28 ユーザー「P4を立てて、WS307の仕様検討を行ってもらってください。ユーザ確認事項があれば回答させていただきます。仕様を確定させたいです。そのためのphaseをQ1が作り、P4に実行させてください。」
