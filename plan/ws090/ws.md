<!-- awesome-plan project=zedbsd record=ws090 -->

# WS090: widget・control の共有 library（慣性の smooth scroll を含む）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p011（Terminal・Notes の窓、KUI_VERSION 11）uncleared（2026-09-30、ユーザーの指示で優先を下げた。main に入れた。残りの試験は未実施、再開はユーザーが言うとき）。p008 cleared。次は p009・p010（Files）、p007（WS089 の完了の後）、p015（案）
<!-- awesome-plan-current:end -->
作業の手引き（2026-10-01）: [guide.md](guide.md)

## 目標（2026-09-29 ユーザー）

「スクロールやボタンなど、ウィジェットやコントロールを共有ライブラリにする。独自のものでよい。慣性スムーズスクロールは少なくともライブラリにして再利用したい。」

- 今は Files・Terminal・Notes・PDF Viewer・ブラウザ・（作成中の）Settings が、scroll・button・list・sidebar・card 等をそれぞれ持つ。慣性の scroll は
  WS081 で `libkeiland/motion.c`・scroller・gesture にまとめ始めた。
- 独自の共有 library（Kei の見た目、Vulkan と Wayland の上）に、少なくとも慣性の smooth scroll（touch・wheel・key・overscroll の rubber band）を
  部品として出し、既存の app を順に移す。button・toggle・slider・list・sidebar・card・text field 等の範囲と、library の名前・置き場（libkeiland を
  広げるか、新しい library か）・API の形は p001 で決める。新しい app（WS089 Settings、WS091・WS092）はこれを使う。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [ws090-p001](phase001/phase.md) | 設計（[design.md](design.md)） | cleared（2026-09-29） | — |
| [ws090-p002](phase002/phase.md) | libkeiui の骨組みと描画の層（canvas・text・icons・theme）（Settings の書き換えは p007 へ） | cleared（2026-09-29。`libkeiui.so` warning 0、host 13/13 で Files・Settings と byte で一致） | p001 |
| [ws090-p003](phase003/phase.md) | scroll view（`kui_scroll`）と入力の層（`kui_ui`）と文字の view の touch（`kui_text_touch`: 1 本指で選択・2 本指で scroll、2026-09-29 ユーザー） | cleared（2026-09-29。KUI_VERSION 2、host 63/63、慣性は scroller と frame ごとに一致。Text Editor への組み込みと QEMU は p004） | p002 |
| [ws090-p004](phase004/phase.md) | 窓の土台（`kui_window`、Vulkan・shm・無し）、Text Editor の窓・present・touch（`kui_text_touch`・`kui_scroll`）・clipboard、image への登録 | cleared（2026-09-30。KUI_VERSION 3、QEMU で開く・編集・保存・1 本指の選択・つまみ・2 本指の scroll・double tap・long press・wheel・clipboard・PRIMARY、host 34/63/13/75、boot PASS） | p003 |
| [ws090-p005](phase005/phase.md) | 部品（button・switch・slider・field・list・sidebar・card・row・header・dialog・chip・progress）と見本の program | cleared（2026-09-30。KUI_VERSION 4、host 94/94、QEMU で pointer・key・touch・dialog と Text Editor の回帰、boot PASS） | p003 |
| [ws090-p006](phase006/phase.md) | file chooser を libkeiui へ、libkeiland から取り除く（KEILAND_VERSION）、Text Editor の chooser・dialog・chip | cleared（2026-09-30。KUI_VERSION 5・KEILAND_VERSION 16、host-chooser 85/85、QEMU で Open・Save As・上書き・取り消し・BUG-112 の回避なしの開閉 13 回・dialog、boot PASS） | p004・p005 |
| ws090-p007 | Settings を libkeiui へ（描画の層の共有の置き換えを含む） | planning | p005、WS089 の完了 |
| [ws090-p008](phase008/phase.md) | PDF Viewer・Image Viewer を移す | cleared（2026-09-30。`kui_window`（Image Viewer は自前の画像の present を残し `KUI_PRESENT_NONE`）、`kui_file_chooser`、KUI_VERSION 10（全画面）、PDF の password の card は keyboard の inset で上へ。demo-s8-s9 前後 PASS、Image Viewer の guest 試験と touch-guest PASS、C9 10/10（2 件は再実行）、boot PASS） | p006 |
| ws090-p009 | Files（その 1）: 描画の層と scroll | planning | p003、**WS094 の完了**（Files に `--desktop` を足している、2026-09-29 main） |
| ws090-p010 | Files（その 2）: 部品と窓 | planning | p009・p005・p004 |
| [ws090-p011](phase011/phase.md) | Terminal・Notes: 窓（見せ方は無し）。scroll の model は p015（案）へ（2026-09-30 Q1） | cleared（2026-09-30: 統合の試験 demo-s8-s9.sh で合格（ユーザーの指示）、Terminal と p088 は未切り分け、実機は未実施） | p004 |
| ws090-p015（案） | Terminal・Notes の scroll を `kui_scroll` へ。`kui_scroll` に rubber band の境界と位置の引き継ぎ（`keiland_scroller_set_position` の相当）を足すことを含む。WS081 の host 試験 2 本（`run-termtouch.sh`・`run-notestouch.sh`）の変更が要る（2026-09-30 Q1） | planning | p011 |
| [ws090-p013](phase013/phase.md) | `kui_window` の text-input-v3 の受け口と Text Editor（WS102 の D1、2026-09-30 ユーザー） | cleared（2026-09-30。KUI_VERSION 6、QEMU の IME で `漢字`・`かな` が Text Editor に入り保存、host の回帰、boot PASS。WS102 の keyboard は未 merge で未実施） | p004 |
| ws090-p014 | file chooser を親の窓の title bar にぶら下がる sheet にする（2026-09-30 ユーザー、下の節）: compositor が `xdg_toplevel.set_parent` の親を覚え、libkeiui の chooser が sheet を求めた子の窓を、自分の title bar を持たず親の title bar の下に付けて前面に出す（親と一緒に動く・前に出る・最小化する、親への入力は sheet が閉じるまで止める、開閉の動き）。親が無いときは今の独立の窓 | cleared（2026-09-30、P4、QEMU。[phase.md](phase014/phase.md)。Titlebar の mode 3 SHEET（version 3、KEILAND_VERSION 20）、chooser は不透明） | p006 |
| ws090-p012 | 規約の全文との照合と回帰 | planning | 全て |
| [ws090-p016](phase016/phase.md) | File Chooser の右の pane の白い背景を左と揃える、または左右とも desktop を少し透かす（2026-10-05 ユーザー） | cleared（2026-10-05 Q1） | p014 |

