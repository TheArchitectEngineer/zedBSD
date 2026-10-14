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
| ws233-p001 | 一覧と license の監査、repo の中身の準備（local）、build の取り方の変更 | in-progress（P1） |

repo の作成と push は Q1 が、監査の結果をユーザーに示してから行う。
