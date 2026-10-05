<!-- awesome-plan project=zedbsd record=ws145-p001 -->

# ws145-p001: 印刷の調査と設計

Status: in-progress（第 2 版と 2 回目の敵対的レビューまで。2026-10-05 夕、T1 の FAIL の対応のため区切った）
Disposition: normal
Parent: [WS145](../ws.md)
Queue: q754（Q1、2026-10-05、P2 g15）
依存: なし

## 範囲

[ws.md](../ws.md) の範囲 1〜7 の設計（daemon の構成・spool・IPP/LPD、設定の保存と権限、libkeiland・拡張・backend の口、Settings の頁、試験の方法）。成果は [design.md](../design.md)。

## 結果（2026-10-05、初版）

- [design.md](../design.md) の初版: 利用者の権限の `keiland-printd`（compositor の backend が起動、root の daemon・setuid・init の service・CUPS を使わない）、利用者ごとの `~/.config/keiland/printers.conf`、fd で文書を渡す protocol（kl_system_manager_v1 version 9、kl_system_printers_v1）、IPP/LPD の最小、Settings の頁、試験の層。
- ユーザーの判断が要る点（design.md §0）: D2 printer の設定を system 全体で共有するか（共有なら root の口が要るので別の設計と承認）、D3 LPD の queue の名前の入力、D6 PDF Viewer の Print を WS に足すか。レビューで加わった点: IPP の resource の path の順、Linux・FreeBSD で既存の CUPS の printer を出さないこと、受け入れの printer の機種（多くの家庭の printer は PDF を受けない → p005 の PWG raster が要る）、login 名を printer に送ること、spool の上限の値。

## 敵対的レビュー（design-reviewer、2026-10-05）の指摘と直す予定（未反映）

重大（p002・p003 の前に design.md を直す）:
1. capability の bit 0x100 が `KL_SYSTEM_HAS_ADMINISTER` と衝突 → 0x200、`KL_SYSTEM_SINCE_PRINTERS 9`、`KL_SYSTEM_CHANGED_PRINTERS 0x100`。
2. result の applied の意味を既存の `KL_SYSTEM_RESULT_*`（OK=0）に合わせる。listener の changed は無く `kl_system_dispatch` の changed の bit。
3. print した app が自分の job の ID を知る event（`queued(request, job)`）と `kl_system_print_job_of`。
4. job の ID は backend が振る。backend は ACCEPTED まで fd を持ち、printd の EOF で再送・ACCEPTED 後の未完は failed（daemon）。待機の終了は IDLE/BYE の合意。起動時に spool の残りを消す。状態遷移の表。
5. title・host・printer の応答の制御文字を compositor と printd の両方で検証（行の protocol と LPD の control file への注入）。LPD の field は RFC の長さ。
6. fd の所有権: submit は成否に関わらず fd を受け取る、却下の経路でも `zwl_take_fd` と close、printd への送信は nonblocking と `MSG_NOSIGNAL`、保持の上限。
7. spool の合計と job の数の上限、`/run` の tmpfs は logout で消えない事実に合わせる、`O_EXCL|O_NOFOLLOW`、XDG_RUNTIME_DIR の検査。

中: IPP の番号の表・IPP/1.1 への fallback・Content-Length・job-state の全ての値・426/401・zedBSD に IPv6 が無いこと、LPD の abort（01）と remove（05）・3 桁の job 番号、printd の内部（poll の loop か thread）と network の応答の解析の上限、fd の種類（`S_ISREG`）と `pread`、backend の口を volumes と同じ型で書く・子 process の fd と signal・`posix_spawn`、printers.conf の writer の thread と flock、名前は足した時だけ問い合わせて保存、job の上限と remove・cancel の規則、試験の抜け（bit の重なり・kill・注入・fd の漏れ・SIGPIPE・fuzz・高い番号の port）、Linux・FreeBSD の build と install の Phase、p005 を別の WS へ（p006 を最後に）。
軽: fd 3 の記述、例の host 名、`%PDF-` の位置、title の長さの規則、get の形を既存に揃える、KL_VERSION の衝突は Q1 が調整、detail の語の一覧、syslog に address を残すか。

## 再開の条件

UAT の所見の対応（q755〜q760）の後、上の指摘を design.md に反映し、もう一度 design-reviewer にかける。判断の要る点を Q1 経由でユーザーに出す。

## 第 2 版（2026-10-05 夕）

- design.md を第 2 版に: protocol version 10・KL_VERSION 34（v9・KL 33 は ws132-p009）、capability 0x200・CHANGED 0x100、result は KL_SYSTEM_RESULT_*、queued(request, job) と kl_system_print_job_of、fd の所有の規則、job の状態遷移の表と IDLE/BYE、両側の行の検め、spool の上限（D8）、printd の内部（poll の loop、応答の上限）、IPP の番号の表と 1.1 への fallback、LPD の abort・remove、detail の語、Linux・FreeBSD の Phase、filter は別の WS へ。判断の候補 D1〜D8。

## 2 回目の敵対的レビュー（2026-10-05 夕）の指摘（未反映、第 3 版で直す）

重大: (1) printd と backend の socket で捨てた行・部分送信の fd の取り違え（printd も fd の FIFO を持つ、捨てた JOB 行も fd を取る、MSG_CMSG_CLOEXEC・MSG_CTRUNC、送信 queue に fd 送信済みの印）。(2) cancel と ACCEPTED の競合で取り消した job が印刷される（JOB の後は常に CANCEL を送り、CANCELLED は printd の STATE で確定、終わった状態は吸収）。(3) backend の口が client の request の番号を受けるので取り違える（volumes と同じく backend が番号を振り、compositor が待ちの表を持つ）。
中: manager の request の番号は 9（10 ではない）、fd が後から届く EAGAIN と fd を取った後の EPROTO の経路、同じ利用者の 2 つの printd の spool（printd ごとの dir と lock）、zedBSD の posix_spawn の sigdefault は SIGKILL・SIGSTOP で失敗する（必要な signal だけ、子で closefrom(4)）、printers.conf は lock の下で読み直して差分を当てる（id は最大値+1、再利用しない）、job_of は表にする、IPP の Cancel-Job・Get-Job-Attributes の job-id と request-id、printd の SIGPIPE、timeout は「進みの無い時間」、crash 後の spool の掃除と home.c の waitpid(-1)（範囲外、Q1 へ）、状態遷移の通常の行と LPD の取り消し、IPP の応答の解析（delimiter 0x05、未知の tag の読み飛ばし、textWithLanguage、1xx、EOF の本体）。
軽: title の規則の層ごとの統一、backend の値と型の定義、add の result の時機と NAME の失敗の既定、id の再利用、LPD の細部（job 番号の種、data file を先に）、HTTP の Host、名前解決の thread、capability は printd の有無で、全ての app に job の title が届くこと（判断の点）、RAM の spool の代わりに fd から直接送る案、Phase の判断の期限・FreeBSD の確認、試験の抜け。
