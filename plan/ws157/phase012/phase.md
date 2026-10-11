<!-- awesome-plan project=zedbsd record=ws157-p012 -->

# ws157-p012: media 管理の経路の review と mediastorage の解析の jail 化

Parent: [WS157](../ws.md)
Status: in-progress（q937-i01、P1。review 済み・小さな直しを commit、設計の案を Q1 へ）
Phase disposition: normal
Queue: q937（[P1 の lane](../../agents/P1/queue.md)）

## 由来

- 2026-10-11 ユーザー:「Codexが実装した部分の設計が若干不安です。libkeiland --> コンポジタ --> mediastorage --> コンポジタに通知 --> libkeilandに通知 --> Phone/Photo appsに通知、この仕組みが本当に適切に実装されているか、P1に見てほしいです。」
- 2026-10-11 ユーザー:「mediastorageがメディアファイルをパースするのは権限的に避けたいです。既存のjailがありますよね、サムネイルで使っている。あれの仕組みを使ってほしいです。できればサムネイルのCLIもそのまま流用するのがよさそう。」（[ws.md](../ws.md) の末尾）
- T1-533 の所見（Q1 経由）: 初回の「Photos & Videos」で picker が開かない、Photos の動画が写真の placeholder、picker の「2 of 16 selected」。

## Review（source、main 9e5b93903 の時点）

経路: app → libkeiland `kl_system_media_*`（`libkeiland/system/system.c`、同期、private queue で最長 125 s 待つ）→ `kl_system_media_v1`（list/add/remove は request id、apply は FD）→ compositor `wayland/media-library.c`（16 の pending、tick ごとに backend を回す）→ `libkeiland-backend/media/media.c`（`posix_spawn` で `/bin/mediastorage <op>`、stdin/stdout の非同期 pipe、結果は無名の tmpfile、120 s で SIGKILL、Home の `waitpid(-1)` と子の所有を分け合う）→ mediastorage（`.lock` の flock、metadata.db を読んで処理、mkstemp+fsync+rename で保存、変更の後に自分で Wayland に繋いで `notify`）→ compositor の generation（変更の完了と notify で +1）→ watch している object に `changed`（generation ごとに 1 回、送れなければ次の tick）→ app が全体を list し直す。

設計どおりの所: OS 依存は backend に閉じ（compositor・libkeiland は DB と file に触らない、DB を書くのは CLI だけ）、compositor の main loop は CLI を待たない（pump は 64 KiB ずつ、10 ms ごと）、pipe の両向きを回すので詰まらない、FD は単一の所有者で dup して渡す、zombie は Home の reaper か backend が回収、client が落ちた後の結果は完了してから捨てる、DB の変更は flock で直列、旧 client は version で opcode を分ける、watch は登録の時に今の generation を 1 回受ける（取りこぼさない）。

