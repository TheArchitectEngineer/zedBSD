<!-- awesome-plan project=zedbsd record=ws233 -->

# WS233: firmware を awemorris/zedBSD-firmware から取る

Status: incomplete（2026-10-14 Q1 起動、P1）
Primary Milestone: MG002
Parent: [Master](../master.md)
Source: 2026-10-14 ユーザー「リポジトリ awemorris/zedBSD-firmware を作って、RPi4のコンパイル済みファームウェアを含め、再配布してよいバイナリであることを確認してそこにすべて入れて、GitHubから落とす形にできませんか？」。きっかけは CI の run 38531306764 が git.kernel.org の 503 で intelbt の firmware を取れず失敗したこと（retry は 6fd32c175 で入れた）。

## 目標

- build が取る全ての firmware の binary（`userland/firmware/*`、`vendor/raspberrypi-firmware` の submodule から使う RPi4 の起動の firmware、`vendor/intel-vbt` など）を一覧にし、それぞれの license が再配布を許すことを確かめる。
- 許す物だけを `awemorris/zedBSD-firmware`（GitHub、public）に license の文書と出所（元の URL・commit・sha256）とともに置く。
- build はそこから固定の commit で取り、今と同じ sha256 で確かめる。元の配布元は予備（mirror）として残すかは P1 の設計で決める。
- CI（.github/workflows）の submodule の clone をやめられるならやめる。

## Phase

| ID | 内容 | 状態 |
| --- | --- | --- |
| [ws233-p001](phase001/phase.md) | 一覧と license の監査、repo の中身の準備（local）、build の取り方の変更、初回の license の同意（yes/no） | in-progress（P1 の実装と host の確かめは済み。Q1 の push・placeholder・submodule の除去と T1 の image の試験が残り） |
| [ws233-p002](phase002/phase.md) | menuconfig の Build boot image で firmware の license を TUI（scroll、Accept/Decline）で聞く | in-progress（P1 の実装と host の確かめは済み。端末での目視は未実施） |

repo の作成と push は Q1 が、監査の結果をユーザーに示してから行う。

## 決定（2026-10-14 ユーザー）

「それでよいです。ghコマンドで作ること、アップロードすることを許可します。ファームウェアのディレクトリごとにライセンスファイルもわかりやすく置くのがよいです。makeで個別ファームウェアをダウンロードしたとき、初回は ライセンス表示と Agree するかの yes/no があるのがいいと思います。CIでは yes | make でいいのでは。」

- repo は public。`gh` での作成と push（upload）は許可済み（Q1 が監査の結果を確かめてから行う）。
- firmware の directory ごとに license の file を置く。
- make で firmware を初めて取る時に license を表示して yes/no で同意を聞く。CI は `yes | make`。
- 初回の表示の順（2026-10-14 ユーザー「はい、何のファームウェアか、著作権表示、ライセンス文、yes/noですね。」）: 何の firmware か → 著作権表示 → license の全文 → yes/no。
- 判断（2026-10-14 ユーザー、クリック）: Cypress の brcmbt・bcm43455 は「入れる」。RPi4 の DTB は「dts を repo に置く」。VBT・OpRegion の raw（vendor/intel-vbt、plan/ws031/display-ref/）は「VBTはテスト用だったので、もう不要と思います。削除できませんか？」→ P1 が参照を調べ、Q1 が git rm する。
- P1 の 36966480d（取り方の切り替え・同意の表示・RPi4 の firmware の取り方・CI）を main に merge（placeholder は空で今の取り方のまま）。
- 2026-10-14 Q1: P1 の 5090f6202（DTB の source、Cypress の README、VBT の参照の除去）を merge。VBT・OpRegion の raw と読む試験 6 file を git rm（531dfe8d4）。`plan/ws031/handover/increment-results/` の patch 7 本（VBT の bytes を含む、約 60 MB）は未決（ユーザーに確認）。
- 2026-10-14 Q1: `gh repo create awemorris/zedBSD-firmware --public` と push（commit ddd36a0d2f0215f4a9000d1c0796e54aeaf1a79f、68 file、SHA256SUMS 一致）。`userland/firmware/zedbsd-firmware.mk` にその SHA を記入（2fd3cc622）。host で 6 つの firmware と rpi4-boot を新しい cache に取り、全部一致。intelbt は strace で接続先が raw.githubusercontent.com（185.199.108〜111.133）だけ、git.kernel.org への接続なしを確認。
- 残り: submodule `vendor/raspberrypi-firmware` の除去（手順は phase001/proposed/remove-submodule.md）、T1 の image の試験（rpi4・amd64 の build、FAT の中の firmware が前と byte 一致、raspi4b の boot-test）。
- 2026-10-14 ユーザー「では作ってください。ライセンスをスクロール表示して、きちんと法的な見た目にしてほしいです。」→ ws233-p002（menuconfig の license の画面）。きっかけは、今の Build boot image が stdin を端末のまま make に渡し、license の質問が log にだけ出て止まって見える問題（Q1 の source の読み、未確かめ）。
- 2026-10-14 ユーザー「WS233はユーザの方で確認できました。CIが通ればOKとします。CI成果物で最終UATする予定です。」→ T1-619 は取り消し。残りは submodule の除去（CI が通った後）。
- 2026-10-14 夜 Q1: zedBSD-firmware に `nvidia-gsp/`（WS037 の GSP-RM 570.144、LICENCE.nvidia 2.1.2）を足して push（3cad3cc0aa84a5991ed558869213de764ed077ef）、zedbsd-firmware.mk の revision を更新。nvidia-gsp・intelbt・i915 を新しい cache に取り、接続先は GitHub だけで全部一致。
