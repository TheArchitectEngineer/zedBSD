# HAL 変更の提案: 静かな起動の早期 console（ws035-p097）

状態: **提案（未適用、承認待ち）**。差分: [hal-quiet-console.diff](hal-quiet-console.diff)
（SHA256 `3f63a8411d5b3684c8bb790e86e049a0d70122626c6f1aca579e5413cbb8566b`、`src/hal/amd64/bsp-pcat/cons.c` だけ）。

## 何のため

ユーザーの指示（2026-09-28）「カーネルパラメータでメッセージをコンソールに出さずにdmesgのような方法で保存だけする指定をします。
これにより完全なグラフィカル起動を実現します。」。kernel の側（`kmsg=quiet`、src/kern・src/drivers）は p097 で入れた。
残るのは HAL の早期 console（kernel の console ができる前の `hal_puts`・`hal_printf`、amd64 の pcat）で、今はそれが
(1) 起動の最初に framebuffer 全体を黒で消し（loader の logo が消える）、(2) `zedBSD amd64 HAL`・`A64 ...` の行を描く。
また kernel の入口（`locore.S`）が進捗の白い block（stage 4）を右上に描く。

## 変更（hal.h は変えない、HAL の責務の変更: 早期 console の振る舞い）

`prekern_pcat_cons_init` で、boot parameter（`hal_get_arch_handoff("boot.command-line")`）に token `kmsg=quiet` が丸ごと
あれば:

- framebuffer を消さない。
- 右上の進捗の panel（136x40、loader の stage と kernel の入口の block の場所）を、その左隣の画素の色（logo の背景）で塗る。
  `locore.S` は変えない（asm で parameter を読まずに済む）。
- `console_suspended = 1`: 早期 console は描かない（debug port は従来どおり全部の行を受ける）。

`kmsg=quiet` が無ければ振る舞いは今と同じ。

## 検証（差分を当てた build で、2026-09-28、QEMU OVMF・NVMe・標準 VGA）

- `plan/ws035/tests/boot-shots.py IMAGE build/ws035-p097/quiet --cfg logo=logo.ppm --cfg kmsg=quiet`: 画面は loader の logo
  （1.4 秒）から console getty が console を読む（16.4 秒）まで logo だけ、その後 console に userland の行と login prompt
  （kernel の message は無い）。`/home/awe/zedBSD-rpi4/build/ws035-shots/p097-20260928-quiet-boot-logo.png`・`-quiet-boot-login.png`。
- 当てない build（今の tree）でも起動は変わらない（`kmsg=quiet` では HAL の早期の行が logo の上に残り、getty で console に
  切り替わる）。boot test（既定の cfg）PASS。

## 当てない場合

`kmsg=quiet` の起動で HAL の早期の行（数十行）が logo の上に出て、kernel の console が隠れた後もそのまま残る。
完全なグラフィカル起動にはこの差分が要る。

## 適用（2026-09-28）

ユーザー「HALのdiffは承認します。」で適用（commit 3f3a6072）。desktop の image の build と boot test は PASS。boot test の image は
`kmsg=console` のため、`kmsg=quiet` の経路（logo を残す、進捗の枠、console の停止）の確認は未実施。
