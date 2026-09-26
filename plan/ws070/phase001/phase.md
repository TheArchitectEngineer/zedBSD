<!-- awesome-plan project=zedbsd record=ws070p001 -->

# ws070-p001: 設計（protocol、libzdesktop の API、zdesktop の model・描画・入力、試験）

Phase ID: `ws070-p001`
Parent: [WS070](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main の Queue への反映は統合する main の session）
承認: 2026-09-27 ユーザー「GLSLコンパイラとSystem Menu Extensionについて、サブエージェントで実装を進めてもらえますか。」

## 範囲

仕様案（[spec.md](../spec.md)）を元に、protocol の定義（request・event・型・error・version）、libzdesktop の API、zdesktop の
menu model・transaction・描画（浮いたタイトルバー、システムバー、popup）・入力（pointer、keyboard、shortcut）・focus の規則、試験を
決める。コードは変えない。

## 受け入れ

1. [design.md](../design.md) に上の全部があり、仕様案と違える所・仕様案に無い所に理由がある。
2. 人間の判断が要る点は未決として既定と共に記録する。
3. 試験の道具（lean な guest image）が build でき、既存の回帰が通る。

## 結果（2026-09-27）

cleared。

- [design.md](../design.md): protocol v1（§2: manager 3 request、menu 14 request と error 8 種、toplevel menu 2 request と 3 event）、
  libwayland の非公開 header と typed listener（§3）、libzdesktop の API と GMenuModel・QMenuBar への写し方（§4）、zdesktop の
  model（§5）、描画・pointer・keyboard・shortcut・focus（§6）、zdesktop-terminal の menu（§7）、試験（§8）、統合の衝突の危険（§10）。
- 仕様案からの主な決定: `append_submenu` は `append_item(type = submenu)`、transaction の外の変更は error、checked は client が持つ、
  shortcut は XKB keysym、`activated` に action を足す、押した入力の代わりに新しい serial。
- 未決（design.md §11、既定で進める）: menu を開く key（F10）、zdesktop が shortcut を実行すること、外の click を client に渡さない
  こと、icon を描かないこと、非 ASCII の label。
- 試験の道具: `plan/ws070/tests/config-amd64-menu.mk`（guest harness から clang・lldb・libcxx を外した lean な image）、
  `build-menu-image.sh`（既定の BUILD は build/amd64: 外部 package は build/amd64/dynamic に link するので、別の BUILD では openssl の
  共有 library が静的 libc に当たって link に失敗した）、`menu-guest.sh`（build/ws070-run の Venus guest）。
  worktree では build/llvm・ws035-fonts・ws035-wallpaper・ws035-sq-venus・NoctLang を main の tree への symlink にし、sysroot は
  `make sysroot-amd64` で自分の tree に作った（main の build には書いていないことを find で確かめた）。image の build は 1 分余り。
- 基準の確認（変更前、QEMU・Venus）: `zdesktop-p068.sh` PASS（build/ws070-base-p068/、prompt・output の画面を見た）。
