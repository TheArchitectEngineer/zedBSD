<!-- awesome-plan project=zedbsd record=ws099 -->

# WS099: Keiland の compositor（zdesktop）のデモの基準

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。p022 以外の Phase は cleared（p012 は 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」）、fg010 の OSC のデモ（2026-10-17）で compositor を使った。p006 は C6 の記録だけ（速さは F-072））
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

WS035 を閉じた後継。2026-09-30 ユーザー「WS099のゴールも、明確な達成基準がないような気がします。それはFGに入れて、WSでは、このソフトがこういう基準を満たす、という明確なゴールを設定したいです。ソフトごとにそれをWSで作りましょう」→ デモの台本は fg010 に移し、この WS の対象は compositor の zdesktop（`/bin/wayland`、`userland/desktop/wayland/`）と、それが起こす greeter・session の遷移だけ。

## 達成基準（2026-09-30 main の案、同日朝ユーザーが「案のまま確定」）

全て QEMU の Venus の自動の試験で確かめ、（実機）の付いたものは 5330 でも確かめる。

| # | 基準 |
| --- | --- |
| C1 | 起動から greeter、login からデスクトップ、Log Out から greeter、Shut Down の各遷移で、黒い画面と文字の console が 0 枚（実機） |
| C2 | 窓の移動、四隅と四辺の resize、最大化と戻し、最小化と戻しが意図した位置と大きさになる |
| C3 | 全画面・最大化を解いた窓は、title bar が system bar に重ならず drag できる（BUG-114 の形） |
| C4 | 窓を閉じたとき、同じ desktop の次の窓に keyboard の focus が移る |
| C5 | App Home と Wiseview の開閉が窓 10 個でも止まらず、開閉の最初の frame まで 100 ms 以内（実機） |
| C6 | 窓 10 個で pointer の移動から表示まで中央値 50 ms 以内（実機） |
| C7 | すりガラスの上の文字の contrast が、既定と生成の 5 枚の壁紙の全てで 4.5:1 以上（WCAG AA） |
| C8 | Terminal の窓は本体の四隅が直角、title bar は丸い。他の窓は両方丸い |
| C9 | 基準の一覧に載せた zdesktop の回帰の試験が全て PASS |
| C10 | 1 時間の連続の操作で zdesktop が落ちず、`ZWL ERROR` が 0 |

## ユーザーの決定

- 2026-09-30 昼（Notes の全画面の出口）「画面を下からSwipeで戻しましょう。また、全画面でコンポジット無効のモードになっているなら、それは使わないように修正して、コンポジットを有効にした上で、スワイプ操作を可能にします。」→ p015。
- 2026-09-30 の方針「広く浅く」: まず動く段をそろえ、磨き込みは段ごとの数値目標の小さな Phase。
- 2026-10-02: WS099 の優先度を上げる。BUG-125 は WS099 の blocking、WS099 を担当するエージェントが直す。C1 などの実機の目視確認はユーザーに依頼する。
- 2026-10-02「窓が多いときの遅延の基準（50 ms）はそのままでいいです。理由があれば見直していいです。BUG-127 が再現しないとき、tracking のままで WS099 を閉じてよいです。」
- 2026-10-02: テスト背景の source を zedBSD・Linux・FreeBSD 共通に収録し、旧画像のぼやけた湖を起動の既定に、収録済みの背景は切り替えで維持（p019）。
- 2026-10-03「Q1の判断で閉じられるものは閉じてください。」→ p017・p020 cleared、BUG-125 resolved。
- 2026-10-03「C5の200msは問題視しません。clearでOKです。理由は、あとでパフォーマンス改善のチケットを作ってまとめて改善するからです。」→ QEMU の C5 は PASS 扱い、速さは F-072。
- 2026-10-04: タイトルバーの検索欄・menu の項目の drag で窓を動かす（p030）。上部の system bar の高さを窓の title bar に揃える（p031）。WiFi の icon の Alt+クリックで IP address と統計の popup（p032）。
- 2026-10-05 午後 UAT「ウィンドウを最大化している状態で、新たにアプリを起動したら、そのアプリは最大化しているのがいいです。タブレットをスクリーン全体で使っているという認識にします。」→ p033。
- 2026-10-06: 上部の bar のデザイン（画像 4 枚）→ p034。light の外観の bar は窓の title bar と同じ色味、dark は今の黒のまま（p034b）。
- 2026-10-06（クリック）「Clearにします。」→ p021（C2 の geometry の不一致と BUG-127 は再現せず非阻害）。
- 2026-10-08「規約の整形（各 WS の全文規約の Phase）はベータ3」→ p022 は WS の外の一括の見直しへ。
- 2026-10-13「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」→ p012。

