<!-- awesome-plan project=zedbsd record=ws102-p021 -->

# ws102-p021: 縁に組み込んだ見た目

Status: cleared（2026-09-30、QEMU の Venus。実機は未実施）
Disposition: normal
Parent: [WS102](../ws.md)
Queue: main の依頼（2026-09-30、順番の変更: p008 の後、p020 の前。worktree `.claude/worktrees/ws102-keyboard`、branch `wt/ws102`、`git merge main -m WIP` の後）

## 範囲と受け入れ

design §2.3 の改め（2026-09-30 ユーザー「ウィンドウのエッジにぴったりと組み込み、フローティングウィンドウにしない見た目」）を反映する。

- flick の panel は右の列の全体に置く。system bar の下から画面の下まで、右の余白は 0。
- QWERTY と手書きの panel は、下端の全幅に置く（余白 0）。
- 外の角丸と影をやめ、内側は 1 px の区切りの線だけにする。
- 開閉は、縁から帯が伸び出す動きにする。

受け入れ: 1280x800 と 1920x1080 で、panel の外の辺が画面の縁と 0 px で接する（log の矩形と画面）。

## 実装（keyboard.c だけ）

- 配置（`keyboard_place`）:
  - flick は `x = width − w`・`y = ZWL_GLASS_BAR`・`h = height − ZWL_GLASS_BAR`。w は key の幅のまま 4 列（1280x800 で 318）。
  - QWERTY は `x = 0`・`y = height − h`・`w = width`。
- flick の key は列の下に詰める。帯と key の間は、§2.10 の道具の面の場所（p016）として空けてある。
- 描画:
  - 影をやめた。glass は角丸 0。
  - window に向いた辺（flick は左、QWERTY は上）に 1 px の線を引く。色は system bar の区切りの線と同じ濃さ（0.18）。
  - key の角丸と、押した key の accent は今のまま。
- 開閉の動き:
  - 開くと `KEYBOARD_SLIDE_MS`（200 ms、§2.8 の時間）の ease-out で縁から伸び出し、閉じると縁へ戻る（flick は右へ、QWERTY は下へ）。
  - 閉じる時は、論理はすぐ閉じる（`close` の log、入力は取らない）。描画だけを戻り切るまで続ける（`leaving`）。
- 角の gesture: panel が角まで届くようになったので、下の角の press を panel より先に角の gesture に渡す。開いた panel の上でも、同じ角の swipe で閉じ、もう一方の角の swipe で替えられる。
- 手書きの面は panel の中の相対の配置なので、そのまま下端の全幅に並んだ（書く面 962×256 at 6,538）。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make … bin/wayland …` | rc 0、warning 0 |
| style | `plan/tools/style-check.py keyboard.c` | 0 件 |
| guest（pen image の複写、1280x800）。全ての手順の座標を縁の配置に直した | `osk-guest.sh build/ws102-shots/p021-final install start pointer flick edges touch send close qwerty hand` | PASS |
| guest（1920x1080） | `… install large` | PASS |
| 回帰 | WS079-p010、WS099 の C9（10 本）、boot test | PASS |

log の矩形（受け入れ）。外の辺が画面の縁と 0 px で接している。

| 画面 | flick | QWERTY |
| --- | --- | --- |
| 1280x800 | `x=962 y=34 width=318 height=766`（右端 1280、下端 800、上端は system bar の下端 34） | `x=0 y=496 width=1280 height=304`（左 0・右 1280・下 800） |
| 1920x1080 | `x=1506 y=34 width=414 height=1046` | `x=0 y=670 width=1920 height=410` |

画面: `build/ws102-shots/p021-final/` の `flick-open.png`・`petals.png`（右の列）、`qwerty-open.png`・`hand.png`（下端）。1920x1080 は `build/ws102-shots/p021-large/`。

## 見つけたこと・制限

- 伸び出す動きの見た目は、画面で確かめていない。この guest は frame が 7 枚/秒前後で、200 ms の動きは 1〜2 frame になる。動きの frame の間隔の数値（≤ 20 ms）は、L3 の p010 で 60 Hz の出る環境で測る。
- flick の列の上の空き（帯と key の間）は、§2.10 の道具の面（p016）が入るまで何も無い。
- 作業の領域（p007）は、panel の内側の辺までを差し引く前提で作る（design §2.8）。
- 実機・Windows の上の QEMU は未実施。

## Resume point

2026-09-30: cleared。次は p020（QWERTY の補助の key の列）。
