<!-- awesome-plan project=zedbsd record=ws174 -->
# WS174: 起動時の Ctrl で safe boot options に切り替える

Master: [master](../master.md)
Status: planning（2026-10-05 夜 追加。設計だけ。ユーザーが仕様を変更: Ctrl = kmsg を console、Shift = login を console、config の形式は変えず bootloader だけ）
Primary Milestone: MG003
Related: MG006（graphical boot）

## 由来

ユーザー（2026-10-05 夜）「おっと、イメージを実機で起動したら、カーネルの起動の途中でpanicしたとみられますが、グラフィカルブートなのでわかりません。ブート時にctrlキーが推されていたらsafe boot optionsの設定に切り替えるように、ブートローダとブートコンフィグファイルを変更したいです。設計だけできますか？」

## 目標

boot の時に Ctrl が押されていたら、bootloader が boot の config の safe boot options（text の kmsg・graphical login なし など）に切り替えて kernel を起動し、panic などが画面で読める。

## Phase

| Phase | 内容 | 状態 | 依存 |
| --- | --- | --- | --- |
| [ws174-p001](phase001/phase.md) | 設計（bootloader の Ctrl の検出、config の safe の行の形、kernel への渡し方、docs）。全文 [design.md](phase001/design.md) 第 2 版 | in-progress（設計の第 2 版まで。ユーザーの判断 D1〜D8 待ち、2026-10-05 夜） | — |
| ws174-p002（案） | 共通 parser: `zbl_uefi_kern_config_parse_mode`、`safe.kmsg=console`・`safe.login=console` の許可の表、normal mode の空測り、host 試験 H1〜H10、3 つの BIOS loader の build と `stage2_end` | planning（p001 の設計の案。Queue 化は Q1） | p001 の判断 |
| ws174-p003（案） | UEFI loader: `safe-key.c`（Ex protocol、S0〜S2）、告知と 1 秒の Stall、640x480 の希望、`vmunix.mk` の native UEFI cfg の safe の行、host 試験 K1・K2、docs、T1 の QEMU 4 cell | planning | p002 |
| ws174-p004（案、任意 D4） | BIOS PC/AT: int 16h AH=02h、`parse_mode(SAFE)`、告知、BIOS 用 cfg は zedinst の admission と調整 | planning（後回しを推奨） | p002 |
| ws174-p005（案） | 規約の全文の見直し（新しい関数と変えた関数） | planning | p003（p004 をやるならその後） |

## 受け入れ（WS）

- 実機 5330 で、Ctrl（を押したまま Space を繰り返し叩く）で起動すると loader の告知が出て kernel の message が console に流れ、**止まった時に kernel の最後の message が画面に残る**（写真）。担当はユーザーと Q1、どの実装 Phase の cleared にも含めない（design.md §9.3）。
- QEMU の証拠（T1、design.md §9.2）と実機の証拠は分けて記録する。
