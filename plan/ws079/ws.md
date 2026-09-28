<!-- awesome-plan project=zedbsd record=ws079 -->

# WS079: 手書きノート（Notes）と PDF Viewer、上の右端からのスワイプ

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: p001 の設計（[design-input-notes.md](design-input-notes.md)・[design-pdf.md](design-pdf.md)）は書けた（2026-09-28）。p002（kernel の pen）・p004（libpdf の writer）が進行中。main の判断: header は `include/libc/pdf.h`（libpdf は独自の API）、保存は非圧縮（deflate は後）、p003 を分けて gesture は p010。ユーザーの判断待ち: design-input-notes §8 の D1〜D8（既定あり）、design-pdf §6 の glyph の outline（p007 の前）
<!-- awesome-plan-current:end -->

## 目標（2026-09-28 ユーザー）

「デスクトップですが、
・スクリーン上部の右上から左下に向かってスワイプすると、手書きノートアプリが起動or起動済みなら最前面化、全画面表示
・手書きノートアプリは圧力4096段階のペンを使って手書きで書類を作れ、PDFで保存できる（PDFにベクトルグラフィックで保存しつつ、メタデータで詳細な編集データを持たせる）
・ついでにPDFビューアアプリも作る。スクロールもできるし、ページ単位でスワイプするモードもある。書き込みはノートアプリで行える。」

同日のユーザーの回答:
- ペンの装置は **USB のペンタブレット**（Wacom など USB HID の digitizer）。QEMU では合成の入力で試験する。
- PDF Viewer は **段階を切る**: ① Notes が書く PDF（ベクタの線・画像）→ ② 一般の PDF の図形・画像・埋め込みの TrueType → ③ CFF・Type1・暗号化など。各段の後に評価する。
- 名前: `userland/desktop/notes`（`/bin/notes`、画面の名前「Notes」）、`userland/desktop/pdfviewer`（`/bin/pdfviewer`、「PDF Viewer」）、
  共有の PDF の library は `userland/base/libpdf`。

## 完了の条件（案、p001 で確定）

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

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| ws079-p001 | 設計: pen の入力（USB HID digitizer → kernel の input の event、QEMU の合成の入力）、`zwp_tablet_v2`、スワイプの gesture、Notes の文書 model、PDF の書き方と metadata、libpdf の構成（parser・content stream・描画の list）、PDF Viewer | planning | — |
| [ws079-p002](phase002/phase.md) | kernel: USB HID の digitizer（筆圧・傾き・消しゴム・button・in-range）と、試験用の合成の入力 | in-progress（2026-09-28: pen の読み取りと host 試験・amd64 の build・boot test まで。注入の device は未着手。main の判断: devfs の /dev/input は event の node だけなので、注入は `/dev/input-inject`（devfs の root、試験専用、CONFIG_INPUT_TEST_INJECT）に置く） | p001 |
| ws079-p003 | compositor: `zwp_tablet_manager_v2`（pad なし）と tablet を bind しない client への pointer の fallback | planning | p001、p002 |
| ws079-p010 | compositor: 上の右端からのスワイプ（design-input-notes §4）で Notes を起動・最前面・全画面 | planning | p003 |
| ws079-p004 | libpdf: 書き出し（page、ベクタの path、画像、編集の metadata）と自分の形式の読み込み | planning | p001 |
| ws079-p005 | Notes v1: 筆圧の線・消しゴム・page・undo・PDF の保存と再編集 | planning | p003、p004 |
| ws079-p006 | libpdf の読み込み ① と PDF Viewer v1（scroll と page の swipe、Notes で書き込み） | planning | p004 |
| ws079-p007 | 段階 ②: 一般の PDF の図形・画像（DCT は libjpeg-compat、Flate は libz-compat）・埋め込みの TrueType（libtruetype） | planning | p006 |
| ws079-p008 | 段階 ③: CFF・Type1・暗号化など | planning | p007 |
| ws079-p009 | 全文規約確認と回帰（必須の最終確認） | planning | 全 Phase |
