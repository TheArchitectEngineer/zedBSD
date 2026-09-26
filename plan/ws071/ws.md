<!-- awesome-plan project=zedbsd record=ws071 -->

# WS071: zedBSD File Manager（Finder 風で zedBSD らしいファイルマネージャ）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: none
Resume point: p001（設計）。WS070（System Menu）の p002〜p004 が使える状態になってから本体を作る（メニューと context menu を使うため）
<!-- awesome-plan-current:end -->

## 目標

2026-09-27 ユーザー: 「これもWSを追加しておいてください。Finder風だけどzedBSDらしいファイルマネージャとして、仕様書のベースになる形で
まとめます。現在の作業は続けてください。」

Finder の操作モデル（左のサイドバー、浮いたツールバー、パンくず、アイコン・リスト表示、プレビュー、タグ、最近のファイル、Quick Look）を
元に、zedBSD のシステム UI（明るいすりガラス、大きい角丸、浮いたタイトルバー、system-owned menu）に合わせた GUI のファイルマネージャ。
ホームは $HOME の一覧ではなく「ファイル作業のダッシュボード」（ヒーローカード、フォルダのカード、最近のファイル）にする。
仕様案の原文とモックアップの画像の説明は [spec.md](spec.md)。

## 方式（案、p001 で決める）

- zdesktop の Wayland client として `userland/base/` に置く（名前は p001 で決める。案: `zdesktop-files`）。描画は zdesktop-terminal と同じく
  標準の Wayland と Vulkan、文字は libtruetype。zdesktop の非標準の拡張（System Menu 等）は libzdesktop の API だけを使う。
- メニューバー（File・Edit・View・Go・Window・Help、spec §36〜37）は WS070 の System Menu で出す。右クリックの context menu（spec §15）は
  「compositor 側のシステムメニュー仕様に合わせる」ので、WS070 の protocol が context menu（popup を位置で開く）を持つかを p001 で確かめ、
  無ければ WS070 への追加として扱う（WS070 の範囲を変える判断なのでユーザーに示す）。
  2026-09-27 WS070 の回答（[design.md §12](../ws070/design.md)）: 中身は `xdg_menu_v1` の木と zdesktop の popup をそのまま使えるが、
  「surface の点に seat・serial に答えて出す」request が要る（version 2 の `get_context_menu` と `xdg_context_menu_v1`、libzdesktop の
  `zdesktop_menu_popup`）。WS071 の Phase として足すか WS070 に足すかは p001 で決める。
- 最初の版の範囲（spec の「最低限」）: ホームのダッシュボード、サイドバー（よく使う項目・その他・タグ）、戻る・進む・ホーム・パンくず、
  アイコン表示・リスト表示、選択、開く、名前変更、ゴミ箱、コピー・移動・削除（background の task と進捗）、検索（名前・拡張子・タグ）、
  プレビューのペイン・Quick Look、ファイル情報（owner・権限を含む）、キーボード操作（spec §35）、タブ・複数窓、Undo。
- 後の版（Future Work の候補）: クラウド（spec §27）、SMB・NFS・WebDAV（§26）、カラム・ギャラリー表示、全体の indexer（§34）、
  動画・PDF のサムネイル、デバイスの eject（§25、zedBSD の mount の仕組み次第）。

## p001 で決めること（判断が要るものはユーザーに示す）

1. program の名前と置き場所、zdesktop-terminal との描画の共有（部品を libzdesktop 等に出すか）。
2. タグ・最近のファイル（アプリ横断の recent database、§19）の保存先: UFS の extended attributes の有無を確かめ、無ければ
   per-user の database（例: `~/.local/share/`）。アプリ横断にするなら libzdesktop の API にする。
3. ゴミ箱の形式（freedesktop.org Trash 仕様に合わせるか）。
4. 既定のアプリで開く（§14）の仕組み: MIME の判定（`file` の magic）と関連付けの database。
5. サムネイルの decoder（PNG・JPEG 等）: 既存の userland にあるか、外部 package を tarball で取り込むか（ライセンスの監査）。
6. ドラッグ&ドロップ（§16）: zdesktop の `wl_data_device` の対応の有無。無ければ zdesktop の Phase が要る。
7. Quick Look（§18）の「中央に大きなシステムプレビューをオーバーレイ」を client の窓（popup）で出すか、compositor の機能にするか。
8. ホームのヒーローカードの絵・文言の出どころ（壁紙と同じく commit しない資産か）。

## 受け入れ（案、p001 で決める）

1. 最初の版の範囲の機能が Venus の QEMU と i915 実機で動く（各機能の試験を host と guest で）。
2. メニューバーが浮いたタイトルバーと docked のシステムバーに出て、選ぶと action が動く。context menu が system の UI で出る。
3. 規約の全文との照合。

## Phase 一覧

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| ws071-p001 | 設計: 範囲（最初の版と後の版）、上の 8 つの決定、画面の構成と部品、file 操作の model（履歴・選択・task・Undo）、試験 | planning | WS070-p001（protocol の形） |
| ws071-p002 以降 | p001 で分ける（目安: 窓と描画の骨格とサイドバー → 一覧の表示と選択 → file 操作と task・ゴミ箱・Undo → 検索・タグ・最近のファイル → ホームのダッシュボード → プレビュー・Quick Look・ファイル情報 → メニューと context menu → 実機・規約の照合（最後）） | planning | p001 |
