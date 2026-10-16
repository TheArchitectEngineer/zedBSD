# WS253 起動の時間の短縮: desktop を先に、network などは後に

- ID: ws253、Master: [master.md](../master.md)、Primary Milestone: MG001
- Status: incomplete（p001: q1221-i01・i02 で実装と QEMU の確認は済み、5330 の素の起動の時間の確認が残る）
- 起点（2026-10-16 ユーザー）:「5330はデスクトップが起動しました。OKです。ただ、起動に1分くらいかかってました。起動時間がめちゃくちゃ長くなってます。これはIntelは関係なく、全般的な話です。これを解決するためのWSを作り、サブエージェントでQEMUを使って解析して、直してください。networkのupを待つ処理のせいかも？それだけでなく、init立ち上げるサービスって、デスクトップセッションを早めに起動するのがいいと思うんです。ネットワークはあとからでいい。もちろん、デバッグ用の起動ではそうではないので、デバッグ時だけ特別扱いしましょう。通常ブートではネットワークは後回し。audiod, networkd, sshdなど大半は後回しで、とにかくsessiondでWaylandを立ち上げる。どうしても依存のあるサービスは教えてください。」

## 目的

通常の起動（graphical login）で、電源から login 画面（sessiond の Wayland）までの時間を短くする。

## 完了の条件

1. QEMU で、起動の各段（kernel → init → 各 service → sessiond → Wayland の login 画面）の時刻を測った表がある（直す前と後）。
2. 通常の起動では sessiond（Wayland の login 画面）が network・audio・sshd などを待たずに先に立ち上がり、それらは後から立ち上がる。
3. debug の起動（kmsg=console・login=console、loader の Ctrl/Shift）では今の順序（network などが先）を保つ。
4. 通常の起動で desktop に要る service（どうしても依存のある物）を一覧にし、理由を添えてユーザーに報告する。
5. QEMU の起動（plan/tools/boot-test.sh）で graphical の login 画面が出る。5330 の素の起動での時間の確認は T（P1 の i915 の回帰と相乗り）で。

## 制約

- QEMU の console・serial の log を判定に使わない。時刻は guest の中の記録（init・sessiond の記録を ssh で読む等）か画面で測る。
- HAL の API・toolchain は変えない。rm は Q1。

## Phase

| Phase | 目的 | 状態 | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 測定・原因の特定・init の service の順序の変更（通常は sessiond を先に、debug は今のまま）・依存の一覧 | in-progress（q1221、実装済み・5330 の確認待ち） | なし |
| p002 | 規約の全文の見直し | planned | p001 |

## 2026-10-16 5330 の測定（T1-703、main 1f22728f5、warm 2 回）

- desktop shown: 1 回目 24.7 s（dd の直後の最初の起動、VFS の初期化 13 s）、2 回目 6.7 s（VFS 0.7 s、kernel→init 1.9 s）。reboot の命令から ssh まで 60.9 s・37.4 s（Linux の終了・UEFI の起動を含む）。
- 2 回目の残り: compositor の wallpaper 1.8 s・glyphs-fonts 1.6 s（USB の読み込み、BUG-327）、networking 約 10 s（desktop の後）。
- 出力: /home/awe/zedBSD-worktrees/t1/build/t1703-out/
