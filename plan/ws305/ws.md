<!-- awesome-plan project=zedbsd record=ws305 -->

# WS305 デスクトップの GPU の passthrough の試験環境（Linux ＋ VFIO ＋ QEMU）

- Master: [plan/master.md](../master.md)（2026-10-27 10:55 作成）。Primary Milestone: MG006。関係: [WS291](../ws291/ws.md)（RDNA の速さ、P2 は素の起動で作業中）、WS039（amdrdna）、WS037・WS242（nvrtx）、[plan/tools.md](../tools.md)「実機」。
- Status: incomplete（2026-10-27 10:55 新設、11:00 P4 が q1619-i01 で p001 に着手）
- 出典: 2026-10-27 10:55 ユーザー「デスクトップマシンはデバイスドライバの開発でLinux+パススルーで使っていこうと思います。」→（Q1 の提案「passthrough の環境を用意する作業を立てませんか？」）「はい、パススルーの環境を立てていいと思います。ただ、今回の作業はコールドリセットが必要かもなあとも思います。使い分けていきましょう。」

## 単一の到達目標

デスクトップ（10.0.10.3、Debian、AMD RDNA か NVIDIA RTX を差し替え）で、GPU を vfio-pci で QEMU の guest の zedBSD に渡し、guest が止まっても host の Linux から VM を止めて立て直せる試験環境を作る。素の起動（BootNext で USB の zedBSD、SwitchBot の cold reset）と使い分ける（ユーザー）。

## 制約

- Debian の既定の起動と、P2 の Linux の基準の計測（amdgpu・RADV）を壊さない: GPU を vfio-pci に結ぶのは試験の時だけ（実行時の unbind/bind、または grub の別の項目）。IOMMU の有効化など起動の引数を変えるなら、変える前に Q1 に案。
- 素の起動の作業（P2）と同じ機械なので、使う時間は Q1 が調整する（lock の file を決める）。
- 判定は SSH と QMP の screendump の PNG（AGENTS.md の検証の例外の形）。console・serial の log では判定しない。
- 遠隔の電源は SwitchBot（AC276E42B252、Q1 が入れる）。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 調査と設計: IOMMU の group、GPU（と HDMI audio）の分離、reset の方法（RDNA1 の reset の問題、vendor-reset の要否）、QEMU の構成（OVMF、q35、guest の image、display の取り出し）、script の形、素の起動との使い分けの手順 | — | in-progress（設計は Q1 の OK、host の事実は未読） |
| p002 | 環境の構築と script（vm-start・stop・reset・screendump・ssh）、RDNA で guest の zedBSD の amdrdna が付き desktop が出る | p001 | planned |
| p003 | RTX でも同じ（GPU を差し替えた時） | p002、ユーザーの差し替え | planned |
