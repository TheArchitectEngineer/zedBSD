<!-- awesome-plan project=zedbsd record=ws094 -->

# WS094: desktop の file の icon

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。実装の残りは無い。p012 の L1 のユーザーの操作は 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」、L3 の数値は 2026-10-03 ユーザー「C5の200msは問題視しません。clearでOKです。理由は、あとでパフォーマンス改善のチケットを作ってまとめて改善するからです。」（F-072））
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

2026-09-29 ユーザー「デスクトップにファイルアイコンの表示。」

`~/Desktop` の file と folder を desktop（壁紙の上）に icon と名前で並べ、double click で開く、選択・移動（配置の保存）・右 click の menu・drag and drop（Files との間）、file の増減の追従を行う。設計は [design.md](design.md)（J1: Files が desktop の層の client として描く）。

## ユーザーの決定

- 2026-09-30（溢れた icon）: 1280x800 の画面に 100 項目を置くと 91 個だけが出る件は「今のまま」（溢れた分は出さない）。
- 2026-10-02「WS094 は現行設計のまま（Files の `files --desktop` が compositor の desktop surface に描く）実装しきる」。
- 2026-10-03「C5の200msは問題視しません。clearでOKです。理由は、あとでパフォーマンス改善のチケットを作ってまとめて改善するからです。」（速さは F-072 へ）
- 2026-10-03「Q1の判断で閉じられるものは閉じてください。」→ p014 cleared。
- 2026-10-08「規約の整形（各 WS の全文規約の Phase）はベータ3」→ p007 の規約は WS の外の一括の見直しへ。
- 2026-10-13「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」→ p012 の L1 のユーザーの操作は OK。

## 結果

- compositor の `keiland_desktop_v1`（role・token・configure、重ね順・合成・入力・focus・popup・DnD の対象、`--session` での起動と起こし直し）と、Files の `--desktop`（`~/Desktop` の icon、監視、選択・開く・keyboard・配置の保存と Clean Up、context menu・名前の変更・Trash・Copy・Paste・New Folder・Show in Files、drag と touch、長い名前の 2 行・画面の大きさの変更、置き場の溢れ）が main にある（p002〜p006・p010・p011）。
- jpg・gif の thumbnail（decoder を `userland/desktop/picture/` に共有、p013）。
- L1・L2（design §7 の受け入れ）は QEMU の Venus の `files-desktop-guest.sh` の手順で PASS。L3 の計測（p008）は QEMU で (a) 2908 ms・(b) 1094 ms・(c) 95 ms、p009 の直しの後 (a) 1954 ms・(c) 90 ms（目標 1500・50 ms を超過）。
- 実機（5330）の L1 の操作はユーザーの UAT で OK（2026-10-13）。L3 の実機の計測は未実施。

## 制限・移管

- L3 の (a)(c) の数値目標（p009・p015）は未達のまま、速さは F-072 へ。Venus の同期の呼び出しの約 10 ms の刻みは F-064。zdesktop の import の短縮は WS099 p016 へ移した。
- probe の restart の手順で試験用の probe が時々終わらない件は BUG-123。
- 規約の全文と全 guest の回帰（p007）は WS の外の一括の見直しへ。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws094-p001 | 設計（design.md、J1〜J7） | cleared（2026-09-29） |
| ws094-p002 | compositor の `keiland_desktop_v1` | cleared（2026-09-30） |
| ws094-p003 | libkeiland の client の API と Files の `--desktop` の骨組み | cleared（2026-09-30） |
| ws094-p004 | 選択・開く・keyboard・配置の保存と Clean Up | cleared（2026-09-30、QEMU） |
| ws094-p005 | context menu・名前の変更・Trash・Copy・Paste・New Folder | cleared（2026-09-30、QEMU） |
| ws094-p006 | drag（desktop の中・folder へ・Files の窓との DnD）と touch | cleared（2026-09-30、QEMU） |
| ws094-p007 | 全文の規約と回帰 | uncleared のまま WS を閉じた（q588 の source・host・build の部分は cleared。規約の全文は WS の外の一括の見直しへ） |
| ws094-p008 | L3a: 100 項目の計測の道具と基準値 | cleared（2026-09-30、QEMU） |
| ws094-p009 | L3b: 100 項目で L3 の数値目標に入れる | uncleared のまま WS を閉じた（(a)(c) 超過、速さは F-072） |
| ws094-p010 | L4a: 長い名前と画面の大きさの変更 | cleared（2026-09-30、統合の試験 demo-s8-s9.sh） |
| ws094-p011 | L4b: 置き場の溢れと保存の行の掃除 | cleared（2026-10-02、q582） |
| ws094-p012 | L5: 実機（5330）での L1〜L3 の確認 | planned のまま WS を閉じた（L1 の操作は 2026-10-13 ユーザーの UAT で OK、L3 の実機の計測は未実施） |
| ws094-p013 | L3c: jpg と gif の thumbnail | cleared（2026-09-30、host と QEMU） |
| ws094-p014 | q588 の所有外の規約の指摘を直す | cleared（2026-10-03 Q1） |
| ws094-p015（条件つき） | Files の frame の Vulkan の呼び出しの削減 | 着手せず（速さは F-072、Phase の directory なし） |

## 記録の所在

Phase の計画・結果・試験（`phaseNNN/`・`tests/`）と作業の手引き（`guide.md`）は削除する（Q1 の pipeline）。git の commit `fb1f6afee` の `plan/ws094/` にある。Queue の履歴は [plan/history](../history/index.md)。
