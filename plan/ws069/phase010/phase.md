<!-- awesome-plan project=zedbsd record=ws069p010 -->

# ws069-p010: BUG-057（GLX の間欠の止まり）の原因と修正

Phase ID: `ws069-p010`
Parent: [WS069](../ws.md)
Status: cleared（q489-i01、2026-09-27）
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

## 結果（2026-09-27、q489-i01）

- 失敗した send の errno は **24（EAGAIN）**（libX11 の `wr()` に足した報告: `Xlib: send failed: errno=24 with 195480 bytes of the request left`、
  最初の send は 64 KiB を送って部分成功）。
- 原因: `waitq_sleep` は「眠る前に wakeup が来た（sequence が進んだ）」を EAGAIN で返す約束（呼び手は条件を見直して眠り直す）。
  WAITQ_INTERRUPTIBLE の道は signal を見るために condition lock を一度外すので、その間に受け手が buffer を空けると EAGAIN になる。
  `unix_stream_wait_space`（`src/kern/net/unix-socket.c`）と `socket_enqueue_packet_wait` の待ち（`src/kern/net/socket.c`）はこれを
  失敗として返し、**blocking の send が EAGAIN で失敗**していた。libX11 の `wr()` は EINTR 以外で要求を打ち切り、X の stream がずれて
  次の要求の返事が来なくなった（BUG-057 の止まり）。他の `waitq_sleep` の呼び手（tcp.c、accept、受信）は EAGAIN で眠り直している。
- 修正: 2 つの待ちで EAGAIN は条件を見直して眠り直す。libX11 の `wr()` は失敗の errno と残りの byte を stderr に出す（診断）。

## 検証

- host: `plan/ws014/tests/run-handle-fd-test.sh`、`run-gpu-fence-payload-test.sh`（socket.c・unix-socket.c を含む、通常と ASan/UBSan）PASS。
- Venus（QEMU）: x11-p005 PASS（frame 300、DONE、回る）、x11-p003・p004・zdesktop-p070・egl-p008 PASS。zgears を約 5 分（2200 frame 超）
  回して STALL・send の失敗・止まった client の報告は 0。
- i915 実機（capture）: run3（`build/ws069-p010-hw3/`）で 6 検査 PASS、zgears は 5000 frame 超（31〜44 fps）。run1・run4 は BUG-056
  （zdesktop が client の後片付けや App Home の後に終わる）、run2 は zgears が最初の frame の前に黙って終わった（BUG-058、1 回）。
  実機の LCD の目視は未実施。
- boot test PASS（`build/ws069-p010-boot/login.png`）。
- 変えた kernel の file の規約の指摘の数は変わらない（legacy の数のまま）。
