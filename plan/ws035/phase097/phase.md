<!-- awesome-plan project=zedbsd record=ws035p097 -->

# ws035-p097: kmsg=quiet（kernel の message を console に出さず dmesg へ）と、表示の lease の間の keyboard

Phase ID: `ws035-p097`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント。HAL の早期 console の部分は提案で承認待ち）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て。ユーザー「カーネルパラメータでメッセージをコンソールに出さずにdmesgのような方法で
保存だけする指定をします。これにより完全なグラフィカル起動を実現します。」）

## 実装（2026-09-28）

- boot parameter（`src/kern/boot.c`・`include/kern/boot.h`）: 既知の名前に `kmsg=`（`quiet`・`console`）と `login=`
  （`graphical`・`console`、使うのは p098）。他の値は parse error（起動が見える error で止まる）。
  `kern_boot_parameters_token_present`（parse の前の token の検査）。
- klog（`klog.c`）: `kern_log_set_quiet`・`kern_log_quiet`。quiet の間は record を ring（dmesg）と debug port へだけ、
  `kernel_putc` へは出さない。`entry.c` が `kern_log_init` の直後に `kmsg=quiet` を読む（最初の driver の log の前）。
- 文字 console（`text-display.c`・`text-display.h`）: ops に任意の `reveal`、`kern_text_reveal()`（log も loud に戻す）、
  `kern_text_kernel_putc`（`kernel_putc` として公開: quiet の間は HAL が自分で出す文字を log へ）。
  pcat の text 層（`drivers/platform/pcat/graphics/text.c`）: quiet の起動では cell を保つが描かず（画面の logo を消さない）、
  reveal で framebuffer 全体を消して cell と cursor を描く。snapshot は隠れていても描く。
- reveal する時: console を読む process（`console.c` の `console_read`: console getty の login prompt、graphical login の
  fallback）、kernel の panic（`panic.c` の `__libc_panic`・`kern_fatal`）。
- keyboard（`tty.c`・`tty.h`）: `tty_console_input_hold`・`unhold`。GPU の表示の lease（`gpu.c` の claim の成功から
  release・close まで、session ごとに数える）の間、keyboard の event を文字 console に渡さない（greeter や desktop で打った
  password が console の getty に入り echo されない）。serial の console の入力は止めない。
- **HAL の提案（未適用）**: [proposed/hal-quiet-console.md](../proposed/hal-quiet-console.md)・`hal-quiet-console.diff`
  （`src/hal/amd64/bsp-pcat/cons.c`: `kmsg=quiet` で早期 console が framebuffer を消さず描かず、右上の進捗の panel を logo の
  背景色で塗る。hal.h は不変）。当てない今は、quiet の起動で HAL の早期の行が logo の上に残る。
- 文書: `docs/reference/kernel-boot-parameters.md` §7a（`kmsg=`）。

## 検証（amd64、QEMU、2026-09-28）

- 静かな起動（HAL の提案を当てた build、`boot-shots.py --cfg logo=logo.ppm --cfg kmsg=quiet`）: 1.4〜16.4 秒は logo だけ、
  console getty の read で console が現れ、userland の行と login prompt だけ（kernel の message は無い）:
  `p097-20260928-quiet-boot-logo.png`・`-quiet-boot-login.png`。
- `plan/ws035/tests/zdesktop-p097.sh`（Venus の guest、`kmsg=quiet` の image の写し、提案を当てた build）PASS: dmesg に
  `boot: parameters ... kmsg=quiet`・HAL の `A64 TIMER TICK`・`vfs: runtime filesystems mounted`。zdesktop が lease を
  持つ間に打った `hello` は console の login prompt に入らず（`p097-20260928-hold.png`）、zdesktop の終了後の `world` は入る
  （`-release.png`）。
- `kmsg=loud`: `boot: parameter parsing failed (3); entering idle.` が画面に出て止まる。
- 回帰（提案を当てない今の tree）: boot test（既定の cfg）PASS、zdesktop-p095（greeter から session と Log Out、GPU の
  受け入れ）PASS。
- 規約: 変えた kernel・driver の file の style-check は増えていない。build warning 0。
- 実機: 未実施。

## 制限・残り

- HAL が直接 `hal_fatal` で止まる場合（kernel の `kern_fatal` を通らない）は quiet の console を reveal しない（debug port と
  serial には出る）。kernel の開発には `kmsg=console`。
- HAL の早期 console の提案の承認。
- reveal の後は kernel の message が console に出る（従来の console に戻る）。
