# ws253-p001 起動の時間の測定と service の順序の変更

- 親: [WS253](../ws.md)、Queue: q1221-i01、状態: in-progress
- 範囲: WS253 の完了の条件 1〜4（5 の QEMU の起動の確認を含む）。
- 手順:
  1. QEMU で今の main の image を起動し、段ごとの時刻を測る（guest の記録・画面。console/serial の log は使わない）。何が待っているか（networkd の notify-timeout・DHCP の待ち・after= の連鎖・sessiond の GPU の待ち等）を特定する。
  2. init の service の定義（userland/base/init/services/、rc.conf の既定）と init・sessiond を直し、通常の起動では sessiond を先に、networkd・audiod・sshd・ntpdate・cron などを後に立てる。debug の起動（kmsg=console・login=console・Ctrl/Shift）では今の順序。
  3. 直した後の時刻を測り、表にする。boot-test.sh で graphical の login を確かめる。
  4. desktop がどうしても依存する service の一覧と理由を書く。
- 結果: （未）
