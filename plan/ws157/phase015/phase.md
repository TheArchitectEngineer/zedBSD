<!-- awesome-plan project=zedbsd record=ws157-p015 -->

# ws157-p015: Photos・Phone の thumbnail の decode を jail へ（#11）

Parent: [WS157](../ws.md)
Status: in-progress（P1。実装と host の試験・build まで。判断 L1（大きな写真の上限）を Q1 へ、T1 の確かめ待ち）
Phase disposition: normal
Queue: P1 の lane（p012 の #11、2026-10-11 ユーザー承認「全部承認です。」）

## 由来

[p012](../phase012/phase.md) の #11: Photos・Phone の写真の decode が app の process の中（`photos/decode.c`）で、細工した画像の decode の穴が app の権限で動く。Files と同じく keiland-preview の jail へ。

## 実装

| 所 | 変更 |
| --- | --- |
| `photos/decode.c` | **app の中の decoder（JPEG・PNG・GIF の decode と `ph_decode`・`ph_fit`・`ph_thumbnail`）を除いた**。新しい `ph_preview(path, side, cover, image)`: 先頭 16 byte の magic（`kl_media_kind`、解析でない）で動画なら ENOTSUP、他は `preview_picture`（zedBSD は `sandbox_spawn` の keiland-preview）で cover（中央の正方形）か contain（side 以下に縮める）の絵を作る。`ph_preview_adopt` は preview の絵を `kl_image` に渡す。`ph_turn` は残す |
| Photos | thumbnail の worker（`thumbs.c`）: whole は contain 2048、thumbnail は cover 160（cache の PPM はそのまま）。drag の絵（`main.c`）: contain `PH_DRAG_SIDE`。Makefile: `picture.c` と libpng・libjpeg・libgif の compat を外し、preview の client と zedBSD の spawn、依存に `desktop/preview` |
| Phone | media picker の thumbnail（picker の thread）: cover。会話の写真と下書きの preview（`view.c`、window の thread）: **待たない形に**（16 の slot、待ち・作り中・済み、同時に 2 つまで `preview_picture_begin`、main の loop で `ph_view_media_follow` が済んだ物を取り入れて描き直す、作り中の間は loop が 10 ms で見る、作り中は「Loading photo」）。slot を入れ替える時と view の解放で作り中を `preview_picture_cancel`。Makefile は Photos と同じ |
| 試験 | `plan/ws197/tests/host-media-receive.c`: 受けた写真を `ph_preview` で（Linux の preview の wrapper）。`run-host-mediastorage.sh` の host-media-wire の link を preview の client に。新しい `plan/ws157/tests/run-host-preview-sizes.sh` |

## 確かめ（2026-10-11 P1、host）

| コマンド | 結果 |
| --- | --- |
| `make BUILD=build/ws157-q937 ZEDBSD_CONFIG=config/current-uat.mk -j16 …/bin/photos …/bin/phone …/bin/keiland-preview …/bin/files …/bin/settings …/bin/imageview …/bin/notes …/bin/music` | rc 0、warning 0 |
| `make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws157-q937-linux -j16 all` | rc 0、warning 0 |
| `sh plan/ws157/tests/run-host-preview-sizes.sh build/ws157-q937-linux`（新） | all PASS: 12 MP の JPEG の cover 160 → 160×160、contain 2048 → 2048×1536、Phone の 480 → 480×360、EXIF の 90° → 1536×2048、小さい写真は大きくしない（120×90）、54 MP は拒否（status 3） |
| `sh plan/ws157/tests/run-host-mediastorage.sh build/ws157-q937-host build/ws157-q937-linux` | rc 0（受けた写真が `ph_preview` で 2×3 の絵になる、を含む） |
| `python3 plan/tools/style-check.py`（変えた C） | 新しい指摘 0（host-media-receive.c:127 は以前からの物） |

## 判断（Q1 経由でユーザーへ）

| ID | 問い | 事実と推し |
| --- | --- | --- |
| L1 | 大きな写真の上限 | 今までの Photos の中の decoder は 64 M pixel・辺 16384・96 MiB まで、keiland-preview（Files と同じ）は **16 M pixel・辺 8192・64 MiB**。jail へ移すと **16 MP を越える写真（スマホの 48・50 MP の高解像度の写真）は Photos・Phone で絵が出ない**（「作れなかった」の印）。libjpeg の縮小 decode（scale_denom）で直そうとしたが、tree の `libjpeg-compat` は縮小に対応しない（`decompress.c` は常に全体を decode、試して確かめ戻した）。案: (a) **keiland-preview の上限を Photos と同じに上げる**（jail の memory 1 GiB・CPU 10 秒の枠の中、Files の preview も同じ上限になる。Pi 4 では 50 MP の decode が 5 秒の時間切れになりうる）、(b) `libjpeg-compat` に縮小 decode（1/2・1/4・1/8）を足す別の Phase（thumbnail も 8〜64 倍速くなる、正しい直し、base の library の変更）、(c) 今のまま（16 MP まで）。**推し: (b) を別 Phase で、それまで (a)**（(a) は定数 3 つ） |

## L1 の決定と上限の引き上げ（2026-10-11）

ユーザーの決定（main の `plan/ws157/ws.md` の末尾）: libjpeg-compat の縮小 decode は別 Phase（[p017](../phase017/phase.md)）、それまで keiland-preview の上限を Photos の今の値に上げる。`userland/desktop/preview/preview.h`: 入力 64 → **96 MiB**、decode の辺 8192 → **16384**、画素 16 M → **64 M**（Files の preview も同じ上限）。

確かめ（host、Linux の build の preview を `prlimit --as=1073741824 --cpu=10`（zedBSD の jail と同じ 1 GiB・10 秒）の下で）: 8192×8192（64 M pixel、雑音で 49 MB の JPEG）を cover 160・contain 2048 → どちらも成功、**2.9 秒、最大の常駐 410 MB**（1 GiB の枠の半分以下）。9000×8000（72 M）は status 3 で拒否。`run-host-preview-sizes.sh`（54 MP は作る、72 MP は拒否に改めた）と `run-host-preview-info.sh` は all PASS、zedBSD の keiland-preview・files・settings・photos・phone と Linux の keiland の build は warning 0。

残る危険（記録）: Pi 4 では 64 MP の decode が client の待ちの 5 秒（`PREVIEW_LIMIT_MS`）か CPU の 10 秒を越えうる（この host の 2.9 秒の数倍の見込み、未測定）。越えた写真は「作れなかった」の印になる。p017 の縮小 decode で解く。

## 未実施

- T1（QEMU、current-uat）: Photos の一覧の thumbnail、写真を開いた絵、drag、Phone の会話の写真と下書き、picker の thumbnail が出ること、`ps` で `keiland-preview` が起きること。合否: 絵が出る、Photos・Phone の process が libjpeg 等を読み込まない（`ldd` 相当）。
- 実機。

## 残り

- L1 の決定。
- p012 の #5（変更のたびに thumbnail を全部捨てる）は別。