| # | 問題（file:行） | 起こる状況 | 重さ | 推しの直し |
| --- | --- | --- | --- | --- |
| 1 | app の media API が同期で、UI の thread から呼ばれている: Photos の印の保存（`photos/main.c` の `ph_marks` → `kl_system_media_apply`）、Phone の受信 MMS の保存（`phone/receive.c:89`）、picker の open（`libkeiland/ui/picker.c` `picker_list`）、file chooser の media の list（`chooser.c:165`） | CLI は flock で直列なので、別の app の大きな import（動画の copy に数十秒）の後ろに並ぶと UI が最長 125 s 固まる | 高 | libkeiland に非同期の形（request を出して event で受ける `kl_system_media_*_begin`＋`KL_SYSTEM_CHANGED_MEDIA_DONE`、または picker・chooser は自分の thread と接続で list する）。picker は window を先に出して「Loading…」、list は picker の thread で（Photos の worker と同じく専用の接続）。**設計の変更（API 追加）なので案** |
| 2 | 1 回の変更で watcher が 2 回起きる: compositor の完了の +1 と、CLI 自身の `media_db_notify` の +1（`mediastorage/main.c` の notify、`media-library.c` の `media_answers`） | 各 app が library を 2 回 list し直す（CLI が 2 本余分に走る） | 中 | **直した**: compositor が起こす CLI には `--no-notify`（`backend/media/media.c`）。端末から人が起こす CLI は今までどおり notify する |
| 3 | list も `LOCK_EX`（`mediastorage/main.c`） | 長い import の間は list も並び、複数の app の list も互いに並ぶ | 中 | **直した**: list は `LOCK_SH`（metadata.db は rename で置き換わるので一貫した 1 版を読む。空の library の最初の list が書く時も mkstemp の別名＋同じ中身の rename で安全） |
| 4 | 120 s の SIGKILL（`media.c` `MEDIA_JOB_SECONDS`）が add にも同じく掛かる、copy は最終の名前へ直に `O_EXCL` で書く（`photos/import.c:398`） | 大きな動画（GB）の add-list が 120 s を超えると途中で殺され、半端な copy が Files/ に残り、DB には載らない（同じ file を入れ直すと別名の copy） | 中 | add は時間でなく進み（stdout の無い間）で見る、または size から上限（例 120 s＋1 s/50 MB、backend が path を stat）、copy は一時名から rename。**案**（下の jail 化と一緒に直す） |
| 5 | 変更の後の通知が全体の読み直し: Photos は `changed` で snapshot を全部読み、thumbnail をすべて捨てる（`photos/main.c` `ph_media_results` → `ph_worker_drop_thumbs`） | 大きな library（上限 50,000）で 1 枚の印の変更でも全件の TSV と見えている thumbnail の作り直し | 低〜中 | 今は可（TSV の parse は軽い）。thumbnail は id で残す（drop しない）のが先、差分の event は後 |
| 6 | picker の open ごとに `kl_system_open`（system の全 object を作る round trip）と CLI の list | open が遅い（QEMU で数百 ms〜） | 低 | 1 と一緒に（list だけの軽い接続） |
| 7 | 初回の picker が開かない（T1-533） | Phone は失敗の notice（3 秒）を出したと見られるが、PNG に写らず、原因の errno が log に無い | 中 | **直した（調べる手段）**: Phone の log に `REQUEST action=attach media=1 contact=..` と `FAILED operation=media-picker error=..`。T1 の再試験で errno を見て直す。source の読みでは決め手無し（card の click は届く、list は軽い） |
| 8 | Photos の動画が写真の placeholder | Photos の thumbnail は動画を decode できず FAILED の icon | 低 | **直した**: `ph_decode` は動画で ENOTSUP、view は `PH_THUMB_VIDEO`（映画の icon と再生の印、picker と同じ） |
| 9 | picker の「2 of 16 selected」 | 16 は件数でなく下書きの残りの上限 | 低 | **直した**: 「2 selected, up to 16」 |
| 10 | 失敗した変更（CLI が error）でも generation を上げる | 部分的な成功（add-list の一部失敗）は保存されるので上げるのが正しい。空振りの list し直しが 1 回 | — | 直さない（正しい側に倒れている） |
| 11 | Photos・Phone の thumbnail の decode は app の process の中（`photos/decode.c`、Phone の `ph_media_thumbnail`）。Files は keiland-preview の jail | 細工した画像の decode の穴が app の権限で動く | 中 | 下の jail 化の後で、Photos・Phone の thumbnail も keiland-preview（`preview_picture_begin`）へ。**案** |

## 設計の案: mediastorage の解析を keiland-preview の jail へ（Q1 → ユーザー）

今 mediastorage が自分で読む中身: 種類（先頭の magic、`kl_media_kind`）、寸法（`mediastorage/dimensions.c`: PNG・GIF の header、JPEG の segment を辿る）、撮影日時（`photos/exif.c`: JPEG の APP1 の TIFF/IFD を辿る）、SHA-256（bytes を読むだけ）。

