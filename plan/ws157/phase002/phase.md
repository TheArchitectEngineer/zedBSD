<!-- awesome-plan project=zedbsd record=ws157-p002 -->

# ws157-p002: 写真の library（`~/Pictures`・EXIF の日付・album・お気に入りと回転の保存）

Status: uncleared（2026-10-07 q835: ユーザーの要件（~/Pictures/Library・取り込み・db）で置き換え。code は p004・p005 で作り直した）
Disposition: canceled（2026-10-07 ユーザーの要件で最初の既定案を置き換え。置き換え先 [p004](../phase004/phase.md)・[p005](../phase005/phase.md)）
Parent: [WS157](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p001](../phase001/phase.md)（既定案）

## 範囲（正常系）

`userland/desktop/photos/`（新規）の `photos.h`・`library.c`・`exif.c`・`store.c`。

- `exif.c`: JPEG の APP1 の EXIF（TIFF の IFD0 → Exif IFD）から DateTimeOriginal（0x9003）、無ければ IFD0 の DateTime（0x0132）を読む。
- `library.c`: folder を深さ 4 まで見て、先頭の bytes が JPEG・PNG・GIF の file を集める。日付は EXIF、無ければ mtime（地方時）。
  album は 1 段目の folder（名前の順）。写真は日付の新しい順、同じなら path の順。Timeline・Favorites・album の一覧。
- `store.c`: お気に入りと回転（右回りの 1/4 回転の数）を `$XDG_CONFIG_HOME/keiland/photos.conf` に 1 行ずつ（`favorite <path>`・`turn <n> <path>`）、一時 file と rename で書く。

## 確かめ（2026-10-07、P2）

- host: `sh plan/ws157/tests/run-host-photos-library.sh` → PASS（34 の check、ASan・UBSan、TZ=UTC）。`make-photos.py` が作る folder:
  暦の往復（1970・閏日・1970 年より前）、EXIF の DateTimeOriginal（little・big endian、APP0 無し）、IFD0 の DateTime だけ、0 の日付は file の時刻、
  PNG・GIF は file の時刻、text・偽の JPEG・隠し folder・深さ 4 より下は入れない、新しい順、album の名前の順と枚数、写真の album、
  Timeline・album・Favorites の一覧と capacity、外の file を日付の位置に足す・2 度目は同じ・text は EINVAL、
  `photos.conf` の読み（範囲外の turn を捨てる）・書き（rename）・読み直し・library に無い path の印を残す・file が無い時。
- style-check 0（photos.h・exif.c・library.c・store.c、試験の C）。

## 積み残し

[WS177 backlog-p2](../../ws177/backlog-p2.md) の WS157 の行。
