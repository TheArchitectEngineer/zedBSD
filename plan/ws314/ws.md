<!-- awesome-plan project=zedbsd record=ws314 -->

# WS314 libkeiland の公開の API を component ごとの header に分け、プログラミングの解説を書き、fd と UNIX への依存を API から外す

- Master: [plan/master.md](../master.md)（2026-10-30 作成）。Primary Milestone: MG006。
- Status: planning（2026-10-30 Q1 が作成、q1690 で P2）
- 目標: (1) `userland/desktop/include/keiland/keiland.h` の定義を component ごとの `keiland_*.h` に分け、`keiland.h` はそれらを include する入口にする。
  (2) Keiland のプログラミングの解説（英語）を `docs/` に書く。(3) app・desktop の program が fd・pipe・socket・fork に触れない API に置き換え、
  `kl_app_watch_fd()` と `kl_instance_fd()` を公開の API から外す。その後に解説へ新しい API を足す。

## ユーザーの決定（2026-10-30、原文）

1. 「fdはWindowsに移植できないので、このインタフェースは許容できないです。Mailer, Installerは、kl_process_みたいなサブプロセスAPIで抽象化したいです。」
2. 「Mailerに関しては、appにイベントをポストするAPIを導入すれば、fdやパイプのメカニズムを削除できますね。同様にwindowにイベントをポストするAPIも、この件では必須ではないものの、普通に考えればあった方がいいです。」
3. 「Installerに関しては、kl_process_*を導入するのがいいです。Terminalではどうなってますか？」
4. 「Settingsも、kl_instance_*からfdのAPIを削除して、instanceを指定してそのappにイベントをポスト可能にするのがいいです。」
5. 「Browserに関して、ネットワークI/OはすべてKeiland非依存のlibbrowserに閉じ込める必要があります。KeilandのBrowserは、libbrowserのシェルに過ぎない、というのが設計意図です。直接にシェルがネットワーク接続やsocketやfdを触ってはいけません。」
6. 「Keilandデスクトップから、UNIX依存を削除する必要があります。それは設計意図から逸脱しています。」
7. 「libkeilandについて提案していただいたことをすべて承認します。」（下の「承認された案」）
8. 「include/keiland/keiland.hにすべての定義が入っていますが、include/keiland/keiland_*.hに、それぞれのkl_xxxのコンポーネントの定義を、分割して移したいです。トップレベルはkeiland.hのままで、そこからincludeされます。さらに、kl_uiのimmediate mode UIのプログラミングモデルを、docs/にドキュメント化してほしいです。Keilandのプログラミングの解説ドキュメントです。appの作成、windowの作成、タイトルバーの作成、uiの作成とメインループ、widgetの作成、メニューの作成、その他の機能の使い方、の順で、人間が呼んでも理解しやすい構成にしてほしいです。英語で記述してください。そのあとで、fdを削除するという、先ほどまでの合意事項のリファクタリングを行います。ドキュメントには、fdのAPIのことは書かないで進めておき、fdの削除後、作成されたAPIに関する説明を追記します。」
9. 「以上のことを、WSを立てて、サブエージェントですすめてほしいです。」

## 承認された案（2026-10-30 Q1 の提案、上の 7 で承認）

- **app・window への event の post**: thread から呼べる `kl_app_post`（と `kl_window_post`）。Mailer の pipe 2 本を外す。起こす仕組みは platform の層に閉じる。
- **`kl_process_*`**: 起動（argv・env・cwd）、stdin への書き込み、stdout・stderr の受け取り（byte・行）、終了の event と code、kill、pty の mode と大きさ
  （Terminal、Windows なら ConPTY）。Installer・Terminal と、別の program を起動する Files・Preview・PDF Viewer・Phone・Settings を寄せる
  （高い水準の起動の API `kl_launch` の類も検討）。権限を上げる起動を API に含めるかは設計で示しユーザーに聞く。
- **`kl_instance`**: instance を app に結び、後の起動の要求を `kl_app` の event（`KL_APP_INSTANCE` の類）で届ける。`kl_instance_fd()` を外す。
- **Browser**: libbrowser が自分の network の thread を持ち、結果は「起こしてほしい」callback で呼ぶ側に知らせる（shell は中で `kl_app_post`）。
  shell から `pollfd`・socket・fd を消す（DNS の thread と media の engine の起こしも同じ callback に）。
- 範囲: app と libkeiland の公開の API から UNIX の依存を外す。compositor・`libkeiland-backend-*`・sessiond は platform の層として残す（Q1 の解釈）。

## Phase

| Phase | 目的 | 状態 | 依存 |
| --- | --- | --- | --- |
| ws314-p001 | `keiland.h` を `keiland_*.h` に分ける（定義の移動だけ、API・ABI は変えない。KL_VERSION の履歴の comment の置き場所も） | planned | — |
| ws314-p002 | `docs/` に Keiland のプログラミングの解説（英語）: app → window → titlebar → ui と main loop → widget → menu → その他。fd の API は書かない | planned | p001 |
| ws314-p003 | 設計: post・`kl_process`・`kl_instance` の event・libbrowser の network の thread（design.md、design-reviewer） | planned | p001 |
| ws314-p004 | `kl_app_post`・`kl_window_post` の実装と Mailer の移行 | planned | p003 |
| ws314-p005 | `kl_process_*` の実装と Installer・Terminal・起動する program の移行 | planned | p003 |
| ws314-p006 | `kl_instance` の event と Settings の移行 | planned | p004 |
| ws314-p007 | libbrowser の network の thread と Browser の shell の移行 | planned | p004 |
| ws314-p008 | `kl_app_watch_fd`・`kl_instance_fd` を公開の API から外す（KL_VERSION を上げる）、Linux・FreeBSD の build | planned | p004〜p007 |
| ws314-p009 | 解説に新しい API（post・process・instance）を足す | planned | p008 |
| ws314-p010 | 規約の全文の見直し | planned | p009 |

## Resume point

- 2026-10-30 Q1: 作成。P2 が q1690 で p001 から。
