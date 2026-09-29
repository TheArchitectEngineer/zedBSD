<!-- awesome-plan project=zedbsd record=ws085 -->

# WS085: Windows版QEMUのVenusでデスクトップを表示する

Status: incomplete
Primary Milestone: MG006
Related Milestones: MG001
Parent: [Master](../master.md)
Queue: [q499](../queue.md)
Resume point: p001。Windows QEMUのmapped blob scanoutでデスクトップを確認済み。SDL→仮想USB HIDの10指タッチを追加し、QMPの2指注入でメニュー起動を確認。Files起動停止の原因だったWindowsの共有画像通信のpadding・fd所有権を修正し、Files開閉・再起動とTerminal同時起動を確認。物理タッチ入力と所有者切替の確認待ち。

## 目標と境界

ユーザーの2026-09-29指示「Windowsで動くように修正してみてください」と、その後の`vendor/`へのforkソース導入・直接修正許可に基づき、Windows版WINQ-EMU Alpha10で既定のamd64 imageのデスクトップを表示し、copy表示の速度問題を調べ、Windows SDLの指入力で人がタッチUIをデバッグできるようにする。Linux hostの実証済みVenus経路を維持する。HALとtoolchainは変更しない。vendorの変更はユーザーがレビューしてcommit/pushする。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [ws085-p001](phase001/phase.md) | paired Windows rendererの同期契約、copy fallback、mapped blob scanout、SDLマルチタッチ入力を検証 | in-progress | — |
