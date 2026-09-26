<!-- awesome-plan project=zedbsd record=ws073 -->

# WS073: Bug Board の掃討（bug sweep）

<!-- awesome-plan-current:start -->
Status: incomplete（2026-09-27 開始）
Primary Milestone: MG002
Related Milestones: MG004, MG006
Objectives: O1
Parent: [Master](../master.md)
Executor: WS073 のサブエージェント（branch `worktree-agent-aefedcaf4a52a0507`）。main が merge する
Resume point: 下の Phase 一覧の最初の planned
<!-- awesome-plan-current:end -->

## 目標

2026-09-27 ユーザー「バグリストに載っているものを解決するサブエージェントを1つ追加しましょう。」
[Bug Board](../known-bugs.md) の open な bug（他の agent の担当を除く）を 1 件ずつ、再現 → 原因 → 修正 → 絞った試験 → ticket の更新で片付ける。
再現できない bug は調べた範囲と証拠を記録して tracking のまま残す。検証の無い修正を主張しない。

担当外: BUG-059・BUG-060（WS072）、BUG-046（WS056、ユーザーの判断待ち）、BUG-050（WS001 の POSIX agent。未修正なら main に確かめてから）、
BUG-027・BUG-033（性能。単独の計測を後で）。

## 規則と道具

- AGENTS.md の規則の全て。commit は `git commit -m WIP -- <paths>`、push しない、集約の `make check` を走らせない。
- 起動の確認は `plan/tools/boot-test.sh` だけ。QEMU の serial・console の log で判定しない。
- 新しいコードは [coding-style.md](../coding-style.md) の全文（`python3 plan/tools/style-check.py`: 新しい file 0、既存の file は悪化させない）。
- HAL の API（`include/hal/hal.h`・`include/hal/arch/*.h`・HAL の責務）の変更は適用せず `plan/ws073/proposed/` に置いて main へ。
- 道具: [tests/kernel-image.sh](tests/kernel-image.sh)（main の測定用 guest image `build/ws053-full-hal-guest` の ESP の vmunix だけを差し替える。clang・sshd がある）、
  [tests/g.sh](tests/g.sh)（`GUEST_RUNTIME=build/ws073-run` で `plan/tools/guest/guest.py` を呼ぶ）、
  [tests/style-diff.py](tests/style-diff.py)（変えた行だけの style-check。既存の file の「悪化させない」の確認）。

## Phase 一覧

| Phase | Bug | 内容 | Status |
| --- | --- | --- | --- |
| [ws073-p001](phase001/phase.md) | BUG-061 | devfs の `/dev/fd/N`・`/dev/stdin` を lstat・readlink・readdir で symbolic link に | cleared |
| [ws073-p002](phase002/phase.md) | BUG-065 | 同じ block device の 2 度目の mount を EBUSY に（優先度 高） | cleared |
| ws073-p003 | BUG-062 | i386 pcat の vmunix の `sched.c` の -Watomic-alignment | planned |
| ws073-p004 | BUG-063 | `truncate -s N` が無い file を作る、`mount -o rw` を受ける | planned |
| ws073-p005 | BUG-028 | 閉じた loopback の port への connect が返らない | planned |

## 判断が要る点

- ws073-p002: kernel が private に持つ boot の FAT（ESP）の公開の mount が EBUSY になった。ESP を running system から触るなら kernel の mount を公開する案（[phase](phase002/phase.md)）。
