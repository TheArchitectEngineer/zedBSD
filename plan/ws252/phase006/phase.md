<!-- awesome-plan project=zedbsd record=ws252-p006 -->

# ws252-p006: ICL の video・PXP の除外・attach・DMC

Status: in-progress（2026-10-26 q1599 P4: attach と除外は実装・build・5330 の回帰。DMC は q1601。ICL の実機は無く未確認。cleared は Q1）
Parent: [WS252](../ws.md)
Queue: q1599-i01（P4）
設計: [design.md](../design.md) §6.2 の「直す（p006）」、D1-I「C 既定で付ける」（2026-10-19 ユーザー）

## やった事

- 557c9de05: `i915.c` の `identifiers[]` に `I915_PCI_ICL_IDS`（既定で付く）。display は WS245 p045 の display 11。
- 4e316feac・e68361995: video（worker の video engine、Vulkan の video の申告）は版 12 以上だけ（executor は Gen12 の MFX の command を書く）、PXP は版 12 未満で作らない。suspend の D0 の枝は Gen9 だけ（ICL の display は xd）。
- DMC（2026-10-27 q1601、ユーザー「zedBSD-firmware/ツリーに、i915の依存としてすべて入れられませんか？」）: driver が名前を出す DMC の全部（`display-tables-xd.h` の icl_dmc_ver1_09・tgl_dmc_ver2_12・rkl_dmc_ver2_03・adls_dmc_ver2_01・adlp_dmc と `dmc-shell.c` の予備の adlp_dmc_ver2_16）を i915-firmware の package に入れた。linux-firmware dc85cced（tag 20260410）から git.kernel.org で取り、新しい 4 つは gitlab.com/kernel-firmware の同じ commit とも sha256 が一致。WHENCE は全部 i915 の節（LICENSE.i915、Redistributable）。
  - `userland/firmware/i915/`: Makefile（size・sha256・path・cache・install の対応、cache は 8 file、cache の directory は `<revision>-all-dmc`。前の 4 file の cache を壊れと言わないため）、manifest、notice、`userland/firmware/README.md` の i915 の段落。
  - zedBSD-firmware（`/home/awe/zedBSD-worktrees/zedBSD-firmware`）: local の commit fcdfccfa00130d90d77d2ca9eb45fc4357458f03（親 e13c96f = GitHub の main、push していない）。i915/ の 4 file・i915/README.md・SHA256SUMS の 4 行・README.md の i915 の行と file の表の 4 行。
  - WS233: `plan/ws233/tests/firmware-inventory.txt`（4 行）、`repo-README.head.md`（i915 の行）、`check-repository-fetch.sh`（i915 の cache の名前）。
  - 確かめた事: 自分の cache（`build/p4-fw/i915`）に production の取り方で 8 file を取り、全部が size・sha256 一致（mirror の e13c96f には無いので upstream から）、2 回目は再利用。fixture で local の zedBSD-firmware（file://）だけから 8 file を取れた。

## 確かめた事

- build warning 0。5330（素の起動、kernel 557c9de05 = p004〜p006 の全部、2026-10-26）: ssh まで 30 s、survey lit（frames +135、CRC 2 回同値、underrun 0）、i21 の基準と steady の差 0 → PASS。書き込みの digest は 13594 → 13591（display の code は変えていない。起動ごとに揺れる: 同じ main の 1 回目は 13602）。画面（keiland-shot）は desktop と Settings の初回の画面。証拠 `plan/ws245/tests/evidence/p4-ws252-p006-557c9de05.md`、PNG `plan/ws245/temp/5330/p4-ws252-p006-557c9de05.png`（git の外）。ICL の実機での attach・起動・表示・desktop は未確認（p007）。
