<!-- awesome-plan project=zedbsd record=ws079 -->

# WS079: 手書きノート（Notes）と PDF Viewer、上の右端からのスワイプ

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p002〜p016 と p009 の規約の全文は cleared。p017（text box）は実装 main・T1-265b fail 0 で人の目視だけ残り、2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」。fg010 の OSC のデモ（S8 Notes・S9 PDF Viewer）は 2026-10-17 に完了）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

画面の上の右端から左下へのスワイプで手書きノート（Notes、`/bin/notes`）を起動・最前面・全画面にし、筆圧 4096 段階の pen で書いて PDF（ベクタ＋編集の metadata）に保存・再編集できる。PDF Viewer（`/bin/pdfviewer`、scroll と page の swipe、Notes で書き込み）と共有の PDF の library（`userland/base/libpdf`）を作る。設計は [design-input-notes.md](design-input-notes.md)・[design-pdf.md](design-pdf.md)・[libtruetype-outline.md](libtruetype-outline.md)、手引きは [guide.md](guide.md)、デモの手順は [demo-s8-s9-manual.md](demo-s8-s9-manual.md)。

## ユーザーの決定

2026-09-28 ユーザー（目標の原文）:

「デスクトップですが、
・スクリーン上部の右上から左下に向かってスワイプすると、手書きノートアプリが起動or起動済みなら最前面化、全画面表示
・手書きノートアプリは圧力4096段階のペンを使って手書きで書類を作れ、PDFで保存できる（PDFにベクトルグラフィックで保存しつつ、メタデータで詳細な編集データを持たせる）
・ついでにPDFビューアアプリも作る。スクロールもできるし、ページ単位でスワイプするモードもある。書き込みはノートアプリで行える。」

同日のユーザーの回答:
- ペンの装置は **USB のペンタブレット**（Wacom など USB HID の digitizer）。QEMU では合成の入力で試験する。
- PDF Viewer は **段階を切る**: ① Notes が書く PDF（ベクタの線・画像）→ ② 一般の PDF の図形・画像・埋め込みの TrueType → ③ CFF・Type1・暗号化など。各段の後に評価する。
- 名前: `userland/desktop/notes`（`/bin/notes`、画面の名前「Notes」）、`userland/desktop/pdfviewer`（`/bin/pdfviewer`、「PDF Viewer」）、
  共有の PDF の library は `userland/base/libpdf`。

同日のユーザーの回答（design-input-notes §8）:「pen tablet はノーブランドのUSB-C or HDMI+USBの10インチタッチLCD＋AES Penを買いました。
HDMIをメイン出力にする方法を確立して、全画面1スクリーンのみでデモに使いたいです。ペンのボタンはまだ届いてないのでわかりませんが、AES/WGPなので2つくらいあるかも。
ノートの保存は保存ボタンだけでなく、一定時間操作がないときに自動保存、ジャーナリング的に復元できるのがいいと思います。
キーボードショートカットは標準的なものにしたほうがわかりやすいと思います。マニアックな使いこなしよりも標準を目指しましょう。
libtruetypeにはアウトラインを返すAPIを追加しましょう。」
- D1: 10 インチの touch LCD（USB-C、または HDMI + USB）と AES の pen。pen・touch は USB HID の digitizer として来る見込み（届いたら descriptor を確かめる）。
- 出力: i915 の実機で **HDMI を主な出力**にし、全画面の 1 画面だけでデモする方法を確立する（新しい Phase、WS075 の display と Keiland）。
- D5: pen の button は 2 つ程度（届いてから割り当て）。D6: 保存 button に加えて、操作の無い時間の後の自動保存と、journal からの復元。
- D8: keyboard の shortcut は標準的なもの（Ctrl+S・Ctrl+Z・Ctrl+Shift+Z/Ctrl+Y・Ctrl+N・Ctrl+O 等）。
- stage ② の glyph: libtruetype に outline を返す API を足す。
- main の判断（2026-09-28、p004 の報告の設計の食い違い）: stroke の形は `pdf_outline_stroke()` を唯一の元にする。Catmull-Rom の平滑化と丸い join は
  この関数に入れ（p005 の前）、Notes は画面と PDF の両方にこの輪郭を使う（design-input-notes §5.3 の `stroke-geometry.c` はこれを呼ぶ）。

