<!-- awesome-plan project=zedbsd record=ws073 -->

# WS073: Bug Board の掃討（bug sweep）

<!-- awesome-plan-current:start -->
Status: incomplete（2026-09-27 開始）
Primary Milestone: MG002
Related Milestones: MG004, MG006
Objectives: O1
Parent: [Master](../master.md)
Executor: WS073 のサブエージェント（branch `worktree-agent-a4f5b29b09938aa63`。p001・p002 は `worktree-agent-aefedcaf4a52a0507`）。main が merge する
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
| [ws073-p003](phase003/phase.md) | BUG-062 | i386 pcat の vmunix の `sched.c` の -Watomic-alignment（main の ws036-p021 の修正を build で確認） | cleared |
| [ws073-p004](phase004/phase.md) | BUG-063 | `truncate -s N` が無い file を作る、`mount -o rw` を受ける | cleared |
| [ws073-p005](phase005/phase.md) | BUG-028 | 閉じた port への connect を ECONNREFUSED に、自分の interface の address への packet を lo0 で届ける | cleared |
| [ws073-p006](phase006/phase.md) | BUG-067 | devfs の文字 device の node が chmod・chown を受ける（`mesg n`） | cleared |
| [ws073-p007](phase007/phase.md) | BUG-068 | 多 thread の process の execve が、joiner に先に reap された兄弟を待ち続ける | cleared |
| [ws073-p008](phase008/phase.md) | BUG-069 | 端末と pty の読み書きが waitq_sleep の EAGAIN を失敗として返す（console の POSIX-R2 10 回連続 status 0） | cleared |
| [ws073-p009](phase009/phase.md) | — | kernel の boot の FAT を公開する: BOOT を /boot、ESP を /boot/esp（ユーザーの判断 2026-09-27） | cleared |
| [ws073-p010](phase010/phase.md) | BUG-071 | FAT に普通の道具で file を作れる（mount の見せる mode で見せる） | cleared |
| [ws073-p012](phase012/phase.md) | BUG-072 | FAT32 の metadata を仕様どおりに（`..`、FSInfo、日時）、boot の FAT の sync | cleared |
| [ws073-p013](phase013/phase.md) | BUG-029（BUG-052 の node） | 多くの tmpfs の file が system 全体の inode を尽くさない（heap の inode、cache 16384、tmpfs の共有の上限） | cleared |
| [ws073-p011](phase011/phase.md) | BUG-070 | USB HID の keyboard が keypad・Num Lock・Print Screen・日本語の key などを出す | cleared |
| [ws073-p014](phase014/phase.md) | — | amd64 は /boot・/boot/esp を自動で見せず fstab に任せる。fstab の ESP の mount は kernel の hold を adopt する | cleared |
| [ws073-p015](phase015/phase.md) | BUG-073 | 最終の layout: `bootN:` の file を持つ boot の slot を /boot/boot0〜3 に自動で mount（amd64 UEFI も、起動後の `swapon bootN:` も）、ESP は fstab、使用中の file は読めるが書けない。claim のある FAT の line の書き戻し（BUG-073）を修正 | cleared |
| ws073-p016 | BUG-074 | FAT の readdir の位置を entry の物理的な位置にし、走査中の unlink で entry を飛ばさない（`rm -r`） | planned |

## 判断が要る点

- （解決 2026-09-27）ws073-p002: kernel が private に持つ boot の FAT（ESP）の公開の mount が EBUSY になった。ESP を running system から触るなら
  kernel の mount を公開する案（[phase](phase002/phase.md)）。ユーザーの判断（原文）: 「ESPは/boot/espにします。/bootはBOOTという名前のFATパーティションですね。
  UEFIのみのイメージでBOOTパーティションがない場合もあります。」→ BOOT の FAT を `/boot`、ESP を `/boot/esp` に公開する。BOOT が無い UEFI だけの image では
  `/boot` はただの directory で ESP は `/boot/esp`。ws073-p009 で行う。
- （解決 2026-09-27）amd64 の /boot・/boot/esp: ユーザーの判断（原文）「amd64 のUEFIおよびハイブリッドのイメージでは、カーネルが特殊な処理で/bootや/boot/espを
  マウントせず、fstabに任せてください。つまり、デフォルトの配布イメージではマウントしなくていいです。インストーラがfstabに書けば済むことです。」→ ws073-p014。
- （解決 2026-09-27、最終の layout、p009・p014 の公開を置き換える）ユーザーの判断（原文）「amd64 UEFIでも、/boot/boot0みたいなマウントは自動でやりましょう。
  ESPはfstabです。スワップだけでもマウントします。rootfs.imgは読み込み専用なので、書き込みできなくても、読み込めていいと思います。」→ ws073-p015（cleared）。
- ws073-p015（既定を選んだ、可逆）: ESP 自身が `bootN:` で参照される slot なら `/boot/bootN` にも出す（fstab の `/boot/esp` は同じ状態の別の view）。
  root に promote された slot は出さない。rpi4 の legacy ARM overlay の boot の FAT は `/boot/boot0` に。起動後の `swapon bootN:PATH` は成功時にその slot を出す
  （[phase](phase015/phase.md)）。
- ws073-p010（既定を選んだ、可逆）: FAT での file の作成は要求の mode を捨て、mount が見せる mode（0755 root:wheel）で見せる（msdosfs と同じ）。
  以前の「一致しなければ EOPNOTSUPP」に戻すなら fat.c の 2 つの関数を戻す（[phase](phase010/phase.md)）。
