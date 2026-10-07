<!-- awesome-plan project=zedbsd record=ws180 -->

# WS180: Emacs の拡張 — Emacs をベースにしたグラフィカルなエディタ（エージェント開発のための次世代のエディタ）

Status: planning（2026-10-07 ユーザーの依頼で WS だけ作成。段（ベータ）・見積もり・Phase は未定、Queue なし）（2026-10-07 ユーザー（クリック）: ベータ3 以降）
Master: [master](../master.md)
Primary Milestone: MG006

## 由来（ユーザー、2026-10-07、原文）

> これ、WSだけ作れます？
>
> Emacsの拡張
> - 目的
>   - Emacsをベースにしたグラフィカルなエディタを作りたい
> - 背景:現状の実装
>   - 我々のemacsはNoctで実装されています
>   - Emacs LispはNoctで書かれた処理系になっています
>   - GNU EmacsではCで書かれているコア機能は、我々のEmacsではNoctで書かれています
>   - NoctはCのNAPIライブラリを持っており、そこにターミナル制御があります
>   - 現在はそのターミナル制御を使って描画しています
> - 背景:VScodeではだめな理由
>   - VScodeは2026年の今、エディタの標準の地位にあります
>   - ですが、2026年夏頃から、Claude CodeやCodexが開発環境の主流になりつつあります
>   - 個人的な感想として:
>     - コーディングエージェントはサーバで高負荷で実行してクライアントでGUIアタッチしている
>     - エージェントのチャット画面、リモートのソースツリー表示、ファイル編集に統一感がないと感じている
>     - ようするにエージェント開発で使える次世代のエディタの必要性を個人的に感じています
> - やること
>   - libkeilandをNoctのNAPIライブラリ Keiland.* で利用できるようにする
>   - emacs -g で起動すると、ttyでなく Keiland.* を使って表示するようになる
>   - Keiland appsのフローティングペイン分割をベースにUI/UXを作ります
>   - 中央のペインはエディタでEmacs操作、左ペインはソースツリー、下ペインはTerminal
>   - 中央のペインはタブがあり、Emacs編集タブ、エージェントチャットタブがある
>   - 計画のMarkdownドキュメントをきれいにレンダリングしたい
>   - 右ペインにおまけ的にエージェントを入れるのではなく、メインの操作としたい
>   - 複数の異なるエージェントをタブごとに使えるようにしたい
>   - エージェントCLIに接続してターミナル表示するのではない
>   - チャットにはClaudeやCodexのように画像表示、テーブル表示がほしい
>   - チャット入力は下ペインがいいかも

## 目標（上の要件の整理）

1. **Noct の NAPI に libkeiland**: `Keiland.*` の Noct の API で libkeiland（窓・glass の panel・部品・text・入力・IME）を使えるようにする。
2. **`emacs -g`**: REmacs（userland/base/emacs）が tty の端末の制御の代わりに `Keiland.*` で描く表示の backend を持つ。
3. **UI/UX**: Keiland の app の floating の pane の分割をもとに:
   - 中央の pane: tab を持つ。Emacs の編集の tab と、エージェントのチャットの tab。
   - 左の pane: source の tree。
   - 下の pane: Terminal（チャットの入力も下の pane が候補）。
4. **エージェントが主の操作**: 右の pane のおまけではなく主の操作。tab ごとに異なるエージェントを使える。エージェントの CLI に接続して端末を表示するのではない（チャットの UI を自前で持つ）。
5. **チャットの表示**: Claude・Codex のように画像と表を表示する。計画の Markdown の文書をきれいに render する。
6. **背景の前提**: エージェントは server で動き、client の GUI が attach する形（remote の source の tree・file の編集・チャットに統一感を持たせる）。

## 設計で決めること（p001、未着手）

- `Keiland.*` の NAPI の面（どこまで libkeiland を出すか、Noct の event loop と kl_app の loop の結び方）。
- REmacs の表示の層の抽象（tty と Keiland の 2 つの backend、face・font・frame・window の対応）。
- エージェントとの接続の形（CLI の端末ではない → どの protocol・API で話すか、remote の server への attach、認証）。複数のエージェントの tab。
- Markdown・画像・表の render（libkeiland の部品か、libbrowser の再利用か、自前か）。
- remote の source の tree と file の編集（agent の server の上の tree の扱い）。
- 段（ベータ）・見積もり・Phase の分割はユーザーと決める。

## Phase

（未定。p001 の設計からユーザーと決める）
