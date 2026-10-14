<!-- awesome-plan project=zedbsd record=ws236-p006 -->

# ws236-p006: 実機の確かめと直し

Parent: [WS236](../ws.md)
Status: in-progress（P3、2026-10-15。frame 0・1〜15 の値・速さ・BUG-314 の直しを実装し、host 試験と build（warning 0）まで済み。test-wait（T の実機、下の依頼）。agent/p3-ws236）
Phase disposition: normal

## 範囲

- p003 の probe（R1）の判定: 合えば Main・High のまま、外れれば `src/drivers/gpu/bcm2711/vulkan-video.c` の `VIDEO_BASELINE_ONLY` を 1 にしてベータ2 は Baseline だけ（design §5）。→ R1 は合った（T1-626・T1-629 の run b、T3 の vkvideo で frame 1 以降が decode の順で出て、画も正しい）。Main・High のまま。
- p005 の T1 の依頼の結果（vkvideo-probe の hash、Video Player の `backend=vulkan-video`・画・CPU、seek）で見つかった不具合の直し。
- 2026-10-15 Q1 の依頼（P3）: (1) frame 0 が出ない理由と 1〜15 の値が 1 小さい理由を見つけて直す。(2) vulkan-video の道の時間の行き先を見つけ、stream の速さで再生できるようにする。(3) BUG-314（V3D の submit の EIO に理由の log が無い）に、EIO を返す submit の道ごとに理由を言う rate limit 付きの log を足す（動作は変えない）。
- 長時間（耐久）は別枠でユーザーに確かめてから。

## 見つけたこと（T3 の結果 `plan/agents/T3/results-20261015-t1-637-ws236.md` と dump の解析、2026-10-15）

### frame 0

- dmesg の流れ（T3 の ws236-b.txt）: decode 1 の input が返った後、押し出し（sets を CONFIG で再送、delimiter）の buffer も返るが frame は来ない。decode 2 の input（次の picture の slice）が来て初めて pts 1 と pts 2 の frame が続けて出る。以後は各 picture が自分の FRAME_END で出る。
- つまり firmware は start（作成・flush）の後の最初の picture だけを、次の picture の slice が来るまで持つ。FRAME_END・CONFIG の sets・AUD は効かず、空の FRAME_END は Pi が戻らなかった（T1-633）、EOS は参照を捨てる（T1-631）。firmware の内部の理由は不明。
- 直し: start の後の最初の unit の直後に、本物の「次の picture」を自分で送る（push）。中身は自前の PPS（id は parameters が使っていない最大の id、CAVLC、deblocking の制御あり）と、非参照（nal_ref_idc 0）の I slice 1 枚（全 macroblock が I_16x16 の DC・係数なしで灰色、deblocking off）。frame_num は最初の picture の次、POC は最初の picture の POC＋1（type 0 の lsb）。非参照なので参照も POC の導出も変えず、stream はそのまま decode を続けられる。push の frame は pts 0 で来て捨てる。最初の picture が非参照なら push しない（従来どおり 500 ms 待って skip）。
- host の確かめ（ffmpeg）: 3 つの試験の stream と 1080p の stream で、最初の AU の後に push を挟むと、元の全 frame が bit 単位で同じまま、位置 1 に灰色の frame が 1 枚増える（`run-h264-test.sh` の `h264-push: all PASS`）。firmware での確かめは実機（下）。

### 1〜15 の値が 1 小さい

- T3 の dump（3 stream、frame 1 以降の全 sample）を参照の値ごとに数えた: 参照の値が 2〜15 の sample は全て出力が 1 小さく、0 と 16 以上は全て一致（Y・Cb・Cr とも例外なし）。値 1 の sample は 3 stream に無い。位置にも予測にもよらず値だけで決まるので、decoder の内部（参照の絵）は正しく、firmware が NV12 で出す時の変換で起きる（16 を中心に 1 よりわずかに大きい倍率を掛けて切り捨てる形と合う）。firmware の設定で避ける道は資料が無く見つからない。
- 直し: 出力を写す時に、値 1〜14 の sample に 1 を足す（`drv_rpi4_mmal_copy_samples`、8 byte ずつの SWAR）。真の 1 は firmware が 0 にするので 0 のまま（区別できない）。host 試験（`mmal-wire-test` の全値・全 alignment）PASS。

### 遅さ（Video Player の shown 73・late 226）