| ID | 決め | 理由 |
| --- | --- | --- |
| J1 | **keiland-preview に `--info` を足す**（同じ binary、同じ起こし方: fd 0 = 元の file、fd 1 = 無名の一時 file、zedBSD は `sandbox_spawn`、Linux は seccomp、FreeBSD は今の preview と同じく process の中）。出力は PPM の代わりに text: `keiland-preview-info 1` の行、`kind=jpeg|png|gif|video|none`、`width=`、`height=`、`taken=YYYY:MM:DD HH:MM:SS`（EXIF の文字のまま、無ければ空） | サムネイルの CLI と jail をそのまま使う。時刻の変換（tz の file が要る）は jail の外で、文字を数に直すだけ（解析でない） |
| J2 | `--info` は入力の**先頭の 4 MiB だけ**読む（64 MiB の全体読みはしない）。寸法の解析は `dimensions.c` を preview へ移し（buffer 版）、EXIF は `photos/exif.c` を preview の source に足す（複写しない）。動画は magic で `kind=video`、寸法 0（今と同じ） | 大きな動画でも速い。EXIF・SOF は普通は先頭の数百 KiB |
| J3 | **起こすのは mediastorage**（preview の client の `preview_start`/`preview_wait` を mediastorage に link）。compositor・backend は今のまま | 端末から使う CLI も同じ安全さ。import の手順（日時で置き場所を決める）が CLI の中で閉じる |
| J4 | 失敗・時間切れ（`PREVIEW_LIMIT_MS` 5 s、killed）・読めない出力: その file は**取り込む**が、寸法 0、日時は取り込んだ日、種類は mediastorage の magic の比較（先頭の固定の bytes の比較だけ、解析でない）で決める。`kind=none` なら今と同じく拒否 | 壊れた画像でも元の file を失わない。種類の判定を jail に寄せすぎると時間切れで動画を落とす |
| J5 | **SHA-256 は mediastorage に残す**（copy のために全部読むので、その bytes で hash。中身の構造は見ない） | jail に寄せると 2 回読む（大きな動画で倍の IO）。hash は解析でない |
| J6 | 3 OS: zedBSD・Linux は jail。FreeBSD は Capsicum の対応まで preview と同じく process の中（今の Files と同じ制限、記録する） | preview の今の作り |
| J7 | 試験: preview の host 試験に `--info` の fixture（JPEG の EXIF・SOF、PNG、GIF、壊れた・切れた file、動画の先頭）、mediastorage の host 試験は preview を差し替えずに通す（host では preview を普通の子として起こす）、時間切れの試験 | |

あわせて #4（add の時間の上限、一時名からの rename）も同じ Phase で直す案。Photos・Phone の thumbnail の jail 化（#11）は別 Phase の案。

## 実装（直した所）

| file | 変更 |
| --- | --- |
| `userland/desktop/mediastorage/main.c` | `--no-notify` を受ける、list は `LOCK_SH` |
| `userland/desktop/libkeiland-backend/media/media.c` | compositor の起こす CLI に `--no-notify` |
| `userland/desktop/photos/decode.c`・`app.h`・`view.c` | 動画は ENOTSUP → `PH_THUMB_VIDEO`、映画の icon と再生の印 |
| `userland/desktop/libkeiland/ui/picker-view.c` | 「N selected, up to M」 |
| `userland/desktop/phone/main.c`・`view.c` | picker の open の失敗と attach の request を log |

## 確認

| コマンド | 結果 |
| --- | --- |
| `make BUILD=build/ws157-q937 ZEDBSD_CONFIG=config/current-uat.mk … bin/phone bin/photos bin/wayland bin/mediastorage bin/audio-loop` | rc 0、warning 0 |
| `make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws157-q937-linux all` | rc 0、warning 0 |
| `sh plan/ws157/tests/run-host-mediastorage.sh build/ws157-q937-host build/ws157-q937-linux` | 全 PASS（media wire の external CLI の notify・watch、backend、selection、receive） |

未実施: QEMU（T1: 初回の picker の再現と Phone の log、Photos の動画の icon、picker の見出し）。
