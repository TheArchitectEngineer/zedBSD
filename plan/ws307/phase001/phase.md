<!-- awesome-plan project=zedbsd record=ws307-p001 -->

# ws307-p001 インストーラの仕様の確定

- 親: [WS307](../ws.md)。Queue: q1643 / q1643-i01（担当 P4、2026-10-28）、q1647（3 回目のレビューの起動）、q1648 / q1648-i01（3 回目のレビューと J1〜J4 の反映、P4、2026-10-28）。
- Status: in-progress（受け入れを満たした、Q1 の確認待ち。2026-10-28 P4、q1648）
- 出典: 2026-10-28 ユーザー「P4を立てて、WS307の仕様検討を行ってもらってください。ユーザ確認事項があれば回答させていただきます。仕様を確定させたいです。そのためのphaseをQ1が作り、P4に実行させてください。」

## 目的

ws.md に記録したユーザーの希望の仕様（原文）をブラッシュアップし、実装に入れる仕様（`plan/ws307/spec.md`）として確定する。code は書かない。

## 手順

1. 調べる（読むだけ）: 今の tree の関係する所を file:行で押さえる。
   - disk と partition: GPT の扱い（`tools/build/zedimage-host.c`・旧 zedinst があればその source）、UFS の mkfs・swap、kernel の disk・partition の reload（`diskpart`）、ESP の FAT の作り方と loader（`\EFI\BOOT\BOOTX64.EFI`・`zedbsd.cfg`）、UEFI の boot entry の口（efibootmgr 相当の有無）。
   - 対応する disk の driver（NVMe・VMD・AHCI/SATA・USB Mass Storage）と、guest から disk の一覧・型番・大きさを取る口。
   - 入れる物: base・Keiland Desktop・firmware の今の build の単位（`make` の target、image の作り方）、`userland/packages/` の package の単位と依存の書き方、clang・OpenSSH・OpenSSL・CA 証明書・GTK4・Fonts・libavcodec・curl の在り処。
   - Keiland のアプリの作り方（libkeiland・既存の Settings などのウィザード状の UI）、root の権限が要る操作の今の口（sessiond・passkey・setuid の helper）、翻訳（WS158）。
   - 試験の道具（QEMU の OVMF、NVMe・USB・SATA の disk、boot-test.sh）。
2. 仕様の案を `plan/ws307/spec.md` に書く: 画面ごとの表示・入力・検査・遷移、partition の配置の既定値（ESP・root・swap の大きさ）、ESP の「十分な大きさ」、disk 全体を使う時の確認、tarball の形と置き場所（`/opt/installer`）と作り方、package の依存の解決、インストールの手順（partition → mkfs・mkswap → 展開 → loader と boot entry → 設定）、失敗とキャンセルの扱い（途中で止めた disk の状態）、権限の分け方（UI と root の helper）、試験の方法、範囲外。
3. 判断の要る点を「ユーザーの確認事項」として、各々に選択肢と推しを付けて Q1 に送る（Q1 がユーザーに聞き、答えを返す）。答えを spec.md に反映し、残りが無くなるまで繰り返す。
4. design-reviewer（model: "opus"）で spec.md の敵対的レビュー、反映。
5. 実装の Phase の分割の案（p002〜、見積もり LW、依存）を ws.md の表に書く。

## 受け入れ

- spec.md に、ユーザーの原文の各項目が漏れなく対応し、ユーザーの確認事項が全部回答済み（回答の原文を引用）。
- design-reviewer の指摘が反映済み。
- 実装の Phase の表がある。code・build の変更は無い。

## 結果（2026-10-28、P4）

- 調査: disk・partition・ESP・loader・swap（diskpart・mkfs・mkswap・kernel の partition・block-identity・swap-source・UEFI の loader）、driver（NVMe・VMD・AHCI・USB）、build と package（Makefile の登録・rootfs の rule・userland/packages・firmware）、Keiland の app・権限（account-admin・sessiond・security.md）・翻訳（WS158）・試験の道具（boot-test.sh・guest.py・AAT）を file:行で押さえ、[spec.md](../spec.md) の本文に引いた。
- 仕様: [spec.md](../spec.md)。ユーザーの原文の各項目との対応は §14。
- ユーザーの確認事項: U1〜U29 を Q1 経由で聞き、全て回答済み。回答の原文は spec §15 と [ws.md](../ws.md) の「ユーザーの決定」。追加の指示（loader が UFS から kernel を読む、`make installer-image`、OS 名 Kei の削除）も反映。
- design-reviewer（opus）: 3 回（082c46884 の版、ea2d011cc の版、2ac2eeaad の版）。指摘と反映は spec §17（3 回目は §17.3、全文 [design-review3.md](../design-review3.md)）。
- 3 回目の後のユーザーの判断 J1〜J4 は回答済み（原文は spec §15・ws.md）。J1 の方法の J5 は Q1 に確認中（2026-10-28）。本文は推しの A で書き、B・C の時の直す所を spec §18 に書いた。
- 実装の Phase の表: [ws.md](../ws.md)（p002〜p014）。
- code・build の変更は無い（`plan/ws307/` だけ）。
- 確かめ（q1648）: 反映で引いた file:行を main d483706d4 で読んで確かめた（`volume-discovery.c:409-411`・`:446-452`・`:854-862`、`sessiond/main.c:694-723`、`wayland/main.c:33`・`:155`、`account-admin/Makefile:8`、`cred.c:122`、`session.c:646`、`zedbsd-config.c:620-624`、`bootx64.c:1735`、`zedimage-host.c:18`・`:27`・`:749-760`、`tools/release/kei-nightly.mk`、`include/uapi/gpu-op.h:9`、`ime/dict/Makefile:15`、`aatlib.py:48-50`）。reviewer の挙げた他の行（ufs.c の j3・byte order、block-identity.c:182-188、check-amd64-native-image.py:190-202 など）は reviewer の観察として引いた。