- T3 の数: 10 秒の 1080p30 の 300 枚が全て decode され（shown＋late＝299）、時計は 21.7 秒 → decode は 1 枚 約 72 ms（13.8 fps）。videoplayer は decode を別の thread で回し、表示が遅れた分の古い絵を late として捨てる。
- vkvideo-probe の decode time（64×64 と 352×288、最初の 1 枚を除く）は 1 枚 約 7.5 ms で大きさによらない → 1 枚あたりの固定の往復（ioctl・executor・MMAL の message）が約 7.5 ms。
- 1080p の 1 枚あたりの CPU の仕事（実装を読んだ見立て、未計測）:
  1. 出力の image（V3D の memory、uncached）から readback の buffer（uncached）への kernel の CPU copy（`vulkan-buffer-copy.c`、8 byte ずつ）: uncached の 3 MB の読み。
  2. libmedia の readback の map（uncached）から自分の frame への memcpy（userland）: uncached の 3 MB の読み、もう 1 回。
  3. decode の待ち（VPU）の間、V3D の executor の thread は塞がり、compositor の V3D の仕事が進まない（design R3）。1 の copy も executor の thread で、controller の mutex を持ったまま走る。
- uncached の読みは load ごとに bus の往復になる。2 回の 3 MB の読みが 72 ms の大半と見る（未計測、下の T で数える）。
- 直し（この版）:
  1. MMAL の bulk の memory（staging・scratch・出力の buffer）を cached の RAM にし（DMA の window を coherent=1、direct map で触る）、手で coherent に保つ: 送る前に clean、受けた後に invalidate、解放の前に invalidate。frame は cached の memory に来る。
  2. frame を image へ写す時（上の補正付き）、cached の frame の位置を「image の cached の写し」として覚え、同じ image の readback（`vkCmdCopyImageToBuffer`）はそこから写す（uncached の読みを無くす）。写しは decoder の次の receive・flush・session の解放の前に忘れる。写しが覆わない範囲と別の image は従来どおり image から読む。
  3. decode を step に分けた: 最初の step で unit（と push）を送り、各 step は window を開けて最大 2 ms だけ frame を探し、無ければ `EINPROGRESS` で step を終える（native job は cursor をその event に置いて返り、他の session の step が走る、`vulkan-native-job.c`）。期限 500 ms は従来どおり。
  4. 計測: decode が続く間、2 秒ごとに 1 行（1 boot で最大 64 行）`v3d: video: N decodes in M ms (… skipped, … pushes, … steps); avg_us send … frame … place …, max_us frame …; readbacks … cached avg_us …, … uncached avg_us …`。send＝unit を送るまで、frame＝送ってから frame が届くまで（VPU の decode・bulk・invalidate）、place＝補正して image へ写す時間、readback＝NV12 の readback（cached の写しから／image から）。
- 残る遅さ（この Phase の外）: libmedia の uncached の map からの memcpy（上の 2）。V3D の memory に HOST_CACHED の型が無いため（WS141）。T の数で大きければ Q1 へ（WS141 の cached の memory 型、または libmedia の読み方、WS202）。

### BUG-314

- submit の ioctl（`runtime_submit`）は `runtime_ready` と `bcm2711_render_worker_submit` で、session の stop・engine の uncertain・not ready・fault の時に EIO を返すが、どちらも log を出さない（実行時の失敗だけが `v3d: command failed` の行を出す）。compositor の EIO（log なし）はこの admission の拒否と合う。
- 直し（判定は不変）: EIO を返す submit の道に理由の 1 行 `v3d: EIO at <site>: <reason> (N more refusals since the last such line)`（1 秒に最大 1 行、間の拒否は数える）。site: `command submit`・`job reserve`・`job commit`・`job capacity`（render-worker.c）、`command`・`synchronous command`・`command execution`（render-runtime.c）、`queue submit`・`queue execution`（vulkan-queue.c）。理由は判定の式を順に言う（`bcm2711_render_refusal_reason`）。

## 変えた file（9c1595a71）

| file | 内容 |
| --- | --- |
| `src/drivers/platform/rpi4/rpi4-h264.c`・`.h` | `drv_rpi4_h264_write_push`（自前の PPS と非参照の灰色の I slice） |
| `src/drivers/platform/rpi4/rpi4-mmal-wire.c`・`.h` | `drv_rpi4_mmal_copy_samples`（1〜14 に 1 を足す写し） |
| `src/drivers/platform/rpi4/rpi4-mmal.c`・`.h` | bulk の memory を cached に、clean・invalidate、解放前の invalidate。image へ直に受ける target と `busy` を削除（出力は常に cached の bounce） |
| `src/drivers/gpu/bcm2711/vulkan-video.c`・`.h` | decode の step 化（`EINPROGRESS`）、push、補正付きの relayout、image の cached の写しと readback、計測の行。古い押し出し（sets・delimiter）を削除 |
| `src/drivers/gpu/bcm2711/vulkan-native-job.c` | video の event の `EINPROGRESS` で step を終える（`#if CONFIG_DRIVER_BCM2711_VIDEO` の中） |
| `src/drivers/gpu/bcm2711/vulkan-buffer-copy.c` | NV12 の readback は先に cached の写しから（`#if` の中）、時間を計測の行へ |
| `src/drivers/gpu/bcm2711/render-worker.c`・`.h`、`render-runtime.c`、`vulkan-queue.c` | BUG-314 の理由の log（`#if` の外、rpi4 の全 build に入る。判定は不変） |
| `plan/ws236/tests/h264-write-test.c`・`run-h264-test.sh`・`mmal-wire-test.c` | push と補正の host 試験 |

