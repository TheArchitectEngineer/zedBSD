<!-- awesome-plan project=zedbsd record=ws156 -->

# WS156: app の通知（画面の下の中央を流れる headline の popup と、ring の形の通知の log）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」。根拠: p001〜p004 cleared（popup と ring の log、T1-269・T1-375b）、実機の UAT は 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」、p006（Linux の D-Bus）は 2026-10-14 Q1 の判定で T1-588 の Debian の通知）
<!-- retained-after-completion --> phase001 の phase.md は残す: userland/desktop/ の通知の source（include/keiland/keiland.h:1557、libkeiland/system/kl-system-protocol.h:608、wayland/notify*.c・notify*.h・media.c・network.c、libkeiland-backend-linux/notify-linux.c、libkeiland-backend/unsupported/notify-unsupported.c）の comment が設計の節を仕様として参照している。source の comment を直す（または仕様を docs/ へ移す）までの間
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

app から通知を出せる仕組みを作り、画面の下の中央を headline の news の板のように流れる popup で見せ、消えた後も hotkey で ring の形の log を左右にたどれるようにする。

## ユーザーの決定

- 2026-10-04 夜「既存の右上の通知領域ではなく、アプリ通知の実装についてです。WindowsやMacだと、通知領域近辺にポップアップが出て、そのあとは画面右側に通知が一覧になって出ると思います。ポップアップは我々も出そうと思いますが、画面中央の下部に、画面サイズの20%程度のポップアップにしたいです。また、画面右下に、画面右端から出てきて、フェードインしながら画面中央にしゅっと素早く移動して、3秒とどまったら、画面左端にしゅっと素早く移動してアルファ値もフェードアウトして消えていくのがいいです。ヘッドラインニュースのボードみたいなものをイメージしています。通知は消えたあとも、ホットキーでリング上に左右に移動してログを見られるといいですね。通知は表示中はバツボタンで消せるといいです。ログの端、最新まで来ると、通知をすべて消去、というボタンがあるといいです。個別に消した通知はログに残さない方がいいです。」
- 2026-10-05「Linuxではlibkeiland-backendにD-bus機能を入れて通知を取ればいいですね。実装はあと回しでいいです。設計だけ記録してください。」
- 2026-10-05 夜: 設計の H1〜H7 をユーザーが決定（p001）。
- 2026-10-08 ユーザー: 規約の全文の Phase はベータ3 → p005 の規約は WS の外の一括の見直しへ。
- 2026-10-13「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」

## 結果

- 通知の口（Keiland の拡張の protocol `kl_system_notify_v1` と libkeiland の `kl_*notify*`）、compositor の popup（右から中央、3 秒、左へ fade-out、× で消す、全画面・lock の扱い、system の通知）、hotkey の ring の log（すべて消去、個別に消した物は残さない）、Linux の libkeiland-backend の D-Bus の `org.freedesktop.Notifications` が main にある。2026-10-10 に WS197 p004c が lock_text（ロック画面に出す文）を足した。
- 証拠: QEMU の T1-269（p002）・T1-375b（p003・p004、20 項目 ok）、Debian の QEMU+KVM の T1-588（p006）。実機（5330）は 2026-10-13 のユーザーの一括の cleared（個別の観察の記録は無い）。

## 制限・移管

- FreeBSD の D-Bus の通知は後（範囲の外）。
- p006 の phase.md は test-wait のまま（判定は 2026-10-14 Q1 で cleared 相当）。
- 規約の全文（p005）は WS の外の一括の見直しへ。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws156-p001 | 設計（通知の口・内容・popup の動き・log と hotkey・保存・試験） | cleared（H1〜H7 はユーザーが決定） |
| ws156-p002 | 通知の口（protocol・libkeiland）と compositor の受け取り | cleared（T1-269） |
| ws156-p003 | popup の描画と動き、× で消す、全画面・lock、system の通知 | cleared（T1-375b） |
| ws156-p004 | log（hotkey、ring の左右、すべて消去） | cleared（T1-375b） |
| ws156-p005 | 全文の規約と QEMU の回帰、実機の UAT（directory なし） | uncleared。実機の UAT は 2026-10-13 の一括、規約の全文は WS の外の一括の見直しへ |
| ws156-p006 | Linux: libkeiland-backend の D-Bus の通知 | phase.md は test-wait のまま。2026-10-14 Q1 の判定: T1-588 の Debian で D-Bus の通知・popup・log・NotificationClosed |

## 記録の所在

phase001 は上の理由で残す。他の Phase の記録と試験は削除する（Q1）。git の commit `fb1f6afee` の `plan/ws156/` にある。Queue の履歴は [plan/history](../history/index.md)。
