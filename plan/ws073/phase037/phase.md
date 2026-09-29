<!-- awesome-plan project=zedbsd record=ws073-p037 -->

# ws073-p037: BUG-106（`ssh -tt` の出力の欠け）の再確認 — BUG-051 の重複

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-106](../../bugs/BUG-106.md)（→ [BUG-051](../../bugs/BUG-051.md) の duplicate）
Queue: main が WS073 のサブエージェント（`wt/ws073`）に割り当て（2026-09-29、30 分以内）。Queue の ID は main が記録する

## 範囲と受け入れ

BUG-051 の修正（ws073-p030、signal の frame が amd64 の red zone を空ける）の後の image で ws086 と同じ形の `ssh -tt ... 'cd /bin && ls'` を多数回流し、
毎回の終了状態と guest の `killed by signal` を記録する。欠けが出なければ BUG-106 を BUG-051 の重複として閉じる。

## 手順と結果

試験 [tests/bug106-ls.sh](../tests/bug106-ls.sh)（新規）: `; true` 付きで取った基準（1489 byte）と毎回の出力を比べ、終了状態・byte 数、違った回と 50 回ごとに
guest の `dmesg | grep -c 'killed by signal'` を記録。`LOAD=1` で guest に register の probe の負荷（`regcheck`、ws073-p030）。
image は worktree の `build/amd64/hdd-image.img`（p035 の build、BUG-051 の修正を含む。main bc256de0 相当の source から kernel は不変）、KVM・4 CPU。

| kernel | 負荷 | 回数 | 欠け | 欠けの形 | guest の SIGSEGV |
| --- | --- | --- | --- | --- | --- |
| 修正あり（今の tree） | なし | 300 | 0 | — | 0 |
| 修正あり | regcheck | 500 | 0 | — | 0 |
| 修正なし（`SIGNAL_USER_RED_ZONE` を 0 にした一時の vmunix を同じ image の ESP に。source はすぐ戻した） | なし | 300 | 5 | 全て rc=255、48〜867 byte | 6 |
| 修正なし | regcheck | 500 | 12 | 全て rc=255、48〜1389 byte | 累計 18 |

- 修正なしの落ち（dmesg）: `at 0x10031a518, address 0` = libcrypto + 0x25a518（`ChaCha20_ctr32`、p030 と同じ場所）、`at 0xaf0bc, address 0x18`
  （sshd-session の PIE の text の中の関数）。欠けた回は全て ssh の終了状態 255 と guest の SIGSEGV を伴い、rc 0 で欠けた回は 0。
- 結論: BUG-106 の欠けは sshd-session（privsep の子）が signal の frame に red zone を壊されて落ちる BUG-051 の現れ。BUG-051 の修正の後は 800 回で 0。
  BUG-106 を BUG-051 の duplicate として閉じる。記録は `build/ws073-bug106/{noload,load,noredzone-noload,noredzone-load}/runs.txt`。
- 未実施: 実機、ws086 の image（`config-amd64-ssh.mk`）そのものでの再試験（同じ kernel の source なので使わなかった）。

## Resume point

完了。
