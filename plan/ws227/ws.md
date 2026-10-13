<!-- awesome-plan project=zedbsd record=ws227 -->

# WS227: menuconfig の Base・Desktop の表示名の整理、画像と PDF の library を Desktop へ、Linux・FreeBSD の build に zedBSD 独自の base の命令

<!-- awesome-plan-current:start -->
Status: completed（2026-10-14 Q1。結果は p001。制限: FreeBSD は make -n だけ、config/ci/config-intelmac.mk には使わない 4 つの library が残る）
Primary Milestone: MG002
Related Milestones: なし
Objectives: O1
Parent: [Master](../master.md)
Queue: q970（finished）
Target: ベータ2
Resume point: p001 の Q1 の判定と ※要確認の 3 件（phase001/phase.md の「Linux・FreeBSD」）。
<!-- awesome-plan-current:end -->

## 原文（2026-10-14 ユーザー）

「make menuconfigのBaseで、プリグラムは基本的にコマンド名、ライブラリはlib*やlib*-compat, terminal definitionsはlibcurses * terminal definitions, daemonは*d,に表示を修正します。…また、Linux/FreeBSD向けのビルドでは。baseのこれらも、/opt/keiland/bin/や/opt/keiland/sbin/に入れます。passkeyコマンドのようなbaseのものも、Linux/FreeBSDではビルドします。」
（Q1 が「base の program で libpng・libpdf などを使う物は無い」と報告した後）「libpng-compat, libjpeg-compat, libtruetype, libpdf, これらをすべて Desktop/ に移しても、baseのビルドには影響がないと言うことですね。じゃあそっちがいいなあ。」
「ここだけ変更します。menuconfigのDesktopで、プログラムはコマンド名＋説明。ライブラリはlib*やlib*-compat。libpng-compat, libgif-compat, libjepg-compat, libpdfはDesktopに移します。menuconfigの表示だけでなく、srcも。」
「menuconfigのbaseでは、プログラム名かライブラリ名でOKです。説明はいらないです。」
「Linux・FreeBSD で /opt/keiland/bin・sbin に入れる「base のもの」は、zedBSD 独自のコマンド（passkey など）だけで、ls や cat のように host にもあるコマンドは入れない、という理解でよいです。」

## 目標

1. menuconfig の Base の表示は名前だけで説明を付けない: program は命令の名前（例 `bt`、`emacs`、`fidoctl`、`noct`）、library は `lib*`・`lib*-compat`（例 `curses` → `libcurses`）、terminal の定義は `libcurses major terminal definitions`・`libcurses extra terminal definitions`、daemon は `*d`（例 `audiod`、`bluetoothd`、`volumed`）。
2. menuconfig の Desktop の表示: program は「命令の名前＋説明」、library は `lib*`・`lib*-compat`。
3. libpng-compat・libgif-compat・libjpeg-compat・libpdf の source を `userland/base/` から `userland/desktop/` へ移し、menu も Desktop に。libtruetype（source は既に desktop）の menu も Desktop に。libz-compat は base に残す（ユーザーの列挙に無い）。build の規則（`platform/*/vmunix.mk`・`keiland-linux.mk`・`keiland-freebsd.mk`・Makefile.linux/freebsd）、試験・docs の path を追従。
4. Linux・FreeBSD の build: zedBSD 独自の base の命令（passkey など、host に同じ物が無い物）を build して `/opt/keiland/bin`・`/opt/keiland/sbin` に入れる。ls・cat など host にある命令は入れない。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 表示名・移動・Linux/FreeBSD の build の全部（小さいので 1 つ） | cleared（2026-10-14） | — |
