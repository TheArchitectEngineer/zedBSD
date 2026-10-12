<!-- awesome-plan project=zedbsd record=ws212 -->

# WS212: Wiseview の作り直し（仮想デスクトップと display の矩形、窓の移動、display のローテーション）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-12 Q1 作成、ユーザーの要望）
Primary Milestone: MG006
Related Milestones: MG008
Parent: [Master](../master.md)
Queue: 未割当（ユーザーが着手を決める）
Resume point: p001 の調査と設計（今の Wiseview・switcher・仮想デスクトップ・multi head の作り、新しい配置、drag の移動、ローテーションと比率の保持、hotkey、Pi 4 での描画の重さ）。
<!-- awesome-plan-current:end -->

## 由来

2026-10-12 ユーザー:「ウィンドウ一覧(wiseview)の機能性を変更します。ウィンドウ一覧を出すと、3枚の仮想デスクトップの矩形が横並びに表示され、その中にそれぞれのウィンドウ一覧が出ます。ウィンドウを選択すると、その仮想デスクトップに切り替わって、ウィンドウが選択されます。ウィンドウは仮想デスクトップの矩形から違う矩形にドラッグすることで、別な仮想デスクトップに移動できます。さらにマルティディスプレイのとき、縦方向にディスプレイの矩形がスタックされ、その中に横方向に仮想デスクトップが並びます。ウィンドウは仮想デスクトップやディスプレイをまたがって移動できます。さらに、ディスプレイのローテーションを実装します。マルチディスプレイでウィンドウ選択画面を出したとき、ディスプレイ矩形の左上にタグが表示されます。これをクリックすると、そのディスプレイのウィンドウ群と、現在メインディスプレイにあるウィンドウ群が、スワップされます。ウィンドウの位置とサイズは、ディスプレイサイズに対する比率で保持されます。メインディスプレイと他のディスプレイの中身を順繰りにスイッチする、Shift+Super+left/rightみたいなホットキーも用意します。」

## 目標（要望の整理）

1. Wiseview を出すと、仮想デスクトップ 3 枚の矩形が横に並び、その中にそれぞれの窓の一覧（プレビュー）が出る。
2. 窓を選ぶと、その仮想デスクトップに切り替わり、その窓が選ばれる（前面・focus）。
3. 窓を別の仮想デスクトップの矩形へ drag すると、その仮想デスクトップへ移る。
4. multi display の時は、display の矩形が縦に積まれ、それぞれの中に仮想デスクトップが横に並ぶ。窓は仮想デスクトップと display をまたいで移せる。
5. display のローテーション: multi display の Wiseview で display の矩形の左上にタグが出る。押すと、その display の窓の群と、今の main display の窓の群が入れ替わる。窓の位置と大きさは display の大きさに対する比率で保ち、入れ替えた先の大きさに合わせる。
6. hotkey（Shift+Super+←/→ など）で、main display と他の display の中身を順に入れ替える。

## 範囲と未決（p001 で決める）

- compositor（userland/desktop/wayland/ の Wiseview・switcher・desktop・heads・output-switch）。client の protocol は変えない見込み。
- 仮想デスクトップの数（今の 3 枚と固定か）、窓が display をまたいで移る時の大きさ（比率）、最大化・全画面・dock した窓の扱い、display の解像度・scale が違う時、display を抜いた時の窓の戻り先。
- 入れ替えの単位（仮想デスクトップ 3 枚ごとか、今見えている 1 枚か）と、main display の定義（system bar のある display）。
- hotkey の割り当て（既存の Super の組と衝突しないか）と touchpad の gesture との関係（WS142）。
- 描画の重さ: 仮想デスクトップ 3 枚×display 数のプレビューを Pi 4 で 60fps に（WS211 の絵の持ち方と束ねを使う）。
- 試験: QEMU の multi head（T1）、5330 の外部 display、Pi 4。

## 完了の条件（仮）

上の 1〜6 が QEMU と実機（5330 の外部 display）で動き、Pi 4 で Wiseview の開閉と drag が滑らか（ユーザーの確認）。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| p001 | 調査と設計（今の作り、配置、drag、ローテーションと比率、hotkey、Pi の描画、試験） | planned |
| p002 | 仮想デスクトップの矩形の配置と選択・drag の移動（単一 display） | planned |
| p003 | multi display の積み重ねと、display をまたぐ移動 | planned |
| p004 | display のローテーション（タグ・hotkey・比率の保持） | planned |
| p005 | 規約の全文の見直し | planned |
