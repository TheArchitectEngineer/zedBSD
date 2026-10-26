# WS255 試験機の zedBSD の試験の image を静的な IP にする

- ID: ws255、Master: [master.md](../master.md)、Primary Milestone: MG001
- Status: canceled（2026-10-27 ユーザーの回答（クリック）「取り下げ」: WS254 の DHCP の取り直しで足りる。BUG-328 は Bug として残す）。旧: incomplete・保留（BUG-328 待ち）（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001 の手引きと QEMU は PASS。static の default route と DNS が起動の後に入ら…
- 起点（2026-10-16 ユーザー）:「さらに、テスト機ではstatic IPにすることで、自律ループを維持しやすくできると考えます。」
- 対象: 5330（Latitude 5330、zedBSD の試験の起動、今は DHCP で 10.0.30.3）と RDNA 機（RX 5600 XT、zedBSD は DHCP で 10.0.30.5）。Linux・Debian の側の設定は変えない。

## 完了の条件

1. 試験の image を作る時に、機械ごとの静的な IPv4（address・prefix・gateway・DNS）を入れる手段がある（試験の config.mk と個別の file の複写、または boot の parameter）。通常の image と release の image は DHCP のまま。
2. 5330 と RDNA 機の値を決めて（今 DHCP が渡している address、prefix・gateway・DNS は各機械の lease の記録から）、P1 の 5330-bootcycle.sh・P3 の hw-run.sh が使う image の作り方に入れる手引きを書く（他の WS の script の変更は Q1 経由）。
3. QEMU で静的な設定の image が DHCP 無しで ssh に応える。

## Phase

| Phase | 目的 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 静的な IP の入れ方・2 台の値・手引き・QEMU の確認 | in-progress（q1227、QEMU PASS、gateway・DNS の依存で Q1 の判定待ち） |
