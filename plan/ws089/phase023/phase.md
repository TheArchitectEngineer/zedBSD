<!-- awesome-plan project=zedbsd record=ws089-p023 -->

# ws089-p023: Settings の Storage の頁: folder の階層ごとの使用量の解析（multi-thread、逐次の更新、Stop）と Trash を空にする

Status: cleared（2026-10-05 Q1: T1-159 の settings-p023 PASS（Analyze が du と一致・folder・Stop・Trash の大きさ・確認つきの Empty で removed=5））。以前: in-progress（実装済み、T1 待ち）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: q728（P2、2026-10-05）

## ユーザーの要望（2026-10-04 夜、UAT-3 の後、原文）

「SettingsのStorageタブは、解析ボタンを押すと、フォルダ階層ごとの使用量を解析して、リアルタイムに解析結果をアップデートして表示し、Stopボタンで止められるマルチスレッド実装にしてほしい。また、Storageタブに、Trashのクリアを実装してほしい。」

## 範囲

1. Storage の頁に「解析」の button。押すと folder の階層ごとの使用量（du に当たる）を解析する。
   - 解析は UI の thread と別の thread（複数の worker の thread で directory の木を分けて走査する multi-thread）で行い、UI を止めない。
   - 結果は走査の途中から逐次に表示を更新する（folder ごとの合計が増えていくのが見える、上位の folder の大きい順、子へ降りられる表示は設計で決める）。
   - Stop の button で止められる（worker の thread を安全に止め、途中までの結果は残す）。
   - 対象（利用者の home か、mount している volume ごとか、権限の無い directory の扱い、hard link・別の file system を跨がないか）を設計で決める。
2. Storage の頁に「Trash を空にする」。利用者の Trash（Files（WS127）の Trash の場所と形式に合わせる）の大きさを表示し、確認の後に空にする。Files が開いている時の通知・表示の更新も考える。
3. 権限・OS の境界: app の自分の機能の file の走査なので、app の中で行ってよい（Guardrail の「app の自分の機能のための OS の依存はこの規則の対象外」の考え方）。libkeiland の thread・UI の部品を使う。

## 受け入れ（案）

- 大きな木（例: 数万 file）の解析中も UI が応答し、表示が逐次に更新され、Stop で 1 秒以内に止まる。
- 結果の合計が `du` の値と合う（host の試験と QEMU）。
- Trash を空にすると Trash の中身が消え、表示の大きさが 0 になる。Files の Trash の表示とも合う。
- C の全文の規約、build warning 0。QEMU は T1、実機は UAT。

## 依存

WS127（Files の Trash の実装）、libkeiland の thread の扱い。

## 設計（2026-10-05、P2）

- **対象**: 利用者の home（`$HOME`）。folder の行の click でその folder を解析し直し（子へ降りる）、「Up」で上へ（home まで）。他の file system に入らない（du -x と同じ、st_dev）、symbolic link を辿らない（AT_SYMLINK_NOFOLLOW・O_NOFOLLOW）、hard link の file は 1 度だけ数える（device・inode の集合）、大きさは disk の上の大きさ（st_blocks × 512、du と同じ）。読めない folder は数えて別に表示。
- **解析**（`userland/desktop/settings/storage-scan.c`、UI と独立で host で試験）: root の entry を読み、folder ごとに group（最大 46、超えた分は「Other folders」）、root の直下の file は「Files here」。各 folder は共有の job の list に入り、**4 つの worker の thread** が取って読み、file の大きさを group に足し、子の folder を list に戻す。256 entry ごとに合計を足し stop を見る（Stop は 1 秒以内、数えた分は残る）。UI は描く時に view（大きい順の group と合計・状態・generation）を写すだけで、generation が動いた時に最大 200 ms ごとに描き直す。
- **Trash**（`storage-trash.c`）: Files と同じ freedesktop.org の home の Trash（`$XDG_DATA_HOME/Trash` か `~/.local/share/Trash`）。大きさは `Trash/files` を同じ解析で数える。「Empty Trash」は確認（Empty・Cancel、「Its items are removed for good.」）の後、別の thread で `files/` と `info/` の中を消す（folder は残す、link は消すが link の先は消さない、他の file system には入らない）。終わったら数え直す。Files は Trash の folder を読み直した時に空になる（Files の側の通知は範囲外）。volume ごとの Trash（`$topdir/.Trash-$uid`）は対象外。
- **OS の境界**: app 自身の機能の file の走査なので app の中（POSIX の openat・fdopendir・fstatat・unlinkat と pthread、Linux・FreeBSD でも同じ）。

## 確認（2026-10-05）

- host: `plan/ws089/tests/run-host-storage-scan.sh` PASS（ASan・UBSan: 合計が GNU `du -sx -B1` と一致、hard link を 1 度、外への link を辿らない、大きい順、読めない folder を別に、30000 file の木を全て数える、すぐの Stop が 1 秒以内（1 ms）で stopped、数えた分が残る）。`run-host-storage-trash.sh` PASS（home の Trash の場所、files/・info/ の中を全て消し folder は残す、link の先の file は残る）。settings-render で Storage の頁（解析の結果・Trash の確認・空にした後）を描いて目視。
- build（warning 0）: zedBSD の settings、Linux の Keiland（-Werror）。`keiland-os-boundary/check.sh` PASS。style-check: 新しい file は違反 0。
- QEMU（T1 に依頼）: `plan/ws089/tests/settings-p023.sh`（/root に 3 MB と 4000 file の folder と 2 項目の Trash を作り、Analyze の合計が `du -skx /root` と一致、folder の行で降りる、Analyze と即 Stop、Trash の大きさ・Empty の確認・空に、PNG 2 枚）。
- 未実施: 実機（UAT）。

## T1-157 の後（2026-10-05、P2）

- analyze・done・stopped・trash は ok。`STORAGE emptied removed=4` だけ MISSING: 実際は `removed=5`（`files/old.txt`・`files/Old/inner.txt`・`files/Old`・2 つの `.trashinfo`。folder の中身も 1 項目ずつ数える）で、試験の期待の誤り。→ 試験を `removed=5` に。

## Q1 の判定（2026-10-05）

T1-159 の settings-p023 PASS（Analyze が du と一致・folder・Stop・Trash の大きさ・確認つきの Empty で removed=5）。**cleared**。
