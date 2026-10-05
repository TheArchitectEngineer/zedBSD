<!-- awesome-plan project=zedbsd record=ws174 -->
# WS174: 起動時の Ctrl / Shift で boot の選択を変える

Master: [master](../master.md)
Status: planning（2026-10-05 夜 追加。設計の第 3 版まで。ユーザーが仕様を変更: Ctrl = kmsg を console（logo なし、640x480 の希望）、Shift = login を console、config の形式は変えず UEFI の bootloader だけ。ユーザーの実装の指示あり（BIOS は後日））
Primary Milestone: MG003
Related: MG006（graphical boot）

## 由来

ユーザー（2026-10-05 夜）「おっと、イメージを実機で起動したら、カーネルの起動の途中でpanicしたとみられますが、グラフィカルブートなのでわかりません。ブート時にctrlキーが推されていたらsafe boot optionsの設定に切り替えるように、ブートローダとブートコンフィグファイルを変更したいです。設計だけできますか？」

## 目標

boot の時に **Ctrl** が押されていたら UEFI の bootloader が kernel に渡す `kmsg=` を `console` にし（logo を出さず、GOP に 640x480 を希望）、**Shift** が押されていたら `login=` を `console` にして（sessiond が終わり getty が始まる）kernel を起動し、kernel の最後の message が画面で読める。boot の config の file の形式は変えない。kernel・init・sessiond は変えない。

ユーザーの仕様の変更（2026-10-05 夜）と決定（logo を落とす: Yes、640x480: Yes、1 秒止める: No）、実装の指示「OKです。ブートローダの仕様変更を実装してください。BIOSは後日でよいです。」は [phase001/phase.md](phase001/phase.md) の末尾の 3 節。

## Phase

| Phase | 内容 | 状態 | 依存 |
| --- | --- | --- | --- |
| [ws174-p001](phase001/phase.md) | 設計（UEFI loader の Ctrl・Shift の検出、parameter record の書き換え、kernel への渡し方、docs、試験）。全文 [design.md](phase001/design.md) **第 3 版** | in-progress（第 3 版、design-reviewer のレビュー済み。Q1 の判定待ち、2026-10-05 夜） | — |
| ws174-p002（案） | module と host 試験: `bootloader/common/boot-override.c/.h`（record の書き換え、純粋）、`bootloader/uefi/boot-keys.c/.h`（Ex protocol の検出）、`uefi.h` の宣言、`vmunix.mk` の object の rule と link、`tests/config-amd64-keys.mk`。host 試験 O1〜O11・K1・K2 | planning（design.md §10。Queue 化は Q1） | p001 |
| ws174-p003（案） | UEFI loader の統合: `bootx64.c` の S0〜S2・書き換えの適用・640x480 の希望・告知・`A64 PARAMS OVERRIDE`。docs 4 件。`run-boot-keys-qemu.sh`。T1 の QEMU 5 cell（Q1 経由） | planning | p002 |
| ws174-p004（案、後日） | BIOS PC/AT: int 16h AH=02h の Shift/Ctrl の flag、`zbl_boot_override_apply()` の i386 の object、告知、`stage2_end` の記録 | planning（ユーザー「BIOSは後日でよいです」） | p002 |
| ws174-p005（案） | 規約の全文の見直し（新しい関数と変えた関数） | planning | p003（p004 をやるならその後） |

## 受け入れ（WS）

- 実機 5330 で、Ctrl と Shift（を押したまま Space を繰り返し叩く）で起動すると loader の告知 2 行が出て kernel の message が console に流れ、**止まった時に kernel の最後の message が画面に残る**（写真）。Ctrl だけ・Shift だけ・修飾 key 単独で効くかは firmware の事実として記録する。担当はユーザーと Q1、どの実装 Phase の cleared にも含めない（design.md §9.3）。
- QEMU の証拠（T1、design.md §9.2）と実機の証拠は分けて記録する。
