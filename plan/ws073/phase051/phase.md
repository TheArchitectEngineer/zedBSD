<!-- awesome-plan project=zedbsd record=ws073-p051 -->
# ws073-p051: BUG-135 — UFS の journal の commit の flush を mount の lock の外へ出し、stat の秒単位の停止の残りを直す

Status: in-progress（q653-i01、P9 generation1（Fable 5.1、high）、2026-10-04）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-135](../../bugs/BUG-135.md)
Queue: q653（2026-10-04 user「BUG-135はFable 5.1 HighのサブエージェントP9に修正を依頼しましょう。テストは依頼しないで除外しましょう。」）

## 範囲（Q1、2026-10-04）

[ws073-p045](../phase045/phase.md)（q624）が直した後に残る stat の停止（4 回のうち 2 回、最長 3.8 秒）の原因を特定して直す。
確認は build（warning 0）・host 試験・自分の診断の範囲。**T1・T2 への QEMU・実機の試験の依頼はしない（user の指示）**。
HAL の API は変えない。

## 原因（code の読みと p045 の gdbstub の記録）

p045 の後の鎖:

1. `stat` → `ufs_lookup` → `namespace_share` が `namespace_lock` を一瞬通る。名前の変更（`ufs_rename`・`ufs_create` など）が
   `namespace_enter` で `namespace_lock` を排他で持っている間、lookup はそこで待つ。
2. 名前の変更は `namespace_lock` を持ったまま `ms->lock`（mount の lock）を待つ。
3. `ms->lock` は fsync（`ufs_file_sync` → `ufs_sync`）か flusher の `j3_hook` が持ったまま `j3_commit_locked` を走らせる。
   `j3_commit_locked` は payload の複写の後に `j3_commit_seal` で **descriptor を書いて `disk_sync`（buf_sync ＋ `bio_flush`）、
   commit record を書いてもう一度 `disk_sync`** する。host の disk の flush が秒単位のとき、この 2 回の flush の間 `ms->lock` が
   取られたままで、鎖の先の lookup が秒単位で止まる。

p045 の直し（lookup と commit が namespace を共有、fsync の最後の `disk_sync` を lock の外へ）は hook の commit を lookup から外したが、
名前の変更 → `ms->lock` → commit の flush の鎖は残っていた（p045 の記録の「残る停止」と同じ）。

## 直し（設計）

journal（j3）の commit を 2 段に分ける:

- **閉じる段**（`j3_close_transaction`。`ms->lock` と `j3.lock` の中。memory の複写だけ）: pinned な metadata の今の内容を journal の slot
  の cache line へ写し（`j3_commit_payload`、従来どおり）、descriptor を作り、running transaction の ranges・freed set・sequence を
  「閉じた transaction」の側（`closed_*`）へ入れ替え、新しい running transaction を空で始める（sequence＋1）。
- **書いて flush する段**（`j3_finish_commit`。lock の外）: descriptor を書く → durable → commit record を書く → durable
  （`j3_commit_seal`、順序は従来どおり）→ 閉じた transaction の ranges の pin を外す → その home を書く（`buf_writeback_range`）→
  `j3.lock` の中で閉じた側を空にし `closing` を 0 にして待ち手を起こす。
- 同時に走る commit は 1 つ（`closing`）。fsync（`ufs_sync`）は `ms->lock` と `j3.lock` を取って `closing` なら両方を離して
  `mutex_wait` で待ってやり直す（`ms->lock` を持ったまま flush を待たない）。`j3_hook` は commit が書かれている間はその回を飛ばす
  （次の interval で commit する）。transaction が満ちたときの commit（`j3_write`）は従来どおり lock の中で同期的に行う（稀）。
- **pin を transaction の sequence の tag に**（`include/kern/buf.h`・`src/kern/buf.c`）: `b_journal_pin` は 0 か pin した transaction の
  sequence。`buf_write_pinned(..., pin)` は tag を上書きし、`buf_unpin(..., pin)` は tag が一致する line だけ外す。閉じた transaction N の
  commit が書かれている間に running の N＋1 が同じ line を書き換えると tag は N＋1 になり、N の unpin はその line を外さない
  （N＋1 の変更が N＋1 の commit record より先に home へ届かない）。
- **freed set を 2 つ**: 閉じた transaction が解放した block は、その commit が durable になるまで content の書き込みを hold する
  （`j3_content_freed` は running と閉じた側の両方を見る）。
- 新しい `buf_writeback_range(disk, block, count)`: 範囲の line を home へ書く（pinned な line は飛ばす）。N の home を N＋1 を閉じる前に
  device へ出すために使う（N＋1 が N の line を pin し直すと N＋2 の durable の `buf_sync` がその line を飛ばすため）。

### journal の順序の保証（変えない）

- 「content と前の home を書く → durable → descriptor → durable → commit record → durable → home の書き」は保たれる。durable は
  書いて flush する段の `j3_commit_seal` の中で従来どおり 2 回。閉じた transaction の pinned な line は commit record が durable に
  なる（`j3_commit_seal` が返る）まで `buf_unpin` されず、flusher・`buf_sync` は pinned な line を飛ばす（`writeback_line`）。
- N の home が device へ出る前に N の slot の反対側（N−1 の slot）が上書きされることはない: N＋1 の閉じる段は N の finish
  （unpin と home の書き）の後で、N＋1 の record より前に N＋1 の durable が flush する。

## 実施（この attempt）

（実施の後に書く: 変更の file、build、host 試験、自分の診断、残り）
