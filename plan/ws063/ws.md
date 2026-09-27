<!-- awesome-plan project=zedbsd record=ws063 -->

# WS063: UFS の journal を既定にする（journal の無い image は mount の時に作る、`nojournal` の mount option）

<!-- awesome-plan-current:start -->
Status: completed
Primary Milestone: MG004
Related Milestones: MG002（fg011: configure の性能）
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: q443（p001）、2026-09-27 の並列実行（p002）
Resume point: —（完了。残りは下の「制限・移管」）
<!-- awesome-plan-current:end -->

## 目標

2026-09-26 ユーザー指示: 「ジャーナルなしで作成したイメージも、マウント時にジャーナルを作り直すようにして、ジャーナルをデフォルトで有効にしてください。マウントオプションでジャーナルオフに対応しましょう。」

受け入れ: journal の無い image（今の native の root）を mount すると journal が作られ有効になる。`mount -o nojournal` で無効。強制終了の後に起動でき、volume の検査（`check-volume.py`）が UFS OK。configure が journal 無しの write cached と同程度。回帰。規約。

## 結果（2026-09-27、completed）

| 項目 | 結果 |
| --- | --- |
| 既定の journal | 書き込み可能な write cached の mount は batched journal（v3、WS060）を使う。journal の無い volume は最初の mount で root の directory に `.ufs-journal` を作り、superblock の予備の word に locator（`ZJ3L`）を書く |
| 大きさ | mkfs（`zedimage-host ufs --journal-size=MIB`、guest の `mkfs -t ufs --journal-size=MIB`）が superblock に記録（`ZJ3R`、0〜1024 MiB）。記録が無ければ ext4 の表（最大 128 MiB）。空きの 1/8 まで。mount で再利用・確保し直し・作成。block は連続でなくてよい（header に extent の一覧） |
| 名前 | `.ufs-journal` は root の directory で VFS から作成・open・削除・rename の先・link ができず（`EPERM`）、一覧に出ない |
| option | `mount -o nojournal`（journal を使わない、replay だけ）、`-o writethru`（write through、journal なし）。`mount` の一覧に `(rw,nojournal)`・`(rw,writethru)` |
| 強制終了 | v3 の作業 volume 4 時点（3・5・8・13 秒）、v2 の tail の volume 2 時点、root で、replay の後 UFS OK・sync した内容が残る（ws063-p002） |
| configure | `/root` 11.3 秒（host 10.9〜11.2 秒、ws063-p001。p002 は負荷のため計測せず） |
| 回帰 | ws063-p001: make の差分試験 91/91、SMP 6/6、COW、itimer、swaphog、`dir-grow.sh`、boot、sh の差分試験。ws063-p002（意味を変えない書き直し）: build warning 0、boot、強制終了と journal の機能の試験 |
| 規約 | WS060・WS063 の新しい code を全文で見直した（ws063-p002）。style-check の各 file の数は WS060 の前より少ない（ufs.c 621 → 579、buf.c 140 → 137、mount 57 → 50、mkfs 6 → 6・26 → 26、zedimage-host 73 → 70） |

QEMU だけ。実機は未実施。

## Phase 一覧

| Phase | 内容 | Status |
| --- | --- | --- |
| ws063-p001 | journal の大きさを mkfs で記録、mount で `.ufs-journal` を再利用・確保し直し・作成、extent の一覧、名前の特別扱い | cleared（q443-i01） |
| ws063-p002 | 強制終了の試験、回帰、規約の適合（WS060 の変更を含む） | cleared（2026-09-27） |

Phase の記録は git の履歴にある（WS の完了で削除）。

## 制限・移管

- **v2 の tail の journal**（`--profile=journal-snapshot` の volume）は v2 のまま動く（操作ごとの flush で遅い、BUG-040 の形）。v2 の locator は snapshot の領域と一緒に volume の末尾にあり、v3 へ移すと snapshot の on-disk の形に手が入るため、既定（逆に戻せる）として移行しない。この profile は明示の指定でしか作られない（試験と snapshot 用）。移行が要るなら Future Work の候補（判断が要る点 1）。
- 走っている transaction が解放した block の集合（`J3_FREED_MAX` の半分、8192 block）を超えた解放は追わない。その block に commit の前に新しい中身が書かれると、解放を戻す crash の後に古い持ち主の中身が変わりうる（metadata の整合性は保たれる）。transaction の範囲の数か payload が満ちたときの commit は操作の途中で起きうる。どちらも設計どおりの妥協（ws060-p002）で、この WS では変えていない。
- 見つけた不具合（ws063-p002、この WS の前から）: write cached の mount の上の regular file への guest の `mkfs -t ufs` は format を書くが EBUSY で終わり、その後 volume の `umount` と `sync` が EBUSY になる（`-o writethru` では成功）。Bug Board への登録を main session に依頼した。

## 判断が要る点

1. v2 の tail の journal の volume を v3 へ移すか（今は移さない。上の「制限・移管」）。**2026-09-27 ユーザーの判断: 今のまま**（「じゃあとりあえず今のままでOKです。」）。スナップショットの機能を設計するときに見直す。

## 試験の道具（plan/tools へ移した）

- `plan/tools/ufs/crash-test.sh`: 既定を v3（journal の無い volume を mount で journal 化）と NVMe（2 台目 `nvme1n1`）に。`PROFILE=journal-snapshot` で v2。
- `plan/tools/ufs/journal-func.sh`（guest 側 `journal-guest.sh`）: 名前の拒否、option の表示、mount での作成、`--journal-size=0`、`dir-grow.sh`、mkfs の記録。
- `plan/tools/ufs/root-crash.sh`: root の強制終了と replay。
- `plan/tools/ufs/zedimage-compare.sh`: 2 つの zedimage-host の UFS の出力を byte 単位で比べる（書き直しの確認）。
- `plan/tools/guest/config-amd64-ssh.mk`・`build-ssh-image.sh`: clang の無い SSH の guest image（`build/amd64` に作る。package が `build/amd64/dynamic` に link するため）。
