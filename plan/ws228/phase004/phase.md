<!-- awesome-plan project=zedbsd record=ws228-p004 -->
# ws228-p004: kernel の VT console の CSI に引数 2 つの `H` と最小限の `m` を足す

Status: planned（2026-10-14 Q1、q973、P1）
Disposition: normal
Parent: [WS228](../ws.md)
Queue: q973
依存: 無し（vi の p001 の画面が console で正しく出る前提。vi の側に回避を入れない）

## 由来

[review-1](../../../build/review/ws228-p001-review-1.md) の F1: `src/kern/tty.c` の `tty_render`（1528-1569）は CSI の中の `;` を見るとその列を丸ごと捨て、`tty_console_csi`（1415-1493）は引数を 1 つしか持たず `H` は常に (0,0)、`m` を扱わない。terminfo の `zed`（`userland/base/terminfo/zed.zti`）は `cup=\E[%i%p1%d;%p2%dH` を申告しているので、vi・less などの全画面の命令が console で壊れる。
ユーザーの決定（2026-10-14 クリック）:「kernel の console を直す」。

## 範囲

- CSI の引数を複数（少なくとも 2 つ、省略は既定値）持てるようにし、`H`・`f`（行;桁、1 起点）を実装する。`m`（SGR）は 0（解除）・1（太字）・7（反転）・27・22 と前景・背景の 30〜37・39・40〜47・49 の最小限（表示できない属性は無視して列は捨てない）。
- 既存の 1 引数の列（A・B・C・D・G・J・K など）の振る舞いを変えない。知らない終端・private の列（`?`）は今まで通り捨てる。
- `zed.zti` の申告と console の実装を突き合わせ、申告と実装が食い違う capability を一覧にして phase.md に書く（直すのは cup と SGR に関わる物。他は一覧だけ）。
- HAL の API は変えない（tty の内部だけ）。`plan/coding-style.md` の全文を適用。

## 確かめ

- amd64 の build warning 0。tty の CSI の解析を host で試せる形があればその試験（無ければ解析の関数を切り出して host 試験を作る）。
- QEMU（T1）: console の login の後に `printf '\033[5;10HX\033[7mR\033[0m'` などで位置と反転を PNG で判定、vi（p001 が merge 済みなら）を console で開いた画面の PNG。依頼の文を phase.md に。
