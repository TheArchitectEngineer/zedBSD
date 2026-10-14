<!-- awesome-plan project=zedbsd record=ws239 -->

# WS239: Wayland（compositor）の起動の失敗時に text console の login へ戻す

- Status: planned（2026-10-15 Q1）
- Master: [master.md](../master.md)
- Primary Milestone: MG002（ベータ2）
- 由来: 2026-10-15 ユーザー「あと、Waylandが起動に失敗したとき、/dev/systemを通じて、カーネルメッセージをコンソール出力につなぎ直し、グラフィカルブートを解除してttyのログインを表示してほしいです。」
- 目的: graphical boot（login=graphical、kmsg=quiet）で compositor が起動に失敗した（起動しない・すぐ終わる・繰り返し失敗する）時に、(1) `/dev/system` を通して kernel の message の出力を console へつなぎ直し（kmsg を console に）、(2) graphical boot を解除し（boot の logo・graphical の login をやめる）、(3) tty の login（getty）を表示する。
- 完了の条件: 失敗を模した起動（compositor を無くす・壊す）で、画面に kernel の message と tty の login が出て login できる。正常な graphical boot は変わらない。
- 制約: 既存の `/dev/system` の操作を使い、無ければ最小の操作を足す（kernel の ABI の追加は Q1 に報告）。HAL の API は変えない。

## Phase

| Phase | 内容 | 状態 | 依存 |
| --- | --- | --- | --- |
| ws239-p001 | 調査・実装・host 試験・T の手順 | planned | — |
| ws239-p002 | QEMU での確かめ（T） | planned | p001 |
