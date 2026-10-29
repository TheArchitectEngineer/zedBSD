<!-- awesome-plan project=zedbsd record=ws307-p013 -->

# ws307-p013 文書: installer の how-to と reference

- 親: [WS307](../ws.md)。Queue: q1665 / q1665-i09（担当 P4、2026-10-29、worktree p4-ws307m、基点 main 3f2370248）。
- Status: in-progress（文書を書いて links.py と anchor の確かめまで。Q1 の判定待ち）
- 範囲（Q1）: `docs/howto/boot-and-storage.md` の古い zedinst の節を新しい installer に置き換え、「installer の image を運用に使ってはいけない」規則と root 起動相当の扱い（J5）、`docs/reference` の installer・install-admin の頁。ws.md の行の install-admin・efiboot・mkswap・diskpart・mkfs・loader の UFS も含める。docs/ は目標の設計を書き、plan/ を参照しない。
- 依存の p012（実機）は未着手だが、文書は目標の設計を書く物なので Q1 の割り当てで先に行った。

## 変えた文書

- `docs/howto/boot-and-storage.md`: 題と Status を直した。zedinst の節（Install from the running installation disk・coexistence・dedicated・PC98 FAT・Shell and recovery）を「Install zedBSD with the installer」（image の規則（J5）・`make installer-image`・手順 S1〜S11・導入先の確かめ・キャンセルと失敗・手作業の道具）に置き換えた。「What the loader selects」に `rootpart:` の path、「Native root」に導入先の形と kernel の入れ替えの手順（新しい名前 → sync → rename → sync、p008 の j3 の sync の確かめに従う）。Kernel image placement は独立の節に移した。Failure diagnosis に `A64 KERN UFS`。開発の管理情報の段落（q190）を外した。
- `docs/reference/installer.md`（新規）: 規則（J5）、image の build の条件・順・形・`/opt/installer`・unit の形・index、installer の画面と検査、install-admin（呼び出し元の確かめ・exit・probe の出力・session の要求と出力・install の段・settings の内容）、導入先の型と label と `zedbsd.cfg`、efiboot、source。
- `docs/reference/block-administration.md`: diskpart の add・delete・batch（32 操作・set の attribute・16 entry・EOPNOTSUPP・exit 3）と `zedBSD-unassigned` の型。古い「installer integration remain separate」を外した。
- `docs/reference/image-formatters.md`: mkswap の partition の mode、512・4096 の sector（UFS の fragment 4 KiB）、FAT32 の乱数の serial、`--check-size` の `--sector-size` と出力の欄。
- `docs/reference/kernel-boot-parameters.md`: §9.2（amd64 UEFI の `kernel=rootpart:`・`logo=rootpart:`、PARTUUID だけ、同じ disk で 1 つ、他の disk の重なりで止まる、UFS の reader の規則、`A64 KERN UFS`）と source の表の行。
- `docs/reference/boot-provenance.md`・`block-command-output.md`: 旧 zedinst の記述（rootfs.img の照合・`/run/zedinst/payload`）を一般の形に直した。
- `docs/reference/README.md`・`docs/howto/README.md`: 新しい頁の行と題。

## 担当の判断（推しで進めた所、Q1 へ報告済み）

- efiboot は未実装（p007 は HAL の差分の承認待ち）だが、spec §5.8 の目標の設計として reference に書いた。spec に無い引数の形（`efiboot create NAME DISK PARTITION LOADER-PATH`・`list` の出力の中身）は担当が決めた。p007 で変われば文書を先に直す。
- installer の導入の段の表は今の実装の 10 段で書き、Boot#### は `finish` の段で書く形にした（spec §7.2 の段 11 を finish に含めた）。
- 物理の USB への書き込みは今の文書の方針（raw device の command を書かない）を保ち、「host の disk image の writer で drive 全体へ」とだけ書いた。QEMU の試し方（Venus の GPU が要る）は how-to に書かなかった。

## 確かめ

- `python3 -I plan/tools/record-audit/links.py <変えた docs の 9 file>` → TOTAL 0 broken in 9 files。
- 頁の中の anchor（`#…`）を見出しから作った slug と照らす小さな script（scratchpad）→ anchor errors 0。
- 記述は source（`userland/desktop/installer/`・`admin/` の header と main.c・session.c、`diskpart/main.c`、`mkfs/block-command.c`、`mkswap/swap-device.c`・`swap-format.c`、`bootloader/uefi/root-config.c`・`bootx64.c`・`bootloader/common/ufs-read.c`、`platform/amd64/installer-image.mk`・`installer-items`）と照らした。
- 未実施: 文書の手順を実機・QEMU で辿る確かめ（p011・p012 の後）。
