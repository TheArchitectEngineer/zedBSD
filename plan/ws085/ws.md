<!-- awesome-plan project=zedbsd record=ws085 -->

# WS085: Windows版QEMUのVenusでデスクトップを表示する

Status: incomplete
Primary Milestone: MG006
Related Milestones: MG001
Parent: [Master](../master.md)
Queue: [q499](../queue.md)
Resume point: p001。Windows rendererのstrict7とゲストのcopy表示でデスクトップとランチャーメニューがSDLに現れた。表示所有者の終了後にQEMUが応答しなくなる問題とcopy表示の遅さが残る。

## 目標と境界

ユーザーの2026-09-29指示「Windowsで動くように修正してみてください」に基づき、Windows版WINQ-EMU Alpha10で既定のamd64 imageのデスクトップを表示する。Linux hostの実証済みVenus経路を維持する。HALとtoolchainは変更しない。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [ws085-p001](phase001/phase.md) | paired Windows rendererの同期契約と、blob scanoutを表示できないhostでの既存copy表示への切替を検証 | in-progress | — |