2026-09-28 ユーザー:「ウィンドウのフローティングタイトルバーを二本指でタッチする（叩く）と、Zオーダーが後ろに回って奥に行き、次のウィンドウが表示されるようにしたいです。」
→ p012（kernel の multitouch）と p013（compositor の touch と二本指のタップ）。今の kernel は指の collection を無視し、compositor には wl_touch が無い。
touch の LCD の USB はまだ見えていない（H1）ので、QEMU の注入の device で先に作る。
同日のユーザー:「2本指でタッチというより、2本指で軽く短く上方向こすって、「あっちにいけ」というジェスチャー、というのがいいですね。とりあえず3回クリックで実装しつつ、マルチタッチが実現したら実装しましょう。」
同日のユーザー:「では、マウスで3回クリックすると同じ動作にしましょう。」→ 浮いたタイトルバーの mouse の triple click でも同じ（窓を後ろへ）。
main の注意: タイトルバーの double click は今ドッキング（ws035-p062）なので、triple click を見分けるために double click の動作を
click の間隔の上限（今の double click の 400 ms）まで待たせる（ドッキングがその分遅れる）。triple click は p013 で compositor と一緒に作るが、
mouse の部分は touch を待たずに先に入れてよい。
- 2026-10-06: 「Notes に text box を作る（別の Phase）」（p017、ws175-p008 と一緒）。
- 2026-10-13 UAT: 「Notesアプリでペンの感度が有効であることを確認しました。」（L3 のうち筆圧は実機で確認、傾き・消しゴムは未確認）
- 2026-10-13: 「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」
- 2026-10-27（q1608）: 「…masterとWSを修正して、completedやcanceledにしてください。」

## 完了の条件（p001 で確定）

1. 画面の上の右端から左下へのスワイプ（pointer の drag と、ペン・touch があればそれも）で Notes が起動する。起動済みなら最前面に出て全画面になる。
2. USB の HID の digitizer のペンの筆圧（4096 段階）・傾き・消しゴム・側面の button を kernel が読み、compositor が Wayland の tablet の protocol
   （`zwp_tablet_manager_v2`）で app に渡す。QEMU では合成の入力で、実機では USB のペンタブレットで確かめる（実機は機種が決まってから）。
3. Notes で筆圧に応じた太さの線を書き、消し、page を足し、undo でき、PDF に保存できる。PDF はベクタの図形で描かれ（他の viewer でも読める）、
   編集の詳細（stroke の点・筆圧・時刻・道具）は PDF の中の metadata（埋め込みの file の stream など）に持ち、Notes は保存した PDF を開いて編集を続けられる。
4. PDF Viewer は縦の scroll と page 単位の swipe の 2 つの mode を持ち、段階 ① の PDF を正しく表示する。「書き込む」で Notes にその PDF を開かせる。
5. 段階 ②・③ は ① の後に評価して進める。

## 制約

- 共有の library の置き場と header の方針は他の compat の library と同じ（base、`include/libc/` の適所）。libpdf は Wayland に依らない。
- 描画は Vulkan（Keiland の他の app と同じ）。画面の文字列に Keiland の名前を出さない。
- kernel の入力の変更は HAL の API に触れない（HID の driver と input の層）。

## 結果

