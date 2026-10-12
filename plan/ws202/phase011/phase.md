<!-- awesome-plan project=zedbsd record=ws202-p011 -->

# ws202-p011: Video Player、media-probe の音、利用者の文書

Status: uncleared（software実装あり、全条件の確認は未完）
Disposition: normal
Parent: [WS202](../ws.md)
見積もり: 3 LW
依存: p007、p010

## 目的

Video Player を自前の decoder に合わせ、p012 に要る音の道具と scenario を揃える（design §9.3・§10.3）。

## 成果

1. `userland/desktop/videoplayer/`:
   - `main.c` の `vp_notice`: DEVICE・PROFILE・BUSY の文、MISSING の文の変更（design §9.3）。vkvideo は open で session まで作る（D28）ので、BUSY・PROFILE（大きさ）は
     open の問題として来る（send の誤りから notice を作る道は要らない）。
   - 縦横比: `media_frame_aspect` を描画の fitted の計算に。
   - log（L-04）: `VIDEOPLAYER OPEN … video=<codec>/<backend> audio=<codec>/<backend>`。`VIDEOPLAYER FRAMES shown=N time_ms=T` に `late=M` を**足す**（`time_ms` を残す）。
     1 枚目・100 枚ごとに加えて、一時停止・終わり・close の時にも 1 行。`vp_media_take` が捨てた数を数える。
   - seek（D14 改）: `media.c` の flush の後に `media_decoder_trim(sound, skip_before)` を呼び、0 なら音の frame の単位の捨てをしない（ENOTSUP なら今のまま）。音の track は
     video の sync の dts まで戻るので pre-roll（D30）の分は普通足りる。目標が sync から 1 frame 以内の時の最初の音の遅れ（最大 1 frame）は制限。
   - comment の「libavcodec が要る」の記述を直す。
2. `userland/tests/media-probe/` に音の部分（M-10 の決定、design §10.3）: `--audio-rms`（普通の `media_decoder_sound` で 16 bit・48 kHz・stereo、1024 frame ごとの左右の RMS）。
   `plan/ws202/tests/host-media-rms.c`（libmedia の source を host で compile し同じ計算）で AAC の全 stream の `X.rms` を作り `streams/` に置く。比べ: RMS < 1e-4 の frame は
   絶対差 ≦ 1e-6、他は相対差 ≦ 1e-3。試験のための口を `media_decoder_*` の名で足さない。
3. scenario（`tests/`、Q1 へ差分か許可）:
   - `tests/scenarios/apps/videoplayer/play.md`: `areas`・`paths`・目的を今の形（AAC は libmedia、MPEG-4 Part 2 は libavcodec）に。
   - 新 `h264-native.md`（5330、libavcodec 無しの image、`h264-high-b-aac.mp4` で `video=h264/vulkan-video audio=aac/libmedia`、再生・seek・終わり、`late=`）。
   - 新 `no-video-decode.md`（QEMU、libavcodec 無し、`problem=4` と notice の撮影）。
   - stream の置き場所は `/usr/share/zedbsd-tests/ws202/`（`test-image.sh` の `--file` と `--mode …=0644`、design §10.4）。
4. 利用者の文書: `docs/reference/media-playback.md`（新、英語）: 自前で再生できる形、Vulkan Video の要る機械、AAC-LC と HE-AAC の扱い、libavcodec が入っている時の扱い、
   notice の意味、制限（`iTunSMPB` を読まない、8 個の video の context、他の Vulkan の実装の de-tile）。docs/ から plan/ へ link しない。
   `docs/reference/vulkan-video.md` の「Example program」に libmedia が利用者であることを 1 文（WS083 の文書なので差分を Q1 に送り確認を受ける）。

## 確認

| コマンド | 期待 |
| --- | --- |
| design §10.6 の build（`ZEDBSD_CONFIG=plan/ws202/tests/config-media.mk`、media-probe を含む） | exit 0、`grep -c 'warning:'` が 0 |
| `sh plan/ws202/tests/run-host-media-rms.sh`（`host-media-rms.c` で `.rms` を作る） | 2 回作って同じ |
| image の package の一覧（`config-media.mk`） | libmedia・videoplayer・music・openssh・audiod・media-probe があり、libavcodec が無い（H-07） |

QEMU・実機は p012。


## 構造改訂と部分結果（2026-10-10）

Video Playerはapp共通adapterでtrackごとにnativeを試し、非対応ならapp所有のdlopen fallback。libmedia backend表にavcodecを入れない。native/fallback双方のframe/scaler寿命とno-FFmpeg noticeを確認する。 [変更理由・依存・結果](../policy-20261010.md)。旧記録は保持し、対象外の未実施条件をclearedとしない。共有投影/他担当/GitHubはQ1へpending。

## Native再生software結果（2026-10-10）

