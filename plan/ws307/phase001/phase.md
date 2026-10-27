<!-- awesome-plan project=zedbsd record=ws307-p001 -->

# ws307-p001 インストーラの仕様の確定

- 親: [WS307](../ws.md)。Queue: q1643 / q1643-i01（担当 P4、2026-10-28）。
- Status: in-progress
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
- ユーザーの確認事項: U1〜U27 を Q1 経由で聞いた。回答の原文は spec §15 と [ws.md](../ws.md) の「ユーザーの決定」。追加の指示（loader が UFS から kernel を読む、`make installer-image`）も反映。
- design-reviewer（opus）: 2 回（082c46884 の版と ea2d011cc の版）。指摘と反映は spec §17。
- 実装の Phase の表: [ws.md](../ws.md)（p002〜p013）。
- code・build の変更は無い（`plan/ws307/` だけ）。