- kernel: USB HID の digitizer の pen（筆圧・傾き・消しゴム・button・in-range）と試験用の注入の device（p002）、multitouch（protocol B、p012）。
- compositor: `zwp_tablet_manager_v2` と pointer の fallback（p003）、上の右端からのスワイプで Notes（p010）、`wl_touch`・端のジェスチャー・浮いたタイトルバーの triple click と二本指の上へのこすりで窓を後ろへ（p013）。
- libpdf: 書き出しと自分の形式の読み込み（p004、`pdf_outline_stroke()` の Catmull-Rom と丸い join）、読み込み ①（display list・stroker・CPU rasterizer、p006）、② 一般の図形・画像・埋め込み TrueType（p007）、③ CFF・Type1・暗号化（p008）、CCITTFax・password・thumbnail（p015）、他の PDF への増分の更新（p014）。
- Notes v1（p005: 筆圧の線・消しゴム・page・undo・保存と再編集、自動保存 5 秒・journal）と仕上げ（p011）、text box（p017、IME 対応、ws175-p008 と一緒）。PDF Viewer v1（p006）。
- デモの S8・S9 の通しの試験（p016: QEMU で PASS、頁送り 10 回の最長 142 ms）。規約の全文（p009、style-check の違反 0）。
- 証拠は QEMU（Venus）と host が中心。実機は 2026-10-13 のユーザーの UAT（筆圧）、fg010 の OSC のデモ（2026-10-17 完了）。

## 制限・移管

- L3 の実機のペンの傾き・消しゴム・側面の button は未確認（機材の計画次第）。p002・p003・p012 の実機の pen・touch は未実施。
- p017 の画面と IME の目視は未実施（T1-265b は fail 0）。
- F-051（libpdf の保存の圧縮）、F-062（guest の試験の固定の待ち）。
- 試験（`tests/`）は pen・touch の注入の試験 guest（`config-amd64-pen.mk`・`pen-guest.sh`・`build-pen-image.sh`）と Notes・PDF の host 試験・libpdf の表の生成の script として多くの所から使われるので `plan/tools/` へ移す（Q1 の OK の後）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws079-p001 | 設計: pen の入力・tablet・スワイプ・Notes の model・PDF と metadata・libpdf・PDF Viewer | cleared（2026-09-28、design-input-notes.md） |
| ws079-p002 | kernel: USB HID の digitizer と合成の入力 | cleared（2026-09-28、QEMU と host。実機の pen は未実施） |
| ws079-p003 | compositor: `zwp_tablet_manager_v2` と pointer の fallback | cleared（2026-09-28、QEMU の Venus） |
| ws079-p004 | libpdf: 書き出しと自分の形式の読み込み | cleared（2026-09-28） |
| ws079-p005 | Notes v1 | cleared（2026-09-28） |
| ws079-p006 | libpdf の読み込み ① と PDF Viewer v1 | cleared（2026-09-28） |
| ws079-p007 | 段階 ②: 一般の PDF の図形・画像・埋め込み TrueType | cleared（2026-09-28） |
| ws079-p008 | 段階 ③: CFF・Type1・暗号化 | cleared（2026-09-28） |
| ws079-p009 | 全文規約確認と回帰 | cleared（2026-09-29） |
| ws079-p010 | 上の右端からのスワイプで Notes | cleared（pointer の範囲、2026-09-28） |
| ws079-p011 | Notes の仕上げ（デモ向け） | cleared（2026-09-28） |
| ws079-p012 | kernel の multitouch | cleared（2026-09-28 main の判断、QEMU と host） |
| ws079-p013 | compositor の touch と窓を後ろへ回すジェスチャー | cleared（2026-09-28） |
| ws079-p014 | Notes で他の PDF に書き込む | cleared（2026-09-28） |
| ws079-p015 | CCITTFax・暗号化の password・thumbnail | cleared（2026-09-29） |
| ws079-p016 | L2: デモの S8・S9 の通しの試験 | cleared（2026-09-30、QEMU。実機・Windows の QEMU はユーザー） |
| ws079-p017 | Notes の text box（IME 対応） | in-progress のまま WS を閉じた（実装 main・host PASS・T1-265b fail 0、目視は未実施。2026-10-13 ユーザー「UATの結果待ちのものは…」） |

## 記録の所在

Phase の計画・結果は削除する。git の commit `fb1f6afee` の `plan/ws079/` にある。Queue の履歴は [plan/history](../history/index.md)。