p007〜p011 は app ごとに独立で、デモ（10/17）の前は 10/10 までに移し終えたものだけ残す（design.md J5）。

## 2026-09-29 の申し送り（main）

- WS089（Settings）は files の `canvas.c`・`text.c`・`icons.c` と `artwork/mark.c` を source のまま共有して compile している（ws089-p002）。files に target 別の CPPFLAGS が付くと中身が変わりうる。共有の library にするときの最初の対象の候補（F-038 を昇格したもの）。
- 2026-09-29 ユーザー:「テキストエディタのファイルピッカーは、KeiのUIライブラリに入れるのがいいと思いました。」→ 最初の共有の部品はファイルピッカー（Open・Save As の chooser）。今は libkeiland に `keiland_file_chooser_*` として置き（WS092 のエージェントが作る）、WS090 の library の設計の時にそこへ移す。

### ユーザーの要望（2026-09-30、file chooser の sheet）

「File Pickerは、独立したタイトルバーを持つウィンドウではなく、親ウィンドウのタイトルバーにぶらさがって前面に表示されるスタイルにしたいです。
親ウィンドウがない場合は独立にします。可能でしょうか？」→ ws090-p014。

- libkeiui の `kui_file_chooser_open` は既に親の `xdg_toplevel` に `set_parent` している。zdesktop の `toplevel_set_parent`（toplevel.c）は親を確かめるだけで
  覚えず、窓は独立に置いている。
- Q1 の案: sheet は opt-in（Keiland の protocol の flag。他の toolkit の `set_parent` の dialog は今のまま）。sheet の窓は、自分の title bar を持たず、親の浮いた
  title bar の下辺に上端を付けて親の横の中央に置き（幅は親より狭く）、上から滑り出す。親を動かす・前に出す・最小化する・Wiseview では一緒に扱う。
  sheet が開いている間は、親の本体への入力を止める（親の title bar の drag だけは効く）。
- 同日の追加（ユーザー）:「File Chooserは透過ウィンドウをやめましょう。」→ p014 の中で、file chooser の窓（sheet・独立の両方）を不透明の地にする。


## 2026-10-06 UAT のフィードバック

- BUG-211 慣性 scroll を libkeiland の UI の scroll に（scroll bar を使う全ての窓）→ **新しい Phase**。BUG-218 Phone の慣性の開始の遅れ（約 300 ms → 50 ms 以内、最大 80 ms）を共通の実装で
- BUG-226 左の pane の hover の再描画の遅れ（Files・Mail・Calendar・Settings）: CPU の合成を調べ、無ければ frame rate の安定化 → **新しい Phase**（BUG-221 と共通の仕組み）

## Phase（2026-10-06 追加: 再設計）

- [ws090-p017](phase017/phase.md) 設計と実装: 慣性 scroll と開始の遅れ（test-wait、q790 で実装 58b9027a。他の app の慣性は残り）
- [ws090-p018](phase018/phase.md) 設計: pointer の追従の再描画の frame rate（planned）
