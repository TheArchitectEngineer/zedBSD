<!-- awesome-plan project=zedbsd record=ws069p010 -->

# ws069-p010: BUG-057（GLX の間欠の止まり）の原因と修正

Phase ID: `ws069-p010`
Parent: [WS069](../ws.md)
Status: in-progress（q489-i01）
Phase disposition: normal
Queue: q489-i01
承認: 2026-09-26 ユーザーの自律実行の指示、2026-09-27「続けてください。」
Bug: [BUG-057](../../bugs/BUG-057.md)

## 分かっていること（p008 まで）

server は PutImage（261016 byte）の 196608 byte（3×64 KiB、unix socket の high-water mark の 3 回分）と、その後の GetGeometry の 8 byte を
受け取っている（196616 = 196608 + 8）。つまり client の `send()` が途中で失敗し、libX11 の `wr()`（EINTR だけ再試行）が要求を途中で
打ち切り、次の要求の byte が PutImage の中身として読まれて stream がずれ、GetGeometry の返事が来ない。

## 範囲

1. 失敗する send の errno を特定する（libX11 の `wr()` の失敗の報告、Venus で再現）。
2. 原因を直す: kernel の unix socket（`src/kern/net/unix-socket.c`、socket の file 層）か libX11（`wr()`・`rd()` が一時的な失敗で要求を
   打ち切らない）。両方に問題があれば両方。
3. Venus の x11-p005（frame 300 と DONE）、実機の `zdesktop-x11` の run（gears_turns を含む 6 検査）で確かめる。p008 の受け入れ 2 も満たす。

## 受け入れ

1. 原因が記録にある（errno と、それを返す kernel の道）。
2. Venus の x11-p005 が PASS（frame 300、DONE、回る）。実機の run で 6 検査 PASS。kernel を変えたので boot test。
3. 新しい・変えた C は規約の全文（style-check の指摘が増えない）。
