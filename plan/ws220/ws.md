<!-- awesome-plan project=zedbsd record=ws220 -->

# WS220: TCP の転送の速さの安定（継続の転送ですぐ 1 MB/s 未満に落ちる、window の制御の直し）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: 落ちる仕組みの直し p003〜p005 は main で cleared。未着手の p006・p007 は 2026-10-28 ユーザーの回答「WS221 へ移す (推し)」で WS221 の p008・p009 へ）
<!-- retained-after-completion --> tests/ は残す: `userland/tests/tcp-stream`（Makefile・main.c の comment）が `plan/ws220/tests/tcp-stream-series.sh` を名指し、WS221 p008・p009 が模型と時系列の道具を使う
Completed: 2026-10-27（q1608）。2026-10-28 に残りの Phase を WS221 へ移した（q1660）
Primary Milestone: MG005
Related Milestones: なし
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

TCP の送り・受けの制御（`src/kern/net/tcp.c` ほか）を直し、継続の転送で速さが落ちず、損失があっても回復して戻る。

## ユーザーの決定

- 2026-10-13（原文）「WSを作ります。TCPのコントロールが不安定です。ウィンドウ制御がよくないと思われ、継続した転送で、TCPの速度がすぐに1MB/s未満になります。これを解決します。」
- 2026-10-13「TCPですが、60秒以上の転送も計測してください。そうでないとTCP Windowの実装の問題が見えないです。」→ T1 の測りは全部 60 秒を超える転送を含める。
- 2026-10-13（p001 の後）「はい、推奨でいいです。」→ QEMU の受け入れは usb-net（CDC ECM）のまま。RTO の最小は 200 ms（RFC 6298 の式に最小の項 200 ms を入れる形）。
- 2026-10-13「遅くなった時の向きは受信です」「接続を諦めるまでの時間は延ばしてよいです。」→ 症状は受けの向き。SYN の再送を諦めるまでは RFC 1122 の R2 に合わせて最低 3 分。
- 2026-10-13（ws220-p005 review-1 M7・M8、ユーザーの決定）「FreeBSD と同じ＋p006 で pool を切り離す (Recommended)」→ 窓 0 の相手は FreeBSD と同じ persist と `TCP_MAXUNACKTIME`（既定 0）。答え続ける相手が pool を握る問題は送りの buffer の切り離し（旧 p006(b)、今は WS221 p008）で解く。
- 2026-10-13 夜（AskUserQuestion）「main に残し、直しを急ぐ」。
- 2026-10-28 ユーザーの回答（クリック）「WS221 へ移す (推し)」→ 未着手の p006（輻輳の制御と送りの buffer、`tcp_device_mss` の持ち主）・p007（受けの窓）を WS221 の [p008](../ws221/phase008/phase.md)・[p009](../ws221/phase009/phase.md) に移した。

## 結果

- 測りの道具: kernel の `net.tcpstats`（sysctl、508d33db9）、長い転送の時系列 `tests/tcp-stream-series.sh`、本物の tcp.c の host の模型 `tests/tcp-model/run.sh`、`userland/tests/tcp-stream`（塞がる送り手）。
- 直し（main）: p002 正しさ（sequence の剰余の比較、SND.WL1/WL2、重なりのある segment、諦めの時間、RFC 5961、c13a8413f）、p003 送りの packet の不足、p004 RTT と RTO（RFC 6298）、p005 fast retransmit・NewReno・persist。
- QEMU の証拠（T1）: T1-581（同じ session の基線より全部の値で良い）、T1-577、T1-585（損失 0.1 % の up 3.66 MB/s、損失 1 % 完走 2.89 MB/s、60 秒を超える損失 0 の転送で後半/前半 97〜100 %）。実機の測りは未実施。

## 制限・移管

- p002 の QEMU の確かめ（T1）は未実施。
- 輻輳の制御・byte の送りの buffer・`tcp_device_mss`（旧 p006）と受けの窓（旧 p007）は WS221 の p008・p009。
- 受けの SACK（旧 p008 の候補）は行き先が無いまま閉じた。
- USB LAN の遅さは [BUG-222](../bugs/BUG-222.md)。TCP の ISN と SYN flood は Future Work の F-100・F-101。
- 規約の全文は WS の外の一括の見直しへ。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws220-p001 | 調べと測り、道具、host の模型、直しの設計 | in-progress のまま WS を閉じた（設計と道具は済み、T1 の基線と design-reviewer は未） |
| ws220-p002 | 正しさ（剰余の比較・WL1/WL2・重なり・諦めの時間・RFC 5961） | in-progress のまま WS を閉じた（実装は main c13a8413f、QEMU の確かめは未実施） |
| ws220-p003 | 送りの packet の不足 | cleared（T1-581） |
| ws220-p004 | RTT の推定と RTO（RFC 6298） | cleared（T1-581・T1-577） |
| ws220-p005 | fast retransmit と NewReno、persist | cleared（T1-585） |
| ws220-p006 | 輻輳の制御と送りの buffer、相手の MSS、Nagle | WS221 p008 へ移した（2026-10-28 ユーザー） |
| ws220-p007 | 受けの窓、SWS 回避、window scale、delayed ACK | WS221 p009 へ移した（2026-10-28 ユーザー） |
| ws220-p008 | 受けの SACK（候補） | 候補のまま閉じた（行き先なし） |
| ws220-p009 | 規約の全文の見直し | WS の外の一括の見直しへ |

## 記録の所在

Phase の計画・設計・review は削除した。git の commit `fb1f6afee` の `plan/ws220/` にある（直しの設計は `phase001/phase.md` §4）。試験の道具は [tests/](tests/) に残す。Queue の履歴は [plan/history](../history/index.md)。
