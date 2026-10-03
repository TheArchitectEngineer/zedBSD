<!-- awesome-plan project=zedbsd record=ws128-p005 -->

# ws128-p005: Image Viewer の改善

Status: in-progress（q666、P2、2026-10-04。3 項目の実装と host 試験は済み、QEMU は T1 に依頼して結果待ち。画像の copy は F-074 へ移管）
Disposition: normal
Parent: [WS128](../ws.md)
Queue: q666（Q1 の dispatch、2026-10-04。user「任せます」→ Q1 の採否）
依存: p001（2026-10-04 Q1 が委任で採用）
目安: 2h（1 Queue）。実行者の目安: phase-runner-mid
所有 path: `userland/desktop/imageview/`（`image.c` は WS094 p014 と直列、`picture/` を変えるなら WS127 p004 と直列）

## 範囲

候補: File > Move to Trash（Files の Trash の規則、次の画像へ進む）、Open With（WS093 の対応）、slideshow（全画面で一定の間隔）、Edit > Copy（画像を clipboard へ、image/png）。p001 で選んだ物だけ。

## 受け入れ

選んだ項目ごとの guest の手順と host の試験 PASS、既存の `run-host.sh`・`imageview-guest.sh` PASS、boot test PASS。

## 検証の方法と範囲

host と QEMU の Venus。 やっていない確認は「未実施」と書く。

## 未決の判断

なし。2026-10-04 Q1 の判断: 「画像の copy」は libkeiui に API を足さない（WS131 の間は WS090 を動かさない決め D9。libkeiui は WS131 で
libkeiland に吸収されるので、その時に `kl_` の clipboard の API として入れる）→ Future Work の F-074 に移管。この Phase は残りの 3 つ。

## 実装（2026-10-04、q666）

- **Move to Trash**（File の menu・context menu・Delete キー）: `imageview/share.c` が Files の `files/trash.c`（ws127-p003 の volume の trash を
  含む）を source で共有して、記録を書いてから rename（file system が違えば copy して消す、失敗なら記録と copy を消して元のまま）。`view.c` は
  `want_trash` を立てるだけで、`main.c` の `main_share()` が移し、`iv_app_removed()` が folder を読み直して同じ位置の画像（最後なら一つ前、
  無くなれば空）を出す。log `TRASH path= trashed= errno=`、`REMOVED left= index=`。trash.c は `fm_paths_free`（clip.c）を使わない形にした。
- **Open With**（File > Open With の submenu、8 枠）: `share.c` が Files の `files/apps.c` を source で共有し、Files と同じ open-with の表
  （利用者・system・組み込み）と既定（`files.open-with.<type>`、kl_settings）で、Quick Look を除いた application を出す。`menu.c` の
  `iv_menu_openers()` が枠の label と表示を画像ごとに変え、選ぶと `IV_ACTION_OPEN_WITH_FIRST + n` → `main_share()` が `fm_apps_launch`。
  log `MENU openers count= first=`、`OPEN-WITH index= path= errno=`、Files の `LAUNCH`。`share.c` は Files の `fm_log` を viewer の log に出す。
- **slideshow**（View > Slideshow、F5）: 全画面にして `IV_SLIDESHOW_MS`（3 秒）ごとに次の画像、最後の次は最初。Esc・F5・menu で止まり、
  slideshow が全画面にした時は全画面も戻す。log `SLIDESHOW start|next|stop`。
- Makefile（zedBSD・Linux・FreeBSD）に `share.c`・`files/trash.c`・`files/apps.c` と mount の表の読み手（mntent／FreeBSD）を足した。
- 試験: `plan/tools/imageview/host-imageview.c` の view に slideshow（待ち・次・一周・Esc）、Delete と Open With の要求、
  `iv_app_removed`（途中・最後）。新しい [host-share](../tests/host-share.sh)（trash・同名の .2・無い file・利用者の open-with の行・起動・範囲外）。
  guest の手順 [imageview-p005.sh](../tests/imageview-p005.sh) と image の config [config-amd64-imageview.mk](../tests/config-amd64-imageview.mk)。

## 確認（host。QEMU は T1 待ち、実機は未実施）

- `sh plan/tools/imageview/run-host.sh` → `host-imageview: PASS`。`sh plan/ws128/tests/host-share.sh` → `host-share: PASS`。
  Files の `host-model.sh` PASS（trash.c を変えたため）。
- zedBSD amd64 の build（`BUILD=build/p2-files`、imageview と files）rc=0、warning 0。Linux・FreeBSD の build は未実施（host の gcc で share.c・
  trash.c・apps.c・mounts-mntent.c を `-Werror` で compile できることは host-share で確かめた）。
- `plan/tools/style-check.py` 0（imageview の全 .c と files/trash.c）、`plan/tools/imageview/style-extra.py` は変えた file で増えていない。
- 未実施（T1 へ）: guest の `imageview-p005.sh`（Open With は menu の枠の数を log で確かめるだけで、menu から選んで起動するのは host-share で
  確かめた）、既存の `imageview-guest.sh`（旧い BIN の copy の形の試験）、boot test。

## Event

2026-10-02 / ws128-beta1-plan: fg019 の計画で新設。
