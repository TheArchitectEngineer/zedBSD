<!-- awesome-plan project=zedbsd record=ws071 -->

# WS071: zedBSD File Manager（Finder 風で zedBSD らしいファイルマネージャ）

<!-- awesome-plan-current:start -->
Status: completed
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行し、main が merge）
Resume point: —（完了 2026-09-27。残りは下の「制限・移管」）
<!-- awesome-plan-current:end -->

## 目標

2026-09-27 ユーザー: 「これもWSを追加しておいてください。Finder風だけどzedBSDらしいファイルマネージャとして、仕様書のベースになる形で
まとめます。現在の作業は続けてください。」

Finder の操作モデル（サイドバー、ツールバー、パンくず、アイコン・リスト表示、プレビュー、タグ、最近のファイル、Quick Look）を元に、
zedBSD のシステム UI（明るいすりガラス、大きい角丸、浮いたタイトルバー、system-owned menu）に合わせた GUI のファイルマネージャ。
ホームは「ファイル作業のダッシュボード」。仕様案の原文は [spec.md](spec.md)、設計は [design.md](design.md)（§15 の既定の判断を含む）。

同日のユーザーの追加の指示（結果に反映済み）:

- 「今のファイラーのはウィンドウ内部の上部にナビゲーションバーを持っていますが、これをウィンドウのフローティングタイトルバーにマージします。」
  「ファイラーはこのcompositorでしか使えなくてOKです。」→ toolbar を WS070 の CONTROLS の titlebar へ（p014）、fallback なし。
- 「ファイラーのタブは、右側のコンテントペインが所有するのがいいと思うなあ。…左側のペインと右側のペインで、背景をなくして、付箋メモのような
  フローティングにして、すりガラスエフェクトで合成する…」「タブは複数あるときだけ表示することにしよう。」→ p016・p015（ws035-p083）。
  p016 を見た指示（タブは文字の高さの約 2.2 倍、選択は青い文字・青い下線・明るい地、中央揃え、content も白で塗らない）→ p015。
- card の外側をタイトルバーの幅に揃え、docking を確かめる → p017。

## 受け入れと結果（2026-09-27、completed）

受け入れ（p001 で決めた）: 1. 最初の版の範囲の機能が動く（host と guest の試験）、2. menubar が浮いたタイトルバーと docked のシステムバーに出て
action が動き、context menu が system の UI で出る、3. 規約の全文との照合。

| 項目 | 結果 |
| --- | --- |
| program | `userland/desktop/files`（`/bin/files`）。標準の Wayland と Vulkan、CPU の canvas（canvas・text・icons）を Vulkan で貼る。zdesktop の拡張は libkeiland だけを使う。App Home の項目 |
| 画面 | Home の dashboard（hero、folder cards、recent）、sidebar（Favorites・Locations・Tags）、content（icon・list、並べ替え、選択、scroll）、preview の pane。3 つの pane は窓の中に浮いたすりガラスの card（`keiland_glass_v1`、窓の地は透明、ws035-p083・p057）で、外側は浮いたタイトルバーの幅に揃う |
| titlebar | 戻る・進む・Home・パンくず・検索・表示の切替・preview・進みの輪は WS070 の CONTROLS（浮いたタイトルバー、最大化ではシステムバーの Application Zone）。Ctrl+F・Ctrl+L で titlebar の欄へ。拡張が無ければ起動で失敗 |
| 操作 | copy・move・delete・duplicate・link の background の task と進み、clipboard、new folder、rename、ゴミ箱（freedesktop.org Trash、Put Back・Empty）、undo・redo、検索（名前・拡張子・kind・tag）、タグ（xattr `user.zdesktop.tags`）、recent（libkeiland の API）、Get Info（owner・権限・checksum・xattr）、開く・別のアプリで開く（関連付け）、Quick Look（窓の中の overlay）、サムネイル（PPM・PGM・PNG: libz-compat の inflate・libpng-compat の decode）、窓の中の DnD（folder・sidebar・tag・Trash・tab、Ctrl で copy・Ctrl+Shift で link） |
| menu | File・Edit・View・Go・Window・Help（System Menu、状態の反映、CONTROLS の窓では右端の「…」）、shortcut（spec §35）、context menu（WS070 protocol version 2、項目・空き地・Trash・sidebar） |
| タブ・窓 | 2 つ以上のときだけ content の card の中のタブの行（等幅・中央・選択は青い文字と下線）、New Tab・Close Tab・Previous/Next、Open in New Tab、New Window |
| 大きさ | 新しい窓は zdesktop の `xdg_toplevel.configure_bounds`（xdg-shell v4）に収まる（p018: 1280x800 で 1120x690） |
| 試験 | host（files-render の model と描画、PNG、host-p009・p010・p013・p014）と Venus の guest（files-p002〜p018、files-regress 14 本）。WS070 の menu・titlebar の回帰、WS035 の zdesktop の回帰 |
| 規約 | 新しい file の style-check 0、全文の手の照合（p011、p018）。boot test は 2026-09-27 のユーザーの指示で締めでは無し（p016・p015・p017・p010 までは PASS） |

