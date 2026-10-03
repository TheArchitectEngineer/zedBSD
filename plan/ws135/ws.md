<!-- awesome-plan project=zedbsd record=ws135 -->

# WS135: 設定の読み書きを libkeiland に一本化する（desktop.conf を compositor の内部に）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-04）
Primary Milestone: MG006
Related Milestones: —
Objectives: O2
Parent: [Master](../master.md)
Queue: q656（P2）
Resume point: なし。
<!-- awesome-plan-current:end -->

## 目標（2026-10-03 ユーザー、BUG-162）

「BUG-162ですが、WSを作ってください。設定の読み取りと変更はすべて、libkeilandを通して行います。libkeilandが直接解決する設定もあれば、コンポジタと拡張で通信して解決する設定もあります。設定項目を監視して変更の通知を受け取るインタフェースも必要です。desktop.confはコンポジタ内部の設定ファイルにして、ほかのプロセスからの利用はやめます。」

同日の先の指示:「desktop.confに書き込むのはコンポジタの都合であって、それはログインセッションの終了時に書き込んで再開時にロードするようにします。それ以外に書き込む必要はないです。」「デスクトップから変更する音量は。audiodに依頼をするのであって、あとのことはaudiodに任せてください。」

## 完了の条件

1. app（Settings・zdesktop の system bar・他の app）の設定の読み・変更・監視は、すべて libkeiland の API を通る。app は設定の file を開かない。
2. libkeiland の API は設定の項目ごとに解決の先を持つ: libkeiland が直接解決する項目（例: 音量は audiod へ）と、compositor と拡張の protocol で通信して解決する項目（壁紙・窓の透明度・pointer・keyboard の repeat など）。app からは区別が見えない。
3. 設定の項目を監視し、変更の通知を受け取る API がある（別の process での変更も届く。毎秒の file の poll ではない）。
4. `desktop.conf` は compositor の内部の file: session の開始で一度読み、終了（Log Out・Shut Down・Restart・compositor の終了）で一度書く。それ以外に読み書きしない。他の process は読まない・書かない。compositor の 1 秒ごとの監視の thread（`wayland/preferences.c`）を除く。
5. Settings の各頁と system bar が新しい API で動き、変更が即座（frame の単位）に効き、session をまたいで残る。旧 `keiland_preferences_*` の公開 API を除く。
6. zedBSD・Linux・FreeBSD の build、host 試験、QEMU の試験（T1/T2）、全文の規約の確認。

## 結果（2026-10-04）

設計 [design.md](design.md) 第 2 版（D1〜D6 は 2026-10-04 ユーザーの承認、D3 は (a)）のとおり実装した。
- compositor: 設定の store（session の開始で一度読み、終わり（Log Out・終了・SIGHUP）に「この session の差だけ merge」で一度書く、writer thread）、`kl_system_manager_v1`（capabilities）と `kl_system_settings_v1`（set・reset、value・done・result）、同じ uid の client だけ（`kl_backend_peer_uid`）、壁紙の非同期の読み、音量は backend で audiod へ、repeat の送り直し。1 秒ごとの監視の thread（`wayland/preferences.c`）を除いた。
- libkeiland: `kl_settings_*`（11 関数、get・set・reset・watch・dispatch）。compositor の項目は拡張、app の項目（`terminal.*`・`files.open-with.<type>`）は `~/.config/keiland/<app>.conf` を直接（別の process には通知しない、D3 (a)）。旧 `keiland_preferences_*` の公開 API を除いた。
- app: Settings（look・input・sound）、Terminal（`terminal.ambiguous-wide`）、Files（open-with、旧い `# set by Files` の行の選択は引き継がない）。
- 境界の checker に S1（desktop.conf を名前で参照するのは `wayland/settings-store.c` だけ）。

確認（QEMU と FreeBSD guest、実機は未実施）: T2-014 PASS 10/10（settings-p003・p004・p005・p007・pages 24 頁・terminal-p009-guest・files-open always/mouse・volume-p005・boot-test）、T2-016（FreeBSD 15.1 の native build warning 0、host-store 43・host-settings 38、backend の host 試験）、T1-058（規約の直しの後の main で boot-test と settings-p007）、全文規約の監査と修正（約 167 件、p006）。

## 制限・移管

- compositor が crash（SIGKILL を含む）した時は、その session の設定の変更が残らない（D4、ユーザーの方針の帰結）。
- app だけの設定は別の process（別の Terminal）に通知されない（D3 (a)）。
- Files の open-with の旧い選択は引き継がない。
- WS131 の p010・p011 の設定の部分（manager の枠・settings・監視の除去・preferences API の除去）は WS135 で済んだ。WS131 は manager に network・audio・power の interface を足す（[WS131](../ws131/ws.md) の記録）。
- 試験は `plan/tools/settings/`（host-store・host-settings・host-kl-settings・settings-p003・config-amd64-settings.mk）へ移した。

## Phase

| Phase | 内容 | 状態 |
| --- | --- | --- |
| p001 | 設計（design.md 第 2 版、design-reviewer） | cleared |
| p002 | compositor の store・protocol・peer_uid・壁紙・音量・repeat | cleared（T2-014・T2-016） |
| p003 | libkeiland の `kl_settings_*` と probe | cleared（T2-014・T2-016） |
| p004 | Settings の移行、監視と preferences API の除去 | cleared（T2-014・T2-016） |
| p005 | Terminal と Files の open-with | cleared（T2-014・T2-016） |
| p006 | 全文規約の監査と修正、WS の回帰 | cleared（T1-058） |
