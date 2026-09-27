<!-- awesome-plan project=zedbsd record=ws071 -->

# WS071: zedBSD File Manager（Finder 風で zedBSD らしいファイルマネージャ）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main の Queue への反映は main の session）
Resume point: WS070 の titlebar 仕様の設計（ws070-p007）の後に p013（タブ、TABS model との関係を設計で決める）。2026-09-27 に大きさで分けた: p007 から Get Info と開く（p012）、p008 からタブ（p013）
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

## 設計（p001、2026-09-27）

[design.md](design.md)。上の「p001 で決めること」8 点と設計中に出た判断は design.md §15「判断が要る点（既定で進めた）」に既定と理由が
ある（戻せる既定。ユーザーの判断があれば変える）。要点: `zdesktop-files`（`/bin/zdesktop-files`）、CPU の canvas を Vulkan で貼る描画、
タグは xattr `user.zdesktop.tags`、recent は libzdesktop の新しい API、ゴミ箱は freedesktop.org の Trash、context menu は WS070 の
protocol の version 2（**WS070 の protocol の拡張を WS071 の p009 として実装**）、PNG は WS035 の D2〜D4 に従う libz-compat・
libpng-compat の decode（WS035 p040・p041 の decode の半分を p010 で先に作る）、DnD は窓の中だけ、Quick Look は窓の中の overlay。

## 2026-09-27 ユーザーの指示: toolbar を浮いたタイトルバーへ

「今のファイラーのはウィンドウ内部の上部にナビゲーションバーを持っていますが、これをウィンドウのフローティングタイトルバーにマージします。」「ファイラーはこのcompositorでしか使えなくてOKです。」 → design §3 の toolbar（戻る・進む・Home・パンくず・検索・表示の切替・preview・進みの輪）は WS070 の titlebar 仕様（[titlebar-spec.md](../ws070/titlebar-spec.md)）の CONTROLS model で浮いたタイトルバー（最大化では System Bar の Application Zone）に移し、窓の中の bar は無くす。fallback（仕様 §27）は持たない: zdesktop の titlebar の拡張が無ければ起動で明示的に失敗してよい。設計は ws070-p007、実装は WS070 の Phase と WS071 の「navigation bar → CONTROLS titlebar」の Phase（ws070-p007 の Phase 分けで決める）。

## Phase 一覧

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws071-p001](phase001/phase.md) | 設計（design.md） | cleared | WS070-p001 |
| [ws071-p002](phase002/phase.md) | 骨格: window（pointer・keyboard）、present（Vulkan の canvas）、canvas・text・icons、toolbar・sidebar・content の静的な配置、host の render 試験、guest の image と起動 | cleared | p001 |
| [ws071-p003](phase003/phase.md) | 一覧と移動: dir、nav（履歴・パンくず・Back/Forward/Home）、icon・list 表示、並べ替え、選択、scroll、folder を開く、Ctrl+L | cleared | p002 |
| [ws071-p004](phase004/phase.md) | file 操作: task（copy・move・delete・duplicate・link）、clipboard、new folder、rename、ゴミ箱（Put Back・Empty）、完全削除の確認、undo・redo、進みと status | cleared | p003 |
| [ws071-p005](phase005/phase.md) | 検索、タグ（xattr・定義・索引・sidebar）、recent（libzdesktop の API）、Favorites の編集、Locations（mount） | cleared | p004 |
| [ws071-p006](phase006/phase.md) | Home の dashboard（hero、folder cards、recent files・folders） | cleared | p005 |
| [ws071-p007](phase007/phase.md) | preview pane、Quick Look、サムネイル（PPM・PGM、thumb.c の cache）、MIME の中身の判定を preview に | cleared | p005 |
| [ws071-p012](phase012/phase.md) | Get Info（owner・権限・checksum・xattr）、開く・別のアプリで開く（apps.c の関連付けと起動） | cleared | p007 |
| [ws071-p008](phase008/phase.md) | menubar（System Menu: File・Edit・View・Go・Window・Help、状態の反映）、New Window・Close Window・Minimize・Zoom、keyboard の shortcut の全体（spec §35）、Help の card | cleared | p012、WS070-p004 |
| ws071-p013 | タブ（2 つ以上のときだけの tab bar、New Tab・Close Tab・Previous/Next Tab、Open in New Tab、menu の Window のタブの項目） | planned | p008 |
| ws071-p009 | context menu: WS070 protocol version 2（libwayland、zdesktop、libzdesktop `zdesktop_menu_popup`）と file manager の context menu | planned | p013 |
| ws071-p010 | サムネイル（libz-compat の inflate、libpng-compat の decode）と窓の中の DnD | planned | p007 |
| ws071-p011 | App Home の項目、規約の全文との照合、回帰、boot test、i915 実機（任意） | planned | p002〜p010、p012、p013 |

## Future Work の候補（main session が future-work.md へ）

| 候補 | 内容 | 出どころ |
| --- | --- | --- |
| F-a | クラウド（spec §27）、SMB・NFS・WebDAV・「サーバーへ接続」（spec §26） | ws.md の後の版 |
| F-b | カラム表示・ギャラリー表示（spec §10） | 同 |
| F-c | 全体の indexer と file の中身の検索（spec §7、§34） | 同 |
| F-d | 動画・PDF・JPEG のサムネイル、disk の thumbnail cache | 同、design §9 |
| F-e | 装置の unmount・eject（spec §25） | 同、design §13 |
| F-f | 描き直しを damage の矩形に絞る（CPU の canvas の最適化） | design §2 |
| F-g | canvas・text・icons を共有の UI library へ（2 つ目の使い手のとき） | design §2 |
| F-h | 窓ごとの本当のすりガラス（alpha の Vulkan surface の後ろに zdesktop が blur） | design §2、WS035 |
| F-i | zdesktop の `wl_data_device`（窓・アプリの間の DnD と clipboard） | design §5.6、§12 |
| F-j | compositor が描く system の Quick Look | design §9 |
| F-k | 名前の衝突の Replace・Skip の dialog、日本語の UI 文言と IME、`$topdir/.Trash-$uid` | design §5.3、§5.4、§15 |
