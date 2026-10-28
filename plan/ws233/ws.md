<!-- awesome-plan project=zedbsd record=ws233 -->

# WS233: firmware を awemorris/zedBSD-firmware から取る

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。2026-10-14 ユーザー「WS233はユーザの方で確認できました。CIが通ればOKとします。CI成果物で最終UATする予定です。」）
<!-- retained-after-completion --> phase001・phase002 は source の comment（platform/amd64/vmunix.mk、userland/firmware/zedbsd-firmware.mk、tools/menuconfig.py、userland/firmware/license-agree.sh）が設計の出典として参照している。消すか残すかは Q1 の判断待ち。
Completed: 2026-10-27（q1608）
Primary Milestone: MG002
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

build が取る全ての firmware の binary を一覧にし、license が再配布を許す物だけを `awemorris/zedBSD-firmware`（GitHub、public）に license の文書と出所（元の URL・commit・sha256）とともに置き、build はそこから固定の commit で取り、今と同じ sha256 で確かめる。CI の submodule の clone をやめられるならやめる。

## ユーザーの決定

- 2026-10-14（原文）「リポジトリ awemorris/zedBSD-firmware を作って、RPi4のコンパイル済みファームウェアを含め、再配布してよいバイナリであることを確認してそこにすべて入れて、GitHubから落とす形にできませんか？」（きっかけは CI の run 38531306764 が git.kernel.org の 503 で intelbt の firmware を取れず失敗したこと）
- 2026-10-14「それでよいです。ghコマンドで作ること、アップロードすることを許可します。ファームウェアのディレクトリごとにライセンスファイルもわかりやすく置くのがよいです。makeで個別ファームウェアをダウンロードしたとき、初回は ライセンス表示と Agree するかの yes/no があるのがいいと思います。CIでは yes | make でいいのでは。」
- 2026-10-14「はい、何のファームウェアか、著作権表示、ライセンス文、yes/noですね。」（初回の表示の順）
- 2026-10-14（クリック）Cypress の brcmbt・bcm43455 は「入れる」。RPi4 の DTB は「dts を repo に置く」。VBT・OpRegion の raw は「VBTはテスト用だったので、もう不要と思います。削除できませんか？」
- 2026-10-15「5330 VBT削除は私の指示で、コミットから削除になりました。もしあるなら削除しておいてください。」「vbt-decodeは削除してください。」
- 2026-10-14「では作ってください。ライセンスをスクロール表示して、きちんと法的な見た目にしてほしいです。」→ p002。
- 2026-10-14「WS233はユーザの方で確認できました。CIが通ればOKとします。CI成果物で最終UATする予定です。」

## 結果

- p001: 一覧と license の監査、repo の中身、取り方の切り替えと同意の表示、RPi4 の firmware の取り方、CI（P1 の 36966480d・5090f6202 を main に merge）。Q1 が `gh repo create awemorris/zedBSD-firmware --public` と push（ddd36a0d2f02、68 file、SHA256SUMS 一致）、`userland/firmware/zedbsd-firmware.mk` に SHA（2fd3cc622）。host で 6 つの firmware と rpi4-boot を取り全部一致、intelbt の接続先は raw.githubusercontent.com だけ（strace）。2026-10-14 夜に `nvidia-gsp/`（WS037 の GSP-RM）を足して push（3cad3cc0aa84）。
- VBT・OpRegion の raw と読む試験は git rm（531dfe8d4）、2026-10-15 に 5330 の VBT・OpRegion の raw、それを含む patch 7 本、vbt-decode.txt も削除。
- submodule `vendor/raspberrypi-firmware` は tree に無い（2026-10-28 に `.gitmodules`・`vendor/` が無いことを確かめた）。
- p002: menuconfig の Build boot image の license の TUI（scroll、Accept/Decline）。host の確かめまで。
- T1 の image の試験（T1-619）はユーザーの確かめで取り消し。

## 制限・移管

- p002 の license の画面の端末での目視は未実施。
- zedBSD-firmware の repo を作り直す道具（`make-firmware-repo.sh` ほか）と `run-i915-firmware-package-test.sh` は plan/tools/ へ移す（Q1 の OK の後）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws233-p001 | 一覧と license の監査、repo の中身、build の取り方の変更、初回の license の同意 | in-progress のまま閉じた（repo の作成・push と取り方は main、ユーザーの確かめ） |
| ws233-p002 | menuconfig の Build boot image で firmware の license を TUI で聞く | in-progress のまま閉じた（実装と host の確かめ、端末での目視は未実施） |

## 記録の所在

Phase の計画・結果・試験は削除する（上の retained の 2 つは Q1 の判断の後、道具は plan/tools/ へ）。git の commit `fb1f6afee` の `plan/ws233/` にある。Queue の履歴は [plan/history](../history/index.md)。
