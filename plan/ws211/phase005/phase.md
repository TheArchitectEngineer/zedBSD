<!-- awesome-plan project=zedbsd record=ws211-p005 -->

# ws211-p005: 時計の 1 分ごとの更新を時計の所だけにする

Parent: [WS211](../ws.md)
Status: in-progress（実装済み agent/p3 fab6d4df1、Pi は未確認。2026-10-12 P3）
Phase disposition: normal
Queue: P3（2026-10-12 ユーザー「時計の部分だけに最適化」、Q1 が「小さい物は review 無しで先に実装してよい」）

## 由来と事実

- v3d.perf=1 の boot（scratchpad/p3g3/b10）では、何もしていない時も 1 分ごとのコマが全画面だった。99 draw、exec 36ms、render 36ms、max_us 73ms。
- 原因: `shell.c` の tick が、分が変わると `server->dirty = 1`（全画面）にしていた。

## 直し（実装済み）

- 分が変わった時は、system bar の帯（幅は全体、高さは `KWL_GLASS_BAR + BAR_CLOCK_REACH`（16）。bar の glass と影が届く範囲）だけを `kwl_damage_rect` で damage にする。時計の幅が変わると status の pill も動くので、時計の pill だけでなく bar の帯の全体を描き直す。
- App Home が見えている時は、時計の帯（出力の上端から最初のページの icon の上まで）も damage にする（`kwl_home_clock_damage`）。
- glass の look でない時、greeter・lock の時は、今どおり全画面にする（greeter は自分の時計を持つ）。

## 確かめ

- build: rpi4 の wayland は warning 0。amd64 は shell.o・home.o を warning 0 で compile した。
- Pi（未確認）: 何もしていない時の 1 分ごとのコマが、`KWL DAMAGE` で bar の帯になること。`KWL DRAWS` が減ること。v3d の perf で exec・render が減ること。時計の表示と、status の pill の位置が正しいこと（時計の幅が変わる 9:59→10:00 など）。
