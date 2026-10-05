<!-- awesome-plan project=zedbsd record=ws168 -->
# WS168: プレビュー（縮小表示）を作る隔離された専用の command

Status: planning（2026-10-05 追加、**ベータ2**（2026-10-05 ユーザー）、見積もり 3 LW。p001 の設計の第 2 版あり、段の案は p001 の §9）
Master: [master](../master.md)
Primary Milestone: MG006

## 由来

ユーザー（2026-10-05）「プレビューの作成を行う専用コマンドを作成。このコマンドはchrootと、そのほかの可能な限りのjailを適用して実行する。最低でもchrootで隔離されたディレクトリで実行され、画像やPDFなどのファイルに埋め込まれた攻撃でRCEされたとしても、乗っ取られる権限を最小にする。入力ファイルと出力ファイルはfd 0,1でオープンされた状態で起動し、出力は規定の場所にファイル保存される。このプロセスはそのほかのファイルをオープンできない。ネットワークも利用できない。forkも不能。純粋にfd 0,1の入出力と計算のみ行える。」

## 目標（ユーザーの要件）

1. 縮小表示（画像・PDF ほか）を作る専用の command（今は Files・Settings・desktop の中で decode している）。
2. 起動の時に入力の file を fd 0、出力の file を fd 1 で開いた状態で渡す。出力は規定の場所に保存される（呼び出し側が開いた file）。
3. 最低でも chroot（空の、または最小の directory）で隔離して走らせる。そのほか可能な限りの jail を当てる。
4. この process は他の file を open できない、network を使えない、fork・exec できない。fd 0・1 の読み書きと計算（と exit、memory の確保）だけができる。
5. file に埋め込まれた攻撃で RCE されても、乗っ取られる権限を最小にする。

## 設計で決めること（p001）

- kernel の仕組み: 今の kernel は chroot（`src/kern/namei.c` の `fs_chroot`）を持つが、capability mode（FreeBSD の Capsicum の `cap_enter`、Linux の seccomp の strict、OpenBSD の pledge("stdio") に当たる物）は無い。要件 4 を満たす「新しい open・socket・fork・exec を全部断る」process の mode を kernel に足す設計（UAPI の追加、HAL は不変）。mmap（匿名の memory の確保）と exit と既存の fd の read・write・close・fstat だけを許す。
- 権限: 呼び出し側の uid のまま走らせるか、専用の権限の無い uid に落とすか（setuid の helper か sessiond の仲介か）。chroot は root の権限が要るので、その順序（chroot → uid を落とす → capability mode）。
- decoder: libpng・libjpeg・libpdf（PDF）を静的 link にして、chroot の中で dynamic の library を読まなくて済むようにする。
- 呼び出し側（Files・Settings・compositor）の変更: 縮小表示をこの command に任せ、時間の上限と memory の上限（rlimit）、失敗の扱い。
- 試験: 正常な file・壊れた file・open や socket や fork を試みる試験の program が全部断られること。

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws168-p001](phase001/phase.md) | 要件と設計（kernel の capability mode の UAPI の案を含む） | planning（設計の第 2 版（子を sandbox の中に起こす `sandbox_spawn`）、2026-10-05 P1。ユーザーの review と §8 の H1〜H7 の判断待ち） | — |
