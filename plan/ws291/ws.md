<!-- awesome-plan project=zedbsd record=ws291 -->

# WS291 AMD RDNA（amdrdna）の描画の速さ: desktop 60 fps と重い 3D で Linux 以上

- Master: [plan/master.md](../master.md)（2026-10-26 23:05 作成）。Primary Milestone: MG006。前提: [WS039](../ws039/ws.md)（amdrdna の driver、display・executor・desktop は実機で動く）。手本: [ws270-p007](../ws270/phase007/phase.md)（VF2 の PowerVR の desktop の速さ）。
- Status: incomplete（2026-10-26 23:05 新設、p001 cleared）
- 出典: 2026-10-26 23:05 ユーザー「P2も立てます。ssh 10.0.10.3で、RDNAの接続されたDebianに入れます。USBメモリにはzedBSDが入っています。この環境でカーネルをアップデートしてUEFIの起動項目を変更しながら再起動し、amdrdnaドライバの改善を行ってほしいです。要領としては、PowerVRのドライバの改善のときと同じようにコンポジタを操作して時間を計測し、改善できる部分を改善してほしいです。デスクトップで60fpsをまずは達成します。スクリプトは既存のものがあると思います。もしかしたらすでに60fpsになっているかもしれません。そのあと、Vulkan Model Viewerを使ったり、その改良版を作ったりして、重たい3DシーンをLinuxドライバと同じfpsで描画できるように調整します。一つのやり方として思うのは、Linuxで60fpsを切るような重さに3Dモデルの量を調整し、たとえば50fpsにします。これをzedBSDでも50fps達成できるように調整し、できれば55fpsくらいまで、Linuxよりよくなるまでプロファイルベースで改善します。できそうですか？」

## 単一の到達目標

実機 10.0.10.3（Navi10 の RX 5600 XT、Debian 13 と USB の zedBSD）で、(1) Keiland の desktop の操作（ws270-p007 の frame-ops の段）が 60 fps、(2) Linux（Debian の RADV）で約 50 fps になる重さの 3D の scene（mview か改良版）を、zedBSD の amdrdna で Linux と同じ fps、できれば 55 fps 前後の Linux 超えまで、profile を見て改善する。

## 手順と道具

- 実機の操作: Debian（ssh 10.0.10.3）から USB の zedBSD の /boot に vmunix・package を入れ、`efibootmgr --bootnext`（zedBSD USB の Boot0001 か KIOXIA の項目）で再起動、終わったら Debian へ戻る（zedBSD の BootOrder は変えない、BootNext の 1 回だけ）。zedBSD の側は ssh か serial の対話。console・serial の log で判定しない。画面の判定は keiland-shot の PNG。
- 計測: `plan/tools/frame-ops/`（frame-ops.sh・frame-times.py・key-latency.py・shot-diff.py）、mview の `--spin=N`（fps の報告）。Linux の基準は Debian の上の同じ mview（Wayland の上の Vulkan、RADV）を同じ解像度・同じ model で。
- 試験の方針（AGENTS.md）の例外: 実機の計測はこの WS の本体なので担当が直に行う（WS039 と同じ）。QEMU は使わない。

## 受け入れ

- desktop: frame-ops の各段が 60 fps（ws270-p007 の基準の表と同じ形で記録）。
- 3D: Linux で約 50 fps の scene を定め（model・数・解像度・shading を記録）、zedBSD の fps が Linux 以上。各改善の前後の数値と profile を記録。
- amd64 の vmunix・変えた userland が warning 0。実機の証拠と未実施の事を分けて記録。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | desktop の計測と 60 fps（既に 60 fps なら記録して閉じる） | WS039 p009 | cleared（2026-10-26、main bad29b52f、全段 60 fps） |
| [p002](phase002/phase.md) | Linux で約 50 fps の 3D の scene を決める（mview の拡張を含む）、zedBSD との差を測る | p001、p002a | in-progress（Linux 50.0 fps: qs40 ×6500 instanced、MAILBOX、1920x1080。command の重さの組 big-N・subs-N も測った） |
| [p002a](phase002a/phase.md) | amdrdna の executor に depth・index の draw・mip の texture（mview が動くまで） | p002 の Linux の基準 | planned |
| p003〜 | profile による改善（p002 の結果で立てる） | p002 | — |
| 最後 | 規約の全文の見直し | 実装の全部 | — |