## 検証（2026-10-15、host と build）

- `sh plan/ws236/tests/run-h264-test.sh` → `h264: all PASS`・`h264-write: all PASS`・`h264-push: all PASS`。1080p の stream（`pb-high-1080.h264`）も手で同じ確かめ（8 → 9 frame、元の 8 枚は同じ）。
- `sh plan/ws236/tests/run-mmal-wire-test.sh` → `mmal-wire: all PASS`（補正の全値・全 alignment を含む）。`run-vchiq-wire-test.sh` → all PASS。
- build（warning 0、`arm64 vmunix check: PASS`）: `config/ci/config-rpi4.mk`（VCHIQ・VIDEO が y、`build/ws236-p006-ci`）、VCHIQ・VIDEO を n にした CI（`build/ws236-p006-off`）、`plan/ws236/tests/config-rpi4-video-probe.mk`（`build/ws236-p006-probe`）。
- `git diff --check` 問題なし。
- 未実施: 実機（下の T の依頼）。QEMU は VPU が無いので対象外。

## T への依頼（Pi 4 実機 kei@10.0.30.2、QEMU 不要、約 25 分）

1. build: merge 後の main（または agent/p3-ws236 の 9c1595a71 以降）で `make -j16 BUILD=build/<T>/ci ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/<T>/ci/vmunix`（warning 0 を確かめる）。userland は変えていない（`/lib/libmedia.so`・`/home/kei/ws236/vkvideo-probe` と stream・mp4 は T3 の時のまま使う。無ければ T3 の手順で `plan/ws236/tests/config-rpi4-video.mk` から build して複写）。probe の config は使わない（起動が戻らなかった）。
2. 実機: `/boot/boot0/vmunix` を退避して差し替え、sync、reboot、約 1 分で ssh が戻ること。
3. `dmesg | grep -E 'rpi4-(vchiq|mmal)|v3d: (vk|video|EIO)'` の全行を保存（起動直後）。
4. vkvideo-probe（T3 と同じ）: `vkvideo-probe --expect=/home/kei/ws236/<s>.sha256 --time --dump=/tmp/vk-<s>.dump /home/kei/ws236/<s>.h264` を `p-baseline-64`・`i-high-352-cqm`・`pb-high-352-pyramid` で各 1 回。終了 code と全出力。
   - 期待: 3 つとも rc 0、`picture 0 status -1` が無い、全 frame が MATCH。
5. 4 の後の dmesg（3 の grep）を保存。期待: `the first picture is held`・`picture … skipped`・`late frame`・`no push picture` の行が無い。
6. host で dump-compare: `python3 -I plan/ws236/tests/dump-compare.py /tmp/vk-<s>.dump <outdir>` を 3 つ、`summary.txt` の `differing samples` を報告（期待: 全 frame 0）。
7. Video Player（T3 と同じ、`XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 videoplayer --timeout-s=25 /home/kei/ws236/high-1080p30-10s.mp4 2>/tmp/vp-high.log`、同じく `baseline-1080p30-10s.mp4`）。再生中に別の ssh で `top -b -n 3 -d 2`（CPU の使用率）。
   - 報告: `/tmp/vp-*.log` の `MEDIA backend=`・`FRAMES`・`DONE` の行、top の出力、再生の後の dmesg の `v3d: video:` の計測の行の全部と `v3d: EIO` の行。
   - 期待: `backend=vulkan-video`、`PLAYBACK failed` が無い。`FRAMES` の shown が T3（73・77）より増え late が減る。計測の行の `readbacks … cached` が decode の数と同じで `uncached` が 0。
8. 戻し: 終わったら 2 で退避した kernel に戻す（他の試験が CI の kernel を使う時は Q1 の指示に従う）、sync。
- 合否（Q1 判定）: 4 が 3 つとも rc 0（frame 0 と 1〜15 の値の直し）。7 は計測の行の数（send・frame・place・readback）を見て、stream の速さ（33 ms／枚）に届かない分の行き先を判断する（libmedia の uncached の読みが残る見込み）。
- 危険と止め所: decoder の memory を cached にした（cache の保守に漏れがあれば絵が崩れる）。4 の 64×64 の後に Pi が応答しなくなったら、そこで止めて報告する（再起動の要否はユーザーに確かめる）。

## 残り

- T の結果の判定（Q1）。
- libmedia の uncached の map からの読み（WS141 の HOST_CACHED の memory 型か、WS202 の libmedia の読み方）。T の計測の行を見て Q1 へ提案する。
- seek の確かめ（T3 まで未実施）。
- BUG-314 は再発時に dmesg の `v3d: EIO at` の行で理由が分かる（ticket の更新は再発の後）。
- p007（規約の全文の確認）に、この Phase で変えた C を含める。
