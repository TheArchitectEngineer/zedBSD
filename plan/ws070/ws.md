<!-- awesome-plan project=zedbsd record=ws070 -->

# WS070: zdesktop の System Menu（`xdg_toplevel_menu_v1`、libzdesktop で包む）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: none（p001〜p004 は 2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main への統合は main の session）
Resume point: p008（titlebar の protocol と model）。p007（Titlebar Presentation の設計）cleared 2026-09-27。順序は titlebar-design.md §14（WS071 と合わせた計画）。p006 は titlebar の Phase の後
Executor: WS071 の作業用のサブエージェント（2026-09-27 ユーザー「WS071のサブエージェントでスケジューリングするのがいいと思います。」）
<!-- awesome-plan-current:end -->

## 目標

2026-09-27 ユーザー: 「この画像は、フローティングタイトルバーとドッキングタイトルバーにメニューを追加してみたものです。下記が仕様です。
X11サーバの実装が終わったら、OpenGLよりも、これを先に実装してもらえませんか？あとでGTK4やQt6のネイティブメニューバーとしても
利用可能にするつもりです。XDG拡張ではあるものの、libzdesktopでラップします。」

Wayland のクライアントがメニューの意味（階層・ラベル・状態・action・shortcut・role・icon name）を zdesktop に渡し、zdesktop が
システムの UI として描き操作する。通常の窓では浮いたタイトルバーに、最大化（docked）ではシステムバーに同じメニューを出す。
仕様案の原文と添付画像の説明は [spec.md](spec.md)。

## 方式

- protocol は仕様案の `xdg_menu_manager_v1`・`xdg_menu_v1`・`xdg_toplevel_menu_v1`（item は数値の ID、transaction の commit）。
  zdesktop の非標準の拡張なので、client は libzdesktop の API を使い protocol を直接話さない（2026-09-27 ユーザー決定）。
  後で GTK4・Qt6 の native menubar の backend がこの API（または protocol）を使う。
- zdesktop が描く: 浮いたタイトルバーの題名の右、docked のときはシステムバーの題名の右に top-level の項目（text だけ）、選ぶと
  zdesktop の popup（submenu、checkbox・radio、separator、shortcut の表示、keyboard の操作、外の click で閉じる）。
- 最初の使い手は zdesktop-terminal（Shell・Edit・View・Session・Help、画像のとおり）。

## 受け入れ（p001 で決めた）

1. zdesktop-terminal のメニューが浮いたタイトルバーと docked のシステムバーに出て、pointer・keyboard（F10）・shortcut で選ぶと
   terminal の action が動く（Venus: p003・p004 で済み。i915 実機: p005 で済み、capture display）。
2. 動的な更新（enabled・checked）が commit の単位で反映される。focus の窓のメニューがシステムバーに出る（Venus で済み）。
3. protocol の誤りは design.md §2.2 の error で返り、libzdesktop は送らずに errno で返す（Venus で済み、menu-probe）。
4. 規約の全文との照合（p005: WS071 と共有しない file。共有する file は p006）。

設計は [design.md](design.md)（§11 の 5 点は 2026-09-27 ユーザーが既定のまま確定: F10、shortcut は zdesktop が実行、外の click は閉じるだけ、icon なし、ASCII の label。§12 の context menu の余地）。

## 追加の目標: Titlebar Presentation（2026-09-27）

ユーザー:「WS070に仕様追加します。WS071のサブエージェントでスケジューリングするのがいいと思います。」と
「zedBSD Titlebar Presentation Specification」のたたき台（原文は [titlebar-spec.md](titlebar-spec.md)）。

タイトルバーを system-owned な presentation surface とし、client は意味（identity・menu・controls・tabs の model）だけを渡し、
zdesktop が浮いたタイトルバー（通常）とシステムバーの Application Zone（最大化）に描く。主の内容は `MENU`・`CONTROLS`・`TABS` の
どれか 1 つ（排他）。幅の不足の段階的な縮退、docking と restore の animation（200〜300 ms）、docked からの下への drag・swipe で restore、
double click の対称、transaction の atomic な mode の切り替え、client は pixel・font・色を指定しない、pointer と touch の適応、
未対応の compositor では client 側の装飾へ fallback。

既存の System Menu（p001〜p005 の `xdg_toplevel_menu_v1`）はこの仕様の `MENU` mode の model になる。`CONTROLS` の最初の使い手は
zdesktop-files（WS071、仕様の §14 の File Manager の例）、`TABS` の使い手は後で（zdesktop-files は CONTROLS なのでタブ（WS071 p013）は窓の中、titlebar-design.md §11）。
同日のユーザーの補足:「つまり、今のファイラーのはウィンドウ内部の上部にナビゲーションバーを持っていますが、これをウィンドウのフローティングタイトルバーにマージします。」
→ 具体的な受け入れの一つ: zdesktop-files の窓の中の上部のナビゲーションバー（戻る・進む・ホーム・path・検索・表示の切り替え等）を
`CONTROLS` の model として浮いたタイトルバー（最大化ではシステムバーの Application Zone）へ移し、窓の中の bar は消す。
さらにユーザー:「ファイラーはこのcompositorでしか使えなくてOKです。」→ zdesktop-files には fallback（§27）を持たせない（拡張が無ければ起動時にはっきり失敗してよい）。
Phase の分け方・順序・受け入れは p007（設計）で決める。WS071 の Phase と組み合わせて WS071 のサブエージェントが計画する。

