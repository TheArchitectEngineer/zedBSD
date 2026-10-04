<!-- awesome-plan project=zedbsd record=ws090-p016 -->

# ws090-p016: File Chooser の右の pane の白い背景を、左の pane と揃える（または左右とも desktop の背景を少し透かす）

Status: in-progress（T1 の QEMU の結果を Q1 が判定するまで cleared にしない）
Disposition: normal
Parent: [WS090](../ws.md)
Queue: q709（P2）

## ユーザーの要望（2026-10-05、原文）

「File Chooserの右ペインが、背景が真っ白なので、デザイン的に浮いている。右ペインも、左ペインと同じような色がいいと思う。あるいは、左右ともに、デスクトップ背景を少し透けさせたような背景にするのもいいと思う。」

## 今の状態（Q1 が source で確かめた）

- File Chooser は `userland/desktop/libkeiland/ui/chooser-view.c`。左の sidebar と右の content を `kl_panel(style, …, 1)`・`kl_panel(style, …, 0)` で描く（`chooser-view.c:158-159`）。
- `kl_panel`（`userland/desktop/libkeiland/ui/cards.c:73-`）は、glass の時は左右とも薄い veil（`theme->glass_sidebar`・`theme->glass_content`、compositor が下に glass を描く）、glass でない時は左が sidebar の veil、右が **白い card と影**（Files の不透明の見た目）。
- [ws090-p014](../phase014/phase.md) で chooser を親の窓の title bar にぶら下がる sheet にし、**chooser は不透明**にした。そのため右の pane が白い card になっている。

## ユーザーの決定（2026-10-05）

「WS090 p016	File Chooser の右の pane を左右とも透かす」→ **案 (b)**: sheet を glass にし、左右とも desktop の背景を少し透かす veil にする。

## 範囲（案は 2 つ、(b) に決定）

- (a) **右の pane を左と同じ系統の色に**: 不透明の時の content の panel を sidebar と同じ veil の色（または少し明るい同系の色）にし、白い card と影をやめる。chooser だけに効く style の指定にするか、`kl_panel` の不透明の時の見た目を全 app で変えるか（Files・Text Editor・PDF Viewer などへの影響）を決める。
- (b) **左右とも desktop の背景を少し透かす**: sheet を glass（compositor が下に desktop の背景の blur を描く）にし、左右を glass の veil にする。sheet が親の窓の上に重なるので、何を透かすか（親の窓か desktop の壁紙か）と、読みやすさ（文字の contrast）を確かめる。ws090-p014 で不透明にした理由を確かめて、glass に戻して問題が無いかを見る。
- どちらでも、light・dark（ws089-p017 の dark の外観を入れる時）で破綻しないこと。

## 受け入れ（案）

- File Chooser の左右の pane の見た目が揃い、白く浮かない（QEMU の PNG、実機の UAT）。他の app の panel の見た目は意図した範囲だけ変わる。host-chooser の試験と QEMU の chooser の試験（T1）が PASS。C の全文の規約、build warning 0。

## 実施（2026-10-05、q709、P2）

### ws090-p014 で不透明にした理由の確認

- p014 の範囲の「追加: chooser の窓（sheet・独立の両方）を不透明にし、glass を透かさない」は、p014 の時点の指示で、
  sheet が親の窓の上に重なるので何も透かさない形にした（`keiland_glass` を作らない、`style.glass = 0`）。
  技術の制約は無い。今回のユーザーの決定 (b) で置き換える。

### 設計

- chooser は zdesktop に glass がある時（`kl_window_see_through` が真（wl_shm は常に真）で `kl_glass_create` が成功）、
  `style.glass = 1` にし、glass の panel を 3 つ送る。glass が無い compositor（Linux・FreeBSD の他の compositor など）では今までの不透明のまま。
  1. **窓全体の panel**（radius 14、zdesktop の窓と同じ）。sheet の時は上に 14 伸ばす（y=-14）。zdesktop の `draw_sheet` は
     親の title bar の下端で scissor を切るので、sheet の上の角は角張ったまま、下の角だけ丸くなる。窓の地（card の間の隙間・縁）は
     親の窓ではなく、desktop の壁紙の blur の frost になる。
  2. 3. **左の sidebar と右の content の card**（radius 16、`keiui_chooser_panels` の位置）。Files と同じに card の rim が出る。
  その上に client は地を透明にし、`kl_panel` の glass の veil（sidebar は白 40、content は白 60、theme の `glass_sidebar`・
  `glass_content`）を描く。左右とも同系の薄い veil で、白い card と影は出ない。
- 透かすのは desktop の壁紙（`set_blur` は既定の 0）。下の窓（親の窓）は透かさない。読みやすさは Files の glass の時と同じ（同じ veil と文字の色）。
- panel は大きさが変わった時だけ送る（`chooser_glass_panels`、frame の present の前、同じ commit で効く）。窓を閉じる時に glass を先に壊す。
- dark の外観（ws089-p017）は未実装。theme の glass の veil を dark の theme が持てば、この経路はそのまま従う。

### 変更

- `userland/desktop/libkeiland/ui/chooser.c`: `chooser_glass`・`chooser_glass_panels`、glass と panel の記録、header の説明。
- `plan/ws090/tests/sheet-guest.sh`: open と saveas の step に glass の panel の log の確認を追加（`ZWL GLASS ... panels=3 card:0,-14,W,H,14 card:8,8,...`）。

### 確認（host）

- build: zedBSD の `dynamic/libkeiland.so`（`ZEDBSD_CONFIG=plan/ws089/tests/config-amd64-settings.mk BUILD=build/p2-q703`）warning 0、
  Linux（`make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/p2-q703-linux all`）warning 0。FreeBSD は未実施。
- `sh plan/tools/keiui/host-chooser.sh build/p2-q703/keiui/host-chooser`: 85/85 passed。`build/keiui-shots/host-chooser-open-glass.png`
  （host の近似: 空色の地を frost 代わりに白くしたもの）で左右の pane が同系の veil になり、文字（主・副）が読めることを目視した。

### 未実施（T1 に依頼）

- QEMU（Venus、zdesktop `--glass`）: `plan/ws090/tests/sheet-guest.sh OUT install open saveas` の PASS と open.png・saveas.png の目視
  （左右とも壁紙が少し透ける veil、白い card が無い、上の角が title bar に接して角張る）。image は `plan/ws090/tests/config-amd64-textinput.mk`。
- 実機の UAT（ユーザー）。
