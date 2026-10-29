# ws009-p009 の設計の敵対的レビュー（design-reviewer、2026-10-29）

対象: [phase.md](phase.md)（worktree `agent/p5-design`、HEAD 61630bb0b、phase.md は未 commit）。
読んだ物: AGENTS.md、docs/style.md、plan/ws009/ws.md・tests/README.md・tests/*.noct、include/uapi/gpu*.h・ioctl.h・hosted.h・sysctl.h、
src/drivers/gpu/gpu.c・gpu-fence.c、各 driver の ops と capabilities、docs/reference/control-devices.md・evdev.md・visionfive2-gpu.md、
docs/architecture/kernel-and-hal.md、userland/desktop/sessiond/seat.c、userland/desktop/libvulkan/context.c、plan/ws014/tests/gpu-uapi-layout.c。

## 確かめて合っていた事

- 行数: gpu.h 262、gpu-allocation.h 69、gpu-fence.h 107、gpu-display.h 426、gpu-scanout.h 99、gpu-job.h 101、gpu-op.h 2016、gpu.c 8116、gpu-fence.c 626（`wc -l`）。
- ioctl: 'G' 0〜45 の 46 個で抜け・重なりが無い。header ごとの番号の割り振りも phase.md の表どおり（`grep _IO` で全部を数えた）。
- `gpu%u` の名前（gpu.c 1328）、open の権限が root か `euid == inode の uid`（gpu.c 1390〜1406）。
- `drv_gpu_register` を呼ぶ driver は i915・nvrtx・amdrdna・powervr（render と display）・bcm2711（render と display）・venus・ghci の 10 file（`src/drivers/gpu/compiler` は driver ではない）。
- 45 個の `struct gpu_*` の大きさは、host の x86_64 と `-m32`（`-DKERN_UAPI_NATIVE`）で全部同じだった（scratchpad で sizeof を出して diff。ILP32 と LP64 の差は無い）。

## Blocker

### B1. venus の capability の主張が誤り、capability の表の作り方も誤り

- 根拠: phase.md 3 節 9「例 venus は RESOURCE・BLOB を持たない」。実際は `VENUS_CAPABILITIES = (63U | GPU_CAP_DISPLAY | …)`（src/drivers/gpu/venus/venus.c 29）。63 は
  RESOURCE・CAPSET・BLOB・TRANSFER・COMMAND・PRESENT で、venus は RESOURCE・BLOB を持つ（しかも PRESENT を持つのは venus だけ）。
- さらに `GPU_GET_INFO` は backend の `get_info` が書いた `capabilities` を捨て、`device->ops->capabilities` で上書きする（gpu.c 2523）。
  venus の `venus_get_info` は `strict_queue == 0` の時に `GPU_CAP_JOB | GPU_CAP_JOB_CAPACITY` を消す（venus.c 614〜616）が、この消去は user に届かない。
  その時 `GPU_JOB_RESERVE`・`GPU_JOB_CAPACITY` は `ENOTSUP` を返す（venus/transport.c 179・408）。つまり bit があるのに ioctl は動かない。
  libvulkan は bit ではなく capset の `VULKAN_VENDOR_CAPSET_STRICT_QUEUE`（userland/desktop/libvulkan/context.c 31）で判断している。
- 起こる事: 2 節の規則「ops の capabilities の代入と、条件付きで足す所から書く」に従うと、`get_info` の中の条件を「条件付き」と読んで、
  venus の JOB を「strict queue の時だけ」と書く。これは GET_INFO が返す値と食い違う。逆に返す値どおりに書くと、文書は動かない ioctl を約束する。
  venus の「63U」は名前の grep では見つからない。
- 直し方: (1) 9 の例を消すか正しい値にする。(2) 2 節に「GET_INFO の bit は `ops->capabilities`（gpu.c 2523）。backend の `info->capabilities` は使われない」と書く。
  (3) venus の JOB の食い違いは Bug の候補として Q1 に送る（driver の直しはこの Phase の範囲の外）。直るまでの文書の書き方（「bit があっても host が strict queue を持たない時は ENOTSUP」）を決める。

### B2. 受け入れの link の試験が今の tree では通らず、観測もできない

- 根拠: `noct plan/ws009/tests/check-relative-links.noct docs plan` を流すと（main の `build/NoctLang/build-static/noct`、読むだけ）、
  `docs/agent/awesome-plan/README.md: missing relative link target: img/1.png` で止まった（`img/` は git にも無い。main の checkout にも無い）。
  `System.error` で最初の誤りで止まる（check-relative-links.noct の 43 行）ので、それより後の file（新しい gpu.md を含む）の誤りは出力に出ない。
  引数は directory だけを受ける（`walk` が `listDirectory` する、同 50〜68 行）。file だけを指す使い方はできない。
- 起こる事: 5 節の合格「新しい文書と、link を足した文書の全ての相対 link が解決」が、文書の出来と関係なく FAIL になる。範囲を狭めて流した PASS も、全体の PASS とは言えない。
- 直し方: 引数を明記する（例 `docs/reference docs/architecture`）。今の失敗（awesome-plan の README）は既知の失敗として Q1 に送る（docs/agent/ はこの Phase の file ではない）。
  受け入れは「指定の directory で PASS と件数」にする。

### B3. DOC-T10（試験を出典に書く）を満たす道が設計に無い

- 根拠: tests/README.md DOC-T10「Public UAPI statements cite headers, implementations, and executable tests」と、ws.md 3 節の「implementation and test references」。
  docs/style.md「A document in `docs/` never links into `plan/`」。GPU の UAPI の試験の多くは plan/ws014/tests/（WS014 は closed。AGENTS.md 5 節では消す対象）にある。
- 起こる事: 5 節の 4 つの試験はどれも、文書が試験を出典に書いたかを見ない。受け入れに DOC-T10 を挙げているが、確かめる手段が無い。
- 直し方: 出典にする試験を source tree の物に決める。例: `userland/tests/gpu-selftest`・`gpu-admission`・`gpu-recovery`・`gpu-share`・`display-control`・
  `display-scanout`・`acquire-fence`。主張の段ごとにどの試験が裏付けるかを 5 節の「自己の照合」の表の列に加える。
  plan/ws014/tests/gpu-uapi-layout.c を使い続けるなら、WS009 の tests へ移すことを Q1 に頼む（下の M2）。

## Major

### M1. 例の compile の command が host の ioctl の番号を使う

- 根拠: include/uapi/ioctl.h は `__ZEDBSD__` も `KERN_UAPI_NATIVE` も無い時に host の `<sys/ioctl.h>` を使う（ioctl.h 44〜54 行、hosted.h 28〜32 行）。
  `clang -E -I include` で `GPU_GET_INFO` を展開すると、Linux の `_IOWR`（`(2U|1U) << 30 | 'G' << 8 | …`）になった。
  `-DKERN_UAPI_NATIVE` を付けると zedBSD の `KERN_IOC`（`0x80000000UL | 0x40000000UL | …`）になった（scratchpad で確かめた）。
- 起こる事: 5 節の「host の clang で `-fsyntax-only -I include`（host の ioctl の番号は使わない）」は、書いた条件と反対の事をする。
  `-fsyntax-only` はそもそも番号も大きさも確かめない。
- 直し方: target の clang で compile する: `$(ZEDBSD_TARGET_CLANG) --target=x86_64-unknown-zedbsd --sysroot=<build>/amd64/sysroot -fsyntax-only`。
  三つの 64 bit の target（GPU の driver があるのは amd64・aarch64（rpi4）・riscv64（visionfive2）だけ。config/drivers/pci.drivers 8〜12）でも同じ command にする。
  host だけで済ませるなら最低でも `-DKERN_UAPI_NATIVE`。例の中に `_Static_assert(sizeof(struct gpu_info) == 56, …)` のような assert を入れる。

### M2. 照合の script が見るのは名前の一部だけ

- 根拠: 5 節「全ての `GPU_*` の ioctl の macro と `GPU_CAP_*` の bit が文書に現れる」。数えると、ioctl 46 個と CAP 18 個の計 64 個。
  一方 6 本の header の `#define GPU_` は 131 個、`struct gpu_*` は 45 個ある。
- 見つけられない誤り: ioctl の番号・`_IOW` と `_IOWR` の違い・struct の名前の取り違え、flag（`GPU_WAIT_CONSUME`・`GPU_HANDLE_*`・`GPU_BLOB_*`・`GPU_FENCE_*`・
  `GPU_DISPLAY_*` の約 60 個）の抜け、struct の欄と大きさの誤り。
- 逆の向きの検査が誤って FAIL になる事も二つある。(a) gpu-op.h の命令の番号は `#define` ではなく `enum gpu_op` の 159 個の要素（gpu-op.h 176 行〜）。
  `#define` だけを集めると、8 節で `GPU_OP_SET_REPLY_STREAM` などを書いた時に「header に無い」と出る。
  (b) `GPU_TELEMETRY_*` は sysctl.h にある（include/uapi/sysctl.h 186〜205 付近）。
- 直し方: header から（名前・番号・向き・struct）の組を取り、文書の ioctl の表の行と組ごとに比べる。gpu-op.h を除く `#define GPU_` の全部と `struct gpu_*` の全部を対象にする。
  enum の要素も集める。大きさは layout の assert を compile して確かめる。plan/ws014/tests/gpu-uapi-layout.c は 32 個の struct を確かめるが、13 個を確かめない
  （allocation の 3 個、device_info、scanout_constraints、display の power・refresh・cursor の 3 個・plane_state・commit・overlay_info）。直して WS009 の tests に移すか、新しく書く。

### M3. Status の行が既存の検査と DOC-T50 に合わない

- 根拠: 3 節 1「Status 行（`reference`）」。plan/ws009/tests/check-completed-producer-docs.noct 20〜27 行は、docs/ の全 `.md` に
  `Status: current|experimental|deprecated|planned` を求める。DOC-T50 も current・deprecated・planned を区別することを求める。
  既存の user-administration.md・visionfive2-gpu.md は `Status: reference` で、この検査に通らない（今はこの検査が手前の `net wifi set-key` の照合で止まるので、まだ表に出ていない）。
- 起こる事: 新しい文書がもう一つの違反を足す。UAPI の安定の度合い（ws.md 3 節「stability/version status」）も書かれない。
- 直し方: 例 `Status: experimental reference for GPU ABI version 1`。WS306・WS309 がまだ足しうる UAPI なので、current より experimental を勧める。
  style.md の「文書の種類」と producer-docs の検査の両方を満たす。既存の 2 文書と stale な検査は Q1 に報告する。

### M4. ws.md 3 節が求める質が構成から抜けている

- 根拠: ws.md 3 節は、対応する architecture、ownership・lifetime・**concurrency・blocking・cancellation**、**normal example plus timeout/failure example**、implementation と test の参照を求める。
  3 節の構成の 1〜10 にこれらの段が無い。
- source の事実: 同じ fd で並んで動けるかは ioctl ごとに違う。`GPU_JOB_*`・`GPU_COMMAND_WAIT`・`GPU_DISPLAY_EVENTS`・`GPU_DISPLAY_REFRESH`・
  fence の 6 個・`*_SYNC`・`GPU_DISPLAY_COMMIT`・ungated な display の要求は、session の table の admission の外で走る。
  他の要求は `gpu_session_enter` の下で順に走る（gpu.c 1972〜2111）。待ちの上限も ioctl ごとに違う（display の wait・refresh は 1 回の呼び出しで最大 1 秒。gpu-display.h 211〜212・253〜254）。
- 直し方: 「並行と待ち」の節を足す（admission の外の要求の一覧、各 wait の timeout の 0・有限・UINT64_MAX・EINTR）。
  5 節の例は「普通の例」と「timeout か失敗の例」の 2 つを必須にする。対応する architecture（amd64・aarch64・riscv64）と ILP32 の扱いを 1 節に書く。

### M5. 権限の行が書きの権限だけで、読みの権限が抜けている

- 根拠: open は読みの権限も記録する（gpu.c 1426〜1427）。`readable` を見る所が少なくとも 12 か所ある（2264・2339・2435・3211・3489・3610・3754・6163・6470 など）。
  display の書かない要求（query・mode など）は `readable` を要る（gpu.c 3741〜3754）。mmap は prot と両方の権限を照らす（3610〜3615）。
  phase.md の挙げる行も正確でない: 1419 は comment で、`writable` を立てるのは 1422〜1423。2547 と 3204 も comment か、その近く。`writable` を見る所は約 20 か所ある。
- 他の抜け: devfs の既定の mode は 0666（userland/desktop/sessiond/seat.c 60 の `SEAT_GPU_MODE`）。sessiond が seat の user に 0600 で渡すのは `/dev/gpu0`〜`gpu3` だけ（seat.c 47〜48・103〜104）。
  node の番号は空いている一番小さい番号を使い回す（gpu.c 678〜696）。番号は probe の順で決まり、固定ではない。
- 直し方: ioctl ごとの表に「要る open の mode（R・W・不要）」の列と「要る CAP」の列を置く。番号の付き方（使い回し、`GPU_DEVICE_QUERY` の `device_id` で同じ device を見分ける）と、
  gpu4 より後が root だけで開けることを書く。

### M6. 「全 struct の先頭の version・size」は誤り

- 根拠: `gpu_display_plane_state`・`gpu_placement` は先頭に version・size を持たない。`*_SYNC` の 2 個は中に入れた struct の size（内側の大きさ）を照らす（gpu.c 6971・6985）。
  `gpu_blob_create_placed` は外側の size（64）を照らす（gpu.c 3063、gpu-scanout.h 48〜51）。中に入れた descriptor（image・allocation）には別の照合がある（gpu.c 4605・5072）。
- 起こる事: 3 節 2 の規約をそのまま書くと、`*_SYNC` に外側の sizeof を入れた呼び手が `EINVAL` になる。
- 直し方: 規約を「top level の要求の version は `GPU_ABI_VERSION`。size は ioctl ごとに決まる（表に書く）」にし、入れ子の 3 つの形を例外として明記する。

### M7. capset の payload は UAPI の header に無い ABI

- 根拠: どの driver も capset id 4・version 0 だけを受ける（venus.c 735、amdrdna/runtime.c 55・293、nvrtx/runtime.c 42・195、powervr/render-runtime.c 50・185、
  bcm2711/render-runtime.c 29・96、ghci/vk/vk-node.c 38・154）。payload の形（vendor の 168 byte・magic `0x5a424453`、native の 176 byte・magic `0x5a4e4154`、
  flag の OPAQUE・STRICT_QUEUE・QUIESCE・HOST_SCANOUT・VIDEO_DECODE_H264）は libvulkan/context.c 28〜35 と各 driver の source にしか無い。
- 起こる事: 「Versioned object model and supported profiles match headers」（DOC-54 の門）の profile の中心が header の外にある。5 節の照合の script はこれを見られない。
- 直し方: 4 節で capset の id・版・payload を「不透明（command の protocol の一部、consumer は libvulkan）」と書くか、形を書いて driver と libvulkan を出典にするかを決める。
  形を書くなら、header に移す（code の変更、範囲の外）要否を Q1 経由で聞く。

### M8. user に見える副作用が構成に無い

- 最初の flip で text console が framebuffer を使わなくなる。`GPU_PRESENT`・`GPU_DISPLAY_PRESENT`・`*_PRESENT_SYNC`・`GPU_DISPLAY_COMMIT` の最初の成功の後に
  `kern_text_end` を 1 回呼ぶ（gpu.c 1776〜1820）。2026-10-28 のユーザーの決定（ws290/ws.md 208 行）。
- display の claim は text console の keyboard の入力を止め、close と release で戻す（gpu.c 1516〜1520 の `tty_console_input_unhold`、release の側は 3920）。
- poll: 完了は POLLIN、display の event は POLLPRI、device の喪失は POLLERR|POLLHUP、session の失敗は POLLERR（gpu.c 2218〜2280）。fence の fd は PENDING でない時 POLLIN、ERROR の時 POLLERR（gpu-fence.c 545〜570）。
- 回復: 失敗した GPU は、古い参照が全部閉じた後の open で回復しうる（gpu.c 1450〜1455・2248）。
- job の policy の値は build の config から来る（`CONFIG_GPU_JOB_*_MS`、gpu.c 53〜63、config/ci/*.mk）。
- 直し方: 3・6・7 節にこれらを足す。6 節の「session の失敗」は errno（EIO・ENODEV）と poll と再 open の手順まで書く。

### M9. 既存の文書との食い違いを残す

- docs/reference/control-devices.md 81 行「There is no published current GPU object ABI to enumerate here.」。DOC-54 の後は誤りになり、DOC-T50 に反する。
- docs/architecture/kernel-and-hal.md 87〜90 行「From then on the text console still draws into the firmware framebuffer」は、今の gpu.c（M8 の 1 つめ）とユーザーの決定に合わない。
- docs/reference/visionfive2-gpu.md は `/dev/gpu0`・`/dev/gpu1` に触れているが、link を足す先の一覧（1 節）に無い。
- 直し方: 3 つとも同じ change で直す（どれも WS009 の文書）。1 節の成果物に加える。

### M10. 範囲と並行の作業の扱いが足りない

- GPU の user の API は gpu*.h の外にもある: `hw.gpu.attaching`・`hw.gpu.start`・`hw.gputelemetry`（include/uapi/sysctl.h 44〜56・180〜220）、`hw.gpu.powervr.stages`。
  4 節にも 9 節の Q-D54-1 にも出てこない。含めるか外すかを決める。外すなら、文書から sysctl の文書へ link する。
- 並行の作業: WS309（P1 の q1662、in-progress）は「複数の flip を queue に積む kernel の機能」を扱う（ws309/ws.md 48〜50）。WS291（P2 の q1663）は gpu.c を変えている（案 5）。
  4 節は WS306 と WS250 だけを挙げ、「依存: なし」としている。
- WS250 の扱いも正確でない: plan/ws250/design.md 253 行は「UAPI（`include/uapi/gpu*.h`）… を変えない」と書く。「意味の変化」は error と回復の振る舞いの事なので、そう書く。
- 直し方: 着手の時と merge の前に `git log -- include/uapi/gpu*.h src/drivers/gpu/gpu.c` を確かめる手順を入れる。WS309・WS291 を「merge の順の調整が要る」と書く。

### M11. 見積もりの過小

- 読む量は header 約 1,060 行（gpu-op.h を除く）、gpu.c 8,116 行、gpu-fence.c 626 行、driver の 9 種類の node の ops と条件、libvulkan の capset。
  書く量は ioctl 46 個 × （struct・向き・mode・CAP・errno）、struct 45 個、例 2 つ以上、照合の script（組の比較と layout）。
  比べると、header 1 本の evdev.md が 205 行、kernel-boot-parameters.md が 632 行。gpu.md は 700〜1,000 行の見込み（推測）。
- 3 LW（45〜75 分）で gpu.c の errno・権限・並行を読み切るのは難しく、1 LW で script・link・3 target の compile を終えるのも難しい（推測）。
- 直し方: 18〜24 LW に直すか、Phase を分ける。例: (a) 共通の規約・権限・object・投入・fence・job と照合の script、(b) display・scanout・driver の表・既存の文書の直し。
  分けない場合も、節ごとに commit して Q1 に報告する区切りを置く。

### M12. driver の表の単位と中身

- 単位は driver ではなく node の種類にする必要がある。bcm2711 と powervr は render と display の 2 node（visionfive2-gpu.md 10〜16 行も 2 node と書く）。
  i915 の display の bit は 3 つの道で違う（display.c 958 は CONTROL まで、bootfb.c 286・capture.c 219 は DISPLAY と EVENTS だけ）。
  amdrdna・nvrtx は runtime の bind で CAPSET・COMMAND・NOTIFICATION・JOB を足す（amdrdna/runtime.c 192、nvrtx/node.c 161）。ghci は DISP2 と vk-node で足す（ghci.c 652〜671、vk/vk-node.c 129）。
- `GPU_DISPLAY_PRESENT` の返る時は driver で違う。amdrdna は flip の latch を待って返る（ws291/phase006/phase.md 386〜387 の記録。present.c の code は未確認）。
  header はこれを output の flag と wait の振る舞いに任せている（gpu-display.h 218〜219「An output whose presents return only once adopted answers at once」）。
- 直し方: 表を node の種類ごとにし、bit が足される条件の列を置く。8 節の危険の「bit と display の有無だけ」に、present が返る時を区別する display の flag（`GPU_DISPLAY_FIFO`・`VIRTUAL_CLOCK` など）の列を加える。

## Minor

- m1. 8 節の command の stream: gpu-op.h は「Kei GPU command protocol」で `GPU_OP_PROTOCOL_VERSION` は 2（video の命令を足した。gpu-op.h 8〜20・129〜137）。
  「Venus の wire format 1 と同じ番号」は version 1 の番号の事。docs/style.md の互換の語の規則に従い、Venus の版（virglrenderer 1.1.0、venus-protocol の commit ca19b63）を名指す。
- m2. `GPU_JOB_CAPACITY` の WAIT の mode には macro が無い（flags が 0 の時が WAIT。gpu-job.h 19〜20・64〜73）。文書で明記する。
- m3. 4 節の amdrdna.h の引用は 16〜17 行にまたがる（行が途中で折れている）。引用の形を正す。
- m4. Q-D54-2（言語）は聞かなくてよい。docs/ の全文書が英語なので既定に従い、ユーザーへの問いを減らす。
- m5. 記録の整合（AGENTS.md 8 節）: ws.md の Phase registry に p009 の行が無い。DOC-54 の行は「Producer manual hold」のまま、Resume point も古い。
  phase.md を commit する時に、同じ commit で ws.md の写しを直す（P5 の範囲の plan/ws009/ か Q1）。
- m6. 2 節の gpu.c の行は、WS291 の gpu.c の変更で動く。照合の表には行に加えて関数の名前（`gpu_open`・`gpu_ioctl_body`・`gpu_info_ioctl`）を書く。
- m7. fence の ioctl（16〜21）が `GPU_CAP_FENCE` を要るかは確かめていない（gpu.c 4321 の commit の fence の照合だけ見た）。CAP の列を書く時に gpu_fence_ioctl を読む。
- m8. plan/ws014/tests/ は closed の WS の試験。AGENTS.md 5 節では消す対象で、tools.md にも登録が無い。B3・M2 で使うなら、移すか登録するかを Q1 に頼む。

## 推測と未確認

- amdrdna の present が latch を待つ事は、記録（ws291 p006）の引用で、code は読んでいない。
- runtime の bind（amdrdna・nvrtx・powervr・bcm2711）が `drv_gpu_register` の前か後かは確かめていない。後なら、GET_INFO の bit が node の公開の後に増え、
  register の時の照合（gpu.c 1029〜1302）も通らない。文書に「bit は node の寿命の中で一定か」を書く前に確かめる。
- check-completed-producer-docs.noct が `Status: reference` で FAIL する事は、script の中身（20〜27 行）から読んだ。実際の実行はその手前の照合で止まり、status の段まで進まなかった。
- 見積もりの行数（700〜1,000 行）は既存の文書からの推測。