## Future Work の候補（main の session が future-work.md へ）

- menu の item の icon（`icon_name`・role の icon を描く）。2026-09-27 ユーザー「アイコンはあとで追加を考えましょう」。icon theme が要る。
- 非 ASCII（日本語）の label: zdesktop の glyph atlas を動的な cache に（WS035 の libtruetype の拡張）。

## Phase 一覧

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws070-p001](phase001/phase.md) | 設計: protocol の定義（request・event・型・error）、libzdesktop の API、zdesktop の model・描画・入力・popup、試験。[design.md](design.md) | cleared | WS069-p008〜p010（2026-09-27 ユーザーがサブエージェントでの並行を指示） |
| [ws070-p002](phase002/phase.md) | protocol（libwayland の client 側と zdesktop の server 側）と zdesktop の menu model（transaction・更新）、menu-probe | cleared | p001 |
| [ws070-p003](phase003/phase.md) | zdesktop の描画と操作: 浮いたタイトルバーとシステムバーの項目、popup、keyboard、shortcut、activation | cleared | p002 |
| [ws070-p004](phase004/phase.md) | libzdesktop の API と zdesktop-terminal のメニュー（Shell・Edit・View・Session・Help） | cleared | p002、p003 |
| [ws070-p005](phase005/phase.md) | i915 実機、規約の全文との照合（WS071 と共有しない file）と回帰。libwayland の flush の EPIPE で protocol error を読み落とす不具合を直した | cleared | p001〜p004 |
| [ws070-p007](phase007/phase.md) | Titlebar Presentation の設計（[titlebar-design.md](titlebar-design.md)）: protocol `zed_titlebar_v1`、libzdesktop、zdesktop の配置・縮退・描画・入力・animation、overflow、glyph cache、試験、Phase の分割 | cleared | p005、[titlebar-spec.md](titlebar-spec.md) |
| ws070-p008 | titlebar の protocol と model: libwayland の `zed_titlebar_*`、zdesktop の titlebar.c（request・model・transaction・error・寿命・log）、libzdesktop の `zdesktop_titlebar_*`、titlebar-probe。描画は変えない | planned | p007 |
| ws070-p009 | glass の UTF-8 と動的 glyph cache（fallback font）、role の icon の rasterize。題名・menu の label の日本語（zdesktop の glass.c: WS035 と調整） | planned | p007 |
| ws070-p010 | CONTROLS の presentation: 配置と縮退、button・segment・検索とパンくずの欄・輪、pointer・keyboard、overflow の popup（隠れた control と窓の menu）、docked の Application Zone、animation の補間（shell.c・menu-shell.c・seat.c: WS035 と調整） | planned | p008、p009 |
| ws070-p011 | TABS の presentation: strip、active・attention・×・＋、縮退、mode の atomic な切替（titlebar-probe で） | planned | p010 |
| ws070-p012 | titlebar の規約の全文との照合、回帰（menu・files・zdesktop）、boot test、i915 実機（任意） | planned | p006、p011、WS071-p014 |
| ws070-p006 | 規約の全文との照合の残り: WS071 と共有する file（`zdesktop/menu.c`・`menu-shell.c`・`menu.h`、`libzdesktop/menu.c`、`zdesktop.h`、`libwayland/menu-protocol.c`）への p005 の指摘の直しと回帰（最後） | planned | p005、WS071 の menu の変更の merge（衝突を避ける分割、2026-09-27） |

## 試験の道具（plan/ws070/tests/）

| 道具 | 用途 |
| --- | --- |
| config-amd64-menu.mk、build-menu-image.sh | lean な guest image（guest harness から clang・lldb・libcxx を外した。BUILD は build/amd64） |
| menu-guest.sh | Venus guest（runtime は build/ws070-run） |
| menu-p002.sh | protocol の error と libzdesktop の検査（/bin/menu-probe） |
| menu-p003.sh | terminal の menu の全体（浮いた・docked・popup・submenu・keyboard・shortcut・動的更新・新しい窓・終了） |
| menu-occlude.sh | 隠れた題名の bar の項目は押せない |
| menu-regress.sh、zdesktop-p068-menu.sh | WS035 の zdesktop の回帰（p068 は題名の double click の位置を直した版） |
| style-compare.sh | 既存の file の style-check の件数を変更前と比べる |
| menu-hw.sh | i915 実機（5330）の System Menu（capture の scenario `zdesktop-menu`）。lock の中で走らせ、/tmp の結果を OUTDIR に写す |