Event: `ws202-native-playback-software-20261010-p011`。Queue: [codex-ws202-playback](../policy-20261010.md#自走の実行承認-codex-ws202-playback)。

Video PlayerのSAR/trim/codec-backend OPEN/FRAMES-time-late/end-audio-drain/failure、native audio RMS probe、英語文書を実装。buildと独立RMSはPASS。AAT共有変更はproposal、window/音/同期はT1・ユーザー待ち。

[最終source/command/結果・限界](../playback-result-20261010.md)、[Q1統合](../handoff-20261010.md)、[T1の準備済み依頼](../t1-playback-request-20261010.md)。旧第2版の手順・昔のpartial outcomeを保存し、最新記録が未実装記述の現在状態を置換する。whole criteriaを満たしたとは扱わず、Q1の意味の統合と未実施matrix/実機結果が再開条件。main/共有投影/GitHubの更新はQ1 pending。


## main統合の追記（2026-10-10）

Event: `ws202-main-integration-20261010-p011`。ユーザー「mainへの統合はあなたがやってOKです。」によりsourceと記録をmainへ統合。最新の承認済み方針・手順・確認・残件は[統合記録](../main-integration-20261010.md)と[policy](../policy-20261010.md)。上の設計時点の推奨、旧未実装/統合pendingは履歴として保存する。software出力の有無とwhole clearanceを区別する。標準readbackの依存はp017、ref-listは既存p016。p012/T1→p013/User UAT→whole p014の確認は未実施、Master/共有Board/GitHubへの投影はQ1に保持。

共有AAT helper/scenarioと旧host-codec/layoutの移管追従は今回mainへ適用。実行結果は統合記録に記載し、旧proposal/pendingを現在の状態として扱わない。

## 2026-10-11 q925（P1）: T1-529 の FAIL の直し

- (a) libavcodec 無しの QEMU で Video Player が DEVICE の notice でなく「The file could not be opened (error 21)」: `MEDIA_PROBLEM_*` が 1〜6 で、同じ口が errno も返すため zedBSD の ENOMEM（4）＝DEVICE、ENOENT（6）＝BUSY と衝突していた。videoplayer の `media_reader_open` が ENOMEM（＝DEVICE）を 0 にして notice の分類を失った（Music・engine にも同じ形）。`userland/desktop/libmedia/media-decoder.h` の値を 1001〜1006 にした（名前で比べる所は不変、数値に依る試験・AAT は無いことを grep で確認）。
- (b) libavcodec 有りの AAT `apps.videoplayer.play` の「frames did not advance: [1], None」: 22f1d6480（WS202）で一時停止の log が `PAUSE shown=` から `FRAMES shown=` に変わり、AAT が `VIDEOPLAYER PAUSE shown=` を 5 秒待って得られなかった（一時停止は効いていた: T1 の app log に `ACTION action=4` と停止）。`PAUSE shown=%u time_ms=%lld late=%u` に戻した（late は残す）。QEMU で late が多い（153）のは合否の外。
- 確認: `run-host-app-fallback.sh`・`run-host-native-playback.sh` PASS（ASan/UBSan）。named build（current-uat.mk の videoplayer・music・libmedia.so、plan/ws202/tests/config-media.mk の media-probe・videoplayer）warning 0。style 新規 0。
- 再試験（T1）: T1-529 の (a)(b) と同じ。

## 2026-10-11 T1-529 の 5330 の動画の FAIL（admission problem=1004）の直し（q925 の続き、P1）

- 切り分け（5330 で直に、kei、T1 の file は触らず `/tmp/p1` に置いて `LD_LIBRARY_PATH=/tmp/p1/lib`）: media-probe に libmedia の log を stderr へ出す口（`media_set_log`）を足し、`vkvideo-runtime.c` の黙った拒否（library 無し・entry point 無し・queue family・拡張・profile・synchronization2・vkCreateDevice）にも log を足した → `Vulkan Video: no vkGetPhysicalDeviceVideoCapabilitiesKHR`・`no vkGetPhysicalDeviceVideoFormatPropertiesKHR`。
- 原因: **libvulkan の `vkGetInstanceProcAddr`** が、device の拡張（VK_KHR_video_queue）の physical-device の command（`dispatch-table.inc`・`api-commands.tsv` で scope が INSTANCE、拡張の bit が `VULKAN_DEVICE_VIDEO_QUEUE`）を **instance の拡張として**調べ（`vulkan_instance_extension`）、有効でないので NULL を返していた。
- 直し: `internal.h` に scope `VULKAN_ENTRY_PHYSICAL`（device の拡張の physical-device の command、どれかの physical device が拡張を持てば見える）、その 2 つの command を PHYSICAL に（`dispatch-table.inc`・`api-commands.tsv`）、`dispatch.c` は PHYSICAL を device の拡張の調べに回す（`vkGetDeviceProcAddr` は今までどおり DEVICE だけ）。
- 確かめ（5330 の実機、image は beta2 b67a6d7 のまま、`/tmp/p1/lib` の libvulkan・libmedia と media-probe）: T1-529 の 6 command 全部 **rc 0**（high-b: --time 50 frames・--twice 50×2・--seek=1 25、nocts 50、uat 300、uat --seek=2.5 240、--expect の hash 一致）。log: queue family 1 が decode（flags 0x20、H.264）、extent 640x368、level 7/14、slots 5/17。zedBSD の build（libvulkan・libmedia・media-probe・videoplayer）rc 0、warning 0。style-check の新しい指摘 0。
- 未実施: Video Player の画面での再生（T1）、QEMU の Venus での回帰（T1、PHYSICAL の scope は Venus の libvulkan にも同じ）。
