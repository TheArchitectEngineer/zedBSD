<!-- awesome-plan project=zedbsd record=ws035p055 -->

# ws035-p055: damage（buffer age と scissor）

Phase ID: `ws035-p055`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-27、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。元は sq001 の planned）

## 範囲

[compositing-design.md](../compositing-design.md) D4 の 2 回目: 変わった所だけを描き直す（swapchain の画像ごとの buffer age の分の damage の
union を scissor に）。

## 設計（決定、2026-09-27）

- **保守的に**: 範囲の分かる変化は 2 つだけ。それ以外の全部（今までの `server->dirty = 1` の 70 余りの場所）は今まで通り全画面。
  1. **pointer の移動**: cursor の前と後の場所（pointer の周り 64 px の四角）。glass の look では、前と後の両方が窓の本体（client の領域）の
     上で、見た目が静か（animation・移動・pull・drag・Home・Wiseview・desktop の slide・see-through・popup・menu が無い）ときだけ。title bar の
     button の hover 等は描くときに pointer を読むので、bar の上の動きは全画面。client の cursor surface があるときは全画面。
  2. **窓の新しい画像**（commit）: 同じ大きさの画像が前の画像を置き換えた toplevel の窓（sub-surface・popup・cursor・最初の画像・消えた画像・
     全画面は除く）の本体。glass の look では、見た目が静かで、上の窓（本体と浮いた title bar）が本体から 96 px（p057 のぼかしの届く距離）より
     近くに無いときだけ。
- `server->damaged`・`server->damage[4]`（damage.c）に集め、frame で `compose_region`（compose.c）が、この frame と、その swapchain の画像が
  前に描かれてから後の frame の damage（履歴 8 frame）の外接矩形を求める。全画面の frame を含む・履歴より古い・一度も描かれていない・
  出力の 70% を超える、なら全画面。部分のときは render pass を LOAD の pass（`compose->pass_load`、p057 の resume と共用、遅延で作る）にし、
  scissor をその矩形に。p057 の backdrop の後の再開も同じ scissor に戻す。
- `zdesktop --log-frames` のとき部分の frame を `ZWL DAMAGE frame= image= x= y= width= height=` と書く。
- 変えた動き: `zwl_glass_motion` は pointer が静かな所（窓の本体の上）にあるとき `dirty` を立てない。`shm.c` の upload は `dirty` を立てない
  （その画像の commit が damage を付けている）。

## 実装（2026-09-27）

- 新 `userland/base/zdesktop/damage.c`（`zwl_damage_pointer`・`zwl_damage_commit`）、`compose.c`（`compose_region`、`compose_record` の
  region、出力を開くとき画像の記録を消す）、`compose.h`（履歴、`pass_load`、`scissor_now`）、`backdrop.c`（resume を `zwl_compose_load_pass`
  に、scissor を戻す）、`shell.c`（`zwl_glass_still`・`zwl_glass_body_damage`・`zwl_glass_pointer_calm`、`damage_near`、`DAMAGE_REACH`）、
  `menu-shell.c`（`zwl_menu_is_open`）、`display.c`（`adopt_commit` で `zwl_damage_commit`、描く条件に damaged）、`input.c`（移動で
  `zwl_damage_pointer`）、`shm.c`、`zwl.h`、`Makefile`。
- 試験: 新 `plan/ws035/tests/zdesktop-p055.sh`（glass と plain: wlshm --band の窓の本体の damage、本体の上の pointer の damage、帯が 1 本
  だけで cursor の残りが無い画素）。

## 検証（amd64 だけ、2026-09-27）

- guest（QEMU、Venus）: `zdesktop-p055.sh` PASS（glass: 本体の damage 29 回、cursor の damage 7 回、plain: 29 回・8 回、帯 1 本 20 行、
  cursor の跡なし、ERROR 無し）。途中で見つけて直したもの: shm の upload と `zwl_glass_motion` が毎回 `dirty` を立てていて部分の frame に
  ならなかった。
- 回帰（同じ image）: menu-regress（p053 p059 p062 p063 p064 p065 p068 p069 p070 p071 p072 p014 p076 p077 p078 p079 p080 p081 p057）・
  menu-p002・menu-p003・titlebar-p008・p009・p010 PASS、files-regress（p002〜p008・p012・p014・p009）PASS、p013・p015 は同じ時に p017 の
  配置へ書き換え中の試験で走って落ちた → この image に合う版（HEAD の試験）で流し直して PASS。boot test PASS
  （`build/ws035-p055-boot/login.png`）。
- 規約: `damage.c` の style-check 0、変えた file は悪化なし（image の後の変更は style だけ: `damage_near` の結果を変数に、`damage.c` の空行と
  comment）。
- 実機（i915）: 未実施（LOAD の pass で前の frame の画素が残ることを i915 の native の WSI で確かめていない）。
