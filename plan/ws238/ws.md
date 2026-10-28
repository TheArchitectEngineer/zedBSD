<!-- awesome-plan project=zedbsd record=ws238 -->

# WS238: amd64 の native の image を ESP（loader だけ）・FAT32（kernel と boot の設定）・UFS の root・swap の構成にする

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。p001 の実装は main、T1-638（QEMU UEFI）PASS、残りの 5330 の素の起動は実機の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」）
Completed: 2026-10-27（q1608）
Primary Milestone: MG004
Parent: [Master](../master.md)
Related: [WS062](../ws062/ws.md)（以前の native の layout: ESP に vmunix・zedbsd.cfg）、[F-014](../future-work.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

native の layout を ESP（BOOTX64.EFI だけ）・FAT32（vmunix・zedbsd.cfg・logo.ppm、GPT の Basic Data、label `ZEDBSD`）・UFS の root・swap にする。loader が ESP の次に FAT32 から load できること、文書の追従、QEMU（UEFI）と 5330 の素の起動での確かめ。`/boot/boot0` を FAT32 に、ESP を `/boot/efi` に mount する。

## ユーザーの決定

- 2026-10-14（原文）「1. native の layout を、ESP（BOOTX64.EFI だけ）・FAT32（vmunix・zedbsd.cfg・logo.ppm）・UFS の root・swap にする。zedimage-host の native と、platform/amd64/vmunix.mk を変えます。2. loader の探索の順を確かめる。ESP の次に FAT32 を見つけて、そこから load できることを試験します。3. 文書を直す。利用の手引きの「vmunix と zedbsd.cfg のある小さな FAT partition」の説明や、i915.gen9=off の手順も、FAT32 の側を指すように直します。4. QEMU（UEFI）と、5330 の素の起動で確かめる。

この修正をお願いします。配布イメージの変更がメインで、ブートローダは運がよければ変更なしだと思います。
修正の理由は、ネットワークが使えなくなったときに、dmesgをFAT32にコピーしたいのですが、ESPだとWindowsでマウントが難しいからです。」
- 2026-10-14（追加）「/boot/boot0がESPからFAT32に変更されたことも確認してください。boot0に加えて、ESPが/boot/efi でマウントされたらうれしいです。ESPがboot0のとき、可能ならefi, boot0で同じディスクをマウントするのがいいです。でも、もし難しければ、boot0だけでもいいです。」

## 結果

- p001: native の layout（ESP・FAT32 `ZEDBSD`・UFS・swap）、`/boot/boot0` と `/boot/efi`、文書と道具の追従。
- T1-638（T2、QEMU UEFI、2026-10-15）PASS: ESP・ZEDBSD（FAT32）・UFS・swap、/boot/boot0 と /boot/efi の mount、dmesg を FAT32 に書いて host の mtools で読める、fsck.fat 両方 rc 0、AAT smoke 8/8。

## 制限・移管

- 実機の確認は未実施（5330 の素の起動、Windows・macOS で ZEDBSD の drive に dmesg.txt）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws238-p001 | native の layout、`/boot/boot0` と `/boot/efi`、文書と道具の追従、試験の手順 | in-progress のまま閉じた（T1-638 の QEMU PASS、5330 は未実施） |

## 記録の所在

Phase の計画・結果は削除する。git の commit `fb1f6afee` の `plan/ws238/` にある。Queue の履歴は [plan/history](../history/index.md)。
