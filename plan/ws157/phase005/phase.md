<!-- awesome-plan project=zedbsd record=ws157-p005 -->

# ws157-p005: Photos の app（取り込み・album・縮小画像の cache）

Status: in-progress（2026-10-07 q835 P2: 実装と host の試験 PASS、zedBSD の build warning 0。QEMU は T1 に依頼、判定は Q1）
Disposition: normal
Parent: [WS157](../ws.md)
Queue: q835（2026-10-07、P2）
依存: [p004](../phase004/phase.md)

## 実装（2026-10-07 P2）

- `main.c`: 起動で `~/Pictures/Library` の db を読む。`photos --import=PATH`（窓の前に取り込み、db を書く。AAT はこれを使う）。File > Import Photo...（Ctrl+I）・
  Import Folder...（libkeiland の file chooser で写真を選ぶ。Folder はその写真の folder）→ 取り込み・db・view を作り直し「Imported N photos (M already in the library).」。
  Refresh は db を読み直す。印・album の変更は `ph_db_save`。`PHOTOS IMPORT done …`・`LIBRARY root=…`・`CHOOSER …`・`SAVE` の log。
- `view.c`: grid の見出しに Import（folder の取り込み）。Photo > Add to Album...（と A）で album の card（album の一覧の button、新しい album の名前の欄と New Album・Enter、
  Cancel・Esc）。album を作ると一覧の album の位置を保つ。空の library の文。
- `thumbs.c`: 縮小画像を `$XDG_CACHE_HOME/keiland/photos/<id>.ppm`（無ければ `~/.cache/...`。P6、160 の正方形、回転は掛けずに）に持ち、次からはそこから読む。
- AAT: `plan/tools/aat/scenarios/helpers_photos.py` と `tests/scenarios/apps/photos/browse.md` を取り込みの流れに書き直し（`--import`、db の行、album の file、2 度目は重複 9、cache 8）。

## 確認（2026-10-07）

- host: `sh plan/ws157/tests/run-host-photos.sh` → PASS 32（取り込んだ library と album 2 つで、p003 の grid・全面・回転・お気に入り・slideshow の確認に加え、
  album の card（New Album で Waves を作り写真を足す、save の印、card が閉じる）、Import の依頼、新しい view が縮小画像を cache から読む（8 枚））。PNG: `build/review/ws157/host-photos-*.png`（card を含む）。
- `sh plan/ws157/tests/run-host-photos-db.sh` → PASS。
- zedBSD: `make ZEDBSD_CONFIG=plan/ws157/tests/config-amd64-photos.mk BUILD=build/ws157-zed build/ws157-zed/bin/photos` warning 0。style-check 0。check-scenarios PASS。
- QEMU: 未実施（T1 に依頼: `--only 'apps\.photos\.'`）。file chooser からの取り込みは AAT では流さない（`--import` で代わり）。

## 積み残し

[WS177 backlog-p2](../../ws177/backlog-p2.md) の WS157 の行（2026-10-07 の作り直しで更新）。
