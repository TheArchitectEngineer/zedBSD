<!-- awesome-plan project=zedbsd record=ws262 -->

# WS262 boot loader（bootloader/）をコーディング規約の全文に合わせる

- ID: ws262、Master: [master.md](../master.md)、Primary Milestone: MG001
- Status: incomplete（2026-10-18 Q1 作成、p001 を P2）
- 起点（2026-10-18 ユーザー）:「UEFIのコードがコーディング規約から逸脱しているようです。bootx64.cとか。ブートローダもコーディング規約に合わせてほしいです。」（担当はクリック「P2」）
- 規則: [plan/coding-style.md](../coding-style.md) の全文。書き方だけを直し、動作は変えない。

## 完了の条件

1. `bootloader/` の全部（uefi・pcat・pc98・bios・x68k・sparcv9・common・include・tests）が plan/coding-style.md の全文に合う（file ごとの点検の記録）。
2. 動作は変わらない: 作った物（BOOTX64.EFI・各 loader の binary・object）を直す前と比べ、code の差が無い事（差が出る所は理由を記録）。build warning 0。
3. 起動の確かめは最後に T1 に 1 回（amd64 UEFI・PC-98・Pi 4 など、変えた loader の分）。

## Phase

| Phase | 目的 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 点検と直し（loader ごとに commit）、binary の比べ、T1 への依頼の文面 | in-progress（q1256、P2。直しは全部済み、2026-10-19 T1 への依頼の文面を Q1 に。判定は Q1） |
