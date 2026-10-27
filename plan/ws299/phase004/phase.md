<!-- awesome-plan project=zedbsd record=ws299-p004 -->

# ws299-p004 待ちを重ねる: 連続の読みの先読みを非同期の worker で（raw の block device）

- 親: [WS299](../ws.md)。Queue: q1617 / q1617-i01（担当 P1、2026-10-27）。前提: p003（cleared 2026-10-27、main be47a24df）。
- Status: in-progress（測る口と測定は済み。範囲は 2026-10-28 Q1 が決定。design-reviewer から再開、q1634）
- Disposition: normal

## 測った事（2026-10-27、5330、kernel は main be47a24df ＋ 下の測る口、`nvme-bench-5330.sh zedbsd zb-p004m … read`）

測る口: `pci-nvme.c` に command ごとの時刻（post の時、割り込みが完了を見た時、待っていた thread が走った時）。4096 command ごとに平均と最大を dmesg へ（`nvme: 4096 commands: post to completion avg … completion to waiter avg …`）。

| 区間 | 256 KiB の command（raw の連続の読み、chunk の段） |
| --- | --- |
| post → 割り込みが完了を見る（device と割り込みの配達） | 平均 259〜283 µs、最大 0.5〜0.8 ms |
| 完了 → 待つ thread が走る（起床） | 平均 3 µs、最大 14〜77 µs |
| 256 KiB の 1 周（256 MiB を 0.42〜0.46 秒 / 1024 run） | 約 430 µs |
| うち command の外（CPU の仕事: line の帳簿、line → staging、staging → user の写し） | 約 160 µs（sys の時間 0.14〜0.19 秒 / 256 MiB = 約 0.65 ms/MiB と合う） |

- 起床の遅れ（割り込み・VMD・scheduler）は 3 µs で、問題ではない。queue を CPU ごとにする・VMD の vector を子に配る事は、1 本の流れの速さには効かない（下の「範囲の変更」）。
- device の時間 270 µs / 256 KiB（QD1 で約 970 MB/s）と CPU の 160 µs が直列に並んでいる。Debian の page cache 経由（1.1〜1.2 GB/s）は先読みで device と写しを重ねている。
- 並べた時の上限の目安: CPU の側は cache の hit の読み（`reread`）が 2.4〜2.7 GB/s、device は QD を上げれば fio の QD32 で 3.1 GB/s（p001）。重ねれば 1.1 GB/s（Debian の 95% は約 1.05 GB/s）に届く見込み。

## 範囲の変更（2026-10-28 Q1 が案のとおり決定）

- p004 の元の案（非同期の BIO・queue の深さ・複数の queue・VMD の vector）のうち、数が効くと示すのは「device の待ちと CPU の仕事を重ねる事」だけ。複数の queue・VMD の vector は起床 3 µs なので後へ（Future Work か、乱読みの QD32 を測る p007 の後に要否を決める）。
- 重ねる道は「driver を完全に非同期にする（割り込みから BIO を完了）」より、「buffer cache が先の run を worker の thread で読む（先読み）」の方が小さく、今の同期の driver と recovery の決まりをそのまま使える。これは元の p005（readahead）の raw の部分に当たる。p004 = raw の block device の先読み、p005 = UFS の file の先読みの窓と cluster・書きの合流、と分け直す案。

## 設計

### D1. 流れの検出（`src/kern/buf.c`）

- leaf の disk ごとに 1 本の流れ（`struct disk` に足さず、buf.c の中の小さな表: leaf・次の block・先読みの済んだ所・世代。表は 8 entry、古い物から使い回す）。
- `buf_read` が run を読み終えた時、その run の頭が流れの「次の block」と等しければ連続とみなし、次の block を run の終わりに進める。違えば流れを作り直す（世代を上げ、出ている先読みは無効）。
- 連続が 2 回続いたら、「先読みの済んだ所」から `BUF_READAHEAD_BYTES`（1 MiB、run は 256 KiB）先までの run を worker に頼む（既に頼んだ所は頼まない）。disk の終わりで止める。

### D2. 先読みの worker（`src/kern/buf.c`）