## 結果

- 基準の一括の試験 `criteria.sh`（C1〜C10）を作り、QEMU の Venus で C1〜C5・C7〜C10 を PASS にした（C5 の QEMU の最初の開きの約 200 ms はユーザーの判断で PASS 扱い）。
- compositor の直し: C5 の最初の frame（5330 で 34 回とも 100 ms 以内、最大 55 ms、p002）、C7 のガラスの明るさの下限と副次の文字の色（72/72 が 4.5 以上、p005）、client の cursor の範囲（BUG-118、p007）、greeter の Shut Down・Restart の表示（p009）、sessiond の起こし直しの延長（BUG-122、p010）、窓の角の drag で窓が消える件（BUG-121、5330 で 20 回中 6 回 → 0、p011）、全画面を常に合成して下からの swipe で戻す（p015）、buffer の import の短縮（p016）、BUG-125（p020）、入力の時刻の順の merge と hover の leave（BUG-142・BUG-141、p029）、log の NUL（p028）、oneshot の poweroff と init（BUG-095、p027）。
- UI: 既定の壁紙（ぼやけた湖、3 OS 共通、p019）、title bar の欄の drag（p030）、system bar の高さ（p031）と WiFi の詳細の popup（p032）、最大化の中の新しい窓（p033）、上部の bar のデザイン（p034・p034b）、App Home の stage と animation（p035）、Power Off の確認の dialog（p037）、dock の時の 4 px の余白（p038）、Gears・Terminal のタイトルバー（BUG-136・BUG-137、p023）。
- 試験の側: 固定の sleep を `ZWL READY` 待ちに（BUG-115、p003）、起動の検出（BUG-147、p024・p026）、client の番号（BUG-146、p025）。
- C10 は 5330 の i915 passthrough で 3602 秒・278 周・errors 0・restarts 0（q578、p014）。C1 の 5330 の目視は 2026-10-13 のユーザーの UAT で cleared 扱い。

## 制限・移管