QEMU（Venus）だけ。i915 実機は未実施（下の移管）。

## Phase 一覧

| Phase | 内容 | Status |
| --- | --- | --- |
| ws071-p001 | 設計（design.md） | cleared |
| ws071-p002 | 骨格: window、present（Vulkan の canvas）、canvas・text・icons、静的な配置、host の render 試験、guest の image | cleared |
| ws071-p003 | 一覧と移動: dir、履歴・パンくず、icon・list、並べ替え、選択、scroll、Ctrl+L | cleared |
| ws071-p004 | file 操作: task、clipboard、new folder、rename、ゴミ箱、完全削除の確認、undo・redo | cleared |
| ws071-p005 | 検索、タグ、recent（libkeiland）、Favorites の編集、Locations | cleared |
| ws071-p006 | Home の dashboard | cleared |
| ws071-p007 | preview pane、Quick Look、サムネイル（PPM・PGM）、MIME の中身の判定 | cleared |
| ws071-p012 | Get Info、開く・別のアプリで開く | cleared |
| ws071-p008 | menubar（System Menu）、New/Close Window・Minimize・Zoom、shortcut の全体、Help の card | cleared |
| ws071-p013 | タブ（New/Close/Previous/Next Tab、Open in New Tab）（main のセッション） | cleared |
| ws071-p014 | 窓の中の toolbar → WS070 の CONTROLS の titlebar | cleared |
| ws071-p016 | タブを content の pane が持つ、タブらしい見た目 | cleared（p015 で作り直し） |
| ws071-p009 | context menu（WS070 protocol version 2 と file manager の項目） | cleared |
| ws071-p015 | pane を浮いたすりガラスの card に、タブの行の作り直し（ws035-p083 と一緒） | cleared |
| ws071-p017 | card の外側をタイトルバーの幅に揃える、CONTROLS の docking の確認 | cleared |
| ws071-p010 | PNG のサムネイル（libz-compat・libpng-compat の decode）と窓の中の DnD | cleared |
| ws071-p011 | App Home の項目、規約の全文との照合、回帰（締め） | cleared |
| ws071-p018 | （補強）新しい窓が画面に収まる（configure_bounds と置き場所）、Help のタブの key | cleared |

Phase の記録は git の履歴にある（WS の完了で削除した）。

## 制限・移管

- **i915 実機**: files の実機の capture の scenario は [WS075](../ws075/ws.md) の p002 で保留になっている（desktop の変更が
  落ち着いたら main の合図で足す）。この WS の受け入れの実機の部分はそこへ移す。
- **窓の外への DnD と titlebar のパンくずへの drop**（design の F-i）: [ws035-p084](../ws035/ws.md) へ移し、2026-09-27 に cleared
  （窓の間の drag and drop、パンくずの段への drop）。
- **libz-compat の deflate・libpng-compat の encode**: [ws035-p040](../ws035/ws.md)・p041 に残す。
- Future Work（[future-work.md](../future-work.md)）: F-032（network の場所）、F-033（カラム・ギャラリー）、F-034（indexer・中身の検索）、
  F-035（動画・PDF・JPEG のサムネイル、disk の cache）、F-036（unmount・eject）、F-037（damage の矩形の描き直し）、F-038（共有の UI
  library）、F-039（DnD の自動 scroll・spring-loaded）、F-040（compositor の Quick Look）、F-041（衝突の dialog、日本語の UI と IME、
  `$topdir/.Trash-$uid`）、F-044（他の client の configure_bounds）。
- 見た目（tint、節の題の濃さ、タブの地）は主観なので、ユーザーの反応で直す。

## 試験の道具（plan/tools/files へ移した）

- `build-files-image.sh`・`config-amd64-files.mk`（lean な guest image、`plan/tools/titlebar/config-amd64-menu.mk` を含む）、
  `files-guest.sh`（Venus の guest、runtime `build/ws071-run`、`build/ws035-sq-venus` の renderer が要る）、`make-home.sh`（試験の home）、
  `qmp-input.py`（pointer と key を 1 つの QMP で、右 button・modifier を押したままの click）。
- `files-regress.sh [OUTDIR] [PHASE...]`: guest の試験（files-p002〜p010・p012〜p015・p017）を順に。`files-p011.sh`（App Home）、
  `files-p018.sh`（configure_bounds と置き場所）、`files-lag.sh`（最後の frame がすぐ出るか）。
- host: `host-build.sh`・`host-run.sh`（files-render: `host-render.c`・`host-model.c`・`host-glass.c`）、`host-p009.sh`・`host-p010.sh`・
  `host-p013.sh`・`host-p014.sh`、`host-png.sh`（libz-compat・libpng-compat を Python の zlib・PIL と比べる）。