- kernel の thread を 2 本（`BUF_READAHEAD_WORKERS`）、最初の先読みの時に作る。仕事の queue は固定の大きさ（16 run）、満ちていれば頼まない（捨てる、待たない）。
- worker は仕事ごとに `transfer_run_lines` を読みで呼ぶ。caller の `data` が要らないように、page 列の道では line へ直に DMA し、caller への写しを飛ばす（`data == NULL` を「先読み: 写さない」と読む）。page 列の道が使えない disk（印の無い driver）には先読みをしない（D1 で leaf の印を見る）。
- `transfer_run_lines` は今どおり、既に valid な line・busy な line に当たると run を縮める（先読みは捨てる）。line は busy・inflight の間、demand の読みは `busy_acquire` で待つので、二重の読みは起きない。
- 誤り: 先読みの誤りは line を `BUF_ERROR` にせず（今の `finish_run_line` は読みの誤りで `BUF_VALID` を外し `BUF_ERROR` を付ける）、demand の読みが自分で読み直す。先読みの run の誤りは数えるだけ。
- memory の圧力: line の確保に失敗したら（`reference_line` の ENOMEM、`capacity_failures`）その仕事を捨て、流れの先読みを 1 回休む。
- 止める時: disk の除去（`disk_media_revoke`・`disk_media_gone`）・suspend の時は queue の仕事を捨て、実行中の仕事の終わりを待つ（buf.c の既存の disk の参照 `disk_buffer_acquire` の決まりに合わせる）。

### D3. 測る口

- p004 の測る口（command ごとの時刻）は WS299 の間だけ残し、p008 で消すか、`CONFIG` で切り替えるかを決める（常に 4096 command ごとに 1 行の log）。
- 先読みの数（頼んだ・捨てた・hit した run）を同じく log へ（UAPI の io-stats は変えない）。

## 変えない物

- HAL の API、UAPI、NVMe の driver の同期の形と recovery、書きの道（書きの重ねは p005）、UFS の file の先読み（p005）。

## 受け入れ（案）

1. 5330 の raw の連続の読み（`nvme-bench-5330.sh` の chunk の段）が Debian の page cache 経由（b-chunk の冷えた段、1.1〜1.2 GB/s）の 95% 以上。届かなければ原因を数で（測る口の値）。
2. 乱読み（bs 4k の dd を飛び飛びに、または先読みが働かない読み）で遅くならない（先読みを頼まない事を数で）。
3. QEMU: boot-test、`qemu-nvme-shapes.sh`、`qemu-grow-reboot.sh`、raw の読みの cksum（先読みと demand が混ざる）、disk を外す・suspend の時に止まらない事（QEMU の `device_del` は NVMe の hot unplug を driver が扱うかを先に確かめる）。
4. build の warning 0（NVMe を持つ platform と buf.c を使う全部の代表: 5330・pcat・visionfive2・rpi4・m68k-virt）。
5. host 試験: 流れの検出（連続・飛び・作り直し・disk の終わり・queue が満ちた時）を純粋な関数に分けて。

## 出典

- 無し（zedBSD の中の仕組みだけ。Linux・FreeBSD の readahead の code・値は見ていない）。

## 再開点（2026-10-27 ラップアップ、P1）

- やった事: 測る口（`pci-nvme.c` の `posted_ns`・`done_ns`・`latency_*`・`nvme_now_ns`、4096 command ごとの log）を実装して commit。build（config-5330、`build/p1-p004m`）rc 0・warning 0。5330 で `nvme-bench-5330.sh zedbsd zb-p004m build/p1-p004m/vmunix read` を流し、上の表の数（log は P1 の worktree の `build/hw5330/ws299/zb-p004m-read-dmesg.txt`、要約は作っていない）。
- 未実施: 上の設計（D1〜D3）の design-reviewer、Q1 への範囲の変更の案（p004 = raw の先読み、複数の queue・VMD の vector は後へ）の判断、実装、QEMU・5330 の試験。測る口の QEMU の回帰も未実施（5330 の起動と読みだけで確かめた）。
- 次の一手: Q1 に範囲の変更の案を出す → design-reviewer → D1・D2 の実装 → host 試験（流れの検出）→ QEMU（boot・nvme-shapes・grow-reboot）→ 5330 で `nvme-bench-5330.sh zedbsd zb-p004a <vmunix> read write`。
- 5330 の使い方: `plan/tools/hw5330/README.md`。NVMe の kernel だけの差し替えは `nvme-bench-5330.sh zedbsd NAME VMUNIX read|write`（`/tmp/i915-hw.lock` を自分で取る、Debian から BootNext で 1 回 zedBSD を起動して戻る）。image ごとは `5330-run.sh --nvme-image IMG NAME`（root は `ufs-grow.py` で p5 いっぱいに広がる）。遠隔の電源は無い（止まったら Q1 → ユーザー）。2026-10-27 の終わりの時点で 5330 の NVMe の p4 の kernel は `build/p1-p004m/vmunix`（測る口入り）。