- C6（実機の pointer の遅延 50 ms）は記録だけ（p006）、速さは F-072。
- USB の素の起動での C10 は未検証。BUG-122 の 5330 の元の失敗（`ZWL EXIT error=21`）は QEMU で再現できず。
- 規約の全文と最後の回帰（p022）は WS の外の一括の見直しへ。
- 2026-10-04 UAT の BUG-166（タッチパッドの押し込みの drag）、2026-10-06 UAT の再設計の候補（BUG-217・BUG-223・BUG-232・BUG-235・BUG-219・BUG-221）は Bug Board の各 ticket。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws099-p001 | C1〜C10 の一括の試験と不足の一覧 | cleared（2026-09-30、QEMU） |
| ws099-p002 | C5: App Home・Wiseview の最初の frame | cleared（2026-09-30、5330 と QEMU） |
| ws099-p003 | BUG-115: 試験の固定の sleep を `ZWL READY` 待ちに | cleared（2026-09-30） |
| ws099-p004 | C1: 起動と Shut Down の替わり目の試験 | cleared（2026-09-30、QEMU） |
| ws099-p005 | C7: client のガラスの上の文字の contrast | cleared（2026-09-30、QEMU） |
| ws099-p006（案） | C6: 実機の pointer の遅延の計測 | 着手せず（記録だけ、速さは F-072。Phase の directory なし） |
| ws099-p007 | BUG-118: client の cursor の範囲 | cleared（2026-09-30、QEMU） |
| ws099-p008 | BUG-119: Shut Down で電源が切れない件の確認と案 | cleared（2026-09-30、確認と案まで。実装は ws073-p043） |
| ws099-p009 | greeter の「Shutting down...」「Restarting...」 | cleared（2026-09-30、QEMU） |
| ws099-p010 | BUG-122: compositor が落ちた後の greeter の回復 | cleared（2026-09-30、QEMU。5330 は未確認） |
| ws099-p011 | BUG-121: 窓の角の drag で窓が消える | cleared（2026-09-30、5330 と QEMU） |
| ws099-p012 | C1 の 5330 の目視、BUG-119・BUG-122 の 5330 の確かめ | cleared（2026-10-13 ユーザー「UATの結果待ちのものは…clearedに…」） |
| ws099-p013 | BUG-119 の電源断の実装 | 不要（ws073-p043 で済んだ。Phase の directory なし） |
| ws099-p014 | C10: i915 passthrough の 60 分の連続操作 | cleared（q578、5330 の passthrough） |
| ws099-p015 | 全画面を常に合成し、下からの swipe で窓に戻す | cleared（2026-09-30、QEMU） |
| ws099-p016 | zdesktop の buffer の import の短縮 | cleared（2026-09-30、QEMU。5330 は未実施） |
| ws099-p017 | BUG-125 の再現と試験の同期の切り分け | cleared（2026-10-03 Q1） |
| ws099-p019 | 壁紙の共通の収録と既定のぼやけた湖 | cleared（2026-10-06 Q1、T1-220） |
| ws099-p020 | BUG-125 の原因と compositor の直し | cleared（2026-10-03 Q1） |
| ws099-p021 | C2 の geometry と BUG-127 | cleared（2026-10-06 ユーザー「Clearにします。」） |
| ws099-p022 | 全文規約と回帰 | planning のまま WS を閉じた（規約の全文は WS の外の一括の見直しへ） |
| ws099-p023 | BUG-136・BUG-137（タイトルバー） | cleared（2026-10-08 Q1 の判定） |
| ws099-p024 | BUG-147: 試験の app の起動の検出 | cleared（2026-10-03 Q1） |
| ws099-p025 | BUG-146: guest 試験の client の番号 | cleared（q645、T1-039） |
| ws099-p026 | BUG-147 の残り: criteria.sh の guest の起動の確かめ | cleared（q645、T2-002） |
| ws099-p027 | BUG-095: oneshot の poweroff と init の待ち合い | cleared（q645、T2-003） |
| ws099-p028 | zdesktop の log の NUL | cleared（2026-10-03 Q1、T1-035） |
| ws099-p029 | BUG-142・BUG-141（入力の順と hover の leave） | cleared（q645、T2-008） |
| ws099-p030 | title bar の欄・menu の項目の drag で窓を動かす | cleared（q670、T2-026） |
| ws099-p031 | system bar の高さを title bar に揃える | cleared（2026-10-05 Q1） |
| ws099-p032 | WiFi の icon の Alt+クリックの詳細の popup | cleared（2026-10-05 Q1、T1-125） |
| ws099-p033 | 最大化の中で新しく開く窓を最大化で | cleared（2026-10-05 Q1、T1-187b） |
| ws099-p034 | 上部の bar のデザイン（第 2 版 p034b） | cleared（2026-10-08 Q1 の判定、T1-228） |
| ws099-p035 | App Home の stage と 2 層の animation | cleared（2026-10-08 Q1 の判定、T1-229・T1-231） |
| ws099-p037 | App Home の Power Off と確認の dialog | cleared（2026-10-06 Q1、T1-225） |
| ws099-p038 | dock の時の中身の rect の 4 px の余白 | cleared（2026-10-08 Q1 の判定、T1-264・T1-277） |

## 記録の所在

Phase の計画・結果・試験（`phaseNNN/`・`tests/`）と作業の手引き（`guide.md`）は削除する（Q1 の pipeline）。基準の試験（`criteria.sh` ほか）は plan/tools/ へ移す案（Q1 の判断）。git の commit `fb1f6afee` の `plan/ws099/` にある。Queue の履歴は [plan/history](../history/index.md)。
