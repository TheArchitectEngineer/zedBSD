<!-- awesome-plan project=zedbsd record=ws158 -->

# WS158: Keiland 本体と Keiland の app の翻訳（英語が基準、日本語はベータ2）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG007
Parent: [Master](../master.md)
Queue: q738（P2、2026-10-05）
Resume point: p001 の設計を書いた（判断の 5 点待ち）。F-068（ローカライズの仕組みと複数言語の UI）をこの WS へ昇格。
<!-- awesome-plan-current:end -->

## 単一目標

Keiland 本体（compositor・system bar・greeter・lock の画面）と Keiland の app の UI の文を翻訳できる仕組みを作り、英語を基準に日本語の翻訳を作る。言語は Settings の Languages の頁で選ぶ。

## ユーザーの指示（2026-10-05、原文）

「Keiland本体と、Keilandアプリの、Translationの作成。英語がベースで、日本語の作成がベータ2の目標。SettingsのLanguagesタブで選択。」

## 関係する既存の項目

- [F-068](../future-work.md)（2026-10-02 user「日本語UIはベータ1に入れません。ローカライズの仕組みをあとで実装して、複数の言語…」）→ この WS へ昇格（Future Work の行を promoted に）。
- [ws089-p015](../ws089/phase015/phase.md)（Settings の日本語の UI）・[ws127-p005](../ws127/phase005/phase.md)（Files の日本語の UI）→ この WS の仕組みの上で行う（それぞれの Phase は app ごとの文の翻訳の作業として残すか、この WS に吸収するかを p001 で決める）。
- [WS154](../ws154/ws.md)（Settings の Languages の頁）→ 言語の選択はここに置く（IME の選択と同じ頁）。

## 範囲（p001 で設計して確定）

1. **翻訳の仕組み**（libkeiland の i18n の口）: message の catalog の形式（gettext の `.po`・`.mo` に当たる物を独自に（Zlib）実装するか、key と文の表か）、英語の文を key にするか ID にするか、複数形・語順の差し替え（printf の位置の指定）、日付・時刻・数の書式（locale）、文字の幅と UI の配置（日本語で長くなる・短くなる時の layout）。
2. **言語の選択と切り替え**（2026-10-05 ユーザーの方針、原文:「言語設定は、Keilandアプリはコンポジタと通信することで言語を取得して、ログアウトなしに、しかもアプリ再起動もなしに、反映できることを目指します。言語変更の通知を作ればいいです。GNOMEアプリなどは、ログアウトしてログインし直すのが最低ラインで、できればアプリ再起動で言語設定を変えられるように、環境変数をマネージしたいです。」）:
   - Settings の Languages の頁で選び、desktop.conf（kl_settings_*）に保存する（書くのは compositor）。
   - **Keiland の app**: 言語を compositor から取得する（compositor の拡張 protocol、kl_settings_* と同じ経路）。compositor が**言語の変更の通知**を出し、app は受けて catalog を読み直し、UI を描き直す。**logout も app の再起動も不要**（目標）。compositor 自身・system bar・greeter・lock も通知で切り替える。
   - **他の toolkit の app（GNOME の GTK・Qt など）**: 最低でも logout・login で新しい言語になる（session の起動の時に `LANG`・`LC_*`・`LANGUAGE` を desktop.conf の言語から作る）。できれば **app の再起動**で変えられるように、compositor（または sessiond）が app を起動する時の環境変数を管理する（App Home・Files からの起動、`keiland-desktop` の launcher（WS111）で、その時点の言語の環境変数を渡す）。既に動いている他の toolkit の app は再起動まで古い言語のまま、と UI で案内する。
   - greeter・lock の画面の言語（login の前の system の既定）。
3. **翻訳の対象**: compositor（system bar・menu・通知（WS156）・Wiseview・dialog）、greeter・lock、Keiland の app 全部（Files・Settings・Text Editor・Terminal・Image Viewer・PDF Viewer・Notes・System Monitor・音量・IME の UI など）。文の抽出の道具（source から英語の文を集める script）。
4. **日本語の翻訳**（ベータ2 の目標）: 用語集（Files・Settings の頁名・操作の名前の統一）、翻訳の review。
5. Linux・FreeBSD の Keiland でも同じ仕組み（OS の locale の環境変数との関係）。

## Phase（案）

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws158-p001](phase001/phase.md) | 翻訳の仕組みの設計（catalog の形式・libkeiland の口・言語の選択と切り替え・locale の書式・抽出の道具・試験） | in-progress（設計済み、判断の 5 点待ち） | WS154 の Languages の頁と設計を合わせる |
| ws158-p002 | libkeiland の i18n の口と catalog の読み込み、抽出の道具 | planning | p001 |
| ws158-p003 | compositor・greeter・lock の文を口に通す | planning | p002 |
| ws158-p004 | 各 app の文を口に通す（app ごとに分けてよい） | planning | p002 |
| ws158-p005 | 日本語の翻訳と用語集、review（ベータ2 の目標） | planning | p003・p004 |
| ws158-p006 | 全文の規約と回帰（英語・日本語の両方の PNG、T1）、実機の UAT | planning | p005 |
