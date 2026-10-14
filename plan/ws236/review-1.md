# WS236 設計（第 1 版 702719dce・44807c010）の敵対的 review-1

2026-10-14、Q1 が起動した design-reviewer。読むだけ、Linux の GPL の source は読んでいない。行番号は 702719dce の design.md。

## 重大

- **S1 status query が無いと libmedia は V3D の Vulkan Video を使わない。** libvulkan は video の family に常に `queryResultStatusSupport = VK_TRUE`（`libvulkan/external-properties.c:707-713`）、libmedia は `vkCreateQueryPool(RESULT_STATUS_ONLY)` の失敗で open を失敗にする（`vkvideo-runtime.c:1155-1157`・`1176-1186`）。V3D の executor に query pool の opcode が無い（`vulkan-dispatch.c:84-85` で ENOTSUP）。→ Video Player は黙って software。vkvideo-probe は pool 無しでも続くので試験で見えない。直し: V3D に RESULT_STATUS_ONLY の query pool（create/destroy/get results/cmd reset/begin/end）。§10 に「libmedia の log で Vulkan Video が選ばれた」を足す。「libvulkan 無変更」の表の行を直す。
- **S2 MMAL を待つ間に controller の mutex を持ち続ける。** V3D の pass は render window で mutex を手放す（`vulkan-native-job.c:321-336`、`render-worker.c:1636-1690`）。1 枚 10〜20 ms、timeout で最大 500 ms×3、全 process の ioctl と compositor が止まる。直し: 待つ間は mutex を離す、送る step と poll の step の分割を既定に、受信の thread は controller の mutex を取らない（lock の順は controller → mmal）。
- **S3 VPU の DMA と memory の寿命。** timeout・skip・壊れた session・破棄・process の終了の後に BULK_RX が完了して解放済みの page へ書く。staging も前の BULK_TX の完了前に上書き。出力の `offset + round4(length)` が image の大きさを超えると隣を壊す。直し: bulk は RX_DONE か abort まで参照を持つ（最悪 quarantine）、staging は TX_DONE の後に再利用、毎枚の大きさの検査（超えたら bounce）。
- **S4 MMAL の bulk の規則（userland の BSD の client との差）。** (a) 受信の bulk の長さは 4 の倍数に丸める（`mmal_vc_client.c:363-364`）。(b) EOS の長さ 0 の buffer にも 8 byte の bulk の受信（`365-368`）。(c) 256 byte を超える EVENT_TO_HOST は bulk（`241-256`）。(d) payload_in_message は OPAQUE と clock の port だけ（`mmal_vc_api.c:624-635`）、H.264 の input は常に bulk。(e) port の flush は `MMAL_WORKER_PORT_FLUSH` に 8 byte の dummy の bulk TX（`395-427`・`378`）。(f) 長さ 0 の EOS の input は `BUFFER_FROM_HOST_ZEROLEN` に 8 byte の bulk（`674-678`）。直し: §4 に表、host の試験。
- **S5 R1 の確かめが失敗を検出できない。** `MMAL_PARAMETER_VIDEO_TIMESTAMP_FIFO`・`MMAL_PARAMETER_VIDEO_INTERPOLATE_TIMESTAMPS`（`mmal_parameters_video.h:89`・`101`）の既定次第で pts が入力の順に付き、表示順の遅れでも単調に増える。直し: 2 つを明示して false、順の判定は絵の checksum を ws083 の参照の decode の順と照合、§12 の (a) を対照（(a) で並べ替えが見えなければ判定不能）。
- **S6 R1 の前提と代わりの道。** (i) Annex C の出力の順は bumping で、max_num_reorder_frames は使わない（reviewer の規格の読み、要確認）。外れる確率は書いてあるより高い。(ii) R1-a は止まる: libmedia は decode k+1 の submit が transfer k の semaphore を待つ鎖（`vkvideo-runtime.c:670-672`・`685-699`）、V3D の queue は 1 本の FIFO、出力 k に入力 k+1 が要ると deadlock。EOS の引き金が無い。直し: ベータ2 の代わりは「VIDEO_CAPABILITIES で profile 66（Baseline）だけを出す」（Main・High は libmedia が software へ）。R1-a は順の外の retire か decode 専用の queue を含めた再設計としてベータ3。

## 中

