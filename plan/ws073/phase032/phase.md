<!-- awesome-plan project=zedbsd record=ws073-p032 -->

# ws073-p032: `ssh -tt` の最後の命令の出力の欠け（BUG-106）の調査

Status: uncleared（2026-09-29、WS081 の作業用サブエージェント（worktree `agent-abe8d4ea8794ae4fc`）が main の依頼で実施。目安の 60 分で区切った。再現できず、原因は未確定、修正なし）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-106](../../bugs/BUG-106.md)
Queue: main の依頼（2026-09-29「BUG-106 を WS073 の新しい Phase として記録し、原因を直す。kernel の pty・tty の修正は可、HAL の API は不可。目安 60 分」）。Queue の ID は main が記録する
Resume point: BUG-106 が再現する image（ws086-p002 と同じ `plan/tools/guest/config-amd64-ssh.mk` の今の main の image）で、失敗した回の ssh の終了状態と guest の `dmesg` の `killed by signal` を同時に取る。SIGSEGV が伴えば BUG-051（ws073-p030）の現れとして p030 に合わせる

## 範囲

ws086-p002 の報告: `ssh -tt ... 'stty cols N rows 24; cd DIR && ls ...'`（ls が最後で shell が exec する）の出力が 8〜25 回に 1 回、空か途中で切れる。
後ろに `; true` を置くと 25/25 回とも完全。調べる点（main）: pty の slave の close で未読の出力を捨てていないか、sshd-session の子の終わりと channel の EOF の順。

## 調べたこと

### kernel の pty（`src/kern/tty.c`、読んだだけ。変更なし）

- slave の close（`pty_slave_close`）は `slave_opens` を減らして master を起こすだけで、出力の ring（`pair->output`、4096 byte）を捨てない。
  pair を止める（`active = 0`）のは master も閉じている時だけ。
- master の read（`pty_master_read`）は、slave が閉じた後も ring が空になるまで返し、空になってから 0（EOF）を返す。
  poll は ring に残りがあれば POLLIN、slave が閉じていれば POLLHUP。
- ring を空にするのは `tty_backend_flush_output` だけで、呼ぶのは `TCFLSH` の `TCOFLUSH`・`TCIOFLUSH` だけ。
  `TCSETSW`・`TCSETSF`（stty の tcsetattr）は drain（master が読むのを待つ）で、出力は捨てない。
- session の leader（exec した ls）の終わり（`tty_detach_process`）は、session を外して前面の group に SIGHUP・SIGCONT を送るだけで、出力には触れない。
- `file_close` は最後の参照でだけ `pty_master_close` を呼ぶ（dup・SCM_RIGHTS で渡した master の複数の fd は 1 つの description）。
- 読んだ範囲では、未読の出力を捨てる経路は見つからなかった。

### OpenSSH 10.5p1（`build/packages/openssh/src`、読んだだけ）

- 子の終わり（`session_close_by_pid` → `session_exit_message`）は exit-status を送って `chan_write_failed` だけを呼び、`chan_read_failed` は呼ばない
  （コメント「there could be some more data waiting in the pipe」）。channel は pty の master を EOF まで読み続ける。
- `session_pty_cleanup`（privsep の `mm_session_pty_cleanup2`）が閉じるのは dup した `s->ptymaster` と monitor の複写だけで、channel の fd は残る。
- 順の問題（EOF の前に channel を閉じる）は、読んだ範囲では見つからなかった。

### guest での再現の試み（QEMU、amd64、KVM、WS081 の pen の image `build/ws081-main-pen.img`、kernel は 2026-09-29 03:00 の main）

host から `ssh -tt`（`plan/tmp/guest/id_ed25519`）で、scratchpad の繰り返しの script で出力の byte 数と ssh の終了状態を数えた。

