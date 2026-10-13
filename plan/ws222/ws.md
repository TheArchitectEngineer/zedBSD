<!-- awesome-plan project=zedbsd record=ws222 -->

# WS222: 画像・PDF・字形の library を menuconfig の Desktop へ戻す（PNG・PDF・JPEG・TrueType）

<!-- awesome-plan-current:start -->
Status: planned（2026-10-13 Q1 作成。ユーザーの指示で、今の作業が全部終わってから取り組む。優先度は最後）
Primary Milestone: MG006
Related Milestones: なし
<!-- awesome-plan-current:end -->

## 原文（2026-10-13 ユーザー）

「ごめんなさい、PNG, PDF, JPEG, TrueType, このあたりをDesktopに再度移します。でも、あとでいいです。今の作業が全部終わってから取り組むWSを作ってください。」

（経緯: 同じ日、サーバ用の image のために Desktop を全部外せるよう menuconfig を直した時、libtruetype は base の libpdf の依存なので、ユーザーの指示「libtruetypeはDesktopに入ってますが、baseにするべきですね。baseのlibpdfの依存なので」で menu の分類を desktop → base に移した（bca0ac303）。その後この指示で、この辺りの library をまとめて Desktop へ戻す。）

## 目的と完了の条件

1. menuconfig の分類（`ZEDBSD_USERLAND_PACKAGE` の 8 番目の欄）を Desktop にする: libpdf（今 base、`userland/base/libpdf/`）、libpng-compat・libjpeg-compat（今 base）、libtruetype（今 base、bca0ac303 で移した物）。libgif-compat・libz-compat は p001 で範囲に入れるかを決める（ユーザーの「このあたり」）。
2. base の program がこれらに依存していないことを確かめる（今 libpdf を使うのは desktop の pdfviewer・keiland-preview・notes だけ、2026-10-13 の grep）。base に依存する物があれば、その program の扱いをユーザーに返す。
3. source の directory の場所（`userland/base/…`・`userland/desktop/…`）と依存の識別子（`base/libpdf` など）を合わせて移すかを p001 で決める（識別子は path と同じ文字列なので、移すなら全部の参照を直す）。
4. 受け入れ: menuconfig の試験（`plan/tools/menuconfig-target-host-test.py`）PASS、Desktop を全部外した構成で `validate-image-config` が通り、これらの library が image に入らない。既定の構成の image の build が通る。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| p001 | 範囲の確定（libgif・libz を含めるか、directory を移すか）と実装、menuconfig の試験 | planned |