- M1 SEPARATE_REFERENCE_IMAGES を立てる（i915 と同じ、`i915/render/video.c:166`・`808`）。
- M2 video を有効にする時期の矛盾。capset が 176 で QUEUE_FAMILY_VIDEO_PROPERTIES が失敗すると physical device 全体が失敗（`instance.c:1412`）、compositor が GPU を失う。Vulkan の session ごとに capset の時点で latch。
- M3 V3D の変更の範囲が足りない（p004 の 12 LW は楽観）: NV12 と video の usage の image（`vulkan-resource.c:22-23`・`260-264`）、buffer の `VIDEO_DECODE_SRC`（`203`）、`vkGetImageSubresourceLayout`（`540-547`）、CPU の copy の format と aspect（`vulkan-buffer-copy.c:382`・`500`）、tile の copy（`vulkan-native-job.c:196-212`）、format の表、barrier の video の layout と plane、後始末、S1。
- M4 1 AU が input の buffer_size を超える場合（buffer の大きさ・数を設定するか分割し最後だけ FRAME_END）。
- M5 gpu_mem（`platform/arm64/config.txt` に無い、既定 76 MB で 1080p に足りるか未確認）。直すと #if の外の共有の file が変わる → 判断の項目に。
- M6 uncached の CPU の読みの費用（memory type は NC 1 つ、`vulkan-query.c:137-138`）。3 MB の NC の copy の時間を測る。
- M7 VCHIQ の制御 message（PAUSE・RESUME・REMOTE_USE・REMOTE_RELEASE・REMOTE_USE_ACTIVE、`vchiq_core.c:2037-2082`）、CONNECT を自分から送る（`3156-3163`）、bulk の actual -1、service ごとの bulk の上限 4（`vchiq_cfg.h:59`）、message と bulk の組の直列化。
- M8 RESET の後の output の buffer の渡し直し、壊れた状態の解除、component は session（か parameters）の作成で作り失敗をその場で返す。
- M9 試験の穴: on の時の既存の desktop、probe は libmedia の CopyImageToBuffer と status query の道を通らない、S4 の host 試験。

## 軽微

m1 maxLevelIdc は StdVideo の enum で返す（`h264.c:869`、i915 `video.c:132-133`）。m2 VUI は全体を合成、bitstream_restriction の残りは制限しない値。m3 32 bit の layout は ARM の EABI で測る。m4 送り手の recycle 待ちは `reserve_wait` で（設計に書く）。m5 ws.md の「前提」の古い記述、Phase の表、p001・J の状態、J の一覧に他の WS の source（libmedia＝WS202、probe＝WS083、libvulkan）。m6 試験の stream が plan/ws083/tests/streams に依存。m7 off の時の関数は全て #if の中。m8 staging の 4 MiB には `max_segment_size` ≥ 4 MiB（`dma.c:311`）。m9 maxLevel 4.2 では level 5.x の 1080p は software。

## 合っていた点

doorbell（BELL0 bit 2、+0x08 の BELL2）、long bulk の語、mailbox channel 3、version 8・最小 3、slot 128・片側 64、fragment 128 byte × 64、MMAL の service version 16・最小 10、V3D の memory（BLOB・連続・uncached・1 GiB 未満）、libvulkan の HOST_VISIBLE の export、同じ queue の 2 回の submit と binary semaphore。

## J1〜J5

J1 賛成（順の外の retire・別 queue は将来の課題）。J2 条件付き賛成（全て #if・ifeq の中、他の WS の source と gpu_mem を Q1 の承認の対象に）。J3 賛成。J4 賛成（probe に CopyImageToBuffer で読む形も）。J5 条件付き（規格に従わない stream を作る hack、C.4.5 の根拠は外し R1 の実機の確かめを必須に）。

## 期限までの最短の道（提案）

1. p002 と最小の p003（同期、EOS なし、NV12、bounce に受ける）→ §12 の probe を T1 で 1 回（S5 の中身の順と (a) の対照、1080p と gpu_mem、long bulk、3 MB の NC の copy の時間、channel 3 の初期化）。
2. R1 が合えば p004 を最小に（S1、M3、S2、S3、M2）。
3. T1 の 2 回目（probe の hash、libmedia の log、Player の PNG と CPU）。
4. R1 が外れたら Baseline だけを出し、既定 off のまま merge。R1-a はベータ3。