| command（最後が exec される） | 回数 | 結果 |
| --- | --- | --- |
| `ls /bin`（image の旧 ls） | 43 | 全て完全（1193 byte） |
| `stty cols 80 rows 24; cd /bin && ls`（旧 ls） | 40 | 全て完全 |
| 同上 `ls -l`（8855 byte、ring 4096 を越える）・`ls -R /usr/share` | 25・25 | 全て完全 |
| `stty cols 80 rows 24; cd /bin && /tmp/ls`（今の main の ls を build して置いた） | 30 + 40 + 60 + 3 並列 × 40 | 1 回だけ 0 byte。他は全て完全（1598 byte） |
| 同上 `/tmp/ls -l` | 150 | 全て完全（8855 byte） |

- 0 byte の 1 回は、guest の sshd の listener（pid 12）が SIGSEGV で落ちた時と重なった:
  `kern: pid 12 killed by signal 11 (vector 14) at 0x0000000100300c64, address 0x0000000000000038`。
  - その後の 25 回は `kex_exchange_identification: Connection reset by peer`。sshd が作り直される（`sshd[820]: Server listening`）まで続いた。
  - この 0 byte は BUG-106 の「pty の出力の欠け」ではなく、listener の SIGSEGV による接続の失敗と見られる。
  - 落ちた address は共有 library の中（`sshd` の image の外）。NULL+0x38 の読みで、BUG-051（sshd-session の子の SIGSEGV、ws073-p030 が調査中）と同じ種類。
  - library と関数の特定はしていない（load address を guest で取れない。p030 の libcrypto の base `0x1000b5000` を仮定すると offset 0x24bc64 だが、p030 の libcrypto と pen の image の libcrypto は別の build で、確かでない）。
- 合わせて約 500 回で、pty の出力の欠け（途中で切れる・rc 0 で空）は 0 回。

## 結論（仮）

- kernel の pty・OpenSSH の読んだ範囲に、出力を捨てる経路は見つからなかった。この image では BUG-106 を再現できなかった。
- 観測した唯一の欠けは sshd の SIGSEGV（BUG-051 の種類）と同時だった。ws086-p002 の「空か途中で切れる」は、sshd-session の子の SIGSEGV（BUG-051）の現れの可能性がある。
  - 子が転送の途中で落ちれば、host の ssh は出力の途中で切れ、終了状態は 255 になる。ws086 は `; true` の回の終了状態 0 だけを記録していて、失敗した回の終了状態と guest の kernel の log は記録していない。
  - `; true` で直る理由は説明できていない（exec の有無で、子の終わりと channel の data の時間の関係が変わる）。
- 修正はしていない（原因が確定しないため。kernel・OpenSSH とも変更なし）。

## 確認（実行したもの）

- 読んだ file: `src/kern/tty.c`（pty の master・slave・ioctl・detach）、`src/kern/file.c`（`file_close`）、`src/kern/net/unix-socket.c`（rights の受け渡しの有無）、
  OpenSSH 10.5p1 の `session.c`・`monitor.c`・`monitor_wrap.c`。
- guest: 上の表（`build/ws081-run`、pen の image）。guest の `dmesg` で `killed by signal` が 1 件（上）。
- 未実施: ws086-p002 と同じ ssh の image（`config-amd64-ssh.mk`、今の main）での再現。失敗した回の ssh の終了状態と `dmesg` の突き合わせ。

## 残り

- 再現する image で、失敗の回ごとに ssh の終了状態・出力の byte 数・guest の `dmesg | grep 'killed by signal'`・`/var/log/messages` の `mm_reap` を記録する。
  - rc 255 と SIGSEGV が伴えば、BUG-106 を BUG-051 の重複（duplicate）にし、p030 で扱う。
  - rc 0 で欠けるなら、pty の側の調査に戻る（master の read と slave の exit の間の順を、gdbstub で `pty_master_read`・`pty_slave_close` に breakpoint を置いて見る）。
- 新しい観測（listener の pid 12、address 0x38）は BUG-051 の 3 回目の観測として p030 に渡す（main 経由）。
