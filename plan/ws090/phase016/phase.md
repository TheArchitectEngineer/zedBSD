<!-- awesome-plan project=zedbsd record=ws090-p016 -->

# ws090-p016: File Chooser の右の pane の白い背景を、左の pane と揃える（または左右とも desktop の背景を少し透かす）

Status: planning
Disposition: normal
Parent: [WS090](../ws.md)
Queue: 未定

## ユーザーの要望（2026-10-05、原文）

「File Chooserの右ペインが、背景が真っ白なので、デザイン的に浮いている。右ペインも、左ペインと同じような色がいいと思う。あるいは、左右ともに、デスクトップ背景を少し透けさせたような背景にするのもいいと思う。」

## 今の状態（Q1 が source で確かめた）

- File Chooser は `userland/desktop/libkeiland/ui/chooser-view.c`。左の sidebar と右の content を `kl_panel(style, …, 1)`・`kl_panel(style, …, 0)` で描く（`chooser-view.c:158-159`）。
- `kl_panel`（`userland/desktop/libkeiland/ui/cards.c:73-`）は、glass の時は左右とも薄い veil（`theme->glass_sidebar`・`theme->glass_content`、compositor が下に glass を描く）、glass でない時は左が sidebar の veil、右が **白い card と影**（Files の不透明の見た目）。
- [ws090-p014](../phase014/phase.md) で chooser を親の窓の title bar にぶら下がる sheet にし、**chooser は不透明**にした。そのため右の pane が白い card になっている。

## 範囲（案は 2 つ、設計でユーザーと決める）

- (a) **右の pane を左と同じ系統の色に**: 不透明の時の content の panel を sidebar と同じ veil の色（または少し明るい同系の色）にし、白い card と影をやめる。chooser だけに効く style の指定にするか、`kl_panel` の不透明の時の見た目を全 app で変えるか（Files・Text Editor・PDF Viewer などへの影響）を決める。
- (b) **左右とも desktop の背景を少し透かす**: sheet を glass（compositor が下に desktop の背景の blur を描く）にし、左右を glass の veil にする。sheet が親の窓の上に重なるので、何を透かすか（親の窓か desktop の壁紙か）と、読みやすさ（文字の contrast）を確かめる。ws090-p014 で不透明にした理由を確かめて、glass に戻して問題が無いかを見る。
- どちらでも、light・dark（ws089-p017 の dark の外観を入れる時）で破綻しないこと。

## 受け入れ（案）

- File Chooser の左右の pane の見た目が揃い、白く浮かない（QEMU の PNG、実機の UAT）。他の app の panel の見た目は意図した範囲だけ変わる。host-chooser の試験と QEMU の chooser の試験（T1）が PASS。C の全文の規約、build warning 0。
