<!-- awesome-plan project=zedbsd record=ws256-p009 -->

# ws256-p009 試験の道具: boot-test の riscv64 の mode

Status: in-progress、test-wait（2026-10-18 q1249、P2。道具の変更と host の試験は済み、main c01387c90。QEMU での実際の起動（既存の amd64 の mode の 1 回と riscv64-virt）は p003 の T1 の依頼に入れた）

- 親: [WS256](../ws.md)。Queue: q1249（承認: 2026-10-18 ユーザー（クリック）「p009 → p003 を今始める」）。
- 範囲: [design.md](../design.md) §5・§6 の p009。`plan/tools/boot-test.sh`・`boot-test.py` に riscv64 の 2 つの mode、画面の pattern の指定、login の後に serial で打って 2 枚目の画面を読む段。`plan/ws256/tests/` の config と image の手順、QEMU の DTB（`tests/virt.dts`）。
- 許可: ユーザーの決定 #18（2026-10-18 19:33、クリック「P2 に許可」）: p009 で plan/tools の boot-test.sh・boot-test.py など 3 file だけ、既存の amd64・arm64 の mode は変えない、Q1 が merge の前に読む。`guest/serial.py` は変えていない（`--tty` は実機の判定 #15 の後）。
- 完了の条件: boot-test.sh が riscv64-virt の command を組み、pattern を変えられ、2 枚目を読む。既存の mode の挙動は不変（amd64 の image で 1 回確かめる）。

## 変えた物

- `plan/tools/boot-test.sh`:
  - `BOOT_MODE=riscv64-virt`: 引数（か `KERNEL`、既定 `build/riscv64-virt/vmunix`）を kernel の ELF として `qemu-system-riscv64 -machine virt -smp ${BOOT_SMP:-4} -m ${BOOT_MEMORY:-2G} -device ramfb -display none -no-reboot -serial unix:WORK/serial.sock,server,nowait -qmp unix:WORK/qmp.sock,server,nowait -kernel KERNEL [-initrd INITRD] [-append BOOT_APPEND] QEMU_EXTRA…`。既定の firmware（OpenSBI）。disk は無いので qcow2 の overlay を作らない。
  - `BOOT_MODE=riscv64-uboot`: 引数を disk の image として `-kernel ${UBOOT:-/usr/lib/u-boot/qemu-riscv64_smode/u-boot.bin} -drive if=none,id=boot,file=WORK/stick.qcow2,format=qcow2 -device virtio-blk-device,drive=boot`、同じ machine・ramfb・serial・qmp。
  - `BOOT_PATTERN`: 1 行に 1 つの正規表現。全部がどれかの画面に出たら合格（boot-test.py の `--pattern` を繰り返す）。未指定なら今までどおり login の prompt。
  - `BOOT_THEN`: 1 行に 1 つの段。`type:TEXT` は serial に TEXT と CR を打つ、`expect:REGEX` は画面に REGEX が出るまで待つ。最後の画面は `OUTPUT/login-after.png`・`.txt`。
  - `BOOT_INTERVAL`: 画面を見る間隔（既定 2 秒、riscv64 の mode は 0.5 秒）。
  - riscv64 の mode は常に TCG（accel の指定なし）。
- `plan/tools/boot-test.py`:
  - `--pattern` を繰り返せる（`action=append`、既定は今の login の pattern 1 つ）。複数の時は各々が別の画面で見えてよい（boot の log が 25 行を超えて最初の行が流れても数える）。最初に見えた画面を `login-N.png`・`.txt` に残す。1 つの時は今までと同じ（`login.png` だけ）。
  - `--interval`、`--serial`、`--then-type`・`--then-expect`（順に並べる）、`--then-timeout`（既定 120 秒）。
  - serial の socket は打つためだけに使う。QEMU は接続した client が読まないと guest の出力で止まり得るので、thread が読んで捨てる（保存しない・判定に使わない）。判定は画面だけ。
- `plan/ws256/tests/boot-test-host.py`（新）: QEMU 無しの host の試験。偽の QMP（font で描いた 640x400 の画面を順に返す）と偽の serial（打たれた行を記録）で boot-test.py を走らせる。

## host の試験（2026-10-18）

- `PYTHONDONTWRITEBYTECODE=1 python3 -I plan/ws256/tests/boot-test-host.py build/tmp/ws256-p009.XXXXXX` → 8 件 PASS（既定の pattern、既定で per-pattern の画面を残さない、prompt が出ない時の FAIL と画面の保存、3 つの pattern が別の画面で、1 つ欠けて FAIL、段の打つ順 root・root・uname -m、最後の画面に riscv64、`--serial` 無しの段は拒む）。
- `bash -n plan/tools/boot-test.sh` OK。偽の emulator（引数を file に書いて終わる script を `QEMU=` に）で: uefi-nvme と raspi4b の QEMU の引数が変更前の script と同じ（work の path を除いて diff 0）。riscv64-virt（INITRD・BOOT_APPEND・QEMU_EXTRA 付き）と riscv64-uboot の引数は上のとおり。`BOOT_THEN=bogus` は「unknown BOOT_THEN step」で exit 1。
- 実際の QEMU の起動は未実施（P2 は QEMU を起動しない）。amd64 の image での既存の mode の 1 回の確認と riscv64-virt の起動は T1 に依頼する（p003 の依頼と一緒）。

## 残り

- `plan/ws256/tests/config-riscv64-virt.mk`（試験の image の config）は p003 の Makefile・menuconfig の riscv64-virt と同時に置く。
- `tests/virt.dts`: QEMU の `-machine virt,dumpdtb=…`（guest は走らない）を T1 か Q1 が 1 回取って `dtc -I dtb -O dts` で置く（design §5.2）。P2 の FDT の parser の host の試験の入力。
- master の Tools の表の boot-test.sh の行（mode の一覧）の更新は Q1。
