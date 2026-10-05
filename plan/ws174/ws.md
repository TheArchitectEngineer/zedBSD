<!-- awesome-plan project=zedbsd record=ws174 -->
# WS174: 起動時の Ctrl で safe boot options に切り替える

Master: [master](../master.md)
Status: planning（2026-10-05 夜 追加。設計だけ）
Primary Milestone: MG003
Related: MG006（graphical boot）

## 由来

ユーザー（2026-10-05 夜）「おっと、イメージを実機で起動したら、カーネルの起動の途中でpanicしたとみられますが、グラフィカルブートなのでわかりません。ブート時にctrlキーが推されていたらsafe boot optionsの設定に切り替えるように、ブートローダとブートコンフィグファイルを変更したいです。設計だけできますか？」

## 目標

boot の時に Ctrl が押されていたら、bootloader が boot の config の safe boot options（text の kmsg・graphical login なし など）に切り替えて kernel を起動し、panic などが画面で読める。

## Phase

| Phase | 内容 | 状態 | 依存 |
| --- | --- | --- | --- |
| [ws174-p001](phase001/phase.md) | 設計（bootloader の Ctrl の検出、config の safe の節の形、kernel への渡し方、docs） | in-progress（設計だけ、2026-10-05 夜） | — |
